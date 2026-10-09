#!/bin/bash
# gdb：File::Read 虚调用 fault 现场——ebx（File 对象）与 [ebx] 8 字节定性。
set -u
PROJ=/mnt/f/TheBindingofIsaacRebirth
OUT=$PROJ/pc-host/build
SCRATCH=$PROJ/pc-host/scratch
EXE=$PROJ/isaac-ohos/entry/src/main/resources/rawfile/isaac-ng.exe
DEMO=/tmp/hp_rf
mkdir -p "$DEMO"

cat > "$DEMO/rf.gdb" <<'GDB'
set pagination off
set confirm off
set height 0

break guest_0166.c:14264
run
delete 1

printf "READ-SITE ebx="
output c->ebx
printf "\n[ebx] vtable="
output *(unsigned int *)(isaac_arena_base + c->ebx)
printf "\nobj bytes: "
x/5xw isaac_arena_base + c->ebx
printf "ebx+0xc name/file-ptr="
output *(unsigned int *)(isaac_arena_base + c->ebx + 0xc)
printf "\n"
continue
printf "---- done ----\n"
GDB

cd "$OUT"
ISAAC_DATA_ROOT="$OUT/isaac-data" ISAAC_TRACE_RESOLVE=1 timeout 240 gdb -batch -x "$DEMO/rf.gdb" \
    --args ./isaac_host "$EXE" > "$SCRATCH/rf_trace.log" 2>&1
grep -A8 "READ-SITE" "$SCRATCH/rf_trace.log" | head -12
