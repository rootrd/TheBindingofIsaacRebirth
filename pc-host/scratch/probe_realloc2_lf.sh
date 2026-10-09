#!/bin/bash
# gdb: log reallocation (sub_0025ceb0) and string-move (sub_000226f0) calls
# with args read from the stack (at function entry [ebp] is the CALLER's frame).
set -u
PROJ=/mnt/f/TheBindingofIsaacRebirth
OUT=$PROJ/pc-host/build
SCRATCH=$PROJ/pc-host/scratch
EXE=$PROJ/isaac-ohos/entry/src/main/resources/rawfile/isaac-ng.exe
DEMO=/tmp/hp_realloc2
mkdir -p "$DEMO"

cat > "$DEMO/realloc.gdb" <<'GDB'
set pagination off
set confirm off
set height 0
set width 0

set $r = 0
set $m = 0

break sub_002597b0
commands
  silent
  printf "[B] sub_002597b0 this=0x%08x src(edx)=0x%08x arg3=0x%08x ret=0x%08x esp=0x%08x ebp=0x%08x\n", g_cpu.ecx, g_cpu.edx, *(unsigned int*)(isaac_arena_base+g_cpu.esp+4), *(unsigned int*)(isaac_arena_base+g_cpu.esp), g_cpu.esp, g_cpu.ebp
  continue
end

break sub_0025ceb0
commands
  silent
  set $r = $r + 1
  set $v = g_cpu.ecx
  printf "[R%03d] realloc this=0x%08x [b]=0x%08x [e]=0x%08x [c]=0x%08x pos=0x%08x val=0x%08x ret=0x%08x\n", $r, $v, *(unsigned int*)(isaac_arena_base+$v), *(unsigned int*)(isaac_arena_base+$v+4), *(unsigned int*)(isaac_arena_base+$v+8), *(unsigned int*)(isaac_arena_base+g_cpu.esp+4), *(unsigned int*)(isaac_arena_base+g_cpu.esp+8), *(unsigned int*)(isaac_arena_base+g_cpu.esp)
  continue
end

break sub_000226f0
commands
  silent
  set $m = $m + 1
  set $d = g_cpu.eax
  set $s = g_cpu.ecx
  set $e = g_cpu.edx
  set $cnt = ($e - $s) / 24
  if $s > 0x40000000 || $e > 0x40000000 || $cnt > 0x10000 || $d > 0x80000000
    printf "[M!!!] #%d dest=0x%08x src=0x%08x end=0x%08x count=%u ret=0x%08x\n", $m, $d, $s, $e, $cnt, *(unsigned int*)(isaac_arena_base+g_cpu.esp)
    bt 4
  else
    printf "[M%03d] dest=0x%08x src=0x%08x end=0x%08x count=%u ret=0x%08x\n", $m, $d, $s, $e, $cnt, *(unsigned int*)(isaac_arena_base+g_cpu.esp)
  end
  continue
end

run
printf "---- done, reallocs=%d moves=%d ----\n", $r, $m
GDB

cd "$OUT"
ISAAC_DATA_ROOT="$OUT/isaac-data" ISAAC_TRACE_MAX=200 \
  timeout 300 gdb -batch -x "$DEMO/realloc.gdb" --args ./isaac_host "$EXE" \
  > "$SCRATCH/realloc2_trace.log" 2>&1
echo "---- log: $SCRATCH/realloc2_trace.log"
grep -n "^\[B\]\|^\[R\|^\[M" "$SCRATCH/realloc2_trace.log" | tail -60
