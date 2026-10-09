#!/bin/bash
# gdb：watch 容器 end（固定地址 0x307c7b3c），打印每次 push 的槽值 + bt。
set -u
PROJ=/mnt/f/TheBindingofIsaacRebirth
OUT=$PROJ/pc-host/build
SCRATCH=$PROJ/pc-host/scratch
EXE=$PROJ/isaac-ohos/entry/src/main/resources/rawfile/isaac-ng.exe
DEMO=/tmp/hp_end
mkdir -p "$DEMO"

cat > "$DEMO/end.gdb" <<'GDB'
set pagination off
set confirm off
set height 0

break isaac_guest_run
run
delete 1

watch *(unsigned int *)(isaac_arena_base + 0x307c7b3c)
commands
  silent
  set $e = *(unsigned int *)(isaac_arena_base + 0x307c7b3c)
  set $b = *(unsigned int *)(isaac_arena_base + 0x307c7b38)
  if $e > $b
    printf "PUSH slot-val="
    output *(unsigned int *)(isaac_arena_base + $e - 4)
    printf "  (n=%d)\n", ($e - $b) / 4
    bt 4
  else
    printf "CLEAR (n=0)\n"
  end
  continue
end

continue
printf "---- done ----\n"
GDB

cd "$OUT"
ISAAC_DATA_ROOT="$OUT/isaac-data" timeout 240 gdb -batch -x "$DEMO/end.gdb" \
    --args ./isaac_host "$EXE" > "$SCRATCH/end_trace.log" 2>&1
grep -E "^PUSH|^CLEAR|^#[0-3]" "$SCRATCH/end_trace.log" | head -50
