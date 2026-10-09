#!/bin/bash
set -e
PROJ=/mnt/f/TheBindingofIsaacRebirth
SRC=$PROJ/isaac-ohos/entry/src/main/cpp/isaac
OUT=$PROJ/pc-host/build
gcc -O1 -g -I$PROJ/pc-host/shim -I$SRC -I$SRC/generated -c $SRC/generated/guest_0166.c -o $OUT/obj/guest_0166.o
gcc -g -o $OUT/isaac_host $PROJ/pc-host/main.c $OUT/obj/*.o -lpthread -lm -ldl
echo BUILD_OK
