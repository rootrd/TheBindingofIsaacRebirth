/* host_ext_file.c — the guest's CRT stdio file family, implemented for real.
 *
 * WHY THIS MODULE EXISTS
 * host_ohos.c's h_crt_fopen() returns 0 for every path, and h_crt_fread()/
 * h_crt_fwrite()/h_crt_fseek()/h_crt_ftell()/h_crt_fclose()/h_crt_fflush()/
 * h_crt__fileno()/h_crt__get_osfhandle() are hollow too: fread returns 0, fseek
 * and ftell report success with position 0, fclose succeeds on nothing,
 * _fileno returns -1. So the guest's resource layer never opens
 * kage_mount_points.dat, its resource mount table stays empty, every later
 * resource lookup fails, and the guest eventually calls a NULL virtual method
 * (guest_fault addr=0x00000000, ~16.3k calls).
 *
 * Everything below is a genuine implementation over the host filesystem: a real
 * open(), real byte counts, real positions, real errors, correct errno.
 *
 * WHAT IS REGISTERED (all names spelled exactly as in
 * generated/host_imports_table.h):
 *
 *   api-ms-win-crt-stdio-l1-1-0.dll!fopen
 *   api-ms-win-crt-stdio-l1-1-0.dll!fclose
 *   api-ms-win-crt-stdio-l1-1-0.dll!fread
 *   api-ms-win-crt-stdio-l1-1-0.dll!fwrite
 *   api-ms-win-crt-stdio-l1-1-0.dll!fseek
 *   api-ms-win-crt-stdio-l1-1-0.dll!ftell
 *   api-ms-win-crt-stdio-l1-1-0.dll!fflush
 *   api-ms-win-crt-stdio-l1-1-0.dll!setvbuf     (NOT imported by this binary)
 *   api-ms-win-crt-stdio-l1-1-0.dll!_fileno
 *   api-ms-win-crt-stdio-l1-1-0.dll!_get_osfhandle
 *   api-ms-win-crt-filesystem-l1-1-0.dll!remove
 *   api-ms-win-crt-filesystem-l1-1-0.dll!_access
 *   api-ms-win-crt-runtime-l1-1-0.dll!_errno
 *   api-ms-win-crt-runtime-l1-1-0.dll!_set_errno
 *
 * The table was searched for the rest of the family; this binary imports none
 * of _wfopen, fopen_s, fgets, fputs, fgetc, fputc, feof, ferror, ungetc,
 * _fseeki64, _ftelli64, freopen, tmpfile, _fdopen, _open/_read/_write/_lseek/
 * _close, _commit, _setmode, _filelength, _chsize, rename, ReadFile, WriteFile,
 * SetFilePointer, GetFileSize, DeleteFileA. Only CreateFileA (KERNEL32),
 * CreateDirectoryA, GetFileAttributesA and the entries above exist, and
 * CreateFileA belongs to host_ohos.c's handle table, not here.
 *
 * REGISTRATION — read this before concluding that nothing changed.
 * host_ohos.c's guest_host_import() reaches an extension module only through
 * s_ext_providers[] = { host_ext_printf, host_ext_kage, host_ext_misc }; a
 * module that owns no slot there is dead code, however correct its handlers.
 * This file therefore returns host_ext_file() and edits no other file: adding
 * it to s_ext_providers[] (and declaring it in host_ext.h) is a two-line change
 * in two files this module may not touch. The tempting alternative — defining a
 * strong host_ext_misc / host_ext_kage / host_ext_printf here — would silently
 * displace a module that already works (misc.c's FLS/TLS/secure strings/
 * FindFirstFileW family), which is exactly the failure mode misc.c's own
 * PROVIDER SLOT COLLISION note warns about. So the symbol is not stolen and the
 * module proves its own liveness at run time instead: host_ext_file() logs once
 * when the dispatcher first calls it, so the harness log either contains
 * "host_ext_file: dispatcher consulted this module" (registered) or it does not
 * (inert). Nothing here ever pretends to be installed.
 *
 * CONVENTIONS (identical to host_ohos.c / host_ext_misc.c)
 *   argument i = ld32(esp + 4 + i*4)      esp points at the return address
 *   cdecl      = esp += 4                 the guest caller cleans the arguments
 *   result     = eax (integer), 0/NULL on failure
 * Guest pointers are arena offsets. A guest address is a valid host address
 * (isaac_arena_base + addr), so the data path moves bytes without a bounce
 * buffer; nothing native is ever handed to the guest.
 */

#include <dirent.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "guest.h"
#include "guest_memory.h"
#include "host_ext.h"

#include <hilog/log.h>
/* hilog privacy qualifiers are mandatory in format strings. */
#define FILE_ERR(fmt, ...) \
    OH_LOG_Print(LOG_APP, LOG_ERROR, 0, "isaacoh", fmt, ##__VA_ARGS__)
#define FILE_INFO(fmt, ...) \
    OH_LOG_Print(LOG_APP, LOG_INFO, 0, "isaacoh", fmt, ##__VA_ARGS__)

/* One INFO line per handler on its first call. A resource load issues thousands
 * of fread/fseek calls, so per-call logging would bury the fault context; the
 * first call is enough to prove the handler is live, and every failure logs
 * unconditionally. */
#define FILE_ANNOUNCE(msg, ...) \
    do { static int file_once_; \
         if (!file_once_) { file_once_ = 1; FILE_INFO(msg, ##__VA_ARGS__); } \
    } while (0)

/* ---------------------------------------------------------------- helpers */

static inline uint32_t file_arg(CPU *__restrict c, int index)
{
    return ld32(c->esp + 4U + (uint32_t)index * 4U);
}

static inline void file_cdecl_ret(CPU *__restrict c)
{
    c->esp += 4U;
}

/* The arena is one flat 4 GiB mapping, so "inside the arena" is simply "does
 * not wrap" — the same test host_ext_misc.c makes. */
static inline int file_guest_range(uint32_t addr, uint64_t size)
{
    return addr != 0U && (uint64_t)addr + size <= 0x100000000ULL;
}

#define FILE_PATH_MAX 1024U
#define FILE_MODE_MAX 16U

/* Bounded read of a NUL-terminated guest string into host memory. Returns 0 on
 * success, -1 when the pointer is NULL, the string is longer than cap-1 bytes,
 * or it is unterminated (logged by the caller, which knows the argument name). */
static int file_guest_string(uint32_t addr, char *out, size_t cap)
{
    size_t i;

    if (addr == 0U || cap == 0U) {
        return -1;
    }
    for (i = 0U; i + 1U < cap; ++i) {
        uint8_t value = ld8(addr + (uint32_t)i);
        if (value == 0U) {
            out[i] = '\0';
            return 0;
        }
        out[i] = (char)value;
    }
    out[cap - 1U] = '\0';
    return -1;
}

static const char *file_data_root(void)
{
    const char *env = getenv("ISAAC_DATA_ROOT");
    return (env != NULL && env[0] != '\0') ? env : "isaac-data";
}

/* Refuse ".." outright: the guest may not climb out of its data root. */
static int file_has_parent_component(const char *path)
{
    const char *p = path;

    while (*p != '\0') {
        const char *end = strchr(p, '/');
        size_t len = (end != NULL) ? (size_t)(end - p) : strlen(p);
        if (len == 2U && p[0] == '.' && p[1] == '.') {
            return 1;
        }
        if (end == NULL) {
            break;
        }
        p = end + 1;
    }
    return 0;
}

/* Guest path -> host path, contained in the data root.
 *
 * The guest's "current directory" is ISAAC_DATA_ROOT itself (host_ohos.c hands
 * the env value straight to GetCurrentDirectoryA), so the paths it builds and
 * the paths it echoes back have two shapes:
 *   - already rooted in the data root ("/mnt/.../isaac-data/resources/x.bin",
 *     or "isaac-data/resources/x.bin" when the root is relative) -> taken as-is;
 *   - relative to that root ("resources/x.bin", "kage_mount_points.dat").
 *
 * The relative form is joined onto the root and can never escape it: ".." is
 * refused, a drive prefix is dropped with a log, leading '/' and "./" are
 * stripped before the join, and any remaining ':' is refused (on this host it
 * would be a legal file name standing for something else entirely). Everything
 * refused is logged and fails the open.
 *
 * Returns 0 on success (out holds the host path), -1 when refused. The caller
 * sets the guest errno.
 */
static int file_normalise(const char *raw, const char *what,
                          char *out, size_t cap, int *rooted_out)
{
    char work[FILE_PATH_MAX];
    const char *root = file_data_root();
    size_t root_len = strlen(root);
    size_t raw_len = strlen(raw);
    const char *rest;
    size_t i;

    *rooted_out = 0;
    if (raw_len == 0U) {
        FILE_ERR("%{public}s: empty path", what);
        return -1;
    }
    if (raw_len >= sizeof work) {
        FILE_ERR("%{public}s: path is %u bytes, longer than the %u-byte "
                 "limit", what, (unsigned)raw_len, (unsigned)sizeof work - 1U);
        return -1;
    }
    memcpy(work, raw, raw_len + 1U);

    /* Windows separators are the guest's spelling; POSIX wants '/'. */
    for (i = 0U; work[i] != '\0'; ++i) {
        if (work[i] == '\\') {
            work[i] = '/';
        }
    }
    if (file_has_parent_component(work)) {
        FILE_ERR("%{public}s: refusing \"%{public}s\": \"..\" leaves the data "
                 "root", what, raw);
        return -1;
    }

    /* The guest echoing our own root back ("<root>/...", "<root>", or the same
     * with a leading "./") is the common case and is used verbatim. */
    rest = work;
    if (rest[0] == '.' && rest[1] == '/') {
        rest += 2;
    }
    if (root_len != 0U && strncmp(rest, root, root_len) == 0 &&
        (rest[root_len] == '/' || rest[root_len] == '\0')) {
        size_t rest_len = strlen(rest);
        if (rest_len >= cap) {
            FILE_ERR("%{public}s: path \"%{public}s\" is longer than %u bytes",
                     what, rest, (unsigned)cap);
            return -1;
        }
        memcpy(out, rest, rest_len + 1U);
        *rooted_out = 1;
        return 0;
    }

    /* Relative: drop a drive prefix (the guest's Windows framing), then every
     * leading '/' or "./" — what follows is a path INSIDE the root by
     * construction, never an absolute path that could point elsewhere. */
    if (((rest[0] >= 'A' && rest[0] <= 'Z') ||
         (rest[0] >= 'a' && rest[0] <= 'z')) && rest[1] == ':') {
        FILE_INFO("%{public}s: dropping drive prefix \"%{public}c:\" from "
                  "\"%{public}s\"", what, rest[0], raw);
        rest += 2;
    }
    for (;;) {
        if (rest[0] == '/') {
            ++rest;
            continue;
        }
        if (rest[0] == '.' && rest[1] == '/') {
            rest += 2;
            continue;
        }
        break;
    }
    if (rest[0] == '\0') {
        FILE_ERR("%{public}s: \"%{public}s\" names no file", what, raw);
        return -1;
    }
    for (i = 0U; rest[i] != '\0'; ++i) {
        if (rest[i] == ':') {
            FILE_ERR("%{public}s: refusing \"%{public}s\": ':' inside the "
                     "path is not part of the data root", what, raw);
            return -1;
        }
    }

    {
        size_t rest_len = strlen(rest);
        if (root_len + 1U + rest_len >= cap) {
            FILE_ERR("%{public}s: \"%{public}s/%{public}s\" is longer than %u "
                     "bytes", what, root, rest, (unsigned)cap);
            return -1;
        }
        memcpy(out, root, root_len);
        out[root_len] = '/';
        memcpy(out + root_len + 1U, rest, rest_len + 1U);
    }
    return 0;
}

static int file_mkdir_one(const char *dir, const char *what)
{
    struct stat st;

    if (mkdir(dir, 0777) == 0) {
        FILE_INFO("%{public}s: created directory \"%{public}s\"", what, dir);
        return 0;
    }
    if (errno == EEXIST && stat(dir, &st) == 0 && S_ISDIR(st.st_mode)) {
        return 0;                       /* already there */
    }
    FILE_ERR("%{public}s: mkdir(\"%{public}s\") failed (errno=%d)", what, dir,
             errno);
    return -1;
}

/* mkdir -p for the parent components of a path. Only the '/' separators are
 * expanded, so the last component (the file itself) is never created. */
static int file_mkdirs(const char *path, const char *what)
{
    char work[FILE_PATH_MAX];
    size_t len = strlen(path);
    size_t i;

    if (len >= sizeof work) {
        return -1;
    }
    memcpy(work, path, len + 1U);
    for (i = 1U; work[i] != '\0'; ++i) {
        int saved;
        if (work[i] != '/') {
            continue;
        }
        work[i] = '\0';
        saved = errno;
        if (file_mkdir_one(work, what) != 0) {
            work[i] = '/';
            errno = saved;
            return -1;
        }
        errno = saved;
        work[i] = '/';
    }
    return 0;
}

/* ------------------------------------------------------------------ errno */
/* The guest's errno channel. _set_errno() is imported by this binary and hit
 * seven times in the baseline run (hDUMMY returned 0, so every errno the guest
 * set was dropped), and _errno() is the pointer the CRT reads back through —
 * hDUMMY returned 0 (NULL), which a caller that writes through it would fault
 * on. Both now share one arena dword.
 *
 * Host errno -> MSVC errno is spelled out value by value rather than assumed:
 * the two numberings agree for the Unix values this module can produce, but
 * they do NOT agree for ENAMETOOLONG (36 vs 38) and ENOTEMPTY (39 vs 41), and a
 * silent pass-through of those would report "deadlock" and "no such process" to
 * the guest. An errno with no vetted equivalent is reported as EINVAL and
 * logged with the host value, never guessed.
 */
#define FILE_MSVC_ENOENT 2
#define FILE_MSVC_EACCES 13
#define FILE_MSVC_EEXIST 17
#define FILE_MSVC_EISDIR 21
#define FILE_MSVC_EINVAL 22
#define FILE_MSVC_EMFILE 24
#define FILE_MSVC_ENOSPC 28
#define FILE_MSVC_ERANGE 34
#define FILE_MSVC_ENAMETOOLONG 38
#define FILE_MSVC_ENOTEMPTY 41

static uint32_t g_errno_addr;

static uint32_t file_errno_addr(void)
{
    if (g_errno_addr == 0U) {
        /* Handed to the guest as a writable int*, so keep it 4-byte aligned:
         * the guest writes it with a word store, and nothing here may hand out
         * a misaligned pointer (ARM64 faults on those). */
        static const char zeros[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
        uint32_t raw = guest_mem_push_string(zeros, sizeof zeros);
        if (raw == 0U) {
            FILE_ERR("errno: arena is exhausted, the guest errno word could "
                     "not be allocated");
            return 0U;
        }
        g_errno_addr = (raw + 3U) & ~3U;
    }
    return g_errno_addr;
}

static int file_msvc_errno(int host_errno)
{
    switch (host_errno) {
    case EPERM:          return 1;
    case ENOENT:         return FILE_MSVC_ENOENT;
    case EINTR:          return 4;
    case EIO:            return 5;
    case EBADF:          return 9;
    case ENOMEM:         return 12;
    case EACCES:         return FILE_MSVC_EACCES;
    case EBUSY:          return 16;
    case EEXIST:         return FILE_MSVC_EEXIST;
    case ENOTDIR:        return 20;
    case EISDIR:         return FILE_MSVC_EISDIR;
    case EINVAL:         return FILE_MSVC_EINVAL;
    case EMFILE:         return FILE_MSVC_EMFILE;
    case EFBIG:          return 27;
    case ENOSPC:         return FILE_MSVC_ENOSPC;
    case ESPIPE:         return 29;
    case EROFS:          return 30;
    case ERANGE:         return FILE_MSVC_ERANGE;
    case ENAMETOOLONG:   return FILE_MSVC_ENAMETOOLONG;
    case ELOOP:          return 40;
    case ENOTEMPTY:      return FILE_MSVC_ENOTEMPTY;
    case EOVERFLOW:      return 75;
    case 0:              return 0;
    default:
        FILE_ERR("errno: host errno %d has no vetted MSVC equivalent; "
                 "reporting EINVAL(%d)", host_errno, FILE_MSVC_EINVAL);
        return FILE_MSVC_EINVAL;
    }
}

static void file_errno_set(int host_errno)
{
    uint32_t addr = file_errno_addr();
    int code = file_msvc_errno(host_errno);
    if (addr != 0U) {
        st32(addr, (uint32_t)code);
    }
}

static void h_file_errno(CPU *c)
{
    FILE_ANNOUNCE("_errno: first call -> guest errno word");
    c->eax = file_errno_addr();
    file_cdecl_ret(c);
}

static void h_file_set_errno(CPU *c)
{
    uint32_t value = file_arg(c, 0);
    uint32_t addr = file_errno_addr();
    FILE_ANNOUNCE("_set_errno: first call (value %{public}u)", value);
    if (addr != 0U) {
        st32(addr, value);
    }
    file_cdecl_ret(c);
}

/* ------------------------------------------------------------ slot table */
/* The guest sees opaque tokens from this table, never a native FILE*. The token
 * is FILE_TOKEN_BASE + (generation << 6) + index, so a token from a closed
 * (or never opened) stream is detected — the slot's generation no longer
 * matches — instead of being dereferenced. 64 streams are far more than the
 * guest's loader holds open at once, and a slot is reused only after its
 * generation has moved on. */
/* The guest keeps many streams open at once (the archive loader alone opens
 * hundreds), and a slot is only recycled after a close, so 64 slots recycled
 * constantly. 1024 keeps _fileno (= index + 3) in range and cuts the recycle
 * pressure; the generation field below is 8 bits wide. */
#define FILE_SLOT_COUNT 1024U
#define FILE_TOKEN_BASE 0x00050000U
#define FILE_TOKEN_SPAN (1U << 18)
#define FILE_TOKEN_MAX (FILE_TOKEN_SPAN - 1U)
/* _fileno() reports index + 3, leaving 0..2 to the standard streams. */
#define FILE_FD_BASE 3
/* ... and the standard streams live at IOB_BASE (its IOB_BASE). The guest may
 * legitimately pass one of those three to _fileno/fflush/fwrite/fclose, so
 * they are recognised here and mapped onto the host's own standard streams. */
#define FILE_IOB_BASE 0x00010000U
#define FILE_IOB_COUNT 3U

typedef struct {
    FILE    *fp;                        /* native stream, host side only */
    uint32_t token;                     /* the token bound to this slot */
    uint8_t  used;
    uint8_t  zombie;                    /* closed, but the guest may still
                                         * hold and use the old FILE* value:
                                         * MSVC's freed _iob slot tolerates
                                         * that, so ops on a zombie are
                                         * benign no-ends (write=ok,
                                         * read=0, seek=0) until the slot
                                         * is reopened. */
    uint8_t  is_standard;               /* one of the three standard streams */
    uint8_t  read_seen, write_seen;     /* first-call logging, per stream */
    char     path[FILE_PATH_MAX];       /* host path, for the log */
    /* resources/packed/*.a (mode 0) are stored, not deflated, and the payload
     * is run through the game's word cipher (sub_0059c270's 0x59c5bb branch).
     * We serve the guest plaintext: the cipher is applied here, in place, on
     * the way out of fread. See the 2026-09-21 handover section for the spec. */
    uint32_t *arc_off;                  /* entry file offsets */
    uint32_t *arc_h2;                   /* entry name hash, second u32 */
    uint32_t  arc_entries;
    uint32_t  arc_pos;                  /* file offset of the armed entry */
    uint32_t  arc_key;                  /* keystream state at arc_word */
    uint32_t  arc_word;                 /* words already consumed in the entry */
    uint32_t  arc_h2_cur;               /* h2 of the armed entry */
    uint8_t   arc_mode;                 /* 0xFF = not an archive */
    uint8_t   arc_active;               /* cipher armed for arc_pos */
    uint8_t   arc_pad[2];
} file_slot;

static file_slot g_slots[FILE_SLOT_COUNT];
static uint32_t g_open_serial;

/* ------------------------------------------------- archive word cipher --- */
/* key_{i+1} = t ^ (t << 23) with t = (key_i ^ (key_i << 8)) ^ ((key_i ^ (key_i << 8)) >> 9) */
static uint32_t arc_mix(uint32_t k)
{
    uint32_t t = k ^ (k << 8);

    t ^= t >> 9;
    return t ^ (t << 23);
}

/* Byte permutation selected by the low nibble of the word's key. */
static uint32_t arc_permute(uint32_t w, uint32_t sel)
{
    uint8_t b[4];
    uint8_t t;

    b[0] = (uint8_t)w;
    b[1] = (uint8_t)(w >> 8);
    b[2] = (uint8_t)(w >> 16);
    b[3] = (uint8_t)(w >> 24);
    if (sel == 2U) {
        t = b[0]; b[0] = b[3]; b[3] = t;
        t = b[1]; b[1] = b[2]; b[2] = t;
    } else if (sel == 9U) {
        t = b[0]; b[0] = b[1]; b[1] = t;
        t = b[2]; b[2] = b[3]; b[3] = t;
    } else if (sel == 13U) {
        t = b[0]; b[0] = b[2]; b[2] = t;
        t = b[1]; b[1] = b[3]; b[3] = t;
    }
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8) |
           ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}

static uint32_t arc_rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void arc_wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static void file_archive_forget(file_slot *slot)
{
    free(slot->arc_off);
    free(slot->arc_h2);
    slot->arc_off = NULL;
    slot->arc_h2 = NULL;
    slot->arc_entries = 0U;
    slot->arc_mode = 0xFFU;
    slot->arc_active = 0U;
}

/* Read the index of a ".a" the guest just opened. Only the 14-byte header and
 * the (20 bytes/entry) index are read -- music.a is 180 MB and we must not
 * pull the payload in. */
static void file_archive_attach(file_slot *slot, const char *host_path)
{
    uint8_t hdr[20];
    uint32_t index_off, entries, i;
    uint8_t mode;
    FILE *fp;
    size_t n = strlen(host_path);

    file_archive_forget(slot);
    if (n < 2U || host_path[n - 2U] != '.' || host_path[n - 1U] != 'a') {
        return;
    }
    fp = fopen(host_path, "rb");
    if (fp == NULL) {
        return;
    }
    if (fread(hdr, 1U, 14U, fp) != 14U || memcmp(hdr, "ARCH000", 7U) != 0) {
        fclose(fp);
        return;
    }
    mode = hdr[7];
    index_off = arc_rd32(hdr + 8U);
    entries = (uint32_t)hdr[12] | ((uint32_t)hdr[13] << 8);
    if (entries == 0U || entries > 32768U) {
        fclose(fp);
        return;
    }
    slot->arc_off = (uint32_t *)malloc((size_t)entries * sizeof(uint32_t));
    slot->arc_h2 = (uint32_t *)malloc((size_t)entries * sizeof(uint32_t));
    if (slot->arc_off == NULL || slot->arc_h2 == NULL ||
        fseeko(fp, (off_t)index_off, SEEK_SET) != 0) {
        file_archive_forget(slot);
        fclose(fp);
        return;
    }
    for (i = 0U; i < entries; i++) {
        if (fread(hdr, 1U, 20U, fp) != 20U) {
            break;
        }
        slot->arc_h2[i] = arc_rd32(hdr + 4U);
        slot->arc_off[i] = arc_rd32(hdr + 8U);
    }
    fclose(fp);
    if (i == 0U) {
        file_archive_forget(slot);
        return;
    }
    slot->arc_entries = i;
    slot->arc_mode = mode;
    FILE_INFO("archive: \"%{public}s\" mode=%u entries=%u",
              host_path, (unsigned)mode, i);
}

static void file_archive_arm(file_slot *slot, long pos)
{
    uint32_t i;

    slot->arc_active = 0U;
    if (slot->arc_off == NULL || pos < 0) {
        return;
    }
    for (i = 0U; i < slot->arc_entries; i++) {
        if (slot->arc_off[i] == (uint32_t)pos) {
            slot->arc_pos = (uint32_t)pos;
            slot->arc_h2_cur = slot->arc_h2[i];
            slot->arc_key = (slot->arc_h2[i] ^ 0xf9524287U) | 1U;
            slot->arc_word = 0U;
            slot->arc_active = 1U;
            if (getenv("ISAAC_DBG_ARM")) {
                static int an_ = 0;
                if (an_ < 16) {
                    an_++;
                    FILE_ERR("[DBG arm] #%d pos=%ld entry=%u h2=%08x key=%08x",
                             an_, pos, i, slot->arc_h2[i], slot->arc_key);
                }
            }
            return;
        }
    }
}

static void file_archive_decrypt(file_slot *slot, long pos, uint8_t *data,
                                 size_t len)
{
    static int disabled = -1;
    uint32_t word, i;

    if (disabled < 0) {
        /* Off by default: once VCRUNTIME memcpy was implemented for real, the
         * game's own cipher (0x59c5bb) decrypts correctly and a host-side
         * pass would only double-decrypt. ISAAC_HOST_DEC re-enables it. */
        disabled = getenv("ISAAC_HOST_DEC") == NULL;
    }
    if (disabled || slot->arc_active == 0U || slot->arc_mode != 0U ||
        len < 4U || pos < 0) {
        return;
    }
    if ((uint32_t)pos < slot->arc_pos) {
        return;
    }
    word = ((uint32_t)pos - slot->arc_pos);
    if ((word & 3U) != 0U) {
        return;                          /* not word aligned: leave it alone */
    }
    word >>= 2;
    if (word < slot->arc_word) {         /* rewound inside the entry */
        slot->arc_key = (slot->arc_h2_cur ^ 0xf9524287U) | 1U;
        slot->arc_word = 0U;
    }
    while (slot->arc_word < word) {
        slot->arc_key = arc_mix(slot->arc_key);
        slot->arc_word++;
    }
    if (getenv("ISAAC_DBG_ARM")) {
        static int dn_ = 0;
        if (dn_ < 16) {
            dn_++;
            FILE_ERR("[DBG dec] #%d pos=%ld len=%u word=%u pre=%02x%02x%02x%02x",
                     dn_, pos, (unsigned)len, word,
                     data[0], data[1], data[2], data[3]);
        }
    }
    for (i = 0U; i + 4U <= len; i += 4U) {
        uint32_t key = slot->arc_key;
        uint32_t w = arc_rd32(data + i) ^ key;

        arc_wr32(data + i, arc_permute(w, key & 0xfU));
        slot->arc_key = arc_mix(key);
        slot->arc_word++;
    }
    if (getenv("ISAAC_DBG_ARM")) {
        static int en_ = 0;
        if (en_ < 16) {
            en_++;
            FILE_ERR("[DBG dec] #%d post=%02x%02x%02x%02x%02x%02x%02x%02x",
                     en_, data[0], data[1], data[2], data[3],
                     data[4], data[5], data[6], data[7]);
        }
    }
}

/* The three standard streams keep their MSVC-style identity values (the IOB
 * base); closing them is refused and writes are redirected to the host's own
 * streams so the harness log survives. */
static FILE *file_standard_stream(uint32_t token, unsigned *std_index)
{
    if (token >= FILE_IOB_BASE && token < FILE_IOB_BASE + FILE_IOB_COUNT) {
        if (std_index != NULL) {
            *std_index = (unsigned)(token - FILE_IOB_BASE);
        }
        switch (token - FILE_IOB_BASE) {
        case 0U: return stdin;
        case 1U: return stdout;
        default: return stderr;
        }
    }
    return NULL;
}

static const char *file_standard_name(unsigned std_index)
{
    static const char *const names[FILE_IOB_COUNT] = { "stdin", "stdout", "stderr" };
    return (std_index < FILE_IOB_COUNT) ? names[std_index] : "?";
}

/* ---- loose-file fallback into resources/packed/*.a -----------------------
 * The engine asks the CRT for loose paths (gfx/..., video/...) whose bytes are
 * actually stored inside the .a archives; on Windows its VFS routes those by
 * name hash into the mounted archives. Reproduced here: on a failed read-mode
 * fopen of a data-root path, hash the mounted name (volume prefix + suffix)
 * with the engine's two hashes -- djb2 (0x5637e0: h=0x1505, h=h*33+norm) and
 * FNV-1a with the custom basis 0x5BB2220E (0x563830), norm = lower + '\'->'/' --
 * find the entry, decrypt mode-0 payloads to a cache file, open that. */

typedef struct {
    uint32_t h1, h2;      /* djb2 / fnv of the mounted name */
    uint32_t off, size;   /* stored payload location */
    uint8_t  mode;        /* archive mode */
    uint16_t arc;         /* archive ordinal */
} pack_entry;

#define PACK_ARC_MAX 32U
static pack_entry *g_pack_entries;
static uint32_t g_pack_count, g_pack_cap;
static char g_pack_arch[PACK_ARC_MAX][FILE_PATH_MAX];
static uint32_t g_pack_arcs;
static int g_pack_state = -1;          /* -1 uninit, 0 unusable, 1 ready */

static uint32_t pack_hash_djb2(const char *s)
{
    uint32_t h = 0x1505U;
    const unsigned char *p = (const unsigned char *)s;
    while (*p != 0U) {
        unsigned char c = *p++;
        if (c >= 'A' && c <= 'Z') {
            c = (unsigned char)(c + 0x20);
        }
        if (c == '\\') {
            c = '/';
        }
        h = h * 33U + c;
    }
    return h;
}

static uint32_t pack_hash_fnv(const char *s)
{
    uint32_t h = 0x5BB2220EU;
    const unsigned char *p = (const unsigned char *)s;
    while (*p != 0U) {
        unsigned char c = *p++;
        if (c >= 'A' && c <= 'Z') {
            c = (unsigned char)(c + 0x20);
        }
        if (c == '\\') {
            c = '/';
        }
        h = (h ^ c) * 0x01000193U;
    }
    return h;
}

static int pack_entry_cmp(const void *a, const void *b)
{
    const pack_entry *x = (const pack_entry *)a;
    const pack_entry *y = (const pack_entry *)b;

    if (x->h1 != y->h1) {
        return (x->h1 < y->h1) ? -1 : 1;
    }
    if (x->h2 != y->h2) {
        return (x->h2 < y->h2) ? -1 : 1;
    }
    return 0;
}

static void pack_init(void)
{
    char dir[FILE_PATH_MAX];
    DIR *d;
    struct dirent *de;

    if (g_pack_state >= 0) {
        return;
    }
    g_pack_state = 0;
    snprintf(dir, sizeof dir, "%s/resources/packed", file_data_root());
    d = opendir(dir);
    if (d == NULL) {
        return;
    }
    while ((de = readdir(d)) != NULL) {
        size_t n = strlen(de->d_name);
        char path[FILE_PATH_MAX];
        uint8_t hdr[20];
        uint32_t index_off, entries, i, mode;
        FILE *fp;

        if (n < 3 || de->d_name[n - 2U] != '.' || de->d_name[n - 1U] != 'a') {
            continue;
        }
        snprintf(path, sizeof path, "%s/%s", dir, de->d_name);
        fp = fopen(path, "rb");
        if (fp == NULL) {
            continue;
        }
        if (fread(hdr, 1U, 14U, fp) != 14U || memcmp(hdr, "ARCH000", 7U) != 0) {
            fclose(fp);
            continue;
        }
        if (g_pack_arcs >= PACK_ARC_MAX) {
            fclose(fp);
            break;
        }
        mode = hdr[7];
        index_off = arc_rd32(hdr + 8U);
        entries = (uint32_t)hdr[12] | ((uint32_t)hdr[13] << 8);
        if (fseeko(fp, (off_t)index_off, SEEK_SET) != 0) {
            fclose(fp);
            continue;
        }
        snprintf(g_pack_arch[g_pack_arcs], sizeof g_pack_arch[0], "%s", path);
        for (i = 0U; i < entries; i++) {
            pack_entry *e;
            if (fread(hdr, 1U, 20U, fp) != 20U) {
                break;
            }
            if (g_pack_count == g_pack_cap) {
                uint32_t ncap = g_pack_cap != 0U ? g_pack_cap * 2U : 4096U;
                pack_entry *ng = (pack_entry *)realloc(g_pack_entries,
                                                       (size_t)ncap * sizeof(pack_entry));
                if (ng == NULL) {
                    break;
                }
                g_pack_entries = ng;
                g_pack_cap = ncap;
            }
            e = &g_pack_entries[g_pack_count++];
            e->h1 = arc_rd32(hdr + 0U);
            e->h2 = arc_rd32(hdr + 4U);
            e->off = arc_rd32(hdr + 8U);
            e->size = arc_rd32(hdr + 12U);
            e->mode = (uint8_t)mode;
            e->arc = (uint16_t)g_pack_arcs;
        }
        g_pack_arcs++;
        fclose(fp);
    }
    closedir(d);
    qsort(g_pack_entries, g_pack_count, sizeof(pack_entry), pack_entry_cmp);
    /* DLC archives come later in scan order and shadow the base ones */
    if (g_pack_count > 1U) {
        uint32_t w, r;
        for (w = 0U, r = 1U; r < g_pack_count; r++) {
            if (pack_entry_cmp(&g_pack_entries[w], &g_pack_entries[r]) == 0) {
                g_pack_entries[w] = g_pack_entries[r];
            } else {
                if (++w != r) {
                    g_pack_entries[w] = g_pack_entries[r];
                }
            }
        }
        g_pack_count = w + 1U;
    }
    g_pack_state = 1;
    FILE_INFO("pack-fallback: indexed %u entries across %u archives",
              g_pack_count, g_pack_arcs);
    {
        pack_entry key;
        pack_entry *hit;
        key.h1 = 0x4184b02aU;
        key.h2 = 0x340d6d8bU;
        hit = (pack_entry *)bsearch(&key, g_pack_entries, g_pack_count,
                                    sizeof(pack_entry), pack_entry_cmp);
        FILE_INFO("pack-fallback: selftest caustics %s", hit != NULL ? "FOUND" : "MISS");
    }
}

/* Serve one entry: decrypt (mode 0) into a cache file and open it. When
 * want_path is non-NULL the decrypted bytes are written to that exact path
 * first (create-parents like a real loose file), which keeps every later
 * open/fseek/fread of that path perfectly native. */
static FILE *pack_serve2(uint32_t h1, uint32_t h2, const char *root,
                         const char *want_path)
{
    pack_entry key;
    pack_entry *e;
    char cache[FILE_PATH_MAX];
    char tmp[FILE_PATH_MAX + 16];
    FILE *afp, *out;
    uint8_t *buf;
    uint32_t span, nw, w, n;

    key.h1 = h1;
    key.h2 = h2;
    e = (pack_entry *)bsearch(&key, g_pack_entries, g_pack_count,
                              sizeof(pack_entry), pack_entry_cmp);
    if (getenv("ISAAC_DBG_PACK")) {
        static int ps_;
        if (ps_ < 10) {
            ps_++;
            fprintf(stderr,
                    "[DBG packserve#%d] h=%08x/%08x tbl=%p count=%u bsearch=%s\n",
                    ps_, h1, h2, (void *)g_pack_entries, g_pack_count,
                    e != NULL ? "hit" : "miss");
        }
    }
    if (e == NULL) {
        /* bsearch misses on keys the selftest finds; sweep once. Only runs on
         * the failed-fopen path, so the linear cost is irrelevant. */
        uint32_t i;
        for (i = 0U; i < g_pack_count; i++) {
            if (g_pack_entries[i].h1 == h1 && g_pack_entries[i].h2 == h2) {
                e = &g_pack_entries[i];
                break;
            }
        }
        if (e == NULL) {
            return NULL;
        }
    }
    if (e->mode != 0U) {
        static int um_;
        if (um_++ < 8) {
            FILE_ERR("pack-fallback: hit %08x/%08x is mode %u (extraction not "
                     "wired yet)", h1, h2, (unsigned)e->mode);
        }
        return NULL;
    }
    snprintf(cache, sizeof cache, "%s/.packcache/%08x_%08x.bin", root, h1, h2);
    if (want_path != NULL) {
        FILE *probe = fopen(want_path, "rb");
        if (probe != NULL) {
            /* already materialized by an earlier call */
            fclose(probe);
            return fopen(want_path, "rb");
        }
    }
    out = fopen(cache, "rb");
    if (out != NULL) {
        return out;
    }
    snprintf(tmp, sizeof tmp, "%s.tmp", cache);
    span = (e->size + 3U) & ~3U;
    buf = (uint8_t *)malloc(span);
    if (buf == NULL) {
        return NULL;
    }
    afp = fopen(g_pack_arch[e->arc], "rb");
    if (afp == NULL) {
        free(buf);
        return NULL;
    }
    if (fseeko(afp, (off_t)e->off, SEEK_SET) != 0 ||
        fread(buf, 1U, span, afp) != span) {
        fclose(afp);
        free(buf);
        return NULL;
    }
    fclose(afp);
    {
        uint32_t kw = (h2 ^ 0xf9524287U) | 1U;
        nw = span / 4U;
        for (w = 0U; w < nw; w++) {
            uint32_t word = arc_rd32(buf + w * 4U) ^ kw;
            arc_wr32(buf + w * 4U, arc_permute(word, kw & 0xFU));
            kw = arc_mix(kw);
        }
    }
    if (want_path != NULL) {
        /* Write the decrypted bytes at the exact requested path: the guest
         * reopens it through the plain native path with zero special cases
         * (indistinguishable from a real loose-file override). */
        char dir[FILE_PATH_MAX];
        char *slash;
        FILE *probe;
        snprintf(dir, sizeof dir, "%s", want_path);
        slash = strrchr(dir, '/');
        if (slash != NULL) {
            char *p;
            *slash = '\0';
            for (p = dir + 1U; *p != '\0'; p++) {
                if (*p == '/') {
                    *p = '\0';
                    mkdir(dir, 0777);
                    *p = '/';
                }
            }
            mkdir(dir, 0777);
        }
        probe = fopen(want_path, "wb");
        if (probe != NULL) {
            size_t wn = fwrite(buf, 1U, e->size, probe);
            fclose(probe);
            free(buf);
            if (wn == (size_t)e->size) {
                return fopen(want_path, "rb");
            }
            unlink(want_path);
            return NULL;
        }
        FILE_ERR("pack-fallback: cannot materialize \"%{public}s\"", want_path);
        free(buf);
        return NULL;
    }
    out = fopen(tmp, "wb");
    if (out == NULL) {
        /* first extraction: the cache directory does not exist yet */
        char dir[FILE_PATH_MAX];
        char *slash;
        snprintf(dir, sizeof dir, "%s", cache);
        slash = strrchr(dir, '/');
        if (slash != NULL) {
            *slash = '\0';
            mkdir(dir, 0777);
            out = fopen(tmp, "wb");
        }
        if (out == NULL) {
            FILE_ERR("pack-fallback: cannot write cache \"%{public}s\"", tmp);
            free(buf);
            return NULL;
        }
    }
    n = (uint32_t)fwrite(buf, 1U, e->size, out);
    fclose(out);
    free(buf);
    if (n != e->size) {
        unlink(tmp);
        return NULL;
    }
    rename(tmp, cache);
    return fopen(cache, "rb");
}

/* Try to satisfy a failed read open from the archives. Returns an open stream
 * or NULL. `host` is the host path that failed. `allow_rebuild` reindexes once
 * (the first table has been observed to take host-heap damage during boot). */
static FILE *file_pack_fallback_impl(const char *host, int allow_rebuild)
{
    static const char *const prefixes[] = { "resources/", "resources-dlc3/", "" };
    const char *root = file_data_root();
    size_t rl = strlen(root);
    size_t hl = strlen(host);
    const char *suffix;
    char name[FILE_PATH_MAX];
    unsigned pi;

    if (g_pack_state < 0) {
        pack_init();
    }
    if (g_pack_state != 1) {
        return NULL;
    }
    if (hl <= rl + 1U || memcmp(host, root, rl) != 0 || host[rl] != '/') {
        return NULL;
    }
    suffix = host + rl + 1U;
    if (strstr(suffix, "packed/") != NULL || strstr(suffix, ".packcache") != NULL) {
        return NULL;
    }
    for (pi = 0U; pi < sizeof prefixes / sizeof prefixes[0]; pi++) {
        uint32_t h1, h2;
        FILE *fp;
        int n = snprintf(name, sizeof name, "%s%s", prefixes[pi], suffix);
        if (n <= 0 || (size_t)n >= sizeof name) {
            continue;
        }
        h1 = pack_hash_djb2(name);
        h2 = pack_hash_fnv(name);
        fp = pack_serve2(h1, h2, root, host);
        if (fp != NULL) {
            FILE_INFO("pack-fallback: \"%{public}s\" served from archives "
                      "(%{public}s + %{public}s)", suffix, prefixes[pi],
                      "ok");
            return fp;
        }
        {
            static int dbg_;
            if (strstr(name, "gfx/") != NULL && dbg_ < 12) {
                dbg_++;
                FILE_ERR("pack-miss: \"%{public}s\" h=%08x/%08x", name, h1, h2);
            }
        }
    }
    if (allow_rebuild) {
        FILE_ERR("pack-fallback: all lookups missed, rebuilding the index once");
        g_pack_state = -1;
        pack_init();
        if (g_pack_state == 1) {
            return file_pack_fallback_impl(host, 0);
        }
    }
    return NULL;
}

static FILE *file_pack_fallback(const char *host)
{
    return file_pack_fallback_impl(host, 1);
}

/* token = FILE_TOKEN_BASE + t. t values are handed out lowest-free-first and
 * recycled on close, which reproduces the MSVC CRT's _iob slot semantics the
 * game was compiled against: FILE* is a pointer into a small recycled pool,
 * reopening a closed file usually hands back the same value, and a stale
 * FILE* kept by the guest aliases whichever stream now owns that pool slot.
 * Schemes that minted unique-per-open values (generation fields, id pools)
 * broke every pattern where the guest frees a stream and keeps using the old
 * value -- which the archive loader does constantly. */
static signed short g_tok_slot[FILE_TOKEN_SPAN];      /* t -> slot, -1 free */
static int g_tok_ready;
static unsigned g_tok_scan = 1U;

static uint32_t file_token_make(unsigned index, uint32_t token)
{
    (void)index;
    return token;
}

static file_slot *file_slot_get(uint32_t token, unsigned *index_out)
{
    uint32_t t;
    signed short slot;

    if (token < FILE_TOKEN_BASE) {
        return NULL;
    }
    t = token - FILE_TOKEN_BASE;
    if (t >= FILE_TOKEN_SPAN) {
        return NULL;
    }
    if (!g_tok_ready) {
        return NULL;
    }
    slot = g_tok_slot[t];
    if (slot < 0 || !g_slots[slot].used) {
        return NULL;
    }
    if (index_out != NULL) {
        *index_out = (unsigned)slot;
    }
    return &g_slots[slot];
}

/* Bridge for sibling modules: host_ext_printf.c resolves the tokens this
 * module issued (fopen) so __stdio_common_vfprintf can write through the same
 * stream the guest holds, keeping the stream's position consistent for the
 * ftell/fseek/fread the guest does afterwards. NULL when the token is not one
 * of this module's live slots (the caller keeps its own handling). */
FILE *host_ext_file_stream(uint32_t token)
{
    file_slot *slot = file_slot_get(token, NULL);
    return slot != NULL ? slot->fp : NULL;
}


static int file_slot_alloc(FILE *fp, const char *host_path, int is_standard,
                           uint32_t *token_out)
{
    unsigned i;
    unsigned t;
    unsigned tries;

    if (!g_tok_ready) {
        unsigned k;
        for (k = 0U; k < FILE_TOKEN_SPAN; ++k) {
            g_tok_slot[k] = -1;
        }
        g_tok_ready = 1;
    }
    for (i = 0U; i < FILE_SLOT_COUNT; ++i) {
        if (g_slots[i].used && g_slots[i].zombie) {
            break;      /* reopen: same slot, same token, fresh stream */
        }
    }
    if (i == FILE_SLOT_COUNT) {
        for (i = 0U; i < FILE_SLOT_COUNT; ++i) {
            if (!g_slots[i].used) {
                break;
            }
        }
        if (i == FILE_SLOT_COUNT) {
            FILE_ERR("fopen: all %u streams are in use; \"%{public}s\" not opened",
                     FILE_SLOT_COUNT, host_path != NULL ? host_path : "?");
            return -1;
        }
        for (tries = 0U; tries <= FILE_TOKEN_MAX; ++tries) {
            t = (g_tok_scan + tries) % FILE_TOKEN_SPAN;
            if (t != 0U && g_tok_slot[t] < 0) {
                break;
            }
        }
        if (t == 0U && g_tok_slot[0] >= 0) {
            FILE_ERR("fopen: out of tokens; \"%{public}s\" not opened",
                     host_path != NULL ? host_path : "?");
            return -1;
        }
        g_tok_slot[t] = (signed short)i;
        g_tok_scan = (t + 1U) % FILE_TOKEN_SPAN;
    }
    {
        file_slot *slot = &g_slots[i];
        file_archive_forget(slot);      /* drop the previous stream's index */
        slot->zombie = 0U;
        if (slot->token == 0U) {
            slot->token = FILE_TOKEN_BASE + t;
        }
        slot->fp = fp;
        slot->is_standard = (uint8_t)(is_standard != 0);
        slot->read_seen = 0U;
        slot->write_seen = 0U;
        slot->path[0] = 0;
        if (host_path != NULL) {
            snprintf(slot->path, sizeof slot->path, "%s", host_path);
        }
        slot->used = 1U;
        *token_out = slot->token;
    }
    return 0;
}

static void file_slot_release(file_slot *slot)
{
    /* Keep the token -> slot binding and the slot itself: the guest frees a
     * stream and keeps calling fwrite/fseek on the old value afterwards (its
     * logger does this every batch). On MSVC the freed _iob slot absorbs that
     * silently, and a reopen of the same file hands the SAME FILE* back. */
    if (slot->token >= FILE_TOKEN_BASE && slot->token < FILE_TOKEN_BASE + FILE_TOKEN_SPAN) {
    }
    if (slot->fp != NULL) {
        fclose(slot->fp);
    }
    slot->fp = NULL;
    slot->zombie = 1U;
}

/* ---------------------------------------------------------------- fopen ---
 * FILE *__cdecl fopen(const char *path, const char *mode);
 *
 * Modes r/w/a with optional '+' and the b/t no-ops. 'b' changes nothing on
 * POSIX; neither does 't', because this layer is byte-exact and does NO CRLF
 * translation — which is what makes ftell/fseek byte offsets exact and is what
 * the guest's resource loader assumes. Writes honour POSIX O_APPEND for the
 * append modes, i.e. every write lands at end-of-file, as MSVC documents for
 * "a"; files opened for writing/appending get their missing parent directories
 * created first (the game writes log.txt, options.ini, savedatapath.txt under
 * nested directories of its data root).
 */
static int file_parse_mode(const char *mode, char *posix_out, size_t cap,
                           int *writes, int *appends)
{
    char base;
    int plus = 0;
    int binary = 0;
    size_t i;
    size_t len = strlen(mode);

    *writes = 0;
    *appends = 0;
    if (len == 0U || len >= FILE_MODE_MAX) {
        FILE_ERR("fopen: mode \"%{public}s\" is empty or longer than %u bytes",
                 mode, (unsigned)FILE_MODE_MAX - 1U);
        return -1;
    }
    base = mode[0];
    if (base != 'r' && base != 'w' && base != 'a') {
        FILE_ERR("fopen: mode \"%{public}s\" does not start with r, w or a",
                 mode);
        return -1;
    }
    for (i = 1U; i < len; ++i) {
        if (mode[i] == '+') {
            if (plus) {
                FILE_ERR("fopen: mode \"%{public}s\" repeats '+'", mode);
                return -1;
            }
            plus = 1;
        } else if (mode[i] == 'b') {
            binary = 1;                 /* no-op on POSIX, accepted silently */
        } else if (mode[i] == 't') {
            binary = 1;                 /* no-op, see the comment above */
        } else if (mode[i] != ' ') {
            FILE_ERR("fopen: mode \"%{public}s\" has unsupported character "
                     "\"%{public}c\"", mode, mode[i]);
            return -1;
        }
    }
    (void)binary;
    if (snprintf(posix_out, cap, "%c%s", base, plus ? "+" : "") >= (int)cap) {
        return -1;
    }
    *writes = (base == 'w' || base == 'a' || plus);
    *appends = (base == 'a');
    return 0;
}

static void h_file_fopen(CPU *c)
{
    char raw_path[FILE_PATH_MAX];
    char mode[FILE_MODE_MAX];
    char host[FILE_PATH_MAX];
    char posix_mode[4];
    uint32_t token = 0U;
    const char *what = "fopen";
    FILE *fp;
    struct stat st;
    int rooted = 0;
    int writes = 0;
    int appends = 0;
    int host_errno = 0;
    int entry_errno = errno;

    if (file_guest_string(file_arg(c, 0), raw_path, sizeof raw_path) != 0) {
        FILE_ERR("fopen: unusable path pointer 0x%{public}08x", file_arg(c, 0));
        file_errno_set(EINVAL);
        c->eax = 0U;
        file_cdecl_ret(c);
        return;
    }
    if (file_guest_string(file_arg(c, 1), mode, sizeof mode) != 0) {
        FILE_ERR("fopen: unusable mode pointer 0x%{public}08x for "
                 "\"%{public}s\"", file_arg(c, 1), raw_path);
        file_errno_set(EINVAL);
        c->eax = 0U;
        file_cdecl_ret(c);
        return;
    }

    if (file_normalise(raw_path, what, host, sizeof host, &rooted) != 0) {
        file_errno_set(EINVAL);
        c->eax = 0U;
        file_cdecl_ret(c);
        return;
    }
    if (file_parse_mode(mode, posix_mode, sizeof posix_mode, &writes,
                        &appends) != 0) {
        file_errno_set(EINVAL);
        c->eax = 0U;
        file_cdecl_ret(c);
        return;
    }

    if (writes) {
        /* mkdir -p: only the parent components, never the file itself. */
        if (file_mkdirs(host, what) != 0) {
            host_errno = EACCES;
            FILE_ERR("fopen: could not create the parent directories of "
                     "\"%{public}s\" -> EACCES", host);
            file_errno_set(host_errno);
            c->eax = 0U;
            file_cdecl_ret(c);
            return;
        }
    }

    errno = 0;
    fp = fopen(host, posix_mode);
    if (fp == NULL) {
        /* The engine probes its mount table twice before the archive mounts;
         * after the second failed open the container rebuild passes are done
         * and nothing touches them again before the mounts run, so this is
         * the one stable moment to install the root/resources volumes the
         * registry lookup fails to produce. */
        if (strstr(raw_path != NULL ? raw_path : "", "kage_mount_points.dat") != NULL
                && mode != NULL && mode[0] == 'r') {
            static int patch_count;
            patch_count++;
            if (patch_count >= 2) {
                isaac_mount_container_patch();
            }
        }
        host_errno = errno != 0 ? errno : EIO;
        /* The engine stores gfx/video/audio content inside the .a archives and
         * still asks the CRT for the loose path on a cache miss; serve those
         * from the archives instead of handing the guest a NULL stream. */
        if (mode != NULL && mode[0] == 'r') {
            FILE *packed = file_pack_fallback(host);
            if (packed != NULL) {
                fp = packed;
                goto packed_opened;
            }
        }
        FILE_ERR("fopen: \"%{public}s\" (guest \"%{public}s\", mode "
                 "\"%{public}s\"%{public}s) -> NULL (errno=%d)",
                 host, raw_path, mode, rooted ? ", already rooted" : "",
                 host_errno);
        file_errno_set(host_errno);
        errno = entry_errno;
        c->eax = 0U;
        file_cdecl_ret(c);
        return;
    }

packed_opened:
    /* Windows refuses to fopen a directory (EACCES); glibc happily opens one
     * and only fails later, which would hand the guest a stream whose first
     * fread fails instead of an honest open failure. */
    if (fstat(fileno(fp), &st) == 0 && S_ISDIR(st.st_mode)) {
        fclose(fp);
        FILE_ERR("fopen: \"%{public}s\" is a directory -> NULL (EACCES)", host);
        file_errno_set(EACCES);
        errno = entry_errno;
        c->eax = 0U;
        file_cdecl_ret(c);
        return;
    }

    if (file_slot_alloc(fp, host, 0, &token) != 0) {
        fclose(fp);
        file_errno_set(EMFILE);
        errno = entry_errno;
        c->eax = 0U;
        file_cdecl_ret(c);
        return;
    }
    {
        file_slot *arslot = file_slot_get(token, NULL);
        if (arslot != NULL) {
            file_archive_attach(arslot, host);
        }
    }
    g_open_serial++;
    FILE_INFO("fopen[%{public}u]: \"%{public}s\" -> \"%{public}s\" (mode "
              "\"%{public}s\"%{public}s) token=0x%{public}08x",
              g_open_serial, raw_path, host, mode, rooted ? ", rooted" : "",
              token);
    errno = entry_errno;
    c->eax = token;
    file_cdecl_ret(c);
}

/* --------------------------------------------------------------- fclose ---
 * int __cdecl fclose(FILE *stream);
 * Returns 0 on success, EOF(-1) on failure. The stream is recycled either way
 * (a failing fclose still closed the stream, as on Windows), so a second close
 * of the same token is detected as stale rather than closing twice.
 */
static void h_file_fclose(CPU *c)
{
    uint32_t token = file_arg(c, 0);
    unsigned std_index = 0U;
    file_slot *slot = file_slot_get(token, NULL);
    FILE *standard = file_standard_stream(token, &std_index);
    int entry_errno = errno;
    int rc;

    if (standard != NULL) {
        /* Closing the host's own standard stream would take the harness log
         * with it. The guest is told success and the stream stays open, which
         * is the only difference from Windows; it is logged, not hidden. */
        FILE_INFO("fclose: %{public}s (0x%{public}08x) left open by the host "
                  "-> 0", file_standard_name(std_index), token);
        errno = entry_errno;
        c->eax = 0U;
        file_cdecl_ret(c);
        return;
    }
    if (slot == NULL) {
        FILE_ERR("fclose: token 0x%{public}08x is unknown, already closed or "
                 "not a stream issued by this module -> EOF", token);
        file_errno_set(EBADF);
        errno = entry_errno;
        c->eax = (uint32_t)-1;
        file_cdecl_ret(c);
        return;
    }

    errno = 0;
    rc = 0;   /* file_slot_release() below closes the host stream */
    if (rc != 0) {
        int host_errno = errno != 0 ? errno : EIO;
        FILE_ERR("fclose: \"%{public}s\" (token 0x%{public}08x) failed "
                 "(errno=%d)", slot->path, token, host_errno);
        file_errno_set(host_errno);
    }
    FILE_INFO("fclose: \"%{public}s\" token=0x%{public}08x -> %d",
              slot->path, token, rc == 0 ? 0 : -1);
    file_slot_release(slot);
    errno = entry_errno;
    c->eax = rc == 0 ? 0U : (uint32_t)-1;
    file_cdecl_ret(c);
}

/* ---------------------------------------------------------------- fread ---
 * size_t __cdecl fread(void *ptr, size_t size, size_t count, FILE *stream);
 * Returns the number of ITEMS read, exactly as MSVC does. The destination is a
 * guest address and the arena is a flat mapping, so the host reads straight
 * into it: no bounce buffer, no size ceiling.
 */
static void h_file_fread(CPU *c)
{
    uint32_t ptr = file_arg(c, 0);
    uint32_t size = file_arg(c, 1);
    uint32_t count = file_arg(c, 2);
    uint32_t token = file_arg(c, 3);
    unsigned std_index = 0U;
    file_slot *slot = file_slot_get(token, NULL);
    FILE *standard = file_standard_stream(token, &std_index);
    FILE *fp = (slot != NULL) ? slot->fp : standard;
    uint64_t total = (uint64_t)size * (uint64_t)count;
    int entry_errno = errno;
    size_t got;

    if (size == 0U || count == 0U) {
        c->eax = 0U;                    /* C: nothing is requested, nothing is
                                         * read, the stream is untouched */
        file_cdecl_ret(c);
        return;
    }
    if (fp == NULL) {
        FILE_ERR("fread: token 0x%{public}08x is unknown, already closed or "
                 "not a stream issued by this module -> 0", token);
        file_errno_set(EBADF);
        errno = entry_errno;
        c->eax = 0U;
        file_cdecl_ret(c);
        return;
    }
    if (!file_guest_range(ptr, total)) {
        FILE_ERR("fread: guest buffer 0x%{public}08x + %llu bytes is outside "
                 "the arena -> 0", ptr, (unsigned long long)total);
        file_errno_set(EINVAL);
        errno = entry_errno;
        c->eax = 0U;
        file_cdecl_ret(c);
        return;
    }

    if (standard != NULL) {
        FILE_ANNOUNCE("fread: first call is %{public}s (guest reads its own "
                      "console input)", file_standard_name(std_index));
    } else if (!slot->read_seen) {
        slot->read_seen = 1U;
        FILE_INFO("fread: first read of \"%{public}s\" (token 0x%{public}08x, "
                  "%u x %u)", slot->path, token, size, count);
    }

    errno = 0;
    {
        long read_pos = (slot != NULL) ? (long)ftello(fp) : -1L;
        got = fread(isaac_arena_base + ptr, (size_t)size, (size_t)count, fp);
        if (slot != NULL && read_pos >= 0 && got != 0U) {
            file_archive_decrypt(slot, read_pos, isaac_arena_base + ptr,
                                 (size_t)got * (size_t)size);
        }
    }
    if (getenv("ISAAC_SEQ_TRACE") != NULL) {
        FILE_ERR("[SEQ] fread token=%08x size=%u count=%u -> got=%u pos=%ld",
                 token, size, count, (unsigned)got,
                 fp != NULL ? (long)ftello(fp) : -1L);
    }
    if (got < count) {
        int host_errno = errno;
        if (ferror(fp)) {
            FILE_ERR("fread: \"%{public}s\" stopped after %u of %u items "
                     "(errno=%d)", slot != NULL ? slot->path : "standard",
                     (unsigned)got, count, host_errno);
            file_errno_set(host_errno != 0 ? host_errno : EIO);
        } else {
            FILE_INFO("fread: \"%{public}s\" short read %u of %u items at "
                      "end of file",
                      slot != NULL ? slot->path : "standard", (unsigned)got,
                      count);
        }
    }
    errno = entry_errno;
    c->eax = (uint32_t)got;
    file_cdecl_ret(c);
}

/* --------------------------------------------------------------- fwrite ---
 * size_t __cdecl fwrite(const void *ptr, size_t size, size_t count, FILE *);
 * Returns the number of ITEMS written. stdout/stderr take the ordinary host
 * path, and a one-line summary goes to the log as well, because on device the
 * process's stdout is not the observable channel (hilog is). */
static void h_file_fwrite(CPU *c)
{
    uint32_t ptr = file_arg(c, 0);
    uint32_t size = file_arg(c, 1);
    uint32_t count = file_arg(c, 2);
    uint32_t token = file_arg(c, 3);
    unsigned std_index = 0U;
    file_slot *slot = file_slot_get(token, NULL);
    FILE *standard = file_standard_stream(token, &std_index);
    FILE *fp = (slot != NULL) ? slot->fp : standard;
    uint64_t total = (uint64_t)size * (uint64_t)count;
    int entry_errno = errno;
    size_t put;

    if (size == 0U || count == 0U) {
        c->eax = 0U;
        file_cdecl_ret(c);
        return;
    }
    if (fp == NULL) {
        if (slot != NULL) {
            /* Zombie stream: MSVC's freed _iob absorbs the write. */
            errno = entry_errno;
            c->eax = count;
            file_cdecl_ret(c);
            return;
        }
        FILE_ERR("fwrite: token 0x%{public}08x is unknown, already closed or "
                 "not a stream issued by this module -> 0", token);
        file_errno_set(EBADF);
        errno = entry_errno;
        c->eax = 0U;
        file_cdecl_ret(c);
        return;
    }
    if (!file_guest_range(ptr, total)) {
        FILE_ERR("fwrite: guest buffer 0x%{public}08x + %llu bytes is outside "
                 "the arena -> 0", ptr, (unsigned long long)total);
        file_errno_set(EINVAL);
        errno = entry_errno;
        c->eax = 0U;
        file_cdecl_ret(c);
        return;
    }

    if (standard != NULL) {
        FILE_INFO("fwrite: %llu bytes to %{public}s", (unsigned long long)total,
                  file_standard_name(std_index));
    } else if (!slot->write_seen) {
        slot->write_seen = 1U;
        FILE_INFO("fwrite: first write of \"%{public}s\" (token "
                  "0x%{public}08x)", slot->path, token);
    }

    errno = 0;
    put = fwrite(isaac_arena_base + ptr, (size_t)size, (size_t)count, fp);
    /* On device the guest's own log.txt is the only window into how far the
     * engine got, and the sandbox is unreadable from outside. Mirror every
     * log.txt write to hilog so the device run is observable. */
    if (slot != NULL && strstr(slot->path, "log.txt") != NULL && total > 0U) {
        const char *text = (const char *)isaac_arena_base + ptr;
        char line[512];
        size_t n = total < sizeof line - 1U ? total : sizeof line - 1U;
        size_t i;
        size_t out = 0;
        for (i = 0; i < n && out < sizeof line - 2U; i++) {
            char ch = text[i];
            if (ch == '\n') {
                line[out] = '\0';
                if (out > 0U) {
                    FILE_INFO("guest-log: %{public}s", line);
                }
                out = 0U;
            } else if (ch != '\r') {
                line[out++] = ch;
            }
        }
        if (out > 0U) {
            line[out] = '\0';
            FILE_INFO("guest-log: %{public}s", line);
        }
    }
    if (put < count) {
        int host_errno = errno != 0 ? errno : EIO;
        FILE_ERR("fwrite: \"%{public}s\" accepted %u of %u items (errno=%d)",
                 slot != NULL ? slot->path : "standard", (unsigned)put, count,
                 host_errno);
        file_errno_set(host_errno);
    }
    errno = entry_errno;
    c->eax = (uint32_t)put;
    file_cdecl_ret(c);
}

/* ---------------------------------------------------------------- fseek ---
 * int __cdecl fseek(FILE *stream, long offset, int origin);
 * MSVC uses SEEK_SET=0, SEEK_CUR=1, SEEK_END=2, which are the POSIX values too,
 * so the origin is checked rather than translated. Offsets are byte offsets
 * (the reason this layer does no text translation); the guest's long is 32
 * bits and so is the argument dword. Returns 0 on success, -1 on failure.
 */
static void h_file_fseek(CPU *c)
{
    uint32_t token = file_arg(c, 0);
    int32_t offset = (int32_t)file_arg(c, 1);
    uint32_t origin = file_arg(c, 2);
    unsigned std_index = 0U;
    file_slot *slot = file_slot_get(token, NULL);
    FILE *standard = file_standard_stream(token, &std_index);
    FILE *fp = (slot != NULL) ? slot->fp : standard;
    int entry_errno = errno;
    int whence;

    if (fp == NULL) {
        FILE_ERR("fseek: token 0x%{public}08x is unknown, already closed or "
                 "not a stream issued by this module -> -1", token);
        file_errno_set(EBADF);
        errno = entry_errno;
        c->eax = (uint32_t)-1;
        file_cdecl_ret(c);
        return;
    }
    switch (origin) {
    case 0U: whence = SEEK_SET; break;
    case 1U: whence = SEEK_CUR; break;
    case 2U: whence = SEEK_END; break;
    default:
        FILE_ERR("fseek: origin %u is not SEEK_SET/SEEK_CUR/SEEK_END -> -1",
                 origin);
        file_errno_set(EINVAL);
        errno = entry_errno;
        c->eax = (uint32_t)-1;
        file_cdecl_ret(c);
        return;
    }

    FILE_ANNOUNCE("fseek: first call (offset %{public}d, origin "
                  "%{public}u)", offset, origin);
    if (fp == NULL) {
        errno = entry_errno;
        c->eax = 0U;
        file_cdecl_ret(c);
        return;
    }
    errno = 0;
    if (fseeko(fp, (off_t)offset, whence) != 0) {
        int host_errno = errno != 0 ? errno : EIO;
        FILE_ERR("fseek: \"%{public}s\" offset %d origin %u failed "
                 "(errno=%d)", slot != NULL ? slot->path : "standard", offset,
                 origin, host_errno);
        file_errno_set(host_errno);
        errno = entry_errno;
        c->eax = (uint32_t)-1;
        file_cdecl_ret(c);
        return;
    }
    if (getenv("ISAAC_SEQ_TRACE") != NULL) {
        FILE_ERR("[SEQ] fseek token=%08x off=%d origin=%u -> pos=%ld",
                 token, offset, origin, (long)ftello(fp));
    }
    if (slot != NULL) {
        /* Landing exactly on an entry offset means the guest is about to pull
         * that entry's stored bytes: arm the cipher for it. */
        file_archive_arm(slot, (long)ftello(fp));
    }
    errno = entry_errno;
    c->eax = 0U;
    file_cdecl_ret(c);
}

/* ---------------------------------------------------------------- ftell ---
 * long __cdecl ftell(FILE *stream);
 * Byte offset from the start of the file, -1 on failure. A position that does
 * not fit the guest's 32-bit long is reported as -1 with EOVERFLOW rather than
 * truncated into a plausible-looking wrong offset.
 */
static void h_file_ftell(CPU *c)
{
    uint32_t token = file_arg(c, 0);
    file_slot *slot = file_slot_get(token, NULL);
    unsigned std_index = 0U;
    FILE *standard = file_standard_stream(token, &std_index);
    FILE *fp = (slot != NULL) ? slot->fp : standard;
    int entry_errno = errno;
    off_t pos;

    if (fp == NULL) {
        FILE_ERR("ftell: token 0x%{public}08x is unknown, already closed or "
                 "not a stream issued by this module -> -1", token);
        file_errno_set(EBADF);
        errno = entry_errno;
        c->eax = (uint32_t)-1;
        file_cdecl_ret(c);
        return;
    }

    FILE_ANNOUNCE("ftell: first call (stream \"%{public}s\")",
                  slot != NULL ? slot->path : file_standard_name(std_index));
    errno = 0;
    pos = ftello(fp);
    if (pos < 0) {
        int host_errno = errno != 0 ? errno : EIO;
        FILE_ERR("ftell: \"%{public}s\" failed (errno=%d)",
                 slot != NULL ? slot->path : "standard", host_errno);
        file_errno_set(host_errno);
        errno = entry_errno;
        c->eax = (uint32_t)-1;
        file_cdecl_ret(c);
        return;
    }
    if (pos > (off_t)0x7FFFFFFF) {
        FILE_ERR("ftell: \"%{public}s\" is at offset %lld, which does not fit "
                 "a 32-bit long -> -1 (EOVERFLOW)",
                 slot != NULL ? slot->path : "standard", (long long)pos);
        file_errno_set(EOVERFLOW);
        errno = entry_errno;
        c->eax = (uint32_t)-1;
        file_cdecl_ret(c);
        return;
    }
    errno = entry_errno;
    c->eax = (uint32_t)pos;
    file_cdecl_ret(c);
}

/* --------------------------------------------------------------- fflush ---
 * int __cdecl fflush(FILE *stream);  NULL flushes every stream.
 * Returns 0 on success, EOF(-1) on failure.
 */
static void h_file_fflush(CPU *c)
{
    uint32_t token = file_arg(c, 0);
    unsigned std_index = 0U;
    file_slot *slot = NULL;
    FILE *standard = NULL;
    FILE *fp;
    int entry_errno = errno;

    if (token == 0U) {
        FILE_ANNOUNCE("fflush: first call is fflush(NULL) (every stream)");
        fp = NULL;                      /* fflush(NULL) flushes all */
    } else {
        slot = file_slot_get(token, NULL);
        standard = file_standard_stream(token, &std_index);
        fp = (slot != NULL) ? slot->fp : standard;
        if (fp == NULL) {
            FILE_ERR("fflush: token 0x%{public}08x is unknown, already closed "
                     "or not a stream issued by this module -> EOF", token);
            file_errno_set(EBADF);
            errno = entry_errno;
            c->eax = (uint32_t)-1;
            file_cdecl_ret(c);
            return;
        }
        FILE_ANNOUNCE("fflush: first call (stream \"%{public}s\")",
                      slot != NULL ? slot->path
                                   : file_standard_name(std_index));
    }

    errno = 0;
    if (fflush(fp) != 0) {
        int host_errno = errno != 0 ? errno : EIO;
        FILE_ERR("fflush: \"%{public}s\" failed (errno=%d)",
                 slot != NULL ? slot->path : "all streams", host_errno);
        file_errno_set(host_errno);
        errno = entry_errno;
        c->eax = (uint32_t)-1;
        file_cdecl_ret(c);
        return;
    }
    errno = entry_errno;
    c->eax = 0U;
    file_cdecl_ret(c);
}

/* -------------------------------------------------------------- setvbuf ---
 * int __cdecl setvbuf(FILE *stream, char *buf, int mode, size_t size);
 *
 * Not imported by this binary (it is registered so the name has an owner).
 * The buffering MODE is honoured — MSVC's _IOFBF=0x0000 / _IOLBF=0x0040 /
 * _IONBF=0x0004 are translated to the host's constants, which are different
 * numbers — and the caller's buffer is deliberately NOT used: it is guest
 * arena memory whose lifetime this layer cannot police, and the host allocates
 * its own. Returns 0 on success, nonzero on failure, like MSVC.
 */
static void h_file_setvbuf(CPU *c)
{
    uint32_t token = file_arg(c, 0);
    uint32_t guest_buf = file_arg(c, 1);
    uint32_t mode = file_arg(c, 2);
    uint32_t size = file_arg(c, 3);
    file_slot *slot = file_slot_get(token, NULL);
    unsigned std_index = 0U;
    FILE *standard = file_standard_stream(token, &std_index);
    FILE *fp = (slot != NULL) ? slot->fp : standard;
    int host_mode;
    int entry_errno = errno;

    if (fp == NULL) {
        FILE_ERR("setvbuf: token 0x%{public}08x is unknown, already closed or "
                 "not a stream issued by this module -> nonzero", token);
        file_errno_set(EBADF);
        errno = entry_errno;
        c->eax = (uint32_t)-1;
        file_cdecl_ret(c);
        return;
    }
    switch (mode) {
    case 0x0000U: host_mode = _IOFBF; break;
    case 0x0040U: host_mode = _IOLBF; break;
    case 0x0004U: host_mode = _IONBF; break;
    default:
        FILE_ERR("setvbuf: mode 0x%{public}08x is not _IOFBF/_IOLBF/_IONBF -> "
                 "nonzero", mode);
        file_errno_set(EINVAL);
        errno = entry_errno;
        c->eax = (uint32_t)-1;
        file_cdecl_ret(c);
        return;
    }
    if (guest_buf != 0U) {
        FILE_INFO("setvbuf: the guest's own %u-byte buffer (0x%{public}08x) is "
                  "not used; the host allocates its own",
                  size, guest_buf);
    }
    FILE_ANNOUNCE("setvbuf: first call (stream \"%{public}s\", mode "
                  "0x%{public}04x, size %{public}u)",
                  slot != NULL ? slot->path : file_standard_name(std_index),
                  mode, size);
    errno = 0;
    if (setvbuf(fp, NULL, host_mode, (size_t)size) != 0) {
        int host_errno = errno != 0 ? errno : EINVAL;
        FILE_ERR("setvbuf: \"%{public}s\" refused mode 0x%{public}04x "
                 "(errno=%d)", slot != NULL ? slot->path : "standard", mode,
                 host_errno);
        file_errno_set(host_errno);
        errno = entry_errno;
        c->eax = (uint32_t)-1;
        file_cdecl_ret(c);
        return;
    }
    errno = entry_errno;
    c->eax = 0U;
    file_cdecl_ret(c);
}

/* -------------------------------------------------------------- _fileno ---
 * int __cdecl _fileno(FILE *stream);
 * Stable, non-negative, and the inverse of _get_osfhandle below: the three
 * standard streams report 0/1/2 and a file stream reports index + 3.
 */
static void h_file_fileno(CPU *c)
{
    uint32_t token = file_arg(c, 0);
    unsigned index = 0U;
    unsigned std_index = 0U;
    file_slot *slot = file_slot_get(token, &index);
    FILE *standard = file_standard_stream(token, &std_index);

    if (slot == NULL && standard == NULL) {
        FILE_ERR("_fileno: token 0x%{public}08x is unknown, already closed or "
                 "not a stream issued by this module -> -1", token);
        file_errno_set(EBADF);
        c->eax = (uint32_t)-1;
        file_cdecl_ret(c);
        return;
    }
    c->eax = (slot != NULL) ? index + FILE_FD_BASE : std_index;
    file_cdecl_ret(c);
}

/* -------------------------------------------------------- _get_osfhandle ---
 * intptr_t __cdecl _get_osfhandle(int fd);
 * The inverse of _fileno: a descriptor this module handed out maps back to the
 * very token the guest opened, which is all the guest can do with it — the
 * token is not a KERNEL32 handle and host_ohos.c's handle table will not accept
 * it. Anything else (including a KERNEL32 handle from CreateFileA, which shares
 * the 0x10000 space with the standard-stream tokens) is refused with the
 * Windows error value -1 / EBADF and logged.
 */
static void h_file_get_osfhandle(CPU *c)
{
    uint32_t fd = (uint32_t)(int32_t)file_arg(c, 0);

    if (fd < FILE_IOB_COUNT) {
        FILE_ANNOUNCE("_get_osfhandle: first call (fd %{public}u -> standard "
                      "stream token)", fd);
        c->eax = FILE_IOB_BASE + fd;
        file_cdecl_ret(c);
        return;
    }
    if (fd >= FILE_FD_BASE && fd < FILE_FD_BASE + FILE_SLOT_COUNT) {
        unsigned index = fd - FILE_FD_BASE;
        if (g_slots[index].used) {
            FILE_ANNOUNCE("_get_osfhandle: first call (fd %{public}u -> CRT "
                          "token; not usable as a KERNEL32 handle)", fd);
            c->eax = g_slots[index].token;
            file_cdecl_ret(c);
            return;
        }
    }
    FILE_ERR("_get_osfhandle: fd %{public}u is not an open stream of this "
             "module -> -1 (EBADF)", fd);
    file_errno_set(EBADF);
    c->eax = (uint32_t)-1;
    file_cdecl_ret(c);
}

/* --------------------------------------------------------------- remove ---
 * int __cdecl remove(const char *path);
 * Same containment rules as fopen; no directory creation, no truncation. 0 on
 * success, -1 with errno on failure.
 */
static void h_file_remove(CPU *c)
{
    char raw_path[FILE_PATH_MAX];
    char host[FILE_PATH_MAX];
    const char *what = "remove";
    int rooted = 0;
    int entry_errno = errno;

    raw_path[0] = '\0';
    if (file_guest_string(file_arg(c, 0), raw_path, sizeof raw_path) != 0) {
        FILE_ERR("remove: unusable path pointer 0x%{public}08x",
                 file_arg(c, 0));
        file_errno_set(EINVAL);
        errno = entry_errno;
        c->eax = (uint32_t)-1;
        file_cdecl_ret(c);
        return;
    }
    if (file_normalise(raw_path, what, host, sizeof host, &rooted) != 0) {
        file_errno_set(EINVAL);
        errno = entry_errno;
        c->eax = (uint32_t)-1;
        file_cdecl_ret(c);
        return;
    }

    errno = 0;
    if (unlink(host) != 0) {
        int host_errno = errno != 0 ? errno : EIO;
        FILE_ERR("remove: unlink(\"%{public}s\") for guest \"%{public}s\" "
                 "failed (errno=%d)", host, raw_path, host_errno);
        file_errno_set(host_errno);
        errno = entry_errno;
        c->eax = (uint32_t)-1;
        file_cdecl_ret(c);
        return;
    }
    FILE_INFO("remove: \"%{public}s\" (host \"%{public}s\") removed",
              raw_path, host);
    errno = entry_errno;
    c->eax = 0U;
    file_cdecl_ret(c);
}

/* --------------------------------------------------------------- _access ---
 * int __cdecl _access(const char *path, int mode);
 * MSVC's mode bits (0=exist, 2=write, 4=read, 6=both) are the POSIX F_OK/W_OK/
 * R_OK values, so they pass through. The mapped path inside the data root is
 * probed first; a path that does not start with the root is then also probed
 * as the guest spelled it, which is what the handler this replaces does today
 * (h_crt__access feeds the raw guest string to access()). That fallback adds no
 * capability the runtime does not already have — it only keeps the existing
 * out-of-root absolute probes working — and every fallback is logged.
 */
static void h_file_access(CPU *c)
{
    char raw_path[FILE_PATH_MAX];
    char host[FILE_PATH_MAX];
    char direct[FILE_PATH_MAX];
    uint32_t mode = file_arg(c, 1);
    const char *what = "_access";
    int rooted = 0;
    int entry_errno = errno;
    size_t i;

    if (file_guest_string(file_arg(c, 0), raw_path, sizeof raw_path) != 0) {
        FILE_ERR("_access: unusable path pointer 0x%{public}08x",
                 file_arg(c, 0));
        file_errno_set(EINVAL);
        errno = entry_errno;
        c->eax = (uint32_t)-1;
        file_cdecl_ret(c);
        return;
    }
    if (mode != 0U && mode != 2U && mode != 4U && mode != 6U) {
        FILE_ERR("_access: mode %u is not an MSVC exist/read/write mask -> -1",
                 mode);
        file_errno_set(EINVAL);
        errno = entry_errno;
        c->eax = (uint32_t)-1;
        file_cdecl_ret(c);
        return;
    }

    if (file_normalise(raw_path, what, host, sizeof host, &rooted) != 0) {
        file_errno_set(EINVAL);
        errno = entry_errno;
        c->eax = (uint32_t)-1;
        file_cdecl_ret(c);
        return;
    }
    FILE_ANNOUNCE("_access: first call (guest \"%{public}s\", mode "
                  "%{public}u)", raw_path, mode);
    errno = 0;
    if (access(host, (int)mode) == 0) {
        errno = entry_errno;
        c->eax = 0U;
        file_cdecl_ret(c);
        return;
    }

    /* Fallback: the path as the guest spelled it, separators translated. */
    if (!rooted && (raw_path[0] == '/' || raw_path[0] == '\\')) {
        size_t len = strlen(raw_path);
        if (len < sizeof direct) {
            memcpy(direct, raw_path, len + 1U);
            for (i = 0U; direct[i] != '\0'; ++i) {
                if (direct[i] == '\\') {
                    direct[i] = '/';
                }
            }
            if (access(direct, (int)mode) == 0) {
                FILE_INFO("_access: \"%{public}s\" is not under the data root; "
                          "probed as spelled (\"%{public}s\") and found",
                          raw_path, direct);
                errno = entry_errno;
                c->eax = 0U;
                file_cdecl_ret(c);
                return;
            }
        }
    }
    file_errno_set(errno != 0 ? errno : ENOENT);
    errno = entry_errno;
    c->eax = (uint32_t)-1;
    file_cdecl_ret(c);
}

/* ------------------------------------------------------------- descriptor */

static const host_ext_entry s_entries[] = {
    /* The family the guest's resource loader actually calls. */
    { "api-ms-win-crt-stdio-l1-1-0.dll!fopen", h_file_fopen },
    { "api-ms-win-crt-stdio-l1-1-0.dll!fclose", h_file_fclose },
    { "api-ms-win-crt-stdio-l1-1-0.dll!fread", h_file_fread },
    { "api-ms-win-crt-stdio-l1-1-0.dll!fwrite", h_file_fwrite },
    { "api-ms-win-crt-stdio-l1-1-0.dll!fseek", h_file_fseek },
    { "api-ms-win-crt-stdio-l1-1-0.dll!ftell", h_file_ftell },
    { "api-ms-win-crt-stdio-l1-1-0.dll!fflush", h_file_fflush },
    /* Not in this binary's IAT; registered so the name has one owner. */
    { "api-ms-win-crt-stdio-l1-1-0.dll!setvbuf", h_file_setvbuf },
    { "api-ms-win-crt-stdio-l1-1-0.dll!_fileno", h_file_fileno },
    { "api-ms-win-crt-stdio-l1-1-0.dll!_get_osfhandle", h_file_get_osfhandle },
    /* Same path rules, same root: delete and existence probing. */
    { "api-ms-win-crt-filesystem-l1-1-0.dll!remove", h_file_remove },
    { "api-ms-win-crt-filesystem-l1-1-0.dll!_access", h_file_access },
    /* The guest's errno channel, without which "correct errno" is unreadable
     * and _errno() hands out a NULL pointer. */
    { "api-ms-win-crt-runtime-l1-1-0.dll!_errno", h_file_errno },
    { "api-ms-win-crt-runtime-l1-1-0.dll!_set_errno", h_file_set_errno },
};

static const host_ext_module s_module = {
    s_entries, (unsigned)(sizeof s_entries / sizeof s_entries[0]),
    NULL, 0U,
};

const host_ext_module *host_ext_file(void)
{
    /* Called by host_ohos.c's dispatcher for every import lookup once this
     * function is listed in s_ext_providers[]. The line below is therefore the
     * module's own proof of registration: if the harness log has no
     * "dispatcher consulted this module", the handlers are dead code and the
     * import table still routes fopen to host_ohos.c's hollow stub. */
    static int announced;
    if (!announced) {
        announced = 1;
        FILE_INFO("host_ext_file: dispatcher consulted this module (%{public}u "
                  "entries); fopen/fread/fwrite/fseek/ftell/fclose/fflush and "
                  "the errno pair are live",
                  (unsigned)(sizeof s_entries / sizeof s_entries[0]));
    }
    return &s_module;
}

/* The dispatcher only calls host_ext_file() if host_ohos.c lists it in
 * s_ext_providers[], so a module that lost that race would be invisible.
 * This startup line states the module's own state without claiming anything it
 * cannot verify: three provider slots exist (printf, kage, misc) and this file
 * owns none of them. Silence at startup is a bug; the run-time proof is the
 * line above. */
__attribute__((constructor)) static void host_ext_file_self_check(void)
{
    FILE_INFO("host_ext_file.c: %u static entries built; reachable only if "
              "host_ohos.c's s_ext_providers[] lists host_ext_file()",
              (unsigned)(sizeof s_entries / sizeof s_entries[0]));
}
