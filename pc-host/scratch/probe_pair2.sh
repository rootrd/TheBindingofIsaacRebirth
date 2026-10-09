#!/bin/bash
# gdb：同轮对齐——C740 的 FindVol 调用实参 vs FindVol 入口收到的实参 vs 返回后 esi。
set -u
PROJ=/mnt/f/TheBindingofIsaacRebirth
OUT=$PROJ/pc-host/build
SCRATCH=$PROJ/pc-host/scratch
EXE=$PROJ/isaac-ohos/entry/src/main/resources/rawfile/isaac-ng.exe
DEMO=/tmp/hp_pair
mkdir -p "$DEMO"

cat > "$DEMO/pair.gdb" <<'GDB'
set pagination off
set confirm off
set height 0

# C740 内 call FindVol（14903 行）
break guest_0153.c:14903
commands
  silent
  printf "[PAIR] C740->FindVol this(ecx)="
  output c->ecx
  printf " edx="
  output c->edx
  printf " str_begin="
  output *(unsigned int *)(isaac_arena_base + c->edx)
  printf " str_end="
  output *(unsigned int *)(isaac_arena_base + c->edx + 4)
  printf " strtext="
  output (char *)(isaac_arena_base + *(unsigned int *)(isaac_arena_base + c->edx))
  printf "\n"
  continue
end

# FindVol 入口（15035 行 = 函数首）
break guest_0166.c:15035
commands
  silent
  printf "[PAIR] FINDVOL enter ecx="
  output c->ecx
  printf " edx="
  output c->edx
  printf "\n"
  continue
end

# FindVol 返回后（C740 内 14904 行附近 = mov esi,eax）——找 0x50c77d 的行
break guest_0153.c:14904
commands
  silent
  printf "[PAIR] C740 after-FindVol esi="
  output c->esi
  printf "\n"
  continue
end

run
printf "---- done ----\n"
GDB

cd "$OUT"
ISAAC_DATA_ROOT="$OUT/isaac-data" timeout 240 gdb -batch -x "$DEMO/pair.gdb" \
    --args ./isaac_host "$EXE" > "$SCRATCH/pair_trace.log" 2>&1
grep -E "^\[PAIR" "$SCRATCH/pair_trace.log" | head -20
