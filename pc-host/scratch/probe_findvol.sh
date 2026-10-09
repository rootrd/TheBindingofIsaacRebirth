#!/bin/bash
# gdb：FindVol(sub_00564bd0) 两轮对比——注册表 this 字段、期望组、空串检查。
set -u
PROJ=/mnt/f/TheBindingofIsaacRebirth
OUT=$PROJ/pc-host/build
SCRATCH=$PROJ/pc-host/scratch
EXE=$PROJ/isaac-ohos/entry/src/main/resources/rawfile/isaac-ng.exe
DEMO=/tmp/hp_fv
mkdir -p "$DEMO"

cat > "$DEMO/fv.gdb" <<'GDB'
set pagination off
set confirm off
set height 0

set $n = 0

break guest_0166.c:15035
commands
  silent
  set $n = $n + 1
  if $n <= 6
    printf "FINDVOL#%d this=", $n
    output c->ecx
    printf " edx_obj="
    output c->edx
    printf " grp([this+4])="
    output *(unsigned int *)(isaac_arena_base + c->ecx + 4)
    printf " str.begin="
    output *(unsigned int *)(isaac_arena_base + c->edx)
    printf " str.end="
    output *(unsigned int *)(isaac_arena_base + c->edx + 4)
    printf "\n"
  end
  continue
end

# 未命中出口（L_00564c12，15096 行）
break guest_0166.c:15097
commands
  silent
  printf "  FV-miss\n"
  continue
end

# 命中出口（L_00564c1b，15099 行附近）
break guest_0166.c:15099
commands
  silent
  printf "  FV-hit\n"
  continue
end

run
printf "---- done ----\n"
GDB

cd "$OUT"
ISAAC_DATA_ROOT="$OUT/isaac-data" timeout 240 gdb -batch -x "$DEMO/fv.gdb" \
    --args ./isaac_host "$EXE" > "$SCRATCH/fv_trace.log" 2>&1
grep -E "^FINDVOL|FV-miss|FV-hit" "$SCRATCH/fv_trace.log" | head -25
