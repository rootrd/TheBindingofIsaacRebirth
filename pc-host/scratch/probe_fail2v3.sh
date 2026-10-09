#!/bin/bash
# gdb：FAIL#2 命中瞬间（进程存活时）动态读容器——不做任何硬编码地址、不删断点。
set -u
PROJ=/mnt/f/TheBindingofIsaacRebirth
OUT=$PROJ/pc-host/build
SCRATCH=$PROJ/pc-host/scratch
EXE=$PROJ/isaac-ohos/entry/src/main/resources/rawfile/isaac-ng.exe
DEMO=/tmp/hp_f2
mkdir -p "$DEMO"

cat > "$DEMO/f2.gdb" <<'GDB'
set pagination off
set confirm off
set height 0

set $n = 0

break guest_0166.c:9485
commands
  silent
  set $n = $n + 1
  if $n == 2
    printf "=== FAIL#2 live dump ===\n"
    printf "container this+0..0x10: "
    output/x *(unsigned long long *)(isaac_arena_base + 0x307c7b30)
    printf " "
    output/x *(unsigned long long *)(isaac_arena_base + 0x307c7b38)
    printf "\n"
    set $b = *(unsigned int *)(isaac_arena_base + 0x307c7b38)
    set $e = *(unsigned int *)(isaac_arena_base + 0x307c7b3c)
    printf "begin=0x%08x end=0x%08x n=%d\n", $b, $e, ($e - $b) / 4
    printf "slot0="
    output/x *(unsigned int *)(isaac_arena_base + $b)
    printf " slot1="
    output/x *(unsigned int *)(isaac_arena_base + $b + 4)
    printf " slot2="
    output/x *(unsigned int *)(isaac_arena_base + $b + 8)
    printf "\n"
    printf "old-block 0x4000a910 now="
    output/x *(unsigned int *)(isaac_arena_base + 0x4000a910)
    printf "\n"
    printf "old obj 0x4000a780 ="
    output/x *(unsigned int *)(isaac_arena_base + 0x4000a780)
    printf "\n"
    printf "=== end dump ===\n"
  end
  continue
end

run
continue
printf "---- done ----\n"
GDB

cd "$OUT"
ISAAC_DATA_ROOT="$OUT/isaac-data" timeout 240 gdb -batch -x "$DEMO/f2.gdb" \
    --args ./isaac_host "$EXE" > "$SCRATCH/f2_trace.log" 2>&1
grep -A10 "FAIL#2 live dump" "$SCRATCH/f2_trace.log" | head -14
