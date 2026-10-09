#!/bin/bash
# gdb 跟踪归档挂载链路（guest 状态经 CPU* c 访问）。
set -u
PROJ=/mnt/f/TheBindingofIsaacRebirth
OUT=$PROJ/pc-host/build
SCRATCH=$PROJ/pc-host/scratch
EXE=$PROJ/isaac-ohos/entry/src/main/resources/rawfile/isaac-ng.exe
DEMO=/tmp/hp_mount
mkdir -p "$DEMO"

cat > "$DEMO/mount.gdb" <<'GDB'
set pagination off
set confirm off
set height 0

break sub_00563880
commands
  silent
  set $n = *(unsigned int *)(isaac_arena_base + c->esp + 4)
  printf "MOUNT name=%s\n", (char *)(isaac_arena_base + $n)
  continue
end

break sub_00563120
commands
  silent
  set $p = *(unsigned int *)(isaac_arena_base + c->esp + 4)
  printf "  RESOLVE in=%s\n", (char *)(isaac_arena_base + $p)
  continue
end

break guest_0166.c:9363
commands
  silent
  printf "  RESOLVE ok -> %s\n", (char *)(isaac_arena_base + c->esi)
  continue
end

break guest_0166.c:9485
commands
  silent
  printf "  RESOLVE FAIL\n"
  continue
end

break sub_00564aa0
commands
  silent
  printf "    MATCHLOOKUP this=0x%08x\n", c->ecx
  continue
end

break sub_00596360
commands
  silent
  printf "    FILEOPEN obj=0x%08x\n", c->ecx
  continue
end

run
printf "---- done ----\n"
GDB

cd "$OUT"
ISAAC_DATA_ROOT="$OUT/isaac-data" timeout 150 gdb -batch -x "$DEMO/mount.gdb" \
    --args ./isaac_host "$EXE" > "$SCRATCH/mount_trace.log" 2>&1
echo "== MOUNT/RESOLVE/OPEN 计数 =="
grep -c "^MOUNT name" "$SCRATCH/mount_trace.log"
grep -c "RESOLVE ok" "$SCRATCH/mount_trace.log"
grep -c "RESOLVE FAIL" "$SCRATCH/mount_trace.log"
echo "== 前 50 条跟踪 =="
grep -E "^MOUNT name|^  RESOLVE|^    MATCHLOOKUP|^    FILEOPEN" "$SCRATCH/mount_trace.log" | head -50
