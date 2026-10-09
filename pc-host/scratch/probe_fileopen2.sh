#!/bin/bash
# gdb：File::Open(sub_00596360) 入口实参——确认 Z:/ 挂载表路径是否到达。
set -u
PROJ=/mnt/f/TheBindingofIsaacRebirth
OUT=$PROJ/pc-host/build
SCRATCH=$PROJ/pc-host/scratch
EXE=$PROJ/isaac-ohos/entry/src/main/resources/rawfile/isaac-ng.exe
DEMO=/tmp/hp_fo
mkdir -p "$DEMO"

cat > "$DEMO/fo.gdb" <<'GDB'
set pagination off
set confirm off
set height 0

set $n = 0

break sub_00596360
commands
  silent
  set $n = $n + 1
  if $n <= 8
    set $p = *(unsigned int *)(isaac_arena_base + c->esp + 4)
    printf "[OPEN#%02d] obj=0x%08x pathptr=0x%08x text=", $n, c->ecx, $p
    output (char *)(isaac_arena_base + $p)
    printf "\n"
  end
  continue
end

run
printf "---- done ----\n"
GDB

cd "$OUT"
ISAAC_DATA_ROOT="$OUT/isaac-data" ISAAC_TRACE_RESOLVE=1 timeout 240 gdb -batch -x "$DEMO/fo.gdb" \
    --args ./isaac_host "$EXE" > "$SCRATCH/fo_trace.log" 2>&1
grep -E "^\[OPEN" "$SCRATCH/fo_trace.log" | head -10
