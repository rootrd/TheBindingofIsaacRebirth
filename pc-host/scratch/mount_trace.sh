#!/bin/bash
# gdb：dump 挂载点容器内容 + 失败出口现场。
set -u
PROJ=/mnt/f/TheBindingofIsaacRebirth
OUT=$PROJ/pc-host/build
SCRATCH=$PROJ/pc-host/scratch
EXE=$PROJ/isaac-ohos/entry/src/main/resources/rawfile/isaac-ng.exe
DEMO=/tmp/hp_mount2
mkdir -p "$DEMO"

cat > "$DEMO/mount.gdb" <<'GDB'
set pagination off
set confirm off
set height 0

break sub_00564aa0
commands
  silent
  set $t = c->ecx
  set $b = *(unsigned int *)(isaac_arena_base + $t + 8)
  set $e = *(unsigned int *)(isaac_arena_base + $t + 0xc)
  printf "MATCHLOOKUP this=0x%08x begin=0x%08x end=0x%08x n=%d\n", $t, $b, $e, ($e - $b) / 4
  set $cnt = ($e - $b) / 4
  if $cnt > 0
    set $i = 0
    while $i < $cnt
      set $e0 = *(unsigned int *)(isaac_arena_base + $b + $i * 4)
      printf "  mount[%d]=0x%08x name='%s' f04=%d f10=%d\n", $i, $e0, (char *)(isaac_arena_base + *(unsigned int *)(isaac_arena_base + $e0 + 0xc)), *(unsigned int *)(isaac_arena_base + $e0 + 4), *(unsigned int *)(isaac_arena_base + $e0 + 0x10)
      set $i = $i + 1
    end
  end
  continue
end

break guest_0166.c:9485
commands
  silent
  printf "RESOLVE-FAIL\n"
  continue
end

break guest_0166.c:9363
commands
  silent
  printf "RESOLVE-OK %s\n", (char *)(isaac_arena_base + c->esi)
  continue
end

run
printf "---- done ----\n"
GDB

cd "$OUT"
ISAAC_DATA_ROOT="$OUT/isaac-data" timeout 150 gdb -batch -x "$DEMO/mount.gdb" \
    --args ./isaac_host "$EXE" > "$SCRATCH/mount_trace2.log" 2>&1
echo "== 前 60 条 =="
grep -E "MATCHLOOKUP|mount\[|RESOLVE" "$SCRATCH/mount_trace2.log" | head -60
