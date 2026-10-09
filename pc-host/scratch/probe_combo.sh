#!/bin/bash
# gdb：同轮组合观测——MATCHLOOKUP 现场 dump + 槽 0 无条件 watch。
set -u
PROJ=/mnt/f/TheBindingofIsaacRebirth
OUT=$PROJ/pc-host/build
SCRATCH=$PROJ/pc-host/scratch
EXE=$PROJ/isaac-ohos/entry/src/main/resources/rawfile/isaac-ng.exe
DEMO=/tmp/hp_combo
mkdir -p "$DEMO"

cat > "$DEMO/combo.gdb" <<'GDB'
set pagination off
set confirm off
set height 0

set $lookups = 0

break sub_00564aa0
commands
  silent
  set $lookups = $lookups + 1
  if $lookups <= 8
    set $b = *(unsigned int *)(isaac_arena_base + c->ecx + 8)
    set $e = *(unsigned int *)(isaac_arena_base + c->ecx + 0xc)
    printf "[LK%02d] this=0x%08x begin=0x%08x n=%d slot0=", $lookups, c->ecx, $b, ($e - $b) / 4
    output *(unsigned int *)(isaac_arena_base + $b)
    printf "\n"
  end
  continue
end

break isaac_guest_run
run
delete 2

set $count = 0
watch *(unsigned int *)(isaac_arena_base + 0x4000ec30)
commands
  silent
  set $count = $count + 1
  printf "[W%02d] slot0 <- 0x%08x\n", $count, *(unsigned int *)(isaac_arena_base + 0x4000ec30)
  if $count == 1
    bt 8
  end
  if $count >= 8
    detach
  end
  continue
end

continue
printf "---- done ----\n"
GDB

cd "$OUT"
ISAAC_DATA_ROOT="$OUT/isaac-data" timeout 300 gdb -batch -x "$DEMO/combo.gdb" \
    --args ./isaac_host "$EXE" > "$SCRATCH/combo_trace.log" 2>&1
grep -E "^\[LK|^\[W" "$SCRATCH/combo_trace.log" | head -40
