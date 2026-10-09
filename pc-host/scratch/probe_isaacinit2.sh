#!/bin/bash
# gdb：ISAAC 初始化全链跟踪——0x5c2f10(init)/0x5c30b0/0x5c2fd0(miniz桩)/0x5b06f0(refresh)/0x59c170(alloc)/0x5ec14c(throw)。
set -u
PROJ=/mnt/f/TheBindingofIsaacRebirth
OUT=$PROJ/pc-host/build
SCRATCH=$PROJ/pc-host/scratch
EXE=$PROJ/isaac-ohos/entry/src/main/resources/rawfile/isaac-ng.exe
DEMO=/tmp/hp_isaacc
mkdir -p "$DEMO"

cat > "$DEMO/isaacc.gdb" <<'GDB'
set pagination off
set confirm off
set height 0

break sub_005c2f10
commands
  silent
  printf "[INIT sub_005c2f10] hit\n"
  continue
end

break sub_005c30b0
commands
  silent
  printf "[ISAACINIT sub_005c30b0] hit\n"
  continue
end

break sub_005c2fd0
commands
  silent
  printf "[MINIZ-STUB sub_005c2fd0] hit\n"
  continue
end

break sub_005b06f0
commands
  silent
  printf "[REFRESH sub_005b06f0] hit\n"
  continue
end

break sub_005ec14c
commands
  silent
  printf "[THROW? sub_005ec14c] hit\n"
  continue
end

break sub_005aeb00
commands
  silent
  printf "[MINIZ sub_005aeb00] hit\n"
  continue
end

run
printf "---- done ----\n"
GDB

cd "$OUT"
ISAAC_DATA_ROOT="$OUT/isaac-data" timeout 240 gdb -batch -x "$DEMO/isaacc.gdb" \
    --args ./isaac_host "$EXE" > "$SCRATCH/isaacc_trace.log" 2>&1
grep -E "^\[" "$SCRATCH/isaacc_trace.log" | head -20
echo "=== 命中统计 ==="
for pat in "INIT sub" "ISAACINIT" "MINIZ-STUB" "REFRESH" "THROW" "MINIZ sub"; do echo -n "$pat: "; grep -c "$pat" "$SCRATCH/isaacc_trace.log"; done
