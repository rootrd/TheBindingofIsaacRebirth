#!/bin/bash
# Demo: the module REGISTERED (in a copy of host_ohos.c/host_ext.h) against a
# data root that contains the file the task targets, kage_mount_points.dat.
# Nothing is inherited from the caller's environment: every path is literal
# (the Git Bash wrapper eats $VAR in commands passed to wsl.exe).
set -e
PROJ=/mnt/f/TheBindingofIsaacRebirth
SRC=$PROJ/isaac-ohos/entry/src/main/cpp/isaac
SHIM=$PROJ/pc-host/shim
OUT=$PROJ/pc-host/build
SCRATCH=$PROJ/pc-host/scratch
DEMO=/tmp/hf
MOUNT=/tmp/demo-data
EXE=$PROJ/isaac-ohos/entry/src/main/resources/rawfile/isaac-ng.exe
SECS=${1:-45}
CFLAGS="-O1 -g -I$SHIM -I$SRC -I$SRC/generated"

mkdir -p "$DEMO/obj"
cp "$SRC/host_ohos.c" "$DEMO/host_ohos.c"
cp "$SRC/host_ext.h"  "$DEMO/host_ext.h"
python3 - "$DEMO" <<'PY'
import sys, pathlib
d = pathlib.Path(sys.argv[1])
def patch(name, old, new):
    p = d / name
    s = p.read_text()
    assert s.count(old) == 1, (name, s.count(old))
    p.write_text(s.replace(old, new))
patch("host_ohos.c",
      "__attribute__((weak)) const host_ext_module *host_ext_misc(void) { return NULL; }",
      "__attribute__((weak)) const host_ext_module *host_ext_misc(void) { return NULL; }\n"
      "__attribute__((weak)) const host_ext_module *host_ext_file(void) { return NULL; }")
patch("host_ohos.c", "    host_ext_misc,\n};", "    host_ext_misc,\n    host_ext_file,\n};")
patch("host_ext.h", "const host_ext_module *host_ext_misc(void);",
      "const host_ext_module *host_ext_misc(void);\nconst host_ext_module *host_ext_file(void);")
print("  registration applied to the COPIES (/tmp/hf)")
PY

gcc $CFLAGS -c "$DEMO/host_ohos.c"    -o "$DEMO/obj/host_ohos.o"
gcc $CFLAGS -c "$SRC/host_ext_file.c" -o "$DEMO/obj/host_ext_file.o"
OBJS=$(ls "$OUT"/obj/*.o | grep -vE '/(host_ohos|host_ext_file)\.o$' | tr '\n' ' ')
gcc -g -o "$DEMO/isaac_host_demo" "$PROJ/pc-host/main.c" $OBJS "$DEMO/obj/host_ohos.o" \
    "$DEMO/obj/host_ext_file.o" -lpthread -lm -ldl

rm -rf "$MOUNT"
mkdir -p "$MOUNT/Documents" "$MOUNT/data" "$MOUNT/mods"
: > "$MOUNT/kage_mount_points.dat"
echo "  data root: $MOUNT (kage_mount_points.dat present, 0 bytes)"

cd "$DEMO"
ISAAC_DATA_ROOT="$MOUNT" timeout "$SECS" ./isaac_host_demo "$EXE" 2>&1
