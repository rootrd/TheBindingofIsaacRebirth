#!/bin/bash
# gdb: log every vector-reallocate (sub_0025ceb0) and every string-move
# (sub_000226f0) call in order, with the vector's begin/end/cap, so the
# first bogus value is visible together with its source.
set -u
PROJ=/mnt/f/TheBindingofIsaacRebirth
OUT=$PROJ/pc-host/build
SCRATCH=$PROJ/pc-host/scratch
EXE=$PROJ/isaac-ohos/entry/src/main/resources/rawfile/isaac-ng.exe
DEMO=/tmp/hp_realloc
mkdir -p "$DEMO"

cat > "$DEMO/realloc.gdb" <<'GDB'
set pagination off
set confirm off
set height 0
set width 0

set $r = 0

break sub_0025ceb0
commands
  silent
  set $r = $r + 1
  set $v = g_cpu.ecx
  printf "[R%03d] ce alloc  this=0x%08x [b]=0x%08x [e]=0x%08x [c]=0x%08x  pos=[ebp+8]=0x%08x val=[ebp+0xc]=0x%08x ebp=0x%08x esp=0x%08x\n", $r, $v, *(unsigned int*)(isaac_arena_base+$v), *(unsigned int*)(isaac_arena_base+$v+4), *(unsigned int*)(isaac_arena_base+$v+8), *(unsigned int*)(isaac_arena_base+g_cpu.ebp+8), *(unsigned int*)(isaac_arena_base+g_cpu.ebp+0xc), g_cpu.ebp, g_cpu.esp
  if $r > 60
    printf "--- too many reallocations, detaching trace ---\n"
    disable 1
  end
  continue
end

break sub_000226f0
commands
  silent
  set $d = g_cpu.eax
  set $s = g_cpu.ecx
  set $e = g_cpu.edx
  set $cnt = ($e - $s) / 24
  if $s > 0x40000000 || $e > 0x40000000 || $cnt > 0x10000
    printf "[M!!!] dest=0x%08x src=0x%08x end=0x%08x count=%u ret=0x%08x\n", $d, $s, $e, $cnt, *(unsigned int*)(isaac_arena_base+g_cpu.esp)
  else
    printf "[M   ] dest=0x%08x src=0x%08x end=0x%08x count=%u ret=0x%08x\n", $d, $s, $e, $cnt, *(unsigned int*)(isaac_arena_base+g_cpu.esp)
  end
  continue
end

run
printf "---- done, reallocs=%d ----\n", $r
GDB

cd "$OUT"
ISAAC_DATA_ROOT="$OUT/isaac-data" ISAAC_TRACE_MAX=200 \
  timeout 300 gdb -batch -x "$DEMO/realloc.gdb" --args ./isaac_host "$EXE" \
  > "$SCRATCH/realloc_trace.log" 2>&1
echo "---- log: $SCRATCH/realloc_trace.log"
grep -n "^\[R\|^\[M" "$SCRATCH/realloc_trace.log" | tail -50
