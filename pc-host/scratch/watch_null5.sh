#!/bin/bash
# gdb：根卷名溯源。h_GetCurrentDirectoryA 的调用者、0x306081d4 全局 string
# 的内容、sub_00564bd0 收到的名字。
set -u
PROJ=/mnt/f/TheBindingofIsaacRebirth
OUT=$PROJ/pc-host/build
SCRATCH=$PROJ/pc-host/scratch
EXE=$PROJ/isaac-ohos/entry/src/main/resources/rawfile/isaac-ng.exe
DEMO=/tmp/hp_root
mkdir -p "$DEMO"

cat > "$DEMO/root.gdb" <<'GDB'
set pagination off
set confirm off
set height 0

break h_GetCurrentDirectoryA
commands
  silent
  printf "GETCWD caller_rva=0x%08x cap=%d\n", *(unsigned int *)(isaac_arena_base + c->esp)
  continue
end

break sub_0050c740
commands
  silent
  set $b = *(unsigned int *)(isaac_arena_base + 0x306081d4)
  set $e = *(unsigned int *)(isaac_arena_base + 0x306081d4 + 4)
  printf "C740 ENTER rootstr begin=0x%08x end=0x%08x len=%d\n", $b, $e, $e - $b
  continue
end

break sub_00564bd0
commands
  silent
  set $s = *(unsigned int *)(isaac_arena_base + c->edx)
  set $t = *(unsigned int *)(isaac_arena_base + c->edx + 4)
  printf "FINDVOL begin=0x%08x end=0x%08x len=%d\n", $s, $t, $t - $s
  continue
end

break sub_00564130
commands
  silent
  printf "PUSHVOL this=0x%08x\n", c->ecx
  continue
end

run
printf "---- done ----\n"
GDB

cd "$OUT"
ISAAC_DATA_ROOT="$OUT/isaac-data" timeout 150 gdb -batch -x "$DEMO/root.gdb" \
    --args ./isaac_host "$EXE" > "$SCRATCH/root_trace.log" 2>&1
echo "== 前 40 条 =="
grep -E "GETCWD|C740 ENTER|FINDVOL|PUSHVOL" "$SCRATCH/root_trace.log" | head -40
echo "== GETCWD 计数 =="
grep -c GETCWD "$SCRATCH/root_trace.log"
