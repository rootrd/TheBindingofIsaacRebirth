# isaac-ohos — 以撒的结合鸿蒙移植骨架 v0.1

> 2026-09-14。接手文档见 `C:\Users\Administrator\Desktop\以撒鸿蒙移植-接手文档.md`（v2.1+）。
> 本工程是 Go/No-Go 阶段的真机载体：XComponent + NAPI + 原生渲染线程 + **VK 优先 / GLES 兜底** 双后端 + G0 图形栈探针。

## 立即运行（签名后）

1. DevEco Studio 打开本目录 → **File → Project Structure → Signing Configs → 勾选 Automatically generate signature**（需登录华为账号）→ OK。
   - 这一步会重新生成 `~/.ohos/config/default_isaac-ohos_*.p7b`（2026-09-14 00:04 那次只出齐了 cer/p12，**p7b 缺失**，签名步骤因此失败）。
   - DevEco 会自动更新 `build-profile.json5` 的 signingConfigs。
2. 点 Run，或命令行：
   ```
   build.bat
   hdc install -r entry/build/default/outputs/default/entry-default-signed.hap
   hdc shell aa start -a EntryAbility -b com.example.isaacoh
   hdc shell hilog -x | grep isaac
   ```

## 真机实测结果（2026-09-14，Pura 80 Ultra）——G0 通过 ✅

- **VK 后端（默认）**：Maleoon 920，Vulkan 1.3.309，VK_OHOS_surface，swapchain BGRA8×4 @2848x1276，**fps 锁 120**
- **GLES 后端**：libGLESv3 → OpenGL ES 3.2（104 扩展），着色器三角形 120fps
- **G0 双库探针**：同一上下文中 libGLESv2.so 的 glGetString 返回 NULL、libGLESv3 正常——gtasa 死因坐实（链接错库，非设备问题）
- 后端热切换、触控注入、横屏全屏、崩溃回溯全部实测通过
- 截图：`F:\TheBindingofIsaacRebirth\g2.jpeg`（VK 清屏）/ `g3.jpeg`（GLES 三角形）

## 签名现状（重要）

**当前 build-profile.json5 借用 gtasa 项目的调试签名材料，包名临时为 `com.example.gtasarootrd`**（2026-09-14 真机测试用，覆盖安装了已暂停的 gtasa App）。交付/正式使用前：
1. `AppScope/app.json5` 的 bundleName 改回 `com.example.isaacoh`
2. DevEco 重新生成新签名（原 isaac-ohos 的 p7b 未曾生成成功），更新 build-profile.json5
签名仅在安装时校验，随时可换。

## 启动后看什么（G0 判据，全部自动打印到 hilog）

- tag `isaac.vk`：Vulkan 完整能力（loader 版本、实例扩展、GPU 名/驱动 API/vendor、swapchain 参数）。**默认走 VK。**
- VK 初始化失败自动回落 GLES 时，tag `isaac.gles` 会打印 **G0 双库探针**：
  - `G0 linked-lib VENDOR/RENDERER/VERSION`（libGLESv3 路径，预期正常）
  - `G0 libGLESv2.so dlsym glGetString ...`（预期 NULL → 坐实 gtasa 死因 = 链接错库）
- 屏幕上应为旋转三角 + 缓变底色，左上角有 FPS（期望 ~60，FIFO 垂直同步）。
- UI 按钮 VK/GLES 可热切换后端（验证两套 present 路径，切换即重初始化）。

## 架构

```
entry/src/main/cpp/
├── main_ohos.cpp      NAPI 桥（init/destroy/touch/getFps/setBackend）+ 渲染线程 + 崩溃回溯进 hilog
├── backend.h          Backend 接口（init/probe/frame/shutdown）
├── backend_vk.cpp     Vulkan：VK_OHOS_surface + swapchain + clear 提交（transfer-dst 清屏路径）
├── backend_gles.cpp   EGL + GLES3 三角形 + G0 双库 glGetString 探针
└── CMakeLists.txt     链接 libEGL/libGLESv3/libnative_window/libvulkan 等
entry/src/main/ets/pages/Index.ets   XComponent 全屏 + FPS + 后端切换按钮
```

设计要点（与接手文档决策对应）：
- 原生渲染线程 + FIFO 垂直同步节拍（D6，不用 ArkTS 定时器）。
- 同一窗口 VK 与 EGL 不共存（Maleoon 闪屏坑，经验库 13 号文档），切换后端 = 销毁重建。
- `OH_NativeWindow_CreateNativeWindowFromSurfaceId(surfaceId)` 取窗口（xboxoh 验证过的路径）。
- 后端 init 失败自动互切并打点；所有诊断一行一条进 hilog，方便 `grep isaac`。

## 已验证 / 未验证

- ✅ hvigor 全链路构建、打包（未签名 HAP ~1.5MB）
- ✅ C++ 编译零告警（sysroot：libvulkan/libEGL/libGLESv3 齐备）
- ⏳ 真机部署与 G0 判据 —— 等签名 p7b 生成后一键可测
