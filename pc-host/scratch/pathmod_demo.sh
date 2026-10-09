#!/bin/bash
# Measure host_ext_path.c with and without the caller's registration, editing
# no project file: host_ohos.c and host_ext.h are copied to /tmp, the two-line
# provider registration is applied to the COPIES, and the resulting objects are
# linked against the objects pc-host/run.sh already built (only host_ohos.o and
# host_ext_path.o are replaced, so the 190 generated translation units and every
# other host_*.o are reused as-is).
#
#   run_demo.sh [--register|--control] [seconds] [label]
#
#   --control   copies that are byte-identical to the tree (no registration):
#               this module is compiled and linked but never consulted, which is
#               today's tree.
#   --register  the two-line registration is applied to the copies.
# ISAAC_DATA_ROOT selects the data root; the guest's log.txt under it is saved
# next to the log for comparison.
#
# Usage: bash pc-host/scratch/pathmod_demo.sh --register 45 reg
set -e
PROJ=/mnt/f/TheBindingofIsaacRebirth
SRC=$PROJ/isaac-ohos/entry/src/main/cpp/isaac
SHIM=$PROJ/pc-host/shim
OUT=$PROJ/pc-host/build
SCRATCH=$PROJ/pc-host/scratch
EXE=$PROJ/isaac-ohos/entry/src/main/resources/rawfile/isaac-ng.exe

MODE=${1:---register}
SECS=${2:-45}
LABEL=${3:-$MODE}
DEMO=/tmp/hp_$LABEL

CFLAGS="-O1 -g -I$SHIM -I$SRC -I$SRC/generated"
mkdir -p "$DEMO/obj"

echo "== copy + register (copies only; mode=$MODE) =="
cp "$SRC/host_ohos.c" "$DEMO/host_ohos.c"
cp "$SRC/host_ext.h"  "$DEMO/host_ext.h"
if [ "$MODE" = "--register" ]; then
python3 - "$DEMO" <<'PY'
import sys, pathlib
d = pathlib.Path(sys.argv[1])

def patch(name, old, new):
    p = d / name
    s = p.read_text()
    assert s.count(old) == 1, (name, s.count(old), old[:60])
    p.write_text(s.replace(old, new))

patch("host_ohos.c",
      "__attribute__((weak)) const host_ext_module *host_ext_file(void) { return NULL; }",
      "__attribute__((weak)) const host_ext_module *host_ext_file(void) { return NULL; }\n"
      "__attribute__((weak)) const host_ext_module *host_ext_path(void) { return NULL; }")
patch("host_ohos.c", "    host_ext_file,\n};", "    host_ext_file,\n    host_ext_path,\n};")
patch("host_ext.h", "const host_ext_module *host_ext_file(void);",
      "const host_ext_module *host_ext_file(void);\nconst host_ext_module *host_ext_path(void);")
print("  patched host_ohos.c (provider list, appended last) and host_ext.h")
PY
else
  echo "  control: copies left byte-identical (module linked, never consulted)"
fi

echo "== compile the two objects =="
gcc $CFLAGS -c "$DEMO/host_ohos.c"    -o "$DEMO/obj/host_ohos.o"
gcc $CFLAGS -c "$SRC/host_ext_path.c" -o "$DEMO/obj/host_ext_path.o"

echo "== link (generated + other host objects reused) =="
OBJS=$(ls "$OUT"/obj/*.o | grep -vE '/(host_ohos|host_ext_path)\.o$' | tr '\n' ' ')
gcc -g -o "$DEMO/isaac_host" "$PROJ/pc-host/main.c" $OBJS "$DEMO/obj/host_ohos.o" \
    "$DEMO/obj/host_ext_path.o" -lpthread -lm -ldl
echo "  -> $DEMO/isaac_host (objects: $(echo $OBJS | wc -w) + 2)"

echo "== run ($SECS s, mode=$MODE) =="
LOG=$SCRATCH/pathmod_${LABEL}.log
cd "$DEMO"
export ISAAC_DATA_ROOT=${ISAAC_DATA_ROOT:-$OUT/isaac-data}
echo "  ISAAC_DATA_ROOT=$ISAAC_DATA_ROOT"
timeout "$SECS" ./isaac_host "$EXE" > "$LOG" 2>&1 || true
echo "  log: $LOG ($(wc -l < "$LOG") lines)"

SAVE="$ISAAC_DATA_ROOT/Documents/My Games/Binding of Isaac Repentance/log.txt"
if [ -f "$SAVE" ]; then
  cp "$SAVE" "$SCRATCH/pathmod_${LABEL}_guest_log.txt"
  echo "  guest log.txt: $SCRATCH/pathmod_${LABEL}_guest_log.txt"
fi

echo "== summary =="
echo "  host import calls: $(grep -c 'import ' "$LOG" || true) traced"
grep -E "guest_call heartbeat: count=[0-9]+" "$LOG" | tail -1 | sed 's/^/  last /'
grep -E "guest_fault: addr|isaac_guest_run" "$LOG" | tail -2 | sed 's/^/  /'
echo "  host_ext_path lines: $(grep -c 'host_ext_path' "$LOG" || true)"
echo "  KERNEL32 file handler lines:"
grep -E "CreateFileA|GetFileAttributesA|CreateDirectoryA|CloseHandle" "$LOG" \
    | sed 's/^/    /' | head -25
if [ -f "$SCRATCH/pathmod_${LABEL}_guest_log.txt" ]; then
  echo "  guest log.txt head:"
  head -8 "$SCRATCH/pathmod_${LABEL}_guest_log.txt" | sed 's/^/    /'
fi
