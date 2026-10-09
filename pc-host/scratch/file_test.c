/* Out-of-tree unit test for host_ext_file.c.
 *
 * It includes the module (its handlers are static) and drives them exactly the
 * way host_ohos.c would: a CPU whose esp points at a fake guest frame, an
 * argument per dword, the result read back from eax. The guest arena is the
 * real one (guest_memory.c), the data root is a scratch directory, and every
 * assertion is checked against the host filesystem afterwards - so the file
 * contents, positions and errno values below are facts, not self-reporting.
 *
 * Build + run (see the report for the exact command):
 *   gcc -O1 -g -I<pc-host/shim> -I<isaac> -I<isaac/generated> \
 *       -o /tmp/file_test file_test.c <isaac>/guest_memory.c
 *   ISAAC_DATA_ROOT=/tmp/isaac-file-test-root /tmp/file_test
 */
#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "guest.h"
#include "guest_memory.h"
#include "../../isaac-ohos/entry/src/main/cpp/isaac/host_ext_file.c"

static int g_pass;
static int g_fail;

static void check(int ok, const char *fmt, ...)
{
    va_list ap;
    if (ok) {
        g_pass++;
    } else {
        g_fail++;
    }
    printf("%s ", ok ? "PASS" : "FAIL");
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
}

/* --------------------------------------------------------------- harness */

#define ARG_AREA  0x00100000U   /* fake guest stack */
#define DATA_AREA 0x00200000U   /* guest data buffers */
#define STR_AREA  0x00300000U   /* guest strings */

static CPU g_cpu;

static const host_ext_entry *handler(const char *name)
{
    unsigned i;
    for (i = 0U; i < s_module.entry_count; ++i) {
        if (strcmp(s_module.entries[i].name, name) == 0) {
            return &s_module.entries[i];
        }
    }
    fprintf(stderr, "FATAL: %s is not registered\n", name);
    exit(2);
}

static uint32_t call(const char *name, int nargs, ...)
{
    va_list ap;
    uint32_t sp = ARG_AREA;
    int i;

    st32(sp, 0xC0DE0000U);              /* fake return address */
    va_start(ap, nargs);
    for (i = 0; i < nargs; ++i) {
        st32(sp + 4U + (uint32_t)i * 4U, va_arg(ap, uint32_t));
    }
    va_end(ap);
    memset(&g_cpu, 0, sizeof g_cpu);
    g_cpu.esp = sp;
    handler(name)->fn(&g_cpu);
    check(g_cpu.esp == sp + 4U, "cdecl: %s popped only the return address",
          name);
    return g_cpu.eax;
}

#define CALL0(n)                 call((n), 0)
#define CALL1(n, a)              call((n), 1, (uint32_t)(a))
#define CALL2(n, a, b)           call((n), 2, (uint32_t)(a), (uint32_t)(b))
#define CALL3(n, a, b, c)        call((n), 3, (uint32_t)(a), (uint32_t)(b), (uint32_t)(c))
#define CALL4(n, a, b, c, d)     call((n), 4, (uint32_t)(a), (uint32_t)(b), (uint32_t)(c), (uint32_t)(d))

static uint32_t S(const char *text)
{
    static uint32_t cursor = STR_AREA;
    size_t len = strlen(text) + 1U;
    uint32_t at = cursor;
    memcpy(isaac_arena_base + at, text, len);
    cursor += (uint32_t)((len + 3U) & ~3U);
    return at;
}

static uint32_t data(const void *bytes, size_t len)
{
    static uint32_t cursor = DATA_AREA;
    uint32_t at = cursor;
    memcpy(isaac_arena_base + at, bytes, len);
    cursor += (uint32_t)((len + 15U) & ~15U);
    return at;
}

static int32_t guest_errno(void)
{
    uint32_t p = CALL0("api-ms-win-crt-runtime-l1-1-0.dll!_errno");
    return p != 0U ? (int32_t)ld32(p) : -1;
}

static long host_size(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 ? (long)st.st_size : -1;
}

static size_t host_read(const char *path, unsigned char *out, size_t cap)
{
    FILE *fp = fopen(path, "rb");
    size_t n;
    if (fp == NULL) {
        return 0U;
    }
    n = fread(out, 1U, cap, fp);
    fclose(fp);
    return n;
}

static const char *ROOT = "/tmp/isaac-file-test-root";

#define FOPEN(path, mode) \
    CALL2("api-ms-win-crt-stdio-l1-1-0.dll!fopen", S(path), S(mode))
#define FCLOSE(t)  CALL1("api-ms-win-crt-stdio-l1-1-0.dll!fclose", (t))
#define FREAD(p, sz, n, t) \
    CALL4("api-ms-win-crt-stdio-l1-1-0.dll!fread", (p), (sz), (n), (t))
#define FWRITE(p, sz, n, t) \
    CALL4("api-ms-win-crt-stdio-l1-1-0.dll!fwrite", (p), (sz), (n), (t))
#define FSEEK(t, off, org) \
    CALL3("api-ms-win-crt-stdio-l1-1-0.dll!fseek", (t), (int32_t)(off), (org))
#define FTELL(t)  CALL1("api-ms-win-crt-stdio-l1-1-0.dll!ftell", (t))
#define FFLUSH(t) CALL1("api-ms-win-crt-stdio-l1-1-0.dll!fflush", (t))
#define FILENO(t) CALL1("api-ms-win-crt-stdio-l1-1-0.dll!_fileno", (t))
#define OSFHANDLE(fd) \
    CALL1("api-ms-win-crt-stdio-l1-1-0.dll!_get_osfhandle", (fd))
#define SETVBUF(t, b, m, s) \
    CALL4("api-ms-win-crt-stdio-l1-1-0.dll!setvbuf", (t), (b), (m), (s))
#define REMOVE(p) \
    CALL1("api-ms-win-crt-filesystem-l1-1-0.dll!remove", S(p))
#define ACCESS(p, m) \
    CALL2("api-ms-win-crt-filesystem-l1-1-0.dll!_access", S(p), (m))
#define SET_ERRNO(v) \
    CALL1("api-ms-win-crt-runtime-l1-1-0.dll!_set_errno", (v))

int main(void)
{
    unsigned char page[512];
    unsigned char expect[512];
    uint32_t tok, wbuf, rbuf, fd;
    char guest_rooted[512];
    char host[512];
    unsigned i;
    size_t n;

    puts("=== host_ext_file.c unit test ===");
    check(guest_mem_init() == 0, "arena allocated (4 GiB, MAP_NORESERVE)");
    setenv("ISAAC_DATA_ROOT", ROOT, 1);
    printf("data root: %s\n", ROOT);

    /* --- the registered set ------------------------------------------- */
    {
        static const char *want[] = {
            "api-ms-win-crt-stdio-l1-1-0.dll!fopen",
            "api-ms-win-crt-stdio-l1-1-0.dll!fclose",
            "api-ms-win-crt-stdio-l1-1-0.dll!fread",
            "api-ms-win-crt-stdio-l1-1-0.dll!fwrite",
            "api-ms-win-crt-stdio-l1-1-0.dll!fseek",
            "api-ms-win-crt-stdio-l1-1-0.dll!ftell",
            "api-ms-win-crt-stdio-l1-1-0.dll!fflush",
            "api-ms-win-crt-stdio-l1-1-0.dll!setvbuf",
            "api-ms-win-crt-stdio-l1-1-0.dll!_fileno",
            "api-ms-win-crt-stdio-l1-1-0.dll!_get_osfhandle",
            "api-ms-win-crt-filesystem-l1-1-0.dll!remove",
            "api-ms-win-crt-filesystem-l1-1-0.dll!_access",
            "api-ms-win-crt-runtime-l1-1-0.dll!_errno",
            "api-ms-win-crt-runtime-l1-1-0.dll!_set_errno",
        };
        check(s_module.entry_count == sizeof want / sizeof want[0],
              "module entry_count is %u (expected %u)", s_module.entry_count,
              (unsigned)(sizeof want / sizeof want[0]));
        for (i = 0U; i < sizeof want / sizeof want[0]; ++i) {
            check(handler(want[i]) != NULL, "registers %s", want[i]);
        }
    }

    /* --- write a new file through the handlers ------------------------- */
    snprintf(host, sizeof host, "%s/logs/deep/roundtrip.bin", ROOT);
    check(host_size(host) == -1, "target does not exist yet (%s)", host);
    tok = FOPEN("logs/deep/roundtrip.bin", "wb");
    check(tok != 0U, "fopen(wb) of a nested path -> token 0x%08x", tok);
    check(host_size(host) == 0, "host file created (size %ld)",
          host_size(host));
    check(access("/tmp/isaac-file-test-root/logs/deep", F_OK) == 0,
          "missing parent directories were created (mkdir -p)");

    for (i = 0U; i < 5U; ++i) {
        uint32_t word = 0x1000U + i;
        memcpy(expect + i * 4U, &word, 4U);
    }
    wbuf = data(expect, 20U);
    check(FWRITE(wbuf, 4U, 5U, tok) == 5U, "fwrite 5 x 4 bytes -> 5 items");
    check(FCLOSE(tok) == 0U, "fclose -> 0");
    check(host_size(host) == 20, "host file is 20 bytes (got %ld)",
          host_size(host));
    memset(page, 0, sizeof page);
    n = host_read(host, page, sizeof page);
    check(n == 20U && memcmp(page, expect, 20U) == 0,
          "host bytes match what the guest wrote (%u bytes)", (unsigned)n);

    /* --- read it back: ftell / fseek, byte-exact ---------------------- */
    tok = FOPEN("logs/deep/roundtrip.bin", "rb");
    check(tok != 0U, "fopen(rb) -> token 0x%08x", tok);
    rbuf = data(page, 0U);              /* reuse a data-area address */
    memset(isaac_arena_base + rbuf, 0, sizeof page);
    check(FREAD(rbuf, 4U, 2U, tok) == 2U, "fread 2 x 4 bytes -> 2 items");
    check(ld32(rbuf) == 0x1000U && ld32(rbuf + 4U) == 0x1001U,
          "first two words read back exactly (0x%08x 0x%08x)", ld32(rbuf),
          ld32(rbuf + 4U));
    check(FTELL(tok) == 8U, "ftell after 8 bytes -> %u", FTELL(tok));
    check(FSEEK(tok, 0, 0) == 0U, "fseek(0, SEEK_SET) -> 0");
    check(FTELL(tok) == 0U, "ftell -> 0");
    check(FREAD(rbuf, 4U, 5U, tok) == 5U, "fread to EOF -> 5 items");
    check(FREAD(rbuf, 4U, 1U, tok) == 0U, "fread past EOF -> 0 items");
    check(FSEEK(tok, -4, 2) == 0U, "fseek(-4, SEEK_END) -> 0");
    check(FTELL(tok) == 16U, "ftell -> 16 (byte offset, %u)", FTELL(tok));
    check(FREAD(rbuf, 4U, 1U, tok) == 1U &&
          ld32(rbuf) == (0x1000U + 4U), "last word read back (0x%08x)",
          ld32(rbuf));
    check(FSEEK(tok, 1, 7) != 0U, "fseek with a bogus origin -> failure");
    fd = FILENO(tok);
    check(fd >= 3U, "_fileno(token) -> %u (stable, non-negative)", fd);
    check(OSFHANDLE(fd) == tok, "_get_osfhandle(_fileno(t)) round-trips to "
          "the same token (0x%08x)", OSFHANDLE(fd));
    check(FCLOSE(tok) == 0U, "fclose -> 0");

    /* --- append + text-mode byte exactness ---------------------------- */
    tok = FOPEN("logs/deep/roundtrip.bin", "a");
    check(tok != 0U, "fopen(a) -> token 0x%08x", tok);
    {
        uint32_t word = 0x2000U;
        wbuf = data(&word, 4U);
    }
    check(FWRITE(wbuf, 4U, 1U, tok) == 1U, "fwrite -> 1 item");
    check(FCLOSE(tok) == 0U, "fclose -> 0");
    check(host_size(host) == 24, "append grew the file to 24 bytes (%ld)",
          host_size(host));

    /* "w" + "r" with no 'b': this layer does no CRLF translation, so what the
     * guest wrote is exactly what it reads back (byte offsets stay exact). */
    tok = FOPEN("text.txt", "w");
    check(tok != 0U, "fopen(text.txt, \"w\") -> token 0x%08x", tok);
    {
        static const char line[] = "line one\nline two\n";
        wbuf = data(line, sizeof line - 1U);
        check(FWRITE(wbuf, 1U, sizeof line - 1U, tok) == sizeof line - 1U,
              "fwrite %u text bytes -> same count", (unsigned)(sizeof line - 1U));
    }
    check(FCLOSE(tok) == 0U, "fclose -> 0");
    snprintf(host, sizeof host, "%s/text.txt", ROOT);
    check(host_size(host) == 18, "text file is 18 bytes, no CR inserted (%ld)",
          host_size(host));
    tok = FOPEN("text.txt", "r");
    memset(isaac_arena_base + rbuf, 0, 64U);
    check(FREAD(rbuf, 1U, 18U, tok) == 18U &&
          memcmp(isaac_arena_base + rbuf, "line one\nline two\n", 18U) == 0,
          "text read back byte-exact");
    check(FFLUSH(tok) == 0U, "fflush(stream) -> 0");
    check(FCLOSE(tok) == 0U, "fclose -> 0");
    check(FFLUSH(0) == 0U, "fflush(NULL) -> 0");

    /* --- errno on a genuine failure ----------------------------------- */
    SET_ERRNO(0);
    tok = FOPEN("no/such/file.dat", "rb");
    check(tok == 0U, "fopen(missing, rb) -> NULL (no inventing a token)");
    check(guest_errno() == 2, "guest errno is ENOENT(2), got %d", guest_errno());
    SET_ERRNO(99);
    check(guest_errno() == 99, "_set_errno/_errno share one word");
    check(ACCESS("text.txt", 0) == 0U, "_access(existing, 0) -> 0");
    check(ACCESS("no/such/file.dat", 0) != 0U,
          "_access(missing, 0) -> -1");
    check(REMOVE("text.txt") == 0U, "remove(existing) -> 0");
    check(host_size(host) == -1, "removed file is gone from the host");
    check(REMOVE("text.txt") != 0U, "remove(already gone) -> -1");

    /* --- path containment --------------------------------------------- */
    SET_ERRNO(0);
    check(FOPEN("../../etc/passwd", "rb") == 0U, "fopen(\"../..\") refused");
    check(guest_errno() == 22, "refused path reports EINVAL(22), got %d",
          guest_errno());
    check(FOPEN("C:/Windows/system32/config/SAM", "rb") == 0U,
          "fopen(\"C:/...\") re-rooted, not escaped");
    snprintf(host, sizeof host, "%s/rooted.bin", ROOT);
    {
        static const char bytes[4] = { 'A', 'B', 'C', 'D' };
        uint32_t word = 0;
        memcpy(&word, bytes, 4U);
        tok = FOPEN("rooted.bin", "wb");
        wbuf = data(bytes, 4U);
        check(tok != 0U && FWRITE(wbuf, 4U, 1U, tok) == 1U, "wrote rooted.bin");
        FCLOSE(tok);
    }
    snprintf(guest_rooted, sizeof guest_rooted, "%s/rooted.bin", ROOT);
    tok = FOPEN(guest_rooted, "rb");           /* already rooted: used as-is */
    check(tok != 0U, "fopen(\"<root>/rooted.bin\") -> token 0x%08x", tok);
    check(FREAD(rbuf, 4U, 1U, tok) == 1U &&
          memcmp(isaac_arena_base + rbuf, "ABCD", 4U) == 0,
          "rooted guest path read the same file");
    FCLOSE(tok);
    tok = FOPEN("/etc/passwd", "rb");          /* absolute, outside the root */
    check(tok == 0U, "fopen(\"/etc/passwd\") cannot leave the root");
    tok = FOPEN(ROOT, "rb");                   /* the root is a directory */
    check(tok == 0U, "fopen(directory) refused like Windows (EACCES)");
    check(guest_errno() == 13, "directory open reports EACCES(13), got %d",
          guest_errno());

    /* --- invalid / stale / foreign handles ---------------------------- */
    check(FREAD(rbuf, 4U, 1U, 0xDEADBEEFU) == 0U,
          "fread(unknown token) -> 0 items");
    check(FSEEK(0xDEADBEEFU, 0, 0) != 0U, "fseek(unknown token) -> failure");
    check(FTELL(0xDEADBEEFU) == 0xFFFFFFFFU, "ftell(unknown token) -> -1");
    check(FCLOSE(0xDEADBEEFU) != 0U, "fclose(unknown token) -> EOF");
    check(FILENO(0xDEADBEEFU) == 0xFFFFFFFFU, "_fileno(unknown token) -> -1");
    check(OSFHANDLE(9999U) == 0xFFFFFFFFU,
          "_get_osfhandle(bogus fd) -> -1");
    tok = FOPEN("rooted.bin", "rb");
    check(FCLOSE(tok) == 0U, "fclose(valid) -> 0");
    check(FCLOSE(tok) != 0U, "second fclose of the same token -> EOF (stale)");
    check(FILENO(tok) == 0xFFFFFFFFU, "_fileno(stale token) -> -1");
    check(CALL2("api-ms-win-crt-stdio-l1-1-0.dll!fopen", 0U, S("rb")) == 0U,
          "fopen(NULL path) -> NULL");
    check(CALL2("api-ms-win-crt-stdio-l1-1-0.dll!fopen", S("rooted.bin"), 0U)
          == 0U, "fopen(NULL mode) -> NULL");
    check(FOPEN("rooted.bin", "q") == 0U, "fopen(bogus mode) -> NULL");
    tok = FOPEN("rooted.bin", "rb");
    check(tok != 0U, "reopened rooted.bin for the bounds test (0x%08x)", tok);
    check(FREAD(rbuf, 0xFFFFFFFFU, 0xFFFFFFFFU, tok) == 0U,
          "fread with a wrapping guest size -> 0 items (arena refused)");
    check(FTELL(tok) == 0U, "the refused fread did not move the stream");
    check(FCLOSE(tok) == 0U, "fclose -> 0");

    /* --- tokens never collide with the standard streams ---------------- */
    check(FILENO(0x00010000U) == 0U, "_fileno(__acrt_iob_func(0)) -> 0");
    check(FILENO(0x00010001U) == 1U, "_fileno(__acrt_iob_func(1)) -> 1");
    check(FILENO(0x00010002U) == 2U, "_fileno(__acrt_iob_func(2)) -> 2");
    check(OSFHANDLE(1U) == 0x00010001U,
          "_get_osfhandle(1) -> the stdout token");
    check(FFLUSH(0x00010001U) == 0U, "fflush(stdout token) -> 0");
    {
        static const char banner[] = "[fwrite to the stdout token]\n";
        wbuf = data(banner, sizeof banner - 1U);
        check(FWRITE(wbuf, 1U, sizeof banner - 1U, 0x00010001U) ==
              sizeof banner - 1U, "fwrite(%u, stdout token) -> same count",
              (unsigned)(sizeof banner - 1U));
    }

    /* --- setvbuf: the mode is translated, the guest buffer is not used - */
    tok = FOPEN("setvbuf.bin", "wb");
    check(tok != 0U, "fopen(setvbuf.bin, wb) -> token 0x%08x", tok);
    check(SETVBUF(tok, 0U, 0x0004U, 0U) == 0U,
          "setvbuf(_IONBF) -> 0 (MSVC value translated)");
    check(SETVBUF(tok, 0x00200000U, 0x0000U, 4096U) == 0U,
          "setvbuf(_IOFBF, guest buffer) -> 0");
    check(SETVBUF(tok, 0U, 0x0012U, 0U) != 0U, "setvbuf(bogus mode) -> failure");
    check(FWRITE(wbuf, 1U, 4U, tok) == 4U, "fwrite after setvbuf -> 4 items");
    check(FCLOSE(tok) == 0U, "fclose -> 0");

    printf("=== %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail != 0 ? 1 : 0;
}
