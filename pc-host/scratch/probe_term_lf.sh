#!/bin/bash
# gdb: catch the CRT fatal path (TerminateProcess) and print the guest call chain.
set -u
PROJ=/mnt/f/TheBindingofIsaacRebirth
OUT=$PROJ/pc-host/build
SCRATCH=$PROJ/pc-host/scratch
EXE=$PROJ/isaac-ohos/entry/src/main/resources/rawfile/isaac-ng.exe
DEMO=/tmp/hp_terminate
mkdir -p "$DEMO"

cat > "$DEMO/term.gdb" <<'GDB'
set pagination off
set confirm off
set height 0
set width 0

break h_TerminateProcess
commands
  silent
  printf "\n=== TerminateProcess called: code=0x%08x\n", *(unsigned int*)(isaac_arena_base + g_cpu.esp + 8)
  bt 40
  printf "\n--- guest esp=0x%08x ebp=0x%08x\n", g_cpu.esp, g_cpu.ebp
  printf "--- guest stack window (return addresses) ---\n"
  x/40xw isaac_arena_base + g_cpu.esp
  continue
end

run
printf "\n---- done ----\n"
GDB

cd "$OUT"
ISAAC_DATA_ROOT="$OUT/isaac-data" \
  timeout 300 gdb -batch -x "$DEMO/term.gdb" --args ./isaac_host "$EXE" \
  > "$SCRATCH/term_trace.log" 2>&1
echo "---- log: $SCRATCH/term_trace.log"
sed -n "$(grep -n '=== TerminateProcess' "$SCRATCH/term_trace.log" | head -1 | cut -d: -f1),+55p" "$SCRATCH/term_trace.log"
