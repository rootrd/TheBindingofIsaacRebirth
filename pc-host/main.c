/* Host harness: runs the recompiled guest on a plain POSIX machine, with no
 * HarmonyOS device involved. Same runtime sources the HAP uses (guest.c,
 * host_ohos.c, ...) so a bug reproduced here is a guest/host-layer bug, not a
 * platform one -- which is exactly what the current import/CRT work needs.
 * Graphics, audio and input still require the device. */
#include <stdio.h>
#include <stdlib.h>

extern int isaac_guest_init(const char *exe_path);
extern int isaac_guest_run(void);
extern void isaac_guest_shutdown(void);

int main(int argc, char **argv) {
    const char *exe = argc > 1 ? argv[1]
                               : "isaac-ng.exe";
    setvbuf(stdout, NULL, _IOLBF, 0);
    setvbuf(stderr, NULL, _IOLBF, 0);

    if (isaac_guest_init(exe) != 0) {
        fprintf(stderr, "isaac_guest_init FAILED for %s\n", exe);
        return 2;
    }
    int rc = isaac_guest_run();
    fprintf(stderr, "isaac_guest_run returned %d\n", rc);
    isaac_guest_shutdown();
    return rc;
}
