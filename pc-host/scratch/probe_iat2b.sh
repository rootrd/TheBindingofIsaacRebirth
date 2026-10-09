#!/bin/bash
# gdb：FAIL#1 命中后（进程存活）对 IAT 槽 0x306064c0 设 HW watch，抓破坏写入者。
set -u
PROJ=/mnt/f/TheBindingofIsaacRebirth
OUT=$PROJ/pc-host/build
SCRATCH=$PROJ/pc-host/scratch
EXE=$PROJ/isaac-ohos/entry/src/main/resources/rawfile/isaac-ng.exe
DEMO=/tmp/hp_iat2
mkdir -p "$DEMO"

cat > "$DEMO/iat2.gdb" <<'GDB'
set pagination off
set confirm off
set height 0

break guest_0166.c:9490
run
delete 1

printf "[FAIL#1 hit] alive, slot before="
output *(unsigned int *)(isaac_arena_base + 0x306064c0)
printf "\n"

watch *(unsigned int *)(isaac_arena_base + 0x306064c0)
commands
  silent
  printf ">>> IAT WRITTEN, bt:\n"
  bt 8
  printf ">>> new value="
  output *(unsigned int *)(isaac_arena_base + 0x306064c0)
  printf "\n"
  detach
end

continue
printf "---- done ----\n"
GDB

cd "$OUT"
ISAAC_DATA_ROOT="$OUT/isaac-data" timeout 300 gdb -batch -x "$DEMO/iat2.gdb" \
    --args ./isaac_host "$EXE" > "$SCRATCH/iat2_trace.log" 2>&1
grep -B1 -A16 "FAIL#1 hit" "$SCRATCH/iat2_trace.log" | head -24
