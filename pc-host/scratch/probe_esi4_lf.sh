#!/bin/bash
# gdb: read esi at the exact return point of sub_004b3340 (0x4b344e mov eax,esi)
# to see whether the caller's esi really is clobbered.
set -u
PROJ=/mnt/f/TheBindingofIsaacRebirth
OUT=$PROJ/pc-host/build
SCRATCH=$PROJ/pc-host/scratch
EXE=$PROJ/isaac-ohos/entry/src/main/resources/rawfile/isaac-ng.exe
DEMO=/tmp/hp_esi4
mkdir -p "$DEMO"

cat > "$DEMO/esi4.gdb" <<'GDB'
set pagination off
set confirm off
set height 0
set width 0

set $n = 0

break sub_004b3340
commands
  silent
  set $n = $n + 1
  if $n <= 3
    printf "\n[call %d] entry: arg0=0x%08x esi=0x%08x esp=0x%08x\n", $n, *(unsigned int*)(isaac_arena_base+g_cpu.esp+4), g_cpu.esi, g_cpu.esp
  end
  continue
end

break guest_0144.c:7089
commands
  silent
  if $n <= 3
    printf "[call %d] at 0x4b344e: esi=0x%08x eax(before)=0x%08x esp=0x%08x\n", $n, g_cpu.esi, g_cpu.eax, g_cpu.esp
  end
  continue
end

run
printf "\n---- done, calls=%d ----\n", $n
GDB

cd "$OUT"
ISAAC_DATA_ROOT="$OUT/isaac-data" \
  timeout 300 gdb -batch -x "$DEMO/esi4.gdb" --args ./isaac_host "$EXE" \
  > "$SCRATCH/esi4_trace.log" 2>&1
echo "---- log: $SCRATCH/esi4_trace.log"
grep -an "^\[call\|---- done" "$SCRATCH/esi4_trace.log" | head -20
