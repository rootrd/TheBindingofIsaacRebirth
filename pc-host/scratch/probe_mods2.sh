#!/bin/bash
# gdb: follow the mods-singleton construction chain: what sub_004b3340 returns
# (the local's address?), what the local holds, and what the singleton gets.
set -u
PROJ=/mnt/f/TheBindingofIsaacRebirth
OUT=$PROJ/pc-host/build
SCRATCH=$PROJ/pc-host/scratch
EXE=$PROJ/isaac-ohos/entry/src/main/resources/rawfile/isaac-ng.exe
DEMO=/tmp/hp_mods2
mkdir -p "$DEMO"

cat > "$DEMO/mods2.gdb" <<'GDB'
set pagination off
set confirm off
set height 0
set width 0

set $n = 0

break guest_0108.c:9024
commands
  silent
  set $n = $n + 1
  if $n <= 3
    printf "\n[call %d] after sub_004b3340: eax=0x%08x  local[ebp-0x28]=0x%08x local[ebp-0x24]=0x%08x  ebp=0x%08x\n", $n, g_cpu.eax, *(unsigned int*)(isaac_arena_base+g_cpu.ebp-0x28), *(unsigned int*)(isaac_arena_base+g_cpu.ebp-0x24), g_cpu.ebp
    printf "         singleton[0x307fece4]=0x%08x\n", *(unsigned int*)(isaac_arena_base+0x307fece4)
  end
  continue
end

break guest_0108.c:9032
commands
  silent
  if $n <= 3
    printf "[call %d] after sub_00007700: singleton[0x307fece4]=0x%08x  eax=0x%08x  local[ebp-0x28]=0x%08x\n", $n, *(unsigned int*)(isaac_arena_base+0x307fece4), g_cpu.eax, *(unsigned int*)(isaac_arena_base+g_cpu.ebp-0x28)
  end
  continue
end

break guest_0108.c:9074
commands
  silent
  if $n <= 3
    printf "[call %d] at use: singleton[0x307fece4]=0x%08x\n", $n, *(unsigned int*)(isaac_arena_base+0x307fece4)
  end
  continue
end

run
printf "\n---- done, calls=%d ----\n", $n
GDB

cd "$OUT"
ISAAC_DATA_ROOT="$OUT/isaac-data" \
  timeout 300 gdb -batch -x "$DEMO/mods2.gdb" --args ./isaac_host "$EXE" \
  > "$SCRATCH/mods2_trace.log" 2>&1
echo "---- log: $SCRATCH/mods2_trace.log"
grep -an "^\[call\|^         singleton\|---- done" "$SCRATCH/mods2_trace.log" | head -20
