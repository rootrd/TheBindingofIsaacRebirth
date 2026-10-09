#!/bin/bash
# Diagnostic pass: why do the synthetic archives move the guest from 15360 to
# 16384 calls, and does host_ext_path.c matter for that?
# Builds control + registered again (WSL /tmp does not persist) and runs:
#   A registered, junk archives, no kage_mount_points.dat, full trace
#   B control   , junk archives, no kage_mount_points.dat, full trace
#   C registered, no archives at all, no mount file (today's tree, full trace)
set -u
PROJ=/mnt/f/TheBindingofIsaacRebirth
SRC=$PROJ/isaac-ohos/entry/src/main/cpp/isaac
SHIM=$PROJ/pc-host/shim
OUT=$PROJ/pc-host/build
SCRATCH=$PROJ/pc-host/scratch
EXE=$PROJ/isaac-ohos/entry/src/main/resources/rawfile/isaac-ng.exe
CFLAGS="-O1 -g -I$SHIM -I$SRC -I$SRC/generated"
ARCHIVES="resources/packed/animations.a resources/packed/config.a
resources/packed/fonts.a resources/packed/graphics.a resources/packed/music.a
resources/packed/rooms.a resources/packed/sfx.a resources/packed/videos.a
packed/afterbirth.a packed/afterbirthp.a packed/repentance.a
packed/repentance_de.a packed/repentance_es.a packed/repentance_jp.a
packed/repentance_kr.a packed/repentance_ru.a packed/repentance_zh.a
packed/repentance_fr.a secret.a"

build_variant() {
  local label=$1 mode=$2 demo=/tmp/hp_$1
  mkdir -p "$demo/obj"
  cp "$SRC/host_ohos.c" "$demo/host_ohos.c"; cp "$SRC/host_ext.h" "$demo/host_ext.h"
  if [ "$mode" = register ]; then
    python3 - "$demo" <<'PY'
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
  fi
  gcc $CFLAGS -c "$demo/host_ohos.c" -o "$demo/obj/host_ohos.o"
  gcc $CFLAGS -c "$SRC/host_ext_path.c" -o "$demo/obj/host_ext_path.o"
  local objs; objs=$(ls "$OUT"/obj/*.o | grep -vE '/(host_ohos|host_ext_path)\.o$' | tr '\n' ' ')
  gcc -g -o "$demo/isaac_host" "$PROJ/pc-host/main.c" $objs "$demo/obj/host_ohos.o" \
      "$demo/obj/host_ext_path.o" -lpthread -lm -ldl
}

mkroot() {                   # mkroot <dir> <junk|none>
  rm -rf "$1"; mkdir -p "$1/resources/packed" "$1/packed"
  if [ "$2" = junk ]; then
    for a in $ARCHIVES; do head -c 4096 /dev/urandom > "$1/$a"; done
  fi
  # deliberately NO kage_mount_points.dat
}

report() {                   # report <label> <log>
  local l=$1 log=$2
  echo "=========== $l"
  echo "  lines=$(wc -l < "$log")  last heartbeat=$(grep -oE 'heartbeat: count=[0-9]+' "$log" | tail -1)"
  echo "  fault=$(grep -oE 'guest_fault: addr=0x[0-9a-f]+ what=.*' "$log" | tail -1)"
  echo "  -- fopen on .a archives (successes and failures) --"
  grep -E 'fopen.*(\.a"|\.a\b)' "$log" | head -12 | sed 's/^/    /'
  echo "  -- print counts per import (top 14) --"
  grep -o 'import .*' "$log" | sed 's/import //' | sort | uniq -c | sort -rn | head -14 | sed 's/^/    /'
  echo "  -- .a / archive related guest lines --"
  grep -iE "archive|\.a\b|mount" "$log" | grep -v guest_call | head -10 | sed 's/^/    /'
}

build_variant ctl control
build_variant reg register

echo "########## A) registered + junk archives, no mount file"
ROOT=$OUT/pathmod-diag-a; mkroot "$ROOT" junk
( cd /tmp/hp_reg && ISAAC_TRACE_MAX=20000 ISAAC_DATA_ROOT="$ROOT" timeout 60 ./isaac_host "$EXE" \
    > "$SCRATCH/pathmod_diag_reg_junk.log" 2>&1 )
cp "$ROOT/Documents/My Games/Binding of Isaac Repentance/log.txt" \
   "$SCRATCH/pathmod_diag_reg_junk_guest_log.txt" 2>/dev/null
report "registered + junk archives" "$SCRATCH/pathmod_diag_reg_junk.log"

echo
echo "########## B) control + junk archives, no mount file"
ROOT=$OUT/pathmod-diag-b; mkroot "$ROOT" junk
( cd /tmp/hp_ctl && ISAAC_TRACE_MAX=20000 ISAAC_DATA_ROOT="$ROOT" timeout 60 ./isaac_host "$EXE" \
    > "$SCRATCH/pathmod_diag_ctl_junk.log" 2>&1 )
cp "$ROOT/Documents/My Games/Binding of Isaac Repentance/log.txt" \
   "$SCRATCH/pathmod_diag_ctl_junk_guest_log.txt" 2>/dev/null
report "control + junk archives" "$SCRATCH/pathmod_diag_ctl_junk.log"

echo
echo "########## C) registered + NO archives, no mount file (today's tree)"
ROOT=$OUT/pathmod-diag-c; mkroot "$ROOT" none
( cd /tmp/hp_reg && ISAAC_TRACE_MAX=20000 ISAAC_DATA_ROOT="$ROOT" timeout 60 ./isaac_host "$EXE" \
    > "$SCRATCH/pathmod_diag_reg_none.log" 2>&1 )
cp "$ROOT/Documents/My Games/Binding of Isaac Repentance/log.txt" \
   "$SCRATCH/pathmod_diag_reg_none_guest_log.txt" 2>/dev/null
report "registered, no archives" "$SCRATCH/pathmod_diag_reg_none.log"

echo
echo "########## import-mix diff: no-archives vs junk-archives (registered)"
diff <(grep -o 'import .*' "$SCRATCH/pathmod_diag_reg_none.log" | sed 's/import //' | sort | uniq -c) \
     <(grep -o 'import .*' "$SCRATCH/pathmod_diag_reg_junk.log" | sed 's/import //' | sort | uniq -c) \
     | head -40

echo
echo "########## guest log diff: no-archives -> junk-archives (registered)"
diff <(tr -d '\0' < "$SCRATCH/pathmod_diag_reg_none_guest_log.txt") \
     <(tr -d '\0' < "$SCRATCH/pathmod_diag_reg_junk_guest_log.txt") | head -30
