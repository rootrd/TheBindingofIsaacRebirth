#!/bin/bash
# Full-trace baseline: rebuild the real harness, run it with an unbounded guest
# trace so every host import call is visible, and summarise the import mix.
# Usage: bash pc-host/scratch/pathmod_baseline.sh [seconds]
PROJ=/mnt/f/TheBindingofIsaacRebirth
EXE=$PROJ/isaac-ohos/entry/src/main/resources/rawfile/isaac-ng.exe
BUILD=$PROJ/pc-host/build
SCRATCH=$PROJ/pc-host/scratch
SECS=${1:-90}

set +e
export ISAAC_DATA_ROOT=$BUILD/isaac-data
tr -d "\r" < "$PROJ/pc-host/run.sh" > /tmp/run.sh
bash /tmp/run.sh 3 >/dev/null 2>&1

cd "$BUILD" || exit 1
ISAAC_TRACE_MAX=20000 ISAAC_DATA_ROOT="$ISAAC_DATA_ROOT" timeout "$SECS" \
    ./isaac_host "$EXE" > "$SCRATCH/pathmod_baseline_trace.log" 2>&1
echo "lines: $(wc -l < "$SCRATCH/pathmod_baseline_trace.log")"
echo "=== import mix ==="
grep -o 'import .*' "$SCRATCH/pathmod_baseline_trace.log" | sed 's/import //' \
    | sort | uniq -c | sort -rn | head -45
echo "=== file-ish imports ==="
grep -o 'import .*' "$SCRATCH/pathmod_baseline_trace.log" | sed 's/import //' \
    | grep -Ei 'CreateFile|CloseHandle|FileAttributes|CreateDirectory|Find' \
    | sort | uniq -c | sort -rn
echo "=== fault ==="
grep -E "guest_fault: addr|isaac_guest_run" "$SCRATCH/pathmod_baseline_trace.log" | tail -3
