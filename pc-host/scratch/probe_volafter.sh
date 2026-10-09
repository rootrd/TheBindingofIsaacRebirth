#!/bin/bash
# gdb：load archives 完成后 dump 卷容器三卷完整字段（找归档句柄绑定）。
set -u
PROJ=/mnt/f/TheBindingofIsaacRebirth
OUT=$PROJ/pc-host/build
SCRATCH=$PROJ/pc-host/scratch
EXE=$PROJ/isaac-ohos/entry/src/main/resources/rawfile/isaac-ng.exe
DEMO=/tmp/hp_vol
mkdir -p "$DEMO"

cat > "$DEMO/vol.gdb" <<'GDB'
set pagination off
set confirm off
set height 0

# 在 shader 加载的 fopen（h_file_fopen 第 7 次左右 = shaders/）前 dump。
# 用 sub_00259f30（shader 路径构造函数）作断点。
break sub_00259f30
run
delete 1

printf "=== 卷容器 dump @shader-load time ===\n"
set $b = *(unsigned int *)(isaac_arena_base + 0x307c7b38)
set $e = *(unsigned int *)(isaac_arena_base + 0x307c7b3c)
printf "container begin=0x%08x end=0x%08x n=%d\n", $b, $e, ($e - $b) / 4
set $i = 0
while $i < ($e - $b) / 4
  set $o = *(unsigned int *)(isaac_arena_base + $b + $i * 4)
  printf "vol[%d] obj=0x%08x: ", $i, $o
  if $o != 0
    output/x *(unsigned long long *)(isaac_arena_base + $o)
    printf " "
    output/x *(unsigned long long *)(isaac_arena_base + $o + 8)
    printf " "
    output/x *(unsigned long long *)(isaac_arena_base + $o + 0x10)
    printf "\n"
    printf "   name="
    output (char *)(isaac_arena_base + *(unsigned int *)(isaac_arena_base + $o + 0xc))
    printf "\n"
  else
    printf "NULL\n"
  end
  set $i = $i + 1
end
printf "=== registry 0x307c7b44 ===\n"
printf "reg begin="
output *(unsigned int *)(isaac_arena_base + 0x307c7b44)
printf " end="
output *(unsigned int *)(isaac_arena_base + 0x307c7b48)
printf "\n"
continue
printf "---- done ----\n"
GDB

cd "$OUT"
ISAAC_DATA_ROOT="$OUT/isaac-data" timeout 240 gdb -batch -x "$DEMO/vol.gdb" \
    --args ./isaac_host "$EXE" > "$SCRATCH/vol_trace.log" 2>&1
grep -A24 "卷容器 dump" "$SCRATCH/vol_trace.log" | head -28
