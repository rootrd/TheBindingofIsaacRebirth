#!/bin/bash
set -u
PROJ=/mnt/f/TheBindingofIsaacRebirth
OUT=$PROJ/pc-host/build
SCRATCH=$PROJ/pc-host/scratch
EXE=$PROJ/isaac-ohos/entry/src/main/resources/rawfile/isaac-ng.exe
DEMO=/tmp/hp_peek
mkdir -p "$DEMO"
cat > "$DEMO/peek.gdb" <<'GDB'
set pagination off
set confirm off
set height 0

break sub_00564aa0
run
delete 1
printf "=== arena 0x306081d4 (48 bytes) ===\n"
x/48cb isaac_arena_base + 0x306081d4
printf "=== arena 0x3075abf8 (24 bytes, 'resources') ===\n"
x/24cb isaac_arena_base + 0x3075abf8
printf "=== arena 0x3075ac04 (24 bytes, 'resources-dlc3') ===\n"
x/24cb isaac_arena_base + 0x3075ac04
continue
printf "---- done ----\n"
GDB
cd "$OUT"
ISAAC_DATA_ROOT="$OUT/isaac-data" timeout 150 gdb -batch -x "$DEMO/peek.gdb" \
    --args ./isaac_host "$EXE" > "$SCRATCH/peek_arena.log" 2>&1
grep -A8 "arena 0x306081d4" "$SCRATCH/peek_arena.log" | head -10
grep -A5 "arena 0x3075abf8" "$SCRATCH/peek_arena.log" | head -7
grep -A5 "arena 0x3075ac04" "$SCRATCH/peek_arena.log" | head -7
