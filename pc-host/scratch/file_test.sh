#!/bin/bash
# Build and run the out-of-tree unit test for host_ext_file.c (WSL/POSIX host).
# Usage: bash pc-host/scratch/file_test.sh
set -e
PROJ=/mnt/f/TheBindingofIsaacRebirth
SRC=$PROJ/isaac-ohos/entry/src/main/cpp/isaac
SHIM=$PROJ/pc-host/shim
TEST=$PROJ/pc-host/scratch/file_test.c
ROOT=/tmp/isaac-file-test-root

rm -rf "$ROOT"
mkdir -p "$ROOT"
gcc -O1 -g -Wall -I"$SHIM" -I"$SRC" -I"$SRC/generated" \
    -o /tmp/file_test "$TEST" "$SRC/guest_memory.c"
echo "--- unit test ---"
ISAAC_DATA_ROOT="$ROOT" /tmp/file_test
echo "--- files the handlers created under $ROOT ---"
find "$ROOT" -type f | sort
