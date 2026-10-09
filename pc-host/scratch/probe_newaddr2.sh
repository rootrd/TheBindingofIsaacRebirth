#!/bin/bash
# gdb：跟踪 0x5eacf8（operator new）的返回地址分布——验证堆是否落在 .rdata 内。
set -u
PROJ=/mnt/f/TheBindingofIsaacRebirth
OUT=$PROJ/pc-host/build
SCRATCH=$PROJ/pc-host/scratch
EXE=$PROJ/isaac-ohos/entry/src/main/resources/rawfile/isaac-ng.exe
DEMO=/tmp/hp_new
mkdir -p "$DEMO"

cat > "$DEMO/new.gdb" <<'GDB'
set pagination off
set confirm off
set height 0

set $n = 0

break sub_005eacf8
commands
  silent
  set $n = $n + 1
  if $n <= 200
    printf "[NEW] ret-to="
    output *(unsigned int *)(isaac_arena_base + c->esp)
    printf "\n"
  end
  continue
end

# new 返回后（0x25bc0a）打印本次分配地址
break guest_0153.c:12419
commands
  silent
  printf "[NEW-RET-25bbc0] block="
  output c->eax
  printf "\n"
  continue
end

run
printf "---- done ----\n"
GDB

cd "$OUT"
ISAAC_DATA_ROOT="$OUT/isaac-data" timeout 240 gdb -batch -x "$DEMO/new.gdb" \
    --args ./isaac_host "$EXE" > "$SCRATCH/new_trace.log" 2>&1
echo "=== NEW 调用样本 ==="
grep -E "^\[NEW" "$SCRATCH/new_trace.log" | head -12
echo "=== 25bbc0 的块地址 ==="
grep -E "NEW-RET" "$SCRATCH/new_trace.log" | head -4
echo "=== 统计：落在 .rdata(0x30606000-0x307a8f5e) 的分配占比 ==="
grep -oE "ret-to=\]\s*$|ret-to=[0-9]+" "$SCRATCH/new_trace.log" | head -2
