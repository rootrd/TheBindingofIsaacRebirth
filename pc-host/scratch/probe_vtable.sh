#!/bin/bash
# gdb：追踪 vtable 从 sub_0059bfa0 设置(0x30765128)到 sub_00563db0 返回(0)的丢失点。
set -u
PROJ=/mnt/f/TheBindingofIsaacRebirth
OUT=$PROJ/pc-host/build
SCRATCH=$PROJ/pc-host/scratch
EXE=$PROJ/isaac-ohos/entry/src/main/resources/rawfile/isaac-ng.exe
DEMO=/tmp/hp_vt
mkdir -p "$DEMO"

cat > "$DEMO/vt.gdb" <<'GDB'
set pagination off
set confirm off
set height 0

# sub_0059bfa0 新建对象后（guest_0172.c:23808 = 设 vtable 下一行）
break guest_0172.c:23809
commands
  silent
  printf "[BFA0-setvt] edi="
  output c->edi
  printf " vtable="
  output *(unsigned int *)(isaac_arena_base + c->edi)
  printf "\n"
  continue
end

# sub_0059bfa0 返回点（guest_0172.c:23889 mov eax,edi）
break guest_0172.c:23889
commands
  silent
  printf "[BFA0-ret] edi="
  output c->edi
  printf " vtable="
  output *(unsigned int *)(isaac_arena_base + c->edi)
  printf "\n"
  continue
end

# sub_00563e70 写 out（guest_0166.c:12306 mov [esi],eax）
break guest_0166.c:12306
commands
  silent
  printf "[E70-writeout] eax="
  output c->eax
  printf " [eax]="
  output *(unsigned int *)(isaac_arena_base + c->eax)
  printf " esi(outptr)="
  output c->esi
  printf "\n"
  continue
end

# sub_00563db0 返回（guest_0166.c:12132 mov eax,esi）
break guest_0166.c:12132
commands
  silent
  printf "[DB0-ret] esi="
  output c->esi
  printf " [esi]="
  output *(unsigned int *)(isaac_arena_base + c->esi)
  printf "\n"
  continue
end

run
printf "==== done ====\n"
GDB

cd "$OUT"
ISAAC_DATA_ROOT="$OUT/isaac-data" timeout 120 gdb -batch -x "$DEMO/vt.gdb" \
    --args ./isaac_host "$EXE" > "$SCRATCH/vt_trace.log" 2>&1
echo "vt_trace.log: $SCRATCH/vt_trace.log"
grep -E "^\[BFA0|^\[E70|^\[DB0|done" "$SCRATCH/vt_trace.log" | head -60