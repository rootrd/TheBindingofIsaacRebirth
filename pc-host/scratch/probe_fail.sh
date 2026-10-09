#!/bin/bash
# gdb：RESOLVE FAIL 现场同轮 dump 容器三槽（消除运行间布局漂移）。
set -u
PROJ=/mnt/f/TheBindingofIsaacRebirth
OUT=$PROJ/pc-host/build
SCRATCH=$PROJ/pc-host/scratch
EXE=$PROJ/isaac-ohos/entry/src/main/resources/rawfile/isaac-ng.exe
DEMO=/tmp/hp_fail
mkdir -p "$DEMO"

cat > "$DEMO/fail.gdb" <<'GDB'
set pagination off
set confirm off
set height 0

break guest_0166.c:9485
run
delete 1

set $b = *(unsigned int *)(isaac_arena_base + 0x307c7b38)
set $e = *(unsigned int *)(isaac_arena_base + 0x307c7b3c)
printf "FAIL-SITE: begin=0x%08x end=0x%08x n=%d\n", $b, $e, ($e - $b) / 4
printf "slot0="
output *(unsigned int *)(isaac_arena_base + $b)
printf " slot1="
output *(unsigned int *)(isaac_arena_base + $b + 4)
printf " slot2="
output *(unsigned int *)(isaac_arena_base + $b + 8)
printf "\n"
# 打印每个非零对象的名字字段
set $i = 0
while $i < 3
  set $o = *(unsigned int *)(isaac_arena_base + $b + $i * 4)
  if $o != 0
    printf "obj[%d]=0x%08x name@", $i, $o
    output $o + 0xc
    printf " name="
    output *(unsigned int *)(isaac_arena_base + $o + 0xc)
    printf " nameptr-text="
    output (char *)(isaac_arena_base + *(unsigned int *)(isaac_arena_base + $o + 0xc))
    printf "\n"
  end
  set $i = $i + 1
end
continue
printf "---- done ----\n"
GDB

cd "$OUT"
ISAAC_DATA_ROOT="$OUT/isaac-data" timeout 240 gdb -batch -x "$DEMO/fail.gdb" \
    --args ./isaac_host "$EXE" > "$SCRATCH/fail_trace.log" 2>&1
grep -E "FAIL-SITE|slot[012]|obj\[|name" "$SCRATCH/fail_trace.log" | head -12
