#!/bin/bash
# 在真 Windows 上运行解包版 isaac-ng.exe 数秒，观察它自己生成的文件
# （特别是 kage_mount_points.dat），随后杀掉进程。判定真 PC 行为。
set -u
GAMEDIR="C:\\Users\\Administrator\\Desktop\\The Binding of Isaac Rebirth 1.7.9b"
cd "$GAMEDIR" || exit 2
before=$(ls -a | sort)
./isaac-ng.exe.unpacked.exe &
pid=$!
sleep 10
kill $pid 2>/dev/null
sleep 1
after=$(ls -a | sort)
echo "=== 新增/变化的文件 ==="
diff <(echo "$before") <(echo "$after")
echo "=== 目录里的 dat/txt ==="
ls -la *.dat *.txt 2>/dev/null
