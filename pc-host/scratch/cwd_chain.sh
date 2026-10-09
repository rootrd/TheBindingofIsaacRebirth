#!/bin/bash
set -u
PROJ=/mnt/f/TheBindingofIsaacRebirth
OUT=$PROJ/pc-host/build
SCRATCH=$PROJ/pc-host/scratch
EXE=$PROJ/isaac-ohos/entry/src/main/resources/rawfile/isaac-ng.exe
DEMO=/tmp/hp_cwd2
mkdir -p "$DEMO"

cat > "$DEMO/cwd.gdb" <<'GDB'
set pagination off
set confirm off
set height 0

break h_GetCurrentDirectoryA
commands
  silent
  printf "GETCWD ebp="
  output c->ebp
  printf " ebpchain="
  output *(unsigned int *)(isaac_arena_base + c->ebp)
  printf " ret@"
  output *(unsigned int *)(isaac_arena_base + c->ebp + 4)
  printf "\n"
  set $e = *(unsigned int *)(isaac_arena_base + c->ebp)
  printf "L1 ret="
  output *(unsigned int *)(isaac_arena_base + $e + 4)
  printf "\n"
  set $e2 = *(unsigned int *)(isaac_arena_base + $e)
  printf "L2 ret="
  output *(unsigned int *)(isaac_arena_base + $e2 + 4)
  printf "\n"
  set $e3 = *(unsigned int *)(isaac_arena_base + $e2)
  printf "L3 ret="
  output *(unsigned int *)(isaac_arena_base + $e3 + 4)
  printf "\n"
  continue
end

run
printf "---- done ----\n"
GDB

cd "$OUT"
ISAAC_DATA_ROOT="$OUT/isaac-data" timeout 150 gdb -batch -x "$DEMO/cwd.gdb" \
    --args ./isaac_host "$EXE" > "$SCRATCH/cwd_trace2.log" 2>&1
grep -E "GETCWD|L[123] ret" "$SCRATCH/cwd_trace2.log" | head -10
