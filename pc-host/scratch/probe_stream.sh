#!/bin/bash
# gdb: dump the stream object the game builds (vtable + get-area pointers) so the
# MSVCP140 helper stubs can be written against the real field layout.
set -u
PROJ=/mnt/f/TheBindingofIsaacRebirth
OUT=$PROJ/pc-host/build
SCRATCH=$PROJ/pc-host/scratch
EXE=$PROJ/isaac-ohos/entry/src/main/resources/rawfile/isaac-ng.exe
DEMO=/tmp/hp_stream
mkdir -p "$DEMO"

cat > "$DEMO/stream.gdb" <<'GDB'
set pagination off
set confirm off
set height 0
set width 0

set $obj = 0

break guest_extra_002597b0.c:81
commands
  silent
  if $obj == 0
    set $obj = g_cpu.ebp - 0xec
    printf "\n=== stream object at 0x%08x (ebp=0x%08x) ===\n", $obj, g_cpu.ebp
    x/28xw isaac_arena_base + $obj
    printf "\n--- source string [ebp-0x28]: data/size/cap ---\n"
    printf "  [ebp-0x28]=0x%08x [ebp-0x1c]=0x%08x [ebp-0x18]=0x%08x\n", *(unsigned int*)(isaac_arena_base+g_cpu.ebp-0x28), *(unsigned int*)(isaac_arena_base+g_cpu.ebp-0x1c), *(unsigned int*)(isaac_arena_base+g_cpu.ebp-0x18)
    printf "\n--- vtable at 0x3073ee54 (first 12 entries) ---\n"
    x/12xw isaac_arena_base + 0x3073ee54
    printf "\n--- local string [ebp-0x2c] data/size/cap ---\n"
    printf "  [ebp-0x2c]=0x%08x [ebp-0x1c]=0x%08x [ebp-0x18]=0x%08x\n", *(unsigned int*)(isaac_arena_base+g_cpu.ebp-0x2c), *(unsigned int*)(isaac_arena_base+g_cpu.ebp-0x1c), *(unsigned int*)(isaac_arena_base+g_cpu.ebp-0x18)
  end
  continue
end

run
printf "\n---- done ----\n"
GDB

cd "$OUT"
ISAAC_DATA_ROOT="$OUT/isaac-data" \
  timeout 300 gdb -batch -x "$DEMO/stream.gdb" --args ./isaac_host "$EXE" \
  > "$SCRATCH/stream_trace.log" 2>&1
echo "---- log: $SCRATCH/stream_trace.log"
sed -n "$(grep -n '=== stream object' "$SCRATCH/stream_trace.log" | head -1 | cut -d: -f1),\$p" "$SCRATCH/stream_trace.log" | head -50
