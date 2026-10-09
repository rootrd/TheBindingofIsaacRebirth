#!/bin/bash
# gdb：同一运行内的完整时序——WinMain 的卷初始化调用、容器每次变化、MOUNT 失败现场。
set -u
PROJ=/mnt/f/TheBindingofIsaacRebirth
OUT=$PROJ/pc-host/build
SCRATCH=$PROJ/pc-host/scratch
EXE=$PROJ/isaac-ohos/entry/src/main/resources/rawfile/isaac-ng.exe
DEMO=/tmp/hp_seq
mkdir -p "$DEMO"

cat > "$DEMO/seq.gdb" <<'GDB'
set pagination off
set confirm off
set height 0

break sub_0048bc50
commands
  silent
  printf "[SEQ] INIT sub_0048bc50 called\n"
  continue
end

break isaac_guest_run
run
delete 2

watch *(unsigned int *)(isaac_arena_base + 0x307c7b3c)
commands
  silent
  set $e = *(unsigned int *)(isaac_arena_base + 0x307c7b3c)
  set $b = *(unsigned int *)(isaac_arena_base + 0x307c7b38)
  if $e > $b
    printf "[SEQ] PUSH n=%d val=0x%08x\n", ($e - $b) / 4, *(unsigned int *)(isaac_arena_base + $e - 4)
  else
    printf "[SEQ] CLEAR\n"
  end
  continue
end

set $fails = 0
break guest_0166.c:9485
commands
  silent
  set $fails = $fails + 1
  if $fails <= 2
    set $b = *(unsigned int *)(isaac_arena_base + 0x307c7b38)
    set $e = *(unsigned int *)(isaac_arena_base + 0x307c7b3c)
    printf "[SEQ] RESOLVE-FAIL #%d container begin=0x%08x n=%d slot0=0x%08x slot1=0x%08x slot2=0x%08x\n", $fails, $b, ($e - $b) / 4, *(unsigned int *)(isaac_arena_base + $b), *(unsigned int *)(isaac_arena_base + $b + 4), *(unsigned int *)(isaac_arena_base + $b + 8)
  end
  continue
end

continue
printf "---- done ----\n"
GDB

cd "$OUT"
ISAAC_DATA_ROOT="$OUT/isaac-data" timeout 240 gdb -batch -x "$DEMO/seq.gdb" \
    --args ./isaac_host "$EXE" > "$SCRATCH/seq_trace.log" 2>&1
grep -E "\[SEQ\]" "$SCRATCH/seq_trace.log" | head -40
