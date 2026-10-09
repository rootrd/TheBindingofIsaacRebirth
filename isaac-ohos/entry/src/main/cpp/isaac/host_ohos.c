/* HarmonyOS host import layer — emulates Windows API calls from the recompiled
 * guest. Each handler reads args from the guest stack, performs the operation,
 * sets eax, and cleans up the stack per the calling convention.
 *
 * Calling convention:
 *   - Args at esp+4, esp+8, ... (esp points at return address)
 *   - stdcall: callee cleans args (esp += 4 + nargs*4)
 *   - cdecl: caller cleans args (esp += 4, only pop return addr)
 *   - Return value in eax
 *   - Guest pointers are arena offsets (isaac_arena_base + addr)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <errno.h>
#include <ctype.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>

#include "guest.h"
#include "guest_memory.h"
#include "msvcp_ret_table.h"

#include <hilog/log.h>
#define HOST_ERR(fmt, ...) OH_LOG_Print(LOG_APP, LOG_ERROR, 0, "isaacoh", fmt, ##__VA_ARGS__)

/* Bytes the real C++ runtime function pops (return address + arguments), or 0
 * for imports that are not part of it. The table is sorted, so binary search. */
static unsigned msvcp_ret_lookup(const char *name)
{
    unsigned lo = 0;
    unsigned hi = MSVCP_RET_COUNT;

    while (lo < hi) {
        unsigned mid = lo + (hi - lo) / 2U;
        int order = strcmp(s_msvcp_rets[mid].name, name);
        if (order == 0) return s_msvcp_rets[mid].pop;
        if (order < 0) lo = mid + 1U; else hi = mid;
    }
    return 0;
}

/* Host-owned words inside the flat guest arena, for imports that must hand
 * back a writable guest pointer (e.g. __p__commode). Placed above the image
 * (loaded at GUEST_IMAGE_BASE) and below the guest stack region. */
#define HOST_SCRATCH_ADDR 0x20000000U

#include "host_ext.h"

/* ---- extension modules ---------------------------------------------------
 * Files that own a large slice of the shim layer publish a descriptor; they
 * are consulted before the generated table so a module can replace an hDUMMY
 * entry. Declared weak so the tree builds before every module exists. */
/* All five extension modules have implementation files in the build, so the
 * symbols below resolve to their strong definitions. (The old weak stubs
 * here were lost to link-order differences on device builds, which silently
 * uninstalled host_ext_misc's 26 imports — FLS/TLS, _libm_sse2_*, roundf,
 * str*_s, FindFirstFileW — and stalled the guest early on device.) */

/* Slot order is load-bearing: the slot index is encoded into the dynamic
 * tokens a module issues, so appending is safe but reordering is not (and
 * host_ext_kage must stay at slot 1). Adding a module means adding a slot
 * here plus its weak stub above. */
static const host_ext_module *(*const s_ext_providers[])(void) = {
    host_ext_printf,
    host_ext_kage,
    host_ext_misc,
    host_ext_file,
    host_ext_path,
};
#define HOST_EXT_PROVIDER_COUNT \
    (sizeof s_ext_providers / sizeof s_ext_providers[0])

/* Enable logging of unimplemented imports */
#define HOST_IMPORT_LOG 1

/* ---------------------------------------------------------------- helpers */

static inline uint32_t host_arg(CPU *__restrict c, int index)
{
    return ld32(c->esp + 4U + (uint32_t)index * 4U);
}

static inline uint32_t host_arg_ptr(CPU *__restrict c, int index)
{
    return host_arg(c, index);
}

static inline char *host_str(CPU *__restrict c, int index)
{
    return (char *)(isaac_arena_base + host_arg(c, index));
}

static inline void *host_ptr(uint32_t addr)
{
    return isaac_arena_base + addr;
}

static inline void host_stdcall_ret(CPU *__restrict c, int nargs)
{
    c->esp += 4U + (uint32_t)nargs * 4U;
}

static inline void host_cdecl_ret(CPU *__restrict c)
{
    c->esp += 4U;
}

static inline void host_ret_eax(CPU *__restrict c, uint32_t val, int nargs, int stdcall)
{
    c->eax = val;
    if (stdcall) host_stdcall_ret(c, nargs);
    else host_cdecl_ret(c);
}

/* ---------------------------------------------------------------- handles */

/* We use a simple handle table for file/kernel handles.
 * Guest handles are indices + 0x10000 (to avoid 0/INVALID_HANDLE_VALUE). */
#define HANDLE_BASE 0x10000
#define MAX_HANDLES 256

typedef struct {
    int used;
    int fd;         /* POSIX file descriptor, -1 if not a file */
    uint32_t type;  /* 0=free, 1=file, 2=heap, 3=other */
} host_handle;

static host_handle g_handles[MAX_HANDLES];

static uint32_t host_handle_alloc(int fd, uint32_t type)
{
    for (int i = 0; i < MAX_HANDLES; i++) {
        if (!g_handles[i].used) {
            g_handles[i].used = 1;
            g_handles[i].fd = fd;
            g_handles[i].type = type;
            return HANDLE_BASE + (uint32_t)i;
        }
    }
    return 0; /* no free slots */
}

static host_handle *host_handle_get(uint32_t h)
{
    uint32_t idx = h - HANDLE_BASE;
    if (idx >= MAX_HANDLES || !g_handles[idx].used) return NULL;
    return &g_handles[idx];
}

static void host_handle_free(uint32_t h)
{
    host_handle *hd = host_handle_get(h);
    if (hd) {
        if (hd->fd >= 0) close(hd->fd);
        hd->used = 0;
        hd->fd = -1;
        hd->type = 0;
    }
}


/* ------------------------------------------------- archive ISAAC decoder */
/* Native re-implementation of the game's archive word-decoder sub_005b06f0
 * (the ISAAC-PRNG keystream XOR, prior art Bob Jenkins readable.c), ported
 * from the upstream Vita port's host_vita_archive_xor.c. The translated
 * decoder consumes the PRNG state through thousands of translated
 * instructions per 1024-byte block; when any part of that long chain drifts,
 * the inflate stage sees garbage and every archive entry fails its checksum.
 * This path reads the same guest state (decoder object -> ISAAC state) and
 * XORs the keystream natively, calling the translated refresh sub_005c2fd0
 * at the wrap point exactly where the binary does. */
void sub_005c2fd0(CPU *__restrict c);

#define ARCH_XOR_RETURN       0x0059c427U
#define ARCH_XOR_OBJECT_BYTES 0xc1cU
#define ARCH_XOR_OUTPUT_OFF   0x81cU
#define ARCH_XOR_OUTPUT_BYTES 0x400U
#define ARCH_XOR_STATE_BYTES  0x810U
#define ARCH_XOR_REFRESH_RET  0x005b072fU

int isaac_archive_xor_try(CPU *__restrict c)
{
    uint32_t output, count, object, decoder, state, index;
    uint32_t offset, key = 0U;
    uint32_t saved_ebp, saved_ebx, saved_esi, saved_edi, frame, stack;

    if (getenv("ISAAC_DBG_ARCHXOR") != NULL) {
        static int ax_ = 0;
        if (ax_ < 10) {
            ax_++;
            fprintf(stderr, "[DBG archxor#%d] ret=%08x arg1=%08x arg2=%u ecx=%08x esi=%u edi=%08x ebx=%08x\n",
                    ax_, (unsigned)ld32(c->esp), (unsigned)ld32(c->esp + 4U),
                    (unsigned)ld32(c->esp + 8U), (unsigned)c->ecx,
                    (unsigned)c->esi, (unsigned)c->edi, (unsigned)c->ebx);
        }
    }
    if (getenv("ISAAC_NO_ARCH_XOR") != NULL) {
        return 0;
    }
    if (c->fault || ld32(c->esp) != ARCH_XOR_RETURN) {
        return 0;
    }
    output = ld32(c->esp + 4U);
    count = ld32(c->esp + 8U);
    object = c->edi;
    decoder = c->ecx;
    stack = c->esp;
    (void)stack;
    if (count == 0U || count > ARCH_XOR_OUTPUT_BYTES || count != c->esi ||
        object == 0U || output != object + ARCH_XOR_OUTPUT_OFF ||
        output != c->ebx || decoder == 0U ||
        ld32(object + 0x14U) != decoder) {
        return 0;
    }
    state = ld32(decoder);
    if (ld8(decoder + 0x10U) != 1U || state == 0U) {
        return 0;
    }
    index = ld32(state);
    if (index >= 256U) {
        return 0;
    }

    saved_ebp = c->ebp;
    saved_ebx = c->ebx;
    saved_esi = c->esi;
    saved_edi = c->edi;
    gpush_generated(c, c->ebp);
    frame = c->esp;
    c->ebp = frame;
    gpush_generated(c, decoder);
    gpush_generated(c, saved_ebx);
    gpush_generated(c, saved_esi);
    c->esi = count;
    gpush_generated(c, saved_edi);
    c->edi = output;

    for (offset = 0U; offset < count; ) {
        uint32_t bytes = count - offset;
        uint32_t value, byte;

        if (bytes > 4U) {
            bytes = 4U;
        }
        key = ld32(state + 4U + index * 4U);
        ++index;
        st32(state, index);
        if (index == 256U) {
            uint32_t ret_slot;
            c->eax = index;
            c->ecx = state;
            c->edx = key;
            c->ebx = offset;
            gpush_generated(c, ARCH_XOR_REFRESH_RET);
            ret_slot = c->esp;
            (void)ret_slot;
            sub_005c2fd0(c);
            if (c->fault) {
                c->eax = decoder;
                c->ecx = state;
                c->esp = frame;
                c->ebp = saved_ebp;
                c->ebx = saved_ebx;
                c->esi = saved_esi;
                c->edi = saved_edi;
                c->esp += 4U + 4U + 8U;   /* our pushed ebp + ret + two stack args */
                return 1;
            }
            index = 0U;
            st32(state, index);
        }
        if (bytes == 4U) {
            value = ld32(output + offset);
            value ^= key;
            st32(output + offset, value);
        } else {
            for (byte = 0U; byte < bytes; ++byte) {
                st8(output + offset + byte,
                    (uint8_t)(ld8(output + offset + byte) ^
                              (uint8_t)(key >> (8U * byte))));
            }
        }
        offset += bytes;
    }

    c->eax = decoder;
    c->ecx = state;
    c->edx = key >> (8U * ((count - 1U) & 3U));
    c->edi = saved_edi;
    c->esi = saved_esi;
    c->ebx = saved_ebx;
    c->esp = frame;
    c->ebp = saved_ebp;
    /* frame is the esp AFTER our own `push ebp`, so the epilogue of the
     * emulated `ret 8` must also walk past that slot: callee-cleanup leaves
     * esp at entry+12, and stopping at frame+12 lands 4 short, which showed
     * up as a steady 4-byte stack leak per decrypted block (the caller then
     * mis-restored esi and called through a garbage object). */
    c->esp += 4U + 4U + 8U;
    return 1;
}

/* ---------------------------------------------------------------- heap */

/* Guest malloc/free: delegate to host malloc, but track addresses.
 * The guest passes us guest-address-space pointers. We allocate on the host
 * and return a guest address by allocating within the arena's heap region. */
/* For simplicity, use the arena's high memory region (0x40000000-0x7FFFFFFF)
 * for heap allocations. We maintain a simple bump allocator. */
/* Host-side bump heap. 0x40000000 was the original base, but the guest's
 * own large VirtualAlloc reservations (the Lua state reserves exactly 1 GiB,
 * and other subsystems reserve with preferred bases near there) land in the
 * same window and their cleanup/reuse wiped live host allocations -- the
 * volume container built by sub_0050c740 lost two of its three slots that
 * way and all 22 archive mounts failed. 0x60000000 is far from every
 * guest-preferred base observed so far. */
static uint32_t g_heap_ptr = 0x60000000U;
static uint32_t g_heap_high_water = 0x60000000U;

static uint32_t host_arena_alloc(uint32_t size)
{
    /* Align to 16 bytes */
    size = (size + 15U) & ~15U;
    uint32_t addr = g_heap_ptr;
    g_heap_ptr += size;
    if (g_heap_ptr > g_heap_high_water + 0x4000000U) {
        g_heap_high_water = g_heap_ptr;
        HOST_ERR("heap: high water 0x%{public}08x (request=%{public}u)",
                 g_heap_ptr, size);
    }
    /* The ceiling used to be 0x70000000 (256 MB above the base), which the
     * engine's own allocation traffic exhausts mid-init: the SEH dispatcher
     * then failed to get its 80-byte record and every throw faulted. The
     * arena is 4 GiB with the string bump area at 0xF0000000, so hand the
     * heap most of it. */
    if (g_heap_ptr >= 0xE0000000U) {
        /* Out of arena heap space */
        HOST_ERR("heap: EXHAUSTED request=%{public}u heap=0x%{public}08x",
                 size, g_heap_ptr);
        return 0;
    }
    return addr;
}

/* ---------------------------------------------------------------- time */

static uint64_t g_qpc_freq = 10000000ULL; /* 10 MHz */
static uint64_t g_qpc_start;

static void host_qpc_init(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    g_qpc_start = (uint64_t)ts.tv_sec * 10000000ULL + (uint64_t)ts.tv_nsec / 100ULL;
}

/* ---------------------------------------------------------------- imports */

typedef void (*host_import_fn)(CPU *__restrict);

typedef struct {
    const char *name;
    host_import_fn fn;
} host_import_entry;

/* Forward declarations for all handlers */
static void h_InitializeSListHead(CPU *c);
static void h_IsProcessorFeaturePresent(CPU *c);
static void h_TerminateProcess(CPU *c);
static void h_GetProcessTimes(CPU *c);
static void h_SetUnhandledExceptionFilter(CPU *c);
static void h_GetCurrentProcess(CPU *c);
static void h_GetCurrentProcessId(CPU *c);
static void h_GetCurrentThread(CPU *c);
static void h_GetCurrentThreadId(CPU *c);
static void h_GetTickCount(CPU *c);
static void h_QueryPerformanceCounter(CPU *c);
static void h_QueryPerformanceFrequency(CPU *c);
static void h_GetSystemTime(CPU *c);
static void h_GetLocalTime(CPU *c);
static void h_GetEnvironmentVariableA(CPU *c);
static void h_GetModuleHandleA(CPU *c);
static void h_GetModuleFileNameA(CPU *c);
static void h_GetProcAddress(CPU *c);
static void h_LoadLibraryA(CPU *c);
static void h_FreeLibrary(CPU *c);
static void h_VirtualAlloc(CPU *c);
static void h_VirtualFree(CPU *c);
static void h_VirtualProtect(CPU *c);
static void h_GetProcessHeap(CPU *c);
static void h_HeapAlloc(CPU *c);
static void h_HeapFree(CPU *c);
static void h_HeapCreate(CPU *c);
static void h_HeapDestroy(CPU *c);
static void h_GetLastError(CPU *c);
static void h_SetLastError(CPU *c);
static void h_CreateFileA(CPU *c);
static void h_ReadFile(CPU *c);
static void h_WriteFile(CPU *c);
static void h_CloseHandle(CPU *c);
static void h_GetFileSize(CPU *c);
static void h_SetFilePointer(CPU *c);
static void h_FlushFileBuffers(CPU *c);
static void h_GetFileAttributesA(CPU *c);
static void h_CreateDirectoryA(CPU *c);
static void h_RemoveDirectoryA(CPU *c);
static void h_DeleteFileA(CPU *c);
static void h_MoveFileA(CPU *c);
static void h_FindFirstFileA(CPU *c);
static void h_FindNextFileA(CPU *c);
static void h_FindClose(CPU *c);
static void h_MultiByteToWideChar(CPU *c);
static void h_WideCharToMultiByte(CPU *c);
static void h_GetSystemTimeAsFileTime(CPU *c);
static void h_GetTimeZoneInformation(CPU *c);
static void h_Sleep(CPU *c);
static void h_GetStdHandle(CPU *c);
static void h_SetConsoleCtrlHandler(CPU *c);
static void h_ExitProcess(CPU *c);
static void h_GetConsoleMode(CPU *c);
static void h_SetConsoleMode(CPU *c);
static void h_GetFileType(CPU *c);
static void h_GetStartupInfoA(CPU *c);
static void h_GetCommandLineA(CPU *c);
static void h_GetVersionExA(CPU *c);
static void h_GetSystemInfo(CPU *c);
static void h_GlobalMemoryStatusEx(CPU *c);
static void h_OutputDebugStringA(CPU *c);
static void h_cxx_throw(CPU *c);
static void h_RaiseException(CPU *c);
static void h_vcruntime_memcpy(CPU *c);
static void h_vcruntime_memmove(CPU *c);
static void h_vcruntime_memset(CPU *c);
static void h_vcruntime_memchr(CPU *c);
static void h_vcruntime_strstr(CPU *c);
static void h_UnhandledExceptionFilter(CPU *c);
static void h_SetHandleCount(CPU *c);
static void h_FlushInstructionCache(CPU *c);
static void h_GetCurrentActCtx(CPU *c);
static void h_ActivateActCtx(CPU *c);
static void h_DeactivateActCtx(CPU *c);
static void h_ReleaseActCtx(CPU *c);
static void h_CreateActCtxA(CPU *c);
static void h_HeapSize(CPU *c);
static void h_HeapReAlloc(CPU *c);
static void h_HeapValidate(CPU *c);
static void h_GetTempPathA(CPU *c);
static void h_GetTempFileNameA(CPU *c);
static void h_lstrlenA(CPU *c);
static void h_lstrcpyA(CPU *c);
static void h_lstrcpynA(CPU *c);
static void h_lstrcmpA(CPU *c);
static void h_lstrcmpiA(CPU *c);
static void h_WaitForSingleObject(CPU *c);
static void h_WaitForMultipleObjects(CPU *c);
static void h_CreateThread(CPU *c);
static void h_ResumeThread(CPU *c);
static void h_SuspendThread(CPU *c);
static void h_TerminateThread(CPU *c);
static void h_GetExitCodeThread(CPU *c);
static void h_SetThreadPriority(CPU *c);
static void h_GetThreadPriority(CPU *c);
static void h_LeaveCriticalSection(CPU *c);
static void h_EnterCriticalSection(CPU *c);
static void h_InitializeCriticalSection(CPU *c);
static void h_DeleteCriticalSection(CPU *c);
static void h_TryEnterCriticalSection(CPU *c);
static void h_CreateMutexA(CPU *c);
static void h_ReleaseMutex(CPU *c);
static void h_CreateEventA(CPU *c);
static void h_CreateEventW(CPU *c);
static void h_WaitForSingleObjectEx(CPU *c);
static void h_InitializeCriticalSectionAndSpinCount(CPU *c);
static void h_SetEvent(CPU *c);
static void h_ResetEvent(CPU *c);
static void h_InterlockedIncrement(CPU *c);
static void h_InterlockedDecrement(CPU *c);
static void h_InterlockedExchange(CPU *c);
static void h_InterlockedExchangeAdd(CPU *c);
static void h_InterlockedCompareExchange(CPU *c);

/* CRT imports */
static void h_crt_malloc(CPU *c);
static void h_crt_free(CPU *c);
static void h_crt_calloc(CPU *c);
static void h_crt_realloc(CPU *c);
static void h_crt__callnewh(CPU *c);
static void h_crt__set_new_mode(CPU *c);
static void h_crt_memset(CPU *c);
static void h_crt_memcpy(CPU *c);
static void h_crt_memmove(CPU *c);
static void h_crt_memcmp(CPU *c);
static void h_crt_strlen(CPU *c);
static void h_crt_strcpy(CPU *c);
static void h_crt_strncpy(CPU *c);
static void h_crt_strcmp(CPU *c);
static void h_crt_strncmp(CPU *c);
static void h_crt__strdup(CPU *c);
static void h_crt__strnicmp(CPU *c);
static void h_crt_strpbrk(CPU *c);
static void h_crt_strchr(CPU *c);
static void h_crt_strrchr(CPU *c);
static void h_crt_strstr(CPU *c);
static void h_crt_tolower(CPU *c);
static void h_crt_toupper(CPU *c);
static void h_crt_ispunct(CPU *c);
static void h_crt_isspace(CPU *c);
static void h_crt_isdigit(CPU *c);
static void h_crt_isalpha(CPU *c);
static void h_crt_isalnum(CPU *c);
static void h_crt_isupper(CPU *c);
static void h_crt_islower(CPU *c);
static void h_crt_isxdigit(CPU *c);
static void h_crt_atoi(CPU *c);
static void h_crt_atof(CPU *c);
static void h_crt_strtol(CPU *c);
static void h_crt_strtoul(CPU *c);
static void h_crt_strtoull(CPU *c);
static void h_crt_sprintf(CPU *c);
static void h_crt__stricmp(CPU *c);
static void h_crt_qsort(CPU *c);
static void h_crt_ceil(CPU *c);
static void h_crt_floor(CPU *c);
static void h_crt_ldexp(CPU *c);
static void h_crt_copysignf(CPU *c);
static void h_crt_sqrt(CPU *c);
static void h_crt_fmod(CPU *c);
static void h_crt_pow(CPU *c);
static void h_crt_sin(CPU *c);
static void h_crt_cos(CPU *c);
static void h_crt_tan(CPU *c);
static void h_crt_atan(CPU *c);
static void h_crt_atan2(CPU *c);
static void h_crt_asin(CPU *c);
static void h_crt_acos(CPU *c);
static void h_crt_exp(CPU *c);
static void h_crt_log(CPU *c);
static void h_crt_log10(CPU *c);
static void h_crt_fabs(CPU *c);
static void h_crt_modf(CPU *c);
static void h_crt_frexp(CPU *c);
static void h_crt__fdclass(CPU *c);
static void h_crt__controlfp_s(CPU *c);
static void h_crt__initterm(CPU *c);
static void h_crt__initterm_e(CPU *c);
static void h_crt__get_initial_narrow_environment(CPU *c);
static void h_crt__register_thread_local_exe_atexit_callback(CPU *c);
static void h_crt__initialize_onexit_table(CPU *c);
static void h_crt__register_onexit_function(CPU *c);
static void h_crt__execute_onexit_table(CPU *c);
static void h_crt__crt_atexit(CPU *c);
static void h_crt__crt_at_quick_exit(CPU *c);
static void h_crt_getenv(CPU *c);
static void h_crt__configthreadlocale(CPU *c);
static void h_crt_remove(CPU *c);
static void h_crt__access(CPU *c);
static void h_crt_mbstowcs_s(CPU *c);
static void h_crt_wcstombs_s(CPU *c);
static void h_crt_fread(CPU *c);
static void h_crt_fwrite(CPU *c);
static void h_crt_fseek(CPU *c);
static void h_crt_ftell(CPU *c);
static void h_crt__fileno(CPU *c);
static void h_crt_fopen(CPU *c);
static void h_crt_fclose(CPU *c);
static void h_crt_fflush(CPU *c);
static void h_crt__get_osfhandle(CPU *c);
static void h_crt_setvbuf(CPU *c);
static void h_crt_strftime(CPU *c);
static void h_crt__gmtime64(CPU *c);
static void h_crt__time64(CPU *c);
static void h_crt__mkgmtime64(CPU *c);
static void h_crt__localtime64(CPU *c);
static void h_crt__setjmp3(CPU *c);
static void h_crt_strtoll(CPU *c);
static void h_crt__strnlen(CPU *c);
static void h_crt__errno(CPU *c);
static void h_crt_abort(CPU *c);
static void h_crt__initialize_narrow_environment(CPU *c);

/* Lua imports — link to native Lua 5.3 */
static void h_lua_stub(CPU *c);

/* OpenGL imports — map to EGL/GLES */
static void h_wglCreateContext(CPU *c);
static void h_wglDeleteContext(CPU *c);
static void h_wglMakeCurrent(CPU *c);
static void h_wglGetProcAddress(CPU *c);
static void h_wglShareLists(CPU *c);

/* USER32 imports */
static void h_user32_stub(CPU *c);
static void h_GetCursorPos(CPU *c);
static void h_SetCursorPos(CPU *c);
static void h_ShowCursor(CPU *c);
static void h_GetMessageA(CPU *c);
static void h_PeekMessageA(CPU *c);
static void h_DispatchMessageA(CPU *c);
static void h_TranslateMessage(CPU *c);
static void h_PostQuitMessage(CPU *c);
static void h_MessageBoxA(CPU *c);
static void h_GetForegroundWindow(CPU *c);
static void h_SetForegroundWindow(CPU *c);
static void h_GetDesktopWindow(CPU *c);
static void h_GetWindowRect(CPU *c);
static void h_GetClientRect(CPU *c);
static void h_AdjustWindowRect(CPU *c);
static void h_CreateWindowExA(CPU *c);
static void h_DestroyWindow(CPU *c);
static void h_ShowWindow(CPU *c);
static void h_UpdateWindow(CPU *c);
static void h_SetWindowLongA(CPU *c);
static void h_GetWindowLongA(CPU *c);
static void h_DefWindowProcA(CPU *c);
static void h_RegisterClassExA(CPU *c);
static void h_UnregisterClassA(CPU *c);
static void h_SetWindowPos(CPU *c);
static void h_ScreenToClient(CPU *c);
static void h_ClientToScreen(CPU *c);
static void h_GetSystemMetrics(CPU *c);
static void h_SetCapture(CPU *c);
static void h_ReleaseCapture(CPU *c);
static void h_SetCursor(CPU *c);
static void h_SetFocus(CPU *c);
static void h_ClipCursor(CPU *c);
static void h_BringWindowToTop(CPU *c);
static void h_GetKeyState(CPU *c);
static void h_GetAsyncKeyState(CPU *c);
static void h_GetKeyboardState(CPU *c);
static void h_MapVirtualKeyA(CPU *c);
static void h_ToAsciiEx(CPU *c);
static void h_GetKeyNameTextA(CPU *c);
static void h_EnumDisplaySettingsA(CPU *c);
static void h_ChangeDisplaySettingsA(CPU *c);
static void h_SetWindowTextA(CPU *c);
static void h_GetWindowTextA(CPU *c);
static void h_GetActiveWindow(CPU *c);
static void h_SetActiveWindow(CPU *c);
static void h_IsWindowVisible(CPU *c);
static void h_IsWindow(CPU *c);
static void h_InvalidateRect(CPU *c);
static void h_ValidateRect(CPU *c);
static void h_RedrawWindow(CPU *c);
static void h_BeginPaint(CPU *c);
static void h_EndPaint(CPU *c);
static void h_GetDC(CPU *c);
static void h_ReleaseDC(CPU *c);
static void h_PixelFormat(CPU *c);

/* GDI32 imports */
static void h_gdi32_stub(CPU *c);
static void h_SetPixelFormat(CPU *c);
static void h_GetDeviceCaps(CPU *c);
static void h_DeleteDC(CPU *c);
static void h_DescribePixelFormat(CPU *c);
static void h_SwapBuffers(CPU *c);
static void h_CreateDCA(CPU *c);
static void h_ChoosePixelFormat(CPU *c);

/* OpenAL imports */
static void h_openal_stub(CPU *c);

/* Steam imports */
static void h_steam_stub(CPU *c);

/* Other DLL stubs */
static void h_advapi32_stub(CPU *c);
static void h_shell32_stub(CPU *c);
static void h_ole32_stub(CPU *c);
static void h_dbghelp_stub(CPU *c);
static void h_winmm_stub(CPU *c);
static void h_bcrypt_stub(CPU *c);
static void h_msvcp_stub(CPU *c);
static void h_msvcp_streambuf_ctor(CPU *c);
static void h_msvcp_ios_ctor(CPU *c);
static void h_msvcp_ipfx(CPU *c);
static void h_msvcp_setstate(CPU *c);
static void h_msvcp_sgetc(CPU *c);
static void h_msvcp_sbumpc(CPU *c);
static void h_msvcp_snextc(CPU *c);
static void h_vcruntime_stub(CPU *c);
static void h_vcruntime_longjmp(CPU *c);
static void h_pack_vtable_stub(CPU *c);

/* VCRUNTIME specific */
static void h__RTDynamicCast(CPU *c);
static void h__except_handler4_common(CPU *c);
static void h__current_exception_context(CPU *c);
static void h_strchr_vcruntime(CPU *c);

static void hDUMMY(CPU *c);

/* Sorted import table — generated by gen_host_imports_table.py */
#include "host_imports_table.h"

/* Dummy handler for unimplemented but non-critical imports */
static void hDUMMY(CPU *c)
{
    c->eax = 0;
    host_cdecl_ret(c);
}


static void h_InitializeSListHead(CPU *c)
{
    uint32_t list_head = host_arg(c, 0);
    if (list_head) {
        st64(list_head, 0);
    }
    host_stdcall_ret(c, 1);
}

/* VCRUNTIME generic stub for functions without a specific host
 * implementation (RTTI helpers, exception internals, etc.) */
static void h_vcruntime_stub(CPU *c)
{
    c->eax = 0;
    host_stdcall_ret(c, 0);
}

static void h_IsProcessorFeaturePresent(CPU *c)
{
    uint32_t feature = host_arg(c, 0);
    (void)feature;
    c->eax = 0; /* Report no special features */
    host_stdcall_ret(c, 1);
}

static void h_TerminateProcess(CPU *c)
{
    uint32_t handle = host_arg(c, 0);
    uint32_t exit_code = host_arg(c, 1);
    (void)handle; (void)exit_code;
    guest_exit(c, (int32_t)exit_code, "TerminateProcess");
    /* not reached */
}

static void h_GetProcessTimes(CPU *c)
{
    /* Return success with zero times */
    c->eax = 1;
    host_stdcall_ret(c, 5);
}

static void h_SetUnhandledExceptionFilter(CPU *c)
{
    uint32_t filter = host_arg(c, 0);
    (void)filter;
    c->eax = 0; /* no previous filter */
    host_stdcall_ret(c, 1);
}

static void h_GetCurrentProcess(CPU *c)
{
    c->eax = 0xFFFFFFFFU; /* pseudo handle */
    host_stdcall_ret(c, 0);
}

static void h_GetCurrentProcessId(CPU *c)
{
    c->eax = (uint32_t)getpid();
    host_stdcall_ret(c, 0);
}

static void h_GetCurrentThread(CPU *c)
{
    c->eax = 0xFFFFFFFEU; /* pseudo handle */
    host_stdcall_ret(c, 0);
}

static void h_GetCurrentThreadId(CPU *c)
{
    c->eax = (uint32_t)(uintptr_t)pthread_self();
    host_stdcall_ret(c, 0);
}

static void h_GetTickCount(CPU *c)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    uint32_t ms = (uint32_t)((uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000);
    c->eax = ms;
    host_stdcall_ret(c, 0);
}

static void h_QueryPerformanceCounter(CPU *c)
{
    uint32_t ptr = host_arg(c, 0);
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    uint64_t count = (uint64_t)ts.tv_sec * 10000000ULL + (uint64_t)ts.tv_nsec / 100ULL - g_qpc_start;
    if (ptr) st64(ptr, count);
    c->eax = 1;
    host_stdcall_ret(c, 1);
}

static void h_QueryPerformanceFrequency(CPU *c)
{
    uint32_t ptr = host_arg(c, 0);
    if (ptr) st64(ptr, g_qpc_freq);
    c->eax = 1;
    host_stdcall_ret(c, 1);
}

static void h_GetSystemTime(CPU *c)
{
    uint32_t ptr = host_arg(c, 0);
    if (ptr) {
        time_t now = time(NULL);
        struct tm *utc = gmtime(&now);
        st16(ptr + 0, (uint16_t)utc->tm_year + 1900);
        st16(ptr + 2, (uint16_t)utc->tm_mon + 1);
        st16(ptr + 4, (uint16_t)utc->tm_wday);
        st16(ptr + 6, (uint16_t)utc->tm_mday);
        st16(ptr + 8, (uint16_t)utc->tm_hour);
        st16(ptr + 10, (uint16_t)utc->tm_min);
        st16(ptr + 12, (uint16_t)utc->tm_sec);
        st16(ptr + 14, 0);
    }
    host_stdcall_ret(c, 1);
}

static void h_GetLocalTime(CPU *c)
{
    uint32_t ptr = host_arg(c, 0);
    if (ptr) {
        time_t now = time(NULL);
        struct tm *local = localtime(&now);
        st16(ptr + 0, (uint16_t)local->tm_year + 1900);
        st16(ptr + 2, (uint16_t)local->tm_mon + 1);
        st16(ptr + 4, (uint16_t)local->tm_wday);
        st16(ptr + 6, (uint16_t)local->tm_mday);
        st16(ptr + 8, (uint16_t)local->tm_hour);
        st16(ptr + 10, (uint16_t)local->tm_min);
        st16(ptr + 12, (uint16_t)local->tm_sec);
        st16(ptr + 14, 0);
    }
    host_stdcall_ret(c, 1);
}

static void h_GetEnvironmentVariableA(CPU *c)
{
    uint32_t name_ptr = host_arg(c, 0);
    uint32_t buf_ptr = host_arg(c, 1);
    uint32_t buf_size = host_arg(c, 2);
    (void)name_ptr; (void)buf_ptr; (void)buf_size;
    c->eax = 0; /* env var not found */
    host_stdcall_ret(c, 3);
}

static void h_GetModuleHandleA(CPU *c)
{
    uint32_t name_ptr = host_arg(c, 0);
    (void)name_ptr;
    c->eax = GUEST_IMAGE_BASE; /* return image base as module handle */
    host_stdcall_ret(c, 1);
}

static void h_GetModuleFileNameA(CPU *c)
{
    uint32_t module = host_arg(c, 0);
    uint32_t buf_ptr = host_arg(c, 1);
    uint32_t buf_size = host_arg(c, 2);
    (void)module;
    /* The drive letter the guest sees for the data root. The engine's path
     * resolver (sub_00563120) classifies a path as absolute ONLY when byte[1]
     * is ':' and byte[2] is '\' or '/' — a POSIX-style root fails that test,
     * gets treated as relative, is matched against the mount-point table
     * (still empty during archive loading), and every
     * "resources/packed/*.a" mount then fails without a single file call
     * reaching the host. "Z:" is a fake drive standing for the data root;
     * the host file layer strips a leading "X:" prefix during
     * normalisation, so "Z:\resources\packed\f.a" maps to
     * "$ISAAC_DATA_ROOT/resources/packed/f.a". */
#define HOST_GUEST_DRIVE "Z:"
#define HOST_GUEST_DRIVE_PATH "Z:/"
    /* The engine derives its game root by stripping the file name off this
     * path; see HOST_GUEST_DRIVE above for why the fake drive is required. */
    const char *path = HOST_GUEST_DRIVE_PATH "game/isaac-ng.exe";
    uint32_t len = (uint32_t)strlen(path);
    if (len >= buf_size) len = buf_size - 1;
    if (buf_ptr && buf_size) {
        memcpy(host_ptr(buf_ptr), path, len);
        ((char *)host_ptr(buf_ptr))[len] = 0;
    }
    c->eax = len;
    host_stdcall_ret(c, 3);
}

/* ---- dynamic (GetProcAddress) tokens ------------------------------------
 *
 * A token is HOST_DYN_BASE | index into s_dyn_entries, and every token the
 * guest receives must have an entry: it will call the token, and
 * guest_host_dynamic() declining it surfaces as "untranslated address" at a
 * site far from the cause. Unknown names therefore return 0 (absent), which
 * is what this guest already handles for optional entry points.
 *
 * The previous implementation handed out a fresh counter token for every
 * request regardless of the name, so the 8th request produced 0x7d000007 --
 * a token no handler owned. */
/* Data root the guest sees as its "user profile": where the game will put
 * saves, options and (later) look for resources. Configurable so the host
 * harness and the HAP can point at different sandboxes. */
static const char *host_data_root(void)
{
    const char *env = getenv("ISAAC_DATA_ROOT");
    return (env != NULL && env[0] != '\0') ? env : "isaac-data";
}

static void h_dyn_GetUserProfileDirectoryA(CPU *c)
{
    uint32_t dir_ptr = host_arg(c, 1);
    uint32_t size_ptr = host_arg(c, 2);
    const char *root = host_data_root();
    uint32_t need = (uint32_t)strlen(root) + 1U;
    uint32_t cap = (size_ptr != 0U) ? ld32(size_ptr) : 0U;
    int ok = (dir_ptr != 0U && cap >= need);

    if (ok) {
        memcpy(isaac_arena_base + dir_ptr, root, need);
    }
    if (size_ptr != 0U) {
        st32(size_ptr, need);
    }
    HOST_ERR("GetUserProfileDirectoryA: root=\"%s\" cap=%u ok=%d", root, cap, ok);
    c->eax = ok ? 1U : 0U;
    host_stdcall_ret(c, 3);
}

/* The guest's current directory is the same data root: it walks relative
 * paths from here to find resources/saves. Never a host path. */
static void h_GetCurrentDirectoryA(CPU *c)
{
    uint32_t cap = host_arg(c, 0);
    uint32_t buf = host_arg(c, 1);
    const char *root = HOST_GUEST_DRIVE_PATH;
    uint32_t need = (uint32_t)strlen(root) + 1U;

    if (buf != 0U && cap >= need) {
        memcpy(isaac_arena_base + buf, root, need);
        c->eax = need - 1U; /* length, terminator excluded */
    } else {
        c->eax = need;      /* required size, terminator included */
    }
    HOST_ERR("GetCurrentDirectoryA: root=\"%s\" cap=%u -> %u", root, cap, c->eax);
    host_stdcall_ret(c, 2);
}

#define HOST_DYN_BASE 0x7d000000U
typedef struct {
    const char *name;
    void (*fn)(CPU *);
} host_dynamic_entry;

static const host_dynamic_entry s_dyn_entries[] = {
    { "GetUserProfileDirectoryA", h_dyn_GetUserProfileDirectoryA },
};
#define HOST_DYN_COUNT (sizeof s_dyn_entries / sizeof s_dyn_entries[0])

int guest_host_dynamic(CPU *__restrict c, uint32_t token)
{
    uint32_t index = token - HOST_DYN_BASE;
    uint32_t mi;
    uint32_t ei;

    if (index < HOST_DYN_COUNT) {
        s_dyn_entries[index].fn(c);
        return 1;
    }

    /* Extension-module entry: (module_index << 16) | entry_index. */
    mi = (index >> 16) & 0xFFFFU;
    ei = index & 0xFFFFU;
    if (mi != 0U) {
        size_t p;
        for (p = 0; p < HOST_EXT_PROVIDER_COUNT; p++) {
            const host_ext_module *m = s_ext_providers[p]();
            if (m == NULL || m->dynamic == NULL) {
                continue;
            }
            if (mi == (uint32_t)(p + 1U) && ei < m->dynamic_count) {
                m->dynamic[ei].fn(c);
                return 1;
            }
        }
    }
    return 0;
}

static void h_GetProcAddress(CPU *c)
{
    uint32_t module = host_arg(c, 0);
    uint32_t name_ptr = host_arg(c, 1);
    uint32_t i;

    if (name_ptr < 0x10000U) {
        HOST_ERR("GetProcAddress: ordinal %u from module 0x%08x -> 0", name_ptr, module);
        c->eax = 0;
        host_stdcall_ret(c, 2);
        return;
    }

    const char *name = (const char *)(isaac_arena_base + name_ptr);
    for (i = 0; i < HOST_DYN_COUNT; i++) {
        if (strcmp(s_dyn_entries[i].name, name) == 0) {
            c->eax = HOST_DYN_BASE | i;
            host_stdcall_ret(c, 2);
            return;
        }
    }

    /* Extension modules may own dynamic entries too. Token layout for them:
     * HOST_DYN_BASE | (module_index << 16) | entry_index, module_index being
     * 1-based so 0 unambiguously means the built-in table above. */
    {
        size_t p;
        for (p = 0; p < HOST_EXT_PROVIDER_COUNT; p++) {
            const host_ext_module *m = s_ext_providers[p]();
            unsigned j;
            if (m == NULL || m->dynamic == NULL) {
                continue;
            }
            for (j = 0; j < m->dynamic_count; j++) {
                if (strcmp(m->dynamic[j].name, name) == 0) {
                    c->eax = HOST_DYN_BASE | (uint32_t)((p + 1U) << 16) | (uint32_t)j;
                    host_stdcall_ret(c, 2);
                    return;
                }
            }
        }
    }

    HOST_ERR("GetProcAddress: %s (module 0x%08x) UNMAPPED -> 0", name, module);
    c->eax = 0;
    host_stdcall_ret(c, 2);
}

static void h_LoadLibraryA(CPU *c)
{
    uint32_t name_ptr = host_arg(c, 0);
    (void)name_ptr;
    c->eax = GUEST_IMAGE_BASE; /* pretend all DLLs are loaded */
    host_stdcall_ret(c, 1);
}

static void h_FreeLibrary(CPU *c)
{
    c->eax = 1;
    host_stdcall_ret(c, 1);
}

/* Host implementation of sub_00563120 (the engine's "resolve to absolute"
 * step). Called from the generated function via the hook at its top.
 *
 * The engine classifies a path as absolute only when byte[1] is ':' and
 * byte[2] is '/' or '\'; everything else goes through the volume-container
 * lookup, which on this runtime never matches, so every relative path
 * ("resources/packed/animations.a", "kage_mount_points.dat") failed
 * resolution and the mount printed "Failed to open" without ever touching a
 * file API. Here a relative path is simply re-homed under the fake Z: drive
 * (see HOST_GUEST_DRIVE): the result passes the engine's absolute test, and
 * the CRT fopen shim strips "Z:" and rejoins it under ISAAC_DATA_ROOT.
 * Returns 1 when c->eax carries the final answer (0 = refused). */
int isaac_hook_resolve_path(CPU *__restrict c)
{
    static uint32_t s_scratch;
    uint32_t path_ptr = *(uint32_t *)host_ptr(c->esp + 4U);
    const char *src;
    char tmp[520];
    uint32_t len, out;
    unsigned i;

    if (path_ptr == 0) {
        c->eax = 0;
        return 1;
    }
    src = (const char *)host_ptr(path_ptr);
    len = (uint32_t)strnlen(src, 400U);
    if (len == 0 || len >= sizeof tmp - 16U) {
        c->eax = 0;
        return 1;
    }
    /* Absolute ("X:...") or already re-homed: let the original body run. */
    if (len > 1U && src[1] == ':') {
        return 0;
    }
    /* Reject traversal outright, matching the file layer's own rule. */
    for (i = 0; i + 1U < len; i++) {
        if (src[i] == '.' && src[i + 1U] == '.') {
            c->eax = 0;
            return 1;
        }
    }
    /* NOTE: do NOT filter non-archive paths here — every relative path must
     * be re-homed (savedatapath.txt, config.ini, ...) or the early init
     * chain fails before the archives even load. The PersistentGameData
     * string-into-slot-table side effect is handled later. */
    memcpy(tmp, HOST_GUEST_DRIVE_PATH, sizeof HOST_GUEST_DRIVE_PATH - 1U);
    memcpy(tmp + sizeof HOST_GUEST_DRIVE_PATH - 1U, src, len + 1U);
    /* Allocate from the top string area, NOT the bump heap: the bump region
     * (0x60000000+) is later carved by the ISAAC decoder objects, and a
     * resolve result living there came back as garbage/functptr faults. The
     * string area grows down from 0xF0000000 and nothing else allocates
     * from it during init. */
    out = guest_mem_push_string(tmp, len + sizeof HOST_GUEST_DRIVE_PATH - 1U);
    if (out == 0) {
        c->eax = 0;
        return 1;
    }
    memcpy(host_ptr(out), tmp, len + sizeof HOST_GUEST_DRIVE_PATH);
    if (getenv("ISAAC_TRACE_RESOLVE") != NULL) {
        HOST_ERR("resolve-hook: \"%s\" -> \"%s\"", src, tmp);
    }
    c->eax = out;
    return 1;
}

/* Called from sub_0050b240's mount sequence (generated guest_0153.c) right
 * after every Archive::Mount: some Mount failure path returns with edi
 * holding a stale value, and the next instruction is `call edi` -- the
 * WINMM!timeGetTime dispatch through the IAT. Re-arm it. */
/* Called from the translated allocator right where it reads the out-pointer
 * slot back (guest_0166.c 0x565121). The slot held &manager+0xcc on the PC
 * harness but reads 0 on device, so log its whole neighbourhood. */
void isaac_diag_allocator_slot(CPU *__restrict c)
{
    HOST_ERR("allocator[565121]: ebp=0x%{public}08x [ebp-0x10]=0x%{public}08x "
             "[ebp+4]=0x%{public}08x [ebp+8]=0x%{public}08x "
             "[ebp+0xc]=0x%{public}08x esp=0x%{public}08x",
             c->ebp, ld32(c->ebp - 0x10U), ld32(c->ebp + 4U),
             ld32(c->ebp + 8U), ld32(c->ebp + 0xcU), c->esp);
}

void isaac_fix_edi_after_mount(CPU *__restrict c)
{
    c->edi = 0x306064c0U;   /* timeGetTime IAT token (see mount-patch above) */
}

/* ---- emergency volume-container patch ----------------------------------
 *
 * The engine's archive mounts ("resources/packed/*.a") resolve each path
 * against a volume container (vector at 0x307c7b30, fields at 0x307c7b38/
 * 3c/40). On a real PC the container holds a root volume, "resources/" and
 * "resources-dlc3/"; here the volume-registry lookup (sub_00564bd0) misses
 * for two of the three build-in passes, so the container ends up as
 * {NULL, NULL, "resources-dlc3/"} and all 22 mounts fail with
 * "Failed to open ..." without a single file call reaching the host.
 *
 * Called from the CRT fopen shim right after the failed open of
 * "kage_mount_points.dat" -- at that point the container rebuild passes
 * have finished and nothing rebuilds them again before the mounts. This
 * fabricates the three missing volumes with the exact field layout of a
 * live entry (the "resources-dlc3/" object): vtable 0x30765038, type
 * 0x40000070, group 0xffffffff (what MATCHLOOKUP's group check compares
 * against), and heap-allocated name strings.
 */
void isaac_mount_container_patch(void)
{
    static int patched;
    static const char *const k_names[3] = { "", "resources/", "resources-dlc3/" };
    uint32_t block;
    unsigned i;

    if (patched || isaac_arena_base == 0) {
        return;
    }
    patched = 1;

    block = host_arena_alloc(3U * 4U);
    if (block == 0) {
        HOST_ERR("mount-patch: no arena for the container block");
        return;
    }
    for (i = 0; i < 3; i++) {
        const char *name = k_names[i];
        uint32_t name_len = (uint32_t)strlen(name);
        uint32_t name_addr = host_arena_alloc(name_len + 1U);
        uint32_t obj = host_arena_alloc(20U);
        uint32_t *o;
        if (name_addr == 0 || obj == 0) {
            HOST_ERR("mount-patch: no arena for volume %u", i);
            return;
        }
        memcpy(host_ptr(name_addr), name, name_len + 1U);
        o = (uint32_t *)host_ptr(obj);
        o[0] = 0x30765038U;   /* vtable (the one sub_00564130's new uses) */
        o[1] = 0x40000070U;   /* type/flags, copied from the live dlc3 entry */
        o[2] = 0U;
        o[3] = name_addr;     /* name pointer */
        o[4] = 0xFFFFFFFFU;   /* group: matches MATCHLOOKUP's expected group */
        *(uint32_t *)host_ptr(block + i * 4U) = obj;
    }
    *(uint32_t *)host_ptr(0x307c7b38U) = block;        /* begin */
    *(uint32_t *)host_ptr(0x307c7b3cU) = block + 12U;  /* end */
    *(uint32_t *)host_ptr(0x307c7b40U) = block + 12U;  /* capacity end */
    /* Re-fill WINMM IAT slots: something during the mount pass scribbles
     * 0x3075aa44 (an import hint/name RVA) over the timeGetTime slot, and
     * WinMain then does `call edi` straight through that slot -> call to
     * .rdata -> "untranslated address". Restore the self-identifying
     * tokens; the overrides table serves the actual calls. */
    *(uint32_t *)host_ptr(0x306064bcU) = GUEST_IMAGE_BASE + 0x6064bcU;
    *(uint32_t *)host_ptr(0x306064c0U) = GUEST_IMAGE_BASE + 0x6064c0U;
    /* And re-fill every other slot too: the scribble may extend beyond the
     * two WINMM entries. guest.c keeps the import table pointer from the
     * original registration, so this restores all 413 tokens. */
    guest_reregister_imports();
    HOST_ERR("mount-patch: container rebuilt with 3 volumes (root/resources/dlc3); WINMM IAT re-filled");
}

static void h_VirtualAlloc(CPU *c)
{
    uint32_t addr = host_arg(c, 0);
    uint32_t size = host_arg(c, 1);
    uint32_t alloc_type = host_arg(c, 2);
    uint32_t protect = host_arg(c, 3);
    (void)protect;
    /* Guest-specified base addresses are re-homed to the bump allocator: the
     * flat arena already IS the guest address space, and honoring a fixed
     * base here is how the Lua state's 1 GiB reservation (VirtualAlloc with
     * a preferred base + memset) zeroed live heap -- the volume vector's
     * storage lives right behind that boundary, which left two of its three
     * slots NULL and made every archive mount fail. */
    addr = host_arena_alloc(size);
    /* MEM_COMMIT (0x1000) promises zeroed pages. Scrub only small commits:
     * large ones (the Lua reserve) are written wholesale by the caller, and
     * zeroing a gigabyte here costs seconds for nothing. */
    if (addr && (alloc_type & 0x1000U) && size <= 0x1000000U) {
        memset(host_ptr(addr), 0, size);
    }
    c->eax = addr;
    host_stdcall_ret(c, 4);
}

static void h_VirtualFree(CPU *c)
{
    uint32_t addr = host_arg(c, 0);
    uint32_t size = host_arg(c, 1);
    uint32_t free_type = host_arg(c, 2);
    (void)addr; (void)size; (void)free_type;
    /* Simple bump allocator — no actual free */
    c->eax = 1;
    host_stdcall_ret(c, 3);
}

static void h_VirtualProtect(CPU *c)
{
    c->eax = 1; /* success */
    host_stdcall_ret(c, 4);
}

static void h_GetProcessHeap(CPU *c)
{
    c->eax = 0x00420000U; /* pseudo heap handle */
    host_stdcall_ret(c, 0);
}

static void h_HeapAlloc(CPU *c)
{
    uint32_t heap = host_arg(c, 0);
    uint32_t flags = host_arg(c, 1);
    uint32_t size = host_arg(c, 2);
    (void)heap; (void)flags;
    uint32_t addr = host_arena_alloc(size);
    if (addr && (flags & 0x08)) { /* HEAP_ZERO_MEMORY */
        memset(host_ptr(addr), 0, size);
    }
    c->eax = addr;
    host_stdcall_ret(c, 3);
}

static void h_HeapFree(CPU *c)
{
    /* Bump allocator — no actual free */
    c->eax = 1;
    host_stdcall_ret(c, 3);
}

static void h_HeapCreate(CPU *c)
{
    c->eax = 0x00420000U;
    host_stdcall_ret(c, 3);
}

static void h_HeapDestroy(CPU *c)
{
    c->eax = 1;
    host_stdcall_ret(c, 1);
}

static void h_HeapSize(CPU *c)
{
    /* Can't track sizes with bump allocator — return a reasonable value */
    c->eax = 0;
    host_stdcall_ret(c, 3);
}

static void h_HeapReAlloc(CPU *c)
{
    uint32_t heap = host_arg(c, 0);
    uint32_t flags = host_arg(c, 1);
    uint32_t old_addr = host_arg(c, 2);
    uint32_t new_size = host_arg(c, 3);
    (void)heap; (void)flags;
    uint32_t new_addr = host_arena_alloc(new_size);
    if (new_addr && old_addr) {
        /* Copy old data — we don't know the old size, copy a reasonable amount */
        memcpy(host_ptr(new_addr), host_ptr(old_addr), new_size);
    }
    c->eax = new_addr;
    host_stdcall_ret(c, 4);
}

static void h_HeapValidate(CPU *c)
{
    c->eax = 1;
    host_stdcall_ret(c, 3);
}

static void h_GetLastError(CPU *c)
{
    c->eax = c->last_error;
    host_stdcall_ret(c, 0);
}

static void h_SetLastError(CPU *c)
{
    c->last_error = host_arg(c, 0);
    host_stdcall_ret(c, 1);
}

static void h_CreateFileA(CPU *c)
{
    uint32_t name_ptr = host_arg(c, 0);
    uint32_t access = host_arg(c, 1);
    uint32_t share = host_arg(c, 2);
    uint32_t security = host_arg(c, 3);
    uint32_t creation = host_arg(c, 4);
    uint32_t flags = host_arg(c, 5);
    uint32_t template_h = host_arg(c, 6);
    (void)share; (void)security; (void)flags; (void)template_h;

    char *path = host_str(c, 0);
    if (!path || !*path) {
        c->eax = 0xFFFFFFFFU; /* INVALID_HANDLE_VALUE */
        c->last_error = 2; /* ERROR_FILE_NOT_FOUND */
        host_stdcall_ret(c, 7);
        return;
    }

    int posix_flags = 0;
    if (creation == 2) { /* CREATE_ALWAYS */
        posix_flags = O_CREAT | O_TRUNC | (access & 0x40000000 ? O_RDWR : O_WRONLY);
    } else if (creation == 3) { /* OPEN_EXISTING */
        posix_flags = (access & 0x40000000 ? O_RDWR : (access & 0x80000000 ? O_RDONLY : O_RDWR));
    } else if (creation == 4) { /* OPEN_ALWAYS */
        posix_flags = O_CREAT | (access & 0x40000000 ? O_RDWR : O_WRONLY);
    } else if (creation == 1) { /* CREATE_NEW */
        posix_flags = O_CREAT | O_EXCL | (access & 0x40000000 ? O_RDWR : O_WRONLY);
    } else { /* TRUNCATE_EXISTING = 5 */
        posix_flags = O_TRUNC | (access & 0x40000000 ? O_RDWR : O_WRONLY);
    }

    int fd = open(path, posix_flags, 0644);
    if (fd < 0) {
        c->eax = 0xFFFFFFFFU;
        c->last_error = (uint32_t)errno;
        host_stdcall_ret(c, 7);
        return;
    }

    uint32_t handle = host_handle_alloc(fd, 1);
    c->eax = handle;
    host_stdcall_ret(c, 7);
}

static void h_ReadFile(CPU *c)
{
    uint32_t handle = host_arg(c, 0);
    uint32_t buf_ptr = host_arg(c, 1);
    uint32_t size = host_arg(c, 2);
    uint32_t bytes_read_ptr = host_arg(c, 3);
    uint32_t overlapped = host_arg(c, 4);
    (void)overlapped;

    host_handle *hd = host_handle_get(handle);
    if (!hd || hd->fd < 0) {
        c->eax = 0;
        c->last_error = 6; /* ERROR_INVALID_HANDLE */
        host_stdcall_ret(c, 5);
        return;
    }

    ssize_t n = read(hd->fd, host_ptr(buf_ptr), size);
    if (n < 0) n = 0;
    if (bytes_read_ptr) st32(bytes_read_ptr, (uint32_t)n);
    c->eax = 1;
    host_stdcall_ret(c, 5);
}

static void h_WriteFile(CPU *c)
{
    uint32_t handle = host_arg(c, 0);
    uint32_t buf_ptr = host_arg(c, 1);
    uint32_t size = host_arg(c, 2);
    uint32_t bytes_written_ptr = host_arg(c, 3);
    uint32_t overlapped = host_arg(c, 4);
    (void)overlapped;

    host_handle *hd = host_handle_get(handle);
    if (!hd || hd->fd < 0) {
        c->eax = 0;
        c->last_error = 6;
        host_stdcall_ret(c, 5);
        return;
    }

    ssize_t n = write(hd->fd, host_ptr(buf_ptr), size);
    if (n < 0) n = 0;
    if (bytes_written_ptr) st32(bytes_written_ptr, (uint32_t)n);
    c->eax = 1;
    host_stdcall_ret(c, 5);
}

static void h_CloseHandle(CPU *c)
{
    uint32_t handle = host_arg(c, 0);
    host_handle_free(handle);
    c->eax = 1;
    host_stdcall_ret(c, 1);
}

static void h_GetFileSize(CPU *c)
{
    uint32_t handle = host_arg(c, 0);
    uint32_t high_ptr = host_arg(c, 1);
    host_handle *hd = host_handle_get(handle);
    if (!hd || hd->fd < 0) {
        c->eax = 0xFFFFFFFFU;
        host_stdcall_ret(c, 2);
        return;
    }
    struct stat st;
    if (fstat(hd->fd, &st) == 0) {
        if (high_ptr) st32(high_ptr, (uint32_t)((uint64_t)st.st_size >> 32));
        c->eax = (uint32_t)st.st_size;
    } else {
        c->eax = 0xFFFFFFFFU;
    }
    host_stdcall_ret(c, 2);
}

static void h_SetFilePointer(CPU *c)
{
    uint32_t handle = host_arg(c, 0);
    uint32_t distance = host_arg(c, 1);
    uint32_t high_ptr = host_arg(c, 2);
    uint32_t method = host_arg(c, 3);
    (void)high_ptr;

    host_handle *hd = host_handle_get(handle);
    if (!hd || hd->fd < 0) {
        c->eax = 0xFFFFFFFFU;
        host_stdcall_ret(c, 4);
        return;
    }

    int whence = (method == 0) ? SEEK_SET : (method == 1) ? SEEK_CUR : SEEK_END;
    off_t result = lseek(hd->fd, (off_t)(int32_t)distance, whence);
    c->eax = (uint32_t)result;
    host_stdcall_ret(c, 4);
}

static void h_FlushFileBuffers(CPU *c)
{
    uint32_t handle = host_arg(c, 0);
    host_handle *hd = host_handle_get(handle);
    if (hd && hd->fd >= 0) {
        fsync(hd->fd);
    }
    c->eax = 1;
    host_stdcall_ret(c, 1);
}

static void h_GetFileAttributesA(CPU *c)
{
    char *path = host_str(c, 0);
    struct stat st;
    if (stat(path, &st) == 0) {
        uint32_t attrs = 0;
        if (S_ISDIR(st.st_mode)) attrs |= 0x10; /* FILE_ATTRIBUTE_DIRECTORY */
        else attrs |= 0x80; /* FILE_ATTRIBUTE_NORMAL */
        c->eax = attrs;
    } else {
        c->eax = 0xFFFFFFFFU; /* INVALID_FILE_ATTRIBUTES */
        c->last_error = 2;
    }
    host_stdcall_ret(c, 1);
}

static void h_CreateDirectoryA(CPU *c)
{
    char *path = host_str(c, 0);
    if (mkdir(path, 0755) == 0) {
        c->eax = 1;
    } else {
        c->eax = 0;
        c->last_error = (uint32_t)errno;
    }
    host_stdcall_ret(c, 2);
}

static void h_RemoveDirectoryA(CPU *c)
{
    char *path = host_str(c, 0);
    if (rmdir(path) == 0) {
        c->eax = 1;
    } else {
        c->eax = 0;
        c->last_error = (uint32_t)errno;
    }
    host_stdcall_ret(c, 1);
}

static void h_DeleteFileA(CPU *c)
{
    char *path = host_str(c, 0);
    if (unlink(path) == 0) {
        c->eax = 1;
    } else {
        c->eax = 0;
        c->last_error = (uint32_t)errno;
    }
    host_stdcall_ret(c, 1);
}

static void h_MoveFileA(CPU *c)
{
    char *src = host_str(c, 0);
    char *dst = host_str(c, 1);
    if (rename(src, dst) == 0) {
        c->eax = 1;
    } else {
        c->eax = 0;
        c->last_error = (uint32_t)errno;
    }
    host_stdcall_ret(c, 2);
}

static void h_FindFirstFileA(CPU *c)
{
    /* Stub — return INVALID_HANDLE_VALUE */
    c->eax = 0xFFFFFFFFU;
    c->last_error = 2;
    host_stdcall_ret(c, 2);
}

static void h_FindNextFileA(CPU *c)
{
    c->eax = 0;
    host_stdcall_ret(c, 2);
}

static void h_FindClose(CPU *c)
{
    c->eax = 1;
    host_stdcall_ret(c, 1);
}

static void h_MultiByteToWideChar(CPU *c)
{
    uint32_t codepage = host_arg(c, 0);
    uint32_t flags = host_arg(c, 1);
    uint32_t src_ptr = host_arg(c, 2);
    uint32_t src_len = host_arg(c, 3);
    uint32_t dst_ptr = host_arg(c, 4);
    uint32_t dst_len = host_arg(c, 5);
    (void)codepage; (void)flags;

    char *src = (char *)host_ptr(src_ptr);
    if (src_len == 0xFFFFFFFFU) src_len = (uint32_t)strlen(src);

    if (dst_ptr == 0 || dst_len == 0) {
        c->eax = src_len + 1;
        host_stdcall_ret(c, 6);
        return;
    }

    uint32_t i;
    uint16_t *dst = (uint16_t *)host_ptr(dst_ptr);
    for (i = 0; i < src_len && i < dst_len; i++) {
        dst[i] = (uint16_t)(unsigned char)src[i];
    }
    if (i < dst_len) dst[i] = 0;
    c->eax = i;
    host_stdcall_ret(c, 6);
}

static void h_WideCharToMultiByte(CPU *c)
{
    uint32_t codepage = host_arg(c, 0);
    uint32_t flags = host_arg(c, 1);
    uint32_t src_ptr = host_arg(c, 2);
    uint32_t src_len = host_arg(c, 3);
    uint32_t dst_ptr = host_arg(c, 4);
    uint32_t dst_len = host_arg(c, 5);
    (void)codepage; (void)flags;

    uint16_t *src = (uint16_t *)host_ptr(src_ptr);
    if (src_len == 0xFFFFFFFFU) {
        src_len = 0;
        while (src[src_len]) src_len++;
    }

    if (dst_ptr == 0 || dst_len == 0) {
        c->eax = src_len + 1;
        host_stdcall_ret(c, 8);
        return;
    }

    uint32_t i;
    char *dst = (char *)host_ptr(dst_ptr);
    for (i = 0; i < src_len && i < dst_len; i++) {
        dst[i] = (char)(src[i] & 0xFF);
    }
    if (i < dst_len) dst[i] = 0;
    c->eax = i;
    host_stdcall_ret(c, 8);
}

static void h_GetSystemTimeAsFileTime(CPU *c)
{
    uint32_t ptr = host_arg(c, 0);
    if (ptr) {
        time_t now = time(NULL);
        uint64_t ft = (uint64_t)now * 10000000ULL + 116444736000000000ULL;
        st64(ptr, ft);
    }
    host_stdcall_ret(c, 1);
}

static void h_GetTimeZoneInformation(CPU *c)
{
    uint32_t ptr = host_arg(c, 0);
    if (ptr) {
        memset(host_ptr(ptr), 0, 172);
        st32(ptr + 0, 1); /* TIME_ZONE_ID_STANDARD */
    }
    c->eax = 1;
    host_stdcall_ret(c, 1);
}

static void h_Sleep(CPU *c)
{
    uint32_t ms = host_arg(c, 0);
    usleep(ms * 1000);
    host_stdcall_ret(c, 1);
}

static void h_GetStdHandle(CPU *c)
{
    uint32_t n = host_arg(c, 0);
    uint32_t handle;
    if (n == 0xFFFFFFF6) { /* STD_INPUT_HANDLE */
        handle = host_handle_alloc(0, 3);
    } else if (n == 0xFFFFFFF5) { /* STD_OUTPUT_HANDLE */
        handle = host_handle_alloc(1, 3);
    } else if (n == 0xFFFFFFF4) { /* STD_ERROR_HANDLE */
        handle = host_handle_alloc(2, 3);
    } else {
        handle = 0;
    }
    c->eax = handle;
    host_stdcall_ret(c, 1);
}

static void h_SetConsoleCtrlHandler(CPU *c)
{
    c->eax = 1;
    host_stdcall_ret(c, 2);
}

static void h_ExitProcess(CPU *c)
{
    uint32_t code = host_arg(c, 0);
    guest_exit(c, (int32_t)code, "ExitProcess");
}

static void h_GetConsoleMode(CPU *c)
{
    c->eax = 1;
    host_stdcall_ret(c, 2);
}

static void h_SetConsoleMode(CPU *c)
{
    c->eax = 1;
    host_stdcall_ret(c, 2);
}

static void h_GetFileType(CPU *c)
{
    c->eax = 2; /* FILE_TYPE_CHAR */
    host_stdcall_ret(c, 1);
}

static void h_GetStartupInfoA(CPU *c)
{
    uint32_t ptr = host_arg(c, 0);
    if (ptr) {
        memset(host_ptr(ptr), 0, 68);
        st32(ptr + 0, 68); /* cb */
    }
    host_stdcall_ret(c, 1);
}

static void h_GetCommandLineA(CPU *c)
{
    /* Return a guest pointer to a command line string in the arena */
    static uint32_t cmdline_addr = 0;
    if (!cmdline_addr) {
        cmdline_addr = host_arena_alloc(256);
        const char *cmd = "isaac-ng.exe";
        memcpy(host_ptr(cmdline_addr), cmd, strlen(cmd) + 1);
    }
    c->eax = cmdline_addr;
    host_stdcall_ret(c, 0);
}

static void h_GetVersionExA(CPU *c)
{
    uint32_t ptr = host_arg(c, 0);
    if (ptr) {
        st32(ptr + 0, 156); /* dwOSVersionInfoSize */
        st32(ptr + 4, 6);   /* dwMajorVersion (Vista) */
        st32(ptr + 8, 1);   /* dwMinorVersion */
        st32(ptr + 12, 0);  /* dwBuildNumber */
        st32(ptr + 16, 2);  /* dwPlatformId = VER_PLATFORM_WIN32_NT */
        memcpy(host_ptr(ptr + 20), "Service Pack 3", 15);
    }
    c->eax = 1;
    host_stdcall_ret(c, 1);
}

static void h_GetSystemInfo(CPU *c)
{
    uint32_t ptr = host_arg(c, 0);
    if (ptr) {
        memset(host_ptr(ptr), 0, 36);
        st32(ptr + 0, 0);     /* wProcessorArchitecture = x86 */
        st32(ptr + 4, 4096);  /* dwPageSize */
        st32(ptr + 8, 0x10000); /* lpMinimumApplicationAddress */
        st32(ptr + 12, 0x7FFFFFFF); /* lpMaximumApplicationAddress */
        st32(ptr + 16, 1);    /* dwNumberOfProcessors */
    }
    host_stdcall_ret(c, 1);
}

static void h_GlobalMemoryStatusEx(CPU *c)
{
    uint32_t ptr = host_arg(c, 0);
    if (ptr) {
        memset(host_ptr(ptr), 0, 64);
        st32(ptr + 0, 64);    /* dwLength */
        st32(ptr + 4, 0);     /* dwMemoryLoad */
        st64(ptr + 8, (uint64_t)1024 * 1024 * 1024); /* ullTotalPhys = 1GB */
        st64(ptr + 16, (uint64_t)512 * 1024 * 1024); /* ullAvailPhys = 512MB */
        st64(ptr + 24, (uint64_t)2048 * 1024 * 1024); /* ullTotalVirtual = 2GB */
        st64(ptr + 32, (uint64_t)1024 * 1024 * 1024); /* ullAvailVirtual = 1GB */
    }
    c->eax = 1;
    host_stdcall_ret(c, 1);
}

static void h_OutputDebugStringA(CPU *c)
{
    char *msg = host_str(c, 0);
    (void)msg;
    host_stdcall_ret(c, 1);
}

/* ---- MSVC x86 C++ exception minimal support ----------------------------
 *
 * The engine uses C++ exceptions as control flow (PersistentGameData load
 * throws length_error("map/set too long") on first run and catches it
 * upstream on a real PC). Without a distributor the throw lands in
 * garbage-code and kills the guest. This walks the guest FS:[0] chain and
 * hands the exception to each frame's installed handler (MSVC frames use
 * __except_handler4, translated at 0x305f22e5) -- the same dispatch the
 * native code would perform. EXCEPTION_RECORD and CONTEXT are synthesized
 * in the arena from the guest CPU state. */

#define MSVC_CXX_EXCEPTION_CODE 0xE06D7363U

static void isaac_seh_dispatch(CPU *__restrict c, uint32_t code,
                               uint32_t obj, uint32_t throw_info)
{
    uint32_t rec = *(uint32_t *)host_ptr(guest_fs_base(c));
    unsigned depth = 0;
    /* The throw site's return address is where the host ended up calling us
     * from; MSVC's frame handler needs a plausible Eip to walk the frames. */
    uint32_t throw_eip = ld32(c->esp);
    /* EXCEPTION_RECORD: Code, Flags, Record, Address, NumParams, params[2] */
    /* One allocation for the whole run: the engine throws repeatedly, and a
     * fresh 0x31c bytes per throw exhausts the arena heap (observed as
     * "no arena for EXCEPTION_RECORD" after a few hundred throws). */
    static uint32_t s_rec, s_ctx;
    uint32_t er;
    if (s_rec == 0U) {
        s_rec = host_arena_alloc(80U);
        s_ctx = host_arena_alloc(0x2CCU);
    }
    er = s_rec;
    if (er == 0) {
        guest_fault(c, code, "RaiseException: no arena for EXCEPTION_RECORD");
        return;
    }
    *(uint32_t *)host_ptr(er + 0U) = code;
    *(uint32_t *)host_ptr(er + 4U) = 0U;              /* EH_NONCONTINUABLE */
    *(uint32_t *)host_ptr(er + 8U) = 0U;
    *(uint32_t *)host_ptr(er + 12U) = c->fault ? 0U : 0U;
    *(uint32_t *)host_ptr(er + 16U) = 3U;             /* NumberParameters */
    *(uint32_t *)host_ptr(er + 20U) = 0x19930520U;    /* magic (vs EH magic) */
    *(uint32_t *)host_ptr(er + 24U) = obj;            /* exception object */
    *(uint32_t *)host_ptr(er + 28U) = throw_info;     /* ThrowInfo */
    /* CONTEXT: let the handler see guest registers at throw time */
    {
        uint32_t ctx = s_ctx;
        uint32_t *cx;
        if (ctx == 0) {
            guest_fault(c, code, "RaiseException: no arena for CONTEXT");
            return;
        }
        memset(host_ptr(ctx), 0, 0x2CCU);
        cx = (uint32_t *)host_ptr(ctx);
        cx[0x0 / 4] = 0x1003FU; /* ContextFlags: i386|all regs */
        /* i386 CONTEXT offsets: Eax 0x9c, Ecx 0xa0, Edx 0xa4, Ebx 0xa8,
         * Esp 0xac, Ebp 0xb0, Esi 0xb4, Edi 0xb8, Eip 0xb8+4=0xbc */
        cx[0x9c / 4] = c->eax;
        cx[0xa0 / 4] = c->ecx;
        cx[0xa4 / 4] = c->edx;
        cx[0xa8 / 4] = c->ebx;
        cx[0xac / 4] = c->esp;
        cx[0xb0 / 4] = c->ebp;
        cx[0xb4 / 4] = c->esi;
        cx[0xb8 / 4] = c->edi;
        /* Eip must be the throw site: the frame handler walks EBP/EIP to walk
         * out of the throwing function, and a zero Eip makes it conclude the
         * exception is unhandled and terminate the process. */
        cx[0xbc / 4] = throw_eip;
        while (rec != 0U && rec != 0xFFFFFFFFU && depth < 64U) {
void sub_005f22e5(CPU *__restrict c);
            uint32_t handler = *(uint32_t *)host_ptr(rec + 4U);
            HOST_ERR("seh-dispatch: frame=%u handler=0x%08x", depth, handler);
            if (handler == 0) {
                break;
            }
            /* guest cdecl: sub_005f22e5(er, frame, ctx, dispatcher=0) --
             * params are already consecutive on the guest stack above the
             * record we are reading. Build a tiny arg block per frame. */
            {
                uint32_t args = host_arena_alloc(16U);
                if (args == 0) {
                    break;
                }
                *(uint32_t *)host_ptr(args + 0U) = rec;
                *(uint32_t *)host_ptr(args + 4U) = rec;
                *(uint32_t *)host_ptr(args + 8U) = ctx;
                *(uint32_t *)host_ptr(args + 12U) = 0U;
                /* Save the guest stack around the handler call: the handler
                 * runs on its own fabricated frame and returns with esp/ebp
                 * pointing into it, which corrupts the guest if not restored
                 * (the host then walks off the arena and segfaults). */
                uint32_t saved_esp = c->esp;
                uint32_t saved_ebp = c->ebp;
                c->esp = args;
                c->ebp = args;
                sub_005f22e5(c);
                c->esp = saved_esp;
                c->ebp = saved_ebp;
                /* __except_handler4's EXCEPTION_DISPOSITION: 1 means "not
                 * handled, keep searching"; anything else means this frame
                 * handled it (MSVC's handler has by then rewritten the
                 * dispatcher context or longjmp'ed), so stop unwinding and
                 * let the guest run on. */
                HOST_ERR("seh-dispatch: handler[%u] returned 0x%{public}08x",
                         depth, c->eax);
                if (c->eax != 1U) {
                    HOST_ERR("seh-dispatch: frame %u handled the exception", depth);
                    return;
                }
            }
            rec = *(uint32_t *)host_ptr(rec);
            depth++;
        }
    }
    HOST_ERR("seh-dispatch: no handler claimed the C++ exception (code=%08x)",
             code);
    guest_fault(c, code, "RaiseException: unhandled C++ exception");
}

/* ---- VCRUNTIME140 memory functions -------------------------------------
 * Hot-path functions used by every std:: container operation. Without
 * these, memcpy/memset do nothing and all container data stays garbage. */

static void h_vcruntime_memcpy(CPU *c)
{
    uint32_t dst = host_arg(c, 0);
    uint32_t src = host_arg(c, 1);
    uint32_t n = host_arg(c, 2);
    uint32_t esi_before = c->esi;
    uint32_t esp_before = c->esp;
    if (n && dst && src) {
        memcpy(host_ptr(dst), host_ptr(src), n);
    }
    if (getenv("ISAAC_DBG_MEMCLOB")) {
        static int mc_ = 0;
        if ((c->esi != esi_before || c->esp != esp_before + 4U) && mc_ < 8) {
            mc_++;
            fprintf(stderr, "[DBG memclob#%d] dst=%08x src=%08x n=%u esi %08x->%08x esp %08x->%08x (cpu=%p arena=%p)\n",
                    mc_, dst, src, n, esi_before, (unsigned)c->esi,
                    esp_before, (unsigned)c->esp,
                    (void *)c, (void *)isaac_arena_base);
        }
    }
    if (getenv("ISAAC_DBG_MEMCPY2")) {
        static int m2_ = 0;
        if (m2_ < 8) {
            m2_++;
            fprintf(stderr, "[DBG memcpy2#%d] dst=%08x src=%08x n=%u dst8=%02x%02x%02x%02x\n",
                    m2_, dst, src, n,
                    dst ? ld8(dst) : 0, dst ? ld8(dst + 1U) : 0,
                    dst ? ld8(dst + 2U) : 0, dst ? ld8(dst + 3U) : 0);
        }
    }
    c->eax = dst;
    host_cdecl_ret(c);
}

static void h_vcruntime_memmove(CPU *c)
{
    uint32_t dst = host_arg(c, 0);
    uint32_t src = host_arg(c, 1);
    uint32_t n = host_arg(c, 2);
    if (n && dst && src) {
        memmove(host_ptr(dst), host_ptr(src), n);
    }
    c->eax = dst;
    host_cdecl_ret(c);
}

static void h_vcruntime_memset(CPU *c)
{
    uint32_t ptr = host_arg(c, 0);
    uint32_t val = host_arg(c, 1) & 0xFFU;
    uint32_t n = host_arg(c, 2);
    if (n && ptr) {
        memset(host_ptr(ptr), (int)val, n);
    }
    c->eax = ptr;
    host_cdecl_ret(c);
}

static void h_vcruntime_memchr(CPU *c)
{
    uint32_t ptr = host_arg(c, 0);
    uint32_t val = host_arg(c, 1) & 0xFFU;
    uint32_t n = host_arg(c, 2);
    if (n && ptr) {
        void *r = memchr(host_ptr(ptr), (int)val, n);
        c->eax = r ? (uint32_t)((uint8_t *)r - isaac_arena_base) : 0U;
    } else {
        c->eax = 0U;
    }
    host_cdecl_ret(c);
}

static void h_vcruntime_strstr(CPU *c)
{
    uint32_t h = host_arg(c, 0);
    uint32_t nd = host_arg(c, 1);
    const char *hs = (const char *)host_ptr(h);
    const char *ne = (const char *)host_ptr(nd);
    const char *r = strstr(hs, ne);
    c->eax = r ? (uint32_t)((uint8_t *)r - isaac_arena_base) : 0U;
    host_cdecl_ret(c);
}

/* MSVC C++ throw. The translated throw sites call this and then fall into an
 * int3 (the compiler assumes the call never returns), so returning zeros
 * faults at that int3 instead of unwinding: hand the exception to the SEH
 * dispatcher, which walks the guest FS:[0] chain and offers it to each frame
 * handler. */
static void h_cxx_throw(CPU *c)
{
    uint32_t obj = host_arg(c, 0);
    uint32_t ti = host_arg(c, 1);
    HOST_ERR("CxxThrowException: obj=0x%{public}08x ti=0x%{public}08x", obj, ti);
    isaac_seh_dispatch(c, MSVC_CXX_EXCEPTION_CODE, obj, ti);
    /* The dispatcher faults when no frame claims the exception. */
}

static void h_RaiseException(CPU *c)
{
    uint32_t code = host_arg(c, 0);
    if (code == MSVC_CXX_EXCEPTION_CODE) {
        /* RaiseException(code, flags, nArgs, lpArguments): the C++ exception
         * object and ThrowInfo pointers ride in lpArguments[0]/[1]. */
        uint32_t lp_args = host_arg(c, 3);
        uint32_t obj = *(uint32_t *)host_ptr(lp_args);
        uint32_t throw_info = *(uint32_t *)host_ptr(lp_args + 4U);
        HOST_ERR("RaiseException: MSVC C++ exception obj=0x%08x ti=0x%08x",
                 obj, throw_info);
        isaac_seh_dispatch(c, code, obj, throw_info);
        return;
    }
    guest_fault(c, code, "RaiseException");
}

static void h_UnhandledExceptionFilter(CPU *c)
{
    c->eax = 0; /* EXCEPTION_CONTINUE_SEARCH */
    host_stdcall_ret(c, 1);
}

static void h_SetHandleCount(CPU *c)
{
    c->eax = host_arg(c, 0);
    host_stdcall_ret(c, 1);
}

static void h_FlushInstructionCache(CPU *c)
{
    c->eax = 1;
    host_stdcall_ret(c, 3);
}

static void h_GetCurrentActCtx(CPU *c)
{
    c->eax = 0;
    host_stdcall_ret(c, 1);
}

static void h_ActivateActCtx(CPU *c)
{
    c->eax = 1;
    host_stdcall_ret(c, 2);
}

static void h_DeactivateActCtx(CPU *c)
{
    c->eax = 1;
    host_stdcall_ret(c, 2);
}

static void h_ReleaseActCtx(CPU *c)
{
    host_stdcall_ret(c, 1);
}

static void h_CreateActCtxA(CPU *c)
{
    c->eax = 0; /* no activation context */
    c->last_error = 0;
    host_stdcall_ret(c, 1);
}

static void h_lstrlenA(CPU *c)
{
    char *str = host_str(c, 0);
    c->eax = str ? (uint32_t)strlen(str) : 0;
    host_stdcall_ret(c, 1);
}

static void h_lstrcpyA(CPU *c)
{
    uint32_t dst = host_arg(c, 0);
    char *src = host_str(c, 1);
    if (dst && src) {
        strcpy((char *)host_ptr(dst), src);
    }
    c->eax = dst;
    host_stdcall_ret(c, 2);
}

static void h_lstrcpynA(CPU *c)
{
    uint32_t dst = host_arg(c, 0);
    char *src = host_str(c, 1);
    uint32_t max = host_arg(c, 2);
    if (dst && src && max) {
        strncpy((char *)host_ptr(dst), src, max - 1);
        ((char *)host_ptr(dst))[max - 1] = 0;
    }
    c->eax = dst;
    host_stdcall_ret(c, 3);
}

static void h_lstrcmpA(CPU *c)
{
    char *a = host_str(c, 0);
    char *b = host_str(c, 1);
    c->eax = (uint32_t)(int32_t)strcmp(a ? a : "", b ? b : "");
    host_stdcall_ret(c, 2);
}

static void h_lstrcmpiA(CPU *c)
{
    char *a = host_str(c, 0);
    char *b = host_str(c, 1);
    c->eax = (uint32_t)(int32_t)strcasecmp(a ? a : "", b ? b : "");
    host_stdcall_ret(c, 2);
}

static void h_WaitForSingleObject(CPU *c)
{
    c->eax = 0; /* WAIT_OBJECT_0 */
    host_stdcall_ret(c, 2);
}

static void h_WaitForMultipleObjects(CPU *c)
{
    c->eax = 0; /* WAIT_OBJECT_0 */
    host_stdcall_ret(c, 4);
}

static void h_CreateThread(CPU *c)
{
    /* Stub: return a fake thread handle, don't actually create a thread */
    c->eax = host_handle_alloc(-1, 3);
    host_stdcall_ret(c, 6);
}

static void h_ResumeThread(CPU *c)
{
    c->eax = 0; /* previous suspend count */
    host_stdcall_ret(c, 1);
}

static void h_SuspendThread(CPU *c)
{
    c->eax = 0;
    host_stdcall_ret(c, 1);
}

static void h_TerminateThread(CPU *c)
{
    c->eax = 1;
    host_stdcall_ret(c, 2);
}

static void h_GetExitCodeThread(CPU *c)
{
    uint32_t ptr = host_arg(c, 1);
    if (ptr) st32(ptr, 0);
    c->eax = 1;
    host_stdcall_ret(c, 2);
}

static void h_SetThreadPriority(CPU *c)
{
    c->eax = 1;
    host_stdcall_ret(c, 2);
}

static void h_GetThreadPriority(CPU *c)
{
    c->eax = 0; /* normal priority */
    host_stdcall_ret(c, 1);
}

static void h_LeaveCriticalSection(CPU *c)
{
    host_stdcall_ret(c, 1);
}

static void h_EnterCriticalSection(CPU *c)
{
    host_stdcall_ret(c, 1);
}

static void h_InitializeCriticalSection(CPU *c)
{
    host_stdcall_ret(c, 1);
}

static void h_DeleteCriticalSection(CPU *c)
{
    host_stdcall_ret(c, 1);
}

static void h_TryEnterCriticalSection(CPU *c)
{
    c->eax = 1; /* success */
    host_stdcall_ret(c, 1);
}

static void h_CreateMutexA(CPU *c)
{
    c->eax = host_handle_alloc(-1, 3);
    host_stdcall_ret(c, 3);
}

static void h_ReleaseMutex(CPU *c)
{
    c->eax = 1;
    host_stdcall_ret(c, 1);
}

static void h_CreateEventA(CPU *c)
{
    c->eax = host_handle_alloc(-1, 3);
    host_stdcall_ret(c, 4);
}

/* CreateEventW(NULL, TRUE, FALSE, NULL) is the CRT's own startup event, and
 * __scrt_common_main_seh treats a NULL handle as fatal (fastfail(7) -> int3).
 * The event model here is the same lightweight one as h_CreateEventA: a
 * non-zero handle plus SetEvent/ResetEvent/WaitForSingleObject that always
 * report success. */
static void h_CreateEventW(CPU *c)
{
    c->eax = host_handle_alloc(-1, 3);
    host_stdcall_ret(c, 4);
}

static void h_WaitForSingleObjectEx(CPU *c)
{
    c->eax = 0; /* WAIT_OBJECT_0 */
    host_stdcall_ret(c, 3);
}

static void h_InitializeCriticalSectionAndSpinCount(CPU *c)
{
    c->eax = 1; /* TRUE: initialized */
    host_stdcall_ret(c, 2);
}

/* Same contract as h_GetModuleHandleA. The CRT asks for the W spelling while
 * starting up (via GetModuleHandleW(NULL)) and stores the result; a NULL
 * module handle sends the following static initializers into a NULL
 * dereference, which surfaces as the CRT's SEH filter + fastfail. */
static void h_GetModuleHandleW(CPU *c)
{
    c->eax = GUEST_IMAGE_BASE; /* image base doubles as the module handle */
    host_stdcall_ret(c, 1);
}

/* CRT helpers that report success. They are void or errno_t; returning a
 * non-zero value from an hDUMMY stub makes _initterm_e believe a static
 * initializer failed, which aborts startup with no diagnostic. */
static void h_crt_ok1(CPU *c)
{
    c->eax = 0;
    host_stdcall_ret(c, 1);
}

/* WINMM time sources. The engine calls timeGetTime through `call edi` with
 * edi loaded straight from the IAT slot, so the slot must hold the
 * self-identifying token at all times (see isaac_mount_container_patch,
 * which re-fills slots that something has scribbled over). */
static void h_timeGetTime(CPU *c)
{
    static uint64_t first_ms;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    uint64_t ms = (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
    if (first_ms == 0) {
        first_ms = ms;
    }
    c->eax = (uint32_t)(ms - first_ms);
    host_stdcall_ret(c, 0);
}

/* __p__commode returns a writable pointer into the process's commode
 * variable; the CRT dereferences what it gets back. */
static void h_crt__p__commode(CPU *c)
{
    c->eax = HOST_SCRATCH_ADDR;
    host_stdcall_ret(c, 0);
}

/* __p___argc / __p___argv must return a WRITABLE guest pointer, not a value:
 * the CRT reads and writes through it. Returning 0 is a NULL dereference. */
#define HOST_ARGC_ADDR (HOST_SCRATCH_ADDR + 0x0100U)
#define HOST_ARGV_ADDR (HOST_SCRATCH_ADDR + 0x0110U)
#define HOST_ARGV0_ADDR (HOST_SCRATCH_ADDR + 0x0140U)

static void host_crt_argv_init(void)
{
    static int done;
    if (done) {
        return;
    }
    done = 1;
    const char *name = "isaac-ng.exe";
    size_t len = strlen(name) + 1;
    memcpy(isaac_arena_base + HOST_ARGV0_ADDR, name, len);
    st32(HOST_ARGC_ADDR, 1U);
    st32(HOST_ARGV_ADDR, HOST_ARGV0_ADDR);       /* argv[0] */
    st32(HOST_ARGV_ADDR + 4U, 0U);               /* argv[1] = NULL */
}

static void h_crt__p__argc(CPU *c)
{
    host_crt_argv_init();
    c->eax = HOST_ARGC_ADDR;
    host_stdcall_ret(c, 0);
}

static void h_crt__p__argv(CPU *c)
{
    host_crt_argv_init();
    c->eax = HOST_ARGV_ADDR;
    host_stdcall_ret(c, 0);
}

static void h_SetEvent(CPU *c)
{
    c->eax = 1;
    host_stdcall_ret(c, 1);
}

static void h_ResetEvent(CPU *c)
{
    c->eax = 1;
    host_stdcall_ret(c, 1);
}

static void h_InterlockedIncrement(CPU *c)
{
    uint32_t ptr = host_arg(c, 0);
    uint32_t val = ld32(ptr) + 1;
    st32(ptr, val);
    c->eax = val;
    host_stdcall_ret(c, 1);
}

static void h_InterlockedDecrement(CPU *c)
{
    uint32_t ptr = host_arg(c, 0);
    uint32_t val = ld32(ptr) - 1;
    st32(ptr, val);
    c->eax = val;
    host_stdcall_ret(c, 1);
}

static void h_InterlockedExchange(CPU *c)
{
    uint32_t ptr = host_arg(c, 0);
    uint32_t new_val = host_arg(c, 1);
    uint32_t old_val = ld32(ptr);
    st32(ptr, new_val);
    c->eax = old_val;
    host_stdcall_ret(c, 2);
}

static void h_InterlockedExchangeAdd(CPU *c)
{
    uint32_t ptr = host_arg(c, 0);
    uint32_t add_val = host_arg(c, 1);
    uint32_t old_val = ld32(ptr);
    st32(ptr, old_val + add_val);
    c->eax = old_val;
    host_stdcall_ret(c, 2);
}

static void h_InterlockedCompareExchange(CPU *c)
{
    uint32_t ptr = host_arg(c, 0);
    uint32_t exchange = host_arg(c, 1);
    uint32_t comparand = host_arg(c, 2);
    uint32_t old_val = ld32(ptr);
    if (old_val == comparand) {
        st32(ptr, exchange);
    }
    c->eax = old_val;
    host_stdcall_ret(c, 3);
}

/* ---------------------------------------------------------------- CRT heap */

static void h_crt_malloc(CPU *c)
{
    uint32_t size = host_arg(c, 0);
    c->eax = host_arena_alloc(size);
    host_cdecl_ret(c);
}

static void h_crt_free(CPU *c)
{
    /* Bump allocator — no actual free */
    host_cdecl_ret(c);
}

static void h_crt_calloc(CPU *c)
{
    uint32_t count = host_arg(c, 0);
    uint32_t size = host_arg(c, 1);
    uint32_t total = count * size;
    uint32_t addr = host_arena_alloc(total);
    if (addr) memset(host_ptr(addr), 0, total);
    c->eax = addr;
    host_cdecl_ret(c);
}

static void h_crt_realloc(CPU *c)
{
    uint32_t old_addr = host_arg(c, 0);
    uint32_t new_size = host_arg(c, 1);
    uint32_t new_addr = host_arena_alloc(new_size);
    if (new_addr && old_addr) {
        /* Copy old data — we don't know old size, copy new_size */
        memcpy(host_ptr(new_addr), host_ptr(old_addr), new_size);
    }
    c->eax = new_addr;
    host_cdecl_ret(c);
}

static void h_crt__callnewh(CPU *c)
{
    c->eax = 0; /* no new handler */
    host_cdecl_ret(c);
}

static void h_crt__set_new_mode(CPU *c)
{
    c->eax = 0;
    host_cdecl_ret(c);
}

/* ---------------------------------------------------------------- CRT string/mem */

static void h_crt_memset(CPU *c)
{
    uint32_t dst = host_arg(c, 0);
    uint32_t val = host_arg(c, 1) & 0xFF;
    uint32_t size = host_arg(c, 2);
    if (dst) memset(host_ptr(dst), val, size);
    c->eax = dst;
    host_cdecl_ret(c);
}

static void h_crt_memcpy(CPU *c)
{
    uint32_t dst = host_arg(c, 0);
    uint32_t src = host_arg(c, 1);
    uint32_t size = host_arg(c, 2);
    if (dst && src) memmove(host_ptr(dst), host_ptr(src), size);
    c->eax = dst;
    host_cdecl_ret(c);
}

static void h_crt_memmove(CPU *c)
{
    uint32_t dst = host_arg(c, 0);
    uint32_t src = host_arg(c, 1);
    uint32_t size = host_arg(c, 2);
    if (dst && src) memmove(host_ptr(dst), host_ptr(src), size);
    c->eax = dst;
    host_cdecl_ret(c);
}

static void h_crt_memcmp(CPU *c)
{
    uint32_t a = host_arg(c, 0);
    uint32_t b = host_arg(c, 1);
    uint32_t size = host_arg(c, 2);
    c->eax = (uint32_t)(int32_t)memcmp(host_ptr(a), host_ptr(b), size);
    host_cdecl_ret(c);
}

static void h_crt_strlen(CPU *c)
{
    char *str = host_str(c, 0);
    c->eax = str ? (uint32_t)strlen(str) : 0;
    host_cdecl_ret(c);
}

static void h_crt_strcpy(CPU *c)
{
    uint32_t dst = host_arg(c, 0);
    char *src = host_str(c, 1);
    if (dst && src) strcpy((char *)host_ptr(dst), src);
    c->eax = dst;
    host_cdecl_ret(c);
}

static void h_crt_strncpy(CPU *c)
{
    uint32_t dst = host_arg(c, 0);
    char *src = host_str(c, 1);
    uint32_t count = host_arg(c, 2);
    if (dst && src) strncpy((char *)host_ptr(dst), src, count);
    c->eax = dst;
    host_cdecl_ret(c);
}

static void h_crt_strcmp(CPU *c)
{
    char *a = host_str(c, 0);
    char *b = host_str(c, 1);
    c->eax = (uint32_t)(int32_t)strcmp(a ? a : "", b ? b : "");
    host_cdecl_ret(c);
}

static void h_crt_strncmp(CPU *c)
{
    char *a = host_str(c, 0);
    char *b = host_str(c, 1);
    uint32_t count = host_arg(c, 2);
    c->eax = (uint32_t)(int32_t)strncmp(a ? a : "", b ? b : "", count);
    host_cdecl_ret(c);
}

static void h_crt__strdup(CPU *c)
{
    char *src = host_str(c, 0);
    if (!src) {
        c->eax = 0;
        host_cdecl_ret(c);
        return;
    }
    uint32_t len = (uint32_t)strlen(src) + 1;
    uint32_t addr = host_arena_alloc(len);
    if (addr) memcpy(host_ptr(addr), src, len);
    c->eax = addr;
    host_cdecl_ret(c);
}

static void h_crt__strnicmp(CPU *c)
{
    char *a = host_str(c, 0);
    char *b = host_str(c, 1);
    uint32_t count = host_arg(c, 2);
    c->eax = (uint32_t)(int32_t)strncasecmp(a ? a : "", b ? b : "", count);
    host_cdecl_ret(c);
}

static void h_crt__stricmp(CPU *c)
{
    char *a = host_str(c, 0);
    char *b = host_str(c, 1);
    c->eax = (uint32_t)(int32_t)strcasecmp(a ? a : "", b ? b : "");
    host_cdecl_ret(c);
}

static void h_crt_strpbrk(CPU *c)
{
    char *str = host_str(c, 0);
    char *chars = host_str(c, 1);
    if (str && chars) {
        char *result = strpbrk(str, chars);
        c->eax = result ? (uint32_t)(result - (char *)isaac_arena_base) : 0;
    } else {
        c->eax = 0;
    }
    host_cdecl_ret(c);
}

static void h_crt_strchr(CPU *c)
{
    char *str = host_str(c, 0);
    uint32_t ch = host_arg(c, 1) & 0xFF;
    if (str) {
        char *result = strchr(str, (int)ch);
        c->eax = result ? (uint32_t)(result - (char *)isaac_arena_base) : 0;
    } else {
        c->eax = 0;
    }
    host_cdecl_ret(c);
}

static void h_crt_strrchr(CPU *c)
{
    char *str = host_str(c, 0);
    uint32_t ch = host_arg(c, 1) & 0xFF;
    if (str) {
        char *result = strrchr(str, (int)ch);
        c->eax = result ? (uint32_t)(result - (char *)isaac_arena_base) : 0;
    } else {
        c->eax = 0;
    }
    host_cdecl_ret(c);
}

static void h_crt_strstr(CPU *c)
{
    char *str = host_str(c, 0);
    char *sub = host_str(c, 1);
    if (str && sub) {
        char *result = strstr(str, sub);
        c->eax = result ? (uint32_t)(result - (char *)isaac_arena_base) : 0;
    } else {
        c->eax = 0;
    }
    host_cdecl_ret(c);
}

static void h_crt_tolower(CPU *c)
{
    c->eax = tolower((int)host_arg(c, 0));
    host_cdecl_ret(c);
}

static void h_crt_toupper(CPU *c)
{
    c->eax = toupper((int)host_arg(c, 0));
    host_cdecl_ret(c);
}

static void h_crt_ispunct(CPU *c)
{
    c->eax = ispunct((int)host_arg(c, 0));
    host_cdecl_ret(c);
}

static void h_crt_isspace(CPU *c)
{
    c->eax = isspace((int)host_arg(c, 0));
    host_cdecl_ret(c);
}

static void h_crt_isdigit(CPU *c)
{
    c->eax = isdigit((int)host_arg(c, 0));
    host_cdecl_ret(c);
}

static void h_crt_isalpha(CPU *c)
{
    c->eax = isalpha((int)host_arg(c, 0));
    host_cdecl_ret(c);
}

static void h_crt_isalnum(CPU *c)
{
    c->eax = isalnum((int)host_arg(c, 0));
    host_cdecl_ret(c);
}

static void h_crt_isupper(CPU *c)
{
    c->eax = isupper((int)host_arg(c, 0));
    host_cdecl_ret(c);
}

static void h_crt_islower(CPU *c)
{
    c->eax = islower((int)host_arg(c, 0));
    host_cdecl_ret(c);
}

static void h_crt_isxdigit(CPU *c)
{
    c->eax = isxdigit((int)host_arg(c, 0));
    host_cdecl_ret(c);
}

static void h_crt_atoi(CPU *c)
{
    char *str = host_str(c, 0);
    c->eax = (uint32_t)(int32_t)(str ? atoi(str) : 0);
    host_cdecl_ret(c);
}

static void h_crt_atof(CPU *c)
{
    char *str = host_str(c, 0);
    double val = str ? atof(str) : 0.0;
    /* Return double in x87 st[0] for cdecl */
    c->st_top = (c->st_top - 1) & 7;
    c->st[c->st_top] = val;
    host_cdecl_ret(c);
}

static void h_crt_strtol(CPU *c)
{
    char *str = host_str(c, 0);
    uint32_t endptr_ptr = host_arg(c, 1);
    uint32_t base = host_arg(c, 2);
    char *end = NULL;
    long val = str ? strtol(str, &end, (int)base) : 0;
    if (endptr_ptr && end) {
        st32(endptr_ptr, (uint32_t)(end - (char *)isaac_arena_base));
    }
    c->eax = (uint32_t)val;
    host_cdecl_ret(c);
}

static void h_crt_strtoul(CPU *c)
{
    char *str = host_str(c, 0);
    uint32_t endptr_ptr = host_arg(c, 1);
    uint32_t base = host_arg(c, 2);
    char *end = NULL;
    unsigned long val = str ? strtoul(str, &end, (int)base) : 0;
    if (endptr_ptr && end) {
        st32(endptr_ptr, (uint32_t)(end - (char *)isaac_arena_base));
    }
    c->eax = (uint32_t)val;
    host_cdecl_ret(c);
}

static void h_crt_strtoull(CPU *c)
{
    char *str = host_str(c, 0);
    uint32_t endptr_ptr = host_arg(c, 1);
    uint32_t base = host_arg(c, 2);
    char *end = NULL;
    unsigned long long val = str ? strtoull(str, &end, (int)base) : 0;
    c->eax = (uint32_t)val;
    /* Also set edx for 64-bit return */
    c->edx = (uint32_t)(val >> 32);
    if (endptr_ptr && end) {
        st32(endptr_ptr, (uint32_t)(end - (char *)isaac_arena_base));
    }
    host_cdecl_ret(c);
}

static void h_crt_strtoll(CPU *c)
{
    char *str = host_str(c, 0);
    uint32_t endptr_ptr = host_arg(c, 1);
    uint32_t base = host_arg(c, 2);
    char *end = NULL;
    long long val = str ? strtoll(str, &end, (int)base) : 0;
    c->eax = (uint32_t)val;
    c->edx = (uint32_t)((uint64_t)val >> 32);
    if (endptr_ptr && end) {
        st32(endptr_ptr, (uint32_t)(end - (char *)isaac_arena_base));
    }
    host_cdecl_ret(c);
}

static void h_crt_sprintf(CPU *c)
{
    /* Dangerous to fully implement — stub for now */
    c->eax = 0;
    host_cdecl_ret(c);
}

static void h_crt_qsort(CPU *c)
{
    /* qsort with guest pointers — can't easily call guest comparator.
     * Stub: do nothing for now. */
    host_cdecl_ret(c);
}

/* ---------------------------------------------------------------- CRT math */

#define CRT_MATH1(func) \
static void h_crt_##func(CPU *c) { \
    double val = ldd(host_arg(c, 0)); \
    c->st_top = (c->st_top - 1) & 7; \
    c->st[c->st_top] = func(val); \
    host_cdecl_ret(c); \
}

#define CRT_MATH1F(func) \
static void h_crt_##func(CPU *c) { \
    float val = ldf(host_arg(c, 0)); \
    c->st_top = (c->st_top - 1) & 7; \
    c->st[c->st_top] = (double)func(val); \
    host_cdecl_ret(c); \
}

CRT_MATH1(ceil)
CRT_MATH1(floor)
CRT_MATH1(sqrt)
CRT_MATH1(sin)
CRT_MATH1(cos)
CRT_MATH1(tan)
CRT_MATH1(atan)
CRT_MATH1(asin)
CRT_MATH1(acos)
CRT_MATH1(exp)
CRT_MATH1(log)
CRT_MATH1(log10)
CRT_MATH1(fabs)

static void h_crt_ldexp(CPU *c)
{
    double val = ldd(host_arg(c, 0));
    int32_t exp = (int32_t)host_arg(c, 1);
    c->st_top = (c->st_top - 1) & 7;
    c->st[c->st_top] = ldexp(val, exp);
    host_cdecl_ret(c);
}

static void h_crt_copysignf(CPU *c)
{
    float a = ldf(host_arg(c, 0));
    float b = ldf(host_arg(c, 1));
    c->st_top = (c->st_top - 1) & 7;
    c->st[c->st_top] = (double)copysignf(a, b);
    host_cdecl_ret(c);
}

static void h_crt_fmod(CPU *c)
{
    double a = ldd(host_arg(c, 0));
    double b = ldd(host_arg(c, 1));
    c->st_top = (c->st_top - 1) & 7;
    c->st[c->st_top] = fmod(a, b);
    host_cdecl_ret(c);
}

static void h_crt_pow(CPU *c)
{
    double a = ldd(host_arg(c, 0));
    double b = ldd(host_arg(c, 1));
    c->st_top = (c->st_top - 1) & 7;
    c->st[c->st_top] = pow(a, b);
    host_cdecl_ret(c);
}

static void h_crt_atan2(CPU *c)
{
    double a = ldd(host_arg(c, 0));
    double b = ldd(host_arg(c, 1));
    c->st_top = (c->st_top - 1) & 7;
    c->st[c->st_top] = atan2(a, b);
    host_cdecl_ret(c);
}

static void h_crt_modf(CPU *c)
{
    double val = ldd(host_arg(c, 0));
    uint32_t iptr_addr = host_arg(c, 1);
    double intpart;
    double frac = modf(val, &intpart);
    if (iptr_addr) std_(iptr_addr, intpart);
    c->st_top = (c->st_top - 1) & 7;
    c->st[c->st_top] = frac;
    host_cdecl_ret(c);
}

static void h_crt_frexp(CPU *c)
{
    double val = ldd(host_arg(c, 0));
    uint32_t expptr = host_arg(c, 1);
    int exp_val = 0;
    double result = frexp(val, &exp_val);
    if (expptr) st32(expptr, (uint32_t)exp_val);
    c->st_top = (c->st_top - 1) & 7;
    c->st[c->st_top] = result;
    host_cdecl_ret(c);
}

static void h_crt__fdclass(CPU *c)
{
    /* Stub for MSVC _fdclass — return 0 */
    c->eax = 0;
    host_cdecl_ret(c);
}

static void h_crt__controlfp_s(CPU *c)
{
    uint32_t cur = host_arg(c, 0);
    uint32_t new_val = host_arg(c, 1);
    uint32_t mask = host_arg(c, 2);
    (void)new_val; (void)mask;
    if (cur) st32(cur, 0x0008001FU); /* default FP control */
    c->eax = 0;
    host_cdecl_ret(c);
}

/* ---------------------------------------------------------------- CRT runtime */

static void h_crt__initterm(CPU *c)
{
    uint32_t start = host_arg(c, 0);
    uint32_t end = host_arg(c, 1);
    /* Walk the function pointer table and call each initializer */
    for (uint32_t p = start; p < end; p += 4) {
        uint32_t fn_addr = ld32(p);
        if (fn_addr) {
            guest_call(c, fn_addr);
        }
    }
    host_cdecl_ret(c);
}

static void h_crt__initterm_e(CPU *c)
{
    uint32_t start = host_arg(c, 0);
    uint32_t end = host_arg(c, 1);
    /* Walk the function pointer table and call each initializer.
     * _initterm_e initializers return non-zero on failure. */
    c->eax = 0; /* success */
    for (uint32_t p = start; p < end; p += 4) {
        uint32_t fn_addr = ld32(p);
        if (fn_addr) {
            guest_call(c, fn_addr);
            if (c->eax != 0) {
                c->eax = 1; /* failure */
                break;
            }
        }
    }
    host_cdecl_ret(c);
}

static void h_crt__get_initial_narrow_environment(CPU *c)
{
    /* Return a pointer to an empty environment block */
    static uint32_t env_addr = 0;
    if (!env_addr) {
        env_addr = host_arena_alloc(4);
        st32(env_addr, 0); /* null-terminated */
    }
    c->eax = env_addr;
    host_cdecl_ret(c);
}

static void h_crt__register_thread_local_exe_atexit_callback(CPU *c)
{
    /* Stub — no thread-local atexit */
    c->eax = 0;
    host_cdecl_ret(c);
}

static void h_crt__initialize_onexit_table(CPU *c)
{
    c->eax = 0;
    host_cdecl_ret(c);
}

static void h_crt__register_onexit_function(CPU *c)
{
    c->eax = 0;
    host_cdecl_ret(c);
}

static void h_crt__execute_onexit_table(CPU *c)
{
    c->eax = 0;
    host_cdecl_ret(c);
}

static void h_crt__crt_atexit(CPU *c)
{
    c->eax = 0;
    host_cdecl_ret(c);
}

static void h_crt_getenv(CPU *c)
{
    char *name = host_str(c, 0);
    char *val = name ? getenv(name) : NULL;
    if (val) {
        uint32_t len = (uint32_t)strlen(val) + 1;
        uint32_t addr = host_arena_alloc(len);
        memcpy(host_ptr(addr), val, len);
        c->eax = addr;
    } else {
        c->eax = 0;
    }
    host_cdecl_ret(c);
}

static void h_crt__configthreadlocale(CPU *c)
{
    c->eax = 0;
    host_cdecl_ret(c);
}

static void h_crt_remove(CPU *c)
{
    char *path = host_str(c, 0);
    c->eax = path ? (unlink(path) == 0 ? 0 : -1) : -1;
    host_cdecl_ret(c);
}

static void h_crt__access(CPU *c)
{
    char *path = host_str(c, 0);
    uint32_t mode = host_arg(c, 1);
    (void)mode;
    c->eax = path ? (access(path, F_OK) == 0 ? 0 : -1) : -1;
    host_cdecl_ret(c);
}

static void h_crt_mbstowcs_s(CPU *c)
{
    c->eax = 0;
    host_cdecl_ret(c);
}

static void h_crt_wcstombs_s(CPU *c)
{
    c->eax = 0;
    host_cdecl_ret(c);
}

/* ---------------------------------------------------------------- CRT stdio */

static void h_crt_fread(CPU *c)
{
    (void)host_arg(c, 0); (void)host_arg(c, 1); (void)host_arg(c, 2);
    c->eax = 0;
    host_cdecl_ret(c);
}

static void h_crt_fwrite(CPU *c)
{
    (void)host_arg(c, 0); (void)host_arg(c, 1); (void)host_arg(c, 2);
    c->eax = 0;
    host_cdecl_ret(c);
}

static void h_crt_fseek(CPU *c)
{
    c->eax = 0;
    host_cdecl_ret(c);
}

static void h_crt_ftell(CPU *c)
{
    c->eax = 0;
    host_cdecl_ret(c);
}

static void h_crt__fileno(CPU *c)
{
    c->eax = (uint32_t)-1;
    host_cdecl_ret(c);
}

static void h_crt_fopen(CPU *c)
{
    c->eax = 0;
    host_cdecl_ret(c);
}

static void h_crt_fclose(CPU *c)
{
    c->eax = 0;
    host_cdecl_ret(c);
}

static void h_crt_fflush(CPU *c)
{
    c->eax = 0;
    host_cdecl_ret(c);
}

static void h_crt__get_osfhandle(CPU *c)
{
    c->eax = 0;
    host_cdecl_ret(c);
}

static void h_crt_setvbuf(CPU *c)
{
    c->eax = 0;
    host_cdecl_ret(c);
}

/* ---------------------------------------------------------------- CRT time */

static void h_crt_strftime(CPU *c)
{
    c->eax = 0;
    host_cdecl_ret(c);
}

static void h_crt__gmtime64(CPU *c)
{
    c->eax = 0;
    host_cdecl_ret(c);
}

static void h_crt__time64(CPU *c)
{
    c->eax = (uint32_t)time(NULL);
    host_cdecl_ret(c);
}

static void h_crt__mkgmtime64(CPU *c)
{
    c->eax = 0;
    host_cdecl_ret(c);
}

static void h_crt__localtime64(CPU *c)
{
    c->eax = 0;
    host_cdecl_ret(c);
}

/* ---------------------------------------------------------------- CRT misc */

static void h_crt__setjmp3(CPU *c)
{
    /* VCRUNTIME _setjmp3 (x86): record ebx/esi/edi/ebp, the caller's esp/eip,
     * and (when level != 0) the SEH chain with the 'VC22' cookie, so longjmp
     * can unwind back into this frame. cdecl: caller pops the 8 arg bytes. */
    uint32_t buf = host_arg(c, 0);
    uint32_t level = host_arg(c, 1);
    uint32_t entry_esp = c->esp;
    uint32_t ret_eip = ld32(entry_esp);
    uint32_t caller_esp = entry_esp + 4U + 8U;

    if (buf != 0U) {
        st32(buf + 0x00U, c->ebx);
        st32(buf + 0x04U, c->esi);
        st32(buf + 0x08U, c->edi);
        st32(buf + 0x0CU, c->ebp);
        st32(buf + 0x10U, caller_esp);
        st32(buf + 0x14U, ret_eip);
        if (level != 0U) {
            st32(buf + 0x18U, ld32((uint32_t)(guest_fs_base(c))));
            st32(buf + 0x24U, 0x56433232U);   /* 'VC22' */
        }
    }
    if (getenv("ISAAC_DBG_SJ")) {
        static int sj_;
        if (sj_ < 12) {
            sj_++;
            fprintf(stderr, "[DBG setjmp#%d] buf=%08x level=%u ret=%08x ebp=%08x -> eax=0\n",
                    sj_, buf, level, ret_eip, (unsigned)c->ebp);
        }
    }
    c->eax = 0U;
    c->esp = entry_esp + 4U + 8U;
}

/* VCRUNTIME longjmp: route through the translated-program setjmp registry
 * (guest_setjmp_prepare recorded the guest buf of every live site). When no
 * site matches, guest_longjmp faults with a name — that is a real
 * "longjmp without setjmp" and must not be papered over. */
static void h_vcruntime_longjmp(CPU *c)
{
    uint32_t buf = host_arg(c, 0);
    uint32_t val = host_arg(c, 1);

    val = val != 0U ? val : 1U;
    if (getenv("ISAAC_DBG_SJ")) {
        static int lj_;
        if (lj_ < 12) {
            lj_++;
            fprintf(stderr, "[DBG longjmp#%d] buf=%08x val=%d\n", lj_, buf, val);
        }
    }
    guest_longjmp(c, buf, (int32_t)val);
}

/* The KAGE file-reader class vtable (0x30764894) has 15 methods; the image
 * loader calls +0x6c on it after a successful decode. On Windows that slot
 * holds a real method of a fuller class; here it reads the GLFW string that
 * follows the vtable in .rdata. guest.c's guest_register_imports points the
 * slot at this token instead: succeed and leak (harmless for a one-shot
 * image load), keeping the decoded texture. */
static void h_pack_vtable_stub(CPU *c)
{
    c->eax = 1U;
    host_cdecl_ret(c);
}

static void h_crt__strnlen(CPU *c)
{
    char *str = host_str(c, 0);
    uint32_t max = host_arg(c, 1);
    uint32_t len = 0;
    if (str) {
        while (len < max && str[len]) len++;
    }
    c->eax = len;
    host_cdecl_ret(c);
}

static void h_crt__errno(CPU *c)
{
    /* Return pointer to errno value in arena */
    static uint32_t errno_addr = 0;
    if (!errno_addr) {
        errno_addr = host_arena_alloc(4);
        st32(errno_addr, 0);
    }
    c->eax = errno_addr;
    host_cdecl_ret(c);
}

static void h_crt_abort(CPU *c)
{
    guest_exit(c, 3, "abort");
}

static void h_crt__initialize_narrow_environment(CPU *c)
{
    c->eax = 0;
    host_cdecl_ret(c);
}

/* ---------------------------------------------------------------- OpenGL */

static void h_wglCreateContext(CPU *c)
{
    c->eax = 1; /* fake GL context handle */
    host_stdcall_ret(c, 1);
}

static void h_wglDeleteContext(CPU *c)
{
    c->eax = 1;
    host_stdcall_ret(c, 1);
}

static void h_wglMakeCurrent(CPU *c)
{
    c->eax = 1;
    host_stdcall_ret(c, 2);
}

static void h_wglGetProcAddress(CPU *c)
{
    char *name = host_str(c, 0);
    (void)name;
    /* Return a dynamic token for GL extension functions */
    static uint32_t gl_token = 0x7d100000U;
    c->eax = gl_token++;
    host_stdcall_ret(c, 1);
}

static void h_wglShareLists(CPU *c)
{
    c->eax = 1;
    host_stdcall_ret(c, 2);
}

/* ---------------------------------------------------------------- USER32 */

static void h_user32_stub(CPU *c)
{
    c->eax = 0;
    host_stdcall_ret(c, 0);
}

/* ---- input state -------------------------------------------------------
 * The engine reads the keyboard through GetAsyncKeyState/GetKeyState and the
 * pointer through GetCursorPos. The ArkTS layer feeds this table from touch
 * events (and virtual buttons) via native.key()/native.touch(), which the
 * NAPI bridge in main_ohos.cpp forwards to isaac_input_*. */
static uint8_t s_key_down[256];
static uint8_t s_key_prev[256];
static int32_t s_cursor_x;
static int32_t s_cursor_y;

void isaac_input_key(uint32_t vk, int down)
{
    if (vk < 256U) {
        s_key_prev[vk] = s_key_down[vk];
        s_key_down[vk] = (uint8_t)(down != 0);
    }
}

void isaac_input_pointer(int32_t x, int32_t y)
{
    s_cursor_x = x;
    s_cursor_y = y;
}

static void h_GetCursorPos(CPU *c)
{
    uint32_t ptr = host_arg(c, 0);
    if (ptr) {
        st32(ptr, (uint32_t)s_cursor_x);
        st32(ptr + 4, (uint32_t)s_cursor_y);
    }
    c->eax = 1;
    host_stdcall_ret(c, 1);
}

static void h_SetCursorPos(CPU *c)
{
    c->eax = 1;
    host_stdcall_ret(c, 2);
}

static void h_ShowCursor(CPU *c)
{
    c->eax = 0; /* cursor display count */
    host_stdcall_ret(c, 1);
}

static void h_GetMessageA(CPU *c)
{
    /* No messages — return 0 (WM_QUIT) to exit message loop */
    c->eax = 0;
    host_stdcall_ret(c, 4);
}

static void h_PeekMessageA(CPU *c)
{
    /* No messages available */
    c->eax = 0;
    host_stdcall_ret(c, 5);
}

static void h_DispatchMessageA(CPU *c)
{
    c->eax = 0;
    host_stdcall_ret(c, 1);
}

static void h_TranslateMessage(CPU *c)
{
    c->eax = 0;
    host_stdcall_ret(c, 1);
}

static void h_PostQuitMessage(CPU *c)
{
    host_stdcall_ret(c, 1);
}

static void h_MessageBoxA(CPU *c)
{
    c->eax = 1; /* IDOK */
    host_stdcall_ret(c, 4);
}

static void h_GetForegroundWindow(CPU *c)
{
    c->eax = 1; /* fake window handle */
    host_stdcall_ret(c, 0);
}

static void h_SetForegroundWindow(CPU *c)
{
    c->eax = 1;
    host_stdcall_ret(c, 1);
}

static void h_GetDesktopWindow(CPU *c)
{
    c->eax = 1;
    host_stdcall_ret(c, 0);
}

static void h_GetWindowRect(CPU *c)
{
    uint32_t hwnd = host_arg(c, 0);
    uint32_t ptr = host_arg(c, 1);
    (void)hwnd;
    if (ptr) {
        st32(ptr, 0);       /* left */
        st32(ptr + 4, 0);   /* top */
        st32(ptr + 8, 1280);  /* right */
        st32(ptr + 12, 720);  /* bottom */
    }
    c->eax = 1;
    host_stdcall_ret(c, 2);
}

static void h_GetClientRect(CPU *c)
{
    uint32_t hwnd = host_arg(c, 0);
    uint32_t ptr = host_arg(c, 1);
    (void)hwnd;
    if (ptr) {
        st32(ptr, 0);       /* left */
        st32(ptr + 4, 0);   /* top */
        st32(ptr + 8, 1280);  /* right */
        st32(ptr + 12, 720);  /* bottom */
    }
    c->eax = 1;
    host_stdcall_ret(c, 2);
}

static void h_AdjustWindowRect(CPU *c)
{
    /* Return client rect as-is */
    c->eax = 1;
    host_stdcall_ret(c, 3);
}

static void h_CreateWindowExA(CPU *c)
{
    c->eax = 1; /* fake window handle */
    host_stdcall_ret(c, 12);
}

static void h_DestroyWindow(CPU *c)
{
    c->eax = 1;
    host_stdcall_ret(c, 1);
}

static void h_ShowWindow(CPU *c)
{
    c->eax = 1;
    host_stdcall_ret(c, 2);
}

static void h_UpdateWindow(CPU *c)
{
    c->eax = 1;
    host_stdcall_ret(c, 1);
}

static void h_SetWindowLongA(CPU *c)
{
    c->eax = 0;
    host_stdcall_ret(c, 4);
}

static void h_GetWindowLongA(CPU *c)
{
    c->eax = 0;
    host_stdcall_ret(c, 2);
}

static void h_DefWindowProcA(CPU *c)
{
    c->eax = 0;
    host_stdcall_ret(c, 4);
}

static void h_RegisterClassExA(CPU *c)
{
    c->eax = 0x0001; /* atom */
    host_stdcall_ret(c, 1);
}

static void h_UnregisterClassA(CPU *c)
{
    c->eax = 1;
    host_stdcall_ret(c, 2);
}

static void h_SetWindowPos(CPU *c)
{
    c->eax = 1;
    host_stdcall_ret(c, 7);
}

static void h_ScreenToClient(CPU *c)
{
    c->eax = 1;
    host_stdcall_ret(c, 2);
}

static void h_ClientToScreen(CPU *c)
{
    c->eax = 1;
    host_stdcall_ret(c, 2);
}

static void h_GetSystemMetrics(CPU *c)
{
    uint32_t index = host_arg(c, 0);
    uint32_t val = 0;
    switch (index) {
    case 0: val = 1280; break; /* SM_CXSCREEN */
    case 1: val = 720; break;  /* SM_CYSCREEN */
    case 78: val = 1280; break; /* SM_CXVIRTUALSCREEN */
    case 79: val = 720; break;  /* SM_CYVIRTUALSCREEN */
    default: val = 0; break;
    }
    c->eax = val;
    host_stdcall_ret(c, 1);
}

static void h_SetCapture(CPU *c)
{
    c->eax = 1;
    host_stdcall_ret(c, 1);
}

static void h_ReleaseCapture(CPU *c)
{
    c->eax = 1;
    host_stdcall_ret(c, 0);
}

static void h_SetCursor(CPU *c)
{
    c->eax = 0;
    host_stdcall_ret(c, 1);
}

static void h_SetFocus(CPU *c)
{
    c->eax = 1;
    host_stdcall_ret(c, 1);
}

static void h_ClipCursor(CPU *c)
{
    c->eax = 1;
    host_stdcall_ret(c, 1);
}

static void h_BringWindowToTop(CPU *c)
{
    c->eax = 1;
    host_stdcall_ret(c, 1);
}

static void h_GetAsyncKeyState(CPU *c)
{
    uint32_t vk = host_arg(c, 0);
    uint32_t result = 0U;
    if (vk < 256U) {
        if (s_key_down[vk]) {
            result |= 0x8000U;          /* currently down */
        }
        if (s_key_down[vk] && !s_key_prev[vk]) {
            result |= 0x0001U;          /* pressed since the last poll */
        }
        s_key_prev[vk] = s_key_down[vk];
    }
    c->eax = result;
    host_stdcall_ret(c, 1);
}

static void h_GetKeyState(CPU *c)
{
    uint32_t vk = host_arg(c, 0);
    c->eax = (vk < 256U && s_key_down[vk]) ? 0x8000U : 0U;
    host_stdcall_ret(c, 1);
}

static void h_GetKeyboardState(CPU *c)
{
    uint32_t ptr = host_arg(c, 0);
    if (ptr) memset(host_ptr(ptr), 0, 256);
    c->eax = 1;
    host_stdcall_ret(c, 1);
}

static void h_MapVirtualKeyA(CPU *c)
{
    c->eax = 0;
    host_stdcall_ret(c, 3);
}

static void h_ToAsciiEx(CPU *c)
{
    c->eax = 0;
    host_stdcall_ret(c, 5);
}

static void h_GetKeyNameTextA(CPU *c)
{
    c->eax = 0;
    host_stdcall_ret(c, 3);
}

static void h_EnumDisplaySettingsA(CPU *c)
{
    uint32_t dev = host_arg(c, 0);
    uint32_t mode = host_arg(c, 1);
    uint32_t ptr = host_arg(c, 2);
    (void)dev; (void)mode;
    if (ptr) {
        memset(host_ptr(ptr), 0, 172);
        st32(ptr + 0, 172); /* dmSize */
        st32(ptr + 40, 1280); /* dmPelsWidth */
        st32(ptr + 44, 720);  /* dmPelsHeight */
        st32(ptr + 48, 32);   /* dmBitsPerPel */
    }
    c->eax = 1;
    host_stdcall_ret(c, 3);
}

static void h_ChangeDisplaySettingsA(CPU *c)
{
    c->eax = 0; /* DISP_CHANGE_SUCCESSFUL */
    host_stdcall_ret(c, 2);
}

static void h_SetWindowTextA(CPU *c)
{
    c->eax = 1;
    host_stdcall_ret(c, 2);
}

static void h_GetWindowTextA(CPU *c)
{
    c->eax = 0;
    host_stdcall_ret(c, 3);
}

static void h_GetActiveWindow(CPU *c)
{
    c->eax = 1;
    host_stdcall_ret(c, 0);
}

static void h_SetActiveWindow(CPU *c)
{
    c->eax = 1;
    host_stdcall_ret(c, 1);
}

static void h_IsWindowVisible(CPU *c)
{
    c->eax = 1;
    host_stdcall_ret(c, 1);
}

static void h_IsWindow(CPU *c)
{
    c->eax = 1;
    host_stdcall_ret(c, 1);
}

static void h_InvalidateRect(CPU *c)
{
    c->eax = 1;
    host_stdcall_ret(c, 3);
}

static void h_ValidateRect(CPU *c)
{
    c->eax = 1;
    host_stdcall_ret(c, 2);
}

static void h_RedrawWindow(CPU *c)
{
    c->eax = 1;
    host_stdcall_ret(c, 4);
}

static void h_BeginPaint(CPU *c)
{
    uint32_t hwnd = host_arg(c, 0);
    uint32_t ps_ptr = host_arg(c, 1);
    (void)hwnd;
    if (ps_ptr) memset(host_ptr(ps_ptr), 0, 64);
    c->eax = 1; /* fake HDC */
    host_stdcall_ret(c, 2);
}

static void h_EndPaint(CPU *c)
{
    c->eax = 1;
    host_stdcall_ret(c, 2);
}

static void h_GetDC(CPU *c)
{
    c->eax = 1; /* fake HDC */
    host_stdcall_ret(c, 1);
}

static void h_ReleaseDC(CPU *c)
{
    c->eax = 1;
    host_stdcall_ret(c, 2);
}

static void h_PixelFormat(CPU *c)
{
    c->eax = 1;
    host_stdcall_ret(c, 0);
}

/* ---------------------------------------------------------------- GDI32 */

static void h_gdi32_stub(CPU *c)
{
    c->eax = 0;
    host_stdcall_ret(c, 0);
}

static void h_SetPixelFormat(CPU *c)
{
    c->eax = 1;
    host_stdcall_ret(c, 3);
}

static void h_GetDeviceCaps(CPU *c)
{
    uint32_t hdc = host_arg(c, 0);
    uint32_t index = host_arg(c, 1);
    (void)hdc;
    uint32_t val = 0;
    switch (index) {
    case 2: val = 1280; break; /* HORZRES */
    case 3: val = 720; break;  /* VERTRES */
    case 12: val = 32; break;  /* BITSPIXEL */
    default: val = 0; break;
    }
    c->eax = val;
    host_stdcall_ret(c, 2);
}

static void h_DeleteDC(CPU *c)
{
    c->eax = 1;
    host_stdcall_ret(c, 1);
}

static void h_DescribePixelFormat(CPU *c)
{
    c->eax = 1;
    host_stdcall_ret(c, 4);
}

static void h_SwapBuffers(CPU *c)
{
    /* This will be hooked to the GLES backend's swap */
    c->eax = 1;
    host_stdcall_ret(c, 1);
}

static void h_CreateDCA(CPU *c)
{
    c->eax = 1; /* fake HDC */
    host_stdcall_ret(c, 4);
}

static void h_ChoosePixelFormat(CPU *c)
{
    c->eax = 1; /* pixel format index */
    host_stdcall_ret(c, 2);
}

/* ---------------------------------------------------------------- OpenAL */

static void h_openal_stub(CPU *c)
{
    c->eax = 0;
    host_cdecl_ret(c);
}

/* ---------------------------------------------------------------- Steam */

static void h_steam_stub(CPU *c)
{
    c->eax = 1; /* SteamAPI_Init returns true */
    host_cdecl_ret(c);
}

/* Steam is absent on this target, and SteamAPI_Init must say so.
 *
 * The blanket stub above answered 1 ("initialised") to every entry point.
 * The guest believed it, took the Steam-present path, and called a function
 * pointer out of the context returned by SteamInternal_ContextInit -- which
 * the same stub had filled with a non-NULL-looking 1, so the pointer it
 * dereferenced was NULL and the call landed on 0x00000000.
 *
 * Reporting absence instead lets the guest run its own no-Steam path, which
 * already exists: the GOG build of this game has no Steam at all.
 * Observed call order: CoInitialize, SteamAPI_Init, SteamInternal_ContextInit. */
static void h_steam_init_failed(CPU *c)
{
    c->eax = 0; /* FALSE: Steam unavailable */
    host_cdecl_ret(c);
}

static void h_steam_null(CPU *c)
{
    c->eax = 0; /* NULL handle / context / interface */
    host_cdecl_ret(c);
}

/* ---------------------------------------------------------------- Other stubs */

static void h_advapi32_stub(CPU *c)
{
    c->eax = 1; /* success for privilege functions */
    host_stdcall_ret(c, 3);
}

static void h_shell32_stub(CPU *c)
{
    c->eax = 0;
    host_stdcall_ret(c, 2);
}

static void h_ole32_stub(CPU *c)
{
    c->eax = 0; /* S_OK */
    host_stdcall_ret(c, 0);
}

static void h_dbghelp_stub(CPU *c)
{
    c->eax = 1;
    host_stdcall_ret(c, 4);
}

static void h_winmm_stub(CPU *c)
{
    c->eax = 0;
    host_stdcall_ret(c, 1);
}

static void h_bcrypt_stub(CPU *c)
{
    c->eax = 0; /* STATUS_SUCCESS */
    host_stdcall_ret(c, 5);
}

/* ------------------------------------------------- MSVC iostream internals */
/* The game parses its data files through std::istream, and the loader's
 * line-splitting loop only terminates when the stream reports eof. Stubbing
 * these to a fixed value made that loop spin forever: the line vector grew
 * until allocations failed silently and a wild copy scribbled over the whole
 * arena, IAT slots included (`call 0`). They are therefore implemented against
 * the real object layout, read off the DLL's own code:
 *
 *   basic_streambuf<char> (MSVC v14):
 *     [+0x1c] -> char**  _IGnext  : pointer to the _Gnext member
 *     [+0x2c] -> int*    _IGcount : pointer to the _Gcount member
 *     vtable[6] = underflow(), vtable[7] = uflow()   (virtual, the game's own)
 *   basic_ios:
 *     [+0x0c] = _Mystate (exactly what the real setstate reads and writes)
 */
#define SB_IGNEXT       0x1cU
#define SB_IGCOUNT      0x2cU
#define SB_VT_UNDERFLOW 0x18U
#define SB_VT_UFLOW     0x1cU

#define IOS_STATE_OFF   0x0cU
#define IOS_EOF_BIT     0x01U
#define IOS_FAIL_BIT    0x02U
#define IOS_BAD_BIT     0x04U
#define IOS_FMT_BIT     0x06U   /* failbit|badbit, what the sentry sets */
#define SB_EOF          0xffffffffU

/* Call a guest function pointer as a __thiscall member (ecx = this). The
 * pushed word only serves as the callee's return slot; esp is restored here so
 * the caller's frame is untouched whatever the callee believed. */
static uint32_t host_msvcp_vcall(CPU *__restrict c, uint32_t fn, uint32_t this_ptr)
{
    uint32_t saved = c->esp;
    uint32_t result;

    if (fn == 0U) {
        return SB_EOF;
    }
    gpush_generated(c, 0x5eb83eU);
    c->ecx = this_ptr;
    guest_call(c, fn);
    result = c->eax;
    c->esp = saved;
    return result;
}

static uint32_t msvcp_sbumpc(CPU *__restrict c, uint32_t this_ptr)
{
    uint32_t ig_next = ld32(this_ptr + SB_IGNEXT);
    uint32_t next = ig_next ? ld32(ig_next) : 0U;

    if (next != 0U) {
        uint32_t ig_count = ld32(this_ptr + SB_IGCOUNT);
        int32_t count = ig_count ? (int32_t)ld32(ig_count) : 0;
        if (count > 0) {
            st32(ig_count, (uint32_t)(count - 1));
            st32(ig_next, next + 1U);
            return (uint32_t)ld8(next);
        }
    }
    return host_msvcp_vcall(c, ld32(ld32(this_ptr) + SB_VT_UFLOW), this_ptr);
}

static uint32_t msvcp_sgetc(CPU *__restrict c, uint32_t this_ptr)
{
    uint32_t ig_next = ld32(this_ptr + SB_IGNEXT);
    uint32_t next = ig_next ? ld32(ig_next) : 0U;

    if (next != 0U) {
        uint32_t ig_count = ld32(this_ptr + SB_IGCOUNT);
        if (ig_count != 0U && (int32_t)ld32(ig_count) > 0) {
            return (uint32_t)ld8(next);
        }
    }
    return host_msvcp_vcall(c, ld32(ld32(this_ptr) + SB_VT_UNDERFLOW), this_ptr);
}

static void h_msvcp_sgetc(CPU *c)
{
    c->eax = msvcp_sgetc(c, c->ecx);
    host_stdcall_ret(c, 0);
}

static void h_msvcp_sbumpc(CPU *c)
{
    c->eax = msvcp_sbumpc(c, c->ecx);
    host_stdcall_ret(c, 0);
}

/* snextc: skip the current character, then peek at the next one. */
static void h_msvcp_snextc(CPU *c)
{
    uint32_t this_ptr = c->ecx;
    uint32_t ch = msvcp_sbumpc(c, this_ptr);

    c->eax = (ch == SB_EOF) ? SB_EOF : msvcp_sgetc(c, this_ptr);
    host_stdcall_ret(c, 0);
}

/* basic_istream::_Ipfx(bool): the stream sentry. It passes only while the
 * stream is good; on a stream that already carries a state flag it marks
 * failbit|badbit and fails, which is how getline/operator>> terminate. */
static void h_msvcp_ipfx(CPU *c)
{
    uint32_t this_ptr = c->ecx;
    uint32_t state = this_ptr ? ld32(this_ptr + IOS_STATE_OFF) : 0U;

    if (state == 0U) {
        c->eax = 1;
    } else {
        st32(this_ptr + IOS_STATE_OFF, state | IOS_FMT_BIT);
        c->eax = 0;
    }
    host_stdcall_ret(c, 4);
}

/* basic_ios::setstate(int, bool): OR the state word in, never rethrow. */
static void h_msvcp_setstate(CPU *c)
{
    uint32_t this_ptr = c->ecx;
    uint32_t state = host_arg(c, 0);

    if (this_ptr) {
        st32(this_ptr + IOS_STATE_OFF, ld32(this_ptr + IOS_STATE_OFF) | state);
    }
    c->eax = 0;
    host_stdcall_ret(c, 8);
}

/* MSVCP140 generic stub: thiscall, zero extra args. Default behavior is to
 * return 0. Key constructions (basic_ios/basic_iostream/basic_streambuf)
 * get dedicated handlers below. */
static void h_msvcp_stub(CPU *c)
{
    c->eax = 0;
    host_stdcall_ret(c, 0);
}

/* MSVC basic_streambuf ctor: thiscall, this=ecx. Reproduces the DLL's own
 * init (MSVCP140 RVA 0xc7b0), which is what makes setg/sgetc work: the class
 * keeps its buffer members behind self-relative pointers, so _IGfirst/_IGnext/
 * _IGcount must point at the object's own +4/+0x14/+0x24 slots. Zeroing the
 * object instead (as a plain memset did) makes the game's setg write to
 * address 0. The vtable is written by the caller afterwards. */
static void h_msvcp_streambuf_ctor(CPU *c)
{
    uint32_t t = c->ecx;

    if (t) {
        st32(t + 0x0cU, t + 0x04U);   /* _IGfirst */
        st32(t + 0x10U, t + 0x08U);   /* _IPfirst */
        st32(t + 0x1cU, t + 0x14U);   /* _IGnext  */
        st32(t + 0x20U, t + 0x18U);   /* _IPnext  */
        st32(t + 0x2cU, t + 0x24U);   /* _IGcount */
        st32(t + 0x30U, t + 0x28U);   /* _IPcount */
        st32(t + 0x04U, 0U);          /* _Gfirst = null -> setg fills it   */
        st32(t + 0x08U, 0U);
        st32(t + 0x14U, 0U);          /* _Gnext                        */
        st32(t + 0x18U, 0U);
        st32(t + 0x24U, 0U);          /* _Gcount                       */
        st32(t + 0x28U, 0U);
    }
    c->eax = t;
    host_stdcall_ret(c, 0);
}

/* MSVC basic_ios ctor: thiscall, this=ecx. Mirrors the real one (MSVCP140 RVA
 * 0xa4e0 + the ios_base ctor at 0xb020): zero the object's own fields, nothing
 * more. The previous version memset 0xD8 bytes, which ran past the ios
 * subobject and wiped whatever followed it -- in the line-splitting path that
 * was the caller's frame, stack cookie included (-> __security_check_cookie
 * failure -> fastfail 0xC0000409). */
static void h_msvcp_ios_ctor(CPU *c)
{
    uint32_t t = c->ecx;
    uint32_t off;

    if (t) {
        for (off = 0x08U; off <= 0x30U; off += 4U) {
            st32(t + off, 0U);
        }
        st32(t + 0x38U, 0U);
        st32(t + 0x3cU, 0U);
        st8(t + 0x40U, 0U);
    }
    c->eax = t;
    host_stdcall_ret(c, 0);
}

/* ---------------------------------------------------------------- VCRUNTIME specific */

static void h__RTDynamicCast(CPU *c)
{
    /* C++ RTTI dynamic_cast — return the input pointer (no real RTTI) */
    c->eax = host_arg(c, 0);
    host_cdecl_ret(c);
}

static void h__except_handler4_common(CPU *c)
{
    /* SEH handler — return EXCEPTION_CONTINUE_SEARCH */
    c->eax = 0;
    host_cdecl_ret(c);
}

static void h__current_exception_context(CPU *c)
{
    c->eax = 0;
    host_cdecl_ret(c);
}

static void h_strchr_vcruntime(CPU *c)
{
    char *str = host_str(c, 0);
    uint32_t ch = host_arg(c, 1) & 0xFF;
    if (str) {
        char *result = strchr(str, (int)ch);
        c->eax = result ? (uint32_t)(result - (char *)isaac_arena_base) : 0;
    } else {
        c->eax = 0;
    }
    host_cdecl_ret(c);
}

/* ---------------------------------------------------------------- Lua stub */

static void h_lua_stub(CPU *c)
{
    c->eax = 0;
    host_cdecl_ret(c);
}

/* ---------------------------------------------------------------- dispatch */

/* Overrides for imports the generated table still routes to hDUMMY. Kept
 * here rather than in generated/host_imports_table.h so that regenerating
 * the table does not silently drop them. */
typedef struct {
    const char *name;
    void (*fn)(CPU *);
} host_import_override;

static const host_import_override s_host_overrides[] = {
    { "KERNEL32.dll!CreateEventW", h_CreateEventW },
    { "WINMM.dll!timeGetTime", h_timeGetTime },
    { "WINMM.dll!timeBeginPeriod", h_crt_ok1 },
    { "KERNEL32.dll!WaitForSingleObjectEx", h_WaitForSingleObjectEx },
    { "KERNEL32.dll!InitializeCriticalSectionAndSpinCount",
      h_InitializeCriticalSectionAndSpinCount },
    { "KERNEL32.dll!GetModuleHandleW", h_GetModuleHandleW },
    { "api-ms-win-crt-runtime-l1-1-0.dll!_set_app_type", h_crt_ok1 },
    { "api-ms-win-crt-runtime-l1-1-0.dll!_configure_narrow_argv", h_crt_ok1 },
    { "api-ms-win-crt-runtime-l1-1-0.dll!_initialize_narrow_environment", h_crt_ok1 },
    { "api-ms-win-crt-stdio-l1-1-0.dll!_set_fmode", h_crt_ok1 },
    { "api-ms-win-crt-stdio-l1-1-0.dll!__p__commode", h_crt__p__commode },
    { "api-ms-win-crt-runtime-l1-1-0.dll!__p___argc", h_crt__p__argc },
    { "api-ms-win-crt-runtime-l1-1-0.dll!__p___argv", h_crt__p__argv },
    { "KERNEL32.dll!GetCurrentDirectoryA", h_GetCurrentDirectoryA },
    { "steam_api.dll!SteamAPI_Init", h_steam_init_failed },
    { "steam_api.dll!SteamInternal_ContextInit", h_steam_null },
    { "steam_api.dll!SteamInternal_CreateInterface", h_steam_null },
    { "steam_api.dll!SteamAPI_GetHSteamPipe", h_steam_null },
    { "steam_api.dll!SteamAPI_GetHSteamUser", h_steam_null },
};
#define HOST_OVERRIDE_COUNT \
    (sizeof s_host_overrides / sizeof s_host_overrides[0])

typedef void (*host_handler_fn)(CPU *);

static host_handler_fn host_ext_lookup_import(const char *name)
{
    size_t p;
    for (p = 0; p < HOST_EXT_PROVIDER_COUNT; p++) {
        const host_ext_module *m = s_ext_providers[p]();
        unsigned i;
        if (m == NULL || m->entries == NULL) {
            continue;
        }
        for (i = 0; i < m->entry_count; i++) {
            if (strcmp(m->entries[i].name, name) == 0) {
                return m->entries[i].fn;
            }
        }
    }
    return NULL;
}

static host_handler_fn host_ext_lookup_dynamic(const char *name)
{
    size_t p;
    for (p = 0; p < HOST_EXT_PROVIDER_COUNT; p++) {
        const host_ext_module *m = s_ext_providers[p]();
        unsigned i;
        if (m == NULL || m->dynamic == NULL) {
            continue;
        }
        for (i = 0; i < m->dynamic_count; i++) {
            if (strcmp(m->dynamic[i].name, name) == 0) {
                return m->dynamic[i].fn;
            }
        }
    }
    return NULL;
}

int guest_host_import(CPU *__restrict c, const char *name)
{
    host_handler_fn ext = host_ext_lookup_import(name);
    uint32_t lo = 0;
    uint32_t hi = HOST_IMPORT_COUNT;
    static int s_trace_import_esp = -1;

    if (s_trace_import_esp < 0) {
        s_trace_import_esp = (getenv("ISAAC_TRACE_IMPORT_ESP") != NULL) ? 1 : 0;
    }

    if (ext != NULL) {
        g_host_import_calls++;
        ext(c);
        return 1;
    }

    /* Explicit overrides win over the generated table unconditionally, so a
     * shim can also replace a blanket per-DLL stub (steam_api's, for one),
     * not only an hDUMMY entry. */
    {
        size_t i;
        for (i = 0; i < HOST_OVERRIDE_COUNT; i++) {
            if (strcmp(s_host_overrides[i].name, name) == 0) {
                g_host_import_calls++;
                s_host_overrides[i].fn(c);
                return 1;
            }
        }
    }

    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2U;
        int order = strcmp(s_host_imports[mid].name, name);
        if (!order) {
            g_host_import_calls++;
            if (s_host_imports[mid].fn == hDUMMY) {
                /* A hDUMMY import returns 0. Callers that treat 0 as a
                 * failure (the CRT does) turn this into a fatal fastfail, so
                 * make it visible instead of leaving it to stderr, which
                 * never reaches hilog. */
                HOST_ERR("host_import: UNIMPLEMENTED %{public}s (returns 0)", name);
            }
            /* The C++ runtime's callee-side cleanup is authoritative: the guest
             * was compiled against the real DLL, so a shim that pops a
             * different number of bytes drifts the guest esp by that much on
             * every call, and the drift eventually makes the guest's epilogue
             * pop garbage instead of the caller's saved esi/edi/ebx. Pin esp to
             * the real `ret N` (see msvcp_ret_table.h). */
            {
                uint32_t esp_before = c->esp;
                unsigned pop = msvcp_ret_lookup(name);
                s_host_imports[mid].fn(c);
                if (pop != 0U) {
                    if (s_trace_import_esp) {
                        HOST_ERR("import-esp: %{public}s shim_left=0x%{public}08x abi=0x%{public}08x",
                                 name, c->esp, esp_before + pop);
                    }
                    c->esp = esp_before + pop;
                }
            }
            return 1;
        }
        if (order < 0) lo = mid + 1U; else hi = mid;
    }

    HOST_ERR("host_import: NOT IN TABLE %{public}s", name);
    return 0;
}

/* Initialize host layer */
void host_ohos_init(void)
{
    memset(g_handles, 0, sizeof g_handles);
    host_qpc_init();
}