#!/bin/bash
# gdb：watch PersistentGameData vector 的 end 字段（0x3070C0D8），抓全部写入者。
set -u
PROJ=/mnt/f/TheBindingofIsaacRebirth
OUT=$PROJ/pc-host/build
SCRATCH=$PROJ/pc-host/scratch
EXE=$PROJ/isaac-ohos/entry/src/main/resources/rawfile/isaac-ng.exe
DEMO=/tmp/hp_vec
mkdir -p "$DEMO"

cat > "$DEMO/vec.gdb" <<'GDB'
set pagination off
set confirm off
set height 0

set $cnt = 0

break sub_0025bbc0
run
delete 1

watch *(unsigned int *)(isaac_arena_base + 0x3070c0d8)
commands
  silent
  set $cnt = $cnt + 1
  printf "[V%02d] [base+4] <- ", $cnt
  output *(unsigned int *)(isaac_arena_base + 0x3070c0d8)
  printf "\n"
  if $cnt <= 6
    bt 5
  end
  if $cnt >= 10
    detach
  end
  continue
end

continue
printf "---- done ----\n"
GDB

cd "$OUT"
ISAAC_DATA_ROOT="$OUT/isaac-data" timeout 300 gdb -batch -x "$DEMO/vec.gdb" \
    --args ./isaac_host "$EXE" > "$SCRATCH/vec_trace.log" 2>&1
grep -E "^\[V[0-9]|^#[0-4]" "$SCRATCH/vec_trace.log" | head -40
