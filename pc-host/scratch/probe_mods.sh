#!/bin/bash
# gdb: why is the mods singleton still NULL after its construction call?
set -u
PROJ=/mnt/f/TheBindingofIsaacRebirth
OUT=$PROJ/pc-host/build
SCRATCH=$PROJ/pc-host/scratch
EXE=$PROJ/isaac-ohos/entry/src/main/resources/rawfile/isaac-ng.exe
DEMO=/tmp/hp_mods
mkdir -p "$DEMO"

cat > "$DEMO/mods.gdb" <<'GDB'
set pagination off
set confirm off
set height 0
set width 0

break guest_0108.c:9024
commands
  silent
  printf "[after 4b3340] eax=0x%08x (the constructed temp)\n", g_cpu.eax
  continue
end

break guest_0108.c:9030
commands
  silent
  printf "[before 7700] ecx(singleton addr)=0x%08x [0x307fece4]=0x%08x esp=0x%08x [esp+4]=0x%08x\n", g_cpu.ecx, *(unsigned int*)(isaac_arena_base+0x307fece4), g_cpu.esp, *(unsigned int*)(isaac_arena_base+g_cpu.esp+4)
  continue
end

break guest_0108.c:9032
commands
  silent
  printf "[after 7700] [0x307fece4]=0x%08x eax=0x%08x esp=0x%08x\n", *(unsigned int*)(isaac_arena_base+0x307fece4), g_cpu.eax, g_cpu.esp
  continue
end

break guest_0108.c:9074
commands
  silent
  printf "[at 3a93a9] [0x307fece4]=0x%08x\n", *(unsigned int*)(isaac_arena_base+0x307fece4)
  continue
end

run
printf "\n---- done ----\n"
GDB

cd "$OUT"
ISAAC_DATA_ROOT="$OUT/isaac-data" \
  timeout 300 gdb -batch -x "$DEMO/mods.gdb" --args ./isaac_host "$EXE" \
  > "$SCRATCH/mods_trace.log" 2>&1
echo "---- log: $SCRATCH/mods_trace.log"
grep -an "^\[after\|^\[before\|^\[at\|---- done" "$SCRATCH/mods_trace.log" | head -20
