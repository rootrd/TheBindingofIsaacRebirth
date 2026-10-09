#!/bin/bash
# gdb: read the actual register-save slots on the guest stack (memory truth),
# at the prologue and at the epilogue of sub_0025c990.
set -u
PROJ=/mnt/f/TheBindingofIsaacRebirth
OUT=$PROJ/pc-host/build
SCRATCH=$PROJ/pc-host/scratch
EXE=$PROJ/isaac-ohos/entry/src/main/resources/rawfile/isaac-ng.exe
DEMO=/tmp/hp_slot2
mkdir -p "$DEMO"

cat > "$DEMO/slot2.gdb" <<'GDB'
set pagination off
set confirm off
set height 0
set width 0

set $e = 0

break sub_0025c990
commands
  silent
  set $ra = *(unsigned int*)(isaac_arena_base + g_cpu.esp)
  if $ra == 0x25981d && $e == 0
    set $e = g_cpu.esp
    printf "ENTRY esp=0x%08x  slots: ebx@0x%08x esi@0x%08x edi@0x%08x cookie@0x%08x\n", $e, $e-0x24, $e-0x28, $e-0x2c, $e-0x30
    printf "  caller regs (may be stale): esi=0x%08x edi=0x%08x ebx=0x%08x\n", g_cpu.esi, g_cpu.edi, g_cpu.ebx
  end
  continue
end

break guest_0061.c:17023
commands
  silent
  printf "PROLOGUE done, slots written: ebx=0x%08x esi=0x%08x edi=0x%08x cookie=0x%08x\n", *(unsigned int*)(isaac_arena_base+$e-0x24), *(unsigned int*)(isaac_arena_base+$e-0x28), *(unsigned int*)(isaac_arena_base+$e-0x2c), *(unsigned int*)(isaac_arena_base+$e-0x30)
  continue
end

break guest_0061.c:17199
commands
  silent
  printf "EPILOGUE: slots now: ebx=0x%08x esi=0x%08x edi=0x%08x cookie=0x%08x | ret=0x%08x\n", *(unsigned int*)(isaac_arena_base+$e-0x24), *(unsigned int*)(isaac_arena_base+$e-0x28), *(unsigned int*)(isaac_arena_base+$e-0x2c), *(unsigned int*)(isaac_arena_base+$e-0x30), *(unsigned int*)(isaac_arena_base+$e)
  continue
end

break guest_extra_002597b0.c:81
commands
  silent
  printf "BACK in caller: esi=0x%08x edi=0x%08x ebx=0x%08x esp=0x%08x\n", g_cpu.esi, g_cpu.edi, g_cpu.ebx, g_cpu.esp
  continue
end

run
printf "---- done ----\n"
GDB

cd "$OUT"
ISAAC_DATA_ROOT="$OUT/isaac-data" ISAAC_TRACE_MAX=200 \
  timeout 300 gdb -batch -x "$DEMO/slot2.gdb" --args ./isaac_host "$EXE" \
  > "$SCRATCH/slot2_trace.log" 2>&1
echo "---- log: $SCRATCH/slot2_trace.log"
grep -n "^ENTRY\|^  caller\|^PROLOGUE\|^EPILOGUE\|^BACK" "$SCRATCH/slot2_trace.log" | head -20
