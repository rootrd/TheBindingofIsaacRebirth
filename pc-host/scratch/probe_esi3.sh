#!/bin/bash
# gdb: watch the saved-esi slot in sub_0056c7e0's frame to find the overwriter.
set -u
PROJ=/mnt/f/TheBindingofIsaacRebirth
OUT=$PROJ/pc-host/build
SCRATCH=$PROJ/pc-host/scratch
EXE=$PROJ/isaac-ohos/entry/src/main/resources/rawfile/isaac-ng.exe
DEMO=/tmp/hp_esi3
mkdir -p "$DEMO"

cat > "$DEMO/esi3.gdb" <<'GDB'
set pagination off
set confirm off
set height 0
set width 0

set $armed = 0
set $hits = 0
set $slot = 0

break sub_0056c7e0
commands
  silent
  if $armed == 0
    set $armed = 1
    set $slot = g_cpu.esp - 0x3c
    printf "\n=== arm: entry esp=0x%08x saved-esi slot=0x%08x esi=0x%08x\n", g_cpu.esp, $slot, g_cpu.esi
    watch *(unsigned int *)(isaac_arena_base + $slot)
  end
  continue
end

break guest_0167.c:21723
commands
  silent
  set $hits = $hits + 1
  if $hits <= 6
    printf "  [epilogue pop esi] esp=0x%08x slot(0x%08x)=0x%08x\n", g_cpu.esp, $slot, *(unsigned int*)(isaac_arena_base+$slot)
  end
  continue
end

run
printf "\n---- done, epilogue hits=%d ----\n", $hits
GDB

cd "$OUT"
ISAAC_DATA_ROOT="$OUT/isaac-data" \
  timeout 300 gdb -batch -x "$DEMO/esi3.gdb" --args ./isaac_host "$EXE" \
  > "$SCRATCH/esi3_trace.log" 2>&1
echo "---- log: $SCRATCH/esi3_trace.log"
grep -an "=== arm\|Old value\|New value\|Hardware watchpoint [0-9]*: \*\|epilogue pop esi\|---- done" "$SCRATCH/esi3_trace.log" | head -30
