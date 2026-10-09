#!/bin/bash
# gdb：跟踪归档读取序列（fread/fseek/ftell 入口参数，前 50 次）。
set -u
PROJ=/mnt/f/TheBindingofIsaacRebirth
OUT=$PROJ/pc-host/build
SCRATCH=$PROJ/pc-host/scratch
EXE=$PROJ/isaac-ohos/entry/src/main/resources/rawfile/isaac-ng.exe
DEMO=/tmp/hp_arch2
mkdir -p "$DEMO"

cat > "$DEMO/arch.gdb" <<'GDB'
set pagination off
set confirm off
set height 0

set $cnt = 0

break host_ext_file.c:619
commands
  silent
  set $cnt = $cnt + 1
  if $cnt <= 50
    printf "[FREAD] size="
    output *(unsigned int *)(isaac_arena_base + c->esp + 8)
    printf " x"
    output *(unsigned int *)(isaac_arena_base + c->esp + 12)
    printf "\n"
  end
  continue
end

break host_ext_file.c:947
commands
  silent
  if $cnt <= 50
    printf "[FSEEK] off="
    output *(int *)(isaac_arena_base + c->esp + 8)
    printf " whence="
    output *(unsigned int *)(isaac_arena_base + c->esp + 12)
    printf "\n"
  end
  continue
end

break host_ext_file.c:1007
commands
  silent
  if $cnt <= 50
    printf "[FTELL]\n"
  end
  continue
end

run
printf "---- done ----\n"
GDB

cd "$OUT"
ISAAC_DATA_ROOT="$OUT/isaac-data" timeout 300 gdb -batch -x "$DEMO/arch.gdb" \
    --args ./isaac_host "$EXE" > "$SCRATCH/arch_trace.log" 2>&1
grep -E "^\[(FREAD|FSEEK|FTELL)" "$SCRATCH/arch_trace.log" | head -45
