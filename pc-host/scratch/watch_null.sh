#!/bin/bash
# gdb：抓"往卷容器写入 NULL 条目"的现场。
set -u
PROJ=/mnt/f/TheBindingofIsaacRebirth
OUT=$PROJ/pc-host/build
SCRATCH=$PROJ/pc-host/scratch
EXE=$PROJ/isaac-ohos/entry/src/main/resources/rawfile/isaac-ng.exe
DEMO=/tmp/hp_watchn
mkdir -p "$DEMO"

cat > "$DEMO/watchn.gdb" <<'GDB'
set pagination off
set confirm off
set height 0

break isaac_guest_run
run
delete 1

set $vec_begin_ptr = isaac_arena_base + 0x307c7b38
watch *(unsigned int *)(isaac_arena_base + 0x307c7b3c)
commands
  silent
  set $end = *(unsigned int *)(isaac_arena_base + 0x307c7b3c)
  set $begin = *(unsigned int *)($vec_begin_ptr)
  if $end > $begin
    if *(unsigned int *)(isaac_arena_base + $end - 4) == 0
      printf ">>> PUSH NULL captured, end=0x%08x n=%d\n", $end, ($end - $begin) / 4
      bt 8
      detach
    end
  end
  continue
end

continue
printf "---- done ----\n"
GDB

cd "$OUT"
ISAAC_DATA_ROOT="$OUT/isaac-data" timeout 150 gdb -batch -x "$DEMO/watchn.gdb" \
    --args ./isaac_host "$EXE" > "$SCRATCH/watch_null3.log" 2>&1
grep -A10 "PUSH NULL captured" "$SCRATCH/watch_null3.log" | head -14
