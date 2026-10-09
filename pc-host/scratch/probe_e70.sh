#!/bin/bash
# gdb：sub_00563e70 是否命中 bucket；打印 key 路径与成功/失败分支。
set -u
PROJ=/mnt/f/TheBindingofIsaacRebirth
OUT=$PROJ/pc-host/build
SCRATCH=$PROJ/pc-host/scratch
EXE=$PROJ/isaac-ohos/entry/src/main/resources/rawfile/isaac-ng.exe
DEMO=/tmp/hp_e70
mkdir -p "$DEMO"

cat > "$DEMO/e70.gdb" <<'GDB'
set pagination off
set confirm off
set height 0

set $enter = 0
set $ok = 0
set $fail = 0

break sub_00563e70
commands
  silent
  set $enter = $enter + 1
  if $enter <= 8
    set $p = *(unsigned int *)(isaac_arena_base + c->ebp + 8)
    set $op = *(unsigned int *)(isaac_arena_base + c->ebp + 12)
    printf "[E70-%02d] this(ecx)=", $enter
    output c->ecx
    printf " key_ptr=0x%08x key=", $p
    x/s isaac_arena_base + $p
    printf "  out_ptr=0x%08x\n", $op
  end
  continue
end

break guest_0166.c:12308
commands
  silent
  set $ok = $ok + 1
  if $ok <= 8
    printf "[E70-OK%02d]\n", $ok
  end
  continue
end

break guest_0166.c:12332
commands
  silent
  set $fail = $fail + 1
  if $fail <= 8
    printf "[E70-FAIL%02d]\n", $fail
  end
  continue
end

run
printf "==== done: enter=%d ok=%d fail=%d ====\n", $enter, $ok, $fail
GDB

cd "$OUT"
ISAAC_DATA_ROOT="$OUT/isaac-data" timeout 120 gdb -batch -x "$DEMO/e70.gdb" \
    --args ./isaac_host "$EXE" > "$SCRATCH/e70_trace.log" 2>&1
echo "e70_trace.log: $SCRATCH/e70_trace.log"
grep -E "^\[E70|done" "$SCRATCH/e70_trace.log" | head -40