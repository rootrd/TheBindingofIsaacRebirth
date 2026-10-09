#!/bin/bash
# gdb：PersistentGameData fault 链全字段抓取。
set -u
PROJ=/mnt/f/TheBindingofIsaacRebirth
OUT=$PROJ/pc-host/build
SCRATCH=$PROJ/pc-host/scratch
EXE=$PROJ/isaac-ohos/entry/src/main/resources/rawfile/isaac-ng.exe
DEMO=/tmp/hp_pd
mkdir -p "$DEMO"

cat > "$DEMO/pd.gdb" <<'GDB'
set pagination off
set confirm off
set height 0

break guest_0061.c:19924
run
delete 1

printf "=== fault chain ===\n"
printf "this(esi)="
output c->esi
printf "\n"
printf "base [esi]="
output *(unsigned int *)(isaac_arena_base + c->esi)
printf "\n"
set $b = *(unsigned int *)(isaac_arena_base + c->esi)
printf "lvl2 [base+4]="
output *(unsigned int *)(isaac_arena_base + $b + 4)
printf "\n"
set $l2 = *(unsigned int *)(isaac_arena_base + $b + 4)
printf "entry [l2+esi+0x38]="
output *(unsigned int *)(isaac_arena_base + $l2 + c->esi + 0x38)
printf "\n"
set $ent = *(unsigned int *)(isaac_arena_base + $l2 + c->esi + 0x38)
printf "entry obj bytes: "
x/6xw isaac_arena_base + $ent
printf "caller [esp]="
output *(unsigned int *)(isaac_arena_base + c->esp)
printf "\n"
bt 6
continue
printf "---- done ----\n"
GDB

cd "$OUT"
ISAAC_DATA_ROOT="$OUT/isaac-data" timeout 240 gdb -batch -x "$DEMO/pd.gdb" \
    --args ./isaac_host "$EXE" > "$SCRATCH/pd_trace.log" 2>&1
grep -B1 -A16 "fault chain" "$SCRATCH/pd_trace.log" | head -22
