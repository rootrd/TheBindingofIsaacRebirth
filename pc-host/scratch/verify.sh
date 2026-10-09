#!/bin/bash
# Everything in ONE pass (WSL /tmp is tmpfs, so nothing may be staged earlier):
#   A. rebuild the real harness (run.sh compiles every host_*.c, so the new
#      module is picked up) and run it directly for a FULL log
#   B. build + run the registration demo against a data root that contains
#      kage_mount_points.dat (empty: no format is invented)
# Usage: bash pc-host/scratch/verify.sh
PROJ=/mnt/f/TheBindingofIsaacRebirth
SRC=$PROJ/isaac-ohos/entry/src/main/cpp/isaac
EXE=$PROJ/isaac-ohos/entry/src/main/resources/rawfile/isaac-ng.exe
SCRATCH=$PROJ/pc-host/scratch
BUILD=$PROJ/pc-host/build
export ISAAC_TRACE_MAX=0

tr -d '\r' < "$PROJ/pc-host/run.sh" > /tmp/run.sh
tr -d '\r' < "$SCRATCH/run_demo.sh" > /tmp/run_demo.sh

echo "########## A) real harness, module compiled in but NOT registered"
bash /tmp/run.sh 5 > /dev/null 2>&1 || true
ls -l "$BUILD/obj/host_ext_file.o" | sed 's/^/  built: /'
cd "$BUILD"
ISAAC_DATA_ROOT=$BUILD/isaac-data timeout 60 ./isaac_host "$EXE" > "$SCRATCH/after_runsh.log" 2>&1 || true
echo "  host_ext_file lines in the full log:"
grep -c "host_ext_file" "$SCRATCH/after_runsh.log" | sed 's/^/    count=/'
grep -E "host_ext_file|dispather|dispatcher" "$SCRATCH/after_runsh.log" | head -3
echo "  fopen lines (host_ohos.c's stub still owns the import):"
grep -c "fopen\[" "$SCRATCH/after_runsh.log" | sed 's/^/    count=/'
echo "  last calls:"
grep -E "heartbeat: count=15|guest_fault: addr" "$SCRATCH/after_runsh.log" | tail -4

echo
echo "########## B) demo build (provider registered in a COPY), data root has kage_mount_points.dat"
MOUNT=/tmp/demo-data
rm -rf "$MOUNT"
mkdir -p "$MOUNT/Documents" "$MOUNT/data" "$MOUNT/mods"
: > "$MOUNT/kage_mount_points.dat"
export ISAAC_DATA_ROOT=$MOUNT
bash /tmp/run_demo.sh 45 > "$SCRATCH/demo_mount.log" 2>&1 || true
echo "-- every file-handler line (fopen success/failure, byte counts, positions) --"
grep -E "fopen\[|fopen: |fread:|fwrite: first|ftell:|fseek:|fclose:|_fileno|_get_osfhandle|_set_errno: first|_errno: first" "$SCRATCH/demo_mount.log" | head -30
echo "-- host files the guest created under that root --"
find "$MOUNT" -type f | sort
echo "-- the guest's own log.txt (written through this module's fwrite) --"
head -14 "$MOUNT/Documents/My Games/Binding of Isaac Repentance/log.txt" 2>/dev/null
echo "-- fault, if any --"
grep -E "guest_fault: addr|heartbeat: count" "$SCRATCH/demo_mount.log" | tail -3
