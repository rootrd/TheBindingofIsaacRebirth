#!/bin/bash
# Same as run_demo.sh, plus: instrument the COPY of host_ohos.c so the
# non-CRT file APIs the guest uses are visible (h_CreateFileA/h_GetFileAttributesA
# currently log nothing and do not map paths into ISAAC_DATA_ROOT). Diagnostic
# only - the project files are untouched.
# Usage: bash pc-host/scratch/run_demo_probe.sh [seconds]
set -e
PROJ=/mnt/f/TheBindingofIsaacRebirth
SRC=$PROJ/isaac-ohos/entry/src/main/cpp/isaac
SHIM=$PROJ/pc-host/shim
OUT=$PROJ/pc-host/build
DEMO=/tmp/hf
SECS=${1:-60}
CFLAGS="-O1 -g -I$SHIM -I$SRC -I$SRC/generated"

mkdir -p "$DEMO/obj"
cp "$SRC/host_ohos.c" "$DEMO/host_ohos.c"
cp "$SRC/host_ext.h"  "$DEMO/host_ext.h"
python3 - "$DEMO" <<'PY'
import sys, pathlib
d = pathlib.Path(sys.argv[1])

def patch(name, old, new, count=1):
    p = d / name
    s = p.read_text()
    assert s.count(old) == count, (name, s.count(old), old[:60])
    p.write_text(s.replace(old, new))

patch("host_ohos.c",
      "__attribute__((weak)) const host_ext_module *host_ext_misc(void) { return NULL; }",
      "__attribute__((weak)) const host_ext_module *host_ext_misc(void) { return NULL; }\n"
      "__attribute__((weak)) const host_ext_module *host_ext_file(void) { return NULL; }")
patch("host_ohos.c", "    host_ext_misc,\n};", "    host_ext_misc,\n    host_ext_file,\n};")
patch("host_ext.h", "const host_ext_module *host_ext_misc(void);",
      "const host_ext_module *host_ext_misc(void);\nconst host_ext_module *host_ext_file(void);")

# PROBE: report what the KERNEL32 file APIs are asked for.
patch("host_ohos.c",
      """    int fd = open(path, posix_flags, 0644);
    if (fd < 0) {
        c->eax = 0xFFFFFFFFU;
        c->last_error = (uint32_t)errno;
        host_stdcall_ret(c, 7);
        return;
    }""",
      """    int fd = open(path, posix_flags, 0644);
    OH_LOG_Print(LOG_APP, LOG_ERROR, 0, "isaacoh",
                 "PROBE CreateFileA(\\"%s\\") creation=%u -> %s (errno=%d)",
                 path, creation, fd < 0 ? "FAIL" : "ok", errno);
    if (fd < 0) {
        c->eax = 0xFFFFFFFFU;
        c->last_error = (uint32_t)errno;
        host_stdcall_ret(c, 7);
        return;
    }""")
print("  patched: provider list + CreateFileA probe")
PY

gcc $CFLAGS -c "$DEMO/host_ohos.c"    -o "$DEMO/obj/host_ohos.o"
gcc $CFLAGS -c "$SRC/host_ext_file.c" -o "$DEMO/obj/host_ext_file.o"

OBJS=$(ls "$OUT"/obj/*.o | grep -vE '/(host_ohos|host_ext_file)\.o$' | tr '\n' ' ')
gcc -g -o "$DEMO/isaac_host_demo" "$PROJ/pc-host/main.c" $OBJS "$DEMO/obj/host_ohos.o" \
    "$DEMO/obj/host_ext_file.o" -lpthread -lm -ldl

cd "$DEMO"
export ISAAC_DATA_ROOT=${ISAAC_DATA_ROOT:-$OUT/isaac-data}
timeout "$SECS" ./isaac_host_demo "$PROJ/isaac-ohos/entry/src/main/resources/rawfile/isaac-ng.exe" 2>&1
