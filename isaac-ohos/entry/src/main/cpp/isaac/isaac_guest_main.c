/* Guest initialization and main execution loop for the HarmonyOS port.
 * Called from the render thread after the graphics backend is initialized. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <hilog/log.h>

#include "guest.h"
#include "guest_memory.h"

#define GLOG(fmt, ...) OH_LOG_Print(LOG_APP, LOG_INFO, 0, "isaacoh", fmt, ##__VA_ARGS__)
#define GERR(fmt, ...) OH_LOG_Print(LOG_APP, LOG_ERROR, 0, "isaacoh", fmt, ##__VA_ARGS__)

extern void guest_register_all(void);
extern void guest_register_all_imports(void);
extern guest_fn guest_entry_point(void);
extern void host_ohos_init(void);

static CPU g_cpu;
static int g_guest_initialized;

int isaac_guest_init(const char *exe_path)
{
    if (g_guest_initialized) {
        GERR("isaac_guest_init: already initialized");
        return 1;
    }

    host_ohos_init();

    if (guest_mem_init() != 0) {
        GERR("isaac_guest_init: failed to init arena");
        return 1;
    }

    if (guest_image_load(exe_path) != 0) {
        FILE *tf = fopen(exe_path, "rb");
        if (!tf) {
            GERR("isaac_guest_init: cannot open exe file %{public}s", exe_path);
        } else {
            fseek(tf, 0, SEEK_END);
            long sz = ftell(tf);
            fclose(tf);
            GERR("isaac_guest_init: PE load failed, exe=%{public}s size=%ld", exe_path, sz);
        }
        return 1;
    }

    guest_register_all();
    guest_register_all_imports();

    guest_cpu_init(&g_cpu);

    if (guest_stack_init(&g_cpu) != 0) {
        GERR("isaac_guest_init: failed to init stack");
        return 1;
    }

    guest_tls_process_attach(&g_cpu);

    g_guest_initialized = 1;
    GLOG("isaac_guest_init: OK");
    return 0;
}

int isaac_guest_run(void)
{
    if (!g_guest_initialized) {
        GERR("isaac_guest_run: not initialized");
        return -1;
    }

    guest_fn entry = guest_entry_point();
    if (!entry) {
        GERR("isaac_guest_run: no entry point");
        return -1;
    }

    /* Fault-resilient restart: an unhandled MSVC C++ exception (throw with
     * no full unwinding support) or a data-dependent fault stops the guest,
     * but re-entering WinMain lets the rest of the engine keep running.
     * Capped so a deterministic fault cannot spin forever. */
    int restarts = 0;
    int result = GUEST_RUN_FAULT;

    for (;;) {
        GLOG("isaac_guest_run: calling entry point (restart %d)", restarts);
        result = guest_run_until_stop(&g_cpu, entry);

        if (result != GUEST_RUN_FAULT || restarts >= 8) {
            break;
        }
        restarts++;
        GERR("isaac_guest_run: fault swallowed at 0x%{public}08x (%{public}s), "
             "restarting %d/8", g_cpu.fault_addr,
             g_cpu.fault ? g_cpu.fault : "?", restarts);
    }

    switch (result) {
    case GUEST_RUN_RETURNED:
        GLOG("isaac_guest_run: guest returned normally");
        break;
    case GUEST_RUN_FAULT:
        GERR("isaac_guest_run: guest faulted at 0x%{public}08x: %{public}s",
               g_cpu.fault_addr, g_cpu.fault ? g_cpu.fault : "?");
        break;
    case GUEST_RUN_EXIT:
        GLOG("isaac_guest_run: guest exited with code %d via %{public}s",
               g_cpu.exit_code, g_cpu.exit_api ? g_cpu.exit_api : "?");
        break;
    default:
        GERR("isaac_guest_run: unknown stop kind %d", result);
        break;
    }

    return result;
}

void isaac_guest_shutdown(void)
{
    guest_stack_free(&g_cpu);
    guest_image_free();
    guest_mem_shutdown();
    g_guest_initialized = 0;
}
