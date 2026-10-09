#!/bin/bash
set -u
PROJ=/mnt/f/TheBindingofIsaacRebirth
OUT=$PROJ/pc-host/build
cd "$OUT"
cat > /tmp/dst2.gdb <<'GDB'
set pagination off
set confirm off
set $n = 0
break guest_0172.c:24512
commands
  silent
  set $n = $n + 1
  if $n == 2
    set $obj = g_cpu.edi
    set $dec = *(unsigned int*)(isaac_arena_base + $obj + 0x14)
    set $st = *(unsigned int*)(isaac_arena_base + $dec)
    set $hst = isaac_arena_base + $st
    set $hin = isaac_arena_base + $obj + 0x1c
    printf "=== refill#2 obj=%08x dec=%08x state=%08x host_state=%p host_in=%p\n", $obj, $dec, $st, $hst, $hin
    dump binary memory /mnt/f/TheBindingofIsaacRebirth/pc-host/scratch/isaac_state.bin $hst ($hst + 0x810)
    dump binary memory /mnt/f/TheBindingofIsaacRebirth/pc-host/scratch/entry_in.bin $hin ($hin + 0x400)
  end
  continue
end
run
GDB
ISAAC_DATA_ROOT="$OUT/isaac-data" ISAAC_DBG_BUFFER=1 timeout 120 gdb -batch -x /tmp/dst2.gdb \
  --args ./isaac_host "$PROJ/isaac-ohos/entry/src/main/resources/rawfile/isaac-ng.exe" 2>&1 |
  grep -a "refill#\|dump\|Error\|error" | head -8
