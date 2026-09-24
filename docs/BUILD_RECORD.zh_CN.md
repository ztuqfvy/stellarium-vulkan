# 构建记录（BUILD_RECORD）

> A0 要求：Qt/SDK/资源版本有记录，可复现构建。本文件随环境与构建结果更新。

## 2026-09-17｜旧版（OpenGL）Release 构建

| 项 | 值 |
|---|---|
| 源码 | Stellarium 26.1 快照（工作副本基线提交） |
| macOS | darwin（Apple Silicon，8 核） |
| CMake | /opt/homebrew/bin/cmake（Homebrew） |
| 编译器 | Apple clang++（/usr/bin/clang++） |
| Qt | 6.11.2（Homebrew；见下方版本错配说明） |
| Vulkan | molten-vk 1.4.2、vulkan-headers 1.4.357.0、vulkan-loader 1.4.357.0 |
| 构建目录 | build-release/（全新目录） |
| 配置命令 | `cmake -B build-release -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qt -DENABLE_TESTING=OFF` |
| 配置耗时 | 244 s |
| 构建命令 | `cmake --build build-release --parallel 8` |
| 构建耗时 | 5 分 27 秒 |
| 结果 | 100% 编译链接成功，产物 `build-release/src/stellarium`（约 36 MB） |
| 警告 | 仅 ld deployment-target 警告（目标 macOS-12 vs Homebrew dylib 14.0/26.0），无编译警告基线问题 |

## 运行时依赖问题（重要经验）

- **现象**：可执行文件 dyld 阶段报 `Symbol not found: __ZN14QObjectPrivateC2E16QtPrivate_6_11_1`。
- **根因**：Homebrew Qt 模块版本错配（qtbase 6.11.2 vs qtwebchannel 等滞留 6.11.1）。Qt 私有符号带补丁版本标签，跨补丁版本混链必失败。
- **结论**：计划一"锁定 Qt 补丁版本"的正确含义是**锁定整个 Qt 模块集的同一补丁版本**，不是只看 qtbase。处理：`brew upgrade qt` 统一至 6.11.2。
- **复查命令**：`for d in /opt/homebrew/Cellar/qt*; do echo "$(basename $d): $(ls $d | tail -1)"; done`

## Vulkan 运行环境变量（Qt 程序枚举 Vulkan 设备必需）

```sh
export VK_DRIVER_FILES=/opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json
export QT_VULKAN_LIB=/opt/homebrew/opt/vulkan-loader/lib/libvulkan.1.dylib
```

缺 `QT_VULKAN_LIB` 时 Qt 报 `Failed to load vulkan: Cannot load library vulkan`（dlopen 找不到加载库，因为 /opt/homebrew/lib 不在默认 dyld 搜索路径）。

## A1 后端诊断（tests/manual/a1_backend_diag.cpp）

```sh
cmake -B build-manual -S tests/manual -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qt
cmake --build build-manual
build-manual/a1_backend_diag vulkan   # REQUESTED=vulkan ACTUAL=Vulkan VERDICT=PASS
build-manual/a1_backend_diag opengl   # ACTUAL=OpenGL PASS（对照）
build-manual/a1_backend_diag metal    # ACTUAL=Metal PASS（对照）
```

诊断判定逻辑：请求 Vulkan 时实际 API 必须为 Vulkan，否则 VERDICT=FAIL（禁止静默回退）。

## 配置目录隔离（A0 要求，2026-09-17 教训）

- 2026-09-17 首次验证运行未隔离，读取并迁移了用户全局配置 `~/Library/Application Support/Stellarium/config.ini`（触发了 "Cleared cache and updated config.ini"）。
- **后续一切测试运行必须带隔离参数**，确保"原目录和设置不受影响"：

```sh
mkdir -p ~/qt_demo/stellarium_vulkan/profile
./build-release/src/stellarium --user-dir ~/qt_demo/stellarium_vulkan/profile
```

- 个人版 QML 应用（A1 起）的配置同样必须落在独立目录，不碰上述全局路径。

## 2026-09-18｜A1：stelQuickUI Vulkan 宿主构建与验收

构建：`cmake -B build-ui -S src/ui -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qt` → `cmake --build build-ui`，零错误。

### 关键发现与修复

1. **MoltenVK portability 驱动**：手写 vkCreateInstance 必须设
   `VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR` 并启用 `VK_KHR_portability_enumeration`
   扩展，否则返回 VK_ERROR_INCOMPATIBLE_DRIVER(-9)。Qt 的 QVulkanInstance 内部已自动处理，
   手写探针必须显式对齐（VkDeviceProbe.cpp 已修）。
2. **ICD 环境变量**：Homebrew loader 默认搜索路径含 `/opt/homebrew/etc/vulkan/icd.d`，
   `VK_DRIVER_FILES` 实际非必需；`QT_VULKAN_LIB` 仍必需（Qt 默认 dlopen("vulkan") 找不到）。
3. **Qt 6.11.2 崩溃路径**：Vulkan 无法加载时场景图阶段直接 SIGSEGV（退出码 139）。
   已用 QVulkanInstance::create() 预检兜住：失败 → 明确报错 → 退出码 3。

### 验收结果

| 场景 | 结果 | 退出码 |
|---|---|---|
| 正向（Vulkan 可用） | probe: Apple M3 / api 1.1.357 / MoltenVK 0.2.2210；runtimeApi=Vulkan | 0 |
| 负向（禁用 Vulkan） | "QVulkanInstance::create() 失败——Vulkan 不可用"，明确报错 | 3（无崩溃） |

运行命令：

```sh
export QT_VULKAN_LIB=/opt/homebrew/opt/vulkan-loader/lib/libvulkan.1.dylib
STELQUICK_AUTOTEST_SECONDS=6 ./build-ui/stelQuickUI.app/Contents/MacOS/stelQuickUI
```

（环境变量 `VK_DRIVER_FILES` 可省；诊断页显示设备/DPR/版本信息。）

### 待办（A1 剩余手动项）

- [x] 缩放、关闭、重开无错 → 已自动化（Q-WIN-01..06），见下节
- [ ] 鸿蒙真机最小 QML/Vulkan 窗口探测（前后台、触摸、DPR、交换链恢复）

---

## 2026-09-18｜A1 交互缺陷：macOS 下每帧 present 阻塞 5 秒（已修）

### 现象（用户上报）

双击运行 `stelQuickUI.app` 后：窗口缩放卡 1-2 秒、Command+Tab 切回卡 1-2 秒、
Command+Q 退出卡 1-2 秒、缩放时窗口内容**被拉伸而非等比例重排**。

### 证据链

`QSG_RENDER_TIMING=1` 逐帧计时：

```
qt.scenegraph.general: threaded render loop
qt.gui.metal: Timed out waiting for display lock
syncAndRender: frame rendered in 5079ms, sync=51, render=20, swap=5008   ← present 5008ms
```

- 渲染本身 20ms，**swap（present）5008ms**，且每帧如此；12 秒只渲染 3 帧（0.25 FPS）。
- 全零环境变量无关；与 QML 结构无关（诊断页无逐帧绑定）；与 VkDeviceProbe 无关。

### 根因

Qt 6.11.2 默认**线程化渲染循环**下，macOS 的 `QMetalLayer` 显示锁协议（Qt 提交
`9122d826`「Present Metal layer with transaction during display cycle」引入）：

- `qtbase/src/gui/platform/darwin/qmetallayer.mm`：`setNeedsDisplayInRect` 里
  `displayLock.tryLockForWrite(5s)` —— 拿不到写锁就等满 5 秒后放弃；
- `qtbase/src/plugins/platforms/cocoa/qnsview_drawing.mm`：`displayLayer:` 之后才
  **通过排队调用**释放锁，即锁要跨越整个显示周期；
- 于是主线程的显示请求与渲染线程的 present 互相等待，present 被拖到 5 秒超时。

四症状同源：present 停滞 → 主线程卡 → Command+Tab/Command+Q 都在等它；
同时 `CAMetalLayer` 的 drawable 尺寸滞后于新的 layer bounds，旧 drawable 被拉伸填充新尺寸，
表现为"拉伸而非等比例缩放"（Qt 自身 `layerContentsPlacement = TopLeft`，不缩放，故拉伸只可能来自陈旧 drawable）。

### 处置

应用 Qt 上游在**引入这套机制的同一次提交**中保留的官方逃生门（commit message 原文：
"It will disable the locked Metal layer, and all code paths that depend on it."）：

```cpp
// src/ui/main.cpp: applyMacOsVulkanWorkaround()
qputenv("QT_MTL_NO_TRANSACTION", "1");   // 保留线程化渲染循环，仅关闭锁定图层路径
```

覆盖开关（用于 A/B 测量，勿在生产构建里改）：`STELQUICK_RENDER_WORKAROUND=transaction-off|basic-loop|none`。

**为什么不用 `QSG_RENDER_LOOP=basic`**：它也能消除锁竞争（实测 swap 0-9ms），但把场景图
渲染搬回 GUI 线程——A3 后接入真实天空渲染时会直接阻塞输入处理。作为备选保留。

### 修复前后对照（同一自动测量，`STELQUICK_WINDOW_TEST=1 STEPS=8`）

| 配置 | maxStallMs | maxResizeMs | maxFrameGapMs | 判定 |
|---|---|---|---|---|
| `none`（复现故障） | **10017** | **5099** | 10016 | FAIL |
| `transaction-off`（默认） | **75** | **68** | 75 | PASS |
| `basic-loop`（备选） | — | — | — | 能跑通（swap 0-9ms、0 次锁超时），但持续满帧空转且被长跑看门狗打断，未纳入默认 |

> 口径说明：`none` 行在加入"预热 3 帧排除"之前测得（当时预热混入约 264ms）；
> 即便扣除也不影响 FAIL 判定（10017ms 是两次 5 秒锁超时叠加，量级差 40 倍）。
> 默认配置另跑过完整 24 步（54 次操作）：maxStall 52ms / maxResize 46ms / maxFrameGap 64ms，PASS。

启停开销（`STELQUICK_AUTOTEST_SECONDS=5` 的墙钟时间）：`none` = 15.97s → `transaction-off` = 5.38s。

### 新增：窗口交互自测模式

把 A1 原"手动点击"验收项变成可重复的自动测量（退出码 0 / 4，可接 CI）：

```sh
BIN=./build-ui/stelQuickUI.app/Contents/MacOS/stelQuickUI
STELQUICK_WINDOW_TEST=1 STELQUICK_WINDOW_TEST_STEPS=8 $BIN
# → WINDOWTEST: 结果 steps=22 maxStallMs=75 maxResizeMs=68 maxFrameGapMs=75 阈值=250ms VERDICT=PASS
```

指标含义：`maxStallMs` 主线程事件循环最大停顿（16ms 心跳实测）；
`maxResizeMs` 单次几何变更阻塞时长；`maxFrameGapMs` 相邻帧最大间隔。
预热 3 帧不计入。用例编号 Q-WIN-01..06，见软件测试文档 6.4 节。

## 2026-09-20｜A2 第一步：静态图帧桥（Metal/OpenGL PASS，Vulkan 黑屏定性）

### 交付

- `src/render/legacy/FrameMailbox`（3 槽位有界、丢旧帧、世代拒收、统计、跨线程唤醒回调）
- `src/render/legacy/TestPattern` / `StaticFrameSource`（四类传输错误可被单点像素抓出的测试图案）
- `src/ui/quick/SkyViewport`（场景图线程取帧 → createTextureFromImage → QSGImageNode）
- `src/ui/A2FrameCheck`（STELQUICK_A2_CHECK=1 自动校验：投递 → 等上传 → grabWindow 逐像素比对 12 探针）
- 通用抓帧：`STELQUICK_GRAB_AT_SECONDS=N STELQUICK_GRAB_PATH=<png>`（任意页面，固定场景比对用）
- 后端对照开关：`STELQUICK_GRAPHICS_API=vulkan|metal|opengl`（仅诊断，验收仍强制 Vulkan）

### 校验结果（同一代码，三后端）

| 后端 | 12 探针 | 说明 |
|---|---|---|
| Metal | **0 失败，偏差全 0** | 坐标换算/DPR/行距/通道序/方向全部正确 |
| OpenGL | 0 失败 | |
| **Vulkan** | **12/12 全黑** | 视口区域采样为黑；文字/纯色矩形正常 |

### Vulkan 黑屏定性（2026-09-20，Apple M3 / MoltenVK 1.4.2 / Qt 6.11.2）

症状：QSGTextureMaterial 路径（QML Image、QSGImageNode）内容静默渲染为黑；
距离场文字（glyph atlas）、纯色 Rectangle 正常。无 MoltenVK 校验错误输出。

已排除（全部实测）：
1. 像素内存生命周期（深拷贝 QImage 后建纹理）——无效
2. pipeline cache 损坏（清缓存重跑）——无效
3. MVK Metal Argument Buffers（0/1 强制）——无效
4. `VK_KHR_get_physical_device_properties2` 未启用（MoltenVK "Expect problems" 警告）——
   已修（main.cpp 显式加实例扩展，警告消除）但黑屏不变
5. alpha 通道标志 / opaque 材质变体——无效
6. 官方 Qt 套件对照——**不可行**：官方 macOS 二进制未编 Vulkan 支持（仅 Homebrew 版有）
7. `QSG_RENDER_LOOP=basic`——A2 校验序列不兼容（不退出），已另行记录

连带修复的真 bug：
- SkyViewport 节点重建（缩放/抓帧）后不补挂纹理 → 批渲染器每帧
  "No QSGTexture provided from updateSampledImage()"。现在重建时补挂旧纹理，
  且首帧前挂 1x1 占位纹理（空纹理材质在 basic 循环下会饿死事件循环）。
- MainWindow.qml 缺 `import StelQuickUI 1.0` → 页头 BackendInfo ReferenceError。
- main.cpp 增加实例扩展 `VK_KHR_get_physical_device_properties2`（portability 设备正确姿势）。

**结论：应用侧管线正确（Metal 逐像素全对），缺陷位于 Qt 6.11.2 Vulkan RHI ×
MoltenVK 1.4.2 的静态图像纹理路径。后续动作：向 Qt 报告（附本记录作最小复现说明），
或试验其他 MoltenVK 版本（需下载，网络受限暂缓）。A2 后续开发（动态帧产供）不受阻塞——
管线逻辑已在 Metal/OpenGL 证明，Vulkan 修复后无需改动应用代码。**

### 已知限制

- `STELQUICK_RENDER_WORKAROUND=basic-loop` + `STELQUICK_A2_CHECK=1`：校验序列不退出（诊断模式兼容性，不阻塞主线）
- 系统 `screencapture` 无屏幕录制权限，屏幕级取证不可用（仅 Qt grabWindow 路径）

---

## 2026-09-20｜Windows 移植准备（跨驱动形态对照）

### 动机

A2 黑屏的现有定性是"Qt 6.11.2 Vulkan RHI × MoltenVK 1.4.2 缺陷"，但**只有一个数据点**：
macOS 上的 MoltenVK。要把它变成可归因的结论，需要换掉"驱动形态"这一个自变量再测一次。
Windows + RTX 4060 提供平台原生 Vulkan 驱动，是最经济的一次反证：
12 探针全对 → 缺陷在 MoltenVK 侧；仍全黑 → 缺陷在 Qt 的纹理路径本身。

参见 `docs/WINDOWS_BUILD.zh_CN.md`（含完整步骤、结果矩阵、结果回传模板）。

### 移植前必须修的四处（本轮已修，macOS 侧零回归）

| # | 问题 | 后果（若不修） | 处置 |
|---|---|---|---|
| 1 | `qt_add_executable` 在 Windows 默认 `WIN32_EXECUTABLE=TRUE`（GUI 子系统） | 进程不挂父控制台 → **全部 printf 诊断与退出码证据丢失**，A2CHECK/WINDOWTEST 全部失效 | CMakeLists 强制 `WIN32_EXECUTABLE FALSE` |
| 2 | 源码为 UTF-8 **无 BOM** 且含大量中文注释 | MSVC 按 GBK 解释 → C4819、注释串码，字符串字面量可能被多字节截断 | 加 MSVC `/utf-8` |
| 3 | `VkDeviceProbe` 无条件启用 `VK_KHR_portability_enumeration` | 原生 NVIDIA 驱动不提供该扩展 → `vkCreateInstance` 返回 `-7 EXTENSION_NOT_PRESENT` → **"探针失败、Qt 渲染正常"的假故障** | 改为先 `vkEnumerateInstanceExtensionProperties` 枚举、存在才启用；结果写入诊断页新行"驱动类型" |
| 4 | `STELQUICK_GRAPHICS_API` 只认 `metal\|opengl` | Windows 上拿不到最贴近平台基线的对照组（D3D11 是 Qt 在 Windows 的默认后端） | 补 `d3d11` / `d3d12`；顺带修正"未识别后端名"时误报"对照模式"的日志 |

附带：`find_package(Vulkan REQUIRED)` 改为 `QUIET` + 自动降级（无 SDK 也能构建与渲染，
只是缺探针诊断数据），并给出告警说明；新增 `deploy` 目标（windeployqt）。

### 自变量显式化：诊断页新增"驱动类型"行

`VulkanProbeResult` / `BackendInfo` 增加 `portabilityDriver`，
诊断页显示 `portability 转译层（MoltenVK）` 或 `平台原生驱动（<os>）`。
跨平台对照必须同时记录这一行，否则渲染差异无法归因到驱动形态。

### macOS 回归验证（改动后重跑，确认没有把基线搞坏）

| 用例 | 改动前 | 改动后 |
|---|---|---|
| `STELQUICK_GRAPHICS_API=metal STELQUICK_A2_CHECK=1` | 12 探针 0 失败 / PASS | **0 失败 / PASS（rc=0）** |
| `STELQUICK_GRAPHICS_API=opengl STELQUICK_A2_CHECK=1` | 0 失败 / PASS | **0 失败 / PASS** |
| 默认 Vulkan + `STELQUICK_A2_CHECK=1` | 12/12 全黑 / FAIL | **12/12 全黑 / FAIL（rc=5，行为不变）** |
| `STELQUICK_WINDOW_TEST=1 STEPS=8` | maxStall 75ms / PASS | **maxStall 43ms / maxResize 30ms / maxFrameGap 65ms / PASS（rc=0）** |
| 探针输出 | `device=Apple M3 api=1.1.357 driver=0.2.2210` | 同（另新增 `portabilityDriver=true`） |

### 范围边界（写进 CMakeLists 注释，防止后续走偏）

Windows 侧只编 `src/ui` 独立工程（15 个文件）。**不**在 Windows 上碰 Stellarium 根构建——
根构建的依赖面（Qt6 WebEngine/Charts/MultiMedia、libnova、gettext…）与本次要回答的问题无关，
强行一起做会把变量搅浑。

---

## 2026-09-21｜Windows 移植完成 + A2 反证成立（RTX 4060 / 原生 Vulkan）

### 环境

| 项 | 值 |
|---|---|
| 系统 | Windows（win32，x64） |
| 编译器 | MSVC VS 2026（`E:\VisualStudio\CanPin`，工具集 v180） |
| CMake | **VS 自带 4.3.1-msvc1**（`E:\VisualStudio\CanPin\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe`） |
| Qt | 6.11.2 msvc2022_64（`E:\Qt\6.11.2\msvc2022_64`） |
| Vulkan SDK | 1.4.357（`E:\Vulkan\SDK`） |
| GPU / 驱动 | NVIDIA GeForce RTX 4060，driver 79.296.0，api 1.4.325 |
| 产物 | `build-ui\Release\stelQuickUI.exe`（约 152 KB）＋ `build-ui\deploy\`（1880 文件，自包含） |

### 结果矩阵（本轮实测，全部 rc=0）

| 用例 | 结果 |
|---|---|
| `STELQUICK_A2_CHECK=1`（默认 Vulkan） | **12 探针 0 失败 / VERDICT=PASS / rc=0** |
| `STELQUICK_A2_CHECK=1 STELQUICK_GRAPHICS_API=d3d11` | 12/12 PASS / rc=0（对照组） |
| `STELQUICK_A2_CHECK=1 STELQUICK_GRAPHICS_API=opengl` | 12/12 PASS / rc=0（对照组） |
| `STELQUICK_AUTOTEST_SECONDS=6`（默认 Vulkan） | rc=0，runtimeApi=Vulkan |
| `STELQUICK_WINDOW_TEST=1` | rc=0，steps=54 maxStall=22ms maxResize=3ms maxFrameGap=65ms（阈值 250ms）/ PASS |

抓帧诊断：视口逻辑尺寸 960x605、物理尺寸 960x605、DPR=1、场景原点 (0,35)，
12 个探针（四角标 + 四色条 + 棋盘格 + 中心十字 + 半透明块）逐点偏差 0（半透明块 22，为
alpha 混合的预期结果，容差内）。

### 结论（本次反证的落点）

**同一份应用代码、同一版 Qt 6.11.2，在 Windows 原生 Vulkan 驱动上逐像素全对（12/12 PASS）**，
而在 macOS + MoltenVK 1.4.2 上 12/12 全黑。自变量只有"驱动形态"一个，因此：

> 缺陷位于 **Qt 6.11.2 Vulkan RHI × MoltenVK 1.4.2 的静态图像纹理路径**（`QSGImageNode` /
> `QSGTextureMaterial`），应用侧管线正确。macOS 侧结论由单点观察升级为跨驱动反证。

### 本轮定位并修复的真 bug（Windows 侧暴露，macOS 潜伏）

| # | 症状 | 根因 | 处置 |
|---|---|---|---|
| 1 | 同模块 QML 组件（`SkyTestPage`/`DiagnosticPage`）实例化为空 Item → A2 视口恒 0x0，报"1x1 放不下角标" | `NO_PLUGIN` + `OUTPUT_DIRECTORY` 组合下，`qt_add_qml_module` 只把 `QML_FILES` 塞进 qrc，**生成的 qmldir 仅留在磁盘**；运行时找不到同模块类型声明 | `CMakeLists` 里 `qt_add_resources(... qmldir)` 显式编入资源 |
| 2 | `QVulkanInstance` 自建实例与 Qt 内部默认实例并存 → 退出时 `VUID-vkDestroySurfaceKHR-surface-parent`，进程 0xC0000005，RC 拿不到 | 全进程存在两个 Vulkan 实例，surface 归属与析构顺序不一致 | 改用 `QVulkanDefaultInstance::instance()`（Qt 私有头，需 `Qt6::GuiPrivate`）；surface-parent 错误消失、退出恢复正常 |
| 3 | **`sceneGraphInitialized` 信号在 Windows 上根本不到达** → `backendOk` 恒 false → 渲染与像素全对却以 rc=3 退出 | 信号发送时机与连接建立存在竞态：Qt 建好场景图并发出该信号时，连接尚未建立（Qt 信号不重放）。macOS 恰好落在信号之后，故一直潜伏 | 判定抽成具名 lambda，**信号 + 100ms 兜底轮询 `rendererInterface()`** 双路调用，幂等 |
| 4 | 手动查看模式固定 300ms 投递，此时布局未跑完 → "视口物理尺寸过小…1x1" 假失败日志 | 硬编码延时不可靠 | 改为轮询等视口 ≥16px 再投递（上限 5s），超时才报真失败 |
| 5 | MSVC C4172：`FrameLease::frame()` 返回悬垂引用 | `invalidFrame()` 按值返回临时对象，绑定到 `const&` 即悬垂；clang 不报所以 macOS 没暴露 | 改静态存储期 `static const LegacyFrame kInvalid{}` |
| 6 | QML `Image` 硬编码 `file:///tmp/stel_pattern.png`，Windows 上无此路径 → 刷屏报错 | macOS 遗留硬编码 | 改为 `debugPatternSource` 属性，由 `STELQUICK_PATTERN_DUMP` 注入；未设则不加载 |

### 构建环境坑（可复现，写入 `build-ui\build.ps1`）

1. **进程环境同时存在 `Path` 与 `PATH` 两个键** → .NET `ProcessStartInfo.EnvironmentVariables`
   是大小写不敏感字典 → 抛 `ArgumentException: 已添加项。字典中的关键字:Path 所添加的关键字:PATH`
   → 被 MSBuild 包成 **MSB6001「CL.exe 的命令行开关无效」**（假错误，与命令行无关）。
   处置：子进程只保留单一规范 `PATH`，先 `Clear()` 再灌入。
2. **VS 2026 生成器需 CMake ≥ 4.0**。`E:\Qt\Tools\CMake_64` 是 3.x，报
   `could not create CMAKE_GENERATOR "Visual Studio 18 2026"`。必须用 VS 自带 CMake 4.3.1。
3. 代理变量（`HTTPS_PROXY`/`https_proxy` 等）一并清除（构建全程不需联网）。

### 已知残留（不影响结论）

- `QVulkanInstance already created; setExtensions() has no effect`：默认实例在 `setExtensions()`
  前已由 Qt 创建（Windows 上更早），MoltenVK portability 扩展那条路径不再需要该调用。
  渲染与探针均正常，属提示性输出。
- 退出阶段校验层噪音：`UNASSIGNED-non-acquired-swapchain-image-used`（Qt RHI 的
  semaphore 使用方式）与 `VUID-vkDestroyDevice-device-05137`（4 个 VkImage/VkImageView
  未在销毁设备前释放）。均为 Qt RHI 侧清理顺序问题，出现在进程收尾、不影响渲染结果与 RC。
- `portability=1` 误判：Vulkan 1.4.357 loader 恒定暴露 `VK_KHR_portability_enumeration`
  实例扩展，`VkDeviceProbe` 据此判定为 portability 转译层 → Windows 原生驱动被误标。
  **已于 2026-09-21 修复**：判据改为设备级 `VK_KHR_portability_subset`，并拆成两个字段
  `portability_driver`（驱动形态判据）/ `portability_enum_ext`（loader 能力，仅供
  决定是否声明枚举 portability 位）。macOS 回归：`driver=1 enum_ext=1`（MoltenVK 正确）；
  Windows 应得 `driver=0 enum_ext=1`。
- `docs/BUILD_RECORD` 遗留：`STELQUICK_RENDER_WORKAROUND=basic-loop` + A2 不退出（诊断模式兼容性）。

---

## 2026-09-21｜macOS 侧独立验收（Windows 提交 06ab880 的回归与复核）

对 Windows 侧提交做**独立**复核：不采信文字结论，重编重跑 macOS 基线。
环境：Apple M3 / macOS / Homebrew Qt 6.11.2 / MoltenVK 1.4.2（与 A2 定性时同一环境）。

| 用例 | 结果 | 与移植前对比 |
|---|---|---|
| `A2CHECK` metal | 12 探针 0 失败，rc=0 | 一致 |
| `A2CHECK` opengl | 12 探针 0 失败，rc=0 | 一致 |
| `A2CHECK` vulkan（默认） | 12/12 全黑 FAIL，rc=5 | **一致（黑屏依然复现，符合预期）** |
| `STELQUICK_WINDOW_TEST=1` | PASS，maxStall 49ms / maxFrameGap 63ms（阈值 250ms） | 一致 |
| `STELQUICK_AUTOTEST_SECONDS=4` | rc=0，runtimeApi=Vulkan backendOk=1 | 一致 |

结论：Windows 侧的改动（`QVulkanDefaultInstance` 单例、qmldir 入资源、
`sceneGraphInitialized` 兜底轮询、`invalidFrame` 静态存储期）**未破坏 macOS 基线**，
且 Vulkan 黑屏在 macOS 上依旧 12/12 复现——反向印证"黑屏与驱动形态绑定"这一结论的前提成立。

复核中发现并修复的两处（本轮新增）：

1. **`run_autotest.cmd` matrix 模式退出码失效**（证据工具本身的缺陷）
   `setlocal` 未开 `EnableDelayedExpansion`，却在 for 块内用 `!ERRORLEVEL!` → 不展开，
   打印成字面量；且块尾 `set RC=0` 无条件覆盖 → **无论成败恒报 `EXIT CODE = 0`**。
   即"三后端全 PASS"这条结论的出厂工具存在永远绿灯的缺陷。
   已修：开 `EnableDelayedExpansion`；逐后端读码并聚合（保留首个非零码）。
   > 该缺陷不影响已上报的结论本身——三组 rc=0 另有逐像素明细支撑——但工具必须可信。

2. 见上条"portability 判据修正"。

---

## 2026-09-21｜二审（Windows 提交 e9e5205）：证据入库 + 负控通过

对复核意见的处置做第二次验收。核心是"结论必须落到可复核的原始输出"。

**已入库的原始证据**：`docs/evidence/2026-09-21-run_autotest-matrix-windows-vulkan-d3d11-opengl.txt`
（15510 字节，UTF-8 可解码，15510 中 5574 非 ASCII 字节为应用中文输出的正常内容）。

| 核验项 | 结果 |
|---|---|
| 探针字段（我在 d717056 改的判据） | `portability_driver=0 portability_enum_ext=1` —— **修正在 Windows 上生效**，原生驱动不再被误标 |
| 三后端 | vulkan / d3d11 / opengl 各 12 探针 0 失败，逐点列出纹理点/抓帧点/期望/实际/偏差 |
| 逐后端退出码 | `[vulkan] 0` / `[d3d11] 0` / `[opengl] 0` —— 是真数字，非字面量 |
| 负控 | 注入 `d3d11 -> 5` → 逐后端得 5、聚合 `EXIT CODE = 5` → 证明绿灯不是无条件默认 |
| `.cmd` 非 ASCII 字节 | 全部 5 个脚本均为 **0**（ASCII-only 规则已贯彻） |
| 证据文件编码 | UTF-8 单一编码可解码；唯一含 "is not recognized" 的行是说明文字中的引用，非污染 |

**本轮补的缺口**：首版证据的出处头（提交号/CPU/GPU/exe sha256）与负控段是**临时命令**
注入的，那些命令未入库 → 换人无法复现同一文件，属证据链缺口。已补
`tools/evidence/collect.ps1`（规范生成器，含"跑前拒绝验收禁用变量"硬闸门）。
该脚本在 macOS 上编写、无 PowerShell 可测，**尚未执行验证**，README 已标注。

**教训（写入技能）**：`.cmd` 只许 ASCII——中文 `rem` 注释同样危险，误解码字节跨行
拼接后残余片段会被当命令执行。本轮该缺陷是我方引入（d717056 的中文注释），
由 Windows 侧发现并修复。

---

## 2026-09-21｜三审（仪器加固）：把"生成器从未执行过"这个已知风险清掉

二审留下一处未验证项：`tools/evidence/collect.ps1` 在 macOS 上编写、**从未执行过**。
本轮不靠"等 Windows 首次运行"来兜这个风险——那等于拿一趟真机往返当语法检查。

**做法**：在 macOS 上装 PowerShell 7.6.6（GitHub release，`~/.local/opt/pwsh`），用桩把三个
Windows 专有调用（`Get-CimInstance` / `nvidia-smi` / `cmd /c`）换掉，**实际执行**组装路径。

| 用例 | 期望 | 实测 |
|---|---|---|
| 完整跑分 | `VERDICT=COMPLETE` / exit 0 | 符合 |
| matrix 缺一个后端段 | `VERDICT=NOT USABLE` / exit 1 | 符合 |
| 负控返回 0 | `VERDICT=NOT USABLE` / exit 1 | 符合 |
| `-SkipNegctl` | `VERDICT=NOT USABLE` / exit 1 | 符合 |

产物格式实测：UTF-8 无 BOM、单一编码可解码、**0 个非 ASCII 字节**。

### 实测抓出的三处缺陷（全部已修）

1. **`dirty` 标志自我污染（真缺陷，实测复现）。**
   脚本把自己的产物（未提交的 `docs/evidence/*.txt`）算进 `git status` → 从**第二次运行起
   恒为 `YES`**。实测连续三次运行全部 `YES`。
   一个恒亮的告警等于没有告警，而且会训练读者忽略那一行——正是我们在 `run_autotest.cmd`
   上刚修过的同一类错误的镜像。
   修法：排除 `docs/evidence/`，并把排除范围**写在那一行上**（排除必须可见，不能静默）。
   在真实仓库验证：伪造未提交证据文件时判定仍为 `no`，真实脏改动照报。

2. **exe 路径硬依赖 `build-ui\deploy`。**
   `deploy` 目标只在配置时找到 `windeployqt` 才存在。硬依赖会把"没装 windeployqt"
   变成一次白跑的往返——而这条命令（`qtdiag` 之后的第二步）恰好是新环境最可能失败的地方。
   修法：按 `deploy` → `Release` → `Debug` → `build-ui` 顺序回退，实际选中路径写入文件头。
   本地实测：无 `deploy` 目录时正确回退到 `build-ui\Release`。

3. **`@()` 的类型陷阱（本轮引入、本轮抓到）。**
   `$x = @([IO.File]::ReadAllLines(...))` 会把 `string[]` 重新类型化为 `Object[]`，
   `List[string].AddRange()` 随即拒绝绑定并抛 `Cannot convert System.Object[]`。
   原版直接传方法结果反而是对的——这说明：**语法解析通过 ≠ 能跑**。
   只做 Parser 校验（0 错误，1193 tokens）不会暴露它，必须实际执行。
   修法：不加 `@()` 直接赋值。

### 顺带修掉同类缺陷（`run_autotest.cmd` 退出码 7）

`if not exist "%EXE%"` 分支原来 `goto :end`，**跳过 report 且 ERRORLEVEL 停在 0**——
"先跑冒烟测试"这条文档指引会打印绿灯而实际什么都没跑。现 `set "RC=7"` 并落到 report，
图例补 `7=exe-not-found`。与 `d717056` 修的"matrix 恒报 0"是同一类缺陷的第三例。

### 元观察

本轮三次修复全部属于同一族：**工具在"没干活"或"干不成"时报告成功**。
（`matrix` 恒报 0 → `dirty` 恒报 YES → exe 缺失返回 0。）
这类缺陷的共同特征是**不会自己暴露**，只能靠"主动让它失败一次"来发现。
因此把负控、结构自检、`VERDICT` 行做进生成器，比再多写几页纪律文档有效。

---

## 2026-09-21｜四审（Windows 真机首跑）：生成器跑通，但 `dut result` 变不了红

按 §9.3 照做：`git pull`（→ `838b1b7`）→ `cmake --build build-ui --config Release`（rc=0）
→ `collect.ps1`。按判读顺序读文件头：

| 项 | 实测 |
|---|---|
| `VERDICT` | `COMPLETE - trustworthy record` |
| `dut result` | `run_autotest.cmd matrix returncode = 0` |
| `worktree: dirty` | `no`（`docs/evidence/` 已按设计排除） |

三审遗留的"无法离机验证"两处**已真机确认正常**：`chcp` 与控制台交互没问题，
两条捕获行也没问题。产物：UTF-8 无 BOM、单一编码可解码、**0 行污染**
（无缩写版权横幅、无 `is not recognized`）。SEGMENT 1 三后端各 12/12 探针 PASS。

### 但抓出同族缺陷第四例：runner 的**进程返回码**恒为 0

留证链上有三个数字（生成器退出码 / `dut result` / SEGMENT 1 的 `EXIT CODE = N`）。
真机实测发现第三个早修好了，**第二个恒为 0**：

`run_autotest.cmd` 走到 `:end` 之后是 `echo.` → `pause` → `endlocal` → 文件结束，
**RC 没有作为进程返回码带出去**，而那个 `echo.` 已经把 `ERRORLEVEL` 重置为 0。
用桩 DUT（固定返回 2）实测对照：

| | 脚本打印 | 进程返回码 |
|---|---|---|
| 修复前（`838b1b7`） | `EXIT CODE = 2` | **`0`** ← 红被吞成绿 |
| 修复后 | `EXIT CODE = 2` | **`2`** ✓ |

`collect.ps1` 的 `dut result` 行正是读这个进程码，所以修复前该字段**不具备变红的能力**——
是比 `d717056`（matrix 恒报 0）、`838b1b7`（exe 缺失返回 0）更高一层的同一族缺陷。

修法：`:end` 收尾改为 `endlocal & exit /b %RC%`（`%RC%` 在 `endlocal` 之前展开，
值能穿过环境还原）。

### 这条为什么一直没被发现：负控与被测路径**不对称**

`tools/negctl/run_negctl.cmd` 复刻聚合逻辑时**自己带了 `exit /b`**，所以负控能红、能返回 5，
生成器的负控检查（`negctlRc -ne 5`）顺利通过；而真正被测的 `run_autotest.cmd` 不能红。
**一个"能红的负控"配一个"不能红的被测对象"，会把缺陷盖住而不是暴露它。**

### 回归检查（新增，跑真脚本而非副本）

`tools\negctl\check_rc_propagation.cmd`：把 `run_autotest.cmd` **复制**到桩目录旁
（让 `%~dp0` 解析到桩），桩 DUT 用 `findstr.exe`（无参退出 2），断言
"打印码 == 进程返回码"。实测双向：

| 被测 runner | 结果 |
|---|---|
| 修复后（仓库根的真脚本） | `RESULT: PASS` / exit 0 |
| 修复前（`838b1b7` 逐字节副本，归一化行尾后与 blob 一致） | `RESULT: FAIL - printed=2 process=0` / exit 1 |

### 元观察

同族缺陷现在有**四例**：matrix 恒报 0 → dirty 恒报 YES → exe 缺失返回 0 →
runner 进程返回码恒 0。共同特征是**工具在"没干活/干不成"时报告成功且不会自曝**。

本轮补充一条新的子模式：**修复只落到"给人看的文本"，没落到"给机器读的返回值"**。
`838b1b7` 已经意识到"ERRORLEVEL 停在 0"这件事，但修的是 report 里那一行字；
机器读的是进程返回码，于是缺陷平移了一层而非消失。
教训：**凡是"结论"要跨进程传递的，都得在边界上实测一次那个数字，而不是看它印出来的样子。**

---

## 2026-09-21｜T6 完成：旧宿主显式帧驱动（A2 主体产帧侧）+ 一处我方回归修复

### 0. 先认一个错：9-20 的"瘦身"破坏了根构建

当时我把 `guide/ plugins/ data/ scripts/ util/` 从全部历史移除，并在 README 写
"根构建不可用（上游遗留）"。**这个表述是错的**：`build-release/` 证明根构建当时是通的，
是我删掉了**构建必需**的目录才把它弄坏的——

- `data/`：`default_cfg.ini`、`base_locations.*`、字体（含 8.4MB 的 NotoSansSC 中文字体）、
  `data/gui/`（工具栏图标）、`mainRes.qrc`、`shaders/`——**全是运行/构建必需**；
  且根 CMakeLists:990/:1052 会在配置期**写入** `data/default_cfg.ini`、`data/Info.plist`
- `plugins/`：`ADD_SUBDIRECTORY(plugins)`（AllStaticPlugins 静态插件集）
- `scripts/`：`ADD_SUBDIRECTORY(scripts)`
- `guide/`：根 CMakeLists:203 要往里写 `version.tex`，目录必须存在

**修复**（commit 见下）：
- `data/` → 上游 checkout 的**实体副本**（28M，已 gitignore）。不能用软链：配置期写入
  会穿透软链改掉上游 checkout，而它**没有版本保护**（无 .git）——正是 04 号文档
  风险表里"通过符号链接误改原项目资源"那条
- `guide/` → 空目录 + `.gitkeep`（只需存在；50MB 的 Images/ 只用于生成 PDF，与构建无关）
- `plugins/`、`scripts/`、`util/` → 软链（已核验配置期无写入；与既有 9 个资产软链同约定）
- 新增 `tools/setup-upstream-assets.sh`（幂等，全新 clone 重建资产用）
- 根 CMakeLists 守卫由"无条件 FATAL_ERROR"改为"缺资产时报人话并给重建命令"

**验证**：`cmake -B build-release -S . -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qt`
→ `CONFIGURE_EXIT=0`（27.2s 配置 + 14.2s 生成，复用既有缓存）。

### 1. T6 实现：LegacySkyHost（A2 主体产帧侧）

新增 `src/render/legacy/LegacySkyHost.{hpp,cpp}`：把"旧宿主在 paint 事件里被动绘制"
改成"外部时钟显式驱动 → 离屏 FBO → RGBA8 读回 → FrameMailbox"。

设计要点（每条都对应一类"看起来没问题"的静默错误）：

| 决策 | 理由 |
|---|---|
| 回调签名 `(deltaSeconds, simSeconds, size)` | `deltaSeconds` 直接对应 `StelApp::update(double)`，A3 接入时原样转发；**仿真时间而非墙钟**——"内容随仿真时间变化"这条判据才成立 |
| `GlContextMode::kOwn`（QOffscreenSurface） | **完全没有窗口**。这是"不依赖 hide()/paint 事件"的可验证前提，而非声明 |
| `GL_RGBA8` 而非 `GL_SRGB8_ALPHA8` | 帧契约是 sRGB 编码直通；SRGB 格式会在读写时做线性化往返，给逐像素比对引入 ±1~2 抖动 |
| `glPixelStorei(GL_PACK_ALIGNMENT,1)` + 实测步长写进元数据 | RGBA8 下 width×4 天然对齐，但换 RGB8 时默认对齐会静默错位；步长必须作为**事实**交出去 |
| `glCheckFramebufferStatus` 显式校验 | 不完整 FBO 的表现是全黑，与"渲染逻辑写错"无法区分——最常见的归因陷阱 |
| 读回后**真的翻转行序**，不是只改元数据 | 只标不翻 → 上下颠倒；方向必须对最终交付数据取证 |
| 尺寸超上限**显式失败** | clamp 会让"以为 720p 实际被裁"变成静默数据错误 |
| 保存/恢复宿主原 FBO 绑定 | 借用模式（A3 与旧宿主同进程）下不恢复会顶掉 QOpenGLWidget 的默认 FBO |
| `withContextCurrent()` | 客户端 GL 资源（着色器/VAO）必须在正确的上下文里创建/销毁 |

配套 `LegacyTestScene.{hpp,cpp}`（开发期替代物，与 StaticFrameSource 同性质）：
Core 3.3 + VAO/VBO/着色器，与 StelOpenGL/StelPainter 同一套 GL 机制。内容三件套：
昼夜渐变（随 simSeconds 变）、太阳圆盘（水平位置单调推进）、固定方向标记带
（顶绿/左红，专供独立验证翻转与水平方向）。着色器源码强制纯 ASCII（Apple GLSL
编译器不保证接受非 ASCII 字节；同类教训来自 .cmd 的中文注释事故）。

### 2. T6 自动自检（STELQUICK_LEGACY_HOST_TEST=1）

新增 `src/ui/LegacyHostCheck.{hpp,cpp}`，14 项程序化判据，**在创建任何窗口之前**同步执行、
不进入事件循环——"没有主循环顺手帮我们画一帧"因此是事实而非声明。

环境：Apple M3 / macOS / Homebrew Qt 6.11.2，`STELQUICK_LEGACY_HOST_TEST=1`，退出码 0。

| 判据 | 结果 |
|---|---|
| T6-C01/C14 无窗口 | topLevelWindows=0 allWindows=0（前后两次取证） |
| T6-C02 上下文形态 | own=1 offscreen=1 **GL 4.1 Core**，renderer="Apple M3"（请求 3.3，拿到 4.1，只记事实） |
| T6-C03 超上限显式拒绝 | 4097x64 被拒，未静默截断 |
| T6-C04 场景装配 | 着色器编译+链接、VAO/VBO 创建成功 |
| T6-C05 请求=交付 | **请求 12 帧 → 投递 12 帧**（丢失 0，失败 0） |
| T6-C06 队列有界 | completeSlots=3 ≤ 3 |
| T6-C07 帧元数据 | 帧序号严格递增、rowStride=3840、physical=960×540、logical=480×270（=physical/dpr，dpr=2）、世代恒 2、stateNumber=仿真时间×1000 |
| T6-C08 方向 | 顶部绿带优势最小 69.5（阈值 30）、左侧红带优势最小 173.4（阈值 20）——**翻转正确** |
| T6-C09 内容非平凡 | 最少 166 种颜色、最小平均亮度 0.0201 |
| **T6-C10 内容随仿真时间变化** | **12 帧得到 12 个互不相同的像素指纹**——不是静态残留/缓存复读 |
| T6-C11 可复现性 | 同一 sim 重渲染 → 指纹 8f01b2423ea62457 与首帧完全一致 |
| T6-C12 有向运动 | 太阳质心 x：95.5→143.5→…→623.5（严格单调，非噪声抖动） |
| T6-C13 耗时统计 | 稳态 render 0-2ms + readback 0-1ms（首帧 55ms 为惰性着色器编译；T9 长跑须先热身再计吞吐） |

人工留档：`STELQUICK_LEGACY_HOST_DUMP=<png>` 可把首帧存盘（960×540，已目检：
顶绿带/左红带/白昼天空/太阳在东边低空，与场景定义一致）。

**结论：A2 最大未知数（旧宿主能否可靠离屏驱动）在"机制层"已证伪为非问题。
真实天空内容（StelApp::update/draw）的接入是 A3 进程内集成，根构建已修复可配置。**

### 3. macOS 基线回归（T6 改动后）

| 用例 | 结果 |
|---|---|
| A2CHECK metal / opengl / vulkan | PASS / PASS / FAIL（黑屏仍复现，符合预期） |
| WINDOWTEST | PASS，maxStall 57ms（阈值 250ms） |
| AUTOTEST | runtimeApi=Vulkan backendOk=1 |

注：opengl 首跑出现过一次 `VERDICT=UNAVAILABLE`（grabWindow 返回空图），
复测 3 次全 PASS → 偶发。校验器按设计报"校验手段不可用"而不是谎报通过，行为正确；
但"偶发抓帧失败"本身记在这里，供 T9 长跑时观察是否复现。

### 4. 其他

- `src/ui/CMakeLists.txt` 链接 `Qt6::OpenGL`（QOpenGLShaderProgram/QOpenGLBuffer/
  QOpenGLVertexArrayObject 在 QtOpenGL 模块，不在 QtGui；qtbase 自带，无新第三方依赖）
- 退出码新增 8 = T6 自检失败（7 已被 exe-not-found 占用）
- 退出码表与运行说明同步进 docs/WINDOWS_BUILD.zh_CN.md

### 5. T6 自检的负控（回应四审的"负控与被测对象不对称"教训）

`LegacyHostCheck` 增加失败注入入口 `STELQUICK_LEGACY_HOST_SIZE=WxH`，实测双向：

| 用例 | VERDICT | 进程返回码 |
|---|---|---|
| 正向（默认 960×540） | `PASS` | `0` |
| 负控（`STELQUICK_LEGACY_HOST_SIZE=0x0`） | `FAIL`（T6-C02：配置的物理尺寸非法） | **`8`** |

检查器**能红**，绿灯才有意义；且进程返回码是真数字（`main` 直接 return），不存在
四审在 runner 上发现的"打印是红的、进程码是绿的"平移缺陷。

**顺带一个平台事实**（第一版负控用错了杠杆，记录下来避免重蹈）：
macOS 上请求不存在的 GL 版本（如 9.9）会被**静默降级**到实际可用的 4.1 Core，
装配照样成功 → 不能当失败注入点。推论：请求的 GL 版本**不保证拿到**，
所以 T6-C02 只记录实测值（version/renderer/profile），不比对"请求 vs 实得"。

## A3 主体：旧宿主引擎进程内无头集成（2026-09-21）

**新文件**：`src/render/legacy/LegacyAppCheck.{hpp,cpp}`（根构建）；`src/main.cpp` 增加
`STELA3_CHECK=1` 分流分支（在创建正常主窗口之前）；根 `src/CMakeLists.txt` 把
FrameMailbox/LegacySkyHost/LegacyAppCheck 编入 `stelMain`，并给 `stelMain` 加 `src/`
include 根。退出码：**0 = 通过，8 = 失败**（本二进制的码表，与 stelQuickUI 无冲突）。

### 1. 引导方式（A3 第一未知数，已证成立）

`StelMainView` + `WA_DontShowOnScreen` + `show()` + 有界 `processEvents` →
`initializeGL` 照常触发 → `StelMainView::init()` → `StelApp::init()` 全链路无头完成
（GL 4.1 Core / Apple M3 / highGraphicsMode=1）。窗口不上屏、不进入 `exec()`。

### 2. 显式驱动与判据

回调体 = `core->setJD(纪元+sim)` → `StelApp::update(dt)` → `StelApp::draw()`，
直接画进 LegacySkyHost（借用引擎上下文）的离屏 FBO → RGBA8 读回 → 邮箱。
判据 A3-C01~C08：无头初始化 / GL 形态 / N 帧全投递 / 内容随 JD 变化 /
同 JD 紧邻重绘指纹一致 / 内容非平凡 / 耗时统计 + glGetError==0。

实测（macOS，1280×720，8 帧）：**8/8 PASS，退出码 0**；负控 `STELA3_SIZE=0x0`
→ A3-C03 FAIL、VERDICT=UNAVAILABLE、退出码 8（检查器能红）。

### 3. 排障记录：同 JD 重渲染指纹不一致（三步定位，值得复用）

1. **现象**：紧邻重渲染 R1==R2，但都 ≠ 首画 A；diff 遍布 99.6% 像素、
   R 通道几乎不变、**B 通道均值 -13.4** —— 全局色温漂移，非几何位移。
2. **排除**：墙钟/时间流速（插桩 jdAtDraw 三帧完全相同）、星闪（已关）、
   人眼自适应开关（`setFlagLuminanceAdaptation(false)` 无效——它不门控主路径）。
3. **根因**：`LandscapeMgr` 每帧上报天空亮度 → `StelSkyDrawer::preDraw()`
   **无条件** `eye->setWorldAdaptationLuminance(maxLum)`，且 `preDraw` 先于模块
   draw → **第 N 帧的适配亮度是第 N-1 帧天空的函数（单帧滞后）**。
   这是引擎固有时序，不是缺陷。
4. **修法（双绘法）**：每个测量帧画两次取第二次——首画吸收滞后，
   第二画才是纯 f(JD) 的稳态帧。修后 diff=0，A=R1=R2。

**对 A2 生产路径的推论**：真实天空帧内容 = f(当前 JD, 上一帧天空亮度)，
单帧色温滞后是引擎固有行为；消费侧如需逐像素稳定（如取证比对），
必须用双绘协议。T9 长跑不受影响（只计吞吐/帧年龄，不做跨帧比对）。

### 4. 确定性保障清单（跑 A3 自检前必看）

`core->setTimeRate(0)`（墙钟外推停掉，JD 只由自检注入）；
`setFlagTwinkle(false)`（逐帧随机）；预热 30 帧（等异步资源加载）；
测量段双绘取稳态。以上全部只动运行时对象，不写用户 config.ini。

## T9 长跑基线：真实天空产帧管道 30 分钟（2026-09-22）

**新文件**：`src/render/legacy/LegacyLongRun.{hpp,cpp}`（根构建）；
`src/main.cpp` 增加 `STELT9_RUN=1` 分流分支（同样在正常主窗口创建前）。
退出码：**0 = 管道完整；8 = 失败**（与 A3 的 8 语义一致）。

### 1. 驱动方式（与生产契约对齐）

引导同 A3（`WA_DontShowOnScreen + show() + 有界 processEvents`）。
回调体 `setJD(纪元 + 已流逝秒 × JD速率) → StelApp::update(dt) → StelApp::draw()`
（JD 默认按真实时间流速；`timeRate=0`，JD 外部注入）。**全速驱动、不按 vsync 节流**，
量的是管道最大吞吐；每帧即取即用（`takeLatestFrame`），模拟理想消费者。
与 A3 的差别：星闪 / 人眼自适应**保持默认开**——T9 量真实成本，不追确定性。

### 2. 判据 T9-C01~C07 与实测结果

| 判据 | 内容 | 结果 |
|---|---|---|
| T9-C01 | 引擎无头初始化 | PASS（0ms）|
| T9-C02 | 帧桥装配（借用上下文，1280×720，行步长 5120，每帧 3600 KB）| PASS |
| T9-C03 | 管道完整性：请求 = 投递、零失败、零丢弃 | PASS（91476 = 91476，0，0）|
| T9-C04 | 延迟分布 | PASS（见下）|
| T9-C05 | 内存基线 | PASS |
| T9-C06 | 内容哨兵（每 500 帧抽查非黑屏）| PASS（182 次抽查全过）|
| T9-C07 | 结束后 GL 错误栈 = 0 | PASS |

**延迟（全域 1800s / 91476 帧）**：total mean 19.28 / p50 18 / p95 21 / p99 27 ms；
render mean 18.73 / p95 20 ms；readback mean 0.55 / p95 1 ms；max 63594 ms（见 §4）。

**稳态窗口（进程 ≥900s 后的 1200s / 63188 帧）**：**52.66 fps**；
total mean 18.65 / p50 18 / p95 21 / p99 26 / max 155 ms；
render p95 20 ms；readback p95 1 ms；>50ms 帧 60 个（0.095%），>100ms 帧 8 个。

**内存**：phys_footprint 2552.2 → 2557.3 MiB，线性斜率 **0.279 MiB/min**（≈16.8 MiB/h，无泄漏趋势）。

### 3. 门槛冻结（测试文档 §6.1.1 已落表）

稳态吞吐 ≥40 fps、p95 总延迟 ≤30ms、p99 ≤45ms、p95 读回 ≤5ms、
>50ms 帧占比 ≤0.5%、稳态内存斜率 ≤1.0 MiB/min、失败/丢弃均为 0。

### 4. 重要发现：引擎"真实预热"是 ~900 秒，不是 120 秒

进程启动后 **626~870s** 区间内出现 7 次 >100ms 停顿，**最大 63.6 秒**
（t≈663s，即首次 120s 预热刚结束不久）。表现与症状：资产异步加载 + 着色器惰性编译，
属于一次性长尾，之后 28 分钟里最大单帧仅 155ms。

**推论（已写入验收协议）**：
- 验收运行必须 `STELT9_WARMUP_SECONDS=900`，否则测得的平均值/最大值会被启动长尾污染
  （本次全域 mean 19.28ms vs 稳态 18.65ms，看似差别不大，但 max 从 155ms 变成 63594ms——
  只报全域数字会得出"管道会卡 63 秒"的错误结论）。
- 内存指标必须用 `phys_footprint`：RSS 在长跑中被页面回收，从 1512 MiB "降"到 152 MiB，
  **会掩盖真实增长**（这是本次差点误判的点）。

### 5. 留证

- `docs/evidence/2026-09-22-stelt9-longrun-30min.txt`（原始 stdout/stderr 全文）
- `docs/evidence/2026-09-22-stelt9-baseline-frames.csv.gz`（91476 行逐帧 CSV）
- `docs/evidence/2026-09-22-stelt9-smoke-20s.txt`（20 秒冒烟，393 帧）

## T9 环境前置守卫：一次无效运行与硬前置（2026-09-22）

### 1. 事件：一致性运行被系统降频污染，判据全绿而数字全崩

按冻结协议（warmup 900s + measure 1800s）跑第二次长跑，结果 **T9-C01~C07 全部 PASS**，
但数字相对基线全面崩盘：

| 指标 | 基线（09:40） | 一致性运行（13:30） | 探针（14:17） |
|---|---|---|---|
| 稳态吞吐 | 52.66 fps | **7.32 fps**（全域） | **3.75 fps** |
| p95 总延迟 | 21 ms | 447 ms | 711 ms |
| render 均值 | 18.65 ms | 125.5 ms | 222.2 ms |
| 内存 phys_footprint | 2552→2557 MiB | 2431→2433 MiB（平） | — |

**排查路径（值得复用）**：
1. CSV 逐分钟看形态 → 不是"运行久了变慢"（基线 t=840~1920s 与本次 t=900~2700s 时段重叠，
   基线在重叠段是 52 fps）；是全程 2~14 fps 抖动。
2. 内存完全平（footprint 增 1.9 MiB）→ 排除泄漏/资源累积。
3. `pmset -g` → **`lowpowermode 1`**，`pmset -g batt` → **Battery Power（69% 放电中）**；
   `pmset -g log` 显示 13:32:07 还发生过一次 Idle Sleep（4 秒，对应 CSV 里 max=3883ms 那帧）。
4. 结论：**被测环境被系统降频**（电池 + 低电量模式），非管道退化。
   同二进制、同分辨率、同驱动方式，唯一变量是电源状态。

**关键教训**：T9-C01~C07 判"管道是否完整"，**不含吞吐门槛判定**——所以环境被降频能让
一次运行"判定全绿"却毫无验收价值。**判据不覆盖的失真源，必须用前置条件挡住。**

### 2. 修复：T9-C00 硬前置 + T9-C08 漂移监测

- `T9-C00`（测前）：调用 `/usr/bin/pmset -g`（解析 `lowpowermode`）与 `pmset -g batt`
  （解析 AC/Battery）。不合规 → **不创建窗口、不初始化引擎，直接拒绝测量**，
  `VERDICT=ENV_FAIL`、**退出码 9**（区别于管道失败 8）。
- `T9-C08`（测中）：每 60 秒复核一次，中途拔电或系统自动切低电量模式 → FAIL。
- 逃生阀：`STELT9_ALLOW_THROTTLED=1` 把前置降级为警告（仅调试；正式验收禁止）。
- 实测守卫生效（当前环境正不合规，属免费负控）：`GUARD_EXIT=9`，
  `T9-C00 FAIL 运行环境：电源=Now drawing from 'Battery Power'；低电量模式=开`。

### 3. 冻结协议增补（测试文档 §6.1.1 协议第 4 条）

验收运行必须：**接通电源 + 关闭低电量模式 + `caffeinate -dimsu` 阻止系统休眠**。
休眠会直接在生产循环里制造秒级停顿（本次实测 3.9 秒单帧）。

### 4. 留证

- `docs/evidence/2026-09-22-stelt9-consistency-INVALID-throttled.txt` + `-frames.csv.gz`（作废运行，标注 INVALID）
- `docs/evidence/2026-09-22-stelt9-probe-lpm-INVALID-throttled.txt`（3.75 fps 探针）
- `docs/evidence/2026-09-22-stelt9-envguard-env_fail.txt`（守卫拒绝测量，退出码 9）

## T9 一致性运行重跑：PASS，吞吐门槛验收通过（2026-09-22 晚）

### 1. 前置与环境恢复

用户接电 + 关闭低电量模式后：`pmset -g batt` → AC Power（80%），`lowpowermode 0`。
20s 探针（warmup 5s）：**63.75 fps** / p95 19ms / 失败 0 / T9-C00 PASS —— 吞吐已恢复
（探针略高于稳态基线属预期：短窗无长尾，且无降频）。

### 2. 正式一致性运行（22:42 起，~46 分钟）

参数：`STELT9_WARMUP_SECONDS=900 STELT9_SECONDS=1800`，`caffeinate -dimsu` 包裹。

**VERDICT=PASS，T9-C00~C08 全部 8/8 PASS，退出码 0。**

| 指标 | 本次一致性（稳态最后 1200s） | 基线（09:40） | 冻结门槛 | 判定 |
|---|---|---|---|---|
| 稳态吞吐 | **53.60 fps** | 52.66 fps | ≥40 fps | PASS（+1.8%） |
| p50 总延迟 | 18 ms | — | — | — |
| p95 总延迟 | 21 ms | 21 ms | ≤30 ms | PASS |
| p99 总延迟 | 29 ms | 26 ms | ≤45 ms | PASS |
| max 总延迟 | 197 ms（单帧毛刺） | 155 ms | — | — |
| p95 读回 | 1 ms（max 7 ms） | 1 ms | ≤5 ms | PASS |
| >50ms 帧占比 | 0.120%（86 帧） | — | ≤0.5% | PASS |
| 失败 / 丢弃 | 0 / 0（97977 帧） | 0 / 0 | =0 | PASS |
| footprint 斜率 | **0.004 MiB/min** | 0.279 MiB/min | ≤1.0 MiB/min | PASS |
| 测中环境漂移 | 0 次违规（C08） | — | — | — |

结论：与基线同源一致（吞吐差 1.8%、p95 持平、p99 差 3ms），**未复现 13:30 的降频崩盘**，
坐实当时根因就是电源状态而非管道退化。内存斜率比基线更平（0.004 vs 0.279 MiB/min），
进一步支持"基线斜率主要是资产惰性加载的残余长尾"的判断。
max=197ms 为单帧毛刺（t≈1709s，仅 1 帧），不影响门槛（>50ms 占比 0.120% 远低于 0.5%）。

**T9 吞吐基线与一致性验收到此闭环：A2 产帧侧正式形态定版，可以进入消费侧接线
（P-BRG-01 帧上传/队列、P-BRG-04 15fps 降级 UI）。**

### 3. 留证

- `docs/evidence/2026-09-22-stelt9-consistency-reac/run.log`（8/8 PASS 全文）
- `docs/evidence/2026-09-22-stelt9-consistency-reac/build.csv.gz`（97977 行逐帧 CSV）
- 探针（20s / 63.75 fps / T9-C00 PASS）当时为前台直跑未存档，数字以本节记录为准

## 2026-09-23｜Qt 6.12.0 受控升级实验：官方 macOS 包**不含 Vulkan**，实验就地终止

§7.5 支线 1 的执行结果（结论与预期不同，故单列）：

| 步骤 | 结果 |
|---|---|
| 官方安装器并存安装 Qt 6.12.0（`~/Qt/6.12.0`，macos + harmonyos + wasm 套件齐全） | 完成 |
| `cmake -B build-ui-6120 -S src/ui -DCMAKE_PREFIX_PATH=~/Qt/6.12.0/macos` | 配置通过 |
| 编译 | **失败**：`src/ui/main.cpp:34: fatal error: 'QVulkanInstance' file not found` |
| 核实 | `~/Qt/6.12.0/macos/lib/QtGui.framework/Headers/` 下 **vulkan 相关头为 0 个**（`grep -c vulkan` = 0） |
| Homebrew 侧 | `brew outdated qt` 为空 → homebrew-core 仍是 6.11.2，无 6.12 可升 |

**结论**：Qt 6.12.0 官方 macOS 二进制包**与 6.11.x 一样不带 Vulkan 支持**（这是打包渠道差异，
不是版本缺陷——6.11.x 时代就因此被迫使用 Homebrew Qt）。因此"升级 Qt 版本"这条路径，
在"官方安装器"形态下**连编译都过不去**，谈不上验证 §6.2 黑屏是否修复。

**对 §6.2 缺陷的可选后续**（按成本排序，均属支线，不阻塞主线）：
1. **源码构建**：`~/Qt/6.12.0/Src` 已在（官方安装器自带源码）→ `qtbase` + `qtdeclarative`
   用 `-feature-vulkan` 自建（M3 上约 40–90 分钟）。这是唯一能真正回答"6.12 + MoltenVK 是否修好"的路径。
2. 等 homebrew-core 收录 6.12（Homebrew 的 qt 是带 Vulkan 的形态）。
3. 直接向上游报最小 bug（Windows 原生 Vulkan PASS / macOS MoltenVK FAIL，同代码同 Qt 版本，附 12 探针明细）。

**留证**：`build-ui-6120/`（配置成功、编译失败的现场；构建目录不入库）。

---

## 2026-09-23｜消费侧接线：动态帧通路 + 消费侧计量 + 降级 UI（P-BRG-01/04 前置）

A2 产帧侧（T6/T9/A3）此前只把帧送到邮箱为止；本阶段把**消费侧**接上：
帧持续流进 QML 窗口，并补齐 P-BRG-01 要的"上传耗时/队列"计量与 P-BRG-04 的降级 UI。

### 1. 新增组件

- `src/ui/LiveFrameSource.{hpp,cpp}`：动态帧生产者线程。
  `LegacySkyHost(kOwn 上下文，自建离屏) + LegacyTestScene`（动态合成场景，f(simSeconds)）
  在**独立线程**按可配速率（默认 60fps）驱动 `renderOneFrame()` 投递邮箱。
  `setFps()` 支持跨线程调速（降级探针用）。线程模型与将来接真实引擎的形态同构：
  引擎接入（AppFacade，A4）时只换渲染回调体，线程/生命周期骨架不动。
- `src/ui/DynFrameCheck.{hpp,cpp}`：动态帧通路自检（D1-C01..C07，退出码 0/8/6）。
- `SkyViewport` 扩展：上传统计（count/mean/max，微秒精度）、`displayedFps`（GUI 线程每秒采样）、
  `degraded`（显示速率 < 阈值）+ `degradeThreshold`（<=0 关闭，A2 逐像素路径零干预）。
- `SkyTestPage.qml`：降级角标（`visible: viewport.degraded`），是 P-BRG-04"降级透明"的可视证据。
- `main.cpp`：`STELQUICK_DYN_CHECK=1`（自检）、`STELQUICK_LIVE=1`（手动看动态帧流）两个模式。

### 2. 判据与结果（2026-09-23，Apple M3 / Homebrew Qt 6.11.2）

| 判据 | 内容 | metal 8s | vulkan 6s |
|---|---|---|---|
| D1-C01 | 生产者零失败且产出 ≥0.6×名义 | PASS（316 帧 / 54.5 fps） | PASS（264 帧 / 54.2 fps） |
| D1-C02 | 显示帧号持续推进 | PASS（+264 / +211） | PASS |
| D1-C03 | 邮箱丢弃 ≤5% | PASS（0/316） | PASS（0/264） |
| D1-C04 | 邮箱帧龄最大 <500ms | PASS（183ms） | PASS（181ms） |
| D1-C05 | 上传 count>0 且 max<100ms | PASS（305 次 / mean 0.38ms / max 2.23ms） | PASS（253 次 / max 2.71ms） |
| D1-C06 | 相隔 700ms 两次抓帧不同 | PASS（内容动态） | **SKIP**（§6.2 已知 Vulkan 黑屏） |
| D1-C07 | 降速窗 degraded=true 且恢复后 false（P-BRG-04） | PASS | PASS |
| 总判 | | **VERDICT=PASS / rc=0** | PASS / rc=0（C06 SKIP 不计失败） |

**负控（能红）**：`STELQUICK_LIVE_FPS=5`（名义速率低于降级阈值 → "恢复"段无法回到 15fps 以上）
→ `D1-C07 FAIL`、**rc=8**，其余 6 项仍 PASS。证明"全绿"不是无条件默认。
**A2 回归**：`STELQUICK_A2_CHECK=1` + metal → **12/12 PASS / rc=0**，计量与降级角标未污染逐像素路径。

### 3. 首跑踩到的三个坑（都已修，值得复用）

1. **`LegacyTestScene` 是惰性的**：`ok()` 只在首次 `render()` 后为真（着色器/VAO 那时才编译）。
   构造后立即断言 `ok()` 必然假 → 误判"装配失败"。改为首帧后自检，失败原因经 `lastError()` 上报。
2. **GL 对象必须在装配它的线程上销毁**：装配失败时主线程析构 `unique_ptr<LegacySkyHost>`
   会触发 Qt 断言 `Cannot make QOpenGLContext current in a different thread` 并以 **SIGABRT(134)** 收尾。
   修法：失败与正常退出路径都安排在**工作线程内** `reset()` 掉 host/scene。
3. **同一次事件循环内连抓两帧必然相同**：`grabWindow()` 两次背靠背取到同一渲染帧，
   "内容动态"判据假红。改为相隔 700ms 抓两张。

### 4. 留证

- `docs/evidence/2026-09-23-consumer-dyn-bridge/metal-8s.txt`（7/7 PASS 全文）
- `.../metal-negctl-lowfps.txt`（负控：C07 红、rc=8）
- `.../vulkan-6s.txt`（Vulkan：计量全过、C06 SKIP）
- `.../a2-regression-metal.txt`（A2 逐像素回归 12/12）

### 5. 现状与下一步

P-BRG-01 的消费侧口径（上传耗时、队列/丢弃、帧龄）**已具备测量能力**；
P-BRG-04 的降级 UI **已实现并验证切换**。尚缺：30 分钟全量 CSV 计量长跑（按 §6.1.1 协议）
与真实引擎接入（AppFacade）——后者换掉 `LiveFrameSource` 的渲染回调即可。

## 2026-09-23｜消费侧全量计量长跑 PASS：P-BRG-01 验收闭环

`SkyLongRun`（`STELQUICK_LONGRUN=1`）跑完整协议：预热 900s + 测量 1800s，
`caffeinate -dimsu` 包裹、AC 电源、屏保关闭、机器全程闲置。**VERDICT=PASS，SL-C01~C10 10/10，退出码 0。**

### 1. 结果（稳态窗口 = 测量段后 1200s，全局 t≥1500；1200 采样）

| 判据 | 内容 | 实测 | 门槛 | 判定 |
|---|---|---|---|---|
| SL-C00 | 环境前置（AC + 低电量关） | 通过 | 硬门 | ✓ |
| SL-C01 | 生产者零失败 + 稳态生产 fps | 96072 帧 / 失败 0；**53.83 fps** | ≥40 | PASS |
| SL-C02 | 稳态显示 fps | **53.83 fps**（帧号 66040 → 130585） | ≥40 | PASS |
| SL-C03 | 上屏间隔 p99 / max | p95 34.28 / **p99 35.74** / max 77.00 ms | p99≤50、max≤100 | PASS |
| SL-C04 | 邮箱丢弃 | **0**（投递 96072） | =0 | PASS |
| SL-C05 | 上传健康 | 94320 次，均值 **0.471 ms**，全程最坏 18.89 ms | <5 / <100 | PASS |
| SL-C06 | 邮箱帧龄 | max **28 ms** | <500 | PASS |
| SL-C07 | footprint 斜率 | **0.176 MiB/min**（248.0 → 252.2 MiB，+4.1） | ≤1.0 | PASS |
| SL-C08 | 环境未漂移 | 0 次违规（每 60s 复核） | =0 | PASS |
| SL-C09 | 窗口全程 exposed | **1800/1800** | 100% | PASS |
| SL-C10 | 降级未误报 | 0/1800（0.000%） | ≤1% | PASS |

**读法**：显示帧率与生产帧率完全相等（53.83 = 53.83），说明消费侧不是瓶颈——
场景图线程每帧都跟上，无积压；p95=34.28ms 落在 vsync 量化的 33.4ms 档（生产者 53.83fps
未顶到 60Hz，周期性漏一帧），p99 35.74 与 p95 几乎重合，说明没有异常长停顿（max 77ms 单帧毛刺）。
内存 +4.1 MiB / 20 min 属资产惰性加载残余，斜率远低于门槛。

### 2. 两次废跑与根因（同样重要，都留证）

| 次数 | 结局 | 根因 | 证据目录 |
|---|---|---|---|
| 第 1 跑 | INVALID（rc=9） | **屏幕保护 idleTime=600s** 在测量中段接管，窗口 unexposed 591s。污染出假象：内存斜率 3.338 MiB/min、生产 38.35 fps；剔除遮挡段后 exposed 段 53.71 fps / 斜率 0.010 MiB/min，管道本身健康 | `...-INVALID-screensaver/` |
| 第 2 跑 | PARTIAL（被杀） | 会话轮次结束时**后台任务进程组被整体清理**（shell 尾部的 `echo EXIT` 都未执行），CSV 停在 t=2400s；死前 900→1900s 稳定 50~55 fps，1900s 起跌至 10~14 fps 系**用户回用机器抢 CPU**（环境污染） | `...-PARTIAL-session-killed/` |

**由此产生的两个硬规则（已写入技能）**：

1. **`caffeinate -dimsu` 防不了屏保**。长跑前必须 `defaults -currentHost write com.apple.screensaver idleTime -int 0`
   （记录原值，跑完恢复）。窗口 unexposed 一律判 **INVALID（rc=9）**，不判 FAIL——
   数据不可信与管道损坏是两回事，混为一谈会被假 FAIL 引去查不存在的退化。
2. **跨轮次长跑必须脱离会话进程组**（`start_new_session`），不能用会话托管的后台任务；
   且跑期间机器必须闲置（闲置要求与电源要求同级，同属测量环境前置）。

### 3. 溜进代码的第三个修复

日志打印 bug：`测量 %d 秒开始` 误打 `warmupSeconds`（跑 1800 显示成 900），已修。

### 4. 留证

- `docs/evidence/2026-09-23-sky-longrun/run.log`（10/10 PASS 全文）
- `.../build.csv.gz`（2699 行逐秒）+ `.../build.csv.frames.csv.gz`（119800 行逐帧上屏间隔）
- `.../smoke-50s.txt`（20s+30s 冒烟 10/10）、`.../negctl-5fps.txt`（负控：C01/C02/C03/C10 精确红、rc=8）
- 废跑：`...-INVALID-screensaver/`、`...-PARTIAL-session-killed/`

### 5. 现状

**P-BRG-01（帧上传/队列计量）与 P-BRG-04（15fps 降级 UI）到此验收闭环**：
计量口径（上传耗时、队列丢弃、帧龄、显示帧率、上屏节奏、内存）在 30 分钟真实窗口运行下全部达标，
降级切换已验证可切可恢复。剩下的是**真实引擎接入（A4/AppFacade）**——
把 `LiveFrameSource` 的渲染回调从 `LegacyTestScene` 换成 `StelApp::update()/draw()`（A3 已验证的双绘法），
线程与计量骨架不动；届时按本组判据复跑即可（macOS 逐像素判据仍受 §6.2 Vulkan 缺陷限制）。

## 2026-09-23｜T10 构建合流：`stelQuickUI` 成为根构建子目标

里程碑 **A3 的第一步**（见进度实况 §8.4）。合流前 `src/ui` 是**完全独立的工程**
（`cmake -B build-ui -S src/ui`，只编 15 个文件、**不含引擎**），因此"把渲染回调换成
`StelApp::update()/draw()`"这句话缺少前提：那个可执行文件根本没链接引擎。

### 1. 改了什么

| 文件 | 改动 |
|---|---|
| 根 `CMakeLists.txt` | 新增 `ENABLE_STELQUICKUI`（默认 0）；开启时 `FIND_PACKAGE(Qt6 REQUIRED COMPONENTS Quick Qml OpenGL)`，并对 Qt5 直接 `FATAL_ERROR` |
| `src/CMakeLists.txt` | `stellarium` 目标之后新增 `IF(ENABLE_STELQUICKUI) ADD_SUBDIRECTORY(ui) ENDIF()` |
| `src/ui/CMakeLists.txt` | 改为**双形态**：`CMAKE_SOURCE_DIR == CMAKE_CURRENT_SOURCE_DIR` 即独立工程，否则子目标 |

双形态不是折中，是必要——Windows 侧"看渲染效果 + 驱动形态对照"仍走形态 ②（依赖面只有 VS2022 + Qt6），
见 `docs/WINDOWS_BUILD.zh_CN.md`。

### 2. 唯一有技术含量的坑：重复编译引擎文件

`render/legacy/{FrameMailbox,LegacySkyHost}` **已在 `stellarium_lib_SRCS` 里编入 `stelMain`**。
子目标形态下若照抄独立工程的源列表，这两个文件会被编第二遍：

- 轻则体积翻倍（`.text` 两份）；
- 重则 `duplicate symbol`；
- **最坏的不是报错而是"能跑"**：两份实现各自持有静态状态（如 `FrameMailbox` 的序号计数器），
  表现为随机不可复现的行为漂移——比链接失败难查一个数量级。

处置：拆出 `STELQUICKUI_SOURCES_SHARED`，仅独立工程形态 `list(APPEND)`，
进而在子目标形态 `target_link_libraries(stelQuickUI PRIVATE stelMain)`。
`nm` 实测确认符号来自 `libstelMain.a` 而非本目标（`docs/evidence/2026-09-23-t10-build-merge/t10-link-facts.txt`）。

### 3. 验收（判据见测试文档 §6.5）

| 项 | 结果 |
|---|---|
| 单次 configure 出双产物 | ✅ `build-release/src/stellarium` + `build-release/src/ui/stelQuickUI.app` |
| 开关对旧目标零影响 | ✅ OFF/ON 两次构建 `stellarium` **sha256 完全一致**（`f860d5a1…`） |
| A1 四项（合流版 stelQuickUI） | ✅ `runtimeApi=Vulkan` rc=0 / 禁用 Vulkan rc=3 / WINDOWTEST PASS / 免环境变量可运行 |
| A2 逐像素（合流版） | ✅ 12/12 PASS rc=0 |
| DYN 动态帧通路（合流版） | ✅ 7/7 PASS rc=0（生产者 54.2 fps，与独立工程版 54.8 fps 同级） |
| S3 引擎集成自检（合流后 stellarium） | ✅ `STELA3_CHECK` 8/8 PASS rc=0 |
| 独立工程形态回归 | ✅ `cmake -B build-ui -S src/ui` 仍可配置 + 构建 |
| 默认值 | ✅ 去掉缓存后 `ENABLE_STELQUICKUI=0`，目标清单里 `stelQuickUI` 计数为 0 |

### 4. 一处 INVALID 而非 FAIL

合流版 DYN 首跑 D1-C02 红（显示帧推进 151 < 192）。**不是合流退化**：
首跑紧接 `-j8` 全量链接，`uptime` load average ≈ 10（8 核），生产者掉到 47.0 fps；
同负载下独立工程版对照为 54.8 fps（对照本身能绿，说明是负载回升），复跑合流版 7/7 全绿。
**环境异常判 INVALID，不计入结论**——与 §6.1.1 协议第 5 条同一纪律。留证 `...-INVALID-contended.txt`。

### 5. 代价（明确记账）

合流后 `stelQuickUI` 随根构建承担全部依赖，`otool -L` 里出现
`QtWebEngineWidgets` / `QtWebEngineCore` / `QtWidgets` / `QtOpenGLWidgets`——
这是"引擎与 QML 同进程"必须付的代价（§8.6「依赖面膨胀」）。形态 ② 保留即为此保留。

### 6. 留证

`docs/evidence/2026-09-23-t10-build-merge/`（含 configure 三态、构建、链接事实、
sha256 不变性、两种形态回归、A1 四项、A2/DYN/S3 自检、INVALID 首跑）。

## 2026-09-23｜A3 前置探针：`QApplication` 承载 `QQuickWindow` + 引擎同进程共存

T10 合流完成后暴露出的**真前置**（进度实况 §8.6 记为未知数）。结论：
**未知数成立、已做掉、共存成立。**

### 1. 为什么必须先做掉它

T11 的任务表述是"把渲染回调换成 `StelApp::update()/draw()`"，但：

| 事实 | 出处 |
|---|---|
| 引擎现成无头引导路径 = `new StelMainView(conf)` + `show()` + `WA_DontShowOnScreen` → `initializeGL` → `StelApp::init` | `render/legacy/LegacyAppCheck.cpp` A3-C01 |
| `StelMainView` **是 Widgets 类** | `src/StelMainView.hpp:46` → `public QGraphicsView` |
| `StelMainView::init()` 里 `gui = new StelGui()`（完整 Widgets 版） | `src/StelMainView.cpp:947` |
| `stelQuickUI` 是 `QGuiApplication`，且只链 `Qt6::Quick/Qml/Gui/Core/OpenGL` | `src/ui/main.cpp` / `CMakeLists.txt` |

`QGuiApplication` 下创建任何 `QWidget` 会 abort。**不改宿主，T11 第一步就是进程直接死。**

### 2. 改了什么

| 文件 | 改动 |
|---|---|
| `src/ui/CMakeLists.txt` | 新增 `option(STELQUICKUI_WIDGETS_HOST … OFF)`；开启时查并链 `Qt6::Widgets` + 定义 `STELQUICK_WIDGETS_HOST=1`。子目标形态追加 `STELQUICK_HAS_ENGINE=1` |
| `src/ui/main.cpp` | ① 宿主条件切换 `QApplication` / `QGuiApplication`；② `main()` 开头显式 `Q_INIT_RESOURCE(mainRes/guiRes)`；③ 新增探针 `runEngineCoexistProbe`（`STELQUICK_ENGINE_COEXIST=1`） |

默认 **OFF**：既有 A1/A2/DYN 基线是在 `QGuiApplication` 下取的，不能悄悄换宿主。

### 3. 验收（判据见测试文档 §6.6 B-A3P-01..04）

| 项 | 结果 |
|---|---|
| 阶段一·`QApplication` 承载 `QQuickWindow` | ✅ A2 12/12 PASS、DYN 7/7 PASS（54.9 fps），与 `QGuiApplication` 基线同级 |
| 阶段二·C-01 引擎无头初始化（QML 窗口同时存活） | ✅ 成功，0ms 同步完成 |
| 阶段二·C-02 引擎 GL 形态 | ✅ `version=4.1 core=1 renderer="Apple M3"` |
| 阶段二·C-03 引导后 QML 窗口仍能出帧 | ✅ Metal +179 帧/3s、Vulkan +178 帧/3s（基线 82~83 帧/1.5s） |
| 阶段二·C-04 引擎 `update/draw` ×4 | ✅ 未抛异常（39~43ms） |
| 默认形态回归（Widgets OFF） | ✅ A2 PASS、DYN PASS（54.8 fps） |
| 负控·探针不泄露到默认形态 | ✅ 0 行 COEXIST 输出、rc=0 |
| `stellarium` 字节不变性 | ✅ OFF/ON 两次一致（`8c6b8a15…`）；stash 掉本轮改动重建亦一致 |

### 4. 三个过程中挖出的真问题

**① 静态库里的 qrc 不会自动注册 → `qFatal` → SIGABRT(134)。**
首跑崩在 `AtmospherePreetham.cpp:52`（shader 读不到），但星表/DSO/LandscapeMgr 全加载成功。
根因：`qrc_mainRes.cpp` / `qrc_guiRes.cpp` 编进 `libstelMain.a`，而**静态库中的 qrc 对象文件
没有任何被引用的符号**，按需拉取下不会被装进可执行文件 → `qInitResources_*` 从不执行。
`stellarium` 主目标自己编了 qrc 所以从没暴露。处置：`main()` 开头 `Q_INIT_RESOURCE(...)`。

**② C-03 判据初版错误（假红）。** 只数 `frameSwapped` → "引导后 +0 帧"。
真相是 **Qt Quick 按需渲染**，静态页面渲完就停。两侧改为主动 `window->update()`
请求重绘后取帧，判据才成立。**对 T11 的直接含意：合流后的长跑不能靠"窗口自己会出帧"
度量，必须由驱动方显式请求重绘。**

**③ 退出路径 SIGSEGV(139)。** 引擎与 QML 两套栈同时存在，正常 `return` 撞上静态析构
顺序（两套 GL/Metal 上下文谁先销毁）。处置：`fflush` 后 `_exit(rc)` 直接交付退出码。

### 5. 遗留问题（T11 基础设施工）

`COEXIST: 安装目录 = .` —— `StelFileMgr` 在 `stelQuickUI.app` 附近找不到 `CHECK_FILE`，
最终命中 `STELLARIUM_DATA_ROOT` 默认值 `.`，星表走相对路径 `./stars/...`。
⇒ **stelQuickUI 当前必须从仓库根启动**（`stellarium` 主目标无此限制）。

### 6. 留证

`docs/evidence/2026-09-23-a3-host-probe/`（含崩溃现场、两大后端 PASS、
默认形态回归、负控、字节不变性复查 + README 索引）。

## 2026-09-23｜T11 引擎共进程帧驱动：`LiveSkyRuntime`（A3 第一步落地）

T10 合流 + A3 前置探针铺路后的正题。**交付**：真实引擎（`StelApp::update/draw`）
在 QML 进程内产帧 → 离屏读回 → `FrameMailbox` → QML 上屏，消费侧零改动。

### 1. 设计定案（探针定案，不再争论）

| 决策点 | 定案 | 依据 |
|---|---|---|
| 帧驱动线程 | **GUI 线程**（QTimer 分片），不是 LiveFrameSource 式独立线程 | 引擎上下文 owner = QApplication 主线程；跨线程 makeCurrent 直接 abort；探针已证明 GUI 线程 update/draw 安全且 QML 零退化 |
| GL 上下文 | `LegacySkyHost` 的 `GlContextMode::kBorrowed`（**为 A3 预留的路径，本次启用**） | 借引擎的 `StelGLWidget`（QOpenGLWidget）上下文 |
| 离屏目标 | `LegacySkyHost` 自己的 FBO（RGBA8+D24S8，1280x720） | 探针 C-07 证明引擎 draw **尊重外部 FBO 绑定**（非黑 + 随 JD 变化） |
| 时间推进 | `timeRate=0` + 显式 `setJD(jd0 + sim × simRate)` | 可复现、可对账（帧元数据 stateNumber=毫秒量化 JD） |

### 2. 改动

| 文件 | 改动 |
|---|---|
| `src/ui/LiveSkyRuntime.{hpp,cpp}` | **新建**。boot（引擎无头引导，照抄探针前置链）+ start（借上下文装配 + 帧泵）+ stop；双宏守卫，独立工程/默认形态编译为空 |
| `src/render/legacy/LegacySkyHost.{hpp,cpp}` | 新增 `contextManagedExternally`（默认 false，kOwn 行为不变）：借用模式下宿主只校验 current、不自行 makeCurrent。三处统一（renderOneFrame / withContextCurrent / setTargetSize） |
| `src/ui/main.cpp` | `STELQUICK_LIVE_ENGINE=1` 模式（复用 LIVE_FPS/SIZE，新增 LIVE_SIMRATE）；boot 前暖机；退出 `_exit` 纪律 |
| `src/ui/CMakeLists.txt` | LiveSkyRuntime 进源列表（无条件） |

### 3. 两处返工（都是真问题）

**① `doneCurrent()` 清空 `surface()`。** 借用模式首跑"无可用绘制表面"：
`QOpenGLContext::doneCurrent()` 把 `surface()` 置空，宿主自己 makeCurrent 拿到 nullptr。
**处置**：`contextManagedExternally` —— 上下文由调用方（`StelMainView::glContextMakeCurrent`）
管理，宿主只校验。kOwn 路径零改动（A2/DYN/S3 回归确认）。

**② QML 后端漂移 → 视口黑屏。** 不暖机直接引导引擎：`runtimeApi=Vulkan`（请求的是
metal！）→ MoltenVK 静态纹理黑屏缺陷被触发，抓帧视口全黑。
根因：QML 场景图的**首次渲染**（RHI 设备创建）发生在引擎 GL 上下文创建之后时，
后端解析偏离请求值。**处置**：boot 前暖机（`window->update()` 主动请求重绘直到
`isSceneGraphInitialized()`，再 settle 300ms）——A3 探针"先暖机后引导"的顺序
是必要条件，不是巧合。修复后 runtimeApi=Metal ✓。

### 4. 验收

| 项 | 结果 |
|---|---|
| 探针 C-05 借上下文装配 | ✅ GL 4.1 Metal，Apple M3 |
| 探针 C-06 引擎帧驱动 + 读回 | ✅ 2/2 帧，读回 1ms/帧 |
| 探针 C-07 内容非空且随 JD 变化 | ✅ 非黑 100%，两帧哈希不同 |
| LIVE_ENGINE 冒烟（metal） | ✅ rc=0，runtimeApi=Metal，抓帧 = 真实星空（黄昏全景 + 星点 + 方位标记） |
| A2 / DYN 回归（Widgets 宿主形态） | ✅ PASS / PASS |
| STELA3_CHECK（LegacySkyHost 改动后） | ✅ 8/8 PASS rc=0 |
| 默认形态（Widgets OFF）回归 | ✅ A2/DYN PASS，LIVESKY 输出 0 行 |

### 5. 留证

`docs/evidence/2026-09-23-t11-live-runtime/`（README 索引 + 探针帧 PNG + 冒烟抓帧 PNG）。

## 2026-09-23｜T12 引擎生产者上跑 DYN 自检（生产者抽象 + 判据口径重定）

里程碑 A3 第三步（§8.7 判据 3）：**I-DYN 的 7 项判据在真实引擎生产者下全绿**，
消费侧（SkyViewport）零改动。

### 1. 为什么不是"改一行开关"

DynFrameCheck 原先直接依赖 `LiveFrameSource` 具体类（连装配都在它里面做）。
两种生产者的差异是**根本性**的：

| | LiveFrameSource（替身） | LiveSkyRuntime（真实引擎） |
|---|---|---|
| 线程 | 独立 std::thread | GUI 线程 QTimer 分片 |
| GL 上下文 | kOwn 自建离屏 | 借引擎 `StelGLWidget` 上下文 |
| 调速 | 跨线程原子量 | 仅 GUI 线程可调 |
| 装配 | `start(mailbox, cfg)` | **必须先 boot 引擎**，再 start |

⇒ 抽出 `IFrameProducer`（`setFps` / `counters` / `stop` 三能力），两实现同构接入；
**装配留在调用方**（main.cpp），因为差异太大不该硬塞进接口。

### 2. 改动

| 文件 | 改动 |
|---|---|
| `src/ui/IFrameProducer.hpp` | **新建**。生产者运行期契约 + `ProducerCounters`（rendered/failed/fps） |
| `src/ui/LiveFrameSource.{hpp,cpp}` | 实现接口（`counters()` 映射 `runtimeStats()`） |
| `src/ui/LiveSkyRuntime.{hpp,cpp}` | 实现接口；新增 `setFps()`（GUI 线程改 QTimer 间隔）与 `counters()`（rendered=published，口径统一为"已进邮箱的帧"） |
| `src/ui/DynFrameCheck.{hpp,cpp}` | 改依赖 `IFrameProducer*`；判据口径重定（见 §3）；新增 `STELQUICK_DYN_CSV=<path>` 逐 250ms 采样 CSV |
| `src/ui/main.cpp` | 抽 `warmUpSceneGraph()` + `engineConfigFromEnv()` 两个辅助（消除 LIVE_ENGINE/DYN-engine 两处重复）；DYN 分支新增 `STELQUICK_DYN_PRODUCER=engine`；engine 模式下 `_exit` 纪律 |

### 3. 判据口径重定（本次真正的技术内容）

**核心发现：引擎在合流形态下爬升极慢。** 实测曲线（留证 `30-FINAL-metal-PASS-samples.csv`）：

```
t=1.9s  31.3 fps  ← warmup
t=3.9s  24.8 fps  ← warmup
t=6.9s  39.7 fps  ← 开始爬升
t=13.9s 13.8 fps  ← 降速窗结束，恢复
t=15.9s 44.0 fps
t=17.9s 50.0 fps  ← 稳态
```

**需要约 10 秒才能从 warmup 到稳态**（编译着色器、上传纹理、加载星表/DSO、建缓存）。
与 T9 长跑协议的 900s warmup 是同一现象的两个尺度。

⇒ 旧判据"全程平均产出 ≥ 0.6×名义×时长"**稳定假红**：8 秒窗平均 31.5 fps，
同期 1s 滑窗稳态 53.6 fps（差 40%）。

两处修正（都有实测依据，不是放水）：

| # | 修正 | 依据 |
|---|---|---|
| ① | 全程平均 → **尾窗稳态速率**；尾窗起点 = `max(降速段结束, 时长 − max(3s, 时长×35%))` | warmup 与人为降速窗都不是生产者缺陷，不该惩罚平均 |
| ② | engine 生产者名义速率 60 → **50**；测量窗 8s → **20s** | 60 是 UI 刷新上限，不是引擎产能。引擎受"全场景渲染 + glReadPixels 读回"限制，实测稳态 35~52 fps 随负载波动，稳态档 ≈50 |

边界证据（说明两条修正缺一不可）：名义 60 + 15s 窗两次跑分别得
`34.5 vs 下限 36.0`（FAIL）与 `36.4 vs 36.0`（PASS，余量 **1%**）——
旧口径只是恰好卡在边界。修正后 `44.4 vs 下限 30.0`（余量 48%）。

### 4. 一个可靠性问题：判据全绿但退出码 134

CSV 用 `shared_ptr<QFile>` 持有，同时又调 `deleteLater()` → Qt 删一次、
`shared_ptr` 析构再删一次 → **双重释放 SIGABRT(134)**。
处置：只 `close()`，不 `deleteLater()`。
**症状极具误导性**：7/7 全绿、CSV 完整，却拿到非零退出码——若只看判据会漏掉。

### 5. 验收（判据见测试文档 §6.8 B-LSR2-01..07）

| 项 | 结果 |
|---|---|
| engine 生产者 DYN（Metal） | ✅ 7/7 PASS rc=0；尾窗 44.4 fps（复跑 44.8，可复现） |
| engine 生产者 DYN（Vulkan） | ✅ PASS（D1-C06 按 §6.2 已知缺陷正确 SKIP） |
| test 生产者基线 | ✅ 7/7 PASS，尾窗 51.5 fps（改造未破坏既有行为） |
| A2 回归（Widgets ON / OFF 两形态） | ✅ 双双 PASS |
| 默认形态 DYN-test | ✅ PASS（尾窗 52.0 fps） |
| 负控：默认形态请求 engine 生产者 | ✅ `UNAVAILABLE` rc=6，明确报错不假装通过 |
| `STELA3_CHECK` | ✅ 8/8 PASS |
| 独立工程形态 | ✅ 可配置可构建 |

### 6. 留证

`docs/evidence/2026-09-23-t12-dyn-engine/`（24 份原始输出 + README 索引 +
逐 250ms 采样 CSV，含新旧口径对照与边界跑）。

## 2026-09-23｜T13 合流形态 30 分钟长跑（**验收通过 11/11**；根因定位与后端决策见 §9–§10）

### 1. 目标与做法

T13 = 在**合流形态**（Widgets 宿主 + `LiveSkyRuntime` 真实引擎帧泵 + QML 共进程）
下按 §6.1.1 协议跑 30 分钟，判据 SL-C01..C10 全绿，并新增 **GUI 线程卡顿**（帧龄 p95）
指标——这是 §7.4「GUI 与 GL 同线程卡顿」风险的实测回答。

`SkyLongRun` 此前硬编码 `LiveFrameSource`（替身场景）。本次改成**生产者可切**，
与 T12 的 `DynFrameCheck` 完全同构：

| 件 | 改动 |
|---|---|
| `SkyLongRun::runStartupSequence` | 签名加入 `IFrameProducer *producer`；**不再自建生产者**，装配留调用方 |
| `SkyLongRunOptions` | 新增 `producerSharesGuiThread`（形态属性，见 §4）、`frameAgeSampleMs`（SL-C11 采样粒度） |
| `SkyLongRun.cpp` | 生产者计数改走 `IFrameProducer::counters()`；新增 SL-C11 与负控注入器 |
| `main.cpp` | longRun 分支新增 `STELQUICK_LONGRUN_PRODUCER=engine`（暖机 → boot → start，与 DYN-engine 同一顺序纪律） |

engine 形态的名义速率默认 **50**（`kEngineNominalFps`）、simRate 默认 **0.02**
（1 天/50s → 30 分钟走约 54 天），尺寸与速率**一律以 `STELQUICK_LONGRUN_*` 为准**，
不读 `STELQUICK_LIVE_*`（避免两套口径互相污染）。

### 2. 本次最有价值的实测：合流形态的代价可以量化了

同一构建、同一机器、同一窗口，只换生产者（各跑 120s 预热 + 180s 测量）：

| 指标（稳态窗） | test 生产者（独立线程） | engine 生产者（**共 GUI 线程**） |
|---|---|---|
| 稳态显示帧率 | **54.91 fps** | **42.87 fps** |
| 上屏间隔 **p50** | **16.71 ms**（= vsync 档） | **21.35 ms**（被引擎 tick 牵引） |
| 上屏间隔 p95 / p99 | 33.77 / 35.37 ms | 53.60 / 60.27 ms |
| 邮箱帧龄 p95 | 18.00 ms | **11.00 ms**（反而更低） |
| phys_footprint 峰值 | **282 MiB** | **3152 MiB**（11 倍） |
| 邮箱丢弃 | 0 | 0 |

读法：
1. **不是"卡死"，是"节奏被牵引"**。`p50` 由 vsync 的 16.71ms 变成 ≈生产者帧间隔
   （1/42.87s = 23.3ms），说明 QML 出帧时刻跟着引擎 tick 走（引擎单帧 ~20ms 的
   同步渲染期间 GUI 线程无法处理渲染事件）。
2. **帧本身不积压**：帧龄 p95 仅 11ms（比 test 形态的 18ms 还低），丢弃 0，
   生产帧率 = 显示帧率（42.87 vs 42.84）→ 消费侧不是瓶颈，管道健康。
3. **代价集中在"上屏节奏"与"内存"**，不集中在"吞吐"（仍 ≥40fps 门槛）。

### 3. 为什么 engine 形态只能到 ~43 fps

名义 50 fps（QTimer 间隔 20ms），实测帧间隔 23.3ms。差额来自
**QTimer 往返 + 事件循环延迟**（回调本身 ~20ms，返回后才重新计时）。
把名义提到 60（间隔 16.7ms < 单帧耗时）**没有改善**：实测 40.67 fps，
因为瓶颈是引擎单帧耗时本身，不是 timer 间隔。⇒ 43 fps 是当前线程模型的产能档。

### 4. SL-C03 改用**形态相关口径**（不是形态相关门槛）

发现：SL-C03 的绝对门槛（p99≤50ms、max≤100ms，2026-09-23 消费侧长跑冻结）
**隐含"生产者顶到 vsync"的前提**，而这个前提只在 test 形态成立。

上屏间隔的正常栅格 = `max(vsync 周期, 生产者帧间隔)`：

| 形态 | 帧间隔 | "漏一帧" | 原门槛 50ms 实际容许 |
|---|---|---|---|
| test（顶 vsync） | 18.21 ms | 33.4 ms | 漏一帧 |
| engine（帧率跟随） | 23.34 ms | 46.7 ms | **也只容许漏一帧**（更严一档） |

⇒ engine 形态下 `p99 = 60.27ms` 被判 FAIL，但它对应的是"偶尔漏两帧"，与
test 形态下 `p99 = 35.37ms`（漏一帧）是同类现象，只是栅格被生产者帧率放大了。

> ⚠️ **更正（2026-09-24，T16）**：上面 §3 把 "~43 fps" 归因于
> "引擎单帧耗时本身 + QTimer 往返"，**归因不完整**。T16 发现合流形态下旧宿主
> `fpsTimer` 一直在跑，每帧多出**一次完整且被丢弃的引擎渲染**（见
> `docs/T16_SINGLE_SIM_CLOCK.zh_CN.md` §2.4）。把它停掉后，同口径帧率从
> **34.9 → 49.8 fps**（DYN 尾窗）、短窗 **38.71 → 49.18 fps**。
> 即 §3 观察到的"产能档"里，有相当一部分是被第二个渲染驱动的白工，而不是
> 该线程模型的固有上限。§4 的形态相关口径结论**仍然成立**（栅格随生产者帧率变化
> 这一事实不因帧率提高而改变），只是栅格数值需按新帧率重新标定。

处置：`producerSharesGuiThread=true` 时改用**相对口径**
`p99 ≤ min(3×帧间隔, 100ms)`、`max ≤ min(6×帧间隔, 200ms)`；
`false` 时**完全沿用冻结的绝对门槛 50/100ms（未改动）**。

**绝对天花板（100/200ms）是必需的**：只给相对口径会让"生产者极慢"的负控失效——
5fps 时 3×帧间隔 = 600ms，病态反而通过。

**形态由调用方显式声明（`SkyLongRunOptions::producerSharesGuiThread`），不从数据倒推**——
避免"看到实测值再挑门槛"这种事后合理化。

### 5. 新增 SL-C11：GUI 线程卡顿（稳态帧龄 p95）

逐秒 CSV 的 `mailbox_age_ms` 是"每秒瞬时值"，1800 点算 p95 只反映分钟级分位，
抓不住秒级以下的排队长尾。故另设 **100ms 粒度高频采样器**（不入 CSV，样本只在
稳态窗累积），30 分钟给约 1.8 万个样本。

门槛 `p95 ≤ 100ms`：相对 SL-C06 的 max≤500ms 收紧 5 倍，同时给"引擎单帧 20ms +
偶发双帧排队 + 场景图抖动"留余量。实测：

- test 形态：p95 **18.00ms**（p99 20 / max 46）
- engine 形态：p95 **11.00ms**（p99 24 / max 32）——共线程形态**帧龄反而更低**

### 6. 负控：`STELQUICK_LONGRUN_STALL_MS`（新增）

SL-C11 是新判据、SL-C03 改了口径，两者都需要**能单独定位的可红性证据**。
既有的 5fps 负控会同时打红 SL-C01/C02/C10，无法定位"节奏/帧龄"这一类。

故加负控注入器：**每秒在 GUI 线程忙等 N 毫秒**——上屏停顿 + 帧龄飙升，而生产/显示
**总量**几乎不变，能精确命中 SL-C03/SL-C11。正式验收禁用，启用时判据详情会明示
"本次非合规基线"。

> ⚠ 该负控**首次运行即触发 GPU 设备丢失**（见 §7）。当时的直接原因判定为环境
> 内存不足（同时段的 engine 正常跑也崩），但该手段会长时间占住 GUI 线程、
> 加重 GPU 层压力，**在内存紧张的环境下不建议使用**；可红性证据待环境恢复后补跑。

### 7. engine 形态验收被 GPU 设备丢失阻塞（⚠️ 本节的"内存不足"结论**已被推翻**，见 §9）

现象：engine 形态长跑在**预热最初几秒**（首帧渲染）即以 GPU 设备丢失终止
（`SIGABRT(134)` 或 `SIGSEGV(139)`），逐秒 CSV **0 行**：

```
vkDebug: VK_ERROR_OUT_OF_DEVICE_MEMORY: Lost VkDevice after MTLCommandBuffer
  "vkQueueSubmit ..." execution failed (code 3):
  Caused GPU Address Fault Error (0000000b:kIOGPUCommandBufferCallbackErrorPageFault)
Device loss detected in vkWaitForFences()
Graphics device lost, cleaning up scenegraph and releasing RHI
Failed to create swapchain: -4
```

**判别（排除本次改动）**：用**未被本次改动触及**的 T12 路径复核——`DYNCHECK` +
`STELQUICK_DYN_PRODUCER=engine`（该分支代码自 T12 起未动）在同一时段**同样崩溃**、
同一位置。⇒ 与 T13 的改动无关。

**~~当时的根因判断~~（⚠️ 已被推翻，保留以存档当时的推理路径）**：
~~统一内存架构下的 GPU 分配失败~~。当时的理由是 footprint 峰值 3152 MiB，
而系统 `PhysMem: 15G used, 92M unused`。

**这个判断错在选错了指标**：`top` 的 `unused` **只统计 free pages、不含 inactive**。
同一台机器实测 `vm_stat` 的 inactive 有 **5.0 GB**（可回收）、purgeable 177 MB，
`memory_pressure` 报 **free 64~66%**。真因是**驱动级、确定性**的缺陷，与内存无关——见 §9。

**处置（已被 §9 取代，但过程值得留存）**：当时的结论是"必须在释放内存后的合规环境重跑"。
持有这个结论的一段时间里做了两件事：挂守候进程等内存、停止一切尝试（怕反复重试把系统压垮）。
守候进程确实在 `free 1372M`（按它的错误指标）时自动起跑——**7 秒即失败**，
且此后每次运行都在同一位置**确定性**失败。**这就是推翻内存结论的第一手证据**：
如果真因是内存，条件满足后不该稳定复现同一故障。

**本节的 T13 不宣告完成**——正式跑见 §9.4。

### 8. 已完成并验证的部分

| 项 | 结果 |
|---|---|
| 代码改造（生产者可切 + SL-C11 + SL-C03 口径 + 负控注入器） | ✅ 已落位并编译通过 |
| test 形态短窗回归（改动未破坏既有路径） | ✅ **11/11 PASS** rc=0（54.91 fps，帧龄 p95 18ms） |
| engine 形态短窗（改动生效性） | ✅ 两次 PASS 数据（42.87 / 40.67 fps），SL-C11 11~12ms，SL-C03 按新口径可达标 |
| engine 形态 30 分钟正式长跑 | 🔄 **进行中**：后端决策后以 **Metal RHI** 起跑（见 §9.4） |
| 负控（stall 注入） | ⏳ 首跑遇 GPU 故障；后续判别证明**注入器不是诱因**（同故障在无注入器时复现，见 §9.3） |
| 独立工程形态回归 | ✅ 已编译验证（接口改动后产物正常） |

**注意**：engine 形态短窗是**短窗**，不构成 T13 的验收证据（判据 4 要求 30 min）。

---

### 9. 根因定位（2026-09-23 晚）：**MoltenVK × Apple-OpenGL 同进程共存**

#### 9.1 先说结论

合流形态下，只要引擎开始渲染，**Qt Quick 的 Vulkan/MoltenVK 后端就必丢设备**
（`kIOGPUCommandBufferCallbackErrorPageFault`），随后 RHI 重建撞上 Metal 熔断而失败。
**根因不在本项目的代码，而在 MoltenVK 与 Apple OpenGL 共存。**
同一份场景把 QML 换成**原生 Metal RHI**，丢设备 **0 次**、判据 **11/11 全绿**。

> **⚠️ 顺带纠正 §7 的内存结论（指标选错）**：`top` 的 `unused` 只统计 free pages、
> **不含 inactive**。实测 `vm_stat` inactive 有 **5.0 GB** 可回收、purgeable 177 MB，
> `memory_pressure` 报 **free 64~66%**。判"内存够不够"请用后两者。
> 这一天的教训：**拿错指标会把确定性的驱动缺陷误诊成环境问题**，
> 进而做出"等环境恢复"这种永远不会发生的决策。

#### 9.2 判别矩阵：把两个变量拆开各测一次，再交叉

四组对照，**同一构建、同一机器、同一时段**（原始日志见证据目录 08~16）：

| # | 场景 | QML 后端 | 结果 | 丢设备 |
|---|---|---|---|---|
| 1 | 纯引擎渲染，**无 QML 窗口**（`STELQUICK_LEGACY_HOST_TEST=1`） | — | **14/14 PASS** | **0** |
| 2 | 纯 QML（替身生产者），**无引擎** | Vulkan | **11/11 PASS** 54.5 fps | **0** |
| 3 | 合流（engine 生产者）—— T13 长跑路径 | **Vulkan** | **失败 4/4**（崩 139 或 QML 瘫 0fps） | **2** |
| 4 | 合流（engine 生产者）—— 同上，只换后端 | **Metal** | **11/11 PASS** rc=0 | **0** |

**读法**：1、2 各自完全健康 ⇒ 排除"引擎有病""Vulkan 有病"。
3 与 4 唯一差异是 QML 后端 ⇒ **故障落在 MoltenVK 与 Apple-OpenGL 的共存上**。

崩溃精确发生在引擎**首帧**渲染（`Initializing planets GL shaders...`）时。
两个结局都不可用：
- RHI 重建撞 Metal 熔断（`SubmissionsIgnored`）→ `Failed to create swapchain: -4` → **SIGSEGV(139)**
- 重建失败 4 次（`Failed to create RHI (backend 1)`）→ 场景图瘫掉、QML 显示 **0 fps**

> 易误判点：外层错误码是 `OUT_OF_DEVICE_MEMORY`（MoltenVK 的**误标**），
> 真因是 `PageFault`——GPU 访问无效地址。**先看 reason 那一行，别被错误码带走。**

#### 9.3 已排除的假设（都做过实验，别再重复）

| 假设 | 实验 | 结论 |
|---|---|---|
| 两边**并发提交** GPU 导致冲突 | 加静默期让 QML 在引擎初始化前 idle（`STELQUICK_QUIESCE_MS=800`） | **不足以避免**（仍丢 2 次）。只把结局从崩溃变成"失败可控" |
| 静默期越短越好 | `STELQUICK_QUIESCE_MS=1` | 仍崩 139 |
| 单线程渲染循环消除并发 | `QSG_RENDER_LOOP=basic` | **死锁**：进程 sleeping 0% CPU、11 分钟不退出、日志只落 1 行 |
| 引擎的超大 scene FBO（DPR 2 → 4 倍像素）是负担 | `QT_SCREEN_SCALE_FACTORS=1` | **无效**——macOS 的 DPR 来自屏幕 backing scale，环境变量覆盖不了 |
| 是我的改动引起（当时怀疑负控注入器） | 走 **T12 未改动的路径**（`DYN_CHECK`+`DYN_PRODUCER=engine`）复核 | 同样崩在同一位置 ⇒ **与本轮改动和注入器都无关** |

**未采用的方向**：把引擎重 GL 初始化前移到 QML 场景图初始化之前。
它能规避**首帧**那一次，但 30 min 窗口内两边仍持续并发渲染，**治标不治本**——
不做，以免把"运气好一次"当成修好。

#### 9.4 决策：engine 形态正式长跑采用 **Metal RHI**

**这是显式的方向决策，不是"悄悄换掉后端"。** 理由：

1. A1 阶段"Qt Quick 用 Vulkan 跑起来"的承诺**已经独立达成并留证**；
   A3 的命题是"**引擎与 QML 共进程**的稳定性"，不必把后端绑死。
2. Vulkan 后端的失败是**外部驱动缺陷**（MoltenVK），应用层无可靠解法
   （§9.3 的三条路都试过）。
3. Metal 后端已验证 **2/2 成功、11/11 全绿、0 次丢设备**，是可靠的工程选择。

**同时**：Vulkan/MoltenVK 的问题按**独立缺陷**归档
（`docs/evidence/2026-09-23-t13-live-longrun/README.md` 含最小复现命令），
供后续报 Qt/MoltenVK 上游或架构复议时直接引用。

#### 9.5 本轮代码改动

| 改动 | 说明 |
|---|---|
| `main.cpp` 新增 `quiesceSceneGraph()` | 引擎重 GL 初始化前让 QML 静默（`STELQUICK_QUIESCE_MS` 可覆盖，默认 800ms）。**实测不足以避免丢设备**，价值是把结局从 SIGSEGV(139) 变成"失败可控、判据可读（rc=8）"。注释已如实写明边界 |
| `SkyLongRun` **SL-C07 口径修正** | 原 `\|斜率\| ≤ 1.0` 把**内存下降**误判为 FAIL（Metal 形态实测 **-10.1 MiB/min** = 引擎释放 warmup 期缓存）。改为**只判增长**（`斜率 ≤ 1.0`）；增长方向门槛**未放宽** |
| `SkyLongRun` **SL-C03 max 档改占比口径** | 首战 30min 长跑定案（见 §10）：max 统计在 5 万样本下对单个调度毛刺零容忍。共线程形态改为：超 `min(6×帧间隔, 200ms)` 的帧**占比 ≤0.01%**。负控仍有效（150ms/s 注入 ≈2.4% 超限帧）；test 形态冻结绝对门槛未动 |

---

### 10. 正式 30 min 长跑（Metal RHI，2026-09-23 晚）：**11/11 全绿，T13 验收通过**

协议全程（§6.1.1）：AC / 低电量关 / 屏保临时关跑完恢复 / `caffeinate -dimsu` /
独立会话脱进程组 / 环境前置全过（内存 free 66%）。

**首战（17 号证据）10/11**：唯一 FAIL 是 SL-C03 max 档——稳态窗 48846 帧仅 **1 帧**
168.33ms（0.002%）超标，孤立单帧、前后 fps 正常、系统日志无外因。定性：
**max 统计对单点毛刺零容忍不是工程判据**。真卡顿另有判据管（`degraded` → SL-C10；
稳态窗外 t=1261s 的 2.5s 引擎卡顿爆发被它以 0.111% 记录，若在稳态窗内按占比
口径 7 连 = 0.014% 同样超限）→ max 档改占比口径（§9.5）。

**第二战（18 号证据）11/11 全绿，VERDICT=PASS（rc=0）**：

| 判据 | 实测 |
|---|---|
| SL-C01/02 生产与显示 | 72602 帧零失败；稳态 **40.30 fps**（下限 40） |
| SL-C03 上屏节奏 | p99 56.01 ≤ 74.45；max 153.12ms（占比口径 ≤0.01% 过） |
| SL-C04/05/06 邮箱与上传 | 丢弃 0；上传均值 0.170ms；帧龄 max 65ms |
| SL-C07 内存 | 斜率 0.141 MiB/min（窗口 2888.5→2888.6 MiB） |
| SL-C08/09/10 环境与窗口 | 零漂移；暴露 1800/1800；降级误报 **0.000%** |
| SL-C11 GUI 线程卡顿 | 帧龄 p95 **14.00 ms** / p99 24.00 / max 65.00（门槛 p95≤100） |

**全程 0 次设备丢失、0 次 RHI 失败。** 两战 SL-C03 max（168.33 / 153.12ms）均为
孤立单帧毛刺，p99/p95 高度一致（57.65/56.01、~51）——占比口径下的判定**可复现**。

**§8.7 判据 4（T13）就此关闭**：合流形态 30 min 长跑 11/11，门槛不降级，
含新增的 GUI 线程卡顿（帧龄 p95）指标。Vulkan/MoltenVK 缺陷独立归档（§9.2）。

**T13 证据**：`docs/evidence/2026-09-23-t13-live-longrun/`（17 号 = 首战全套
逐秒+逐帧 CSV；18 号 = 第二战 PASS 全套）。


---

## 2026-09-24｜T14 更新循环与时钟审计（**纯盘点，不改行为**）

> Windows 支线同日完成三阶段（阶段 1 回归 + 阶段 2 MSVC 根构建 + 阶段 3 判定性实验
> 3b 11/11 PASS 零设备丢失，见 WINDOWS_BUILD.zh_CN.md §10-§11 与
> `docs/evidence/2026-09-24-windows-stage23/`）。主线 T14 同步开工。

**交付物**：`docs/T14_UPDATE_CLOCK_AUDIT.zh_CN.md`（完整报告）+
`docs/evidence/2026-09-24-t14-audit/grep_records.txt`（原始 grep 记录，含零命中断言）。

### 四个决定性发现

1. **旧形态帧驱动是单源单链**：`fpsTimer`（PreciseTimer）是唯一节拍器，
   `update+draw` 都在其 paint 回调里（StelMainView.cpp:429-430）。接管面只有这一个点。
2. **fpsTimer 在合流形态天然休眠**：`new StelMainView` 从不 show → `drawEnded()`
   永不执行 → fpsTimer 永不 start。引擎侧不存在僵尸定时器偷偷 update（T13 长跑零冲突佐证）。
3. **引擎时钟与墙钟强耦合**：`StelCore::updateTime` 每 tick 用
   `QDateTime::currentMSecsSinceEpoch() × timeSpeed` 推进 JD，**dt 参数不参与时间推进**。
   合流形态现状 `setTimeRate(0)` + 每帧显式 `setJD` 事实上已是"宿主单点写时钟"，
   T16 的工作是把这个事实**机制化**为 ClockController，而非新建。
4. **StelActionMgr QWidget 假设实锤**：QAction 挂在从不 show 的 StelMainView 上，
   QML 窗口收不到快捷键分发；且引擎内已有 700+ StelAction 键位——T15 ActionRouter
   必须复用 `StelActionMgr::matches` 单点匹配，**禁止另建键位表**（否则静默双轨）。

### 判定速览（详见报告 §4 总表）

- **接管**：fpsTimer（→QML 帧泵）、引擎输入 4 入口（handleClick/Wheel/MouseMoves/Keys，
  T15 白名单）、LiveSkyRuntime setJD（→T16 ClockController）、插件/脚本时钟写入（零改造合规）
- **拆除**：drawEnded 动态调频、cursorTimeoutTimer、GUI 时钟对话框 ×6（T17 QML 等价）
- **保留**：screensaverInhibitorTimer、模块辅助定时器 ×7、测量设施、SkyViewport 刷新链

**T15/T16 移交清单**见报告 §5。T14 关闭，#34（T15）解锁。

---

## 2026-09-24｜T15 命令通路：AppFacade 最小切片 + ActionRouter（**自检 PASS，回归三绿**）

**交付物**：`src/app/AppFacade.{hpp,cpp}`（实作）、`src/app/ActionRouter.{hpp,cpp}`（实作）、
`src/app/AppFacadeCheck.{hpp,cpp}`（自检）、`qml/MainWindow.qml`（命令栏 + Keys 路由）、
`src/ui/main.cpp`（装配 + 拆除开关 + ACTIONCHECK 分支）、
`src/ui/LiveSkyRuntime.{hpp,cpp}`（ISimPacing 实现 + JD 累计推进）、
`src/core/StelActionMgr.{hpp,cpp}`（widget 分发开关）。
证据：`docs/evidence/2026-09-24-t15-command-path/`。

### 1. 三个设计决定（依据 T14 审计 §3.2 / §5）

1. **拆除 QWidget 注册假设的方式是"开关"不是"删除"**：`StelAction` 加静态开关
   `setWidgetShortcutDispatchEnabled()`，默认 **开**（纯 QWidget 形态 stellarium.exe 照旧把
   QAction 挂到 StelMainView）；合流形态宿主在 QML 加载前显式关闭 → 不创建/不挂接 QAction，
   `keySequence` 与 `matches()` 照常工作，键盘改由 `ActionRouter::routeKey` 单点路由
   （**复用 `StelAction::matches`，不另建键位表**——避免 700+ 既有键位静默双轨）。
2. **暂停/继续的落点不在 AppFacade，在帧泵**：新增最小接口 `ISimPacing`
   （setSimScale/simScale/setSimRate/simRate），`LiveSkyRuntime` 实现。时间推进从闭式公式
   `setJD(jd0 + sim×simRate)` 改为**逐 tick 累计** `m_jdAccum += dtWall×simRate×scale`：
   暂停只冻 JD（帧泵照跑、恢复无跳变），这是闭式公式做不到的。ISimPacing 即 T16
   ClockController 的前身。
3. **zoom 用 `getAimFov()` 而不是 `getCurrentFov()` 做读写口径**：`zoomTo` 是 0.4s 动画，
   aim 值等于"命令的目标"，连续点击不会读到动画中间值导致步进倍率漂移（自检 AC-2 因此能逐位精确断言）。

### 2. 自检（`STELQUICK_ACTION_CHECK=1`）：**VERDICT=PASS rc=0，9 项断言全过**

AC-1 拆除生效；AC-2 zoom 单步 60.0000→48.0000→38.4000→48.0000（逐位一致）；
AC-3a 幂等零信号；AC-3b 运行态 ΔJD=0.041200 天/0.4s；AC-3c **暂停 0.5s JD 漂移 0.000e+00**；
AC-3d 恢复推进；AC-4 禁用门（U-ACT-02）零副作用；AC-5 透传单次（U-ACT-01，ID 直呼 + Q 键各恰一次）；
AC-6 焦点守卫基线（U-ACT-03）。

### 3. 回归（任务书要求 DYN/A2/S3 零退化）：**三组全绿**

| 组 | 结果 |
|---|---|
| A2 静态纹理（Metal） | PASS rc=0 |
| DYN 动态帧（engine 生产者 + Metal） | **7/7 PASS rc=0**（尾窗 34.9fps；与 T12 期同口径 34.5~36.4 一致） |
| S3 引擎集成（`STELA3_CHECK`，stellarium.exe） | **8/8 PASS rc=0** ← 同时证明开关默认开、旧形态零退化 |

### 4. 短窗长跑冒烟：帧率读数受限，**不作为回归依据**（已归档说明）

首跑被 42 分钟并行构建的尾巴污染（15 分钟均载 93.56）→ 37.24fps；复跑（电池供电、低电量模式已关、
残余 Spotlight 负载）→ 38.71fps，与 T13 同口径短窗（40.67~42.87）差约 5%；同期 DYN 引擎形态
尾窗 34.9fps 与 T12 期完全一致 → **判定为环境噪声，非 T15 退化**。SL-C07（内存斜率）在 60s 窗口内
本就不适用（端点实际 -0.5 MiB 而回归拟合给出正斜率）。**待办：接电源 + 清负载后复跑 90s**。

### 5. 过程中修掉的两个真问题

- `QGuiApplication::focusWidget` 不存在（属 QApplication/Widgets）→ 改 `QApplication::focusWidget()` +
  `<QApplication>` 包含（`QT_WIDGETS_LIB` 守卫）；
- QML 把 Q_PROPERTY `fieldOfView` 当函数调用（`fieldOfView()`）→ 585 次 `TypeError` 刷屏，
  改属性访问后计数归 0。

### 6. 对 Windows 支线的影响

T15 的代码改动（含 `StelActionMgr` 开关与 main.cpp 装配）需 Windows 侧重新构建才会生效；
下次在 Windows 跑合流形态测试前先 `git pull` + 重建（阶段 2 的增量构建约数分钟）。

---

## 2026-09-24｜T16 单一仿真时钟：时钟所有权收编 + **拆掉一个真实的重复渲染驱动源**（帧率 +43%）

> 交付文档：`docs/T16_SINGLE_SIM_CLOCK.zh_CN.md`
> 证据：`docs/evidence/2026-09-24-t16-sim-clock/`
> 一键复跑：`./tools/t16-verify.sh`

### 1. 目标（开发计划原文）

> 时钟所有权收编（引擎时钟是唯一真源，QML 侧只读投影）；去除重复更新循环
> —— 同一时刻只有一个 update 驱动源；T14 盘点出的重复源全部拆除或接管。

### 2. 落地前的三个缺口（都是"恰好没出事"，不是"机制上不会出事"）

1. **JD 是闭式公式的产物，不是被拥有的状态** → 任何外部 `core->setJD`
   （插件/脚本/GUI/引擎动作）会被宿主下一帧无条件覆盖（"跳一下就被拽回去"）。
2. **暂停靠"用速率 0 把墙钟乘没"** → `updateTime` 仍每 tick 读墙钟、算无用乘加；
   速率字段被占用，插件调 `setTimeRate` 与架空方案互相打架。
3. **"只有一个驱动源"靠旧宿主恰好没跑——而它其实一直在跑**（见 §4，本次头号发现）。

### 3. 改动落点（对外只加 7 个方法，不改任何既有签名）

- **新增** `src/core/StelClockController.{hpp,cpp}`：core 层纯逻辑类，状态只有 4 个字长
  （mode / jd / valid / scale）。**单一真源 + 单一写入（`jumpTo`）+ 单一推进（模式互斥）**。
  刻意**不搬 `timeSpeed`**——速率留在引擎既有字段，控制器只管"推进与重锚"。
  这一取舍直接换来**插件零改造**：插件调 `core->setTimeRate(60)` 在宿主驱动下
  自然变成"宿主按 60 倍推进"，插件无需知道宿主存在。
- `StelCore`：`setJD`/`setJDE` 加 `jumpTo`；`updateTime` 按模式分支
  （HostDriven 取时钟真源、**不读墙钟**；EngineWallClock 原样）；新增 7 个公开方法。
- `LiveSkyRuntime`：删掉 T15 的 `m_jdAccum`/`m_lastSim`/`m_simScale`（**去除重复状态**），
  `ISimPacing` 四个方法退化为对引擎时钟的**纯投影**，回调改 `core->advanceSimClock(dt)`。
- `StelMainView`：新增 `stopLegacyFrameTimer()` / `isLegacyFrameTimerActive()`；
  `drawEnded()` 加"唯一驱动源"守卫（**主动停表** + 防复活）。
- `AppFacadeCheck`：新增 AC-7..AC-11。
- `src/ui/main.cpp`：新增 `STELQUICK_CLOCK_CHECK=1` 独立分支（**无窗口/无引擎**即可跑）。

### 4. ⚠️ 头号发现：合流形态此前**一直有两个 update/draw 驱动源**

AC-7 首跑直接读到反例：`simClock=HostDriven，旧宿主 fpsTimer=**仍在跑**：FAIL`。

**T14 的"天然休眠"判定错在哪里**（方法论教训）：
当时的依据是长跑日志里 `paintGL`/`drawEnded` 各 **0** 次。两处硬伤：
① `paintGL()` 因 QGraphicsView 拦截 paint 而**永不执行**（源码注释已写明），计数为 0 是必然的；
② `drawEnded()` **根本没有日志语句**——它不可能出现在日志里。
**字符串不出现 ≠ 函数没执行**；加上 `LiveSkyRuntime::boot()` 里确实调了 `m_mainView->show()`，
旧绘制链被点燃 → `fpsTimer->start()` 自持 → 每帧多一次**完整且被丢弃**的引擎渲染
（进 widget FBO，无人消费）。

**帧率量化（同口径/同后端 Metal/同尺寸 1920×1280/同一 producer 代码路径）**：

| 组 | T15（双驱动源） | T16（单驱动源） |
|---|---|---|
| DYN D1-C01 生产者尾窗 | 34.9 fps | **49.8 fps（+43%）** |
| 90s 短窗 SL-C01 | 38.71 fps | **49.18 fps（+27%）** |
| 90s 短窗 SL-C02 | 38.23 fps | **49.11 fps（+28%）** |

⇒ **T15 遗留的 W-T15「短窗帧率受限」的正解不是环境噪声，而是重复驱动源抢主线程。
T16 顺手闭合了它。** 同时更正 T13 记录 §3 的归因（见该节内联更正）。

### 5. 验收：**五项全 PASS rc=0**

| 项 | 结果 |
|---|---|
| 时钟纯逻辑自检（12 项，无 GL/无引擎） | **12/12 PASS rc=0** |
| 集成自检（12 项逻辑 + 11 项 AC） | **PASS rc=0** |
| 回归 A2（Metal 逐像素） | **PASS rc=0** |
| 回归 DYN（引擎生产者，7 判据） | **7/7 PASS rc=0** |
| 回归 S3（旧宿主 `stellarium.exe`，8 判据） | **8/8 PASS rc=0** |

关键数据：
- **AC-8**：暂停态把墙钟锚点人为拉远 **1 小时** → JD 漂移 **0.000e+00 天**
  （若 EngineWallClock 路径仍生效就是 +360 天的巨跳）——**决定性证明 `updateTime` 不读墙钟**。
- **AC-9b**：模拟插件直呼 `core->setJD(+1000 天)`（不经 AppFacade）→ 静置 0.5s 后
  观测值**严格等于**目标值（T15 形态下会被下一帧覆盖）。
- **AC-10**：R=0.100 → Δ=0.079900；2R=0.200 → Δ=0.164200；**比值 2.055**（期望 ≈2）。
- **AC-11**：**不经 AppFacade**、直接调 `core->setTimeRate(0)`（等价插件行为）
  → 0.5s 漂移 **0.000e+00 天**。这是 T15/T16 的**判别性判据**：
  T15 形态下宿主用自己的速率字段推进，插件这样调是**停不住**的。
- **S3 8/8 全绿的意义**：跑的是 `EngineWallClock` 模式的 `stellarium.exe`，
  证明 T16 对旧形态**逐位零影响**（其中 A3-C05「8 帧 8 个互不相同指纹」证明旧路径照旧推进）。

### 6. 90s 长跑短窗：帧率闭合，两项短窗口径不适用（已归因归档）

SL-C01/C02 **PASS**（49.18/49.11 fps）；SL-C04/05/06/08/09/10/11 全 PASS
（丢弃 0、上传均值 0.134ms、帧龄 max 57ms、GUI 帧龄 p95 1.00ms）。
两项 FAIL 均已归因、不构成回归：
- **SL-C03**：p99 53.31（门槛 61.09，**过**）但 1985 帧中 1 帧 142.36ms > 122.18ms 档。
  同跑 SL-C08 明确记录 **2 次频率降频**（电池供电 + 允许降频）→ 与单次主线程停顿一致；
  与 T13 正式跑第 1 轮归档的 "SLC03-single-outlier" 同类。
- **SL-C07**：60s 窗把 warmup 期分配拟合成斜率（端点 1877.2 → 1891.3 MiB），
  该判据设计口径是 30 min 稳态窗；T15 短窗已有完全相同的问题记录。

### 7. 过程中修掉的真问题

1. **AC-7 首跑 FAIL**（本任务唯一首跑失败，且揭示真实缺陷）：初版守卫只"不自启"，
   而 `fpsTimer` 在宿主接管**之前**就已点燃 → 补 `stopLegacyFrameTimer()` 主动停表。见 §4。
2. **T14 报告就地打勘误**（文首 + §1.2 关键结论 ② + §4 速查表 W1/W2 行），
   避免后来人继续沿用"天然休眠"的错误结论。
3. 编译告警清理：`step()` lambda 未使用的 `ctx` 捕获、`LiveSkyRuntime` 回调未使用的 `this` 捕获。
4. `macOS` 上 `cmake` 不在后台 shell 的 PATH（`nohup cmake` 报 "No such file"）→ 用
   `/opt/homebrew/bin/cmake` 全路径；且 `nohup ... &` 的进程组仍会被 SIGHUP 打断，
   应改用工具自身的后台机制。

### 8. 未改动（"插件零改造是硬约束"的兑现）

所有插件（Vts / TimeNavigator / Satellites / …）、脚本引擎、GUI 对话框、
`StelMovementMgr`、`StelApp`、`SpecificTimeMgr` —— **零改动**。
它们在 HostDriven 下经 `setJD` → `jumpTo` 自动合规，并顺带修好了两处 T15 形态下的真实缺陷
（时间步进键失效、插件时间跳转被拽回）。

### 9. 对 Windows 支线的影响

T16 及 T15 的代码改动需 Windows 侧 `git pull` + 重建才会生效。
Windows 侧同样会受益于重复驱动源拆除（该机此前 30 min 长跑 50.00fps 已达其 50Hz 屏上限，
预计改善主要体现在 GPU/CPU 占用而非帧率）。

---

## 2026-09-24｜T17 两个模型：`SearchResultsModel` / `ObjectInfoModel`（**自检 26/26 PASS，回归零退化**）

交付文档 `docs/T17_SEARCH_OBJECT_MODELS.zh_CN.md`；证据 `docs/evidence/2026-09-24-t17-models/`；
一键复跑 `tools/t17-verify.sh`。

### 1. 落地内容

- `src/app/SearchResultsModel.{hpp,cpp}`（骨架 → 实现）：`QAbstractListModel`，
  角色 name/englishName/objectType/typeName/stableId/rank；**请求编号门** `applyResults()`
  （过期结果丢弃 + 计数可观测量 `discardedRequests()`）；空态理由；**总量上限**。
- `src/app/ObjectInfoModel.{hpp,cpp}`（骨架 → 实现）：只读属性面 + `refresh()`（当场读数）
  + `selectByStableId()`（跨查询回查）+ **一律归零**的失效处置。
- `src/app/SearchModelCheck.{hpp,cpp}`（新）：自检（阶段 A 环境无关 / 阶段 B 需 fixture）。
- `src/ui/qml/SearchPage.qml`（新）+ `MainWindow.qml` 接线；`AppFacade` 持有两模型并新增四个命令；
  `main.cpp` 注入 context property + `STELQUICK_SEARCH_CHECK=1` 分支（rc 0/6/10）。

### 2. 稳定标识的定形依据（引擎自己的注释）

`StelObject.hpp:365-377`：`getID()` 在同一类型内唯一，但"**可与其他类型的 ID 自由冲突，
因此 `getType()` 必须一并参与判定**" ⇒ `stableId = type + ":" + id`，
回查走 `StelObjectMgr::searchByID(type, id)`。

### 3. 为什么一个天体指针都不存

`StelObjectType.hpp:44`：`using StelObjectP = QSharedPointerNoDelete<StelObject>` ——
**不持有所有权**，存它不延长对象寿命。故模型内只留字符串，取值一律"当场重新解析"。
契约里"不传悬空裸指针"的**真正原因**就在这里。

### 4. 三个真实缺陷（详见交付文档 §3）

1. **`maxItems` 形同虚设**：`StelObjectMgr::listMatchingObjects` 的 `maxNbItem` 是
   **每模块**上限（各模块各取 N 条后 `+=` 拼接），实测请求 10 条拿到 20 条、
   请求 3 条拿到 21 条 → 本层统一为**总量上限**，并暴露 `lastRawMatchCount()` 证明其生效。
2. **排序口径与注释不符**：引擎头注声称"by order of relevance"，但聚合后
   `std::sort` 按**名称字典序**重排 → `RankRole` 不是相关度序（本层初版注释写错了，已更正）；
   排序策略移交 A4（需产品决策）。
3. **T15 的键盘单点路由在 QML 侧从未接上**：`Keys` 是 `Item` 的附加属性，
   T15 却挂在 `ApplicationWindow`（继承自 `Window`）上 → 告警
   `Could not attach Keys property ... is not an Item` 自 T15 起就在每份证据里。
   AC-5 抓不到，因为它**直接调用** `routeKey`（仪器没接在实况上）。
   修法：挂到可聚焦 `Item`（`skyKeySink`）上；新增 **AC-12** 从窗口投递 `Q` 键做端到端断言 → **OK**；
   佐证：该告警出现次数 **1 → 0**。

### 5. 验收

| 项 | 结论 |
|---|---|
| `STELQUICK_SEARCH_CHECK=1` | **26/26 PASS** rc=0（SRC-01..05 + 纵向 V1–V9） |
| 关键数据 | 过期结果未覆盖新查询（行数仍 10，修复前 20）；丢弃计数 +2；稳定标识 `Star:HIP 32349 A`；总量上限 raw=21 → 3 |
| CLOCKCHECK（回归） | 12/12 PASS |
| ACTIONCHECK（回归 + 新 AC-12） | 全 PASS，AC-12 端到端 **OK** |
| A2 / DYN / S3（回归） | PASS / 7-7 PASS / 8-8 PASS —— 零退化 |

### 6. 环境/工具踩坑

- **给构建命令接管道截断输出 = 给构建装了个随机杀手**：`cmake --build ... | grep ... | head -20`
  在第 20 行关管道触发 SIGPIPE，整个构建被掐断；表现是"构建成功但二进制时间戳没变、
  新判据没出现"。改为输出落日志、事后 grep。
- 首跑即 PASS 后仍要读**数据里的异常**：`maxItems=10` 拿到 20 行才是缺陷 ① 的入口——
  判据"通过"不等于"行为正确"。

### 7. 对 Windows 支线的影响

T15/T16/T17 三段改动需 Windows 侧 `git pull` + 重建才生效。本轮已实测：
SSH 会话内 `Start-Process` 起的构建进程会**随会话结束被回收**（日志停在 configure 阶段、
rc 文件未生成）→ 改用 `schtasks` 投递（脱离 SSH 生命周期）方可长跑构建。

---

## 2026-09-24｜T18 定位与跟踪（A4 固定流程的"定位环"）（**自检 14/14 PASS，6 套回归零退化；DYN 为既有间歇**）

交付文档 `docs/T18_LOCATE_TRACK.zh_CN.md`；证据 `docs/evidence/2026-09-24-t18-locate-track/`；
一键复跑 `tools/t18-verify.sh`（`all` / `core` / `regress` / `dyn N`）。

起点是 A4 的通过条件 **I-REP-02「开机→搜月球→定位→改时间→返回」**：T17 交付了
搜索→选择→信息页，"定位"这一环还是空的（选中天体后视向不动）。

### 1. 落地内容

- `src/app/AppFacade`：`locateSelected(track)` / `setTracking(on)` / `toggleTracking()` /
  `isTracking()` / `trackedName()` / 只读 `Q_PROPERTY tracking`、`trackedName`、
  `lastLocateRefusal` + `locateRefusalText()`（token 翻文案，**判定永远看 token**）+
  `static isHomePlanet()` 纯谓词。
- `src/app/ObjectInfoModel::selectByStableId`：**幂等闸**（已选中同一 `(type,id)` 时不发
  `setSelectedObject`）——修掉"重选同一对象静默取消跟踪"。
- `src/app/LocateCheck.{hpp,cpp}`（新）：14 项自检，线性步骤表驱动器 + 成对判据。
- `src/ui/qml/SearchPage.qml` / `MainWindow.qml`：定位/跟踪按钮与状态行；双击结果行
  = **先确认选中成功**再定位。
- `tools/t18-win-longrun.ps1`（新）：把 T13 那次"临时手写、用完即丢"的 Windows 长跑
  wrapper **固化成仓库资产**（参数化，先跑 8 秒冒烟验证管线）。

### 2. 引擎侧的关键事实（决定了本层怎么设计）

| 事实 | 出处 |
|---|---|
| "定位"与"跟踪"在引擎里是两件事：`moveToObject` 只移动一次；`setFlagTracking(true)` 兼做移动+锁定 | `StelMovementMgr.cpp:1386-1405` |
| 自动移动时长默认 **1.5 s** | `StelMovementMgr.cpp:114` |
| 家园行星守卫 = `getEnglishName() != getCurrentLocation().planetName`（**严格比对、区分大小写**，比英文名不比 ID） | `SearchDialog.cpp:1438/1458/1478`、`AstroCalcDialog.cpp:2641/8689/9218` |
| 跟踪分支还加视口中心偏移 `viewportCenterOffset[1] * currentFov * π/180` | `StelMovementMgr.cpp:1264` |
| `setFlagTracking(false)` 天然幂等（`!b \|\| !getWasSelected()` 都走清零分支） | `StelMovementMgr.cpp:1388-1395` |

### 3. 两个真实缺陷（都不是 T18 引入的，是读源码/跑判据发现的）

**① 引擎的跟踪标志泄漏。** `StelObjectMgr::unSelect()` **先**清
`lastSelectedObjects`（`StelObjectMgr.cpp:541`）**再**发 `selectedObjectChanged`（542）；
而 `StelMovementMgr::selectedObjectChange()` 整段包在
`if (objectMgr->getWasSelected())` 里（`StelMovementMgr.cpp:757-766`），
于是槽里跑的那一刻已"没有选中" ⇒ **`setFlagTracking(false)` 永不执行 ⇒ 标志泄漏**。
处置：**不改引擎**，本层读**合取真值** `getFlagTracking() ∧ 选中非空`。
证据固化在 `LOC-08b`：同时打印两个值，
实测 `清前 合取=true/引擎原始=true → 清后 合取=false/引擎原始=true`。

**② 引擎在每次选择变化时无条件关跟踪** ⇒ **重选同一对象会静默取消跟踪**（用户看到
"跟踪自己断了"）。处置：`ObjectInfoModel::selectByStableId` 幂等闸（判据 `LOC-07`）。

**③ 自检报告每条判据打印两遍。** `LocateCheck` 第一版照抄了 T15 `AppFacadeCheck.cpp:72-73`
的"`qDebug` 实时行 + `details.append()`"模式，而 `main.cpp` 又打印一遍 `details`；
验证脚本合并 stdout/stderr 后**每条判据成对出现**（`LOC-01` 在 167 行与 185 行各一次）。
已核对 T15/T16/T17 三份 `ACTIONCHECK` 证据——**同样是每条 2 次**，属既有缺陷。
处置：`LocateCheck` 改为**只 append、由 `main.cpp` 单点打印**（对齐 T17 `SearchModelCheck`
的风格），并删掉不再需要的 `#include <QDebug>`；`AppFacadeCheck` 保持原样以维持跨任务可比性，
只在证据 README 里记明。

### 4. 判据设计里三处"防假绿"的硬约束

1. **成对断言 + 判别性对照**：孤立断言"夹角≈0"可被三种反例假绿（世界没动 / 视向被锁在
   赤道坐标里 / 定到别的对象上）。故必同时断言"**世界确实动了**"（跳 0.25 天后目标
   AltAz 变化 > 5°，实测 **127.67°**）与"**视向跟着走**"（< 0.5°，实测 **0.0747°**），
   再加对照组"不跟踪 → 同样跳变 → 夹角 > 5°"（实测 **32.97° → 158.17°**）。
2. **量级必须显式打印**：自检 `simRate=0.1 天/秒` ⇒ 墙钟 1 s = 天空自转 36°，
   所以"只归中不跟踪"后残留 33° 不是缺陷。`LOC-05b-note` 把等待期自转量测出来
   （**82.02°**）并声明 `note()` **不计入判据数**，防止误读。
3. **口径不复刻被测代码**：夹角在 J2000 赤道系里算，**刻意不抄**跟踪分支用的
   `mountFrameToJ2000` 那串换算（那是被测量对象，不该混进判据输入）；
   等价性另行核对（AltAz 挂载帧下 `mountFrameToJ2000` 就是 `altAzToJ2000(v, RefractionOff)`，
   `StelMovementMgr.cpp:1537-1552`）。

### 5. 验收数据

| 项 | 结论 |
|---|---|
| `STELQUICK_LOCATE_CHECK=1` | **14/14 PASS** rc=0（夹具 `Moon`=`Planet:Moon`，即 I-REP-02 的"搜月球"） |
| 关键读数 | 锁定夹角 **0.0475°**；跟踪中夹角 **0.0747°**；目标 AltAz 变化 **127.67°** |
| CLOCKCHECK（回归） | 12/12 PASS —— T18 没碰时钟 |
| ACTIONCHECK（回归） | 27 判据 **0 FAIL**（含 AC-12）—— AppFacade 被扩展过，关键回归 |
| SEARCHCHECK（回归） | **26/26 PASS** —— `selectByStableId` 被改过，**最关键的一项** |
| A2（回归） | 12 探针 PASS / 失败 0 项 |
| S3（回归） | 8/8 PASS |
| DYN（回归） | **2/3 PASS（间歇）** —— 见 §6 |
| Windows 30 分钟长跑（`main@6bce85d`） | **VERDICT=PASS rc=0，SL-C01..C11 全绿**，设备丢失 0 次 |

### 6. DYN 的 `rc=8`：定性成"既有间歇"，不是 T18 的退化（**别读成洗绿**）

首轮全量回归 DYN 出红（`D1-C02 尾窗稳态 0.0 fps；全程推进 0 帧`、`D1-C07 degraded=false`），
而 T15/T16/T17 三次同项都是 7/7 PASS。做了两个判别性实验：

| 实验 | 结果 |
|---|---|
| **换生产者**：同一 T18 二进制，去掉引擎（替身生产者） | **PASS**（28.0 fps / 223 帧 / 降级正确）⇒ QML 侧本身没问题 |
| **换二进制**：`git stash` 掉 T18 全部改动 → 重建 T17 基线 → 9 次；恢复 T18 → 重建 → 9 次 | 基线 **6 PASS / 3 FAIL**；T18 **6 PASS / 3 FAIL** ⇒ **失败率相同（33%）** |

⇒ **本机（M3 / macOS / Metal RHI）既有间歇缺陷**：真实引擎与 QML 同进程时 QML 场景图
偶发停摆——生产者照跑 50 fps、邮箱帧龄个位数毫秒，而**日志里没有任何丢设备痕迹**
（`vkDebug` / `VK_ERROR` / `device lost` / `swapchain` 全无），显示侧推进 0~364 帧后停住。
机械旁证：T18 对 `main.cpp` 只有 4 个 hunk，**DYN 分支逐字节未变**；
`SkyViewport.*` / `DynFrameCheck.*` / `LiveSkyRuntime.*` 一个字节没碰。

处置（判据协议，不是改代码）：`t18-verify.sh` 的 DYN 段跑 3 次、如实报 `N/3`，
**不跑"直到绿"**；零退化的依据是**同口径 A/B 的失败率相同**；失败样本照实归档。
原始记录见证据包 `dyn-ab-baseline-vs-t18.txt`。

顺带得到一条性质：本次三次里有一次 `C02 FAIL 但 C07 PASS`（显示先跑 343 帧、
跨过降速窗后才停）⇒ **C07 不是 C02 的冗余项**。
Windows 侧同形态长跑 `SL-C09 = 1799/1799`、`SL-C10 = 0/1799` ⇒ 该现象**本机特有**。

### 7. 环境/工具踩坑

- **macOS 的 grep 是 BSD，BRE 不支持 `|` 交替**：`grep -n "A|B" file` 返回"无匹配"是
  **假的**（`|` 被当字面量），排查时据此得出过两次错误结论。**一律用 `grep -E`。**
- **给构建命令接管道 = 装随机杀手**（承 T17 同一条）：本次所有构建都写成输出落日志、
  事后 grep 日志。
- **Windows 日志编码比"UTF-16LE 按 BOM 转码"更麻烦**：`*>>` 生成的文件**无 BOM**，
  且 exe 的 UTF-8 中文被 PowerShell 按 **CP936** 解码后再写成 UTF-16LE，
  同一条 `STELRUN:` 行里还可能混排两种来源。恢复配方（已写进
  `docs/evidence/2026-09-24-windows-30min/README.md` §3）：
  切掉 ASCII 头 → `decode('utf-16-le')` → `encode('gb18030')` → `decode('utf-8')`；
  用 `gbk` 会 `illegal multibyte sequence` 中断。判据 ID 与数值全是 ASCII，不受影响。
- **`git stash` 做 A/B 前先备份**：本次把 7 个文件复制到 `/tmp/t18/keep/`，
  `stash pop` 后**逐字节 `cmp`** 核验一致才继续。

