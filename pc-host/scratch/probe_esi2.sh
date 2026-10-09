#!/bin/bash
# gdb: find which callee inside sub_004b3340 clobbers esi (the value it returns).
set -u
PROJ=/mnt/f/TheBindingofIsaacRebirth
OUT=$PROJ/pc-host/build
SCRATCH=$PROJ/pc-host/scratch
EXE=$PROJ/isaac-ohos/entry/src/main/resources/rawfile/isaac-ng.exe
DEMO=/tmp/hp_esi2
mkdir -p "$DEMO"

cat > "$DEMO/esi2.gdb" <<'GDB'
set pagination off
set confirm off
set height 0
set width 0

set $armed = 0

break sub_004b3340
commands
  silent
  if $armed == 0
    set $armed = 1
    printf "\n=== sub_004b3340 entry: arg0=[esp+4]=0x%08x esi=0x%08x esp=0x%08x\n", *(unsigned int*)(isaac_arena_base+g_cpu.esp+4), g_cpu.esi, g_cpu.esp
  end
  continue
end

break guest_0144.c:6957
commands
  silent
  printf "  before call 0x7c80    esi=0x%08x\n", g_cpu.esi
  continue
end

break guest_0144.c:6960
commands
  silent
  printf "  after  call 0x7c80    esi=0x%08x\n", g_cpu.esi
  continue
end

break guest_0144.c:6971
commands
  silent
  printf "  before call 0x470460  esi=0x%08x\n", g_cpu.esi
  continue
end

break guest_0144.c:6973
commands
  silent
  printf "  after  call 0x470460  esi=0x%08x\n", g_cpu.esi
  continue
end

break guest_0144.c:7013
commands
  silent
  printf "  before call 0x5eb08e  esi=0x%08x\n", g_cpu.esi
  continue
end

break guest_0144.c:7016
commands
  silent
  printf "  after  call 0x5eb08e  esi=0x%08x\n", g_cpu.esi
  continue
end

break guest_0144.c:7034
commands
  silent
  printf "  before call 0x56c7e0  esi=0x%08x\n", g_cpu.esi
  continue
end

break guest_0144.c:7036
commands
  silent
  printf "  after  call 0x56c7e0  esi=0x%08x\n", g_cpu.esi
  continue
end

run
printf "\n---- done ----\n"
GDB

cd "$OUT"
ISAAC_DATA_ROOT="$OUT/isaac-data" \
  timeout 300 gdb -batch -x "$DEMO/esi2.gdb" --args ./isaac_host "$EXE" \
  > "$SCRATCH/esi2_trace.log" 2>&1
echo "---- log: $SCRATCH/esi2_trace.log"
grep -an "^=== sub_004b3340 entry\|^  before\|^  after\|---- done" "$SCRATCH/esi2_trace.log" | head -40
