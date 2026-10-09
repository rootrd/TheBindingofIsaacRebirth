#!/bin/bash
# gdb: trace register save/restore and esp balance across the sub_0025c990 call
# that the recompiled sub_002597b0 makes.
set -u
PROJ=/mnt/f/TheBindingofIsaacRebirth
OUT=$PROJ/pc-host/build
SCRATCH=$PROJ/pc-host/scratch
EXE=$PROJ/isaac-ohos/entry/src/main/resources/rawfile/isaac-ng.exe
DEMO=/tmp/hp_bal
mkdir -p "$DEMO"

cat > "$DEMO/bal.gdb" <<'GDB'
set pagination off
set confirm off
set height 0
set width 0

break guest_extra_002597b0.c:76
commands
  silent
  printf "[C1 before push edx ] esp=0x%08x esi=0x%08x edx=0x%08x ebx=0x%08x edi=0x%08x\n", g_cpu.esp, g_cpu.esi, g_cpu.edx, g_cpu.ebx, g_cpu.edi
  continue
end

break guest_extra_002597b0.c:80
commands
  silent
  printf "[C2 after call 990 ] esp=0x%08x esi=0x%08x eax=0x%08x ebx=0x%08x edi=0x%08x\n", g_cpu.esp, g_cpu.esi, g_cpu.eax, g_cpu.ebx, g_cpu.edi
  continue
end

break guest_0061.c:17013
commands
  silent
  printf "[S1 push esi (entry)] esp=0x%08x esi=0x%08x\n", g_cpu.esp, g_cpu.esi
  continue
end

break guest_0061.c:17416
commands
  silent
  printf "[S2 pop ebx        ] esp=0x%08x esi=0x%08x ebx=0x%08x edi=0x%08x ebp=0x%08x\n", g_cpu.esp, g_cpu.esi, g_cpu.ebx, g_cpu.edi, g_cpu.ebp
  printf "    stack top 6: "
  x/6xw isaac_arena_base + g_cpu.esp
  continue
end

break guest_0061.c:17429
commands
  silent
  printf "[S3 pop esi        ] esp=0x%08x esi=0x%08x\n", g_cpu.esp, g_cpu.esi
  continue
end

break guest_0061.c:17435
commands
  silent
  printf "[S4 ret 8          ] esp=0x%08x\n", g_cpu.esp
  continue
end

run
printf "---- done ----\n"
GDB

cd "$OUT"
ISAAC_DATA_ROOT="$OUT/isaac-data" ISAAC_TRACE_MAX=200 \
  timeout 300 gdb -batch -x "$DEMO/bal.gdb" --args ./isaac_host "$EXE" \
  > "$SCRATCH/bal_trace.log" 2>&1
echo "---- log: $SCRATCH/bal_trace.log"
grep -n "^\[C\|^\[S\|^    0x" "$SCRATCH/bal_trace.log" | head -40
