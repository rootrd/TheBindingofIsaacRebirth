#!/bin/bash
# gdb：卷创建链参数实况。
#   sub_005986f0  串构造/赋值（ecx=? edx=源? 栈=(0,1)?）——打印全部 + 源串内容
#   sub_00564130  FindOrAdd 卷（esp+4 = string*）——打印 begin/end/内容
set -u
PROJ=/mnt/f/TheBindingofIsaacRebirth
OUT=$PROJ/pc-host/build
SCRATCH=$PROJ/pc-host/scratch
EXE=$PROJ/isaac-ohos/entry/src/main/resources/rawfile/isaac-ng.exe
DEMO=/tmp/hp_str
mkdir -p "$DEMO"

cat > "$DEMO/str.gdb" <<'GDB'
set pagination off
set confirm off
set height 0

break sub_005986f0
commands
  silent
  set $src = c->edx
  printf "STRGEN ecx="
  output c->ecx
  printf " edx="
  output $src
  printf " a0="
  output *(unsigned int *)(isaac_arena_base + c->esp + 4)
  printf " a1="
  output *(unsigned int *)(isaac_arena_base + c->esp + 8)
  printf " srctext="
  output (char *)(isaac_arena_base + $src)
  printf "\n"
  continue
end

break sub_00564130
commands
  silent
  set $sp = *(unsigned int *)(isaac_arena_base + c->esp + 4)
  set $b = *(unsigned int *)(isaac_arena_base + $sp)
  set $e = *(unsigned int *)(isaac_arena_base + $sp + 4)
  printf "FINDADD sp=0x%08x begin=0x%08x end=0x%08x\n", $sp, $b, $e
  continue
end

run
printf "---- done ----\n"
GDB

cd "$OUT"
ISAAC_DATA_ROOT="$OUT/isaac-data" timeout 150 gdb -batch -x "$DEMO/str.gdb" \
    --args ./isaac_host "$EXE" > "$SCRATCH/str_trace.log" 2>&1
grep -E "^STRGEN|^FINDADD" "$SCRATCH/str_trace.log" | head -24
