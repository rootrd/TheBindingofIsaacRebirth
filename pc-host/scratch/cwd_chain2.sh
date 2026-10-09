#!/bin/bash
# gdb：抓 GetCurrentDirectoryA 的 guest 调用链（栈上两级返回地址）。
set -u
PROJ=/mnt/f/TheBindingofIsaacRebirth
OUT=$PROJ/pc-host/build
SCRATCH=$PROJ/pc-host/scratch
EXE=$PROJ/isaac-ohos/entry/src/main/resources/rawfile/isaac-ng.exe
DEMO=/tmp/hp_cwd
mkdir -p "$DEMO"

cat > "$DEMO/cwd.gdb" <<'GDB'
set pagination off
set confirm off
set height 0

break h_GetCurrentDirectoryA
commands
  silent
  printf "GETCWD ret0="
  output *(unsigned int *)(isaac_arena_base + c->esp)
  printf " ret1="
  output *(unsigned int *)(isaac_arena_base + c->esp + 4)
  printf " ret2="
  output *(unsigned int *)(isaac_arena_base + c->esp + 8)
  printf "\n"
  continue
end

run
printf "---- done ----\n"
GDB

cd "$OUT"
ISAAC_DATA_ROOT="$OUT/isaac-data" timeout 150 gdb -batch -x "$DEMO/cwd.gdb" \
    --args ./isaac_host "$EXE" > "$SCRATCH/cwd_trace.log" 2>&1
grep -E "GETCWD ret0" "$SCRATCH/cwd_trace.log" | head -5
