#!/bin/bash
# Minimal gdb sanity test: are breakpoints functional at all in this binary,
# and does h_file_fopen get entered? One pass (WSL /tmp is tmpfs).
set -u
PROJ=/mnt/f/TheBindingofIsaacRebirth
SRC=$PROJ/isaac-ohos/entry/src/main/cpp/isaac
SHIM=$PROJ/pc-host/shim
OUT=$PROJ/pc-host/build
SCRATCH=$PROJ/pc-host/scratch
EXE=$PROJ/isaac-ohos/entry/src/main/resources/rawfile/isaac-ng.exe
CFLAGS="-O1 -g -I$SHIM -I$SRC -I$SRC/generated"
DEMO=/tmp/hp_gdb2
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

cat > "$DEMO/sanity.gdb" <<'GDB'
set pagination off
set confirm off
set height 0
break guest_host_import
commands
  silent
  printf "GHI hit, name=%s\n", name
  continue
end
break h_file_fopen
commands
  silent
  printf "HFOPEN hit\n"
  continue
end
break sub_00596360
commands
  silent
  printf "FILEOPEN hit esp=0x%08x\n", c->esp
  continue
end
run
printf "---- after run ----\n"
info breakpoints
GDB

cd "$DEMO"
ROOT=$OUT/pathmod-gdb-data
ISAAC_DATA_ROOT="$ROOT" timeout 300 gdb -batch -x "$DEMO/sanity.gdb" \
    --args ./isaac_host "$EXE" > "$SCRATCH/pathmod_gdb_sanity.log" 2>&1
echo "sanity log: $SCRATCH/pathmod_gdb_sanity.log ($(wc -l < "$SCRATCH/pathmod_gdb_sanity.log") lines)"
grep -c "GHI hit" "$SCRATCH/pathmod_gdb_sanity.log" | sed 's/^/  guest_host_import hits: /'
grep -c "HFOPEN hit" "$SCRATCH/pathmod_gdb_sanity.log" | sed 's/^/  h_file_fopen hits: /'
grep -c "FILEOPEN hit" "$SCRATCH/pathmod_gdb_sanity.log" | sed 's/^/  sub_00596360 hits: /'
grep -m5 "GHI hit" "$SCRATCH/pathmod_gdb_sanity.log" | sed 's/^/  /'
grep -m5 "HFOPEN hit" "$SCRATCH/pathmod_gdb_sanity.log" | sed 's/^/  /'
sed -n '/after run/,$p' "$SCRATCH/pathmod_gdb_sanity.log" | head -20
