#!/bin/bash
# gdb：嵌套 watch——容器每次 PUSH 后，对刚写入的槽动态加 watch，抓清零者。
set -u
PROJ=/mnt/f/TheBindingofIsaacRebirth
OUT=$PROJ/pc-host/build
SCRATCH=$PROJ/pc-host/scratch
EXE=$PROJ/isaac-ohos/entry/src/main/resources/rawfile/isaac-ng.exe
DEMO=/tmp/hp_nest
mkdir -p "$DEMO"

cat > "$DEMO/nest.gdb" <<'GDB'
set pagination off
set confirm off
set height 0

break isaac_guest_run
run
delete 1

set $armed = 0

watch *(unsigned int *)(isaac_arena_base + 0x307c7b3c)
commands
  silent
  set $e = *(unsigned int *)(isaac_arena_base + 0x307c7b3c)
  set $b = *(unsigned int *)(isaac_arena_base + 0x307c7b38)
  if $e > $b
    if $armed == 0
      set $armed = 1
      set $slot = $e - 4
      printf "[NEST] arming watch on slot 0x%08x (val 0x%08x)\n", $slot, *(unsigned int *)(isaac_arena_base + $slot)
      watch *(unsigned int *)(isaac_arena_base + $slot)
      commands
        silent
        printf ">>> SLOT CHANGED val="
        output *(unsigned int *)(isaac_arena_base + $slot)
        printf "\n"
        bt 6
        detach
      end
      continue
    end
  end
  continue
end

continue
printf "---- done ----\n"
GDB

cd "$OUT"
ISAAC_DATA_ROOT="$OUT/isaac-data" timeout 240 gdb -batch -x "$DEMO/nest.gdb" \
    --args ./isaac_host "$EXE" > "$SCRATCH/nest_trace.log" 2>&1
grep -E "\[NEST\]|SLOT CHANGED|^#[0-5]" "$SCRATCH/nest_trace.log" | head -20
