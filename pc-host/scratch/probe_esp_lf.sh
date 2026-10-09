#!/bin/bash
# gdb: esp trace through sub_0025c990's body to find where the stack balance breaks.
set -u
PROJ=/mnt/f/TheBindingofIsaacRebirth
OUT=$PROJ/pc-host/build
SCRATCH=$PROJ/pc-host/scratch
EXE=$PROJ/isaac-ohos/entry/src/main/resources/rawfile/isaac-ng.exe
DEMO=/tmp/hp_esp
mkdir -p "$DEMO"

cat > "$DEMO/esp.gdb" <<'GDB'
set pagination off
set confirm off
set height 0
set width 0

define esp_at
  printf "%-28s esp=0x%08x esi=0x%08x edi=0x%08x ebx=0x%08x\n", $arg0, g_cpu.esp, g_cpu.esi, g_cpu.edi, g_cpu.ebx
end

break guest_0061.c:16996
commands
  silent
  esp_at "entry push ebp"
  continue
end
break guest_0061.c:17006
commands
  silent
  esp_at "before push fs0"
  continue
end
break guest_0061.c:17009
commands
  silent
  esp_at "at sub esp,0x10"
  continue
end
break guest_0061.c:17012
commands
  silent
  esp_at "before push ebx"
  continue
end
break guest_0061.c:17014
commands
  silent
  esp_at "before push esi"
  continue
end
break guest_0061.c:17016
commands
  silent
  esp_at "before push edi"
  continue
end
break guest_0061.c:17023
commands
  silent
  esp_at "before push cookie"
  continue
end
break guest_0061.c:17041
commands
  silent
  esp_at "before call 0xa06268"
  continue
end
break guest_0061.c:17047
commands
  silent
  esp_at "after  call 0xa06268"
  continue
end
break guest_0061.c:17059
commands
  silent
  esp_at "before call 0xa0626c"
  continue
end
break guest_0061.c:17085
commands
  silent
  esp_at "before call 0xa062a8"
  continue
end
break guest_0061.c:17118
commands
  silent
  esp_at "before call 0xa062d4"
  continue
end
break guest_0061.c:17130
commands
  silent
  esp_at "before call 0x7de0"
  continue
end
break guest_0061.c:17140
commands
  silent
  esp_at "before call 0x5ec14c"
  continue
end
break guest_0061.c:17144
commands
  silent
  esp_at "at add esp,0xc"
  continue
end
break guest_0061.c:17199
commands
  silent
  esp_at "at pop ecx (epilogue)"
  continue
end

run
printf "---- done ----\n"
GDB

cd "$OUT"
ISAAC_DATA_ROOT="$OUT/isaac-data" ISAAC_TRACE_MAX=200 \
  timeout 300 gdb -batch -x "$DEMO/esp.gdb" --args ./isaac_host "$EXE" \
  > "$SCRATCH/esp_trace.log" 2>&1
echo "---- log: $SCRATCH/esp_trace.log"
grep -n "esp=0x803f" "$SCRATCH/esp_trace.log" | tail -25
