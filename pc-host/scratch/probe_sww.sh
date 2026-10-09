#!/bin/bash
# gdb：软件 watchpoint（禁硬件）抓容器槽 0 的完整写入历史。
set -u
PROJ=/mnt/f/TheBindingofIsaacRebirth
OUT=$PROJ/pc-host/build
SCRATCH=$PROJ/pc-host/scratch
EXE=$PROJ/isaac-ohos/entry/src/main/resources/rawfile/isaac-ng.exe
DEMO=/tmp/hp_sww
mkdir -p "$DEMO"

cat > "$DEMO/sww.gdb" <<'GDB'
set pagination off
set confirm off
set height 0
set can-use-hw-watchpoints 0

break isaac_guest_run
run
delete 1

set $count = 0

watch *(unsigned int *)(isaac_arena_base + 0x4000ec30)
commands
  silent
  set $count = $count + 1
  printf "[W%02d] slot0 <- ", $count
  output *(unsigned int *)(isaac_arena_base + 0x4000ec30)
  printf "\n"
  if $count <= 4
    bt 4
  end
  if $count >= 12
    detach
  end
  continue
end

continue
printf "---- done ----\n"
GDB

cd "$OUT"
ISAAC_DATA_ROOT="$OUT/isaac-data" timeout 500 gdb -batch -x "$DEMO/sww.gdb" \
    --args ./isaac_host "$EXE" > "$SCRATCH/sww_trace.log" 2>&1
grep -E "^\[W[0-9]|^#[0-3]" "$SCRATCH/sww_trace.log" | head -30
