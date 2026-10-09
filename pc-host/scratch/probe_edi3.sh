#!/bin/bash
# gdb：污染窗口细粒度二分——窗口内每个 call 后打印 edi。
set -u
PROJ=/mnt/f/TheBindingofIsaacRebirth
OUT=$PROJ/pc-host/build
SCRATCH=$PROJ/pc-host/scratch
EXE=$PROJ/isaac-ohos/entry/src/main/resources/rawfile/isaac-ng.exe
DEMO=/tmp/hp_e2
mkdir -p "$DEMO"

cat > "$DEMO/e2.gdb" <<'GDB'
set pagination off
set confirm off
set height 0

break guest_0153.c:12436
commands
  silent
  printf "[a12436] edi="
  output c->edi
  printf "\n"
  continue
end
break guest_0153.c:12451
commands
  silent
  printf "[b12451] edi="
  output c->edi
  printf "\n"
  continue
end
break guest_0153.c:12473
commands
  silent
  printf "[c12473] edi="
  output c->edi
  printf "\n"
  continue
end
break guest_0153.c:12489
commands
  silent
  printf "[d12489] edi="
  output c->edi
  printf "\n"
  continue
end
break guest_0153.c:12543
commands
  silent
  printf "[e12543] edi="
  output c->edi
  printf "\n"
  continue
end
break guest_0153.c:12572
commands
  silent
  printf "[f12572] edi="
  output c->edi
  printf "\n"
  continue
end

run
printf "---- done ----\n"
GDB

cd "$OUT"
ISAAC_DATA_ROOT="$OUT/isaac-data" timeout 240 gdb -batch -x "$DEMO/e2.gdb" \
    --args ./isaac_host "$EXE" > "$SCRATCH/e2_trace.log" 2>&1
grep -E "^\[a-f]" "$SCRATCH/e2_trace.log" | head -14
