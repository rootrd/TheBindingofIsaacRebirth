#!/bin/bash
# gdb: hardware watchpoints on the register-save slots of sub_0025c990's frame,
# to catch whoever overwrites the caller's saved esi/ebx.
set -u
PROJ=/mnt/f/TheBindingofIsaacRebirth
OUT=$PROJ/pc-host/build
SCRATCH=$PROJ/pc-host/scratch
EXE=$PROJ/isaac-ohos/entry/src/main/resources/rawfile/isaac-ng.exe
DEMO=/tmp/hp_slot
mkdir -p "$DEMO"

cat > "$DEMO/slot.gdb" <<'GDB'
set pagination off
set confirm off
set height 0
set width 0

set $armed = 0

break sub_0025c990
commands
  silent
  set $ra = *(unsigned int*)(isaac_arena_base + g_cpu.esp)
  if $ra == 0x25981d && $armed == 0
    set $armed = 1
    set $e = g_cpu.esp
    # frame: entry E; saved ebx at E-0x24, saved esi at E-0x28, saved edi at E-0x2c, cookie at E-0x30
    printf "ARMED: entry esp=0x%08x  saved_ebx=0x%08x saved_esi=0x%08x saved_edi=0x%08x cookie=0x%08x ret=0x%08x\n", $e, $e-0x24, $e-0x28, $e-0x2c, $e-0x30, $e
    printf "  caller esi=0x%08x edi=0x%08x ebx=0x%08x\n", g_cpu.esi, g_cpu.edi, g_cpu.ebx
    watch *(unsigned int*)(isaac_arena_base + $e - 0x28)
    watch *(unsigned int*)(isaac_arena_base + $e - 0x24)
  end
  continue
end

run
printf "\n---- stopped (watchpoint or signal) ----\n"
printf "saved_esi slot=0x%08x saved_ebx slot=0x%08x\n", $e-0x28, $e-0x24
bt 8
GDB

cd "$OUT"
ISAAC_DATA_ROOT="$OUT/isaac-data" ISAAC_TRACE_MAX=200 \
  timeout 300 gdb -batch -x "$DEMO/slot.gdb" --args ./isaac_host "$EXE" \
  > "$SCRATCH/slot_trace.log" 2>&1
echo "---- log: $SCRATCH/slot_trace.log"
grep -n "ARMED\|caller esi\|Hardware watchpoint\|Old value\|New value\|---- stopped\|saved_esi slot\|^#" "$SCRATCH/slot_trace.log" | head -40
