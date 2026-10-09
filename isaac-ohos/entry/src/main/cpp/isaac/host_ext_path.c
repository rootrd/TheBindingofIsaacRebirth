/* host_ext_path.c — the guest's KERNEL32 file APIs, rooted through the same
 * data-root mapper the CRT stdio layer uses.
 *
 * WHY THIS MODULE EXISTS
 * host_ext_file.c gave the guest a real fopen/fread/fwrite/fseek/ftell/fclose
 * family, and every path there is mapped under ISAAC_DATA_ROOT. The KERNEL32
 * file family was left behind: host_ohos.c's h_CreateFileA(), h_
 * GetFileAttributesA() and h_CreateDirectoryA() pass the RAW guest string to
 * the host (open/stat/mkdir), and the guest's "current directory" is the data
 * root value only *by convention* (h_GetCurrentDirectoryA() hands the guest
 * ISAAC_DATA_ROOT) — the host process's own cwd is wherever the harness was
 * started. So a guest path like "Documents/My Games/Binding of Isaac
 * Repentance" hit the harness's build directory, stat() failed, and the guest
 * was told the directory does not exist (INVALID_FILE_ATTRIBUTES) and that
 * mkdir() failed. The guest's save-path probe therefore never agreed with the
 * save files fopen() had already created under the root.
 *
 * This module roots those three APIs through the same rules host_ext_file.c
 * implements, so the KERNEL32 view and the CRT view of the filesystem are the
 * same view.
 *
 * WHAT IS REGISTERED (names spelled exactly as in
 * generated/host_imports_table.h):
 *
 *   KERNEL32.dll!CreateFileA          -- 7 args
 *   KERNEL32.dll!GetFileAttributesA   -- 1 arg
 *   KERNEL32.dll!CreateDirectoryA     -- 2 args
 *   KERNEL32.dll!CloseHandle          -- 1 arg  (see SUPERSESSION NOTE)
 *
 * DELIBERATELY NOT REGISTERED
 *   KERNEL32.dll!FindFirstFileW / FindNextFileW / FindClose — host_ext_misc.c
 *   already implements all three over the same data root (UTF-16 pattern ->
 *   UTF-8, drive prefix dropped, ".." refused, token table MISC_FIND_BASE
 *   0x00040000, WIN32_FIND_DATAW laid out field by field) and host_ohos.c
 *   already lists host_ext_misc in s_ext_providers[], so they are LIVE today.
 *   Lookup in host_ohos.c is first-match over s_ext_providers[] in order, and
 *   this module is meant to be appended LAST (see REGISTRATION), so duplicating
 *   them here would be dead code at best and, if this module were ever moved
 *   ahead of host_ext_misc, would silently replace a working, more complete
 *   implementation with a thinner one. They are not duplicated.
 *   FindFirstFileA / FindNextFileA do not exist in the guest's IAT at all.
 *
 * WHAT THE GUEST ACTUALLY IMPORTS (checked, not assumed)
 * generated/host_imports_table.h was searched for the whole KERNEL32 file
 * family: this binary imports no ReadFile, WriteFile, SetFilePointer,
 * GetFileSize, FlushFileBuffers, LockFileEx/UnlockFileEx (hDUMMY, never
 * called), DeleteFileA, MoveFileA or RemoveDirectoryA. CreateFileA, CloseHandle
 * and GetFileSize do not appear in the measured baseline's call trace at all
 * until much later stages (CreateFileA: 0 calls), so the handle this module
 * hands out is today only ever closed, never read or written through. The
 * handle table is nevertheless a real table with real fds, because that is the
 * contract the module must keep when the engine does start using it.
 *
 * SUPERSESSION NOTE (CloseHandle) — read this before comparing with the tree
 * KERNEL32.dll!CloseHandle is claimed by host_ohos.c's h_CloseHandle() through
 * the generated table, and it is NOT in s_host_overrides. An extension module
 * is consulted before the generated table, so registering it here replaces
 * that handler, and the rule for the caller is therefore explicit:
 *
 *   host_ext_path's CloseHandle WINS over h_CloseHandle once registered.
 *
 * Why claim it at all: h_CloseHandle() serves a file-static table in
 * host_ohos.c (HANDLE_BASE 0x10000, 256 slots) that no other translation unit
 * can reach. A token issued by this module is not in that table, so
 * h_CloseHandle() would resolve it to nothing and simply leak the fd this
 * module opened — the one thing a handle table exists to prevent. The handler
 * below therefore retires THIS module's tokens. Handles that belong to
 * host_ohos.c's own table (the CRT's event / thread / console pseudo handles
 * from h_CreateEventW, h_CreateThread, h_GetStdHandle) cannot be freed from
 * here; they are logged at LOG_ERROR and reported as closed, never silently
 * dropped, and the measured baseline closes exactly ONE handle in the whole
 * run, so at most one 256-slot table entry is left allocated per run. If a
 * later module exposes host_ohos.c's table, the foreign branch below is the
 * single place to delegate.
 *
 * REGISTRATION — read this before concluding that nothing changed.
 * host_ohos.c's guest_host_import() reaches an extension module only through
 * s_ext_providers[]; a module that owns no slot there is dead code, however
 * correct its handlers. This file returns host_ext_path() and edits no other
 * file: adding it to s_ext_providers[] (and declaring it in host_ext.h) is a
 * two-line change in two files this module may not touch. APPEND IT LAST:
 * lookup is first-match in provider order, and appending keeps host_ext_misc's
 * Find* family, host_ext_file's stdio family and host_kage's slot-1 token
 * encoding exactly as they are.
 *
 * The line the dispatcher cannot silence is the constructor below, and
 * host_ext_path() logs once when the dispatcher first calls it, so the harness
 * log either contains "host_ext_path: dispatcher consulted this module"
 * (registered) or it does not (inert). Nothing here ever pretends to be
 * installed.
 *
 * CONVENTIONS (identical to host_ohos.c / host_ext_file.c / host_ext_misc.c)
 *   argument i = ld32(esp + 4 + i*4)      esp points at the return address
 *   stdcall    = esp += 4 + nargs*4       the callee cleans the arguments
 *   result     = eax; failures take the Windows sentinel (INVALID_HANDLE_VALUE
 *                0xFFFFFFFF, INVALID_FILE_ATTRIBUTES 0xFFFFFFFF, FALSE) with
 *                c->last_error set to a real ERROR_* code, which the guest
 *                reads back through GetLastError.
 * Guest pointers are arena offsets, and a guest address is a valid host address
 * (isaac_arena_base + addr), so the path argument is copied out of the arena and
 * nothing native is ever handed to the guest.
 *
 * PATH RULES — a re-implementation, not a copy
 * host_ext_file.c's file_normalise()/file_mkdirs()/file_data_root() are
 * file-static and no header exposes them, so the rules are reimplemented here
 * byte for byte in behaviour:
 *   - root is ISAAC_DATA_ROOT, or "isaac-data" when unset;
 *   - '\\' -> '/';
 *   - any ".." component is refused outright, before anything else;
 *   - a path already spelling "<root>/..." (the guest echoing back the
 *     directory we handed it) is used verbatim;
 *   - otherwise a leading "X:" drive prefix is dropped with a log, then every
 *     leading '/' and "./" is stripped, and what remains is JOINED onto the
 *     root — so an absolute-looking guest path is re-rooted inside the data
 *     root and can never name anything outside it;
 *   - a ':' surviving that (e.g. an NTFS alternate data stream) is refused;
 *   - an empty path, or one longer than the buffer, is refused.
 * Write modes create the missing parent directories with mkdir -p semantics
 * (host_ext_file.c does the same for fopen), because the game writes its save
 * and log files under nested directories that need not exist yet. Nothing this
 * module creates is ever outside ISAAC_DATA_ROOT.
 *
 * What is NOT mirrored: host_ext_file.c's _access() has a fallback that probes
 * the guest's path as spelled when it is outside the root. That fallback is a
 * property of _access alone and is not reproduced here; every path these
 * handlers touch is inside the root or refused with a log.
 */

#include <errno.h>
#include <fcntl.h>
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

/* O_DIRECTORY is POSIX.1-2008, but spell the fallback out rather than let a
 * bare-metal toolchain fail to compile a handler that never runs there. */
#ifndef O_DIRECTORY
#define O_DIRECTORY 0
#endif

#include <hilog/log.h>
/* hilog privacy qualifiers are mandatory in format strings. */
#define PATH_ERR(fmt, ...) \
    OH_LOG_Print(LOG_APP, LOG_ERROR, 0, "isaacoh", fmt, ##__VA_ARGS__)
#define PATH_INFO(fmt, ...) \
    OH_LOG_Print(LOG_APP, LOG_INFO, 0, "isaacoh", fmt, ##__VA_ARGS__)

/* One INFO line per handler on its first call, for the same reason
 * host_ext_file.c does it: a resource load makes thousands of calls, and the
 * first one is enough to prove the handler is live. Every failure logs
 * unconditionally. */
#define PATH_ANNOUNCE(msg, ...) \
    do { static int path_once_; \
         if (!path_once_) { path_once_ = 1; PATH_INFO(msg, ##__VA_ARGS__); } \
    } while (0)

/* ---------------------------------------------------------------- helpers */

static inline uint32_t path_arg(CPU *__restrict c, int index)
{
    return ld32(c->esp + 4U + (uint32_t)index * 4U);
}

static inline void path_stdcall_ret(CPU *__restrict c, int nargs)
{
    c->esp += 4U + (uint32_t)nargs * 4U;
}

#define PATH_PATH_MAX 1024U

/* Bounded read of a NUL-terminated guest string into host memory. Returns 0 on
 * success, -1 when the pointer is NULL, the string is longer than cap-1 bytes,
 * or it is unterminated. The caller logs, because only it knows the argument. */
static int path_guest_string(uint32_t addr, char *out, size_t cap)
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

static const char *path_data_root(void)
{
    const char *env = getenv("ISAAC_DATA_ROOT");
    return (env != NULL && env[0] != '\0') ? env : "isaac-data";
}

/* Refuse ".." outright: the guest may not climb out of its data root. */
static int path_has_parent_component(const char *path)
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

/* Guest path -> host path contained in the data root, exactly as
 * host_ext_file.c's file_normalise() does it. Returns 0 on success (out holds
 * the host path), -1 when refused (and logs why). */
static int path_normalise(const char *raw, const char *what,
                          char *out, size_t cap, int *rooted_out)
{
    char work[PATH_PATH_MAX];
    const char *root = path_data_root();
    size_t root_len = strlen(root);
    size_t raw_len = strlen(raw);
    const char *rest;
    size_t i;

    *rooted_out = 0;
    if (raw_len == 0U) {
        PATH_ERR("%{public}s: empty path", what);
        return -1;
    }
    if (raw_len >= sizeof work) {
        PATH_ERR("%{public}s: path is %u bytes, longer than the %u-byte "
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
    if (path_has_parent_component(work)) {
        PATH_ERR("%{public}s: refusing \"%{public}s\": \"..\" leaves the data "
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
            PATH_ERR("%{public}s: path \"%{public}s\" is longer than %u bytes",
                     what, rest, (unsigned)cap);
            return -1;
        }
        memcpy(out, rest, rest_len + 1U);
        *rooted_out = 1;
        return 0;
    }

    /* Relative (or absolute-looking): drop a drive prefix, then every leading
     * '/' or "./" — what follows is a path INSIDE the root by construction,
     * never an absolute path that could point elsewhere. */
    if (((rest[0] >= 'A' && rest[0] <= 'Z') ||
         (rest[0] >= 'a' && rest[0] <= 'z')) && rest[1] == ':') {
        PATH_INFO("%{public}s: dropping drive prefix \"%{public}c:\" from "
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
        PATH_ERR("%{public}s: \"%{public}s\" names no file", what, raw);
        return -1;
    }
    for (i = 0U; rest[i] != '\0'; ++i) {
        if (rest[i] == ':') {
            PATH_ERR("%{public}s: refusing \"%{public}s\": ':' inside the "
                     "path is not part of the data root", what, raw);
            return -1;
        }
    }

    {
        size_t rest_len = strlen(rest);
        if (root_len + 1U + rest_len >= cap) {
            PATH_ERR("%{public}s: \"%{public}s/%{public}s\" is longer than %u "
                     "bytes", what, root, rest, (unsigned)cap);
            return -1;
        }
        memcpy(out, root, root_len);
        out[root_len] = '/';
        memcpy(out + root_len + 1U, rest, rest_len + 1U);
    }
    return 0;
}

static int path_mkdir_one(const char *dir, const char *what)
{
    struct stat st;

    if (mkdir(dir, 0777) == 0) {
        PATH_INFO("%{public}s: created directory \"%{public}s\"", what, dir);
        return 0;
    }
    if (errno == EEXIST && stat(dir, &st) == 0 && S_ISDIR(st.st_mode)) {
        return 0;                       /* already there */
    }
    PATH_ERR("%{public}s: mkdir(\"%{public}s\") failed (errno=%d)", what, dir,
             errno);
    return -1;
}

/* mkdir -p for the parent components of a path. Only the '/' separators are
 * expanded, so the last component (the file itself) is never created. */
static int path_mkdirs(const char *path, const char *what)
{
    char work[PATH_PATH_MAX];
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
        if (path_mkdir_one(work, what) != 0) {
            work[i] = '/';
            errno = saved;
            return -1;
        }
        errno = saved;
        work[i] = '/';
    }
    return 0;
}

/* ------------------------------------------------------- Windows errors ---
 * KERNEL32 reports failures through GetLastError(), not errno, and the guest
 * reads that back (h_GetLastError). h_CreateFileA() and h_CreateDirectoryA()
 * currently store the raw POSIX errno there, which is a different numbering
 * for most values (POSIX EEXIST(17) is ERROR_NOT_SAME_DEVICE; Windows
 * ERROR_ALREADY_EXISTS is 183) — a guest that checks for a specific code is
 * then told something else entirely. The mapping below is spelled out value by
 * value; an errno with no vetted equivalent becomes ERROR_GEN_FAILURE(31) and
 * is logged with the host value, never guessed.
 */
#define PATH_ERROR_FILE_NOT_FOUND     2U
#define PATH_ERROR_PATH_NOT_FOUND     3U
#define PATH_ERROR_TOO_MANY_OPEN      4U
#define PATH_ERROR_ACCESS_DENIED      5U
#define PATH_ERROR_INVALID_HANDLE     6U
#define PATH_ERROR_NOT_ENOUGH_MEMORY  8U
#define PATH_ERROR_WRITE_PROTECT      19U
#define PATH_ERROR_GEN_FAILURE        31U
#define PATH_ERROR_FILE_EXISTS        80U
#define PATH_ERROR_INVALID_PARAMETER  87U
#define PATH_ERROR_DISK_FULL          112U
#define PATH_ERROR_DIR_NOT_EMPTY      145U
#define PATH_ERROR_ALREADY_EXISTS     183U
#define PATH_ERROR_FILENAME_EXCED     206U

static uint32_t path_win32_error(int host_errno)
{
    switch (host_errno) {
    case 0:                 return 0U;
    case EPERM:             return PATH_ERROR_ACCESS_DENIED;
    case ENOENT:            return PATH_ERROR_FILE_NOT_FOUND;
    case EIO:               return PATH_ERROR_GEN_FAILURE;
    case EBADF:             return PATH_ERROR_INVALID_HANDLE;
    case ENOMEM:            return PATH_ERROR_NOT_ENOUGH_MEMORY;
    case EACCES:            return PATH_ERROR_ACCESS_DENIED;
    case EEXIST:            return PATH_ERROR_ALREADY_EXISTS;
    case ENOTDIR:           return PATH_ERROR_PATH_NOT_FOUND;
    case EISDIR:            return PATH_ERROR_ACCESS_DENIED;
    case EINVAL:            return PATH_ERROR_INVALID_PARAMETER;
    case EMFILE:            return PATH_ERROR_TOO_MANY_OPEN;
    case ENFILE:            return PATH_ERROR_TOO_MANY_OPEN;
    case EFBIG:             return PATH_ERROR_DISK_FULL;
    case ENOSPC:            return PATH_ERROR_DISK_FULL;
    case EROFS:             return PATH_ERROR_WRITE_PROTECT;
    case ENAMETOOLONG:      return PATH_ERROR_FILENAME_EXCED;
    case ELOOP:             return PATH_ERROR_GEN_FAILURE;
    case ENOTEMPTY:         return PATH_ERROR_DIR_NOT_EMPTY;
    default:
        PATH_ERR("last_error: host errno %d has no vetted ERROR_* equivalent; "
                 "reporting ERROR_GEN_FAILURE(%u)", host_errno,
                 PATH_ERROR_GEN_FAILURE);
        return PATH_ERROR_GEN_FAILURE;
    }
}

/* ------------------------------------------------------------ slot table ---
 * The guest sees opaque tokens from this table, never a native fd or FILE*.
 * Same shape as host_ext_file.c's stream table: the token is
 * PATH_TOKEN_BASE + (generation << 6) + index, so a token from a closed (or
 * never opened) handle is detected — the slot's generation no longer matches —
 * instead of being dereferenced. 64 handles is far more than the guest's file
 * API surface needs, and a slot is reused only after its generation moved on.
 *
 * The base is chosen in a gap no other table occupies: 0x00010000
 * (host_ohos.c HANDLE_BASE and host_ext_printf.c's IOB tokens), 0x00040000
 * (host_ext_misc.c's searches), 0x00050000-0x0008FFFF (host_ext_file.c's
 * streams), 0x7D000000 (GetProcAddress tokens) and 0x7D020000 (host_kage's GL
 * tokens) are all disjoint from 0x00060000-0x0009FFFF.
 */
#define PATH_SLOT_COUNT 64U
#define PATH_TOKEN_BASE 0x00060000U
#define PATH_TOKEN_SPAN (1U << 18)
#define PATH_TOKEN_INDEX_MASK (PATH_SLOT_COUNT - 1U)
#define PATH_TOKEN_GEN_MASK 0xFFFU

typedef struct {
    int      fd;                        /* native descriptor, host side only */
    uint32_t generation;
    uint8_t  used;
    char     path[PATH_PATH_MAX];       /* host path, for the log */
} path_slot;

static path_slot g_path_slots[PATH_SLOT_COUNT];
static uint32_t g_path_open_serial;

static uint32_t path_token_make(unsigned index, uint32_t generation)
{
    return PATH_TOKEN_BASE | ((generation & PATH_TOKEN_GEN_MASK) << 6) |
           (uint32_t)index;
}

/* Resolve a guest token. NULL for anything this module did not issue (or
 * issued and has since reused); the caller logs, nothing is dereferenced. */
static path_slot *path_slot_get(uint32_t token, unsigned *index_out)
{
    uint32_t rel;
    uint32_t index;
    uint32_t generation;

    if (token < PATH_TOKEN_BASE) {
        return NULL;
    }
    rel = token - PATH_TOKEN_BASE;
    if (rel >= PATH_TOKEN_SPAN) {
        return NULL;
    }
    index = rel & PATH_TOKEN_INDEX_MASK;
    generation = (rel >> 6) & PATH_TOKEN_GEN_MASK;
    if (!g_path_slots[index].used || g_path_slots[index].generation != generation) {
        return NULL;
    }
    if (index_out != NULL) {
        *index_out = index;
    }
    return &g_path_slots[index];
}

static int path_slot_alloc(int fd, const char *host_path, uint32_t *token_out)
{
    unsigned i;

    for (i = 0U; i < PATH_SLOT_COUNT; ++i) {
        if (!g_path_slots[i].used) {
            path_slot *slot = &g_path_slots[i];
            slot->generation = (slot->generation + 1U) & PATH_TOKEN_GEN_MASK;
            slot->used = 1U;
            slot->fd = fd;
            slot->path[0] = '\0';
            if (host_path != NULL) {
                snprintf(slot->path, sizeof slot->path, "%s", host_path);
            }
            *token_out = path_token_make(i, slot->generation);
            return 0;
        }
    }
    PATH_ERR("CreateFileA: all %u handles are in use and none was ever "
             "closed; \"%{public}s\" not opened", PATH_SLOT_COUNT,
             host_path != NULL ? host_path : "?");
    return -1;
}

static void path_slot_release(path_slot *slot)
{
    slot->fd = -1;
    slot->used = 0U;
    slot->path[0] = '\0';
}

/* Open accounting: the guest's resource layer can open thousands of files, so
 * full-path INFO lines are emitted for the first PATH_LOG_FIRST opens and then
 * one in PATH_LOG_EVERY, with a one-time notice. Failures are never rate
 * limited. */
#define PATH_LOG_FIRST 128U
#define PATH_LOG_EVERY 64U

static int path_open_should_log(uint32_t serial)
{
    return serial <= PATH_LOG_FIRST || (serial % PATH_LOG_EVERY) == 0U;
}

static void path_open_rate_notice(uint32_t serial)
{
    if (serial == PATH_LOG_FIRST + 1U) {
        PATH_INFO("CreateFileA: %u opens logged; from here one in every %u "
                  "succeeds silently and failures keep logging in full",
                  PATH_LOG_FIRST, PATH_LOG_EVERY);
    }
}

/* -------------------------------------------------------------- CreateFileA
 * HANDLE WINAPI CreateFileA(LPCSTR lpFileName, DWORD dwDesiredAccess,
 *     DWORD dwShareMode, LPSECURITY_ATTRIBUTES lpSecurityAttributes,
 *     DWORD dwCreationDisposition, DWORD dwFlagsAndAttributes,
 *     HANDLE hTemplateFile);          -- 7 args, stdcall
 *
 * The path is rooted first, then the disposition picks the POSIX open() flags:
 *   CREATE_NEW(1)        O_CREAT|O_EXCL        existing -> ERROR_FILE_EXISTS(80)
 *   CREATE_ALWAYS(2)     O_CREAT|O_TRUNC
 *   OPEN_EXISTING(3)     (no create)           missing  -> ERROR_FILE_NOT_FOUND
 *   OPEN_ALWAYS(4)       O_CREAT               existing -> succeeds, and
 *                                              GetLastError()=ERROR_ALREADY_
 *                                              EXISTS(183) exactly as Windows
 *                                              reports it
 *   TRUNCATE_EXISTING(5) O_TRUNC               requires write access, else
 *                                              ERROR_ACCESS_DENIED(5)
 * Any other disposition is not a Windows constant and is refused with
 * ERROR_INVALID_PARAMETER(87) rather than guessed at.
 *
 * dwDesiredAccess maps onto O_RDONLY / O_WRONLY / O_RDWR. GENERIC_READ and
 * GENERIC_WRITE are the two the CRT uses, but the specific rights bits
 * (FILE_READ_DATA, FILE_WRITE_DATA, FILE_APPEND_DATA, DELETE) are accepted too,
 * because a guest that spells "FILE_APPEND_DATA|SYNCHRONIZE" means the same
 * thing. Access 0 (query attributes only) opens read-only, which is what
 * Windows does.
 *
 * Writes land at end-of-file when FILE_APPEND_DATA is requested without
 * FILE_WRITE_DATA, so the guest's append-style logs behave. Write modes get
 * their missing parent directories created first (host_ext_file.c does the
 * same for fopen), which is what lets the game's nested save files appear.
 *
 * dwShareMode is ignored: POSIX has no mandatory sharing, and the guest is the
 * only process in this arena. It is logged on the first call rather than
 * silently dropped. dwFlagsAndAttributes is honoured for FILE_FLAG_BACKUP_
 * SEMANTICS (a directory may only be opened with it, as on Windows); the
 * attribute bits themselves are logged, not silently reinterpreted — every file
 * this module creates is mode 0644.
 */
#define PATH_GENERIC_READ      0x80000000U
#define PATH_GENERIC_WRITE     0x40000000U
#define PATH_FILE_READ_DATA    0x00000001U
#define PATH_FILE_WRITE_DATA   0x00000002U
#define PATH_FILE_APPEND_DATA  0x00000004U
#define PATH_FILE_DELETE       0x00010000U
#define PATH_FILE_READ_ATTRS   0x00000080U
#define PATH_FILE_FLAG_BACKUP_SEMANTICS 0x02000000U

#define PATH_CREATE_NEW        1U
#define PATH_CREATE_ALWAYS     2U
#define PATH_OPEN_EXISTING     3U
#define PATH_OPEN_ALWAYS       4U
#define PATH_TRUNCATE_EXISTING 5U

static void h_path_create_file_a(CPU *c)
{
    char raw_path[PATH_PATH_MAX];
    char host[PATH_PATH_MAX];
    uint32_t access = path_arg(c, 1);
    uint32_t share = path_arg(c, 2);
    uint32_t creation = path_arg(c, 4);
    uint32_t flags = path_arg(c, 5);
    const char *what = "CreateFileA";
    int rooted = 0;
    int wants_write;
    int wants_read;
    int flags_posix;
    int fd;
    uint32_t token = 0U;
    uint32_t win32_error = 0U;
    int entry_errno = errno;

    if (path_guest_string(path_arg(c, 0), raw_path, sizeof raw_path) != 0) {
        PATH_ERR("CreateFileA: unusable path pointer 0x%{public}08x",
                 path_arg(c, 0));
        c->last_error = PATH_ERROR_INVALID_PARAMETER;
        c->eax = 0xFFFFFFFFU;           /* INVALID_HANDLE_VALUE */
        path_stdcall_ret(c, 7);
        return;
    }
    PATH_ANNOUNCE("CreateFileA: first call (guest \"%{public}s\", access "
                  "0x%{public}08x, share 0x%{public}08x, disposition "
                  "%{public}u, flags 0x%{public}08x)",
                  raw_path, access, share, creation, flags);

    if (creation < PATH_CREATE_NEW || creation > PATH_TRUNCATE_EXISTING) {
        PATH_ERR("CreateFileA: \"%{public}s\": disposition %{public}u is not "
                 "CREATE_NEW/ALWAYS/OPEN_EXISTING/OPEN_ALWAYS/TRUNCATE_"
                 "EXISTING -> INVALID_HANDLE_VALUE (ERROR_INVALID_PARAMETER)",
                 raw_path, creation);
        c->last_error = PATH_ERROR_INVALID_PARAMETER;
        c->eax = 0xFFFFFFFFU;
        path_stdcall_ret(c, 7);
        return;
    }

    if (path_normalise(raw_path, what, host, sizeof host, &rooted) != 0) {
        c->last_error = PATH_ERROR_FILE_NOT_FOUND;
        c->eax = 0xFFFFFFFFU;
        path_stdcall_ret(c, 7);
        return;
    }

    wants_write = ((access & (PATH_GENERIC_WRITE | PATH_FILE_WRITE_DATA |
                              PATH_FILE_APPEND_DATA | PATH_FILE_DELETE)) != 0U);
    wants_read = ((access & (PATH_GENERIC_READ | PATH_FILE_READ_DATA |
                             PATH_FILE_READ_ATTRS)) != 0U) ||
                 (access == 0U);
    /* FILE_WRITE_DATA|FILE_APPEND_DATA without write access is the append
     * case; both are covered by wants_write above. */

    if (creation == PATH_TRUNCATE_EXISTING && !wants_write) {
        PATH_ERR("CreateFileA: \"%{public}s\" (host \"%{public}s\") asks to "
                 "truncate without write access -> INVALID_HANDLE_VALUE "
                 "(ERROR_ACCESS_DENIED)", raw_path, host);
        c->last_error = PATH_ERROR_ACCESS_DENIED;
        c->eax = 0xFFFFFFFFU;
        path_stdcall_ret(c, 7);
        return;
    }

    if (wants_write) {
        /* mkdir -p: only the parent components, never the file itself. */
        if (path_mkdirs(host, what) != 0) {
            PATH_ERR("CreateFileA: could not create the parent directories of "
                     "\"%{public}s\" (guest \"%{public}s\") -> "
                     "INVALID_HANDLE_VALUE (ERROR_ACCESS_DENIED)", host,
                     raw_path);
            c->last_error = PATH_ERROR_ACCESS_DENIED;
            c->eax = 0xFFFFFFFFU;
            errno = entry_errno;
            path_stdcall_ret(c, 7);
            return;
        }
    }

    flags_posix = wants_write ? (wants_read ? O_RDWR : O_WRONLY) : O_RDONLY;
    if ((flags & PATH_FILE_FLAG_BACKUP_SEMANTICS) != 0U) {
        /* The only Windows flag with a filesystem-visible meaning here: it is
         * what lets CreateFile open a DIRECTORY. Without it a directory is
         * refused below with ERROR_ACCESS_DENIED, as on Windows. */
        flags_posix |= O_RDONLY | O_DIRECTORY;
    }

    errno = 0;
    switch (creation) {
    case PATH_CREATE_NEW:
        fd = open(host, flags_posix | O_CREAT | O_EXCL, 0644);
        break;
    case PATH_CREATE_ALWAYS:
        fd = open(host, flags_posix | O_CREAT | O_TRUNC, 0644);
        break;
    case PATH_TRUNCATE_EXISTING:
        fd = open(host, flags_posix | O_TRUNC);
        break;
    case PATH_OPEN_ALWAYS:
        /* Windows reports ERROR_ALREADY_EXISTS through GetLastError() while
         * still succeeding; O_EXCL first is the only way to learn that without
         * a race. */
        fd = open(host, flags_posix | O_CREAT | O_EXCL, 0644);
        if (fd < 0 && errno == EEXIST) {
            fd = open(host, flags_posix);
            if (fd >= 0) {
                win32_error = PATH_ERROR_ALREADY_EXISTS;
            }
        }
        break;
    default:                            /* PATH_OPEN_EXISTING */
        fd = open(host, flags_posix);
        break;
    }

    if (fd < 0) {
        int host_errno = errno != 0 ? errno : EIO;
        win32_error = path_win32_error(host_errno);
        if (creation == PATH_CREATE_NEW && host_errno == EEXIST) {
            win32_error = PATH_ERROR_FILE_EXISTS;   /* Windows spells this one
                                                     * differently from the
                                                     * already-exists error */
        }
        PATH_ERR("CreateFileA: \"%{public}s\" (guest \"%{public}s\", "
                 "disposition %{public}u, access 0x%{public}08x%{public}s) -> "
                 "INVALID_HANDLE_VALUE (errno=%d, last_error=%u)",
                 host, raw_path, creation, access,
                 rooted ? ", already rooted" : "", host_errno, win32_error);
        c->last_error = win32_error;
        c->eax = 0xFFFFFFFFU;
        errno = entry_errno;
        path_stdcall_ret(c, 7);
        return;
    }

    /* Windows refuses to open a directory without FILE_FLAG_BACKUP_SEMANTICS;
     * glibc opens one happily and the guest is then handed a handle whose
     * every use fails. Refuse it here instead of inventing a file. */
    if ((flags & PATH_FILE_FLAG_BACKUP_SEMANTICS) == 0U) {
        struct stat st;
        if (fstat(fd, &st) == 0 && S_ISDIR(st.st_mode)) {
            close(fd);
            PATH_ERR("CreateFileA: \"%{public}s\" is a directory and no "
                     "FILE_FLAG_BACKUP_SEMANTICS was given -> "
                     "INVALID_HANDLE_VALUE (ERROR_ACCESS_DENIED)", host);
            c->last_error = PATH_ERROR_ACCESS_DENIED;
            c->eax = 0xFFFFFFFFU;
            errno = entry_errno;
            path_stdcall_ret(c, 7);
            return;
        }
    }

    if (path_slot_alloc(fd, host, &token) != 0) {
        close(fd);
        c->last_error = PATH_ERROR_TOO_MANY_OPEN;
        c->eax = 0xFFFFFFFFU;
        errno = entry_errno;
        path_stdcall_ret(c, 7);
        return;
    }
    g_path_open_serial++;
    if (path_open_should_log(g_path_open_serial)) {
        PATH_INFO("CreateFileA[%{public}u]: \"%{public}s\" -> \"%{public}s\" "
                  "(disposition %{public}u, access 0x%{public}08x%{public}s) "
                  "token=0x%{public}08x", g_path_open_serial, raw_path, host,
                  creation, access, rooted ? ", rooted" : "", token);
    } else {
        path_open_rate_notice(g_path_open_serial);
    }
    c->last_error = win32_error;        /* 0, or ERROR_ALREADY_EXISTS */
    c->eax = token;
    errno = entry_errno;
    path_stdcall_ret(c, 7);
}

/* ------------------------------------------------------ GetFileAttributesA
 * DWORD WINAPI GetFileAttributesA(LPCSTR lpFileName);    -- 1 arg, stdcall
 * FILE_ATTRIBUTE_DIRECTORY (0x10) for a directory, FILE_ATTRIBUTE_ARCHIVE
 * (0x20) for a file, INVALID_FILE_ATTRIBUTES (0xFFFFFFFF) plus
 * ERROR_FILE_NOT_FOUND when the rooted stat fails. The old handler stat()ed the
 * raw guest string against the harness's own cwd, so every relative guest path
 * answered "does not exist" even when fopen had just created the same path
 * under the data root.
 */
static void h_path_get_file_attributes_a(CPU *c)
{
    char raw_path[PATH_PATH_MAX];
    char host[PATH_PATH_MAX];
    struct stat st;
    const char *what = "GetFileAttributesA";
    int rooted = 0;

    if (path_guest_string(path_arg(c, 0), raw_path, sizeof raw_path) != 0) {
        PATH_ERR("GetFileAttributesA: unusable path pointer 0x%{public}08x",
                 path_arg(c, 0));
        c->last_error = PATH_ERROR_INVALID_PARAMETER;
        c->eax = 0xFFFFFFFFU;           /* INVALID_FILE_ATTRIBUTES */
        path_stdcall_ret(c, 1);
        return;
    }
    PATH_ANNOUNCE("GetFileAttributesA: first call (guest \"%{public}s\")",
                  raw_path);

    if (path_normalise(raw_path, what, host, sizeof host, &rooted) != 0) {
        c->last_error = PATH_ERROR_FILE_NOT_FOUND;
        c->eax = 0xFFFFFFFFU;
        path_stdcall_ret(c, 1);
        return;
    }

    errno = 0;
    if (stat(host, &st) != 0) {
        int host_errno = errno != 0 ? errno : EIO;
        uint32_t win32_error = path_win32_error(host_errno);
        PATH_ERR("GetFileAttributesA: \"%{public}s\" (guest \"%{public}s\""
                 "%{public}s) -> INVALID_FILE_ATTRIBUTES (errno=%d, "
                 "last_error=%u)", host, raw_path, rooted ? ", already rooted"
                 : "", host_errno, win32_error);
        c->last_error = win32_error;
        c->eax = 0xFFFFFFFFU;
        path_stdcall_ret(c, 1);
        return;
    }

    if (S_ISDIR(st.st_mode)) {
        c->eax = 0x10U;                 /* FILE_ATTRIBUTE_DIRECTORY */
    } else {
        c->eax = 0x20U;                 /* FILE_ATTRIBUTE_ARCHIVE */
    }
    PATH_INFO("GetFileAttributesA: guest \"%{public}s\" -> host "
              "\"%{public}s\"%{public}s -> 0x%{public}08x", raw_path, host,
              rooted ? " (already rooted)" : "", c->eax);
    path_stdcall_ret(c, 1);
}

/* -------------------------------------------------------- CreateDirectoryA
 * BOOL WINAPI CreateDirectoryA(LPCSTR lpPathName,
 *     LPSECURITY_ATTRIBUTES lpSecurityAttributes);        -- 2 args, stdcall
 *
 * mkdir -p for the whole rooted path, because the game creates its save tree
 * one level at a time and not always from the top. TRUE on success; FALSE with
 * ERROR_ALREADY_EXISTS(183) when the directory is already there (which is what
 * Windows returns, and the reason the pair GetFileAttributesA +
 * CreateDirectoryA is the guest's usual probe). Any other failure keeps its
 * mapped ERROR_* code — the old handler stored the raw POSIX errno there.
 */
static void h_path_create_directory_a(CPU *c)
{
    char raw_path[PATH_PATH_MAX];
    char host[PATH_PATH_MAX];
    struct stat st;
    const char *what = "CreateDirectoryA";
    int rooted = 0;

    if (path_guest_string(path_arg(c, 0), raw_path, sizeof raw_path) != 0) {
        PATH_ERR("CreateDirectoryA: unusable path pointer 0x%{public}08x",
                 path_arg(c, 0));
        c->last_error = PATH_ERROR_INVALID_PARAMETER;
        c->eax = 0U;                    /* FALSE */
        path_stdcall_ret(c, 2);
        return;
    }
    PATH_ANNOUNCE("CreateDirectoryA: first call (guest \"%{public}s\")",
                  raw_path);

    if (path_normalise(raw_path, what, host, sizeof host, &rooted) != 0) {
        c->last_error = PATH_ERROR_PATH_NOT_FOUND;
        c->eax = 0U;
        path_stdcall_ret(c, 2);
        return;
    }

    errno = 0;
    if (mkdir(host, 0777) == 0) {
        PATH_INFO("CreateDirectoryA: \"%{public}s\" (guest \"%{public}s\""
                  "%{public}s) created", host, raw_path,
                  rooted ? ", already rooted" : "");
        c->last_error = 0U;
        c->eax = 1U;                    /* TRUE */
        path_stdcall_ret(c, 2);
        return;
    }

    if (errno == EEXIST && stat(host, &st) == 0 && S_ISDIR(st.st_mode)) {
        PATH_INFO("CreateDirectoryA: \"%{public}s\" (guest \"%{public}s\") "
                  "already exists -> FALSE (ERROR_ALREADY_EXISTS)", host,
                  raw_path);
        c->last_error = PATH_ERROR_ALREADY_EXISTS;
        c->eax = 0U;
        path_stdcall_ret(c, 2);
        return;
    }

    {
        int host_errno = errno != 0 ? errno : EIO;
        uint32_t win32_error = path_win32_error(host_errno);
        PATH_ERR("CreateDirectoryA: mkdir(\"%{public}s\") for guest "
                 "\"%{public}s\" failed (errno=%d, last_error=%u)", host,
                 raw_path, host_errno, win32_error);
        c->last_error = win32_error;
        c->eax = 0U;
    }
    path_stdcall_ret(c, 2);
}

/* -------------------------------------------------------------- CloseHandle
 * BOOL WINAPI CloseHandle(HANDLE hObject);                -- 1 arg, stdcall
 *
 * Retires a handle this module issued, so the fd behind it is closed and its
 * slot can be reused. See the SUPERSESSION NOTE at the top of this file: this
 * handler replaces host_ohos.c's h_CloseHandle(), whose file-static table is
 * not reachable from here, so a handle that does NOT belong to this module is
 * logged loudly and answered TRUE without being freed — never silently
 * treated as if it had been closed. Zero and INVALID_HANDLE_VALUE are refused
 * the way Windows refuses them, with ERROR_INVALID_HANDLE.
 */
static void h_path_close_handle(CPU *c)
{
    uint32_t token = path_arg(c, 0);
    unsigned index = 0U;
    path_slot *slot = path_slot_get(token, &index);
    int entry_errno = errno;
    int rc;

    if (slot == NULL) {
        if (token == 0U || token == 0xFFFFFFFFU) {
            PATH_ERR("CloseHandle: 0x%{public}08x is not a valid handle -> "
                     "FALSE (ERROR_INVALID_HANDLE)", token);
        } else {
            PATH_ERR("CloseHandle: 0x%{public}08x was not issued by this "
                     "module (it is not one of its %u handle slots, or it was "
                     "already closed). host_ohos.c's own handle table is "
                     "file-static and unreachable from here, so nothing was "
                     "closed -> reported TRUE to keep the guest running",
                     token, PATH_SLOT_COUNT);
        }
        c->last_error = PATH_ERROR_INVALID_HANDLE;
        c->eax = (token == 0U || token == 0xFFFFFFFFU) ? 0U : 1U;
        errno = entry_errno;
        path_stdcall_ret(c, 1);
        return;
    }

    errno = 0;
    rc = close(slot->fd);
    if (rc != 0) {
        int host_errno = errno != 0 ? errno : EIO;
        PATH_ERR("CloseHandle: close(\"%{public}s\") (handle 0x%{public}08x) "
                 "failed (errno=%d) -> FALSE", slot->path, token, host_errno);
        c->last_error = path_win32_error(host_errno);
        path_slot_release(slot);        /* the fd is gone either way */
        errno = entry_errno;
        c->eax = 0U;
        path_stdcall_ret(c, 1);
        return;
    }
    PATH_INFO("CloseHandle: \"%{public}s\" (handle 0x%{public}08x, slot "
              "%{public}u) closed", slot->path, token, index);
    path_slot_release(slot);
    errno = entry_errno;
    c->eax = 1U;                        /* TRUE */
    path_stdcall_ret(c, 1);
}

/* ------------------------------------------------------------- descriptor */

static const host_ext_entry s_entries[] = {
    /* The three KERNEL32 file APIs the guest actually imports (see the file
     * header: no ReadFile/WriteFile/DeleteFileA/MoveFileA in its IAT). */
    { "KERNEL32.dll!CreateFileA", h_path_create_file_a },
    { "KERNEL32.dll!GetFileAttributesA", h_path_get_file_attributes_a },
    { "KERNEL32.dll!CreateDirectoryA", h_path_create_directory_a },
    /* Retires the tokens the entry above hands out. NOT registered:
     * FindFirstFileW/FindNextFileW/FindClose, which host_ext_misc.c already
     * implements over this same root. */
    { "KERNEL32.dll!CloseHandle", h_path_close_handle },
};

static const host_ext_module s_module = {
    s_entries, (unsigned)(sizeof s_entries / sizeof s_entries[0]),
    NULL, 0U,
};

const host_ext_module *host_ext_path(void)
{
    /* Called by host_ohos.c's dispatcher for every import lookup once this
     * function is listed in s_ext_providers[]. The line below is therefore the
     * module's own proof of registration: if the harness log has no
     * "dispatcher consulted this module", the handlers are dead code and
     * KERNEL32 file paths are still unmapped. */
    static int announced;
    if (!announced) {
        announced = 1;
        PATH_INFO("host_ext_path: dispatcher consulted this module "
                  "(%{public}u entries); CreateFileA/GetFileAttributesA/"
                  "CreateDirectoryA/CloseHandle are live and rooted under "
                  "ISAAC_DATA_ROOT",
                  (unsigned)(sizeof s_entries / sizeof s_entries[0]));
    }
    return &s_module;
}

/* The dispatcher only calls host_ext_path() if host_ohos.c lists it in
 * s_ext_providers[], so a module that lost that race would be invisible.
 * This startup line states the module's own state without claiming anything it
 * cannot verify: the provider list is in a file this module may not edit, so
 * silence at startup plus no run-time line above means "not registered". */
__attribute__((constructor)) static void host_ext_path_self_check(void)
{
    PATH_INFO("host_ext_path.c: %u static entries built; reachable only if "
              "host_ohos.c's s_ext_providers[] lists host_ext_path() "
              "(append it LAST so host_ext_misc keeps FindFirstFileW)",
              (unsigned)(sizeof s_entries / sizeof s_entries[0]));
}
