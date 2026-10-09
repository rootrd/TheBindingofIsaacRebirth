#!/bin/bash
# gdb：C740 手写 push（0050c7a6 写槽指令）与 C4F0 push（14503）每次命中打印
# 写入目标地址与值——与 FAIL 现场 slot 值对账。
set -u
PROJ=/mnt/f/TheBindingofIsaacRebirth
OUT=$PROJ/pc-host/build
SCRATCH=$PROJ/pc-host/scratch
EXE=$PROJ/isaac-ohos/entry/src/main/resources/rawfile/isaac-ng.exe
DEMO=/tmp/hp_cw
mkdir -p "$DEMO"

cat > "$DEMO/cw.gdb" <<'GDB'
set pagination off
set confirm off
set height 0

# 0050c790: eax = begin（clear 后的 begin 快照）
break guest_0153.c:14926
commands
  silent
  printf "[C740-W] begin="
  output c->eax
  printf " esi="
  output c->esi
  printf "\n"
  continue
end

# 0050c7a6 写槽指令（14935 行）：执行前打印 eax（目标）esi（值）
break guest_0153.c:14936
commands
  silent
  printf "[C740-STORE] target="
  output c->eax
  printf " val="
  output c->esi
  printf "\n"
  continue
end

# C4F0 push（14503 = call 0x564130）：进入前打印 this 与参数串
break guest_0153.c:14503
commands
  silent
  printf "[C4F0-ADDCALL] hit\n"
  continue
end

run
printf "---- done ----\n"
GDB

cd "$OUT"
ISAAC_DATA_ROOT="$OUT/isaac-data" timeout 240 gdb -batch -x "$DEMO/cw.gdb" \
    --args ./isaac_host "$EXE" > "$SCRATCH/cw_trace.log" 2>&1
grep -E "^\[C740|^\[C4F0" "$SCRATCH/cw_trace.log" | head -25
