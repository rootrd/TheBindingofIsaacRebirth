#!/bin/bash
# gdb: hardware watchpoint on the vector's `end` slot (this+4) that
# sub_002597b0 works on, to catch whoever writes the garbage value.
set -u
PROJ=/mnt/f/TheBindingofIsaacRebirth
OUT=$PROJ/pc-host/build
SCRATCH=$PROJ/pc-host/scratch
EXE=$PROJ/isaac-ohos/entry/src/main/resources/rawfile/isaac-ng.exe
DEMO=/tmp/hp_watch
mkdir -p "$DEMO"

cat > "$DEMO/watch.gdb" <<'GDB'
set pagination off
set confirm off
set height 0
set width 0

set $vec = 0

break sub_002597b0
commands
  silent
  if $vec == 0
    set $vec = g_cpu.ecx
    printf "\n=== sub_002597b0 this=0x%08x  [this]=0x%08x [this+4]=0x%08x [this+8]=0x%08x\n", $vec, *(unsigned int*)(isaac_arena_base+$vec), *(unsigned int*)(isaac_arena_base+$vec+4), *(unsigned int*)(isaac_arena_base+$vec+8)
    watch *(unsigned int*)(isaac_arena_base + $vec + 4)
    printf "--- watchpoint armed on end slot 0x%08x\n", $vec+4
  end
  continue
end

run
printf "\n---- stopped at a write to the end slot ----\n"
printf "end slot now = 0x%08x\n", *(unsigned int*)(isaac_arena_base + $vec + 4)
bt 12
GDB

cd "$OUT"
ISAAC_DATA_ROOT="$OUT/isaac-data" ISAAC_TRACE_MAX=200 \
  timeout 300 gdb -batch -x "$DEMO/watch.gdb" --args ./isaac_host "$EXE" \
  > "$SCRATCH/watch_trace.log" 2>&1
echo "---- log: $SCRATCH/watch_trace.log"
grep -n "=== sub_002597b0\|watchpoint armed\|Hardware watchpoint\|Old value\|New value\|end slot now\|---- stopped" "$SCRATCH/watch_trace.log" | head -40
