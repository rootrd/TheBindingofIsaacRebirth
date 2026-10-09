#!/bin/bash
# gdb: catch the crash, dump host backtrace + guest register/stack state.
# Full guest_call trace is enabled so the last calls before the fault are visible.
set -u
PROJ=/mnt/f/TheBindingofIsaacRebirth
OUT=$PROJ/pc-host/build
SCRATCH=$PROJ/pc-host/scratch
EXE=$PROJ/isaac-ohos/entry/src/main/resources/rawfile/isaac-ng.exe
DEMO=/tmp/hp_segv
mkdir -p "$DEMO"

cat > "$DEMO/segv.gdb" <<'GDB'
set pagination off
set confirm off
set height 0
set width 0

handle SIGSEGV stop print nopass

run

printf "\n===== CRASH =====\n"
printf "host bt:\n"
bt 16

printf "\n===== GUEST CPU =====\n"
printf "esp=0x%08x ebp=0x%08x\n", g_cpu.esp, g_cpu.ebp
printf "eax=0x%08x ebx=0x%08x ecx=0x%08x edx=0x%08x\n", g_cpu.eax, g_cpu.ebx, g_cpu.ecx, g_cpu.edx
printf "esi=0x%08x edi=0x%08x\n", g_cpu.esi, g_cpu.edi
printf "fault=%s fault_addr=0x%08x\n", g_cpu.fault, g_cpu.fault_addr
printf "exit_code=0x%08x exit_api=%s\n", g_cpu.exit_code, g_cpu.exit_api

printf "\n===== sub_0025ceb0 frame locals =====\n"
printf "vect this(ebx)=0x%08x  [ebx]=0x%08x [ebx+4]=0x%08x [ebx+8]=0x%08x\n", g_cpu.ebx, *(unsigned int*)(isaac_arena_base+g_cpu.ebx), *(unsigned int*)(isaac_arena_base+g_cpu.ebx+4), *(unsigned int*)(isaac_arena_base+g_cpu.ebx+8)
printf "edi=0x%08x  [ebp-0x14]=0x%08x [ebp-0x1c]=0x%08x [ebp-0x20]=0x%08x [ebp-0x24]=0x%08x [ebp+8]=0x%08x\n", g_cpu.edi, *(unsigned int*)(isaac_arena_base+g_cpu.ebp-0x14), *(unsigned int*)(isaac_arena_base+g_cpu.ebp-0x1c), *(unsigned int*)(isaac_arena_base+g_cpu.ebp-0x20), *(unsigned int*)(isaac_arena_base+g_cpu.ebp-0x24), *(unsigned int*)(isaac_arena_base+g_cpu.ebp+8)
printf "arg elem [ebp+8] cap=[+0x14]=0x%08x size=[+0x10]=0x%08x data=[+0]=0x%08x\n", *(unsigned int*)(isaac_arena_base+*(unsigned int*)(isaac_arena_base+g_cpu.ebp+8)+0x14), *(unsigned int*)(isaac_arena_base+*(unsigned int*)(isaac_arena_base+g_cpu.ebp+8)+0x10), *(unsigned int*)(isaac_arena_base+*(unsigned int*)(isaac_arena_base+g_cpu.ebp+8))

printf "\n===== GUEST STACK (esp..esp+0x50) =====\n"
x/20xw isaac_arena_base + g_cpu.esp
GDB

cd "$OUT"
ISAAC_DATA_ROOT="$OUT/isaac-data" ISAAC_TRACE_MAX=200000 \
  timeout 300 gdb -batch -x "$DEMO/segv.gdb" --args ./isaac_host "$EXE" \
  > "$SCRATCH/segv_trace.log" 2>&1
echo "---- log written: $SCRATCH/segv_trace.log ($(wc -l < "$SCRATCH/segv_trace.log") lines)"
