#!/bin/bash
# Run the harness with host_ext_file.c REGISTERED, without editing any project
# file: host_ohos.c and host_ext.h are copied to /tmp, the two-line provider
# registration is applied to the COPIES, and the resulting objects are linked
# against the objects pc-host/run.sh already built (only host_ohos.o is
# replaced, so the 190 generated translation units are reused as-is).
#
# Usage: bash pc-host/scratch/run_demo.sh [seconds]
set -e
PROJ=/mnt/f/TheBindingofIsaacRebirth
SRC=$PROJ/isaac-ohos/entry/src/main/cpp/isaac
SHIM=$PROJ/pc-host/shim
OUT=$PROJ/pc-host/build
DEMO=/tmp/hf
SECS=${1:-60}

CFLAGS="-O1 -g -I$SHIM -I$SRC -I$SRC/generated"
mkdir -p "$DEMO/obj"

echo "== copy + register (copies only) =="
cp "$SRC/host_ohos.c" "$DEMO/host_ohos.c"
cp "$SRC/host_ext.h"  "$DEMO/host_ext.h"
python3 - "$DEMO" <<'PY'
import sys, pathlib
d = pathlib.Path(sys.argv[1])

def patch(name, old, new):
    p = d / name
    s = p.read_text()
    assert s.count(old) == 1, (name, s.count(old), old[:60])
    p.write_text(s.replace(old, new))

patch("host_ohos.c",
      "__attribute__((weak)) const host_ext_module *host_ext_misc(void) { return NULL; }",
      "__attribute__((weak)) const host_ext_module *host_ext_misc(void) { return NULL; }\n"
      "__attribute__((weak)) const host_ext_module *host_ext_file(void) { return NULL; }")
patch("host_ohos.c", "    host_ext_misc,\n};", "    host_ext_misc,\n    host_ext_file,\n};")
patch("host_ext.h", "const host_ext_module *host_ext_misc(void);",
      "const host_ext_module *host_ext_misc(void);\nconst host_ext_module *host_ext_file(void);")
print("  patched host_ohos.c (provider list) and host_ext.h (declaration)")
PY

echo "== compile the two objects =="
gcc $CFLAGS -c "$DEMO/host_ohos.c"       -o "$DEMO/obj/host_ohos.o"
gcc $CFLAGS -c "$SRC/host_ext_file.c"    -o "$DEMO/obj/host_ext_file.o"

echo "== link (generated objects reused) =="
OBJS=$(ls "$OUT"/obj/*.o | grep -vE '/(host_ohos|host_ext_file)\.o$' | tr '\n' ' ')
gcc -g -o "$DEMO/isaac_host_demo" "$PROJ/pc-host/main.c" $OBJS "$DEMO/obj/host_ohos.o" \
    "$DEMO/obj/host_ext_file.o" -lpthread -lm -ldl
echo "  -> $DEMO/isaac_host_demo (objects: $(echo $OBJS | wc -w) + 2)"

echo "== run ($SECS s) =="
cd "$DEMO"
export ISAAC_DATA_ROOT=${ISAAC_DATA_ROOT:-$OUT/isaac-data}
echo "  ISAAC_DATA_ROOT=$ISAAC_DATA_ROOT"
timeout "$SECS" ./isaac_host_demo "$PROJ/isaac-ohos/entry/src/main/resources/rawfile/isaac-ng.exe" 2>&1
