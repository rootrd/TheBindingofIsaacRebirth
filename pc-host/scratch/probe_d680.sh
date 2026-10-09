#!/bin/bash
# gdb：sub_0025d680 入口现场——this 对象与 [eax+esi+0x38] 表读语义。
set -u
PROJ=/mnt/f/TheBindingofIsaacRebirth
OUT=$PROJ/pc-host/build
SCRATCH=$PROJ/pc-host/scratch
EXE=$PROJ/isaac-ohos/entry/src/main/resources/rawfile/isaac-ng.exe
DEMO=/tmp/hp_d680
mkdir -p "$DEMO"

cat > "$DEMO/d680.gdb" <<'GDB'
set pagination off
set confirm off
set height 0

set $n = 0

break guest_0061.c:19895
commands
  silent
  set $n = $n + 1
  if $n <= 24
    printf "[D%02d] this(esi)=", $n
    output c->esi
    printf " [this+0]="
    output *(unsigned int *)(isaac_arena_base + c->esi)
    printf " caller="
    output *(unsigned int *)(isaac_arena_base + c->esp)
    printf "\n"
    set $b = *(unsigned int *)(isaac_arena_base + c->esi)
    printf "     base obj: [+0]="
    output *(unsigned int *)(isaac_arena_base + $b)
    printf " [+4]="
    output *(unsigned int *)(isaac_arena_base + $b + 4)
    printf " [+8]="
    output *(unsigned int *)(isaac_arena_base + $b + 8)
    printf "\n"
    printf "     tbl[eax+esi+0x38] where eax="
    output c->eax
    printf "\n"
    printf "     entry val = "
    output *(unsigned int *)(isaac_arena_base + c->eax + c->esi + 0x38)
    printf "\n"
  end
  continue
end

run
printf "---- done ----\n"
GDB

cd "$OUT"
ISAAC_DATA_ROOT="$OUT/isaac-data" timeout 240 gdb -batch -x "$DEMO/d680.gdb" \
    --args ./isaac_host "$EXE" > "$SCRATCH/d680_trace.log" 2>&1
grep -E "^\[D[0-9]|     " "$SCRATCH/d680_trace.log" | head -40
