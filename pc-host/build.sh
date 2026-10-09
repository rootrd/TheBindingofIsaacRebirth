#!/bin/bash
# Build the recompiled guest for a POSIX host (no device). Compiles the same
# runtime sources the HAP uses, plus every generated translation unit, into a
# single executable so the import/CRT layer can be debugged with gdb at full
# speed. Graphics/audio/input are out of scope here.
set -e
PROJ=/mnt/f/TheBindingofIsaacRebirth
SRC=$PROJ/isaac-ohos/entry/src/main/cpp/isaac
SHIM=$PROJ/pc-host/shim
OUT=$PROJ/pc-host/build
JOBS=$(nproc)

mkdir -p "$OUT/obj"
CFLAGS="-O1 -g -fno-strict-aliasing -I$SHIM -I$SRC -I$SRC/generated"

echo "== runtime objects =="
for f in guest.c host_ohos.c isaac_guest_main.c guest_memory.c stubs.c; do
  [ -f "$SRC/$f" ] || { echo "  (skip $f: absent)"; continue; }
  printf '  %s\n' "$f"
  gcc $CFLAGS -c "$SRC/$f" -o "$OUT/obj/$(basename "$f" .c).o" &
done
wait

echo "== generated translation units (190) =="
ls "$SRC"/generated/guest_*.c > "$OUT/tu.list"
cat "$OUT/tu.list" | xargs -P "$JOBS" -I{} sh -c \
  'gcc '"$CFLAGS"' -c "{}" -o "'"$OUT"'/obj/$(basename {} .c).o"' 
echo "  compiled: $(ls "$OUT"/obj/guest_*.o | wc -l)"

echo "== link =="
gcc -g -o "$OUT/isaac_host" "$PROJ/pc-host/main.c" "$OUT"/obj/*.o -lpthread -lm -ldl
echo "  -> $OUT/isaac_host"
