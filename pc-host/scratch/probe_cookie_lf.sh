#!/bin/bash
# gdb: compare the stack-cookie slot against the global cookie at the prologue
# and at the check, in every sub_002597b0 call.
set -u
PROJ=/mnt/f/TheBindingofIsaacRebirth
OUT=$PROJ/pc-host/build
SCRATCH=$PROJ/pc-host/scratch
EXE=$PROJ/isaac-ohos/entry/src/main/resources/rawfile/isaac-ng.exe
DEMO=/tmp/hp_cookie
mkdir -p "$DEMO"

cat > "$DEMO/cookie.gdb" <<'GDB'
set pagination off
set confirm off
set height 0
set width 0

set $n = 0

break guest_extra_002597b0.c:41
commands
  silent
  set $n = $n + 1
  printf "[call %d prologue] ebp=0x%08x slot[ebp-0x14]=0x%08x global[0x307aa3b4]=0x%08x\n", $n, g_cpu.ebp, *(unsigned int*)(isaac_arena_base+g_cpu.ebp-0x14), *(unsigned int*)(isaac_arena_base+0x307aa3b4)
  continue
end

break guest_extra_002597b0.c:269
commands
  silent
  printf "[call %d check   ] ebp=0x%08x slot[ebp-0x14]=0x%08x global[0x307aa3b4]=0x%08x ecx=0x%08x\n", $n, g_cpu.ebp, *(unsigned int*)(isaac_arena_base+g_cpu.ebp-0x14), *(unsigned int*)(isaac_arena_base+0x307aa3b4), g_cpu.ecx
  continue
end

run
printf "\n---- done, calls=%d ----\n", $n
GDB

cd "$OUT"
ISAAC_DATA_ROOT="$OUT/isaac-data" \
  timeout 300 gdb -batch -x "$DEMO/cookie.gdb" --args ./isaac_host "$EXE" \
  > "$SCRATCH/cookie_trace.log" 2>&1
echo "---- log: $SCRATCH/cookie_trace.log"
grep -an "^\[call\|---- done" "$SCRATCH/cookie_trace.log" | tail -20
