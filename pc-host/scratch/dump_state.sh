#!/bin/bash
set -u
PROJ=/mnt/f/TheBindingofIsaacRebirth
OUT=$PROJ/pc-host/build
cd "$OUT"
cat > /tmp/dst.gdb <<'GDB'
set pagination off
set confirm off
set $n = 0
break guest_0172.c:24512
commands
  silent
  set $n = $n + 1
  if $n <= 4
    set $obj = g_cpu.edi
    set $dec = *(unsigned int*)(isaac_arena_base + $obj + 0x14)
    set $st = *(unsigned int*)(isaac_arena_base + $dec)
    printf "\n=== refill #%d obj=%08x dec=%08x state=%08x mode=%d\n", $n, $obj, $dec, $st, *(unsigned char*)(isaac_arena_base + $dec + 0x10)
    dump binary memory /mnt/f/TheBindingofIsaacRebirth/pc-host/scratch/isaac_state.bin isaac_arena_base + $st isaac_arena_base + $st + 0x810
    dump binary memory /mnt/f/TheBindingofIsaacRebirth/pc-host/scratch/entry_in.bin isaac_arena_base + $obj + 0x1c isaac_arena_base + $obj + 0x1c + 0x200
  end
  continue
end
run
GDB
ISAAC_DATA_ROOT="$OUT/isaac-data" ISAAC_DBG_BUFFER=1 timeout 120 gdb -batch -x /tmp/dst.gdb \
  --args ./isaac_host "$PROJ/isaac-ohos/entry/src/main/resources/rawfile/isaac-ng.exe" 2>&1 |
  grep -a "refill #\|DBG" | head -12
