#!/bin/bash
# Test-compile the guest runtime layer on a plain POSIX host (no device).
cd /mnt/f/TheBindingofIsaacRebirth/isaac-ohos/entry/src/main/cpp/isaac || exit 1
SHIM=/mnt/f/TheBindingofIsaacRebirth/pc-host/shim
for f in host_ohos.c isaac_guest_main.c guest_memory.c glsl_es_rewrite.c gl_ohos_backend.c; do
  printf '%-24s: ' "$f"
  if gcc -c -O0 -I "$SHIM" -I . "$f" -o "/tmp/pc_$f.o" 2>"/tmp/pc_$f.err"; then
    echo OK
  else
    echo FAIL
    grep -m3 -E 'error' "/tmp/pc_$f.err"
  fi
done
