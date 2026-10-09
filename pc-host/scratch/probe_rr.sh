#!/bin/bash
# gdb：在 sub_00563db0 内部观测 resolve 结果(edi)与传给 sub_00563e70 的 key。
set -u
PROJ=/mnt/f/TheBindingofIsaacRebirth
OUT=$PROJ/pc-host/build
SCRATCH=$PROJ/pc-host/scratch
EXE=$PROJ/isaac-ohos/entry/src/main/resources/rawfile/isaac-ng.exe
DEMO=/tmp/hp_rr
mkdir -p "$DEMO"

cat > "$DEMO/rr.gdb" <<'GDB'
set pagination off
set confirm off
set height 0

# resolve 后（guest_0166.c:12050 mov edi,eax）
break guest_0166.c:12050
commands
  silent
  printf "[R-movedi] eax=0x%08x text=", c->eax
  x/s isaac_arena_base + c->eax
  continue
end

# push edi 前（guest_0166.c:12057 注释行）
break guest_0166.c:12058
commands
  silent
  printf "[R-push] edi=0x%08x text=", c->edi
  x/s isaac_arena_base + c->edi
  continue
end

run
printf "==== done ====\n"
GDB

cd "$OUT"
ISAAC_DATA_ROOT="$OUT/isaac-data" timeout 120 gdb -batch -x "$DEMO/rr.gdb" \
    --args ./isaac_host "$EXE" > "$SCRATCH/rr_trace.log" 2>&1
echo "rr_trace.log: $SCRATCH/rr_trace.log"
grep -E "^\[R-|done" "$SCRATCH/rr_trace.log" | head -30