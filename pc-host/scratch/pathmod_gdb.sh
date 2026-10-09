#!/bin/bash
# ONE-PASS gdb probe: does the guest ever hand the archive names to the file
# layer at all? Breaks on File::Open (sub_00596360), on the mount lookup
# (sub_00563e70) and on fopen (h_file_fopen), printing each guest name.
# Rebuilds the registered binary first (WSL /tmp does not persist).
set -u
PROJ=/mnt/f/TheBindingofIsaacRebirth
SRC=$PROJ/isaac-ohos/entry/src/main/cpp/isaac
SHIM=$PROJ/pc-host/shim
OUT=$PROJ/pc-host/build
SCRATCH=$PROJ/pc-host/scratch
EXE=$PROJ/isaac-ohos/entry/src/main/resources/rawfile/isaac-ng.exe
CFLAGS="-O1 -g -I$SHIM -I$SRC -I$SRC/generated"
DEMO=/tmp/hp_gdb
mkdir -p "$DEMO/obj"
cp "$SRC/host_ohos.c" "$DEMO/host_ohos.c"; cp "$SRC/host_ext.h" "$DEMO/host_ext.h"
python3 - "$DEMO" <<'PY'
import sys, pathlib
d = pathlib.Path(sys.argv[1])
def patch(n, o, w):
    p = d / n; s = p.read_text(); assert s.count(o) == 1, (n, s.count(o)); p.write_text(s.replace(o, w))
patch("host_ohos.c",
      "__attribute__((weak)) const host_ext_module *host_ext_file(void) { return NULL; }",
      "__attribute__((weak)) const host_ext_module *host_ext_file(void) { return NULL; }\n"
      "__attribute__((weak)) const host_ext_module *host_ext_path(void) { return NULL; }")
patch("host_ohos.c", "    host_ext_file,\n};", "    host_ext_file,\n    host_ext_path,\n};")
patch("host_ext.h", "const host_ext_module *host_ext_file(void);",
      "const host_ext_module *host_ext_file(void);\nconst host_ext_module *host_ext_path(void);")
PY
gcc $CFLAGS -c "$DEMO/host_ohos.c" -o "$DEMO/obj/host_ohos.o"
gcc $CFLAGS -c "$SRC/host_ext_path.c" -o "$DEMO/obj/host_ext_path.o"
objs=$(ls "$OUT"/obj/*.o | grep -vE '/(host_ohos|host_ext_path)\.o$' | tr '\n' ' ')
gcc -g -o "$DEMO/isaac_host" "$PROJ/pc-host/main.c" $objs "$DEMO/obj/host_ohos.o" \
    "$DEMO/obj/host_ext_path.o" -lpthread -lm -ldl

cat > "$DEMO/probe.gdb" <<'GDB'
set pagination off
set confirm off
set height 0
set width 0
break sub_00596360
commands
  silent
  set $a = *(unsigned int *)((char *)c->esp + 4)
  printf "FILEOPEN 0x%08x: ", $a
  if $a != 0
    x/s ((char *)isaac_arena_base + $a)
  else
    printf "<null>\n"
  end
  continue
end
break sub_00563e70
commands
  silent
  set $a = *(unsigned int *)((char *)c->esp + 4)
  printf "MOUNTLOOKUP 0x%08x: ", $a
  if $a != 0
    x/s ((char *)isaac_arena_base + $a)
  else
    printf "<null>\n"
  end
  continue
end
break h_file_fopen
commands
  silent
  set $a = *(unsigned int *)((char *)c->esp + 4)
  printf "FOPEN 0x%08x: ", $a
  if $a != 0
    x/s ((char *)isaac_arena_base + $a)
  else
    printf "<null>\n"
  end
  continue
end
run
GDB

cd "$DEMO"
ROOT=$OUT/pathmod-gdb-data
rm -rf "$ROOT"; mkdir -p "$ROOT/resources/packed" "$ROOT/packed"
for a in resources/packed/animations.a resources/packed/config.a \
         resources/packed/graphics.a packed/afterbirth.a secret.a; do
  head -c 4096 /dev/urandom > "$ROOT/$a"
done
python3 - "$ROOT/kage_mount_points.dat" <<'PY'
import struct, sys
out = bytearray()
def d(x): out.extend(struct.pack("<I", x))
def s(t):
    b = t.encode(); d(len(b)); out.extend(b)
d(1); s("zprobemount"); d(1); s("packed/zprobe.a")
open(sys.argv[1], "wb").write(bytes(out))
PY
ISAAC_DATA_ROOT="$ROOT" timeout 300 gdb -batch -x "$DEMO/probe.gdb" \
    --args ./isaac_host "$EXE" > "$SCRATCH/pathmod_gdb_probe.log" 2>&1
echo "gdb log: $SCRATCH/pathmod_gdb_probe.log ($(wc -l < "$SCRATCH/pathmod_gdb_probe.log") lines)"
echo "=== counts ==="
for k in FILEOPEN MOUNTLOOKUP FOPEN; do
  echo "  $k: $(grep -c "^$k" "$SCRATCH/pathmod_gdb_probe.log")"
done
echo "=== first 30 FILEOPEN / distinct names ==="
grep "^FILEOPEN" "$SCRATCH/pathmod_gdb_probe.log" | head -30
echo "=== distinct names (first 40) ==="
grep -A1 "^FILEOPEN" "$SCRATCH/pathmod_gdb_probe.log" | grep -v '^FILEOPEN' | grep -v '^--' | sort -u | head -40
echo "=== MOUNTLOOKUP names ==="
grep -A1 "^MOUNTLOOKUP" "$SCRATCH/pathmod_gdb_probe.log" | grep -v '^MOUNTLOOKUP' | grep -v '^--' | sort -u | head -30
echo "=== FOPEN names ==="
grep -A1 "^FOPEN" "$SCRATCH/pathmod_gdb_probe.log" | grep -v '^FOPEN' | grep -v '^--' | sort -u | head -20
