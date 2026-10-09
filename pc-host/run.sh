#!/bin/bash
# Fast host iteration: rebuild only what changed, relink, run.
# Usage: bash pc-host/run.sh [exe-path] [seconds]
#   ISAAC_TRACE_MAX=N   raise the guest_call trace cap (default 200)
#   ISAAC_DATA_ROOT=DIR data root the guest sees as its profile directory
#   TAIL=N              how many output lines to show (default 40)
set -e
PROJ=/mnt/f/TheBindingofIsaacRebirth
SRC=$PROJ/isaac-ohos/entry/src/main/cpp/isaac
SHIM=$PROJ/pc-host/shim
OUT=$PROJ/pc-host/build
EXE=${1:-$PROJ/isaac-ohos/entry/src/main/resources/rawfile/isaac-ng.exe}
SECS=${2:-60}
CFLAGS="-O1 -g -I$SHIM -I$SRC -I$SRC/generated"

mkdir -p "$OUT/obj"
# guest.c and every host_*.c: picking them up by glob means a new extension
# module needs no change here.
for f in "$SRC"/guest.c "$SRC"/isaac_guest_main.c "$SRC"/guest_memory.c \
         "$SRC"/stubs.c "$SRC"/host_*.c; do
  [ -f "$f" ] || continue
  gcc $CFLAGS -c "$f" -o "$OUT/obj/$(basename "$f" .c).o"
done

gcc -g -o "$OUT/isaac_host" "$PROJ/pc-host/main.c" "$OUT"/obj/*.o -lpthread -lm -ldl
echo "--- run ($SECS s) ---"
cd "$OUT"
timeout "$SECS" ./isaac_host "$EXE" 2>&1 | tail -"${TAIL:-40}"
