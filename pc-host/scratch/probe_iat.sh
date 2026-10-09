#!/bin/bash
# gdb: watch the setstate IAT slot (guest 0x30606264) to catch whoever zeroes it.
set -u
PROJ=/mnt/f/TheBindingofIsaacRebirth
OUT=$PROJ/pc-host/build
SCRATCH=$PROJ/pc-host/scratch
EXE=$PROJ/isaac-ohos/entry/src/main/resources/rawfile/isaac-ng.exe
DEMO=/tmp/hp_iat
mkdir -p "$DEMO"

cat > "$DEMO/iat.gdb" <<'GDB'
set pagination off
set confirm off
set height 0
set width 0

set $hits = 0

watch *(unsigned int *)(isaac_arena_base + 0x30606264)
commands
  silent
  set $hits = $hits + 1
  printf "\n=== IAT 0x30606264 write #%d: now 0x%08x\n", $hits, *(unsigned int *)(isaac_arena_base + 0x30606264)
  bt 6
  if $hits >= 6
    printf "--- enough hits ---\n"
    disable 2
  end
  continue
end

run
printf "\n---- done, hits=%d ----\n", $hits
GDB

cd "$OUT"
ISAAC_DATA_ROOT="$OUT/isaac-data" \
  timeout 300 gdb -batch -x "$DEMO/iat.gdb" --args ./isaac_host "$EXE" \
  > "$SCRATCH/iat_trace.log" 2>&1
echo "---- log: $SCRATCH/iat_trace.log"
grep -n "=== IAT\|^#\|Old value\|New value\|---- done\|enough hits" "$SCRATCH/iat_trace.log" | head -50
