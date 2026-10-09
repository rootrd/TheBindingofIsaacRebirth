#!/usr/bin/env python3
"""Rewrite the handover doc's section 0 as a clean, self-contained resume
point: current state, the single blocker, and the exact first action."""
import io

P = '/mnt/f/TheBindingofIsaacRebirth/交接-新会话入口.md'
s = io.open(P, encoding='utf-8').read()

start = s.index('## ★★ 〇、')
end = s.index('## 二、已完成且勿重做')

new = '''## ★★ 〇、当前状态与下一会话第一步（2026-09-19 收官）

### 一句话

**真机与 PC 完全同步：游戏跑通 19 个着色器初始化 + PersistentGameData，FPS 119 渲染运行；
唯一阻塞是异常路径上的一次非法地址写入（0xFFFFFFF8）。**

### 唯一卡点（根因已定位到指令级）

gdb 抓到的崩溃现场（PC 宿主，真机同源）：

```
memcpy(__len=16, __dest=0x7ffff7bffff8)   ← 写到 arena 末尾之外
stx(a=0xFFFFFFF8)                          ← 目标地址 = -8
sub_000226f0 (guest_0004.c:16221)
  ← sub_0025ceb0 (guest_0061.c:18446)
  ← sub_002597b0 (guest_extra_002597b0.c:141)   ← 我们补翻的函数（build_one.py 产物）
  ← sub_0025bbc0 (PersistentGameData::Load, guest_0061.c:14201)
```

**含义**：guest 把一个 NULL 对象/缓冲减 8 当成了 SSE 存储地址。触发路径经过我们补翻的
`sub_002597b0`——它是 throw helper（原生行为：构造并抛 C++ 异常），现在却走进了正常路径。

### 下一会话第一步（按序）

1. 读 `guest_0004.c:16221`（`sub_000226f0`）与 `guest_0061.c:18446`（`sub_0025ceb0`）的翻译，
   确认 `0xFFFFFFF8` 的来源：通常是 `[ebp-8]` 被当成绝对 guest 地址，或某对象指针为 0 后减 8。
2. 对照 `sub_002597b0` 的原生语义（IDA/objdump 看 RVA 0x2597b0）：它应由 `_CxxThrowException`
   抛出而不是继续执行；我们补翻的版本可能丢了入口处的分支或参数约定。
3. 修好后游戏应越过 PersistentGameData → 进入渲染循环（真 PC 日志的下一行是
   `Viewport: 960x540`）→ 标题画面。

### 相关背景（本轮已完成的修复，勿重做）

**PC 宿主 + 真机共用**：
- `glGetIntegerv` 64 字节越界写（真机 KAGE 失败总根因，已按 pname 返回正确个数）
- GLSL 改写器：无 `#version` 行时补 `#version 300 es`；fragment 补 `precision mediump float;`；
  输出声明带 `mediump`
- MSVCP140：`_Ipfx`（istream 哨兵）返回成功、`setstate` 记录状态位（**解除 1.4 亿次死循环**）
- `_CxxThrowException` → `isaac_seh_dispatch`（已能遍历 FS:[0] 并调用帧 handler=0x305f2420）
- SEH 加固：EXCEPTION_RECORD/CONTEXT 静态缓冲、handler 前后保存恢复 guest esp/ebp、
  Context.Eip = 抛出点返回地址、堆上限 0xE0000000
- WINMM timeGetTime、IAT 重填、edi 重装、容错重启循环、RESOLVE hook（带路径过滤）

**真机专用**：
- 资源解压（77 文件 1.08GB）：raw fd 用 `rm.closeRawFd(name)` 释放；`mkdirSync` 对已存在目录
  抛 13900015 需单独 try 包住；用 `await fileIo.read` 而非 readSync
- `ISAAC_DATA_ROOT` 经 `setenv` 接线（否则 guest 读相对路径沙箱外 → EACCES）
- shader 双路径解压（游戏请求 `shaders/x.vs`，实际在 `resources/shaders/`）
- 启动界面 + **触摸控制完全照搬死亡细胞**（VirtualPad 拖动式摇杆 + FreeButton + 持久化；
  布局与死亡细胞一致：PAUSE/MAP 顶部两角、BOMB/CARD/ITEM/SWAP 右侧散布、
  MOVE 摇杆左下 200、SHOOT 摇杆右下 150）
- guest 的 log.txt 镜像到 hilog（`guest-log:` 前缀），真机可观测

### 构建与运行

```bash
# PC 宿主（秒级迭代）
wsl bash -c "cd /mnt/f/TheBindingofIsaacRebirth && \\
  ISAAC_DATA_ROOT=/mnt/f/TheBindingofIsaacRebirth/pc-host/build/isaac-data bash pc-host/run.sh"

# 真机（HAP 构建 + 安装 + 启动 + 日志）
export PATH="/c/Program Files/Huawei/DevEco Studio/tools/node:/c/Program Files/Huawei/DevEco Studio/tools/hvigor/bin:/c/Program Files/Huawei/DevEco Studio/jbr/bin:$PATH"
cd /f/TheBindingofIsaacRebirth/isaac-ohos && hvigorw.bat assembleHap --mode module -p product=default --no-daemon
HDC="/c/Program Files/Huawei/DevEco Studio/sdk/default/openharmony/toolchains/hdc.exe"
"$HDC" install -r "F:\\\\TheBindingofIsaacRebirth\\\\isaac-ohos\\\\entry\\\\build\\\\default\\\\outputs\\\\default\\\\entry-default-signed.hap"
"$HDC" shell "aa force-stop com.example.gtasarootrd"; "$HDC" shell "hilog -r"
"$HDC" shell "aa start -a EntryAbility -b com.example.gtasarootrd"
"$HDC" shell "hilog -x | grep -E 'guest-log|guest fault' | tail -20"
```

**注意**：改 `generated/guest_*.c` 后需手动重编该 TU 并重链；改 TS/ets 直接重新构建 HAP。

---

'''

s = s[:start] + new + s[end:]
io.open(P, 'w', encoding='utf-8').write(s)
print('section 0 rewritten')
