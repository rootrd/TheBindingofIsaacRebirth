#!/bin/bash
# gdb watchpoint：抓 mount[1] 槽（被写成 NULL 的那个）的写入现场 + backtrace。
set -u
PROJ=/mnt/f/TheBindingofIsaacRebirth
OUT=$PROJ/pc-host/build
SCRATCH=$PROJ/pc-host/scratch
EXE=$PROJ/isaac-ohos/entry/src/main/resources/rawfile/isaac-ng.exe
DEMO=/tmp/hp_watch
mkdir -p "$DEMO"

cat > "$DEMO/watch.gdb" <<'GDB'
set pagination off
set confirm off
set height 0

# 等 guest 内存初始化完成
break isaac_guest_run
run
delete 1

# 最终 vector 块地址在 MOUNT 前可能经历 realloc；watch 三个槽位都不行，
# 所以 watch begin 指针（0x307c7b38）的每次变化 + 第三个槽的写入。
set $vec_begin_ptr = isaac_arena_base + 0x307c7b38
watch *(unsigned int *)($vec_begin_ptr)
commands
  silent
  printf "VEC begin changed -> 0x%08x  (host pc=%p)\n", *(unsigned int *)($vec_begin_ptr), $pc
  bt 6
  continue
end

# 抓第三槽写 NULL：先在第一次 MATCHLOOKUP 时拿 begin 值不可行，改为
# watch MATCHLOOKUP 之前容器计数（0x307c7b3c=end）——这里 watch end 指针变化。
set $vec_end_ptr = isaac_arena_base + 0x307c7b3c
watch *(unsigned int *)($vec_end_ptr)
commands
  silent
  printf "VEC end changed -> 0x%08x (n=%d) host pc=%p\n", *(unsigned int *)($vec_end_ptr), (*(unsigned int *)($vec_end_ptr) - *(unsigned int *)($vec_begin_ptr)) / 4, $pc
  bt 6
  continue
end

continue
printf "---- done ----\n"
GDB

cd "$OUT"
ISAAC_DATA_ROOT="$OUT/isaac-data" timeout 150 gdb -batch -x "$DEMO/watch.gdb" \
    --args ./isaac_host "$EXE" > "$SCRATCH/watch_null.log" 2>&1
echo "== VEC 事件 =="
grep -E "VEC (begin|end)" "$SCRATCH/watch_null.log" | head -20
echo "== 首个事件 backtrace =="
grep -A7 "VEC end changed" "$SCRATCH/watch_null.log" | head -30
