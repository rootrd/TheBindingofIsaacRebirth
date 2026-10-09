#!/bin/bash
# gdb: watch the guest's esi field and report only writes that look bogus,
# to catch which generated function clobbers it.
set -u
PROJ=/mnt/f/TheBindingofIsaacRebirth
OUT=$PROJ/pc-host/build
SCRATCH=$PROJ/pc-host/scratch
EXE=$PROJ/isaac-ohos/entry/src/main/resources/rawfile/isaac-ng.exe
DEMO=/tmp/hp_esi5
mkdir -p "$DEMO"

cat > "$DEMO/esi5.gdb" <<'GDB'
set pagination off
set confirm off
set height 0
set width 0

set $armed = 0
set $hits = 0
set $bogus = 0

break sub_004b3340
commands
  silent
  if $armed == 0
    set $armed = 1
    printf "\n=== arming watch on g_cpu.esi (entry value 0x%08x)\n", g_cpu.esi
    watch g_cpu.esi
  end
  continue
end

run
printf "\n---- done, bogus writes reported=%d ----\n", $bogus
GDB

cd "$OUT"
ISAAC_DATA_ROOT="$OUT/isaac-data" \
  timeout 300 gdb -batch -x "$DEMO/esi5.gdb" --args ./isaac_host "$EXE" \
  > "$SCRATCH/esi5_trace.log" 2>&1
echo "---- armed? ----"
grep -an "=== arming\|---- done" "$SCRATCH/esi5_trace.log" | head -5
echo "---- esi writes seen (first 40) ----"
grep -a -A3 "^Old value\|^New value" "$SCRATCH/esi5_trace.log" | head -60
