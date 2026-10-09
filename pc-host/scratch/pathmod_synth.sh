#!/bin/bash
# Synthetic-data experiment: what changes when the archives the guest asks for
# actually exist under ISAAC_DATA_ROOT, and can a synthetic
# kage_mount_points.dat be made to take effect?
#
#   $1 = content mode for the 19 "*.a" files: none | empty | junk
#   $2 = mount-points content:           none | probe
#   $3 = seconds
#
# Everything is written under pc-host/build/pathmod-synth-data (a data root of
# its own, so the real isaac-data tree is untouched). Requires the registered
# demo binary from pathmod_demo.sh --register.
set -e
PROJ=/mnt/f/TheBindingofIsaacRebirth
SCRATCH=$PROJ/pc-host/scratch
ROOT=$PROJ/pc-host/build/pathmod-synth-data
EXE=$PROJ/isaac-ohos/entry/src/main/resources/rawfile/isaac-ng.exe
DEMO=/tmp/hp_reg/isaac_host

MODE=${1:-empty}
MOUNT=${2:-probe}
SECS=${3:-45}
LABEL="synth_${MODE}_${MOUNT}"

ARCHIVES="resources/packed/animations.a resources/packed/config.a
resources/packed/fonts.a resources/packed/graphics.a resources/packed/music.a
resources/packed/rooms.a resources/packed/sfx.a resources/packed/videos.a
packed/afterbirth.a packed/afterbirthp.a packed/repentance.a
packed/repentance_de.a packed/repentance_es.a packed/repentance_jp.a
packed/repentance_kr.a packed/repentance_ru.a packed/repentance_zh.a
packed/repentance_fr.a secret.a"

rm -rf "$ROOT"
mkdir -p "$ROOT/resources/packed" "$ROOT/packed"
for a in $ARCHIVES; do
  case "$MODE" in
    none)  ;;                                   # not created at all
    empty) : > "$ROOT/$a" ;;                    # exists, 0 bytes
    junk)  head -c 4096 /dev/urandom > "$ROOT/$a" ;;
    zip)   printf 'PK\003\004' > "$ROOT/$a"; head -c 4092 /dev/zero >> "$ROOT/$a" ;;
  esac
done

# kage_mount_points.dat — the layout read out of sub_00564750 (RVA 00564750):
#   uint32 record_count
#   per record: uint32 name_len, name_bytes, uint32 n, { uint32 len, bytes }*n
# All little-endian, no padding, strings NOT NUL-terminated in the file.
if [ "$MOUNT" = "probe" ]; then
  python3 - "$ROOT/kage_mount_points.dat" <<'PY'
import struct, sys
out = bytearray()
def d(x): out.extend(struct.pack("<I", x))
def s(t):
    b = t.encode()
    d(len(b)); out.extend(b)
d(1)                      # one record
s("zprobemount")          # the mount name
d(1)                      # one sub-string
s("packed/zprobe.a")      # the sub-string
open(sys.argv[1], "wb").write(bytes(out))
print("  kage_mount_points.dat: %d bytes" % len(out))
PY
else
  : > "$ROOT/kage_mount_points.dat"
fi

cd /tmp/hp_reg
ISAAC_DATA_ROOT="$ROOT" timeout "$SECS" ./isaac_host "$EXE" \
    > "$SCRATCH/pathmod_$LABEL.log" 2>&1 || true
grep -c . "$SCRATCH/pathmod_$LABEL.log" | sed "s/^/  harness log lines: /"

SAVE="$ROOT/Documents/My Games/Binding of Isaac Repentance/log.txt"
echo "  files under the synthetic root that the guest created:"
find "$ROOT" -newer "$ROOT/packed" -type f 2>/dev/null | head
if [ -f "$SAVE" ]; then
  cp "$SAVE" "$SCRATCH/pathmod_${LABEL}_guest_log.txt"
  echo "  guest log.txt:"
  sed -n '1,12p' "$SCRATCH/pathmod_${LABEL}_guest_log.txt" | sed 's/^/    /'
  echo "  ..."
  grep -nE "Failed to open|archive|mount|shader|error|Error" \
      "$SCRATCH/pathmod_${LABEL}_guest_log.txt" | head -12 | sed 's/^/    /'
fi
echo "  zprobe mentions anywhere in the harness log:"
grep -i "zprobe" "$SCRATCH/pathmod_$LABEL.log" | head -10 | sed 's/^/    /' || true
echo "  fault:"
grep -E "guest_fault: addr|isaac_guest_run returned" "$SCRATCH/pathmod_$LABEL.log" \
    | tail -2 | sed 's/^/    /'
echo "  last heartbeat:"
grep -E "heartbeat: count=[0-9]+" "$SCRATCH/pathmod_$LABEL.log" | tail -1 | sed 's/^/    /'
