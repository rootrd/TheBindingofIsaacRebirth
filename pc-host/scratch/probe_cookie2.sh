#!/bin/bash
# gdb: watch the stack-cookie slot of sub_002597b0's frame to catch the writer.
set -u
PROJ=/mnt/f/TheBindingofIsaacRebirth
OUT=$PROJ/pc-host/build
SCRATCH=$PROJ/pc-host/scratch
EXE=$PROJ/isaac-ohos/entry/src/main/resources/rawfile/isaac-ng.exe
DEMO=/tmp/hp_cookie2
mkdir -p "$DEMO"

cat > "$DEMO/cookie2.gdb" <<'GDB'
set pagination off
set confirm off
set height 0
set width 0

set $armed = 0
set $hits = 0
set $slot = 0

break guest_extra_002597b0.c:41
commands
  silent
  if $armed == 0
    set $armed = 1
    set $slot = g_cpu.ebp - 0x14
    printf "\n=== arming watch on cookie slot 0x%08x (ebp=0x%08x), value=0x%08x\n", $slot, g_cpu.ebp, *(unsigned int*)(isaac_arena_base+$slot)
    watch *(unsigned int *)(isaac_arena_base + $slot)
  end
  continue
end

break guest_extra_002597b0.c:269
commands
  silent
  printf "=== at check: slot=0x%08x hits=%d\n", *(unsigned int*)(isaac_arena_base+$slot), $hits
  continue
end

run
printf "\n---- done, hits=%d ----\n", $hits
GDB

cd "$OUT"
ISAAC_DATA_ROOT="$OUT/isaac-data" \
  timeout 300 gdb -batch -x "$DEMO/cookie2.gdb" --args ./isaac_host "$EXE" \
  > "$SCRATCH/cookie2_trace.log" 2>&1
echo "---- log: $SCRATCH/cookie2_trace.log"
grep -an "arming watch\|Old value\|New value\|Hardware watchpoint [0-9]*:.*\n\|=== at check\|---- done\|^#[0-9]" "$SCRATCH/cookie2_trace.log" | head -40
