#!/bin/bash
set -u
PROJ=/mnt/f/TheBindingofIsaacRebirth
OUT=$PROJ/pc-host/build
cd "$OUT"
cat > /tmp/ab.gdb <<'GDB'
set pagination off
set confirm off
handle SIGABRT stop print nopass
run
bt 15
GDB
ISAAC_DATA_ROOT="$OUT/isaac-data" timeout 120 gdb -batch -x /tmp/ab.gdb \
  --args ./isaac_host "$PROJ/isaac-ohos/entry/src/main/resources/rawfile/isaac-ng.exe" 2>&1 |
  grep -a -A20 "SIGABRT\|Program received" | head -30
