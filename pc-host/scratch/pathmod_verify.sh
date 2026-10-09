#!/bin/bash
# ONE-PASS verification for host_ext_path.c. WSL /tmp is tmpfs and does not
# survive between invocations, so every stage runs in this single script:
#
#   A  build the CONTROL binary   (host_ohos.c copy, byte-identical: today's tree)
#   B  build the REGISTERED binary (only difference in the two copies: the
#      provider list gains host_ext_path, appended last)
#   C  run control and registered against the real isaac-data root
#   D  synthetic-data experiments against a data root of their own
#
# Nothing in the project tree is edited. Usage: bash .../pathmod_verify.sh
set -u
PROJ=/mnt/f/TheBindingofIsaacRebirth
SRC=$PROJ/isaac-ohos/entry/src/main/cpp/isaac
SHIM=$PROJ/pc-host/shim
OUT=$PROJ/pc-host/build
SCRATCH=$PROJ/pc-host/scratch
EXE=$PROJ/isaac-ohos/entry/src/main/resources/rawfile/isaac-ng.exe
SECS=${SECS:-40}
CFLAGS="-O1 -g -I$SHIM -I$SRC -I$SRC/generated"

echo "########## 0) rebuild the real objects once (run.sh picks up host_ext_path.c)"
export ISAAC_DATA_ROOT=$OUT/isaac-data
tr -d "\r" < "$PROJ/pc-host/run.sh" > /tmp/run.sh
ISAAC_TRACE_MAX=0 bash /tmp/run.sh 3 >/dev/null 2>&1
ls -l "$OUT/obj/host_ext_path.o" | sed 's/^/  built: /'

build_variant() {           # build_variant <label> <register|control>
  local label=$1 mode=$2 demo=/tmp/hp_$1
  mkdir -p "$demo/obj"
  cp "$SRC/host_ohos.c" "$demo/host_ohos.c"
  cp "$SRC/host_ext.h"  "$demo/host_ext.h"
  if [ "$mode" = register ]; then
    python3 - "$demo" <<'PY'
import sys, pathlib
d = pathlib.Path(sys.argv[1])
def patch(name, old, new):
    p = d / name; s = p.read_text()
    assert s.count(old) == 1, (name, s.count(old), old[:60])
    p.write_text(s.replace(old, new))
patch("host_ohos.c",
      "__attribute__((weak)) const host_ext_module *host_ext_file(void) { return NULL; }",
      "__attribute__((weak)) const host_ext_module *host_ext_file(void) { return NULL; }\n"
      "__attribute__((weak)) const host_ext_module *host_ext_path(void) { return NULL; }")
patch("host_ohos.c", "    host_ext_file,\n};", "    host_ext_file,\n    host_ext_path,\n};")
patch("host_ext.h", "const host_ext_module *host_ext_file(void);",
      "const host_ext_module *host_ext_file(void);\nconst host_ext_module *host_ext_path(void);")
PY
  fi
  gcc $CFLAGS -c "$demo/host_ohos.c"    -o "$demo/obj/host_ohos.o" || return 1
  gcc $CFLAGS -c "$SRC/host_ext_path.c" -o "$demo/obj/host_ext_path.o" || return 1
  local objs
  objs=$(ls "$OUT"/obj/*.o | grep -vE '/(host_ohos|host_ext_path)\.o$' | tr '\n' ' ')
  gcc -g -o "$demo/isaac_host" "$PROJ/pc-host/main.c" $objs \
      "$demo/obj/host_ohos.o" "$demo/obj/host_ext_path.o" -lpthread -lm -ldl || return 1
  echo "  $label binary: $demo/isaac_host"
}

summarise() {               # summarise <label> <logfile>
  local label=$1 log=$2
  echo "  [$label] harness log lines : $(wc -l < "$log")"
  echo "  [$label] last heartbeat    : $(grep -oE 'heartbeat: count=[0-9]+' "$log" | tail -1)"
  echo "  [$label] fault             : $(grep -oE 'guest_fault: addr=0x[0-9a-f]+ what=.*' "$log" | tail -1)"
  echo "  [$label] host_ext_path     : $(grep -c 'host_ext_path' "$log") lines"
  echo "  [$label] KERNEL32 file APIs:"
  grep -E "CloseHandle:|CreateDirectoryA:|GetFileAttributesA: guest|CreateFileA\[" "$log" \
      | sed 's/^/      /' | head -14
}

echo
echo "########## A/B) build control + registered"
build_variant ctl control
build_variant reg register

echo
echo "########## C) control run"
export ISAAC_DATA_ROOT=$OUT/isaac-data
( cd /tmp/hp_ctl && timeout "$SECS" ./isaac_host "$EXE" > "$SCRATCH/pathmod_ctl.log" 2>&1 )
cp "$ISAAC_DATA_ROOT/Documents/My Games/Binding of Isaac Repentance/log.txt" \
   "$SCRATCH/pathmod_ctl_guest_log.txt" 2>/dev/null
summarise ctl "$SCRATCH/pathmod_ctl.log"

echo
echo "########## C) registered run"
( cd /tmp/hp_reg && timeout "$SECS" ./isaac_host "$EXE" > "$SCRATCH/pathmod_reg.log" 2>&1 )
cp "$ISAAC_DATA_ROOT/Documents/My Games/Binding of Isaac Repentance/log.txt" \
   "$SCRATCH/pathmod_reg_guest_log.txt" 2>/dev/null
summarise reg "$SCRATCH/pathmod_reg.log"

echo
echo "  guest log.txt diff (control -> registered), first 6 differing lines:"
diff "$SCRATCH/pathmod_ctl_guest_log.txt" "$SCRATCH/pathmod_reg_guest_log.txt" \
    | head -6 | sed 's/^/      /' || true
echo "  classic failure lines still present in the registered guest log:"
grep -cE "Failed to open|Failed to load vertex shader" \
    "$SCRATCH/pathmod_reg_guest_log.txt" 2>/dev/null | sed 's/^/      /'

echo
echo "########## D) synthetic data root experiments (registered binary)"
ROOT=$OUT/pathmod-synth-data
ARCHIVES="resources/packed/animations.a resources/packed/config.a
resources/packed/fonts.a resources/packed/graphics.a resources/packed/music.a
resources/packed/rooms.a resources/packed/sfx.a resources/packed/videos.a
packed/afterbirth.a packed/afterbirthp.a packed/repentance.a
packed/repentance_de.a packed/repentance_es.a packed/repentance_jp.a
packed/repentance_kr.a packed/repentance_ru.a packed/repentance_zh.a
packed/repentance_fr.a secret.a"

synth() {                   # synth <file-mode> <mount-mode> <label>
  local mode=$1 mount=$2 label=$3
  rm -rf "$ROOT"; mkdir -p "$ROOT/resources/packed" "$ROOT/packed"
  for a in $ARCHIVES; do
    case "$mode" in
      empty) : > "$ROOT/$a" ;;
      junk)  head -c 4096 /dev/urandom > "$ROOT/$a" ;;
    esac
  done
  if [ "$mount" = probe ]; then
    python3 - "$ROOT/kage_mount_points.dat" <<'PY'
import struct, sys
out = bytearray()
def d(x): out.extend(struct.pack("<I", x))
def s(t):
    b = t.encode(); d(len(b)); out.extend(b)
d(1); s("zprobemount"); d(1); s("packed/zprobe.a")
open(sys.argv[1], "wb").write(bytes(out))
PY
  else
    : > "$ROOT/kage_mount_points.dat"
  fi
  ( cd /tmp/hp_reg && ISAAC_DATA_ROOT="$ROOT" timeout "$SECS" ./isaac_host "$EXE" \
        > "$SCRATCH/pathmod_$label.log" 2>&1 )
  local save="$ROOT/Documents/My Games/Binding of Isaac Repentance/log.txt"
  [ -f "$save" ] && cp "$save" "$SCRATCH/pathmod_${label}_guest_log.txt"
  echo "  --- $label (files=$mode, mount=$mount) ---"
  echo "      archive-open failures in the guest log: $(grep -c 'Failed to open' "$SCRATCH/pathmod_${label}_guest_log.txt" 2>/dev/null)"
  echo "      guest log head:"
  sed -n '1,6p' "$SCRATCH/pathmod_${label}_guest_log.txt" 2>/dev/null | sed 's/^/        /'
  echo "      new/unexpected guest-log lines:"
  grep -vE "Failed to open|Failed to load vertex shader|\[INFO\] - (Initialize|load archives)" \
      "$SCRATCH/pathmod_${label}_guest_log.txt" 2>/dev/null | head -8 | sed 's/^/        /'
  echo "      zprobe anywhere in the harness log: $(grep -ic zprobe "$SCRATCH/pathmod_$label.log")"
  grep -i zprobe "$SCRATCH/pathmod_$label.log" | head -6 | sed 's/^/        /'
  echo "      last heartbeat: $(grep -oE 'heartbeat: count=[0-9]+' "$SCRATCH/pathmod_$label.log" | tail -1)"
  echo "      fault: $(grep -oE 'guest_fault: addr=0x[0-9a-f]+ what=.*' "$SCRATCH/pathmod_$label.log" | tail -1)"
  grep -E "fopen\[[0-9]+\]|Failed to open" "$SCRATCH/pathmod_$label.log" | head -6 | sed 's/^/        /'
}

synth empty probe synth_empty_probe
synth junk  probe synth_junk_probe
synth junk  none  synth_junk_none

echo
echo "########## done"
