#!/bin/bash
# gdb：抓 0x4000ea90（dlc3 有效卷对象）完整 20 字节模板 + MATCHLOOKUP 现场的期望组。
set -u
PROJ=/mnt/f/TheBindingofIsaacRebirth
OUT=$PROJ/pc-host/build
SCRATCH=$PROJ/pc-host/scratch
EXE=$PROJ/isaac-ohos/entry/src/main/resources/rawfile/isaac-ng.exe
DEMO=/tmp/hp_tpl
mkdir -p "$DEMO"

cat > "$DEMO/tpl.gdb" <<'GDB'
set pagination off
set confirm off
set height 0

set $n = 0

break sub_00564aa0
run
delete 1

# 第二次 MATCHLOOKUP（容器已有 3 槽）时抓模板
set $n = $n + 1
while $n < 2
  set $n = $n + 1
  continue
end

set $b = *(unsigned int *)(isaac_arena_base + 0x307c7b38)
set $o2 = *(unsigned int *)(isaac_arena_base + $b + 8)
printf "=== dlc3 volume object @ 0x%08x ===\n", $o2
x/5xw isaac_arena_base + $o2
printf "name="
output (char *)(isaac_arena_base + *(unsigned int *)(isaac_arena_base + $o2 + 0xc))
printf "\nname-ptr="
output *(unsigned int *)(isaac_arena_base + $o2 + 0xc)
printf "\n"
printf "=== registry this 0x307c7b44 fields ===\n"
x/4xw isaac_arena_base + 0x307c7b44
continue
printf "---- done ----\n"
GDB

cd "$OUT"
ISAAC_DATA_ROOT="$OUT/isaac-data" timeout 240 gdb -batch -x "$DEMO/tpl.gdb" \
    --args ./isaac_host "$EXE" > "$SCRATCH/tpl_trace.log" 2>&1
grep -A14 "dlc3 volume object" "$SCRATCH/tpl_trace.log" | head -18
