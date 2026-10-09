#!/bin/bash
# 手动重编 guest_0166.o 并重新链接 isaac_host（供修改 generated 代码后使用）。
set -e
PROJ=/mnt/f/TheBindingofIsaacRebirth
SRC=$PROJ/isaac-ohos/entry/src/main/cpp/isaac
SHIM=$PROJ/pc-host/shim
OUT=$PROJ/pc-host/build
CFLAGS="-O1 -g -fno-strict-aliasing -I$SHIM -I$SRC -I$SRC/generated"

gcc $CFLAGS -c "$SRC/generated/guest_0166.c" -o "$OUT/obj/guest_0166.o"
echo "guest_0166.o rebuilt"
gcc -g -o "$OUT/isaac_host" "$PROJ/pc-host/main.c" "$OUT"/obj/*.o -lpthread -lm -ldl
echo "isaac_host relinked"