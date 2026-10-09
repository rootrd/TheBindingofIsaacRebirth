/* host_ext_misc.c — the remaining high-value guest imports for the recompiled
 * Isaac: fiber/TLS storage, the two UCRT floating-point channels, the secure
 * string copies and (W) file enumeration.
 *
 * host_ohos.c consults extension modules before generated/host_imports_table.h,
 * so every entry below replaces an hDUMMY (for FindClose, the blanket stub)
 * without touching that file.
 *
 * Guest calling conventions, identical to host_ohos.c:
 *   argument i   = ld32(esp + 4 + i*4)        (esp points at the return address)
 *   stdcall      = esp += 4 + nargs*4         cdecl = esp += 4 (caller cleans)
 *   integer result in eax, x87 result through fpush(), SSE result in x[0].d[0]
 * Guest pointers are arena offsets; native pointers are never handed across.
 */
#include <dirent.h>
#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "guest.h"
#include "host_ext.h"

#include <hilog/log.h>
/* hilog privacy qualifiers are mandatory in format strings. */
#define MISC_ERR(fmt, ...) \
    OH_LOG_Print(LOG_APP, LOG_ERROR, 0, "isaacoh", fmt, ##__VA_ARGS__)
#define MISC_INFO(fmt, ...) \
    OH_LOG_Print(LOG_APP, LOG_INFO, 0, "isaacoh", fmt, ##__VA_ARGS__)

/* ---------------------------------------------------------------- helpers */

static inline uint32_t misc_arg(CPU *__restrict c, int index)
{
    return ld32(c->esp + 4U + (uint32_t)index * 4U);
}

static inline void misc_stdcall_ret(CPU *__restrict c, int nargs)
{
    c->esp += 4U + (uint32_t)nargs * 4U;
}

static inline void misc_cdecl_ret(CPU *__restrict c)
{
    c->esp += 4U;
}

/* A cdecl floating-point argument travels as the raw binary32 bits of a guest
 * stack dword (the guest pushes it with `fstp dword ptr [esp]`). */
static inline float misc_arg_f32(CPU *__restrict c, int index)
{
    uint32_t bits = misc_arg(c, index);
    float value;
    memcpy(&value, &bits, sizeof value);
    return value;
}

/* The arena is one flat 4 GiB mapping, so "inside the arena" is simply
 * "does not wrap". Nothing here ever validates against the image or heap
 * layout; that is host_ohos.c's business. */
static inline int misc_guest_range(uint32_t addr, uint32_t size)
{
    return addr != 0U && addr <= 0xFFFFFFFFU - size;
}

/* ------------------------------------------- A/B: fiber- and thread-local
 * storage.
 *
 * The UCRT keeps its per-thread data (the "ptd" block) in a fiber-local slot,
 * and the boot sequence already reaches FlsAlloc/FlsSetValue, so a guest that
 * gets hDUMMY's 0 back cannot build that block.
 *
 * There is no native TLS here: the harness and the HAP run guest code on one
 * thread and never route a new host thread back into the guest. A
 * process-static array is therefore faithful for this guest, and — unlike
 * pthread_key_create — it can hold a guest VA (a callback) without ever
 * mistaking it for a host pointer.
 *
 * FLS callbacks run on thread exit only. This thread does not exit during the
 * run, so a remembered callback is never invoked; that matches Windows, where
 * the callback for the running thread also has not fired yet.
 *
 * NOTE: 0 is a VALID index. The allocator returns the array index directly,
 * so a caller cannot distinguish "index 0" from "no slots left" except by the
 * documented FLS_OUT_OF_INDEXES (0xFFFFFFFF) sentinel; that ambiguity is
 * Windows' own and is why the exhaustion path logs loudly.
 */
#define MISC_SLOT_COUNT 128U
#define MISC_SLOT_OUT_OF_INDEXES 0xFFFFFFFFU

typedef struct {
    uint32_t callback;      /* guest VA, remembered but never invoked */
    uint32_t value;
    uint8_t  allocated;
} misc_slot;

static misc_slot g_fls_slots[MISC_SLOT_COUNT];
static misc_slot g_tls_slots[MISC_SLOT_COUNT];

static uint32_t misc_slot_alloc(misc_slot *slots, uint32_t callback,
                                const char *what)
{
    uint32_t i;
    for (i = 0U; i < MISC_SLOT_COUNT; ++i) {
        if (!slots[i].allocated) {
            slots[i].allocated = 1U;
            slots[i].callback = callback;
            slots[i].value = 0U;
            MISC_INFO("%{public}s: index %u (callback=0x%{public}08x)",
                      what, i, callback);
            return i;
        }
    }
    MISC_ERR("%{public}s: all %u slots in use -> 0xFFFFFFFF",
             what, MISC_SLOT_COUNT);
    return MISC_SLOT_OUT_OF_INDEXES;
}

static uint32_t misc_slot_get(misc_slot *slots, uint32_t index, const char *what)
{
    if (index >= MISC_SLOT_COUNT || !slots[index].allocated) {
        MISC_ERR("%{public}s: index %u is not an allocated slot -> 0",
                 what, index);
        return 0U;
    }
    return slots[index].value;
}

static uint32_t misc_slot_set(misc_slot *slots, uint32_t index, uint32_t value,
                              const char *what)
{
    if (index >= MISC_SLOT_COUNT || !slots[index].allocated) {
        MISC_ERR("%{public}s: index %u is not an allocated slot "
                 "(value 0x%{public}08x dropped) -> FALSE", what, index, value);
        return 0U;
    }
    slots[index].value = value;
    return 1U;
}

static uint32_t misc_slot_free(misc_slot *slots, uint32_t index, const char *what)
{
    if (index >= MISC_SLOT_COUNT || !slots[index].allocated) {
        MISC_ERR("%{public}s: index %u is not an allocated slot -> FALSE",
                 what, index);
        return 0U;
    }
    slots[index].allocated = 0U;
    slots[index].value = 0U;
    return 1U;
}

static void h_fls_alloc(CPU *c)
{
    c->eax = misc_slot_alloc(g_fls_slots, misc_arg(c, 0), "FlsAlloc");
    misc_stdcall_ret(c, 1);
}

static void h_fls_get_value(CPU *c)
{
    c->eax = misc_slot_get(g_fls_slots, misc_arg(c, 0), "FlsGetValue");
    misc_stdcall_ret(c, 1);
}

static void h_fls_set_value(CPU *c)
{
    c->eax = misc_slot_set(g_fls_slots, misc_arg(c, 0), misc_arg(c, 1),
                           "FlsSetValue");
    misc_stdcall_ret(c, 2);
}

static void h_fls_free(CPU *c)
{
    c->eax = misc_slot_free(g_fls_slots, misc_arg(c, 0), "FlsFree");
    misc_stdcall_ret(c, 1);
}

/* TLS: same shape, and the same single-thread caveat (TLS values would have
 * to be per-thread on Windows; with one thread a flat array is exact). */
static void h_tls_alloc(CPU *c)
{
    c->eax = misc_slot_alloc(g_tls_slots, 0U, "TlsAlloc");
    misc_stdcall_ret(c, 0);
}

static void h_tls_get_value(CPU *c)
{
    c->eax = misc_slot_get(g_tls_slots, misc_arg(c, 0), "TlsGetValue");
    misc_stdcall_ret(c, 1);
}

static void h_tls_set_value(CPU *c)
{
    c->eax = misc_slot_set(g_tls_slots, misc_arg(c, 0), misc_arg(c, 1),
                           "TlsSetValue");
    misc_stdcall_ret(c, 2);
}

static void h_tls_free(CPU *c)
{
    c->eax = misc_slot_free(g_tls_slots, misc_arg(c, 0), "TlsFree");
    misc_stdcall_ret(c, 1);
}

/* ------------------------------------------------------- C: floating point
 *
 * Two different channels exist on x86-32 UCRT, and mixing them up returns
 * plausible garbage, so each is spelled out:
 *
 * 1. The `_libm_sse2_*_precise` family (/fp:precise helpers) is a REGISTER
 *    ABI: the operand is the low binary64 lane of XMM0 (pow gets its exponent
 *    in XMM1) and the result comes back in XMM0. Nothing is on the guest
 *    stack, so the handler only pops the return address. Writing d[0] alone
 *    leaves the undefined upper lane and the other XMM registers alone, which
 *    is all the caller-saved contract requires.
 *
 * 2. Ordinary cdecl libm calls that take/return float or double use the GUEST
 *    STACK for arguments and x87 ST(0) for the return value. The translated
 *    caller proves it; the roundf call at guest RVA 0x0003f1e5 reads
 *        fstp dword ptr [esp]       ; argument
 *        call 0x5ec3b8              ; roundf thunk -> IAT slot 0x00606544
 *        fstp dword ptr [esp+0x1c]  ; result consumed from ST(0)
 *        add  esp, 4                ; caller cleans (cdecl)
 *    and nextafterf at RVA 0x00003c2a does the same with two stacked floats
 *    and `add esp, 8`. Hence fpush() is the return channel for both.
 *
 * The native libm is used only as the semantic backend. Host errno is saved
 * and restored so our probing cannot leak an EDOM/ERANGE into unrelated host
 * code. The GUEST errno is not updated: this project's _errno() handler owns
 * its own arena word, and writing a second copy somewhere else would be a
 * plausible-looking lie.
 */
typedef double (*misc_double_fn)(double);

static void misc_unary_double(CPU *c, misc_double_fn operation)
{
    int saved_errno = errno;
    double result;

    errno = 0;
    result = operation(c->x[0].d[0]);
    errno = saved_errno;
    c->x[0].d[0] = result;
    misc_cdecl_ret(c);          /* register ABI: pop RET, no stack arguments */
}

static void h_libm_acos(CPU *c)  { misc_unary_double(c, acos); }
static void h_libm_asin(CPU *c)  { misc_unary_double(c, asin); }
static void h_libm_atan(CPU *c)  { misc_unary_double(c, atan); }
static void h_libm_cos(CPU *c)   { misc_unary_double(c, cos); }
static void h_libm_exp(CPU *c)   { misc_unary_double(c, exp); }
static void h_libm_log(CPU *c)   { misc_unary_double(c, log); }
static void h_libm_log10(CPU *c) { misc_unary_double(c, log10); }
static void h_libm_sin(CPU *c)   { misc_unary_double(c, sin); }
static void h_libm_sqrt(CPU *c)  { misc_unary_double(c, sqrt); }

static void h_libm_pow(CPU *c)
{
    int saved_errno = errno;
    double result;

    errno = 0;
    result = pow(c->x[0].d[0], c->x[1].d[0]);
    errno = saved_errno;
    c->x[0].d[0] = result;
    misc_cdecl_ret(c);
}

/* One binary32 on the cdecl stack, binary32 result in x87 ST(0). */
static void h_roundf(CPU *c)
{
    float x = misc_arg_f32(c, 0);
    fpush(c, (double)roundf(x));
    misc_cdecl_ret(c);
}

/* Two binary32 on the cdecl stack, binary32 result in x87 ST(0). */
static void h_nextafterf(CPU *c)
{
    float from = misc_arg_f32(c, 0);
    float to = misc_arg_f32(c, 1);
    fpush(c, (double)nextafterf(from, to));
    misc_cdecl_ret(c);
}

/* ----------------------------------------------------- D: secure strings --
 *
 * UCRT's string-_s entry points are __cdecl and take explicit sizes:
 *   errno_t strcpy_s (char *dst, rsize_t dstsz, const char *src);           3
 *   errno_t strcat_s (char *dst, rsize_t dstsz, const char *src);           3
 *   errno_t strncpy_s(char *dst, rsize_t dstsz, const char *src, size_t n); 4
 * The caller cleans the arguments, so each handler only pops the return
 * address and the four/six pushes stay for the translated `add esp, N`.
 *
 * Results: 0 success, EINVAL(22) NULL/invalid argument, ERANGE(34) truncation
 * required while it was not permitted, _STRUNCATE(80) for the special
 * n == (size_t)-1 prefix copy.
 *
 * Destination policy: after ERANGE/EINVAL the destination is reset to the
 * empty string, so a caller that ignores the result can never read a
 * half-copied buffer. The _TRUNCATE case deliberately keeps the truncated
 * prefix — retrieving that prefix is the only reason to pass _TRUNCATE, and
 * the destination is still terminated inside its capacity.
 *
 * dstsz == 0 is reported as EINVAL and writes nothing at all: with no declared
 * capacity there is no byte in the destination we may legally touch.
 */
#define MISC_EINVAL 22U
#define MISC_ERANGE 34U
#define MISC_STRUNCATE 80U
/* RSIZE_MAX is SIZE_MAX >> 1; the guest is 32-bit. */
#define MISC_RSIZE_MAX 0x7FFFFFFFU
#define MISC_TRUNCATE_COUNT 0xFFFFFFFFU

static void h_strcpy_s(CPU *c)
{
    uint32_t dst = misc_arg(c, 0);
    uint32_t dstsz = misc_arg(c, 1);
    uint32_t src = misc_arg(c, 2);
    uint32_t i;

    if (dst == 0U || dstsz == 0U || dstsz > MISC_RSIZE_MAX) {
        MISC_ERR("strcpy_s: bad destination 0x%{public}08x / size %u -> EINVAL",
                 dst, dstsz);
        c->eax = MISC_EINVAL;
        misc_cdecl_ret(c);
        return;
    }
    if (src == 0U) {
        st8(dst, 0U);
        MISC_ERR("strcpy_s: NULL source, dest 0x%{public}08x size %u -> EINVAL",
                 dst, dstsz);
        c->eax = MISC_EINVAL;
        misc_cdecl_ret(c);
        return;
    }
    for (i = 0U; i < dstsz; ++i) {
        uint8_t value = ld8(src + i);
        if (value == 0U) {
            st8(dst + i, 0U);
            c->eax = 0U;
            misc_cdecl_ret(c);
            return;
        }
        st8(dst + i, value);
    }
    /* The source did not terminate inside the destination. */
    st8(dst, 0U);
    MISC_ERR("strcpy_s: source 0x%{public}08x does not fit %u bytes "
             "-> ERANGE (destination emptied)", src, dstsz);
    c->eax = MISC_ERANGE;
    misc_cdecl_ret(c);
}

static void h_strcat_s(CPU *c)
{
    uint32_t dst = misc_arg(c, 0);
    uint32_t dstsz = misc_arg(c, 1);
    uint32_t src = misc_arg(c, 2);
    uint32_t used = 0U;

    if (dst == 0U || dstsz == 0U || dstsz > MISC_RSIZE_MAX) {
        MISC_ERR("strcat_s: bad destination 0x%{public}08x / size %u -> EINVAL",
                 dst, dstsz);
        c->eax = MISC_EINVAL;
        misc_cdecl_ret(c);
        return;
    }
    if (src == 0U) {
        st8(dst, 0U);
        MISC_ERR("strcat_s: NULL source, dest 0x%{public}08x size %u -> EINVAL",
                 dst, dstsz);
        c->eax = MISC_EINVAL;
        misc_cdecl_ret(c);
        return;
    }
    while (used < dstsz && ld8(dst + used) != 0U) {
        ++used;
    }
    if (used == dstsz) {
        st8(dst, 0U);
        MISC_ERR("strcat_s: destination 0x%{public}08x has no terminator in "
                 "%u bytes -> EINVAL (destination emptied)", dst, dstsz);
        c->eax = MISC_EINVAL;
        misc_cdecl_ret(c);
        return;
    }
    for (;;) {
        uint8_t value = ld8(src);
        ++src;
        if (value == 0U) {
            break;
        }
        if (used + 1U >= dstsz) {
            st8(dst, 0U);
            MISC_ERR("strcat_s: result exceeds %u bytes -> ERANGE "
                     "(destination emptied)", dstsz);
            c->eax = MISC_ERANGE;
            misc_cdecl_ret(c);
            return;
        }
        st8(dst + used, value);
        ++used;
    }
    st8(dst + used, 0U);
    c->eax = 0U;
    misc_cdecl_ret(c);
}

static void h_strncpy_s(CPU *c)
{
    uint32_t dst = misc_arg(c, 0);
    uint32_t dstsz = misc_arg(c, 1);
    uint32_t src = misc_arg(c, 2);
    uint32_t count = misc_arg(c, 3);
    uint32_t i;

    if (dst == 0U || dstsz == 0U || dstsz > MISC_RSIZE_MAX) {
        MISC_ERR("strncpy_s: bad destination 0x%{public}08x / size %u "
                 "-> EINVAL", dst, dstsz);
        c->eax = MISC_EINVAL;
        misc_cdecl_ret(c);
        return;
    }
    if (src == 0U) {
        st8(dst, 0U);
        MISC_ERR("strncpy_s: NULL source, dest 0x%{public}08x size %u "
                 "-> EINVAL", dst, dstsz);
        c->eax = MISC_EINVAL;
        misc_cdecl_ret(c);
        return;
    }
    if (count == 0U) {
        /* Nothing was requested; the destination is still made valid. */
        st8(dst, 0U);
        c->eax = 0U;
        misc_cdecl_ret(c);
        return;
    }
    /* count == (size_t)-1 is _TRUNCATE: copy the largest prefix that fits and
     * report the truncation instead of failing. */
    for (i = 0U; i < count; ++i) {
        uint8_t value = ld8(src + i);
        if (value == 0U) {
            st8(dst + i, 0U);
            c->eax = 0U;
            misc_cdecl_ret(c);
            return;
        }
        if (i + 1U >= dstsz) {
            if (count == MISC_TRUNCATE_COUNT) {
                st8(dst + dstsz - 1U, 0U);
                MISC_INFO("strncpy_s: _TRUNCATE kept %u of a longer source "
                          "(dest 0x%{public}08x)", dstsz - 1U, dst);
                c->eax = MISC_STRUNCATE;
            } else {
                st8(dst, 0U);
                MISC_ERR("strncpy_s: source needs more than %u bytes with "
                         "count=%u -> ERANGE (destination emptied)",
                         dstsz, count);
                c->eax = MISC_ERANGE;
            }
            misc_cdecl_ret(c);
            return;
        }
        st8(dst + i, value);
    }
    /* Exactly count characters were copied without meeting a terminator, so
     * one is written at dst[count] (C11 K.3.7.4.4). */
    st8(dst + i, 0U);
    c->eax = 0U;
    misc_cdecl_ret(c);
}

/* ----------------------------------------------- E: file enumeration ------
 *
 * The game scans its data root for resources and mods. This is a minimal but
 * honest Win32 search implementation over the host filesystem: a search that
 * matches nothing says so, a directory that cannot be read logs, and no
 * enumeration result is invented.
 *
 * Handles are tokens (index | MISC_FIND_BASE), never native pointers, and an
 * unknown or already-closed token is refused loudly rather than dereferenced.
 *
 * Path mapping. The guest's "current directory" is the host data root value
 * (host_ohos.c hands ISAAC_DATA_ROOT straight to GetCurrentDirectoryA), so the
 * mapping is:
 *   UTF-16 pattern -> UTF-8, '\' -> '/', drop an "X:" drive prefix;
 *   take the result as-is when it already starts with the data root
 *   (the guest is echoing back the path we gave it), otherwise join it
 *   onto the root; refuse any ".." component.
 * Nothing else is interpreted: no drive letters beyond a dropped prefix, no
 * UNC, no device names. Everything refused is logged.
 *
 * WIN32_FIND_DATAW is 592 bytes on x86:
 *   +0   DWORD    dwFileAttributes
 *   +4   FILETIME ftCreationTime      (8)
 *   +12  FILETIME ftLastAccessTime    (8)
 *   +20  FILETIME ftLastWriteTime     (8)
 *   +28  DWORD    nFileSizeHigh
 *   +32  DWORD    nFileSizeLow
 *   +36  DWORD    dwReserved0
 *   +40  DWORD    dwReserved1
 *   +44  WCHAR    cFileName[260]      (520)
 *   +564 WCHAR    cAlternateFileName[14] (28)
 *   = 592
 */
#define MISC_FIND_BASE 0x00040000U
#define MISC_FIND_MAX 32
#define MISC_FIND_INVALID 0xFFFFFFFFU
#define MISC_FIND_DATAW_SIZE 592U
#define MISC_WFD_ATTRIBUTES 0U
#define MISC_WFD_CREATION 4U
#define MISC_WFD_ACCESS 12U
#define MISC_WFD_WRITE 20U
#define MISC_WFD_SIZE_HIGH 28U
#define MISC_WFD_SIZE_LOW 32U
#define MISC_WFD_RESERVED0 36U
#define MISC_WFD_RESERVED1 40U
#define MISC_WFD_NAME 44U
#define MISC_WFD_NAME_WCHARS 260U
#define MISC_WFD_ALT_NAME 564U
#define MISC_WFD_ALT_WCHARS 14U
#define MISC_PATH_MAX 1024U
#define MISC_NAME_MAX 512U
/* A guest pattern is bounded so a pattern that never terminates cannot walk
 * the whole arena. */
#define MISC_PATTERN_WCHARS 1024U

#define MISC_FILE_ATTRIBUTE_READONLY  0x00000001U
#define MISC_FILE_ATTRIBUTE_DIRECTORY 0x00000010U
#define MISC_FILE_ATTRIBUTE_ARCHIVE   0x00000020U

#define MISC_ERROR_FILE_NOT_FOUND 2U
#define MISC_ERROR_PATH_NOT_FOUND 3U
#define MISC_ERROR_INVALID_HANDLE 6U
#define MISC_ERROR_NOT_ENOUGH_MEMORY 8U
#define MISC_ERROR_NO_MORE_FILES 18U
#define MISC_ERROR_INVALID_PARAMETER 87U

typedef struct {
    int      used;
    DIR     *dir;
    char     dir_path[MISC_PATH_MAX];   /* host directory being scanned */
    char     pattern[MISC_PATH_MAX];    /* last component to match */
    char     name[MISC_NAME_MAX];       /* current match, UTF-8 */
    uint32_t attributes;
    uint64_t size;
    uint64_t mtime;
} misc_find;

static misc_find g_finds[MISC_FIND_MAX];

static misc_find *misc_find_get(uint32_t token)
{
    uint32_t index = token - MISC_FIND_BASE;
    if (index >= MISC_FIND_MAX || !g_finds[index].used) {
        return NULL;
    }
    return &g_finds[index];
}

static int misc_find_alloc(DIR *dir, const char *dir_path, const char *pattern)
{
    int i;
    for (i = 0; i < MISC_FIND_MAX; ++i) {
        if (!g_finds[i].used) {
            memset(&g_finds[i], 0, sizeof g_finds[i]);
            g_finds[i].used = 1;
            g_finds[i].dir = dir;
            snprintf(g_finds[i].dir_path, sizeof g_finds[i].dir_path, "%s",
                     dir_path);
            snprintf(g_finds[i].pattern, sizeof g_finds[i].pattern, "%s",
                     pattern);
            return i;
        }
    }
    return -1;
}

static void misc_find_release(misc_find *f)
{
    if (f->dir != NULL) {
        closedir(f->dir);
    }
    memset(f, 0, sizeof *f);
}

/* Case-insensitive matching, because the guest believes it is on a
 * case-insensitive filesystem. '*' and '?' only, plus Windows' DOS rule that
 * the bare pattern "*.*" matches every name including dotless ones. */
static int misc_fold(int ch)
{
    return (ch >= 'A' && ch <= 'Z') ? ch + ('a' - 'A') : ch;
}

static int misc_glob_match(const char *pattern, const char *name)
{
    const char *p = pattern;
    const char *n = name;
    const char *star_p = NULL;
    const char *star_n = NULL;

    if (strcmp(pattern, "*.*") == 0) {
        return 1;
    }
    for (;;) {
        if (*p == '*') {
            star_p = ++p;
            star_n = n;
            continue;
        }
        if (*p == '?' && *n != '\0') {
            ++p;
            ++n;
            continue;
        }
        if (*p != '\0' && *p != '?' && *n != '\0' &&
            misc_fold((unsigned char)*p) == misc_fold((unsigned char)*n)) {
            ++p;
            ++n;
            continue;
        }
        if (*p == '\0' && *n == '\0') {
            return 1;
        }
        if (star_p != NULL && *n != '\0') {
            p = star_p;
            n = ++star_n;
            continue;
        }
        return 0;
    }
}

/* UTF-16 (guest, NUL terminated) -> UTF-8 (host). Returns 0 on success, -1
 * when the pattern is unterminated, unreadable or longer than the buffer. */
static int misc_utf16_to_utf8(uint32_t addr, char *out, size_t cap)
{
    size_t out_len = 0U, i;

    if (addr == 0U || cap == 0U) {
        return -1;
    }
    for (i = 0U; i < MISC_PATTERN_WCHARS; ++i) {
        uint32_t at = addr + (uint32_t)(i * 2U);
        uint16_t unit;
        uint32_t code;
        unsigned need;

        if (at > 0xFFFFFFFFU - 4U) {
            return -1;
        }
        unit = ld16(at);
        if (unit == 0U) {
            out[out_len] = '\0';
            return 0;
        }
        code = unit;
        if (unit >= 0xD800U && unit <= 0xDBFFU) {
            uint16_t low = ld16(at + 2U);
            if (low >= 0xDC00U && low <= 0xDFFFU) {
                code = 0x10000U + (((uint32_t)(unit - 0xD800U)) << 10) +
                       (uint32_t)(low - 0xDC00U);
                ++i;
            } else {
                code = 0xFFFDU;     /* lone high surrogate */
            }
        } else if (unit >= 0xDC00U && unit <= 0xDFFFU) {
            code = 0xFFFDU;         /* lone low surrogate */
        }

        need = (code < 0x80U) ? 1U :
               (code < 0x800U) ? 2U :
               (code < 0x10000U) ? 3U : 4U;
        if (out_len + need >= cap) {
            return -1;
        }
        if (need == 1U) {
            out[out_len++] = (char)code;
        } else if (need == 2U) {
            out[out_len++] = (char)(0xC0U | (code >> 6));
            out[out_len++] = (char)(0x80U | (code & 0x3FU));
        } else if (need == 3U) {
            out[out_len++] = (char)(0xE0U | (code >> 12));
            out[out_len++] = (char)(0x80U | ((code >> 6) & 0x3FU));
            out[out_len++] = (char)(0x80U | (code & 0x3FU));
        } else {
            out[out_len++] = (char)(0xF0U | (code >> 18));
            out[out_len++] = (char)(0x80U | ((code >> 12) & 0x3FU));
            out[out_len++] = (char)(0x80U | ((code >> 6) & 0x3FU));
            out[out_len++] = (char)(0x80U | (code & 0x3FU));
        }
    }
    MISC_ERR("pattern at 0x%{public}08x is unterminated or longer than %u "
             "UTF-16 units", addr, (unsigned)MISC_PATTERN_WCHARS);
    return -1;
}

/* UTF-8 (host) -> UTF-16 written into the guest arena; the destination is
 * always terminated, and the field is zeroed by the caller beforehand. */
static size_t misc_utf8_to_utf16(const char *s, uint16_t *out, size_t cap)
{
    size_t n = 0U;

    while (*s != '\0') {
        unsigned char byte = (unsigned char)*s++;
        uint32_t code;

        if (byte < 0x80U) {
            code = byte;
        } else if ((byte & 0xE0U) == 0xC0U) {
            code = byte & 0x1FU;
            if (((unsigned char)*s & 0xC0U) == 0x80U) {
                code = (code << 6) | (uint32_t)((unsigned char)*s++ & 0x3FU);
            }
        } else if ((byte & 0xF0U) == 0xE0U) {
            code = byte & 0x0FU;
            for (int k = 0; k < 2 && ((unsigned char)*s & 0xC0U) == 0x80U; ++k) {
                code = (code << 6) | (uint32_t)((unsigned char)*s++ & 0x3FU);
            }
        } else if ((byte & 0xF8U) == 0xF0U) {
            code = byte & 0x07U;
            for (int k = 0; k < 3 && ((unsigned char)*s & 0xC0U) == 0x80U; ++k) {
                code = (code << 6) | (uint32_t)((unsigned char)*s++ & 0x3FU);
            }
        } else {
            code = 0xFFFDU;
        }
        if (code >= 0x10000U) {
            if (n + 2U > cap) {
                break;
            }
            code -= 0x10000U;
            out[n++] = (uint16_t)(0xD800U | (code >> 10));
            out[n++] = (uint16_t)(0xDC00U | (code & 0x3FFU));
        } else {
            if (n + 1U > cap) {
                break;
            }
            out[n++] = (uint16_t)code;
        }
    }
    return n;
}

static void misc_write_utf16_field(uint32_t addr, size_t cap_wchars,
                                   const char *utf8)
{
    uint16_t tmp[MISC_WFD_NAME_WCHARS];
    size_t n = misc_utf8_to_utf16(utf8, tmp, cap_wchars - 1U);

    tmp[n] = 0U;
    memcpy(isaac_arena_base + addr, tmp, (n + 1U) * sizeof(uint16_t));
}

/* Returns 0 when the guest structure could not be filled at all (logged), so
 * the caller can fail the search instead of handing back a token whose data
 * was never written. */
static int misc_write_find_data(uint32_t addr, const misc_find *f)
{
    uint64_t filetime;
    unsigned i;

    if (!misc_guest_range(addr, MISC_FIND_DATAW_SIZE)) {
        MISC_ERR("WIN32_FIND_DATAW at 0x%{public}08x is unusable; "
                 "\"%{public}s\" not reported", addr, f->name);
        return 0;
    }
    memset(isaac_arena_base + addr, 0, MISC_FIND_DATAW_SIZE);
    st32(addr + MISC_WFD_ATTRIBUTES, f->attributes);

    /* Host files carry one reliable timestamp. Reporting mtime for creation,
     * access and write alike is honest; a zero FILETIME would read as 1601. */
    filetime = (f->mtime + UINT64_C(11644473600)) * UINT64_C(10000000);
    for (i = 0; i < 3U; ++i) {
        uint32_t at = addr + MISC_WFD_CREATION + i * 8U;
        st32(at, (uint32_t)filetime);
        st32(at + 4U, (uint32_t)(filetime >> 32));
    }
    st32(addr + MISC_WFD_SIZE_LOW, (uint32_t)f->size);
    st32(addr + MISC_WFD_SIZE_HIGH, (uint32_t)(f->size >> 32));
    st32(addr + MISC_WFD_RESERVED0, 0U);
    st32(addr + MISC_WFD_RESERVED1, 0U);
    misc_write_utf16_field(addr + MISC_WFD_NAME, MISC_WFD_NAME_WCHARS, f->name);
    /* cAlternateFileName stays empty: the host has no 8.3 short name and
     * fabricating one is worse than reporting none. */
    misc_write_utf16_field(addr + MISC_WFD_ALT_NAME, MISC_WFD_ALT_WCHARS, "");
    return 1;
}

/* stat() the current match. A file that vanished between readdir and stat is
 * reported with attributes and a zero size rather than dropped, and the
 * failure is logged. */
static uint32_t misc_stat_entry(misc_find *f)
{
    char path[MISC_PATH_MAX + MISC_NAME_MAX + 2U];
    struct stat st;
    uint32_t attributes = MISC_FILE_ATTRIBUTE_ARCHIVE;

    f->size = 0U;
    f->mtime = 0U;
    snprintf(path, sizeof path, "%s/%s", f->dir_path, f->name);
    if (stat(path, &st) != 0) {
        MISC_ERR("find: stat(\"%{public}s\") failed (errno=%d); reporting "
                 "attributes only", path, errno);
        return attributes;
    }
    f->mtime = (uint64_t)st.st_mtime;
    if (S_ISDIR(st.st_mode)) {
        attributes = MISC_FILE_ATTRIBUTE_DIRECTORY;
    } else {
        f->size = (uint64_t)st.st_size;
    }
    if ((st.st_mode & 0222) == 0) {
        attributes |= MISC_FILE_ATTRIBUTE_READONLY;
    }
    return attributes;
}

static int misc_find_advance(misc_find *f)
{
    struct dirent *entry;

    while ((entry = readdir(f->dir)) != NULL) {
        if (!misc_glob_match(f->pattern, entry->d_name)) {
            continue;
        }
        if (strlen(entry->d_name) >= sizeof f->name) {
            MISC_ERR("find: name longer than %u bytes skipped in "
                     "\"%{public}s\"", (unsigned)sizeof f->name, f->dir_path);
            continue;
        }
        snprintf(f->name, sizeof f->name, "%s", entry->d_name);
        f->attributes = misc_stat_entry(f);
        return 1;
    }
    f->name[0] = '\0';
    return 0;
}

/* Refuse ".." outright: the guest may not climb out of its data root. */
static int misc_has_parent_component(const char *path)
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

static const char *misc_data_root(void)
{
    const char *env = getenv("ISAAC_DATA_ROOT");
    return (env != NULL && env[0] != '\0') ? env : "isaac-data";
}

/* Guest UTF-16 pattern -> host path. Returns 0 on success, -1 when refused
 * (and logs why). */
static int misc_map_guest_path(uint32_t pattern_addr, char *out, size_t cap)
{
    char raw[MISC_PATH_MAX];
    const char *root = misc_data_root();
    size_t root_len = strlen(root);
    const char *rest;
    size_t i;

    if (misc_utf16_to_utf8(pattern_addr, raw, sizeof raw) != 0) {
        MISC_ERR("find: unusable UTF-16 pattern at 0x%{public}08x",
                 pattern_addr);
        return -1;
    }
    for (i = 0U; raw[i] != '\0'; ++i) {
        if (raw[i] == '\\') {
            raw[i] = '/';
        }
    }
    if (misc_has_parent_component(raw)) {
        MISC_ERR("find: refusing \"%{public}s\": \"..\" leaves the data root",
                 raw);
        return -1;
    }
    rest = raw;
    if (((rest[0] >= 'A' && rest[0] <= 'Z') ||
         (rest[0] >= 'a' && rest[0] <= 'z')) && rest[1] == ':') {
        rest += 2;                  /* a dropped drive prefix, e.g. "C:\..." */
    }
    while (*rest == '/') {
        ++rest;
    }
    if (root_len != 0U && strncmp(rest, root, root_len) == 0 &&
        (rest[root_len] == '/' || rest[root_len] == '\0')) {
        /* The guest echoed the path we handed out as its current directory. */
        if (strlen(rest) >= cap) {
            MISC_ERR("find: path \"%{public}s\" is longer than %u bytes",
                     rest, (unsigned)cap);
            return -1;
        }
        memcpy(out, rest, strlen(rest) + 1U);
    } else {
        size_t rest_len = strlen(rest);
        if (root_len + 1U + rest_len >= cap) {
            MISC_ERR("find: \"%{public}s/%{public}s\" is longer than %u bytes",
                     root, rest, (unsigned)cap);
            return -1;
        }
        memcpy(out, root, root_len);
        out[root_len] = '/';
        memcpy(out + root_len + 1U, rest, rest_len + 1U);
    }
    return 0;
}

static void h_find_first_file_w(CPU *c)
{
    uint32_t pattern_addr = misc_arg(c, 0);
    uint32_t data_addr = misc_arg(c, 1);
    char host[MISC_PATH_MAX];
    char dir_path[MISC_PATH_MAX];
    char pattern[MISC_PATH_MAX];
    char *slash;
    DIR *dir;
    int slot;
    misc_find *f;

    if (data_addr == 0U || !misc_guest_range(data_addr, MISC_FIND_DATAW_SIZE)) {
        MISC_ERR("FindFirstFileW: unusable WIN32_FIND_DATAW 0x%{public}08x -> "
                 "INVALID_HANDLE_VALUE", data_addr);
        c->last_error = MISC_ERROR_INVALID_PARAMETER;
        c->eax = MISC_FIND_INVALID;
        misc_stdcall_ret(c, 2);
        return;
    }
    if (misc_map_guest_path(pattern_addr, host, sizeof host) != 0) {
        c->last_error = MISC_ERROR_FILE_NOT_FOUND;
        c->eax = MISC_FIND_INVALID;
        misc_stdcall_ret(c, 2);
        return;
    }

    snprintf(dir_path, sizeof dir_path, "%s", host);
    slash = strrchr(dir_path, '/');
    if (slash != NULL) {
        snprintf(pattern, sizeof pattern, "%s", slash + 1);
        *slash = '\0';
        if (dir_path[0] == '\0') {
            snprintf(dir_path, sizeof dir_path, "/");
        }
    } else {
        snprintf(pattern, sizeof pattern, "%s", dir_path);
        snprintf(dir_path, sizeof dir_path, ".");
    }
    if (pattern[0] == '\0') {
        MISC_ERR("FindFirstFileW: \"%{public}s\" names no file pattern -> "
                 "INVALID_HANDLE_VALUE", host);
        c->last_error = MISC_ERROR_FILE_NOT_FOUND;
        c->eax = MISC_FIND_INVALID;
        misc_stdcall_ret(c, 2);
        return;
    }

    dir = opendir(dir_path);
    if (dir == NULL) {
        MISC_ERR("FindFirstFileW: opendir(\"%{public}s\") failed (errno=%d) "
                 "for guest pattern \"%{public}s\" -> INVALID_HANDLE_VALUE",
                 dir_path, errno, host);
        c->last_error = MISC_ERROR_PATH_NOT_FOUND;
        c->eax = MISC_FIND_INVALID;
        misc_stdcall_ret(c, 2);
        return;
    }

    slot = misc_find_alloc(dir, dir_path, pattern);
    if (slot < 0) {
        closedir(dir);
        MISC_ERR("FindFirstFileW: all %d search tokens in use -> "
                 "INVALID_HANDLE_VALUE", (int)MISC_FIND_MAX);
        c->last_error = MISC_ERROR_NOT_ENOUGH_MEMORY;
        c->eax = MISC_FIND_INVALID;
        misc_stdcall_ret(c, 2);
        return;
    }
    f = &g_finds[slot];
    if (!misc_find_advance(f)) {
        /* A pattern that matches nothing is an ordinary answer (the game
         * probes for optional files), not a host failure. */
        MISC_INFO("FindFirstFileW: \"%{public}s\" matched nothing in "
                  "\"%{public}s\"", pattern, dir_path);
        misc_find_release(f);
        c->last_error = MISC_ERROR_FILE_NOT_FOUND;
        c->eax = MISC_FIND_INVALID;
        misc_stdcall_ret(c, 2);
        return;
    }
    if (!misc_write_find_data(data_addr, f)) {
        MISC_ERR("FindFirstFileW: could not report \"%{public}s\" "
                 "(token 0x%{public}08x released)", f->name,
                 MISC_FIND_BASE | (uint32_t)slot);
        misc_find_release(f);
        c->last_error = MISC_ERROR_INVALID_PARAMETER;
        c->eax = MISC_FIND_INVALID;
        misc_stdcall_ret(c, 2);
        return;
    }
    c->last_error = 0U;
    c->eax = MISC_FIND_BASE | (uint32_t)slot;
    MISC_INFO("FindFirstFileW: \"%{public}s\" -> token 0x%{public}08x "
              "first=\"%{public}s\" attrs=0x%{public}08x size=%u",
              host, c->eax, f->name, f->attributes, (unsigned)f->size);
    misc_stdcall_ret(c, 2);
}

static void h_find_next_file_w(CPU *c)
{
    uint32_t token = misc_arg(c, 0);
    uint32_t data_addr = misc_arg(c, 1);
    misc_find *f = misc_find_get(token);

    if (f == NULL) {
        /* The spec for this port wants an unknown/stale token reported as
         * INVALID_HANDLE_VALUE (Windows would return FALSE); it is logged
         * because a truthy failure value can keep a broken guest loop
         * spinning. */
        MISC_ERR("FindNextFileW: token 0x%{public}08x is unknown or already "
                 "closed -> INVALID_HANDLE_VALUE", token);
        c->last_error = MISC_ERROR_INVALID_HANDLE;
        c->eax = MISC_FIND_INVALID;
        misc_stdcall_ret(c, 2);
        return;
    }
    if (data_addr == 0U || !misc_guest_range(data_addr, MISC_FIND_DATAW_SIZE)) {
        MISC_ERR("FindNextFileW: unusable WIN32_FIND_DATAW 0x%{public}08x for "
                 "token 0x%{public}08x -> FALSE", data_addr, token);
        c->last_error = MISC_ERROR_INVALID_PARAMETER;
        c->eax = 0U;
        misc_stdcall_ret(c, 2);
        return;
    }
    if (!misc_find_advance(f)) {
        c->last_error = MISC_ERROR_NO_MORE_FILES;
        c->eax = 0U;
        misc_stdcall_ret(c, 2);
        return;
    }
    if (!misc_write_find_data(data_addr, f)) {
        c->last_error = MISC_ERROR_INVALID_PARAMETER;
        c->eax = 0U;
        misc_stdcall_ret(c, 2);
        return;
    }
    c->last_error = 0U;
    c->eax = 1U;
    misc_stdcall_ret(c, 2);
}

static void h_find_close(CPU *c)
{
    uint32_t token = misc_arg(c, 0);
    misc_find *f = misc_find_get(token);

    if (f == NULL) {
        /* Windows FindClose also returns FALSE here, so no contract is
         * broken; it is still logged because the guest lost a token. */
        MISC_ERR("FindClose: token 0x%{public}08x is unknown or already "
                 "closed -> FALSE", token);
        c->last_error = MISC_ERROR_INVALID_HANDLE;
        c->eax = 0U;
        misc_stdcall_ret(c, 1);
        return;
    }
    misc_find_release(f);
    c->last_error = 0U;
    c->eax = 1U;
    misc_stdcall_ret(c, 1);
}

/* ------------------------------------------------------------- descriptor */

static const host_ext_entry s_entries[] = {
    /* A: fiber-local storage (this binary imports Alloc/SetValue/Free; there
     * is no FlsGetValue in its IAT, but serving it costs nothing). */
    { "KERNEL32.dll!FlsAlloc", h_fls_alloc },
    { "KERNEL32.dll!FlsGetValue", h_fls_get_value },
    { "KERNEL32.dll!FlsSetValue", h_fls_set_value },
    { "KERNEL32.dll!FlsFree", h_fls_free },
    /* B: thread-local storage */
    { "KERNEL32.dll!TlsAlloc", h_tls_alloc },
    { "KERNEL32.dll!TlsGetValue", h_tls_get_value },
    { "KERNEL32.dll!TlsSetValue", h_tls_set_value },
    { "KERNEL32.dll!TlsFree", h_tls_free },
    /* C: floating point, XMM0 channel */
    { "api-ms-win-crt-math-l1-1-0.dll!_libm_sse2_acos_precise", h_libm_acos },
    { "api-ms-win-crt-math-l1-1-0.dll!_libm_sse2_asin_precise", h_libm_asin },
    { "api-ms-win-crt-math-l1-1-0.dll!_libm_sse2_atan_precise", h_libm_atan },
    { "api-ms-win-crt-math-l1-1-0.dll!_libm_sse2_cos_precise", h_libm_cos },
    { "api-ms-win-crt-math-l1-1-0.dll!_libm_sse2_exp_precise", h_libm_exp },
    { "api-ms-win-crt-math-l1-1-0.dll!_libm_sse2_log_precise", h_libm_log },
    { "api-ms-win-crt-math-l1-1-0.dll!_libm_sse2_log10_precise", h_libm_log10 },
    { "api-ms-win-crt-math-l1-1-0.dll!_libm_sse2_pow_precise", h_libm_pow },
    { "api-ms-win-crt-math-l1-1-0.dll!_libm_sse2_sin_precise", h_libm_sin },
    { "api-ms-win-crt-math-l1-1-0.dll!_libm_sse2_sqrt_precise", h_libm_sqrt },
    /* C: floating point, x87 channel */
    { "api-ms-win-crt-math-l1-1-0.dll!roundf", h_roundf },
    { "api-ms-win-crt-math-l1-1-0.dll!nextafterf", h_nextafterf },
    /* D: secure string copies */
    { "api-ms-win-crt-string-l1-1-0.dll!strcpy_s", h_strcpy_s },
    { "api-ms-win-crt-string-l1-1-0.dll!strcat_s", h_strcat_s },
    { "api-ms-win-crt-string-l1-1-0.dll!strncpy_s", h_strncpy_s },
    /* E: file enumeration */
    { "KERNEL32.dll!FindFirstFileW", h_find_first_file_w },
    { "KERNEL32.dll!FindNextFileW", h_find_next_file_w },
    { "KERNEL32.dll!FindClose", h_find_close },
};

static const host_ext_module s_module = {
    s_entries, (unsigned)(sizeof s_entries / sizeof s_entries[0]),
    NULL, 0U,
};

/* PROVIDER SLOT COLLISION — read this before wondering why nothing changed.
 *
 * host_ohos.c owns exactly two extension slots, s_ext_providers[]:
 *   slot 0 = host_ext_printf()      (owned by host_ext_printf.c, strong)
 *   slot 1 = host_ext_misc()        (this function)
 * A slot is one linker symbol, so it can hold exactly one module.
 *
 * At the time this file was written, host_kage.c ALSO defined
 * `const host_ext_module *host_ext_misc(void)` (strong) and returned its
 * GL/wglGetProcAddress module — "Extension module slot 1 in host_ohos.c's
 * s_ext_providers", per its own comment. Two strong definitions of one symbol
 * do not link at all, so this definition is weak: the tree stays buildable,
 * and the moment host_kage.c stops owning the symbol (renaming its provider
 * to e.g. host_ext_kage and adding it to s_ext_providers[] is a two-line
 * change in two other files) these handlers become the ones installed.
 *
 * Losing the symbol silently would leave every handler below dead code with no
 * evidence, so the constructor below reports the shadowed state at startup.
 * This module never silently pretends to be installed. */
__attribute__((weak)) const host_ext_module *host_ext_misc(void)
{
    return &s_module;
}

/* Losing the symbol silently would leave all 26 entries below dead code with
 * no evidence, so report the shadowed (or empty) state once at startup:
 * host_ohos.c's slot 1 is a single symbol, so a module that does not own it is
 * never consulted. The other module's entry counts are printed (26 static / 0
 * dynamic is this file) so the message can be checked rather than trusted;
 * hilog has no %p, hence counts instead of pointers. */
__attribute__((constructor)) static void host_ext_misc_self_check(void)
{
    const host_ext_module *installed = host_ext_misc();

    if (installed == &s_module) {
        return;                 /* this module owns provider slot 1 */
    }
    if (installed == NULL) {
        MISC_ERR("host_ext_misc.c: provider slot 1 is EMPTY (this module's "
                 "weak definition lost to host_ohos.c's stub); the 26 imports "
                 "here (FLS/TLS, _libm_sse2_*, roundf, nextafterf, str*_s, "
                 "FindFirstFileW/FindNextFileW/FindClose) are NOT installed");
        return;
    }
    MISC_ERR("host_ext_misc.c: provider slot 1 serves a module with "
             "%{public}u static and %{public}u dynamic entries, not this "
             "module's 26/0, so the imports here (FLS/TLS, _libm_sse2_*, "
             "roundf, nextafterf, str*_s, FindFirstFileW/FindNextFileW/"
             "FindClose) are NOT installed. host_ohos.c needs a third provider "
             "slot and host_kage.c needs its own provider name; both are other "
             "files that this module may not edit.",
             installed->entry_count, installed->dynamic_count);
}
