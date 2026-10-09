#!/bin/bash
# gdb：追踪 sub_00563db0 的 out 槽([esp+8])残留值来源。
set -u
PROJ=/mnt/f/TheBindingofIsaacRebirth
OUT=$PROJ/pc-host/build
SCRATCH=$PROJ/pc-host/scratch
EXE=$PROJ/isaac-ohos/entry/src/main/resources/rawfile/isaac-ng.exe
DEMO=/tmp/hp_out
mkdir -p "$DEMO"

cat > "$DEMO/out.gdb" <<'GDB'
set pagination off
set confirm off
set height 0

# 12045 mov [esp+0xc],0 之前/之后 esp 值
break guest_0166.c:12045
commands
  silent
  printf "[S45] esp=0x%08x [esp+8]=0x%08x [esp+0xc]=0x%08x\n", c->esp, *(unsigned int*)(isaac_arena_base + c->esp + 8), *(unsigned int*)(isaac_arena_base + c->esp + 12)
  continue
end

# sub_00563e70 调用后读 out（12062 mov esi,[esp+8] 之后）
break guest_0166.c:12062
commands
  silent
  printf "[S62] esp=0x%08x esi=0x%08x [esi]=", c->esp, c->esi
  if c->esi != 0
    output *(unsigned int*)(isaac_arena_base + c->esi)
  else
    printf "n/a"
  end
  printf " [esp+8]=0x%08x\n", *(unsigned int*)(isaac_arena_base + c->esp + 8)
  continue
end

run
printf "==== done ====\n"
GDB

cd "$OUT"
ISAAC_DATA_ROOT="$OUT/isaac-data" timeout 120 gdb -batch -x "$DEMO/out.gdb" \
    --args ./isaac_host "$EXE" > "$SCRATCH/out_trace.log" 2>&1
echo "out_trace.log: $SCRATCH/out_trace.log"
grep -E "^\[S|done" "$SCRATCH/out_trace.log" | head -30