/* KAGE graphics platform seam for the HarmonyOS port.
 *
 * This file replaces the two translated-but-never-emitted KAGE entry points
 * (Graphics::Manager::Initialize @0x560c60 and ::InitializeRenderDisplay
 * @0x561830) plus the Manager vtable leaves that the guest would otherwise
 * take through GLFW/USER32/WGL:
 *
 *   vtable+0x04 0x560c60 Initialize              -> kage_initialize_pc()
 *   vtable+0x08 0x560e30 Shutdown                -> backend + render targets down
 *   vtable+0x2c 0x560eb0 Present                 -> guest original + EGL swap
 *   vtable+0x30 0x560f20 GetFramebufferWidth     -> backend width  (+ RECT cache)
 *   vtable+0x34 0x560f90 GetFramebufferHeight    -> backend height (+ RECT cache)
 *   0x561830     InitializeRenderDisplay         -> kage_initialize_pc()
 *   0x005700e0   KAGE's GL entry-point loader    -> resolve into the typed
 *                                                   73-command surface below
 *
 * There is intentionally NO fake GLFW object: the manager's +0xd8 word is
 * written 0 (upstream manual_kage.c), and nothing here ever touches the
 * guest's byte at image base + 0x7fd66b.
 *
 * GL traffic path (why 0x005700e0 is in this file): every guest GL call site
 * is `call dword ptr [<slot>]`, and each slot starts out holding a KAGE loader
 * thunk that (a) asks 0x005700e0 for the entry point, (b) stores it back into
 * the slot and (c) tail-jumps to it.  Returning a token from the dynamic
 * (GetProcAddress) token space owned by this module therefore installs the
 * 73-command backend into the guest's own loader: the first call crosses the
 * resolver, every later call goes straight from the guest slot into the
 * adapter below, which reads the guest stdcall arguments, calls the typed
 * backend and performs the callee's RET n.
 *
 * Host limit: gl_ohos_backend.c needs libGLESv2/libEGL, so the POSIX harness
 * does not link it.  Both it and the EGL present leaf are therefore weak here,
 * and the harness installs s_kage_host_gl - a clearly named simulation table
 * that keeps the guest's GL queries self-consistent (handles are allocated,
 * bindings and renderbuffer dimensions are tracked) so the guest can run its
 * real code path on a machine with no GL context.  A device build links
 * gl_ohos_backend.c, so the same adapters drive the real 73-command backend.
 */
#include "guest.h"
#include "guest_memory.h"
#include "gl_types.h"
#include "host_ext.h"

#include <hilog/log.h>
#include <stdio.h>
#include <string.h>

#define KLOG(...) ((void)OH_LOG_Print(LOG_APP, LOG_INFO, 0, "isaac.kage", __VA_ARGS__))
#define KERR(...) ((void)OH_LOG_Print(LOG_APP, LOG_ERROR, 0, "isaac.kage", __VA_ARGS__))

/* The 73-command GLES backend (gl_ohos_backend.c) is linked on HarmonyOS only:
 * the POSIX harness has no libGLESv2/libEGL.  Weak so this file links in both
 * builds; kage_gl_install() falls back to the simulation table below. */
extern const guest_gl_backend *gl_ohos_backend(void) __attribute__((weak));

/* EGL present leaf, same weak-link argument. */
typedef void *KageEGLDisplay;
typedef void *KageEGLSurface;
extern KageEGLDisplay eglGetCurrentDisplay(void) __attribute__((weak));
extern KageEGLSurface eglGetCurrentSurface(int32_t readdraw) __attribute__((weak));
extern uint32_t eglSwapBuffers(KageEGLDisplay dpy, KageEGLSurface surf)
    __attribute__((weak));
extern uint32_t eglSwapInterval(KageEGLDisplay dpy, int32_t interval)
    __attribute__((weak));
#define KAGE_EGL_DRAW 0x3059

/* ------------------------------------------------ KAGE manager layout ---- */
/* Exact live singleton in this PE (see upstream manual_kage.c).  Checking the
 * address before reading offsets keeps a malformed ECX inside the controlled
 * fault boundary instead of corrupting the guest. */
#define KAGE_MANAGER_RVA        0x007c7a30u
#define KAGE_MANAGER_ADDRESS    (GUEST_IMAGE_BASE + KAGE_MANAGER_RVA)
#define KAGE_FRAMEBUFFER_OFFSET 0x000000c8u
#define KAGE_RENDERBUFFER_OFFSET 0x000000ccu
#define KAGE_BACKEND_OFFSET     0x000000d8u
#define KAGE_WIDTH_OFFSET       0x000000e0u
#define KAGE_HEIGHT_OFFSET      0x000000e4u
#define KAGE_DEFAULT_WIDTH      960u
#define KAGE_DEFAULT_HEIGHT     540u

/* Exact GetClientRect caches the preserved leaves publish.  Several unrelated
 * callers read these four globals directly, so returning EAX alone is not
 * equivalent to the original. */
#define KAGE_WIDTH_CACHE        0x308071a0u
#define KAGE_HEIGHT_CACHE       0x308071a4u
#define KAGE_WIDTH_COMPANION    0x308071a8u
#define KAGE_HEIGHT_COMPANION   0x308071acu

#define KAGE_RVA_INITIALIZE              0x00560c60u
#define KAGE_RVA_INITIALIZE_RENDER       0x00561830u
#define KAGE_RVA_SHUTDOWN                0x00560e30u
#define KAGE_RVA_PRESENT                 0x00560eb0u
#define KAGE_RVA_GET_FB_WIDTH            0x00560f20u
#define KAGE_RVA_GET_FB_HEIGHT           0x00560f90u
#define KAGE_RVA_GL_PROVIDER             0x005700e0u

/* Guest render-target allocator: game-owned, never stubbed here. */
void sub_00565030(CPU *__restrict c);
extern void guest_original_00560eb0(CPU *__restrict c);

/* ------------------------------------------------ backend state -------- */
enum { KAGE_PC_BACKEND_DISABLED = 0, KAGE_PC_BACKEND_HIDDEN = 1 };

static const guest_gl_backend *s_gl;   /* installed table (real GLES or sim) */
static int      s_gl_installed;
static int      s_initialized;         /* backend accepted the surface size */
static int      s_ready;               /* set only once step 8 has completed */
static uint32_t s_width = KAGE_DEFAULT_WIDTH;
static uint32_t s_height = KAGE_DEFAULT_HEIGHT;
static unsigned s_present_count;
static const char *s_error = "KAGE GL backend: not initialized";

/* Latest name that crossed 0x005700e0 without a typed home, reported verbatim
 * by the loud fault so the missing symbol is named, not guessed. */
static char s_unresolved_name[64];
static unsigned s_kage_wgl_logged;
static unsigned s_kage_absent_logged;

/* Backend lifecycle leaves (kage_pc_backend.h spelling; the translated present
 * and handoff seams call them from generated code). */
int  kage_pc_backend_initialize(uint32_t width, uint32_t height);
void kage_pc_backend_shutdown(void);
int  kage_pc_backend_present(void);

/* Same macro spelling as upstream (kage_pc_backend.h): nonzero selects the
 * explicit native path, which is the only path this port has. */
int kage_pc_backend_mode(void) { return KAGE_PC_BACKEND_HIDDEN; }
int kage_pc_backend_ready(void) { return s_ready; }
uint32_t kage_pc_backend_width(void) { return s_width; }
uint32_t kage_pc_backend_height(void) { return s_height; }
const char *kage_pc_backend_last_error(void) { return s_error; }
unsigned kage_pc_backend_present_count(void) { return s_present_count; }

/* ------------------------------------------------- typed GL boundary ---- */
/* The frozen surface is mirrored in gl_surface_generated.h; the adapter block
 * and the name list below are generated from it (see the header comment). */
typedef struct kage_gl_entry {
    const char *name;
    void (*fn)(CPU *c);
} kage_gl_entry;

#define KAGE_GL_TOKEN_BASE   0x7d020000u   /* ext module 2 (host_ext_misc) */
#define KAGE_GL_NAME_MAX     63u

/* ESP points at the guest return address during an APIENTRY call. */
#define KAGE_ARG_U32(i) ld32(c->esp + 4u + ((uint32_t)(i) * 4u))
#define KAGE_ARG_F32(i) kage_bits_f32(KAGE_ARG_U32(i))
#define KAGE_ARG_F64(i) \
    kage_bits_f64(KAGE_ARG_U32(i), KAGE_ARG_U32((uint32_t)(i) + 1u))

static float kage_bits_f32(uint32_t bits) {
    float v;
    memcpy(&v, &bits, sizeof v);
    return v;
}
static double kage_bits_f64(uint32_t lo, uint32_t hi) {
    uint64_t bits = (uint64_t)lo | ((uint64_t)hi << 32);
    double v;
    memcpy(&v, &bits, sizeof v);
    return v;
}

/* Exact MSVC APIENTRY epilogue of a stdcall leaf: drop the return address and
 * the callee-owned argument words. */
static void kage_stdcall_ret(CPU *c, uint32_t argument_bytes) {
    (void)gpop_generated(c);
    c->esp += argument_bytes;
}

/* Every adopted command is counted and the first few are named: which frozen
 * commands the guest really issued is this seam's traffic receipt. */
static unsigned s_gl_call_count;

static void kage_gl_note(const char *name) {
    s_gl_call_count++;
    if (s_gl_call_count <= 96u) {
        KLOG("guest GL -> %{public}s", name);
    }
}

static void kage_gl_unsupported(CPU *c, const char *name) {
    static char message[192];
    snprintf(message, sizeof message, "unsupported GL backend symbol: %s", name);
    guest_fault(c, KAGE_RVA_GL_PROVIDER, message);
}

/* One adapter per frozen symbol: read the guest argument words, forward them
 * to the installed backend table, then perform the callee's RET n. */
#define KAGE_AD_VOID(m, bytes, ...)                          \
    static void ad_##m(CPU *c) {                             \
        if (s_gl->m == NULL) {                               \
            kage_gl_unsupported(c, #m);                      \
            return;                                          \
        }                                                    \
        kage_gl_note(#m);                                    \
        s_gl->m(__VA_ARGS__);                                \
        kage_stdcall_ret(c, bytes);                          \
    }
#define KAGE_AD_RET(rtype, m, bytes, ...)                    \
    static void ad_##m(CPU *c) {                             \
        rtype result;                                        \
        if (s_gl->m == NULL) {                               \
            kage_gl_unsupported(c, #m);                      \
            return;                                          \
        }                                                    \
        kage_gl_note(#m);                                    \
        result = s_gl->m(__VA_ARGS__);                       \
        c->eax = (uint32_t)result;                           \
        kage_stdcall_ret(c, bytes);                          \
    }

KAGE_AD_VOID(glActiveTexture, 4u, (guest_gl_enum)KAGE_ARG_U32(0))
KAGE_AD_VOID(glAlphaFunc, 8u, (guest_gl_enum)KAGE_ARG_U32(0), KAGE_ARG_F32(1))
KAGE_AD_VOID(glAttachShader, 8u, (guest_gl_uint)KAGE_ARG_U32(0), (guest_gl_uint)KAGE_ARG_U32(1))
KAGE_AD_VOID(glBindFramebuffer, 8u, (guest_gl_enum)KAGE_ARG_U32(0), (guest_gl_uint)KAGE_ARG_U32(1))
KAGE_AD_VOID(glBindRenderbuffer, 8u, (guest_gl_enum)KAGE_ARG_U32(0), (guest_gl_uint)KAGE_ARG_U32(1))
KAGE_AD_VOID(glBindTexture, 8u, (guest_gl_enum)KAGE_ARG_U32(0), (guest_gl_uint)KAGE_ARG_U32(1))
KAGE_AD_VOID(glBlendFuncSeparate, 16u, (guest_gl_enum)KAGE_ARG_U32(0), (guest_gl_enum)KAGE_ARG_U32(1), (guest_gl_enum)KAGE_ARG_U32(2), (guest_gl_enum)KAGE_ARG_U32(3))
KAGE_AD_RET(guest_gl_enum, glCheckFramebufferStatus, 4u, (guest_gl_enum)KAGE_ARG_U32(0))
KAGE_AD_VOID(glClampColorARB, 8u, (guest_gl_enum)KAGE_ARG_U32(0), (guest_gl_enum)KAGE_ARG_U32(1))
KAGE_AD_VOID(glClear, 4u, (guest_gl_bitfield)KAGE_ARG_U32(0))
KAGE_AD_VOID(glClearColor, 16u, KAGE_ARG_F32(0), KAGE_ARG_F32(1), KAGE_ARG_F32(2), KAGE_ARG_F32(3))
KAGE_AD_VOID(glClearDepth, 8u, KAGE_ARG_F64(0))
KAGE_AD_VOID(glCompileShader, 4u, (guest_gl_uint)KAGE_ARG_U32(0))
KAGE_AD_RET(guest_gl_uint, glCreateProgram, 0u)
KAGE_AD_RET(guest_gl_uint, glCreateShader, 4u, (guest_gl_enum)KAGE_ARG_U32(0))
KAGE_AD_VOID(glCullFace, 4u, (guest_gl_enum)KAGE_ARG_U32(0))
KAGE_AD_VOID(glDeleteFramebuffers, 8u, (guest_gl_sizei)KAGE_ARG_U32(0), (guest_gl_addr)KAGE_ARG_U32(1))
KAGE_AD_VOID(glDeleteProgram, 4u, (guest_gl_uint)KAGE_ARG_U32(0))
KAGE_AD_VOID(glDeleteRenderbuffers, 8u, (guest_gl_sizei)KAGE_ARG_U32(0), (guest_gl_addr)KAGE_ARG_U32(1))
KAGE_AD_VOID(glDeleteShader, 4u, (guest_gl_uint)KAGE_ARG_U32(0))
KAGE_AD_VOID(glDeleteTextures, 8u, (guest_gl_sizei)KAGE_ARG_U32(0), (guest_gl_addr)KAGE_ARG_U32(1))
KAGE_AD_VOID(glDepthFunc, 4u, (guest_gl_enum)KAGE_ARG_U32(0))
KAGE_AD_VOID(glDisableVertexAttribArray, 4u, (guest_gl_uint)KAGE_ARG_U32(0))
KAGE_AD_VOID(glDrawElements, 16u, (guest_gl_enum)KAGE_ARG_U32(0), (guest_gl_sizei)KAGE_ARG_U32(1), (guest_gl_enum)KAGE_ARG_U32(2), (guest_gl_addr)KAGE_ARG_U32(3))
KAGE_AD_VOID(glEnable, 4u, (guest_gl_enum)KAGE_ARG_U32(0))
KAGE_AD_VOID(glEnableVertexAttribArray, 4u, (guest_gl_uint)KAGE_ARG_U32(0))
KAGE_AD_VOID(glFramebufferRenderbuffer, 16u, (guest_gl_enum)KAGE_ARG_U32(0), (guest_gl_enum)KAGE_ARG_U32(1), (guest_gl_enum)KAGE_ARG_U32(2), (guest_gl_uint)KAGE_ARG_U32(3))
KAGE_AD_VOID(glFramebufferTexture2D, 20u, (guest_gl_enum)KAGE_ARG_U32(0), (guest_gl_enum)KAGE_ARG_U32(1), (guest_gl_enum)KAGE_ARG_U32(2), (guest_gl_uint)KAGE_ARG_U32(3), (guest_gl_int)KAGE_ARG_U32(4))
KAGE_AD_VOID(glGenFramebuffers, 8u, (guest_gl_sizei)KAGE_ARG_U32(0), (guest_gl_addr)KAGE_ARG_U32(1))
/* Hand-written (not the macro): the out-pointer argument read back 0 on
 * device while the PC harness saw a live arena address, and the whole KAGE
 * bring-up hinges on it. Logs every word of the frame so the stack layout is
 * unambiguous in one run. */
static void ad_glGenRenderbuffers(CPU *c)
{
    if (s_gl->glGenRenderbuffers == NULL) {
        kage_gl_unsupported(c, "glGenRenderbuffers");
        return;
    }
    kage_gl_note("glGenRenderbuffers");
    KLOG("glGenRenderbuffers args: esp=0x%{public}08x ret=0x%{public}08x "
         "w0=0x%{public}08x w1=0x%{public}08x w2=0x%{public}08x "
         "ecx=0x%{public}08x ebx=0x%{public}08x",
         c->esp, ld32(c->esp), ld32(c->esp + 4U), ld32(c->esp + 8U),
         ld32(c->esp + 12U), c->ecx, c->ebx);
    s_gl->glGenRenderbuffers((guest_gl_sizei)KAGE_ARG_U32(0),
                             (guest_gl_addr)KAGE_ARG_U32(1));
    kage_stdcall_ret(c, 8u);
}
KAGE_AD_VOID(glGenTextures, 8u, (guest_gl_sizei)KAGE_ARG_U32(0), (guest_gl_addr)KAGE_ARG_U32(1))
KAGE_AD_RET(guest_gl_int, glGetAttribLocation, 8u, (guest_gl_uint)KAGE_ARG_U32(0), (guest_gl_addr)KAGE_ARG_U32(1))
KAGE_AD_VOID(glGetIntegerv, 8u, (guest_gl_enum)KAGE_ARG_U32(0), (guest_gl_addr)KAGE_ARG_U32(1))
KAGE_AD_VOID(glGetProgramInfoLog, 16u, (guest_gl_uint)KAGE_ARG_U32(0), (guest_gl_sizei)KAGE_ARG_U32(1), (guest_gl_addr)KAGE_ARG_U32(2), (guest_gl_addr)KAGE_ARG_U32(3))
KAGE_AD_VOID(glGetProgramiv, 12u, (guest_gl_uint)KAGE_ARG_U32(0), (guest_gl_enum)KAGE_ARG_U32(1), (guest_gl_addr)KAGE_ARG_U32(2))
KAGE_AD_VOID(glGetRenderbufferParameteriv, 12u, (guest_gl_enum)KAGE_ARG_U32(0), (guest_gl_enum)KAGE_ARG_U32(1), (guest_gl_addr)KAGE_ARG_U32(2))
KAGE_AD_VOID(glGetShaderInfoLog, 16u, (guest_gl_uint)KAGE_ARG_U32(0), (guest_gl_sizei)KAGE_ARG_U32(1), (guest_gl_addr)KAGE_ARG_U32(2), (guest_gl_addr)KAGE_ARG_U32(3))
KAGE_AD_VOID(glGetShaderiv, 12u, (guest_gl_uint)KAGE_ARG_U32(0), (guest_gl_enum)KAGE_ARG_U32(1), (guest_gl_addr)KAGE_ARG_U32(2))
KAGE_AD_RET(guest_gl_addr, glGetString, 4u, (guest_gl_enum)KAGE_ARG_U32(0))
KAGE_AD_RET(guest_gl_addr, glGetStringi, 8u, (guest_gl_enum)KAGE_ARG_U32(0), (guest_gl_uint)KAGE_ARG_U32(1))
KAGE_AD_RET(guest_gl_int, glGetUniformLocation, 8u, (guest_gl_uint)KAGE_ARG_U32(0), (guest_gl_addr)KAGE_ARG_U32(1))
KAGE_AD_VOID(glLinkProgram, 4u, (guest_gl_uint)KAGE_ARG_U32(0))
KAGE_AD_VOID(glReadPixels, 28u, (guest_gl_int)KAGE_ARG_U32(0), (guest_gl_int)KAGE_ARG_U32(1), (guest_gl_sizei)KAGE_ARG_U32(2), (guest_gl_sizei)KAGE_ARG_U32(3), (guest_gl_enum)KAGE_ARG_U32(4), (guest_gl_enum)KAGE_ARG_U32(5), (guest_gl_addr)KAGE_ARG_U32(6))
KAGE_AD_VOID(glRenderbufferStorage, 16u, (guest_gl_enum)KAGE_ARG_U32(0), (guest_gl_enum)KAGE_ARG_U32(1), (guest_gl_sizei)KAGE_ARG_U32(2), (guest_gl_sizei)KAGE_ARG_U32(3))
KAGE_AD_VOID(glShaderSource, 16u, (guest_gl_uint)KAGE_ARG_U32(0), (guest_gl_sizei)KAGE_ARG_U32(1), (guest_gl_addr)KAGE_ARG_U32(2), (guest_gl_addr)KAGE_ARG_U32(3))
KAGE_AD_VOID(glTexImage2D, 36u, (guest_gl_enum)KAGE_ARG_U32(0), (guest_gl_int)KAGE_ARG_U32(1), (guest_gl_int)KAGE_ARG_U32(2), (guest_gl_sizei)KAGE_ARG_U32(3), (guest_gl_sizei)KAGE_ARG_U32(4), (guest_gl_int)KAGE_ARG_U32(5), (guest_gl_enum)KAGE_ARG_U32(6), (guest_gl_enum)KAGE_ARG_U32(7), (guest_gl_addr)KAGE_ARG_U32(8))
KAGE_AD_VOID(glTexParameteri, 12u, (guest_gl_enum)KAGE_ARG_U32(0), (guest_gl_enum)KAGE_ARG_U32(1), (guest_gl_int)KAGE_ARG_U32(2))
KAGE_AD_VOID(glTexSubImage2D, 36u, (guest_gl_enum)KAGE_ARG_U32(0), (guest_gl_int)KAGE_ARG_U32(1), (guest_gl_int)KAGE_ARG_U32(2), (guest_gl_int)KAGE_ARG_U32(3), (guest_gl_sizei)KAGE_ARG_U32(4), (guest_gl_sizei)KAGE_ARG_U32(5), (guest_gl_enum)KAGE_ARG_U32(6), (guest_gl_enum)KAGE_ARG_U32(7), (guest_gl_addr)KAGE_ARG_U32(8))
KAGE_AD_VOID(glUniform1fv, 12u, (guest_gl_int)KAGE_ARG_U32(0), (guest_gl_sizei)KAGE_ARG_U32(1), (guest_gl_addr)KAGE_ARG_U32(2))
KAGE_AD_VOID(glUniform1i, 8u, (guest_gl_int)KAGE_ARG_U32(0), (guest_gl_int)KAGE_ARG_U32(1))
KAGE_AD_VOID(glUniform1iv, 12u, (guest_gl_int)KAGE_ARG_U32(0), (guest_gl_sizei)KAGE_ARG_U32(1), (guest_gl_addr)KAGE_ARG_U32(2))
KAGE_AD_VOID(glUniform1uiv, 12u, (guest_gl_int)KAGE_ARG_U32(0), (guest_gl_sizei)KAGE_ARG_U32(1), (guest_gl_addr)KAGE_ARG_U32(2))
KAGE_AD_VOID(glUniform2fv, 12u, (guest_gl_int)KAGE_ARG_U32(0), (guest_gl_sizei)KAGE_ARG_U32(1), (guest_gl_addr)KAGE_ARG_U32(2))
KAGE_AD_VOID(glUniform2iv, 12u, (guest_gl_int)KAGE_ARG_U32(0), (guest_gl_sizei)KAGE_ARG_U32(1), (guest_gl_addr)KAGE_ARG_U32(2))
KAGE_AD_VOID(glUniform2uiv, 12u, (guest_gl_int)KAGE_ARG_U32(0), (guest_gl_sizei)KAGE_ARG_U32(1), (guest_gl_addr)KAGE_ARG_U32(2))
KAGE_AD_VOID(glUniform3fv, 12u, (guest_gl_int)KAGE_ARG_U32(0), (guest_gl_sizei)KAGE_ARG_U32(1), (guest_gl_addr)KAGE_ARG_U32(2))
KAGE_AD_VOID(glUniform3iv, 12u, (guest_gl_int)KAGE_ARG_U32(0), (guest_gl_sizei)KAGE_ARG_U32(1), (guest_gl_addr)KAGE_ARG_U32(2))
KAGE_AD_VOID(glUniform3uiv, 12u, (guest_gl_int)KAGE_ARG_U32(0), (guest_gl_sizei)KAGE_ARG_U32(1), (guest_gl_addr)KAGE_ARG_U32(2))
KAGE_AD_VOID(glUniform4fv, 12u, (guest_gl_int)KAGE_ARG_U32(0), (guest_gl_sizei)KAGE_ARG_U32(1), (guest_gl_addr)KAGE_ARG_U32(2))
KAGE_AD_VOID(glUniform4iv, 12u, (guest_gl_int)KAGE_ARG_U32(0), (guest_gl_sizei)KAGE_ARG_U32(1), (guest_gl_addr)KAGE_ARG_U32(2))
KAGE_AD_VOID(glUniform4uiv, 12u, (guest_gl_int)KAGE_ARG_U32(0), (guest_gl_sizei)KAGE_ARG_U32(1), (guest_gl_addr)KAGE_ARG_U32(2))
KAGE_AD_VOID(glUniformMatrix2fv, 16u, (guest_gl_int)KAGE_ARG_U32(0), (guest_gl_sizei)KAGE_ARG_U32(1), (guest_gl_boolean)KAGE_ARG_U32(2), (guest_gl_addr)KAGE_ARG_U32(3))
KAGE_AD_VOID(glUniformMatrix2x3fv, 16u, (guest_gl_int)KAGE_ARG_U32(0), (guest_gl_sizei)KAGE_ARG_U32(1), (guest_gl_boolean)KAGE_ARG_U32(2), (guest_gl_addr)KAGE_ARG_U32(3))
KAGE_AD_VOID(glUniformMatrix2x4fv, 16u, (guest_gl_int)KAGE_ARG_U32(0), (guest_gl_sizei)KAGE_ARG_U32(1), (guest_gl_boolean)KAGE_ARG_U32(2), (guest_gl_addr)KAGE_ARG_U32(3))
KAGE_AD_VOID(glUniformMatrix3fv, 16u, (guest_gl_int)KAGE_ARG_U32(0), (guest_gl_sizei)KAGE_ARG_U32(1), (guest_gl_boolean)KAGE_ARG_U32(2), (guest_gl_addr)KAGE_ARG_U32(3))
KAGE_AD_VOID(glUniformMatrix3x2fv, 16u, (guest_gl_int)KAGE_ARG_U32(0), (guest_gl_sizei)KAGE_ARG_U32(1), (guest_gl_boolean)KAGE_ARG_U32(2), (guest_gl_addr)KAGE_ARG_U32(3))
KAGE_AD_VOID(glUniformMatrix3x4fv, 16u, (guest_gl_int)KAGE_ARG_U32(0), (guest_gl_sizei)KAGE_ARG_U32(1), (guest_gl_boolean)KAGE_ARG_U32(2), (guest_gl_addr)KAGE_ARG_U32(3))
KAGE_AD_VOID(glUniformMatrix4fv, 16u, (guest_gl_int)KAGE_ARG_U32(0), (guest_gl_sizei)KAGE_ARG_U32(1), (guest_gl_boolean)KAGE_ARG_U32(2), (guest_gl_addr)KAGE_ARG_U32(3))
KAGE_AD_VOID(glUniformMatrix4x2fv, 16u, (guest_gl_int)KAGE_ARG_U32(0), (guest_gl_sizei)KAGE_ARG_U32(1), (guest_gl_boolean)KAGE_ARG_U32(2), (guest_gl_addr)KAGE_ARG_U32(3))
KAGE_AD_VOID(glUniformMatrix4x3fv, 16u, (guest_gl_int)KAGE_ARG_U32(0), (guest_gl_sizei)KAGE_ARG_U32(1), (guest_gl_boolean)KAGE_ARG_U32(2), (guest_gl_addr)KAGE_ARG_U32(3))
KAGE_AD_VOID(glUseProgram, 4u, (guest_gl_uint)KAGE_ARG_U32(0))
KAGE_AD_VOID(glVertexAttribPointer, 24u, (guest_gl_uint)KAGE_ARG_U32(0), (guest_gl_int)KAGE_ARG_U32(1), (guest_gl_enum)KAGE_ARG_U32(2), (guest_gl_boolean)KAGE_ARG_U32(3), (guest_gl_sizei)KAGE_ARG_U32(4), (guest_gl_addr)KAGE_ARG_U32(5))
KAGE_AD_VOID(glViewport, 16u, (guest_gl_int)KAGE_ARG_U32(0), (guest_gl_int)KAGE_ARG_U32(1), (guest_gl_sizei)KAGE_ARG_U32(2), (guest_gl_sizei)KAGE_ARG_U32(3))

#define KAGE_GL_SURFACE(X) \
    X(glActiveTexture, ad_glActiveTexture) \
    X(glAlphaFunc, ad_glAlphaFunc) \
    X(glAttachShader, ad_glAttachShader) \
    X(glBindFramebuffer, ad_glBindFramebuffer) \
    X(glBindRenderbuffer, ad_glBindRenderbuffer) \
    X(glBindTexture, ad_glBindTexture) \
    X(glBlendFuncSeparate, ad_glBlendFuncSeparate) \
    X(glCheckFramebufferStatus, ad_glCheckFramebufferStatus) \
    X(glClampColorARB, ad_glClampColorARB) \
    X(glClear, ad_glClear) \
    X(glClearColor, ad_glClearColor) \
    X(glClearDepth, ad_glClearDepth) \
    X(glCompileShader, ad_glCompileShader) \
    X(glCreateProgram, ad_glCreateProgram) \
    X(glCreateShader, ad_glCreateShader) \
    X(glCullFace, ad_glCullFace) \
    X(glDeleteFramebuffers, ad_glDeleteFramebuffers) \
    X(glDeleteProgram, ad_glDeleteProgram) \
    X(glDeleteRenderbuffers, ad_glDeleteRenderbuffers) \
    X(glDeleteShader, ad_glDeleteShader) \
    X(glDeleteTextures, ad_glDeleteTextures) \
    X(glDepthFunc, ad_glDepthFunc) \
    X(glDisableVertexAttribArray, ad_glDisableVertexAttribArray) \
    X(glDrawElements, ad_glDrawElements) \
    X(glEnable, ad_glEnable) \
    X(glEnableVertexAttribArray, ad_glEnableVertexAttribArray) \
    X(glFramebufferRenderbuffer, ad_glFramebufferRenderbuffer) \
    X(glFramebufferTexture2D, ad_glFramebufferTexture2D) \
    X(glGenFramebuffers, ad_glGenFramebuffers) \
    X(glGenRenderbuffers, ad_glGenRenderbuffers) \
    X(glGenTextures, ad_glGenTextures) \
    X(glGetAttribLocation, ad_glGetAttribLocation) \
    X(glGetIntegerv, ad_glGetIntegerv) \
    X(glGetProgramInfoLog, ad_glGetProgramInfoLog) \
    X(glGetProgramiv, ad_glGetProgramiv) \
    X(glGetRenderbufferParameteriv, ad_glGetRenderbufferParameteriv) \
    X(glGetShaderInfoLog, ad_glGetShaderInfoLog) \
    X(glGetShaderiv, ad_glGetShaderiv) \
    X(glGetString, ad_glGetString) \
    X(glGetStringi, ad_glGetStringi) \
    X(glGetUniformLocation, ad_glGetUniformLocation) \
    X(glLinkProgram, ad_glLinkProgram) \
    X(glReadPixels, ad_glReadPixels) \
    X(glRenderbufferStorage, ad_glRenderbufferStorage) \
    X(glShaderSource, ad_glShaderSource) \
    X(glTexImage2D, ad_glTexImage2D) \
    X(glTexParameteri, ad_glTexParameteri) \
    X(glTexSubImage2D, ad_glTexSubImage2D) \
    X(glUniform1fv, ad_glUniform1fv) \
    X(glUniform1i, ad_glUniform1i) \
    X(glUniform1iv, ad_glUniform1iv) \
    X(glUniform1uiv, ad_glUniform1uiv) \
    X(glUniform2fv, ad_glUniform2fv) \
    X(glUniform2iv, ad_glUniform2iv) \
    X(glUniform2uiv, ad_glUniform2uiv) \
    X(glUniform3fv, ad_glUniform3fv) \
    X(glUniform3iv, ad_glUniform3iv) \
    X(glUniform3uiv, ad_glUniform3uiv) \
    X(glUniform4fv, ad_glUniform4fv) \
    X(glUniform4iv, ad_glUniform4iv) \
    X(glUniform4uiv, ad_glUniform4uiv) \
    X(glUniformMatrix2fv, ad_glUniformMatrix2fv) \
    X(glUniformMatrix2x3fv, ad_glUniformMatrix2x3fv) \
    X(glUniformMatrix2x4fv, ad_glUniformMatrix2x4fv) \
    X(glUniformMatrix3fv, ad_glUniformMatrix3fv) \
    X(glUniformMatrix3x2fv, ad_glUniformMatrix3x2fv) \
    X(glUniformMatrix3x4fv, ad_glUniformMatrix3x4fv) \
    X(glUniformMatrix4fv, ad_glUniformMatrix4fv) \
    X(glUniformMatrix4x2fv, ad_glUniformMatrix4x2fv) \
    X(glUniformMatrix4x3fv, ad_glUniformMatrix4x3fv) \
    X(glUseProgram, ad_glUseProgram) \
    X(glVertexAttribPointer, ad_glVertexAttribPointer) \
    X(glViewport, ad_glViewport)

/* Registry order IS the token order: token = KAGE_GL_TOKEN_BASE | index.  A
 * name that is not in the frozen surface resolves to the trailing entry, whose
 * adapter reports the exact symbol instead of jumping into a NULL slot. */
#define KAGE_GL_UNKNOWN_INDEX GUEST_GL_SURFACE_COUNT

static void ad_gl_unresolved(CPU *c) {
    static char message[192];
    snprintf(message, sizeof message,
             "GL entry point outside the 73-command surface: %s",
             s_unresolved_name[0] ? s_unresolved_name : "?");
    guest_fault(c, KAGE_RVA_GL_PROVIDER, message);
}

/* ------------------------------------------------- platform GL extras ---- */
/* Two WGL entry points the guest resolves through OPENGL32!wglGetProcAddress
 * and then calls unconditionally (after a NULL check), exactly as on Windows.
 * They are not part of the frozen 73-command GL surface: they control the swap
 * interval of the present path, so they belong to the same seam as
 * kage_pc_backend_present().  On a device build the interval reaches EGL; on
 * the POSIX harness the request is recorded and reported, not silently lost. */
static int s_swap_interval;

static int kage_set_swap_interval(int interval) {
    KageEGLDisplay dpy;

    s_swap_interval = interval;
    if (eglSwapInterval == NULL || eglGetCurrentDisplay == NULL) {
        return 1;   /* host harness: state advanced, no EGL to configure */
    }
    dpy = eglGetCurrentDisplay();
    if (dpy == NULL) {
        return 0;
    }
    return eglSwapInterval(dpy, interval) ? 1 : 0;
}

static void ad_wglSwapIntervalEXT(CPU *c) {
    int interval = (int)KAGE_ARG_U32(0);
    c->eax = (uint32_t)kage_set_swap_interval(interval);
    kage_stdcall_ret(c, 4u);     /* WGLAPI: one argument word */
}

static void ad_wglGetSwapIntervalEXT(CPU *c) {
    c->eax = (uint32_t)s_swap_interval;
    kage_stdcall_ret(c, 0u);
}

int kage_pc_backend_set_vsync(int enabled) {
    return kage_set_swap_interval(enabled ? 1 : 0);
}

int kage_pc_backend_vsync_enabled(void) {
    return s_swap_interval != 0;
}

static const kage_gl_entry s_kage_gl[] = {
#define KAGE_GL_ENTRY(name, adapter) { #name, adapter },
    KAGE_GL_SURFACE(KAGE_GL_ENTRY)
#undef KAGE_GL_ENTRY
    { "*unresolved*", ad_gl_unresolved },
    { "wglSwapIntervalEXT", ad_wglSwapIntervalEXT },
    { "wglGetSwapIntervalEXT", ad_wglGetSwapIntervalEXT },
};

#define KAGE_GL_ENTRY_COUNT \
    ((unsigned)(sizeof s_kage_gl / sizeof s_kage_gl[0]))

/* The dynamic side of the GetProcAddress token space: one name per surface
 * command, in the same order, so the guest's own GL slots resolve into it. */
static const host_ext_entry s_kage_gl_dynamic[] = {
#define KAGE_GL_EXT(name, adapter) { #name, adapter },
    KAGE_GL_SURFACE(KAGE_GL_EXT)
#undef KAGE_GL_EXT
    { "*unresolved*", ad_gl_unresolved },
    { "wglSwapIntervalEXT", ad_wglSwapIntervalEXT },
    { "wglGetSwapIntervalEXT", ad_wglGetSwapIntervalEXT },
};

static void h_kage_wglGetProcAddress(CPU *c);

static const host_ext_entry s_kage_gl_static[] = {
    /* Replaces host_ohos.c's placeholder, which returned an unregistered
     * counter token (see h_kage_wglGetProcAddress below). */
    { "OPENGL32.dll!wglGetProcAddress", h_kage_wglGetProcAddress },
};

static const host_ext_module s_kage_gl_module = {
    s_kage_gl_static,
    (unsigned)(sizeof s_kage_gl_static / sizeof s_kage_gl_static[0]),
    s_kage_gl_dynamic,
    (unsigned)(sizeof s_kage_gl_dynamic / sizeof s_kage_gl_dynamic[0]),
};

/* Extension module slot 1 in host_ohos.c's s_ext_providers (module index 2 in
 * the token word).  host_ohos.c declares this weak; a strong definition here
 * is the module actually installed.  Every GL name the guest resolves now has
 * an owner in guest_host_dynamic()'s token space. */
const host_ext_module *host_ext_kage(void) { return &s_kage_gl_module; }

/* --------------------------- host simulation backend state ---------------- */
/* Only installed when gl_ohos_backend.c is not linked (POSIX harness, no
 * libGLESv2/libEGL).  It is not a renderer: it exists so that the same adapters
 * run end to end and the guest's own GL queries stay self-consistent - object
 * names are allocated, bindings and renderbuffer dimensions are remembered,
 * shader/program queries answer "compiled and linked" - which lets the guest
 * execute its real init path on a machine with no GL context.  Every member is
 * present; nothing here silently claims to have drawn anything. */
enum {
    KAGE_HOST_GL_FRAMEBUFFER                 = 0x8d40u,
    KAGE_HOST_GL_FRAMEBUFFER_BINDING         = 0x8ca6u,
    KAGE_HOST_GL_RENDERBUFFER_BINDING        = 0x8ca7u,
    KAGE_HOST_GL_VIEWPORT                    = 0x0ba2u,
    KAGE_HOST_GL_RENDERBUFFER_WIDTH          = 0x8d42u,
    KAGE_HOST_GL_RENDERBUFFER_HEIGHT         = 0x8d43u,
    KAGE_HOST_GL_RENDERBUFFER_INTERNAL_FORMAT = 0x8d44u,
    KAGE_HOST_GL_VENDOR                      = 0x1f00u,
    KAGE_HOST_GL_RENDERER                    = 0x1f01u,
    KAGE_HOST_GL_VERSION                     = 0x1f02u,
    KAGE_HOST_GL_EXTENSIONS                  = 0x1f03u,
    KAGE_HOST_GL_SHADING_LANGUAGE_VERSION    = 0x8b8cu,
    KAGE_HOST_GL_NUM_EXTENSIONS              = 0x821du,
    KAGE_HOST_GL_MAX_TEXTURE_SIZE            = 0x0d33u,
    KAGE_HOST_GL_COMPILE_STATUS              = 0x8b81u,
    KAGE_HOST_GL_LINK_STATUS                 = 0x8b82u,
    KAGE_HOST_GL_FRAMEBUFFER_COMPLETE        = 0x8cd5u
};

static uint32_t s_host_next_id = 1u;
static uint32_t s_host_bound_fb;
static uint32_t s_host_bound_rbo;
static uint32_t s_host_rbo_w = KAGE_DEFAULT_WIDTH;
static uint32_t s_host_rbo_h = KAGE_DEFAULT_HEIGHT;
static uint32_t s_host_rbo_fmt;
static uint32_t s_host_vp[4];
static int      s_host_read_pixels_warned;

static uint32_t kage_host_new_id(void) { return s_host_next_id++; }

static void kage_host_gen_ids(guest_gl_sizei n, guest_gl_addr addrs) {
    guest_gl_sizei i;
    for (i = 0; i < n; i++) {
        st32(addrs + (uint32_t)i * 4u, kage_host_new_id());
    }
}

static void kage_host_bind_framebuffer(guest_gl_enum target, guest_gl_uint fb) {
    if (target == KAGE_HOST_GL_FRAMEBUFFER) {
        s_host_bound_fb = fb;
    }
}

static void kage_host_bind_renderbuffer(guest_gl_uint rb) {
    s_host_bound_rbo = rb;
}

static void kage_host_rbo_storage(guest_gl_sizei w, guest_gl_sizei h,
                                  guest_gl_enum internalformat) {
    s_host_rbo_w = (uint32_t)w;
    s_host_rbo_h = (uint32_t)h;
    s_host_rbo_fmt = internalformat;
}

static void kage_host_viewport(guest_gl_int x, guest_gl_int y,
                               guest_gl_sizei w, guest_gl_sizei h) {
    s_host_vp[0] = (uint32_t)x;
    s_host_vp[1] = (uint32_t)y;
    s_host_vp[2] = (uint32_t)w;
    s_host_vp[3] = (uint32_t)h;
}

static void kage_host_get_integerv(guest_gl_enum pname, guest_gl_addr data) {
    if (data == 0u) {
        return;
    }
    switch (pname) {
    case KAGE_HOST_GL_FRAMEBUFFER_BINDING:
        st32(data, s_host_bound_fb);
        break;
    case KAGE_HOST_GL_RENDERBUFFER_BINDING:
        st32(data, s_host_bound_rbo);
        break;
    case KAGE_HOST_GL_VIEWPORT:
        st32(data, s_host_vp[0]);
        st32(data + 4u, s_host_vp[1]);
        st32(data + 8u, s_host_vp[2]);
        st32(data + 12u, s_host_vp[3]);
        break;
    case KAGE_HOST_GL_MAX_TEXTURE_SIZE:
        st32(data, 4096u);
        break;
    case KAGE_HOST_GL_NUM_EXTENSIONS:
    default:
        st32(data, 0u);
        break;
    }
}

static void kage_host_get_rbo_param(guest_gl_enum pname, guest_gl_addr params) {
    if (params == 0u) {
        return;
    }
    switch (pname) {
    case KAGE_HOST_GL_RENDERBUFFER_WIDTH:
        st32(params, s_host_rbo_w);
        break;
    case KAGE_HOST_GL_RENDERBUFFER_HEIGHT:
        st32(params, s_host_rbo_h);
        break;
    case KAGE_HOST_GL_RENDERBUFFER_INTERNAL_FORMAT:
        st32(params, s_host_rbo_fmt);
        break;
    default:
        st32(params, 0u);
        break;
    }
}

static void kage_host_get_objectiv(guest_gl_enum pname, guest_gl_addr params) {
    if (params == 0u) {
        return;
    }
    /* Only the two "did it build" queries answer true; counts and lengths stay
     * zero so the guest's enumeration loops terminate immediately. */
    st32(params, (pname == KAGE_HOST_GL_COMPILE_STATUS ||
                  pname == KAGE_HOST_GL_LINK_STATUS) ? 1u : 0u);
}

static void kage_host_get_info_log(guest_gl_addr length, guest_gl_addr infoLog,
                                   guest_gl_sizei bufSize) {
    if (length != 0u) {
        st32(length, 0u);
    }
    if (infoLog != 0u && bufSize > 0) {
        st8(infoLog, 0u);
    }
}

static guest_gl_addr kage_host_get_string(guest_gl_enum name) {
    static uint32_t vendor, renderer, version, glsl, extensions;
    const char *text = "";
    uint32_t *slot = &extensions;

    switch (name) {
    case KAGE_HOST_GL_VENDOR:                        text = "isaac-ohos";   slot = &vendor; break;
    case KAGE_HOST_GL_RENDERER:                      text = "host harness (no GL context)"; slot = &renderer; break;
    case KAGE_HOST_GL_VERSION:                       text = "OpenGL ES 3.0 isaac-ohos-host"; slot = &version; break;
    case KAGE_HOST_GL_SHADING_LANGUAGE_VERSION:      text = "OpenGL ES GLSL ES 3.00"; slot = &glsl; break;
    default:                                         return 0u;   /* GL_EXTENSIONS: absent */
    }
    if (*slot == 0u) {
        *slot = guest_mem_push_string(text, strlen(text));
        KLOG("host simulation: glGetString -> \"%{public}s\"", text);
    }
    return *slot;
}

static void kage_host_read_pixels(guest_gl_sizei width, guest_gl_sizei height) {
    if (!s_host_read_pixels_warned) {
        s_host_read_pixels_warned = 1;
        KERR("host simulation: glReadPixels(%{public}d x %{public}d) ignored - "
             "there is no framebuffer to read on this host", width, height);
    }
}

static void kage_host_glActiveTexture(guest_gl_enum texture)
{
    (void)0;
}
static void kage_host_glAlphaFunc(guest_gl_enum func, guest_gl_float ref)
{
    (void)0;
}
static void kage_host_glAttachShader(guest_gl_uint program, guest_gl_uint shader)
{
    (void)0;
}
static void kage_host_glBindFramebuffer(guest_gl_enum target, guest_gl_uint framebuffer)
{
    kage_host_bind_framebuffer(target, framebuffer);
}
static void kage_host_glBindRenderbuffer(guest_gl_enum target, guest_gl_uint renderbuffer)
{
    kage_host_bind_renderbuffer(renderbuffer);
}
static void kage_host_glBindTexture(guest_gl_enum target, guest_gl_uint texture)
{
    (void)0;
}
static void kage_host_glBlendFuncSeparate(guest_gl_enum sfactorRGB, guest_gl_enum dfactorRGB, guest_gl_enum sfactorAlpha, guest_gl_enum dfactorAlpha)
{
    (void)0;
}
static guest_gl_enum kage_host_glCheckFramebufferStatus(guest_gl_enum target)
{
    return KAGE_HOST_GL_FRAMEBUFFER_COMPLETE;
}
static void kage_host_glClampColorARB(guest_gl_enum target, guest_gl_enum clamp)
{
    (void)0;
}
static void kage_host_glClear(guest_gl_bitfield mask)
{
    (void)0;
}
static void kage_host_glClearColor(guest_gl_float red, guest_gl_float green, guest_gl_float blue, guest_gl_float alpha)
{
    (void)0;
}
static void kage_host_glClearDepth(guest_gl_double depth)
{
    (void)0;
}
static void kage_host_glCompileShader(guest_gl_uint shader)
{
    (void)0;
}
static guest_gl_uint kage_host_glCreateProgram(void)
{
    return kage_host_new_id();
}
static guest_gl_uint kage_host_glCreateShader(guest_gl_enum type)
{
    return kage_host_new_id();
}
static void kage_host_glCullFace(guest_gl_enum mode)
{
    (void)0;
}
static void kage_host_glDeleteFramebuffers(guest_gl_sizei n, guest_gl_addr framebuffers)
{
    (void)0;
}
static void kage_host_glDeleteProgram(guest_gl_uint program)
{
    (void)0;
}
static void kage_host_glDeleteRenderbuffers(guest_gl_sizei n, guest_gl_addr renderbuffers)
{
    (void)0;
}
static void kage_host_glDeleteShader(guest_gl_uint shader)
{
    (void)0;
}
static void kage_host_glDeleteTextures(guest_gl_sizei n, guest_gl_addr textures)
{
    (void)0;
}
static void kage_host_glDepthFunc(guest_gl_enum func)
{
    (void)0;
}
static void kage_host_glDisableVertexAttribArray(guest_gl_uint index)
{
    (void)0;
}
static void kage_host_glDrawElements(guest_gl_enum mode, guest_gl_sizei count, guest_gl_enum type, guest_gl_addr indices)
{
    (void)0;
}
static void kage_host_glEnable(guest_gl_enum cap)
{
    (void)0;
}
static void kage_host_glEnableVertexAttribArray(guest_gl_uint index)
{
    (void)0;
}
static void kage_host_glFramebufferRenderbuffer(guest_gl_enum target, guest_gl_enum attachment, guest_gl_enum renderbuffertarget, guest_gl_uint renderbuffer)
{
    (void)0;
}
static void kage_host_glFramebufferTexture2D(guest_gl_enum target, guest_gl_enum attachment, guest_gl_enum textarget, guest_gl_uint texture, guest_gl_int level)
{
    (void)0;
}
static void kage_host_glGenFramebuffers(guest_gl_sizei n, guest_gl_addr framebuffers)
{
    kage_host_gen_ids(n, framebuffers);
}
static void kage_host_glGenRenderbuffers(guest_gl_sizei n, guest_gl_addr renderbuffers)
{
    kage_host_gen_ids(n, renderbuffers);
}
static void kage_host_glGenTextures(guest_gl_sizei n, guest_gl_addr textures)
{
    kage_host_gen_ids(n, textures);
}
static guest_gl_int kage_host_glGetAttribLocation(guest_gl_uint program, guest_gl_addr name)
{
    return 0;
}
static void kage_host_glGetIntegerv(guest_gl_enum pname, guest_gl_addr data)
{
    kage_host_get_integerv(pname, data);
}
static void kage_host_glGetProgramInfoLog(guest_gl_uint program, guest_gl_sizei bufSize, guest_gl_addr length, guest_gl_addr infoLog)
{
    kage_host_get_info_log(length, infoLog, bufSize);
}
static void kage_host_glGetProgramiv(guest_gl_uint program, guest_gl_enum pname, guest_gl_addr params)
{
    kage_host_get_objectiv(pname, params);
}
static void kage_host_glGetRenderbufferParameteriv(guest_gl_enum target, guest_gl_enum pname, guest_gl_addr params)
{
    kage_host_get_rbo_param(pname, params);
}
static void kage_host_glGetShaderInfoLog(guest_gl_uint shader, guest_gl_sizei bufSize, guest_gl_addr length, guest_gl_addr infoLog)
{
    kage_host_get_info_log(length, infoLog, bufSize);
}
static void kage_host_glGetShaderiv(guest_gl_uint shader, guest_gl_enum pname, guest_gl_addr params)
{
    kage_host_get_objectiv(pname, params);
}
static guest_gl_addr kage_host_glGetString(guest_gl_enum name)
{
    return kage_host_get_string(name);
}
static guest_gl_addr kage_host_glGetStringi(guest_gl_enum name, guest_gl_uint index)
{
    return 0u;
}
static guest_gl_int kage_host_glGetUniformLocation(guest_gl_uint program, guest_gl_addr name)
{
    return 0;
}
static void kage_host_glLinkProgram(guest_gl_uint program)
{
    (void)0;
}
static void kage_host_glReadPixels(guest_gl_int x, guest_gl_int y, guest_gl_sizei width, guest_gl_sizei height, guest_gl_enum format, guest_gl_enum type, guest_gl_addr pixels)
{
    kage_host_read_pixels(width, height);
}
static void kage_host_glRenderbufferStorage(guest_gl_enum target, guest_gl_enum internalformat, guest_gl_sizei width, guest_gl_sizei height)
{
    kage_host_rbo_storage(width, height, internalformat);
}
static void kage_host_glShaderSource(guest_gl_uint shader, guest_gl_sizei count, guest_gl_addr string, guest_gl_addr length)
{
    (void)0;
}
static void kage_host_glTexImage2D(guest_gl_enum target, guest_gl_int level, guest_gl_int internalformat, guest_gl_sizei width, guest_gl_sizei height, guest_gl_int border, guest_gl_enum format, guest_gl_enum type, guest_gl_addr pixels)
{
    (void)0;
}
static void kage_host_glTexParameteri(guest_gl_enum target, guest_gl_enum pname, guest_gl_int param)
{
    (void)0;
}
static void kage_host_glTexSubImage2D(guest_gl_enum target, guest_gl_int level, guest_gl_int xoffset, guest_gl_int yoffset, guest_gl_sizei width, guest_gl_sizei height, guest_gl_enum format, guest_gl_enum type, guest_gl_addr pixels)
{
    (void)0;
}
static void kage_host_glUniform1fv(guest_gl_int location, guest_gl_sizei count, guest_gl_addr value)
{
    (void)0;
}
static void kage_host_glUniform1i(guest_gl_int location, guest_gl_int v0)
{
    (void)0;
}
static void kage_host_glUniform1iv(guest_gl_int location, guest_gl_sizei count, guest_gl_addr value)
{
    (void)0;
}
static void kage_host_glUniform1uiv(guest_gl_int location, guest_gl_sizei count, guest_gl_addr value)
{
    (void)0;
}
static void kage_host_glUniform2fv(guest_gl_int location, guest_gl_sizei count, guest_gl_addr value)
{
    (void)0;
}
static void kage_host_glUniform2iv(guest_gl_int location, guest_gl_sizei count, guest_gl_addr value)
{
    (void)0;
}
static void kage_host_glUniform2uiv(guest_gl_int location, guest_gl_sizei count, guest_gl_addr value)
{
    (void)0;
}
static void kage_host_glUniform3fv(guest_gl_int location, guest_gl_sizei count, guest_gl_addr value)
{
    (void)0;
}
static void kage_host_glUniform3iv(guest_gl_int location, guest_gl_sizei count, guest_gl_addr value)
{
    (void)0;
}
static void kage_host_glUniform3uiv(guest_gl_int location, guest_gl_sizei count, guest_gl_addr value)
{
    (void)0;
}
static void kage_host_glUniform4fv(guest_gl_int location, guest_gl_sizei count, guest_gl_addr value)
{
    (void)0;
}
static void kage_host_glUniform4iv(guest_gl_int location, guest_gl_sizei count, guest_gl_addr value)
{
    (void)0;
}
static void kage_host_glUniform4uiv(guest_gl_int location, guest_gl_sizei count, guest_gl_addr value)
{
    (void)0;
}
static void kage_host_glUniformMatrix2fv(guest_gl_int location, guest_gl_sizei count, guest_gl_boolean transpose, guest_gl_addr value)
{
    (void)0;
}
static void kage_host_glUniformMatrix2x3fv(guest_gl_int location, guest_gl_sizei count, guest_gl_boolean transpose, guest_gl_addr value)
{
    (void)0;
}
static void kage_host_glUniformMatrix2x4fv(guest_gl_int location, guest_gl_sizei count, guest_gl_boolean transpose, guest_gl_addr value)
{
    (void)0;
}
static void kage_host_glUniformMatrix3fv(guest_gl_int location, guest_gl_sizei count, guest_gl_boolean transpose, guest_gl_addr value)
{
    (void)0;
}
static void kage_host_glUniformMatrix3x2fv(guest_gl_int location, guest_gl_sizei count, guest_gl_boolean transpose, guest_gl_addr value)
{
    (void)0;
}
static void kage_host_glUniformMatrix3x4fv(guest_gl_int location, guest_gl_sizei count, guest_gl_boolean transpose, guest_gl_addr value)
{
    (void)0;
}
static void kage_host_glUniformMatrix4fv(guest_gl_int location, guest_gl_sizei count, guest_gl_boolean transpose, guest_gl_addr value)
{
    (void)0;
}
static void kage_host_glUniformMatrix4x2fv(guest_gl_int location, guest_gl_sizei count, guest_gl_boolean transpose, guest_gl_addr value)
{
    (void)0;
}
static void kage_host_glUniformMatrix4x3fv(guest_gl_int location, guest_gl_sizei count, guest_gl_boolean transpose, guest_gl_addr value)
{
    (void)0;
}
static void kage_host_glUseProgram(guest_gl_uint program)
{
    (void)0;
}
static void kage_host_glVertexAttribPointer(guest_gl_uint index, guest_gl_int size, guest_gl_enum type, guest_gl_boolean normalized, guest_gl_sizei stride, guest_gl_addr pointer)
{
    (void)0;
}
static void kage_host_glViewport(guest_gl_int x, guest_gl_int y, guest_gl_sizei width, guest_gl_sizei height)
{
    kage_host_viewport(x, y, width, height);
}

static const guest_gl_backend s_kage_host_gl = {
    .glActiveTexture = kage_host_glActiveTexture,
    .glAlphaFunc = kage_host_glAlphaFunc,
    .glAttachShader = kage_host_glAttachShader,
    .glBindFramebuffer = kage_host_glBindFramebuffer,
    .glBindRenderbuffer = kage_host_glBindRenderbuffer,
    .glBindTexture = kage_host_glBindTexture,
    .glBlendFuncSeparate = kage_host_glBlendFuncSeparate,
    .glCheckFramebufferStatus = kage_host_glCheckFramebufferStatus,
    .glClampColorARB = kage_host_glClampColorARB,
    .glClear = kage_host_glClear,
    .glClearColor = kage_host_glClearColor,
    .glClearDepth = kage_host_glClearDepth,
    .glCompileShader = kage_host_glCompileShader,
    .glCreateProgram = kage_host_glCreateProgram,
    .glCreateShader = kage_host_glCreateShader,
    .glCullFace = kage_host_glCullFace,
    .glDeleteFramebuffers = kage_host_glDeleteFramebuffers,
    .glDeleteProgram = kage_host_glDeleteProgram,
    .glDeleteRenderbuffers = kage_host_glDeleteRenderbuffers,
    .glDeleteShader = kage_host_glDeleteShader,
    .glDeleteTextures = kage_host_glDeleteTextures,
    .glDepthFunc = kage_host_glDepthFunc,
    .glDisableVertexAttribArray = kage_host_glDisableVertexAttribArray,
    .glDrawElements = kage_host_glDrawElements,
    .glEnable = kage_host_glEnable,
    .glEnableVertexAttribArray = kage_host_glEnableVertexAttribArray,
    .glFramebufferRenderbuffer = kage_host_glFramebufferRenderbuffer,
    .glFramebufferTexture2D = kage_host_glFramebufferTexture2D,
    .glGenFramebuffers = kage_host_glGenFramebuffers,
    .glGenRenderbuffers = kage_host_glGenRenderbuffers,
    .glGenTextures = kage_host_glGenTextures,
    .glGetAttribLocation = kage_host_glGetAttribLocation,
    .glGetIntegerv = kage_host_glGetIntegerv,
    .glGetProgramInfoLog = kage_host_glGetProgramInfoLog,
    .glGetProgramiv = kage_host_glGetProgramiv,
    .glGetRenderbufferParameteriv = kage_host_glGetRenderbufferParameteriv,
    .glGetShaderInfoLog = kage_host_glGetShaderInfoLog,
    .glGetShaderiv = kage_host_glGetShaderiv,
    .glGetString = kage_host_glGetString,
    .glGetStringi = kage_host_glGetStringi,
    .glGetUniformLocation = kage_host_glGetUniformLocation,
    .glLinkProgram = kage_host_glLinkProgram,
    .glReadPixels = kage_host_glReadPixels,
    .glRenderbufferStorage = kage_host_glRenderbufferStorage,
    .glShaderSource = kage_host_glShaderSource,
    .glTexImage2D = kage_host_glTexImage2D,
    .glTexParameteri = kage_host_glTexParameteri,
    .glTexSubImage2D = kage_host_glTexSubImage2D,
    .glUniform1fv = kage_host_glUniform1fv,
    .glUniform1i = kage_host_glUniform1i,
    .glUniform1iv = kage_host_glUniform1iv,
    .glUniform1uiv = kage_host_glUniform1uiv,
    .glUniform2fv = kage_host_glUniform2fv,
    .glUniform2iv = kage_host_glUniform2iv,
    .glUniform2uiv = kage_host_glUniform2uiv,
    .glUniform3fv = kage_host_glUniform3fv,
    .glUniform3iv = kage_host_glUniform3iv,
    .glUniform3uiv = kage_host_glUniform3uiv,
    .glUniform4fv = kage_host_glUniform4fv,
    .glUniform4iv = kage_host_glUniform4iv,
    .glUniform4uiv = kage_host_glUniform4uiv,
    .glUniformMatrix2fv = kage_host_glUniformMatrix2fv,
    .glUniformMatrix2x3fv = kage_host_glUniformMatrix2x3fv,
    .glUniformMatrix2x4fv = kage_host_glUniformMatrix2x4fv,
    .glUniformMatrix3fv = kage_host_glUniformMatrix3fv,
    .glUniformMatrix3x2fv = kage_host_glUniformMatrix3x2fv,
    .glUniformMatrix3x4fv = kage_host_glUniformMatrix3x4fv,
    .glUniformMatrix4fv = kage_host_glUniformMatrix4fv,
    .glUniformMatrix4x2fv = kage_host_glUniformMatrix4x2fv,
    .glUniformMatrix4x3fv = kage_host_glUniformMatrix4x3fv,
    .glUseProgram = kage_host_glUseProgram,
    .glVertexAttribPointer = kage_host_glVertexAttribPointer,
    .glViewport = kage_host_glViewport,
};

static int kage_gl_install(void) {
    if (s_gl_installed) {
        return 1;
    }
    if (gl_ohos_backend != NULL) {
        s_gl = gl_ohos_backend();
        if (s_gl == NULL) {
            s_error = "KAGE GL backend: gl_ohos_backend() returned NULL";
            return 0;
        }
        KLOG("GL backend installed: %{public}u typed commands (gl_ohos_backend.c)",
             (unsigned)GUEST_GL_SURFACE_COUNT);
    } else {
        s_gl = &s_kage_host_gl;
        KLOG("GL backend: gl_ohos_backend.c not linked (POSIX harness) -> host "
             "simulation table for %{public}u commands; no GL call reaches a driver",
             (unsigned)GUEST_GL_SURFACE_COUNT);
    }
    s_gl_installed = 1;
    return 1;
}

static void kage_gl_uninstall(void) {
    s_gl = NULL;
    s_gl_installed = 0;
}

static int kage_gl_lookup(const char *name) {
    unsigned i;
    for (i = 0; i < KAGE_GL_ENTRY_COUNT; i++) {
        if (strcmp(s_kage_gl[i].name, name) == 0) {
            return (int)i;
        }
    }
    return -1;
}

/* Bounded copy of a canonical entry-point name out of the flat arena. */
static void kage_gl_read_name(uint32_t address, char *out) {
    uint32_t i;
    for (i = 0; i < KAGE_GL_NAME_MAX; i++) {
        uint8_t ch = ld8(address + i);
        out[i] = (char)ch;
        if (ch == 0) {
            break;
        }
    }
    out[i] = '\0';
}

/* The single resolver for both entry points the guest uses to reach GL:
 * KAGE's own loader (0x005700e0) and OPENGL32!wglGetProcAddress.  A name in the
 * frozen surface gets its adapter's token; any other GL/wgl entry point gets the
 * trailing "unresolved" entry so a later call is attributed with its exact
 * symbol instead of jumping to NULL out of a guest slot; a non-GL name is
 * reported absent, which is what a loader must do. */
static uint32_t kage_gl_token_for_name(const char *name) {
    int index = kage_gl_lookup(name);

    if (index >= 0) {
        return KAGE_GL_TOKEN_BASE | (uint32_t)index;
    }
    if ((name[0] == 'g' && name[1] == 'l') ||
        (name[0] == 'w' && name[1] == 'g' && name[2] == 'l')) {
        if (strcmp(s_unresolved_name, name) != 0) {
            snprintf(s_unresolved_name, sizeof s_unresolved_name, "%s", name);
            KERR("GL loader: '%{public}s' is not in the 73-command surface", name);
        }
        return KAGE_GL_TOKEN_BASE | (uint32_t)KAGE_GL_UNKNOWN_INDEX;
    }
    if (s_kage_absent_logged < 16u) {
        s_kage_absent_logged++;
        KLOG("GL loader: '%{public}s' -> absent (not a GL entry point)", name);
    }
    return 0u;
}

/* KAGE's GL entry-point loader (0x005700e0).  Fastcall success ABI: ECX is the
 * canonical guest name, EAX receives the token, and the plain RET consumes only
 * the guest return word (the wrapper pops its own pushed word).
 *
 * The pushed word and EDX are loader bookkeeping (per-name type/table records)
 * that a table-driven boundary does not need; only the name selects a command. */
void sub_005700e0(CPU *__restrict c) {
    char name[KAGE_GL_NAME_MAX + 1u];

    kage_gl_read_name(c->ecx, name);
    c->eax = kage_gl_token_for_name(name);
    (void)gpop_generated(c);
}

/* OPENGL32!wglGetProcAddress.  host_ohos.c's placeholder hands out an
 * unregistered counter token (0x7d100000, module index 16, which no provider
 * owns) that the guest then jumps to, so the first extension-style GL lookup
 * faults far from its cause.  host_ext_lookup_import() consults extension
 * modules before the generated table, so this module serves the name through
 * the same resolver and token space as 0x005700e0. */
static void h_kage_wglGetProcAddress(CPU *c) {
    char name[KAGE_GL_NAME_MAX + 1u];
    uint32_t name_addr = ld32(c->esp + 4u);

    kage_gl_read_name(name_addr, name);
    c->eax = kage_gl_token_for_name(name);
    if (c->eax == 0u) {
        KLOG("wglGetProcAddress(\"%{public}s\") -> absent", name);
    } else if (s_kage_wgl_logged < 8u) {
        s_kage_wgl_logged++;
        KLOG("wglGetProcAddress(\"%{public}s\") -> token 0x%{public}08x", name, c->eax);
    }
    kage_stdcall_ret(c, 4u);   /* APIENTRY: one argument word */
}

/* --------------------------------------------------- seam helpers ------- */
static int kage_require_pc_mode(CPU *c, uint32_t rva) {
    if (kage_pc_backend_mode() != KAGE_PC_BACKEND_DISABLED) {
        return 1;
    }
    guest_fault(c, rva, "KAGE graphics backend unavailable");
    return 0;
}

static int kage_manager_dimensions(CPU *c, uint32_t rva,
                                   uint32_t *width, uint32_t *height) {
    if (c->ecx != KAGE_MANAGER_ADDRESS) {
        KERR("KAGE seam: unexpected manager 0x%{public}08x at 0x%{public}08x",
             c->ecx, rva);
        guest_fault(c, rva, "KAGE PC backend: unexpected manager address");
        return 0;
    }
    *width = ld32(c->ecx + KAGE_WIDTH_OFFSET);
    *height = ld32(c->ecx + KAGE_HEIGHT_OFFSET);
    if (!*width) {
        *width = KAGE_DEFAULT_WIDTH;
    }
    if (!*height) {
        *height = KAGE_DEFAULT_HEIGHT;
    }
    return 1;
}

/* Step 7 of the bring-up: the game-owned allocator owns the depth renderbuffer,
 * the attachment and the viewport.  It must run with the manager FBO bound. */
static int kage_call_render_target_allocator(CPU *c, uint32_t rva,
                                             uint32_t manager) {
    uint32_t saved = c->esp;

    /* The allocator takes no stack arguments -- it reads ECX as `this` and
     * immediately loads its working extents from manager+0xd0 (disassembly at
     * RVA 0x56503b: `mov esi, [ebx+0xd0]`). If those fields are still their
     * -1 initialiser the out-address it derives is 0, and glGenRenderbuffers
     * is called with a NULL pointer. Log them so the culprit is unambiguous. */
    KLOG("allocator entry: manager+0xd0=0x%{public}08x +0xd4=0x%{public}08x +0xe0=0x%{public}u +0xe4=0x%{public}u",
         ld32(manager + 0xd0U), ld32(manager + 0xd4U),
         ld32(manager + 0xe0U), ld32(manager + 0xe4U));

    /* EXPERIMENT: 0xffffffff is the initialiser these two fields carry until
     * the game's own setter (guest_0143.c:18479 / :18894) runs, and the
     * allocator derives a NULL out-address from it. Clear the sentinel so the
     * allocator sees a defined value, and log what it does. */
    if (ld32(manager + 0xd0U) == 0xffffffffU) {
        st32(manager + 0xd0U, 0u);
        st32(manager + 0xd4U, 0u);
        KLOG("allocator entry: cleared the -1 sentinels on +0xd0/+0xd4");
    }

    /* ABI settled from the translation (guest_0166.c:16244..16529): stdcall
     * `ret 4` with ECX = this. The translated ret is GPOP()+ADJ(4): it pops
     * the slot it believes is the return address and then drops one argument
     * word, so the caller must push a fake return address FIRST and the real
     * stack argument second -- exactly like every generated call site
     * (GPUSH(0x561610) before sub_00565030 there). Skipping the fake return
     * address made ret eat the argument and leave esp 4 high -> the ABI-drift
     * fault, with [esp] showing the out value the function wrote to what it
     * thought was its first argument. */
    gpush_generated(c, 0x565091u);   /* fake return address (any RVA works) */
    gpush_generated(c, 1u);          /* force: the one stack argument */
    /* The device run read [ebp+4]/[ebp+8] back as 0 while the PC harness saw
     * the pushed words, so verify the pushes actually landed before entering
     * the translated allocator. */
    KLOG("allocator pre-call: esp=0x%{public}08x [esp]=0x%{public}08x "
         "[esp+4]=0x%{public}08x manager=0x%{public}08x",
         c->esp, ld32(c->esp), ld32(c->esp + 4U), manager);
    c->ecx = manager;
    sub_00565030(c);
    KLOG("allocator post-call: manager+0xcc=0x%{public}08x esp=0x%{public}08x",
         ld32(manager + 0xccU), c->esp);
    if (c->fault) {
        return 0;
    }
    if (c->esp != saved) {
        c->esp = saved;
        guest_fault(c, rva, "KAGE PC backend: render-target allocator ABI drift");
        return 0;
    }
    return 1;
}

static void kage_zero_render_targets(uint32_t manager) {
    st32(manager + KAGE_FRAMEBUFFER_OFFSET, 0u);
    st32(manager + KAGE_RENDERBUFFER_OFFSET, 0u);
    st32(manager + KAGE_BACKEND_OFFSET, 0u);
}

/* Steps 6 and 8: create and bind the manager render target through the
 * installed table, then run the guest allocator and leave FBO 0 bound.
 * Reports failure through s_error without faulting: the caller tears the seam
 * down first, because guest_fault longjmps and never returns here. */
static int kage_initialize_render_targets(CPU *c, uint32_t rva,
                                          uint32_t manager) {
    uint32_t fb_addr = manager + KAGE_FRAMEBUFFER_OFFSET;
    uint32_t rb_addr = manager + KAGE_RENDERBUFFER_OFFSET;

    (void)rva;
    if (s_gl->glBindFramebuffer == NULL || s_gl->glGenFramebuffers == NULL) {
        s_error = "KAGE GL backend: framebuffer commands missing from the table";
        return 0;
    }
    if (ld32(fb_addr) != 0u) {
        s_gl->glBindFramebuffer(0x8d40u, 0u);
        if (s_gl->glDeleteFramebuffers != NULL) {
            s_gl->glDeleteFramebuffers(1, fb_addr);
        }
        st32(fb_addr, 0u);
    }
    s_gl->glGenFramebuffers(1, fb_addr);
    if (ld32(fb_addr) == 0u) {
        s_error = "KAGE GL backend: glGenFramebuffers returned zero";
        return 0;
    }
    s_gl->glBindFramebuffer(0x8d40u, ld32(fb_addr));
    KLOG("render target: framebuffer=%{public}u bound (manager+0xc8)",
         ld32(fb_addr));

    if (!kage_call_render_target_allocator(c, rva, manager)) {
        return 0;
    }
    if (ld32(rb_addr) == 0u) {
        s_error = "KAGE GL backend: renderbuffer allocator returned zero";
        KERR("render target: manager+0xcc is still 0 after sub_00565030");
        return 0;
    }
    s_gl->glBindFramebuffer(0x8d40u, 0u);   /* step 8: default target bound */
    KLOG("render target: depth renderbuffer=%{public}u, unframebuffer left bound",
         ld32(rb_addr));
    return 1;
}

/* Steps 1..9, shared by Initialize (0x560c60) and InitializeRenderDisplay
 * (0x561830) exactly as upstream's manual_kage.c does. */
static int kage_initialize_pc(CPU *c, uint32_t rva, const char *disabled_fault) {
    uint32_t manager = c->ecx;
    uint32_t width, height;

    (void)disabled_fault;
    if (!kage_require_pc_mode(c, rva)) {
        return 0;
    }
    if (!kage_manager_dimensions(c, rva, &width, &height)) {
        return 0;
    }
    KLOG("KAGE seam 0x%{public}08x: manager=0x%{public}08x %{public}ux%{public}u",
         rva, manager, width, height);

    if (!kage_pc_backend_initialize(width, height)) {
        guest_fault(c, rva, s_error);
        return 0;
    }
    if (!kage_gl_install()) {
        const char *error = s_error;
        kage_gl_uninstall();
        kage_pc_backend_shutdown();
        guest_fault(c, rva, error);
        return 0;
    }
    /* There is intentionally no fake GLFW object at +d8, and this must be in
     * place before guest_kage_pc_handoff_ready() can report success: the
     * generated caller dereferences +d8 unless the handoff bypasses it. */
    st32(manager + KAGE_BACKEND_OFFSET, 0u);
    if (!kage_initialize_render_targets(c, rva, manager)) {
        const char *error = s_error;
        /* Tear the partial seam down before crossing the fault boundary. */
        kage_zero_render_targets(manager);
        kage_gl_uninstall();
        kage_pc_backend_shutdown();
        guest_fault(c, rva, error);
        return 0;
    }
    s_ready = 1;
    c->eax = (c->eax & 0xffffff00u) | 1u;   /* bool true in AL */
    (void)gpop_generated(c);                          /* exact MSVC thiscall RET */
    return 1;
}

/* ------------------------------------------------------------- leaves --- */
void sub_00560c60(CPU *__restrict c) {
    (void)kage_initialize_pc(c, KAGE_RVA_INITIALIZE,
                             "KAGE graphics backend unavailable: Initialize");
}

void sub_00561830(CPU *__restrict c) {
    (void)kage_initialize_pc(c, KAGE_RVA_INITIALIZE_RENDER,
                             "KAGE graphics backend unavailable: InitializeRenderDisplay");
}

void sub_00560e30(CPU *__restrict c) {
    uint32_t manager = c->ecx;

    if (manager == KAGE_MANAGER_ADDRESS && s_gl_installed) {
        uint32_t fb_addr = manager + KAGE_FRAMEBUFFER_OFFSET;
        uint32_t rb_addr = manager + KAGE_RENDERBUFFER_OFFSET;
        if (s_gl->glBindFramebuffer != NULL) {
            s_gl->glBindFramebuffer(0x8d40u, 0u);
        }
        if (ld32(fb_addr) != 0u && s_gl->glDeleteFramebuffers != NULL) {
            s_gl->glDeleteFramebuffers(1, fb_addr);
        }
        if (ld32(rb_addr) != 0u && s_gl->glDeleteRenderbuffers != NULL) {
            s_gl->glDeleteRenderbuffers(1, rb_addr);
        }
        st32(manager + KAGE_FRAMEBUFFER_OFFSET, 0u);
        st32(manager + KAGE_RENDERBUFFER_OFFSET, 0u);
        st32(manager + KAGE_BACKEND_OFFSET, 0u);
    }
    kage_gl_uninstall();
    kage_pc_backend_shutdown();
    (void)gpop_generated(c);
}

void sub_00560eb0(CPU *__restrict c) {
    /* The translated leaf already carries the PC present seam: it calls
     * kage_pc_backend_ready()/present() instead of the GLFW swap. */
    guest_original_00560eb0(c);
}

void sub_00560f20(CPU *__restrict c) {
    /* The manager's own getter is reached *from* InitializeRenderDisplay (the
     * render-target allocator asks for the surface size), so the gate is
     * "initialized", not "handoff complete". */
    if (!s_initialized) {
        guest_fault(c, KAGE_RVA_GET_FB_WIDTH,
                    "KAGE PC backend: width requested before initialize");
        return;
    }
    st32(KAGE_HEIGHT_COMPANION, kage_pc_backend_height());
    st32(KAGE_WIDTH_CACHE, kage_pc_backend_width());
    c->eax = kage_pc_backend_width();
    (void)gpop_generated(c);
}

void sub_00560f90(CPU *__restrict c) {
    if (!s_initialized) {
        guest_fault(c, KAGE_RVA_GET_FB_HEIGHT,
                    "KAGE PC backend: height requested before initialize");
        return;
    }
    st32(KAGE_WIDTH_COMPANION, kage_pc_backend_width());
    st32(KAGE_HEIGHT_CACHE, kage_pc_backend_height());
    c->eax = kage_pc_backend_height();
    (void)gpop_generated(c);
}

/* ------------------------------------------------------ audio boundary --- */
/* KAGE::Sound::Manager::Initialize (0x0056dd70) is the audio half of the same
 * platform seam: the manager's caller ignores AL, and the built-in
 * alcOpenDevice(NULL) failure path leaves the manager uninitialised and lets
 * startup continue.  Reproducing that exact externally visible state costs one
 * seam instead of 24 OpenAL import shims, and keeps the audio system on its
 * own "unavailable" path rather than fabricating device handles. */
#define KAGE_SOUND_MANAGER_ADDRESS 0x307e7d40u
#define KAGE_SOUND_RUNNING         0x00000004u
#define KAGE_SOUND_SUSPENDED       0x00000005u
#define KAGE_SOUND_CONTEXT         0x00000030u
#define KAGE_SOUND_DEVICE          0x00000034u

void sub_0056dd70(CPU *__restrict c) {
    if (!kage_require_pc_mode(c, 0x0056dd70u)) {
        return;
    }
    if (c->ecx != KAGE_SOUND_MANAGER_ADDRESS) {
        KERR("KAGE audio seam: unexpected manager 0x%{public}08x", c->ecx);
        guest_fault(c, 0x0056dd70u,
                    "KAGE PC audio boundary: unexpected manager address");
        return;
    }
    st8(c->ecx + KAGE_SOUND_RUNNING, 0u);
    st8(c->ecx + KAGE_SOUND_SUSPENDED, 0u);
    st32(c->ecx + KAGE_SOUND_CONTEXT, 0u);
    st32(c->ecx + KAGE_SOUND_DEVICE, 0u);
    c->eax &= 0xffffff00u;          /* bool false in AL */
    (void)gpop_generated(c);        /* exact MSVC thiscall RET */
    KLOG("KAGE audio seam 0x0056dd70: sound device unavailable (upstream state)");
}

/* ------------------------------------------------- backend lifecycle ---- */
int kage_pc_backend_initialize(uint32_t width, uint32_t height) {
    /* The GL context is already current on this (render) thread; nothing to
     * create.  Record the surface the manager asked for. */
    s_width = width ? width : KAGE_DEFAULT_WIDTH;
    s_height = height ? height : KAGE_DEFAULT_HEIGHT;
    s_present_count = 0u;
    s_ready = 0;
    s_initialized = 1;
    s_error = "";
    KLOG("KAGE GL backend initialize: %{public}ux%{public}u (context already current)",
         s_width, s_height);
    return 1;
}

void kage_pc_backend_shutdown(void) {
    s_ready = 0;
    s_initialized = 0;
    s_error = "KAGE GL backend: shut down";
}

int kage_pc_backend_present(void) {
    KageEGLDisplay dpy;
    KageEGLSurface surf;

    if (!s_ready) {
        s_error = "KAGE GL backend: present before initialize";
        return 0;
    }
    if (eglSwapBuffers == NULL || eglGetCurrentDisplay == NULL ||
        eglGetCurrentSurface == NULL) {
        /* Host harness: no EGL to swap.  Advance the state loudly instead of
         * failing the guest on a platform fact it cannot fix. */
        s_present_count++;
        if (s_present_count == 1u) {
            KLOG("present: no libEGL in this build (POSIX harness) -> swap counted, not performed");
        }
        return 1;
    }
    dpy = eglGetCurrentDisplay();
    surf = eglGetCurrentSurface(KAGE_EGL_DRAW);
    if (dpy == NULL || surf == NULL) {
        s_error = "KAGE GL backend: no current EGL display/surface on the render thread";
        return 0;
    }
    if (!eglSwapBuffers(dpy, surf)) {
        s_error = "KAGE GL backend: eglSwapBuffers failed";
        return 0;
    }
    s_present_count++;
    /* First few presents name the surface: if these never appear, the guest
     * never reached its render loop and the black screen is upstream of the
     * swap, not in it. */
    if (s_present_count <= 3u) {
        KLOG("present #%{public}u: dpy=0x%{public}08x surf=0x%{public}08x OK",
             s_present_count, (unsigned)(uintptr_t)dpy, (unsigned)(uintptr_t)surf);
    }
    return 1;
}

/* Single commit point for the generated `if (guest_kage_pc_handoff_ready())`
 * bypasses (guest.h declares it; upstream defines it in manual_kage.c).  While
 * this is 0 the guest takes the original Windows/GLFW path and dereferences the
 * manager's NULL +0xd8 word. */
int guest_kage_pc_handoff_ready(void) {
    return kage_pc_backend_mode() != KAGE_PC_BACKEND_DISABLED &&
           kage_pc_backend_ready();
}
