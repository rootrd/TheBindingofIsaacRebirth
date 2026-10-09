#!/bin/bash
# Compile the hand-translated generated TUs (run.sh does not cover generated/),
# then relink. Usage: bash pc-host/compile_extras.sh [more_names...]
set -e
PROJ=/mnt/f/TheBindingofIsaacRebirth
SRC=$PROJ/isaac-ohos/entry/src/main/cpp/isaac
SHIM=$PROJ/pc-host/shim
OUT=$PROJ/pc-host/build

NAMES=${*:-"guest_extra_002599c0 guest_extra_002c8700 guest_extra_00481280"}

for n in $NAMES; do
    f="$SRC/generated/$n.c"
    [ -f "$f" ] || { echo "missing $f"; exit 1; }
    gcc -O1 -g -I"$SHIM" -I"$SRC" -I"$SRC/generated" -c "$f" -o "$OUT/obj/$n.o"
    echo "compiled $n"
done

gcc -g -o "$OUT/isaac_host" "$PROJ/pc-host/main.c" "$OUT"/obj/*.o -lpthread -lm -ldl
echo "relinked $OUT/isaac_host"
