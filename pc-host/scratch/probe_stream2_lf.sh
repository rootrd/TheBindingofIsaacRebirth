#!/bin/bash
# gdb: sample the game's stream object across loop iterations. The field that
# advances is _Gnext; the field it converges to is _Glast.
set -u
PROJ=/mnt/f/TheBindingofIsaacRebirth
OUT=$PROJ/pc-host/build
SCRATCH=$PROJ/pc-host/scratch
EXE=$PROJ/isaac-ohos/entry/src/main/resources/rawfile/isaac-ng.exe
DEMO=/tmp/hp_stream2
mkdir -p "$DEMO"

cat > "$DEMO/stream2.gdb" <<'GDB'
set pagination off
set confirm off
set height 0
set width 0

set $obj = 0
set $n = 0

break guest_extra_002597b0.c:137
commands
  silent
  set $n = $n + 1
  if $obj == 0
    set $obj = g_cpu.ebp - 0xec
    set $caller = *(unsigned int*)(isaac_arena_base + g_cpu.ebp)
    printf "\n=== object 0x%08x ; caller string data=0x%08x size=%u cap=%u\n", $obj, *(unsigned int*)(isaac_arena_base+$caller-0x28), *(unsigned int*)(isaac_arena_base+$caller-0x18), *(unsigned int*)(isaac_arena_base+$caller-0x14)
    printf "    text head: "
    x/16cb isaac_arena_base + *(unsigned int*)(isaac_arena_base+$caller-0x28)
  end
  if $n <= 6
    printf "[iter %d] ", $n
    x/24xw isaac_arena_base + $obj
  end
  if $n >= 6
    disable 1
  end
  continue
end

run
printf "\n---- done, iterations sampled=%d ----\n", $n
GDB

cd "$OUT"
ISAAC_DATA_ROOT="$OUT/isaac-data" \
  timeout 300 gdb -batch -x "$DEMO/stream2.gdb" --args ./isaac_host "$EXE" \
  > "$SCRATCH/stream2_trace.log" 2>&1
echo "---- log: $SCRATCH/stream2_trace.log"
sed -n "$(grep -n '=== object' "$SCRATCH/stream2_trace.log" | head -1 | cut -d: -f1),+60p" "$SCRATCH/stream2_trace.log"
