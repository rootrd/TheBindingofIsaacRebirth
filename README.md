# 以撒的结合：忏悔 — HarmonyOS 静态重编译移植

把 Windows 版《Binding of Isaac: Repentance》(isaac-ng.exe) 通过**静态重编译**
（static recompilation）移植到 HarmonyOS 鸿蒙手机上运行。

不做模拟器、不做解释器逐条翻译——而是把 x86 机器码**离线翻译成 C 代码**，
用目标平台的原生编译器编译成 ARM64，配一层宿主垫片（shim）承接 Windows API。

> **当前状态（2026-10-14）：游戏在 PC 宿主上已通过全部资源加载、
> 完整跑完 17 个主菜单子系统初始化并稳定运行于主菜单循环（0 fault / 0 死循环）。
> 含同样修复的 HAP 已装机，待一次真机开机复测。**

---

## 总进度一览

| 阶段 | 状态 |
|---|---|
| 重编译工具链（PE → capstone → C） | ✅ 完成（约 8500 函数全量翻译，UNSUPPORTED=0） |
| 归档格式逆向（ARCH000 三种模式 + 双哈希 + fold 校验） | ✅ 100% 破解 |
| guest 运行时（平坦 4GiB 内存、寄存器级 CPU 模型、SEH/setjmp） | ✅ 完成 |
| Windows API 垫片（KERNEL32 / CRT / VCRUNTIME / MSVCP / WINMM…） | ✅ 完成（413 个导入槽全接） |
| KAGE 图形接缝（GL 后端 960×540、19 组着色器、纹理/帧缓冲） | ✅ 完成 |
| 资源加载链（22 归档、checksum 0 失败、pack 兜底 + DLC 优先级） | ✅ 完成 |
| **引擎启动 → 全部 XML/ANM2 加载 → 主菜单（PC 宿主实测）** | ✅ **2026-10-14 达成** |
| **真机开机复测（r42 HAP 已装）** | ⏳ **唯一近期卡点** |
| 音频（OpenAL → OHAudio） | ❌ 24 个导入是桩，全程无声 |
| 性能（fopen 扫描成本 ~8500 次/开机；bump 堆无 free） | ❌ 未优化 |
| 输入（触屏虚拟手柄方案已写在 `桌面手柄.md`，未实施） | ❌ 未实施 |

---

## 四个总根源（全部已修，每个都值得记住）

排障过程中沉淀下来的四类"一修修一片"的根因——后来所有怪现象都在这四类里：

1. **垫片空桩 / ABI 漂移**：VCRUNTIME 的 memcpy/memset/memchr/strstr 一度全是空桩
   （数据"流过变 0"→ 归档校验全败）；printf 前缀不写 NUL → `%n` abort；
   ISAAC 原生快速通道 esp 少恢复 4 字节 → 每块漏 4 字节 → 空指针虚调用整局重启。
   **教训：数据"流过变 0"先查导入表；清栈字节数以真 DLL 为准，不能猜。**
2. **归档格式逆向不完整**：三种模式（字密码 / 分块 deflate / +ISAAC 块）、
   双名字哈希（djb2 变体 + FNV-1a 变体自定义基值 0x5BB2220E）、
   fold 校验（初值 0xABABEB98，暂存区只在循环外 memset 一次）——
   任何一处不忠实都表现为"部分资源加载失败"。
3. **平台接缝复刻不完整**：KAGE::Sound::Manager::Initialize 的 seam 只标了 running，
   漏掉真实函数体里"分配 32 桶名字映射挂到 [manager+8]"——
   sounds.xml 注册时把桶写到了 guest 地址 0 附近。
   **教训：seam 必须复刻被跳过代码的全部对外可见状态，包括它分配的数据结构。**
4. **平坦内存的越界踩踏**：bump 堆（0x60000000 单调上涨）先后踩过
   Lua 1GiB 预留区（卷容器丢槽 → 22 归档挂载全败）、合成 TEB（0x7F000000，
   fs:[0x2c] 的 magic-static init epoch 被文件字节覆盖 → 全局惰性构造被整批跳过
   → 实体 tag 表 head=NULL → std::map::find 在 nil=0 上死循环 ~4700 万次/秒）。
   **教训：4GiB 平坦地址空间里，每个"保留窗口"都必须在分配器里显式禁区化。**

完整证据链与逐轮修复记录见 **`交接-新会话入口.md`**（唯一权威入口）。

## ⏳ 当前卡点与后续工作

### 近期（到"能玩"）

1. **真机开机复测**：r42 HAP（含 2026-10-14 两个修复）已 `bm install` 成功。
   开机后 purge 修复会重物化散装树（**需几分钟，别当卡死**），
   过 `Preload sound 533.` 后应到标题。验收判据：
   `Menu Title Init` / `Menu Game Init` 出现、`No layer with Id`=0、无高频 WDWATCH。
2. 若真机与 PC 行为分歧：对首个分歧点做设备专项（rawfd、真实 GLES/Maleon 驱动路径）。

### 中期（到"好听好用"）

3. **音频**：OpenAL32 的 24 个导入仍是桩（游戏静音但逻辑正常）→ 需接 OHAudio。
4. **性能**：每次开机 ~8500 次 fopen 的扫描成本；bump 堆无 free（纯单调上涨，长局有耗尽风险）。
5. **输入**：触屏虚拟手柄按 `桌面手柄.md` 的完整方案实施（可编辑布局 + Preferences 持久化）。

### 远期（打磨）

6. 良性 ASSERT 清理（anm2 层级/DLC 差异的告警刷屏）。
7. 存档完整闭环（目前读 GameState 已通，写回路径未压测）。

---

## 仓库结构

| 目录 | 内容 |
|---|---|
| `isaac-ohos/` | HAP 工程：guest 运行时、Windows API 宿主垫片、GLES 后端、ArkTS 壳 |
| `isaac-ohos/.../generated/` | 约 8500 个 x86 函数的逐指令 C 翻译（工具链产物） |
| `repentogxm/recomp/` | 重编译器工具链：PE 解析 → capstone 反汇编 → 逐指令翻译成 C |
| `pc-host/` | POSIX PC 宿主：与真机同一套运行时源码，秒级迭代调试（主战场） |
| `pc-host/scratch/` | 破解与诊断工具：归档解析/字密码/哈希/校验和/gdb 探针脚本 |
| `交接-新会话入口.md` | **权威交接文档**——当前状态、已修清单、卡点根因、下一步 |

## 运行时自供内容

仓库**不含**任何游戏资源。运行前需用户自备合法拷贝，放入：

```
isaac-ohos/entry/src/main/resources/rawfile/
├── isaac-ng.exe            # 游戏主程序（经 Steamless 脱壳）
├── kage_mount_points.dat   # 引擎挂载表
└── resources/packed/*.a    # 22 个资源归档
```

真机运行时 HAP 会把它们解包到应用沙箱 `files/resources/packed/`。

## 构建

### PC 宿主（WSL / Linux，秒级迭代）

```bash
wsl bash pc-host/run.sh [秒数]            # 增量编译 + 运行，TAIL=N 控制日志行数
```

调试探针全部由环境变量门控：`ISAAC_SEQ_TRACE`（文件 IO 序列）、`ISAAC_TRACE_RESOLVE`（路径解析）、
`ISAAC_TRACE_ABI`（调用约定校验）、`ISAAC_DBG_MAP`（音效名字映射）等。

### 真机 HAP

```bash
cd isaac-ohos && MSYS_NO_PATHCONV=1 cmd.exe /c "build.bat"
# 安装（大包必须走 file send + bm install -p，hdc install -r 会截断）：
hdc file send entry-default-signed.hap /data/local/tmp/isaac.hap
hdc shell bm install -p /data/local/tmp/isaac.hap
hdc shell aa start -a EntryAbility -b com.example.isaacoh
hdc hilog -T isaacoh                      # 观察进度
```

## 文档

- **`交接-新会话入口.md`** — 唯一权威入口：已完成勿重做清单、全部实测证据、
  每个卡点的根因与修复方向、工具与探针一览
- `pc-host/scratch/` — 归档破解全工具（`arch_decode.py` 解析+校验、
  `cipher0.py` mode0 密码、`isaac_ks.py` ISAAC 刷新、`xref_fold.py` 交叉验证）

## 法律

仓库不含任何游戏资源/代码数据段；运行所需内容均由用户自备合法拷贝在运行时提供。
翻译器只处理用户提供的可执行文件。本项目为技术研究性质，与 Nicalis 无关联。
