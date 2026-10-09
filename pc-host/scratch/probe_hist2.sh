#!/bin/bash
# gdb：硬件 watch 容器槽 0（0x4000ec30）的完整写入历史，无条件打印。
set -u
PROJ=/mnt/f/TheBindingofIsaacRebirth
OUT=$PROJ/pc-host/build
SCRATCH=$PROJ/pc-host/scratch
EXE=$PROJ/isaac-ohos/entry/src/main/resources/rawfile/isaac-ng.exe
DEMO=/tmp/hp_hist
mkdir -p "$DEMO"

cat > "$DEMO/hist.gdb" <<'GDB'
set pagination off
set confirm off
set height 0

break isaac_guest_run
run
delete 1

set $count = 0

watch *(unsigned int *)(isaac_arena_base + 0x4000ec30)
commands
  silent
  set $count = $count + 1
  printf "[H%02d] slot0 <- 0x%08x\n", $count, *(unsigned int *)(isaac_arena_base + 0x4000ec30)
  if $count <= 3
    bt 4
  end
  if $count >= 10
    detach
  end
  continue
end

continue
printf "---- done ----\n"
GDB

cd "$OUT"
ISAAC_DATA_ROOT="$OUT/isaac-data" timeout 300 gdb -batch -x "$DEMO/hist.gdb" \
    --args ./isaac_host "$EXE" > "$SCRATCH/hist_trace.log" 2>&1
grep -E "^\[H[0-9]|^#[0-3]" "$SCRATCH/hist_trace.log" | head -26
