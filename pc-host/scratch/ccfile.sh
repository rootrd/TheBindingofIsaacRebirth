#!/bin/bash
PROJ=/mnt/f/TheBindingofIsaacRebirth
SRC=$PROJ/isaac-ohos/entry/src/main/cpp/isaac
SHIM=$PROJ/pc-host/shim
gcc -O1 -g -I"$SHIM" -I"$SRC" -I"$SRC/generated" -c "$SRC/host_ext_file.c" -o /tmp/hf.o 2>&1 | head -12
