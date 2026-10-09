#!/bin/bash
# gdb: single out which callee inside sub_002597b0 clobbers esi.
set -u
PROJ=/mnt/f/TheBindingofIsaacRebirth
OUT=$PROJ/pc-host/build
SCRATCH=$PROJ/pc-host/scratch
EXE=$PROJ/isaac-ohos/entry/src/main/resources/rawfile/isaac-ng.exe
DEMO=/tmp/hp_esi
mkdir -p "$DEMO"

cat > "$DEMO/esi.gdb" <<'GDB'
set pagination off
set confirm off
set height 0
set width 0

# one-shot: only report the first pass through sub_002597b0
set $active = 1

break guest_extra_002597b0.c:54
commands
  silent
  printf "[after mov esi,ecx ] esi=0x%08x\n", g_cpu.esi
  continue
end

break guest_extra_002597b0.c:79
commands
  silent
  printf "[before call 25c990] esi=0x%08x ecx=0x%08x edx=0x%08x\n", g_cpu.esi, g_cpu.ecx, g_cpu.edx
  continue
end

break guest_extra_002597b0.c:81
commands
  silent
  printf "[after  call 25c990] esi=0x%08x eax=0x%08x\n", g_cpu.esi, g_cpu.eax
  continue
end

break guest_extra_002597b0.c:100
commands
  silent
  printf "[before call 25d680] esi=0x%08x\n", g_cpu.esi
  continue
end

break guest_extra_002597b0.c:103
commands
  silent
  printf "[after  call 25d680] esi=0x%08x eax=0x%08x\n", g_cpu.esi, g_cpu.eax
  continue
end

break guest_extra_002597b0.c:127
commands
  silent
  printf "[before call 00079f0] esi=0x%08x ecx=0x%08x\n", g_cpu.esi, g_cpu.ecx
  continue
end

break guest_extra_002597b0.c:129
commands
  silent
  printf "[after  call 00079f0] esi=0x%08x\n", g_cpu.esi
  continue
end

break guest_extra_002597b0.c:137
commands
  silent
  printf "[at L_0025987d before 25ceb0] esi=0x%08x\n", g_cpu.esi
  continue
end

run
printf "---- done ----\n"
GDB

cd "$OUT"
ISAAC_DATA_ROOT="$OUT/isaac-data" ISAAC_TRACE_MAX=200 \
  timeout 300 gdb -batch -x "$DEMO/esi.gdb" --args ./isaac_host "$EXE" \
  > "$SCRATCH/esi_trace.log" 2>&1
echo "---- log: $SCRATCH/esi_trace.log"
grep -n "^\[" "$SCRATCH/esi_trace.log"
