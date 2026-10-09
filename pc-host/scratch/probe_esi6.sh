#!/bin/bash
# gdb: log every write to g_cpu.esi during one sub_0056c7e0 invocation, to see
# where the caller's esi gets replaced.
set -u
PROJ=/mnt/f/TheBindingofIsaacRebirth
OUT=$PROJ/pc-host/build
SCRATCH=$PROJ/pc-host/scratch
EXE=$PROJ/isaac-ohos/entry/src/main/resources/rawfile/isaac-ng.exe
DEMO=/tmp/hp_esi6
mkdir -p "$DEMO"

cat > "$DEMO/esi6.gdb" <<'GDB'
set pagination off
set confirm off
set height 0
set width 0

set $armed = 0
set $hits = 0

break sub_0056c7e0
commands
  silent
  if $armed == 0
    set $armed = 1
    printf "\n=== arm on entry: esi=0x%08x esp=0x%08x\n", g_cpu.esi, g_cpu.esp
    watch g_cpu.esi
  end
  continue
end

run
printf "\n---- done, hits=%d ----\n", $hits
GDB

cd "$OUT"
ISAAC_DATA_ROOT="$OUT/isaac-data" \
  timeout 300 gdb -batch -x "$DEMO/esi6.gdb" --args ./isaac_host "$EXE" \
  > "$SCRATCH/esi6_trace.log" 2>&1
echo "---- log: $SCRATCH/esi6_trace.log"
grep -an "=== arm\|Old value\|New value\|---- done" "$SCRATCH/esi6_trace.log" | head -40
