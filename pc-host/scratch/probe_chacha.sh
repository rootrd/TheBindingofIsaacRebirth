#!/bin/bash
# gdb：断 ChaCha init(sub_005e8fa0) 抓真实密钥（[ebp+0xc] 指向的 32 字节）。
set -u
PROJ=/mnt/f/TheBindingofIsaacRebirth
OUT=$PROJ/pc-host/build
SCRATCH=$PROJ/pc-host/scratch
EXE=$PROJ/isaac-ohos/entry/src/main/resources/rawfile/isaac-ng.exe
DEMO=/tmp/hp_cc
mkdir -p "$DEMO"

cat > "$DEMO/cc.gdb" <<'GDB'
set pagination off
set confirm off
set height 0

set $n = 0

break sub_005e8fa0
commands
  silent
  set $n = $n + 1
  if $n <= 8
    printf "[CC#%02d] this=", $n
    output c->ecx
    printf " keysrc=[ebp+0xc] addr\n"
    set $ks = *(unsigned int *)(isaac_arena_base + c->ebp + 0xc)
    printf "  key32 = "
    output/x *(unsigned long long *)(isaac_arena_base + $ks)
    printf " "
    output/x *(unsigned long long *)(isaac_arena_base + $ks + 8)
    printf "\n"
    printf "  nonce/blob: "
    output/x *(unsigned long long *)(isaac_arena_base + $ks + 16)
    printf " "
    output/x *(unsigned long long *)(isaac_arena_base + $ks + 24)
    printf "\n"
    printf "  arg3([ebp+0x10])="
    output *(unsigned int *)(isaac_arena_base + c->ebp + 0x10)
    printf " arg4([ebp+0x14])="
    output *(unsigned int *)(isaac_arena_base + c->ebp + 0x14)
    printf "\n"
    bt 4
  end
  continue
end

run
printf "---- done ----\n"
GDB

cd "$OUT"
ISAAC_DATA_ROOT="$OUT/isaac-data" timeout 300 gdb -batch -x "$DEMO/cc.gdb" \
    --args ./isaac_host "$EXE" > "$SCRATCH/cc_trace.log" 2>&1
grep -E "^\[CC|  key32|  nonce|  arg|^#[1-4]" "$SCRATCH/cc_trace.log" | head -50
