/* Stub implementations for kage_pc_backend hooks and other missing symbols.
 * These are called by the generated guest code but are not needed for
 * the initial HarmonyOS port. All functions are no-ops. */
#include "guest.h"
#include <time.h>
#include <string.h>

/* kage_pc_backend stubs — signatures from kage_pc_backend.h */
void kage_pc_backend_note_render_entry(void) {}
void kage_pc_backend_note_render_return(void) {}
void kage_pc_backend_note_limiter_entry(void) {}
void kage_pc_backend_note_limiter_exit(void) {}
void kage_pc_backend_note_loop_head(void) {}
void kage_pc_backend_note_service_entry(void) {}
void kage_pc_backend_note_update_entry(void) {}
void kage_pc_backend_note_menu_init(uint32_t site) { (void)site; }
void kage_pc_backend_note_menu_render(uint32_t site) { (void)site; }
void kage_pc_backend_note_menu_character(uint32_t site) { (void)site; }
void kage_pc_backend_note_menu_character_wheel_snapshot(
    uint32_t this_ptr, uint32_t character_count,
    uint32_t scratch_begin, uint32_t scratch_end,
    uint32_t scratch_capacity, uint32_t guard) {
    (void)this_ptr; (void)character_count;
    (void)scratch_begin; (void)scratch_end;
    (void)scratch_capacity; (void)guard;
}
void kage_pc_backend_keyboard_snapshot(uint8_t keys[349]) {
    memset(keys, 0, 349);
}
void kage_pc_backend_audio_cooperative_poll(CPU *cpu) { (void)cpu; }
int  kage_pc_backend_console_trace_enabled(void) { return 0; }
int  kage_pc_backend_note_console_command(uint32_t guest_stack) { (void)guest_stack; return 0; }
int  kage_pc_backend_set_mode(int mode) { (void)mode; return 0; }
/* kage_pc_backend_mode/ready/initialize/shutdown/present/width/height/
 * last_error/present_count live in host_kage.c: the KAGE platform seam owns
 * them, and the generated PC present/handoff seams read them from there. */
const char *kage_pc_backend_gl_version(void) { return ""; }
unsigned kage_pc_backend_clear_count(void) { return 0; }
int  kage_pc_backend_accelerated(void) { return 0; }
void kage_pc_backend_probe_rgba(uint8_t rgba[4]) { memset(rgba, 0, 4); }
uintptr_t kage_pc_backend_gl_proc(const char *name) { (void)name; return 0; }
int  kage_pc_backend_time_seconds(double *seconds) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    *seconds = (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
    return 1;
}
/* kage_pc_backend_set_vsync / vsync_enabled also live in host_kage.c: the swap
 * interval belongs to the same present seam as the EGL swap. */
uint32_t kage_pc_backend_refresh_rate(void) { return 60; }
unsigned kage_pc_backend_vsync_change_count(void) { return 0; }
void kage_pc_backend_note_guest_gl_call(const char *name) { (void)name; }
int  kage_pc_backend_virtual_clipboard_open(uintptr_t owner, uint32_t *result) { (void)owner; (void)result; return 0; }
int  kage_pc_backend_virtual_clipboard_get_data(uint32_t format, uintptr_t *result) { (void)format; (void)result; return 0; }
int  kage_pc_backend_virtual_global_lock(uintptr_t handle, uintptr_t *result) { (void)handle; (void)result; return 0; }
int  kage_pc_backend_virtual_global_unlock(uintptr_t handle, uint32_t *result) { (void)handle; (void)result; return 0; }
int  kage_pc_backend_virtual_clipboard_close(uint32_t *result) { (void)result; return 0; }

/* guest_stack_address — used by some generated code with GUEST_STACK_REQUIRED=1.
 * Our port uses GUEST_STACK_REQUIRED=0, but some code may still reference it. */
uint32_t guest_stack_address(CPU *__restrict c, uint32_t address, uint32_t size) {
    (void)c; (void)size;
    return address;
}

/* Missing sub_ functions — referenced by generated code but not defined in
 * any guest_XXXX.c file. They fault if called at runtime. Fix one by
 * translating it with recomp/build_one.py and checking the output in as
 * generated/guest_extra_<rva>.c, then deleting the stub here. */
__attribute__((unused))
static void missing_func_fault(CPU *c, const char *name) {
    guest_fault(c, 0, name);
}

/* sub_00560c60 (Graphics::Manager::Initialize), sub_00561830
 * (InitializeRenderDisplay), sub_00560e30 (Shutdown), sub_00560eb0 (Present),
 * sub_00560f20/sub_00560f90 (framebuffer size), sub_0056dd70 (Sound::Initialize)
 * and sub_005700e0 (GL entry-point loader) live in host_kage.c: that file is
 * the KAGE platform seam and owns the GL backend the guest resolves into. */
