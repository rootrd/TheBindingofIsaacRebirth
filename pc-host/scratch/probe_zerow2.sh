#!/bin/bash
# gdb：抓最终容器块 SLOT0/SLOT1 被改写为 0 的现场 + backtrace。
set -u
PROJ=/mnt/f/TheBindingofIsaacRebirth
OUT=$PROJ/pc-host/build
SCRATCH=$PROJ/pc-host/scratch
EXE=$PROJ/isaac-ohos/entry/src/main/resources/rawfile/isaac-ng.exe
DEMO=/tmp/hp_z
mkdir -p "$DEMO"

cat > "$DEMO/z.gdb" <<'GDB'
set pagination off
set confirm off
set height 0

break isaac_guest_run
run
delete 1

set $s0 = isaac_arena_base + 0x4000ec30
set $s1 = isaac_arena_base + 0x4000ec34

watch *(unsigned int *)($s0)
commands
  silent
  if *(unsigned int *)($s0) == 0
    printf ">>> SLOT0 ZEROED. bt:\n"
    bt 6
    detach
  else
    continue
  end
end

watch *(unsigned int *)($s1)
commands
  silent
  if *(unsigned int *)($s1) == 0
    printf ">>> SLOT1 ZEROED. bt:\n"
    bt 6
    detach
  else
    continue
  end
end

continue
printf "---- done ----\n"
GDB

cd "$OUT"
ISAAC_DATA_ROOT="$OUT/isaac-data" timeout 150 gdb -batch -x "$DEMO/z.gdb" \
    --args ./isaac_host "$EXE" > "$SCRATCH/zero_trace.log" 2>&1
grep -A8 "ZEROED" "$SCRATCH/zero_trace.log" | head -22
