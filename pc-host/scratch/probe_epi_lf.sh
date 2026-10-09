#!/bin/bash
# gdb: is the real epilogue of sub_0025c990 (pop ecx/edi/esi/ebx, ret 0xc) reached?
set -u
PROJ=/mnt/f/TheBindingofIsaacRebirth
OUT=$PROJ/pc-host/build
SCRATCH=$PROJ/pc-host/scratch
EXE=$PROJ/isaac-ohos/entry/src/main/resources/rawfile/isaac-ng.exe
DEMO=/tmp/hp_epi
mkdir -p "$DEMO"

cat > "$DEMO/epi.gdb" <<'GDB'
set pagination off
set confirm off
set height 0
set width 0

break guest_0061.c:16996
commands
  silent
  printf "[E0 entry sub_0025c990] esp=0x%08x esi=0x%08x edi=0x%08x ebx=0x%08x\n", g_cpu.esp, g_cpu.esi, g_cpu.edi, g_cpu.ebx
  continue
end

break guest_0061.c:17199
commands
  silent
  printf "[E1 pop ecx (real epilogue)] esp=0x%08x esi=0x%08x edi=0x%08x ebx=0x%08x\n", g_cpu.esp, g_cpu.esi, g_cpu.edi, g_cpu.ebx
  continue
end

break guest_0061.c:17211
commands
  silent
  printf "[E2 ret 0xc] esp=0x%08x esi=0x%08x edi=0x%08x ebx=0x%08x\n", g_cpu.esp, g_cpu.esi, g_cpu.edi, g_cpu.ebx
  continue
end

break guest_extra_002597b0.c:79
commands
  silent
  printf "[C  call 25c990] esp=0x%08x esi=0x%08x edi=0x%08x ebx=0x%08x\n", g_cpu.esp, g_cpu.esi, g_cpu.edi, g_cpu.ebx
  continue
end

break guest_extra_002597b0.c:81
commands
  silent
  printf "[C' back in caller] esp=0x%08x esi=0x%08x edi=0x%08x ebx=0x%08x\n", g_cpu.esp, g_cpu.esi, g_cpu.edi, g_cpu.ebx
  continue
end

run
printf "---- done ----\n"
GDB

cd "$OUT"
ISAAC_DATA_ROOT="$OUT/isaac-data" ISAAC_TRACE_MAX=200 \
  timeout 300 gdb -batch -x "$DEMO/epi.gdb" --args ./isaac_host "$EXE" \
  > "$SCRATCH/epi_trace.log" 2>&1
echo "---- log: $SCRATCH/epi_trace.log"
grep -n "^\[E\|^\[C" "$SCRATCH/epi_trace.log" | head -30
