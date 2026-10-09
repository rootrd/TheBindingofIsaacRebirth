#!/bin/bash
# gdb：断 miniz 解压入口 sub_005aeb00（唯一调用点 0x59c3f3）。
set -u
PROJ=/mnt/f/TheBindingofIsaacRebirth
OUT=$PROJ/pc-host/build
SCRATCH=$PROJ/pc-host/scratch
EXE=$PROJ/isaac-ohos/entry/src/main/resources/rawfile/isaac-ng.exe
DEMO=/tmp/hp_mz2
mkdir -p "$DEMO"

cat > "$DEMO/mz2.gdb" <<'GDB'
set pagination off
set confirm off
set height 0

break sub_005aeb00
commands
  silent
  printf "[MZ] state="
  output c->ecx
  printf " input="
  output c->edx
  printf "\n"
  continue
end

run
printf "---- done ----\n"
GDB

cd "$OUT"
ISAAC_DATA_ROOT="$OUT/isaac-data" timeout 240 gdb -batch -x "$DEMO/mz2.gdb" \
    --args ./isaac_host "$EXE" > "$SCRATCH/mz2_trace.log" 2>&1
grep -E "^\[MZ" "$SCRATCH/mz2_trace.log" | head -12
echo "=== hits ==="
grep -c "^\[MZ" "$SCRATCH/mz2_trace.log"
