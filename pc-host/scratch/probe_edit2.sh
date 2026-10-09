#!/bin/bash
# gdb：edi 污染二分——各关键点打印 edi。
set -u
PROJ=/mnt/f/TheBindingofIsaacRebirth
OUT=$PROJ/pc-host/build
SCRATCH=$PROJ/pc-host/scratch
EXE=$PROJ/isaac-ohos/entry/src/main/resources/rawfile/isaac-ng.exe
DEMO=/tmp/hp_edi
mkdir -p "$DEMO"

cat > "$DEMO/edi.gdb" <<'GDB'
set pagination off
set confirm off
set height 0

break guest_0153.c:12406
commands
  silent
  printf "[E1-LOAD] edi="
  output c->edi
  printf "\n"
  continue
end

break guest_0153.c:12416
commands
  silent
  printf "[E2-call-edi-1 after] edi="
  output c->edi
  printf " eax="
  output c->eax
  printf "\n"
  continue
end

break guest_0153.c:12424
commands
  silent
  printf "[E3-after-0x2c3d50] edi="
  output c->edi
  printf "\n"
  continue
end

break guest_0153.c:12572
commands
  silent
  printf "[E4-call-edi-2] edi="
  output c->edi
  printf "\n"
  continue
end

break guest_0153.c:12612
commands
  silent
  printf "[E5-before-Mount] edi="
  output c->edi
  printf "\n"
  continue
end

break guest_0153.c:12614
commands
  silent
  printf "[E6-call-edi-3] edi="
  output c->edi
  printf "\n"
  continue
end

run
printf "---- done ----\n"
GDB

cd "$OUT"
ISAAC_DATA_ROOT="$OUT/isaac-data" timeout 240 gdb -batch -x "$DEMO/edi.gdb" \
    --args ./isaac_host "$EXE" > "$SCRATCH/edi_trace.log" 2>&1
grep -E "^\[E[1-6]" "$SCRATCH/edi_trace.log" | head -20
