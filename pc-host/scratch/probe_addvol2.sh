#!/bin/bash
# gdb：AddVolume(sub_00564130) 全出口跟踪——第 5/6 次调用（第三轮 resources/dlc3）走哪条出口。
set -u
PROJ=/mnt/f/TheBindingofIsaacRebirth
OUT=$PROJ/pc-host/build
SCRATCH=$PROJ/pc-host/scratch
EXE=$PROJ/isaac-ohos/entry/src/main/resources/rawfile/isaac-ng.exe
DEMO=/tmp/hp_add
mkdir -p "$DEMO"

cat > "$DEMO/add.gdb" <<'GDB'
set pagination off
set confirm off
set height 0

set $n = 0

break guest_0166.c:12723
commands
  silent
  set $n = $n + 1
  set $sp = *(unsigned int *)(isaac_arena_base + c->esp + 4)
  set $b = *(unsigned int *)(isaac_arena_base + $sp)
  printf "ADD#%d str_begin=0x%08x text=%s\n", $n, $b, (char *)(isaac_arena_base + $b)
  continue
end

# 出口 A：12769 行（空串 → 直接 ret，不 push）
break guest_0166.c:12770
commands
  silent
  printf "  exit: EMPTY-STR (no push)\n"
  continue
end

# 出口 B：12786 行（registry 已存在 → ret，不 push）
break guest_0166.c:12787
commands
  silent
  printf "  exit: ALREADY-IN-REG1 (no push) found="
  output c->eax
  printf "\n"
  continue
end

# 出口 C：12805 行（第二表已存在 → push 已有对象）
break guest_0166.c:12806
commands
  silent
  printf "  exit: FOUND-IN-REG2 push-existing="
  output c->edi
  printf "\n"
  continue
end

run
printf "---- done ----\n"
GDB

cd "$OUT"
ISAAC_DATA_ROOT="$OUT/isaac-data" timeout 240 gdb -batch -x "$DEMO/add.gdb" \
    --args ./isaac_host "$EXE" > "$SCRATCH/add_trace.log" 2>&1
grep -E "^ADD#|exit:" "$SCRATCH/add_trace.log" | head -30
