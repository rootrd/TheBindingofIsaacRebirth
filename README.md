# 以撒的结合：忏悔 — HarmonyOS 静态重编译移植

把 Windows 版《Binding of Isaac: Repentance》(isaac-ng.exe) 通过**静态重编译**
（static recompilation）移植到 HarmonyOS 鸿蒙手机上运行。

不做模拟器、不做解释器逐条翻译——而是把 x86 机器码**离线翻译成 C 代码**，
用目标平台的原生编译器编译成 ARM64，配一层宿主垫片（shim）承接 Windows API。

## 仓库结构

| 目录 | 内容 |
|---|---|
| `isaac-ohos/` | HAP 工程：guest 解释运行时、Windows API 宿主垫片、GLES 后端、ArkTS 壳 |
| `repentogxm/recomp/` | 重编译器工具链：PE 解析 → capstone 反汇编 → 逐指令翻译成 C |
| `pc-host/` | POSIX PC 宿主：与真机同一套运行时源码，秒级迭代调试 |
| `pc-host/scratch/` | 破解工具：归档解析/字密码/名字哈希/校验和，全部可独立复现 |
| `交接-新会话入口.md` | **权威交接文档**——当前状态、已修清单、下一会话入口 |

## 运行时自供内容

仓库**不含**任何游戏资源。运行前需用户自备合法拷贝，放入：

```
isaac-ohos/entry/src/main/resources/rawfile/
├── isaac-ng.exe            # 游戏主程序（经 Steamless 脱壳）
├── kage_mount_points.dat   # 引擎挂载表
└── resources/packed/*.a    # 22 个资源归档
```

真机运行时 HAP 会把它们解包到应用沙箱 `files/resources/packed/`。

## 当前进度（2026-10-08）

### ✅ 已打通

| 里程碑 | 状态 |
|---|---|
| guest 启动 → 引擎版本横幅 `Repentance v1.7.9b.J835` | ✅ |
| 归档挂载 + 22 归档 20031 条目全量读取 | ✅ |
| 资源校验 `checksum is invalid` **2737 → 0** | ✅ |
| PersistentGameData / options.ini / mods 枚举 | ✅ |
| KAGE GL 后端初始化 **960x540**、`glViewport` | ✅ |
| 19 组着色器编译（`glCreateShader`/`glCompileShader`/`glLinkProgram`） | ✅ |
| 真机渲染循环 **fps=120**（Maleoon 920 GLES 3.2） | ✅ |
| PC ↔ 真机故障完全同源（PC 修 = 真机修） | ✅ |

### 归档格式（已 100% 破解，工具在 `pc-host/scratch/`）

```
头 14 字节:  "ARCH000" + mode(1B) + index_offset(u32) + entries(u16)
索引 20B/条: h1(u32) h2(u32) offset(u32) size(u32) checksum(u32)
```

- **mode 0**（graphics/music/sfx/videos）= 存储型 + 字密码：
  `key = (h2 ^ 0xF9524287) | 1`，每字 `w = permute(w ^ key, key & 0xF)`
  （2=全反序, 9=(b1,b0,b3,b2), 13=rotr16），key 迭代 `k^=k<<8; k^=k>>9; k^=k<<23`
- **mode 1**（animations/config/fonts/rooms）= 分块 deflate（`[u32 len][data]`，bit31=末块）
- **mode 2**（afterbirth*/repentance*）= 分块 deflate + 0x400 字节 ISAAC 块（粘性开关）
- **名字哈希**（查条目用）= 双哈希：djb2 变体（初值 0x1505, ×33）
  + FNV-1a 变体（**自定义基值 0x5BB2220E**, ×0x01000193），归一化=转小写+`\`→`/`，
  查找名 = 挂载卷前缀 + 相对路径
- **校验和** = `fold`: `s = rotr32(s,1) + word`，初值 **0xABABEB98**，
  0x200 暂存区只在循环外 memset 一次（尾部折叠上一块残留字节——不模拟这点只有 24% 命中）

### 一路修掉的总根源（每个都值得记住）

1. **VCRUNTIME memcpy/memset/memchr/strstr 全是空桩**——归档校验器 memcpy 拷了个寂寞，
   数据永远全 0。数据"流过变 0"先查导入表。
2. **printf 垫片格式前缀不写 NUL**——strlen 读到未初始化栈，重建格式串变 `%nllx`
   → glibc 保护 abort。
3. **ISAAC 原生快速通道 esp 少恢复 4 字节**——每解密 0x400 块漏 4 字节，
   累计后 `pop esi` 取到 0 → refill(NULL) → 空指针虚调用 → 整局重启。
4. **名字哈希未破解** → 散文件路径无法路由进归档 → 贴图加载全败。
   破解后宿主 fopen 失败路径可"哈希查表→解密→物化到原路径"兜底。

### ⏳ 当前唯一卡点：PNG 解码后的虚调用

散文件贴图（shadow.png / caustics.png）物化解码**已成功**（setjmp/longjmp 修复后
png_error 归零），但解码把像素写进了 KAGE 文件包装对象头部——布局与归档条目不同，
vtable 被像素数据覆盖 → 后续 `call [vt+0x6c]` 读到 GLFW 字符串字节 → 整局重启。

**下一手（已写入交接文档）**：在 0x5a0c7e 解码返回后恢复包装对象 vtable
（已试：restamp 0x30764894 后 +0x6c 走通，新的 fault 移到 release 调用
`[edi]=0`——像素覆盖范围比预期大，需要给包装对象补齐归档条目布局的尾部字段，
或让工厂在 loose 路径直接构造纹理类对象）。

### 真机运行（需一次手动解锁）

```bash
# 数据线连接、屏幕解锁后：
hdc install -r isaac-ohos/entry/build/default/outputs/default/entry-default-signed.hap
hdc shell aa start -a EntryAbility -b com.example.gtasarootrd
hdc shell hilog -x | grep isaacoh    # 观察进度
```

## 构建

### PC 宿主（WSL / Linux，秒级迭代）

```bash
wsl bash pc-host/build.sh                 # 全量构建（约 3 分钟）
wsl bash pc-host/run.sh [秒数]            # 运行，TAIL=N 控制日志行数
```

调试探针全部由环境变量门控：
`ISAAC_DBG_PACK`（归档兜底）、`ISAAC_DBG_SJ`（setjmp/longjmp）、`ISAAC_DBG_TEX`（纹理对象）、
`ISAAC_SEQ_TRACE`（全部文件 IO）、`ISAAC_TRACE_ABI`（调用约定校验）。

### 真机 HAP

```bash
cd isaac-ohos && build.bat                # DevEco Studio hvigor 构建
```

## 文档

- **`交接-新会话入口.md`** — 唯一权威入口：已完成勿重做清单、全部实测证据、
  每个卡点的根因与修复方向、工具与探针一览
- `pc-host/scratch/` — 归档破解全工具（`arch_decode.py` 解析+校验、
  `cipher0.py` mode0 密码、`isaac_ks.py` ISAAC 刷新、`xref_fold.py` 交叉验证）

## 法律

仓库不含任何游戏资源/代码数据段；运行所需内容均由用户自备合法拷贝在运行时提供。
翻译器只处理用户提供的可执行文件。
