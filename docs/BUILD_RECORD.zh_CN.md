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
  正确判据是**设备级** `VK_KHR_portability_subset`。未修，已记录。
- `docs/BUILD_RECORD` 遗留：`STELQUICK_RENDER_WORKAROUND=basic-loop` + A2 不退出（诊断模式兼容性）。
