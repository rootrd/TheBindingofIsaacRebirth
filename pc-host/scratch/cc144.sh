#!/bin/bash
PROJ=/mnt/f/TheBindingofIsaacRebirth
SRC=$PROJ/isaac-ohos/entry/src/main/cpp/isaac
SHIM=$PROJ/pc-host/shim
gcc -O1 -g -I"$SHIM" -I"$SRC" -I"$SRC/generated" -c "$SRC/generated/guest_0144.c" -o /tmp/g144.o 2>&1 | head -25
