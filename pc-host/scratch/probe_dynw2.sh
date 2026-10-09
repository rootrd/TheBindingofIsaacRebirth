#!/bin/bash
# gdb：运行时动态 watch 卷容器槽 0——第一次 MATCHLOOKUP 时读 begin 再设 watch。
set -u
PROJ=/mnt/f/TheBindingofIsaacRebirth
OUT=$PROJ/pc-host/build
SCRATCH=$PROJ/pc-host/scratch
EXE=$PROJ/isaac-ohos/entry/src/main/resources/rawfile/isaac-ng.exe
DEMO=/tmp/hp_dyn
mkdir -p "$DEMO"

cat > "$DEMO/dyn.gdb" <<'GDB'
set pagination off
set confirm off
set height 0

break sub_00564aa0
run
delete 1

set $this = c->ecx
set $b = *(unsigned int *)(isaac_arena_base + $this + 8)
set $e = *(unsigned int *)(isaac_arena_base + $this + 0xc)
printf "FIRST LOOKUP this=0x%08x begin=0x%08x end=0x%08x n=%d\n", $this, $b, $e, ($e - $b) / 4
printf "slot0="
output *(unsigned int *)(isaac_arena_base + $b)
printf " slot1="
output *(unsigned int *)(isaac_arena_base + $b + 4)
printf " slot2="
output *(unsigned int *)(isaac_arena_base + $b + 8)
printf "\n"

watch *(unsigned int *)(isaac_arena_base + $b)
commands
  silent
  printf ">>> SLOT0 WRITE val="
  output *(unsigned int *)(isaac_arena_base + $b)
  printf "\n"
  bt 5
  continue
end

watch *(unsigned int *)(isaac_arena_base + $b + 4)
commands
  silent
  printf ">>> SLOT1 WRITE val="
  output *(unsigned int *)(isaac_arena_base + $b + 4)
  printf "\n"
  bt 5
  continue
end

continue
printf "---- done ----\n"
GDB

cd "$OUT"
ISAAC_DATA_ROOT="$OUT/isaac-data" timeout 240 gdb -batch -x "$DEMO/dyn.gdb" \
    --args ./isaac_host "$EXE" > "$SCRATCH/dyn_trace.log" 2>&1
grep -E "FIRST LOOKUP|slot[012]=|SLOT[01] WRITE|^#[0-4]" "$SCRATCH/dyn_trace.log" | head -50
