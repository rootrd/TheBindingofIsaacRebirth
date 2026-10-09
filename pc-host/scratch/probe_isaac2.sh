#!/bin/bash
# gdb：ISAAC 解密初始化链跟踪——sub_0059c170(分配)/sub_005c2fd0(解码)/sub_005b06f0(refresh)。
set -u
PROJ=/mnt/f/TheBindingofIsaacRebirth
OUT=$PROJ/pc-host/build
SCRATCH=$PROJ/pc-host/scratch
EXE=$PROJ/isaac-ohos/entry/src/main/resources/rawfile/isaac-ng.exe
DEMO=/tmp/hp_isaac
mkdir -p "$DEMO"

cat > "$DEMO/isaac.gdb" <<'GDB'
set pagination off
set confirm off
set height 0

break sub_0059c170
commands
  silent
  printf "[ALLOC sub_0059c170] ecx="
  output c->ecx
  printf " edx="
  output c->edx
  printf " eax="
  output c->eax
  printf "\n"
  continue
end

break sub_005c2fd0
commands
  silent
  printf "[XOR sub_005c2fd0] ecx="
  output c->ecx
  printf " edx="
  output c->edx
  printf " eax="
  output c->eax
  printf "\n"
  continue
end

break sub_005b06f0
commands
  silent
  printf "[REFRESH sub_005b06f0] ecx="
  output c->ecx
  printf "\n"
  continue
end

run
printf "---- done ----\n"
GDB

cd "$OUT"
ISAAC_DATA_ROOT="$OUT/isaac-data" timeout 240 gdb -batch -x "$DEMO/isaac.gdb" \
    --args ./isaac_host "$EXE" > "$SCRATCH/isaac_trace.log" 2>&1
grep -E "^\[(ALLOC|XOR|REFRESH)" "$SCRATCH/isaac_trace.log" | head -20
echo "=== 命中统计 ==="
grep -cE "^\[ALLOC" "$SCRATCH/isaac_trace.log"; grep -cE "^\[XOR" "$SCRATCH/isaac_trace.log"; grep -cE "^\[REFRESH" "$SCRATCH/isaac_trace.log"
