#!/bin/bash
# gdb：断 length_error 构造（sub_0025c990）——抓抛出点完整栈链与消息参数。
set -u
PROJ=/mnt/f/TheBindingofIsaacRebirth
OUT=$PROJ/pc-host/build
SCRATCH=$PROJ/pc-host/scratch
EXE=$PROJ/isaac-ohos/entry/src/main/resources/rawfile/isaac-ng.exe
DEMO=/tmp/hp_throw
mkdir -p "$DEMO"

cat > "$DEMO/throw.gdb" <<'GDB'
set pagination off
set confirm off
set height 0

break sub_0025c990
run
delete 1

printf "=== length_error ctor hit ===\n"
printf "this(ecx)="
output c->ecx
printf "\nbt 12:\n"
bt 12
continue
printf "---- done ----\n"
GDB

cd "$OUT"
ISAAC_DATA_ROOT="$OUT/isaac-data" timeout 240 gdb -batch -x "$DEMO/throw.gdb" \
    --args ./isaac_host "$EXE" > "$SCRATCH/throw_trace.log" 2>&1
grep -A16 "length_error ctor hit" "$SCRATCH/throw_trace.log" | head -20
