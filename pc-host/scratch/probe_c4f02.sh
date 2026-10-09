#!/bin/bash
# gdb：跟踪 C4F0 的实际执行路径——入口 → 是否到达 0x50c51c（构造）→ 0x50c549（push）
# 带调用者识别。只跟前 6 次。
set -u
PROJ=/mnt/f/TheBindingofIsaacRebirth
OUT=$PROJ/pc-host/build
SCRATCH=$PROJ/pc-host/scratch
EXE=$PROJ/isaac-ohos/entry/src/main/resources/rawfile/isaac-ng.exe
DEMO=/tmp/hp_c4f0
mkdir -p "$DEMO"

cat > "$DEMO/c4f0.gdb" <<'GDB'
set pagination off
set confirm off
set height 0

break sub_0050c4f0
commands
  silent
  printf "C4F0 enter arg_c="
  output c->ecx
  printf " caller="
  output *(unsigned int *)(isaac_arena_base + c->esp)
  printf "\n"
  continue
end

break guest_0153.c:14476
commands
  silent
  printf "  reached CONSTRUCT (0x50c51c)\n"
  continue
end

break guest_0153.c:14502
commands
  silent
  set $sa = *(unsigned int *)(isaac_arena_base + c->ebp + -120)
  printf "  reached PUSH14502 string_begin="
  output $sa
  printf "\n"
  continue
end

break guest_0153.c:14523
commands
  silent
  printf "  reached DLC-CHECK (0x50c56f)\n"
  continue
end

run
printf "---- done ----\n"
GDB

cd "$OUT"
ISAAC_DATA_ROOT="$OUT/isaac-data" timeout 150 gdb -batch -x "$DEMO/c4f0.gdb" \
    --args ./isaac_host "$EXE" > "$SCRATCH/c4f0_trace.log" 2>&1
grep -E "^C4F0 enter|reached" "$SCRATCH/c4f0_trace.log" | head -30
