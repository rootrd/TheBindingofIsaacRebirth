#!/bin/bash
# gdb：抓卷容器三槽的写入现场；非 0 写入打印 backtrace，对比成功/失败链。
set -u
PROJ=/mnt/f/TheBindingofIsaacRebirth
OUT=$PROJ/pc-host/build
SCRATCH=$PROJ/pc-host/scratch
EXE=$PROJ/isaac-ohos/entry/src/main/resources/rawfile/isaac-ng.exe
DEMO=/tmp/hp_m2
mkdir -p "$DEMO"

cat > "$DEMO/m2.gdb" <<'GDB'
set pagination off
set confirm off
set height 0

break isaac_guest_run
run
delete 1

set $s0 = isaac_arena_base + 0x4000ec30
watch *(unsigned int *)($s0)
commands
  silent
  printf "SLOT0 <- "
  output *(unsigned int *)($s0)
  printf "\n"
  if *(unsigned int *)($s0) != 0
    bt 5
  end
  continue
end

set $s1 = isaac_arena_base + 0x4000ec34
watch *(unsigned int *)($s1)
commands
  silent
  printf "SLOT1 <- "
  output *(unsigned int *)($s1)
  printf "\n"
  if *(unsigned int *)($s1) != 0
    bt 5
  end
  continue
end

set $s2 = isaac_arena_base + 0x4000ec38
watch *(unsigned int *)($s2)
commands
  silent
  printf "SLOT2 <- "
  output *(unsigned int *)($s2)
  printf "\n"
  if *(unsigned int *)($s2) != 0
    bt 5
  end
  continue
end

continue
printf "---- done ----\n"
GDB

cd "$OUT"
ISAAC_DATA_ROOT="$OUT/isaac-data" timeout 150 gdb -batch -x "$DEMO/m2.gdb" \
    --args ./isaac_host "$EXE" > "$SCRATCH/m2_trace.log" 2>&1
grep -E "SLOT[012]|^#[0-9]" "$SCRATCH/m2_trace.log" | head -70
