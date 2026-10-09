#!/bin/bash
# gdb: watch the string-move helper sub_000226f0. Prints every call's
# (dest, src, end) and stops loudly when src/end are absurd.
set -u
PROJ=/mnt/f/TheBindingofIsaacRebirth
OUT=$PROJ/pc-host/build
SCRATCH=$PROJ/pc-host/scratch
EXE=$PROJ/isaac-ohos/entry/src/main/resources/rawfile/isaac-ng.exe
DEMO=/tmp/hp_move
mkdir -p "$DEMO"

cat > "$DEMO/move.gdb" <<'GDB'
set pagination off
set confirm off
set height 0
set width 0

set $n = 0

break sub_000226f0
commands
  silent
  set $n = $n + 1
  set $d = g_cpu.eax
  set $s = g_cpu.ecx
  set $e = g_cpu.edx
  set $cnt = ($e - $s) / 24
  if $s > 0x80000000 || $e > 0x80000000 || $cnt > 0x10000
    printf "!!! BOGUS move call #%d dest=0x%08x src=0x%08x end=0x%08x count=%u esp=0x%08x ebp=0x%08x\n", $n, $d, $s, $e, $cnt, g_cpu.esp, g_cpu.ebp
    printf "caller ret-addr = 0x%08x\n", *(unsigned int *)(isaac_arena_base + g_cpu.esp)
    bt 6
  else
    if $n <= 60
      printf "move #%d dest=0x%08x src=0x%08x end=0x%08x count=%u esp=0x%08x\n", $n, $d, $s, $e, $cnt, g_cpu.esp
    end
  end
  continue
end

run
printf "---- done, total move calls=%d ----\n", $n
GDB

cd "$OUT"
ISAAC_DATA_ROOT="$OUT/isaac-data" ISAAC_TRACE_MAX=200 \
  timeout 300 gdb -batch -x "$DEMO/move.gdb" --args ./isaac_host "$EXE" \
  > "$SCRATCH/move_trace.log" 2>&1
echo "---- log: $SCRATCH/move_trace.log"
grep -n "^move #\|^!!! BOGUS\|^caller ret\|^---- done" "$SCRATCH/move_trace.log" | head -80
