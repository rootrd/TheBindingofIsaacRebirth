#!/bin/bash
# gdb：FAIL#2 现场检查——旧块对象 0x4000a780/0x4000e8d0 是否完好，新块 0x4000ec30 附近内存布局。
set -u
PROJ=/mnt/f/TheBindingofIsaacRebirth
OUT=$PROJ/pc-host/build
SCRATCH=$PROJ/pc-host/scratch
EXE=$PROJ/isaac-ohos/entry/src/main/resources/rawfile/isaac-ng.exe
DEMO=/tmp/hp_old
mkdir -p "$DEMO"

cat > "$DEMO/old.gdb" <<'GDB'
set pagination off
set confirm off
set height 0

set $fails = 0

break guest_0166.c:9485
run
delete 1

# 第 2 次 FAIL（第一个归档）时检查
set $fails = $fails + 1
while $fails < 2
  set $fails = $fails + 1
  continue
end

printf "=== FAIL#2 container begin=0x4000ec30 ===\n"
printf "new-block slots:\n"
x/3xw isaac_arena_base + 0x4000ec30
printf "old obj 0x4000a780 (was slot0):\n"
x/8xw isaac_arena_base + 0x4000a780
printf "old obj 0x4000e8d0 (was slot1):\n"
x/8xw isaac_arena_base + 0x4000e8d0
printf "old obj 0x4000ea90 (slot2, alive):\n"
x/8xw isaac_arena_base + 0x4000ea90
printf "vector struct 0x307c7b30 (begin/end/cap):\n"
x/3xw isaac_arena_base + 0x307c7b38
continue
printf "---- done ----\n"
GDB

cd "$OUT"
ISAAC_DATA_ROOT="$OUT/isaac-data" timeout 240 gdb -batch -x "$DEMO/old.gdb" \
    --args ./isaac_host "$EXE" > "$SCRATCH/old_trace.log" 2>&1
grep -A20 "FAIL#2" "$SCRATCH/old_trace.log" | head -24
