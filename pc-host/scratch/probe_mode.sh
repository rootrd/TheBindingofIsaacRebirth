#!/bin/bash
# Compile the one generated TU we instrumented plus host_ohos.c, relink, run.
set -e
PROJ=/mnt/f/TheBindingofIsaacRebirth
SRC=$PROJ/isaac-ohos/entry/src/main/cpp/isaac
OUT=$PROJ/pc-host/build
CFLAGS="-O1 -g -I$PROJ/pc-host/shim -I$SRC -I$SRC/generated"
for f in "$SRC"/host_ohos.c "$SRC"/generated/guest_0172.c "$SRC"/generated/guest_0175.c; do
  gcc $CFLAGS -c "$f" -o "$OUT/obj/$(basename "$f" .c).o"
done
gcc -g -o "$OUT/isaac_host" "$PROJ/pc-host/main.c" "$OUT"/obj/*.o -lpthread -lm -ldl
echo BUILD_OK
