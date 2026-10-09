#!/bin/bash
# gdb：断 FNV 哈希(sub_00563830)——打印被哈希的字符串。
set -u
PROJ=/mnt/f/TheBindingofIsaacRebirth
OUT=$PROJ/pc-host/build
SCRATCH=$PROJ/pc-host/scratch
EXE=$PROJ/isaac-ohos/entry/src/main/resources/rawfile/isaac-ng.exe
DEMO=/tmp/hp_fnv
mkdir -p "$DEMO"

cat > "$DEMO/fnv.gdb" <<'GDB'
set pagination off
set confirm off
set height 0

set $n = 0

break sub_00563830
commands
  silent
  set $n = $n + 1
  if $n <= 60
    printf "[FNV#%02d] str=", $n
    output (char *)(isaac_arena_base + c->ecx)
    printf " -> "
    # 算不了 FNV，让 python 事后算；这里打印哈希返回后的值没有——先记串
    printf "\n"
  end
  continue
end

run
printf "---- done ----\n"
GDB

cd "$OUT"
ISAAC_DATA_ROOT="$OUT/isaac-data" timeout 240 gdb -batch -x "$DEMO/fnv.gdb" \
    --args ./isaac_host "$EXE" > "$SCRATCH/fnv_trace.log" 2>&1
grep -E "^\[FNV" "$SCRATCH/fnv_trace.log" | head -60
