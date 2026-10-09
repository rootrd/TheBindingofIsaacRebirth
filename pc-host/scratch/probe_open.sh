#!/bin/bash
# gdb：验证路径串是否传到 fopen 包装层 sub_00596360。
# 1) 断 sub_00596360 入口，打印 [ebp+8] 路径指针与文本。
# 2) 断 sub_00563db0 返回点（guest_0166.c:12131, mov eax,esi），打印返回值与其 vtable。
set -u
PROJ=/mnt/f/TheBindingofIsaacRebirth
OUT=$PROJ/pc-host/build
SCRATCH=$PROJ/pc-host/scratch
EXE=$PROJ/isaac-ohos/entry/src/main/resources/rawfile/isaac-ng.exe
DEMO=/tmp/hp_open
mkdir -p "$DEMO"

cat > "$DEMO/open.gdb" <<'GDB'
set pagination off
set confirm off
set height 0

set $fo = 0
set $db0ret = 0

break sub_00596360
commands
  silent
  set $fo = $fo + 1
  set $pp = *(unsigned int *)(isaac_arena_base + c->ebp + 8)
  printf "[FO%02d] path_ptr=0x%08x", $fo, $pp
  if $pp != 0
    printf " text="
    x/s isaac_arena_base + $pp
  end
  continue
end

break guest_0166.c:12131
commands
  silent
  set $db0ret = $db0ret + 1
  printf "[DB0ret%02d] esi=", $db0ret
  output c->esi
  printf " vtable="
  if c->esi != 0
    output *(unsigned int *)(isaac_arena_base + c->esi)
  else
    printf "NULL"
  end
  printf "\n"
  continue
end

run
printf "==== done: sub_00596360 hits=%d sub_00563db0 rets=%d ====\n", $fo, $db0ret
GDB

cd "$OUT"
ISAAC_DATA_ROOT="$OUT/isaac-data" ISAAC_TRACE_RESOLVE=1 timeout 120 gdb -batch -x "$DEMO/open.gdb" \
    --args ./isaac_host "$EXE" > "$SCRATCH/open_trace.log" 2>&1
echo "open_trace.log: $SCRATCH/open_trace.log"
grep -E "^\[FO|^\[DB0ret|done" "$SCRATCH/open_trace.log" | head -80