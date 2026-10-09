#!/bin/bash
# gdb：断名字哈希比对点（guest_0166.c:11522）——看两侧哈希值与匹配率。
set -u
PROJ=/mnt/f/TheBindingofIsaacRebirth
OUT=$PROJ/pc-host/build
SCRATCH=$PROJ/pc-host/scratch
EXE=$PROJ/isaac-ohos/entry/src/main/resources/rawfile/isaac-ng.exe
DEMO=/tmp/hp_hc
mkdir -p "$DEMO"

cat > "$DEMO/hc.gdb" <<'GDB'
set pagination off
set confirm off
set height 0

set $n = 0
set $match = 0
set $miss = 0

break guest_0166.c:11522
commands
  silent
  set $n = $n + 1
  if $n <= 40
    printf "[HC#%02d] caller_eax=", $n
    output c->eax
    printf " file_val="
    output *(unsigned int *)(isaac_arena_base + c->esi)
    printf "\n"
  end
  continue
end

run
printf "---- done ----\n"
GDB

cd "$OUT"
ISAAC_DATA_ROOT="$OUT/isaac-data" timeout 240 gdb -batch -x "$DEMO/hc.gdb" \
    --args ./isaac_host "$EXE" > "$SCRATCH/hc_trace.log" 2>&1
grep -E "^\[HC" "$SCRATCH/hc_trace.log" | head -40
echo "=== 总命中 ==="
grep -c "^\[HC" "$SCRATCH/hc_trace.log"
