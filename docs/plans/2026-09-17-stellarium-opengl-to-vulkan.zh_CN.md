# Stellarium 从 OpenGL 迁移到 Vulkan：技术可行性、实施计划与性能预测

日期：2026-09-17。对象：本地 Stellarium CMake 26.1 源码快照。

> 后续决策更新：用户已确定自行维护、重写 QML 界面并实现 Vulkan 天空。本文件保留为最初调查，关于官方接收、继续旧 UI、根据性能决定是否迁移的建议不再作为开发前提。当前执行顺序以 [计划一](2026-09-17-01-qml-ui-vulkan-development.zh_CN.md) 和 [计划二](2026-09-17-02-native-vulkan-sky-development.zh_CN.md) 为准。计划一允许受限的临时 CPU 帧桥；它不是最终架构，也不推翻本文对该桥性能成本的警告。

## 1. 决策摘要

**结论：增加 Vulkan 渲染后端有技术可行性，但不是替换几个 OpenGL 调用，也不是改 Qt 环境变量就能完成。建议有条件立项，保留 OpenGL 后端，先验证窗口/UI 和大气依赖两个关键风险，再决定是否投入完整迁移。**

本项目将天文计算、投影、绘制、Qt 界面、插件和 OpenGL 状态连接在一起。最容易成功的部分是绘制三角形、纹理与基本星空；最可能超期的部分是界面叠加、投影着色器、大气外部依赖、阴影及第三方插件。

本报告的“可行”是源码检查和官方 API 文档支持的工程判断，**不是已经在该项目或鸿蒙真机上验证成功**。当前没有 Vulkan 实现、没有同场景 GPU 测量，因此不能承诺“整体快 30%”或“翻倍”。第 8 节给出可复算的假设模型与验证办法。

如果最终目的仍是鸿蒙适配，必须把两个目标分开：

- 鸿蒙移植：窗口、Qt 平台插件、文件与资源、输入、权限、生命周期、发布。
- Vulkan 渲染：图形后端与资源管理改造。

前者不必然要求后者；后者完成也不等于鸿蒙适配完成。在没有真机能力调查之前，不应把 Vulkan 设为鸿蒙方案唯一前提。

## 2. 调查范围与证据可信度

检查了本地构建定义、主循环、通用绘图、恒星、行星、大气、纹理、投影及插件相关代码，并核对 Qt/Khronos 官方文档。原目录无 Git 元数据，不能据版本号断言与上游某提交完全一致。复制文件 SHA-256 见 `../vulkan/source-manifest.json`。

GitNexus 当前没有可查询的已索引仓库，本报告不声称有完整知识图谱覆盖；调用关系依据源码检查。静态搜索没有发现现成的 Vulkan/QRhi 后端，但这不等同于形式化证明所有路径都被穷尽分析。

|证据类别|本次已完成|仍未完成|
|---|---|---|
|源码|构建与核心绘制路径检查；独立主要源码副本及哈希基线|完整模块逐函数审计、全部插件图形行为审计|
|平台|核对 Qt Vulkan/RHI 接口、MoltenVK、OHOS Surface 规范|当前机器 Vulkan SDK/驱动验证、鸿蒙真机窗口呈现|
|正确性|识别主要视觉与生命周期风险|双后端图像回归、交互回归、完整编译|
|性能|瓶颈分析和数学情景模型|任何实测速度、显存、功耗提升|

源文件定位以下均相对于提取目录；行号基于本次快照，后续编辑会变化。

## 3. 当前渲染结构：真正需要迁移什么

### 3.1 一帧如何发生

```text
StelMainView（QGraphicsView，视口为 QOpenGLWidget）
  └─ StelRootItem::paint（QPainter 原生绘制区段）
      ├─ StelApp::update → StelCore/各模块更新
      └─ StelApp::draw → 预处理/模块按顺序绘制/后处理
          ├─ StelPainter：线、面、纹理、文本
          ├─ StelSkyDrawer / StarMgr：恒星可见性与批量绘制
          ├─ Planet：行星、环、阴影、材质
          ├─ Atmosphere：普通及 ShowMySky 大气
          └─ 各插件：部分直接操作 OpenGL
```

不是在 `QOpenGLWidget::paintGL()` 中集中画完所有东西。`src/StelMainView.cpp:398` 的根场景项绘制通过 `beginNativePainting()` 包围 `app.update()` 与 `app.draw()`，再交回 Qt 绘制。替换错入口会出现“Vulkan 三角形能画，但 Stellarium 不更新或界面丢失”。

### 3.2 关键迁移面

|位置|源码发现|迁移含义|
|---|---|---|
|`src/StelMainView.cpp:87,398,620`|QOpenGLWidget、QGraphicsView、QPainter 交错|窗口和 UI 合成要重新设计，不能只替换 viewport 类型|
|`src/core/StelApp.cpp:469,837,1108`|初始化要求当前 GL 上下文；更新/绘制模块；读取当前 FBO|初始化、更新中的 GPU 操作也要清理；默认目标并非总是 framebuffer 0|
|`src/core/StelModule.hpp:65,77,81` 与 `StelModuleMgr.cpp:208`|模块 init/update/draw，绘制调用排序|保留模块生命周期与透明混合顺序；多线程不能随意并行 QObject 模块|
|`src/core/StelPainter.cpp:714,730,758`|文字以 QPainter 栅格化到 QImage，再缓存 QOpenGLTexture|先复用文字排版/栅格化，替换纹理与绘制；不必先重写字体引擎|
|`src/core/StelSkyDrawer.cpp:435,467`|恒星 CPU 投影、每星扩成六顶点、统一 glDrawArrays|已有批处理，不能假定每颗恒星一个 draw call|
|`src/core/modules/StarMgr.cpp:1341`|分区可见性、星表遍历、天文修正|大量 CPU 工作不因 Vulkan 自动消失|
|`src/core/StelOpenGLArray.hpp`|封装仍暴露 QOpenGLBuffer/VAO|必须让资源句柄与 GL 类型解耦|
|`src/core/StelTextureMgr.cpp:36`|后台线程池与当前 GL 上下文并用|拆分 CPU 解码与 GPU 上传，明确线程和资源存活|
|`src/core/modules/Planet.cpp:4001,4296,4867`|动态 shader、阴影 FBO、uniform 和多条绘制路径|重新定义管线、资源绑定、渲染目标及阴影采样|
|`src/core/StelProjector.cpp:465,503` 与 `StelProjectorClasses.*`|动态拼接正向/反向投影 shader|不能只转换 data/shaders 文件；需覆盖运行时投影变体|
|`src/core/modules/AtmosphereShowMySky.cpp`|外部渲染器与 GL shader/目标耦合|Vulkan 完整迁移涉及外部依赖，不是包一层就解决|
|`src/core/TextureAverageComputer.cpp:74,107`|glReadPixels/glGetTexImage 读回|可能造成同步；异步化要验证曝光反馈时延影响|
|`plugins/Scenery3d/src/` 等|独立 OpenGL 渲染、几何 shader、阴影等|核心能运行不代表插件全部完成迁移|

所有 GPU 相关路径都需在 P0 生成进一步审计表，不能以本表替代完整清单。

构建层面，根 `CMakeLists.txt` 除 src 外还加入 data、textures、星表、天空文化、插件、po 等目录；Qt Widgets、Gui、OpenGL、Charts、Positioning 等依赖也不是都可以随着图形 API 一并删除。当前 C++17，Qt 支持分支最低版本与新 API 可用版本不同。

## 4. 平台与技术路线查证

### 4.1 Qt 可以接 Vulkan，但不自动转换已有 OpenGL 代码

`QVulkanWindow` 提供设备、交换链、命令缓冲及窗口大小变化等基础管理，可作为桌面原型入口。其存在证明 Qt 应用可以使用 Vulkan，不证明 Stellarium 当前 UI 能原样工作。[Qt QVulkanWindow](https://doc.qt.io/qt-6/qvulkanwindow.html)

关键约束：`QWidget::createWindowContainer()` 中的原生子窗口有堆叠和渲染限制，尤其不能按普通 QWidget 假设其可与 `QGraphicsProxyWidget` 或 `QWidget::render()` 互操作。因此，把 QVulkanWindow 塞进旧场景，再期待工具栏/面板自然覆盖上去，不是已验证方案。[Qt QWidget：createWindowContainer](https://doc.qt.io/qt-6/qwidget.html#createWindowContainer)

`QRhiWidget` 从 Qt 6.7 提供跨图形 API 的 QWidget 接口，可作为另一条 UI 原型路线；但 QRhi 家族兼容性保证有限、需要 GuiPrivate，并且不能假设它是 QGraphicsView 原 OpenGL 视口的直接替代品。一个顶层窗口内混合不同 RHI 后端也有限制。[Qt QRhiWidget](https://doc.qt.io/qt-6/qrhiwidget.html)

**工程选择：先用“后端无关接口 + 原生 Vulkan + 独立最小窗口”验证绘制；在 P1 同时做一个有时间上限的 UI 合成实验，最终只选择一条正式 UI 路线。** 不承诺同时维护两套 Vulkan 实现。

|候选路线|适合情况|代价/退出条件|
|---|---|---|
|原生 Vulkan + QVulkanWindow 或平台 Surface|明确研究 Vulkan，需控制同步、资源和鸿蒙 WSI|UI 重构较多；Qt QPA 不支持 Vulkan 时，需另做原生宿主|
|QRhiWidget + QRhi Vulkan 后端|Qt Widgets 集成优先，愿意固定 Qt 版本|不是裸 Vulkan 项目；提高 Qt 最低版本；需证明 UI 合成可用|
|Qt Quick UI + RHI|已有独立 UI 重写计划|UI 大改，不作为当前迁移默认前置任务|
|保留 OpenGL，先优化现有热点|目标主要是性能或快速鸿蒙试运行|不能满足“原生 Vulkan 后端”交付，但应作为投入对照组|

若选 QRhi：macOS 默认可能走 Metal，必须记录并显式选择实验要求的后端，不能将 Metal 测试结果标成 Vulkan。对照实验必须按相同实际后端定义。

### 4.2 着色器、资源与同步可行，但需要重写接口

Vulkan 采用显式资源与同步管理，有机会降低驱动 CPU 开销，也会将更多责任交给应用。相同 GPU 工作不会仅因换 API 就自然更快；过度同步甚至变慢。[Khronos Vulkan basics](https://docs.vulkan.org/samples/latest/samples/vulkan_basics.html)

需要转换 GLSL 接口，包括 uniform block、显式 binding、顶点输入、采样器、渲染目标与坐标约定。原生 Vulkan 路线使用 SPIR-V 工具链；QRhi 路线使用 Qt Shader Tools 的 qsb 打包，不可把 `.qsb` 当裸 SPIR-V 文件使用。[Qt qsb](https://doc.qt.io/qt-6/qtshadertools-qsb.html)

具体策略：

- 先保留天文/投影 CPU 双精度计算，不以 GPU FP64 支持为前提。GPU 几何精度按现有输出验证。
- 把散落的 shader 字符串、投影变体和 data/shaders 纳入统一清单；能离线编译的离线化，必须动态生成的明确编译/缓存与失效机制。
- 明确 GL 与 Vulkan 的裁剪空间、Y 方向、深度范围、正反面、纹理方向、sRGB/线性空间、预乘透明度。采用一致的变换方案，避免重复翻转。
- 禁止依赖“上一模块留下的 GL 状态”；每个绘制包明确管线、纹理、混合、深度与 scissor。
- 移动端和 MoltenVK 不保证全部桌面特性。几何 shader、宽线等必须查询能力并设计替代路径，例如多次绘制立方体面、用三角形实现线条。
- 资源释放按 GPU 完成的帧延迟回收；每帧环形上传缓冲与 descriptor 生命周期独立管理。不要每帧 `vkDeviceWaitIdle()`。

### 4.3 macOS 与鸿蒙分别有额外门槛

macOS 原型需要 MoltenVK 将 Vulkan 映射到 Metal，不是原生 Vulkan 驱动。必须记录实际支持的扩展和功能，按 portability subset 限制实现。[MoltenVK](https://github.com/KhronosGroup/MoltenVK)

Qt `QVulkanInstance` 是否可创建还取决于 Qt 构建和 QPA 平台插件；类存在不等于该平台窗口能呈现。Qt 6.5 起默认启用 portability driver 枚举；自行创建 VkInstance 的路线则要自己处理相关 flags/extensions，不能混用两种初始化说明。[Qt QVulkanInstance](https://doc.qt.io/qt-6/qvulkaninstance.html)

Khronos 已定义 `VK_OHOS_surface` 和 `vkCreateSurfaceOHOS`，以 OHNativeWindow 创建呈现 Surface。这是存在 OHOS 接入机制的证据，**不是所有商业 HarmonyOS 版本、设备和 Qt 发行包都支持的证明**。[VK_OHOS_surface 规范](https://registry.khronos.org/vulkan/specs/latest/man/html/VK_OHOS_surface.html)

鸿蒙 P1 必须记录：系统/SDK 版本、设备/GPU、loader 可用性、实例和设备扩展、队列 present 支持、Surface formats/present modes、交换链重建、前后台切换、Qt QPA 与 OHNativeWindow 所有权。任何一项不满足都应先阻断鸿蒙 Vulkan 交付，不能用 Android 宏或桌面模拟测试代替。

### 4.4 ShowMySky 是独立决策点

本地 CMake 的 ShowMySky 获取路径指向 CalcMySky v0.4.0；当前调用方使用 GL 上下文、shader 和渲染目标。完整迁移至少需要以下之一：

1. 依赖上游提供可用 Vulkan 后端，并验证接口与质量；本次未找到足以宣称已经可用的证据。
2. 将对应渲染算法及资源格式移植到新后端，单独预算、核对依赖许可并维护差异。
3. 第一阶段仅支持普通大气，明确标注功能缺口；直到 ShowMySky 通过验证，不能称“完整等价迁移”。

不建议把跨 API 图像共享作为默认跨平台解决方案；它依赖平台外部内存/同步扩展，可能不覆盖 MoltenVK 或目标鸿蒙设备。CPU 全帧读回再上传仅适合诊断：4K RGBA8 约 33.2 MB/帧，60 帧每秒仅单方向原始像素就约 1.99 GB/s，尚未算同步和额外复制。

## 5. 建议的代码改造边界

以下是待实现设计，提取工作没有创建这些渲染功能。

建议增加 `src/core/render/`：

- `RenderDevice`：能力查询、纹理/缓冲/管线创建，后端选择。
- `FrameContext`：本帧目标、命令记录、临时上传和完成标记。
- 后端无关资源句柄：禁止向模块暴露 GLuint、QOpenGLTexture 或 VkImage。
- `DrawPacket`/材质参数：顶点、变换、纹理、混合及深度状态；保持原模块排序。
- `OpenGLBackend`：先包住已有实现，作为视觉/行为基线。
- `VulkanBackend`：在相同接口下完成呈现、资源和绘制。

不要一次改掉所有模块。先提取一条“纹理矩形/普通线段/恒星批次”纵向链路，证明 OpenGL 重构前后相同，再增加 Vulkan。Planet/ShowMySky/Scenery3d 这种特殊模块允许专用 pass 接口，避免为了抽象而把所有行为压成一个万能 draw 函数。

`init()`、`update()` 中的 GL 上传也必须迁移成显式 GPU 准备阶段；暂不改变 QObject 与模块的线程归属。后台只做 CPU 解码、几何准备等已证明线程安全的工作，之后再评估多线程命令记录。

插件需要后端能力声明。遇到未迁移插件，应明确拒绝启用、在启动时切回整个 OpenGL 后端，或显示缺失功能；不允许悄悄不绘制。不能在同一帧任意混用旧 GL 插件和 Vulkan 目标。

建议未来增加可选后端构建/运行开关，但现有项目尚无此开关。默认保持 OpenGL；Vulkan 创建失败时提供清晰诊断与可配置回退，避免启动即崩溃。

## 6. 分阶段计划与停损条件

工作量为工程估算，不是实际工期承诺；按熟悉 C++/Qt/图形 API 的工程师计，不含等待设备、上游评审或供应商支持。

|阶段|预计工程周|产物|通过标准 / 停损条件|
|---|---:|---|---|
|P0 基线与完整清单|1–2|可复现 Release 构建、场景脚本、GL 调用与资源清单、设备报告|相同配置稳定运行；找出 CPU/GPU 主瓶颈。若仅追求性能且驱动占比很低，优先优化 GL|
|P1 平台/UI/大气路线验证|2–4|最小 Vulkan 窗口、UI 叠加/输入原型、目标设备呈现、大气处理决定|高 DPI、缩放、弹窗、全屏、前后台不出错；UI 无全帧 CPU 读回。鸿蒙不支持所需 WSI 时停止鸿蒙分支|
|P2 渲染抽象及 GL 回归|3–5|后端接口、GL 适配器、资源生命周期、模块能力声明|现有 OpenGL 视觉/行为基本无变化；核心模块不再获取具体 GL 资源|
|P3 Vulkan 基础设施|4–6|交换链、资源、同步、管线、shader 编译缓存、调试标记|验证层无错误；重建/设备失败可恢复或安全退出；无逐帧全局等待|
|P4 核心天空等价|4–8|恒星、线网、文字、天空背景、普通大气、地景、行星/环/阴影|按功能矩阵逐项图像/交互回归；不能只展示一张静态星空|
|P5 高级功能与插件|4–10|ShowMySky、Scenery3d、特殊视口、截图/录制及相关插件方案|每项有等价实现或明确的不支持清单；完整迁移必须消除必要功能缺口|
|P6 优化与交付|3–5|多平台基准、内存/功耗结果、文档、CI、可评审补丁|性能目标与正确性达标；上游未接受也有可维护独立分支|

合计约 **21–40 工程周**，并非加几天 API 替换。某些阶段可由不同人员并行，但 P1/P2 未解决前并行重写大量功能通常只会返工。单人还不熟悉项目型 C++ 时应先完成小规模验证，不按上述熟练工程师估算承诺交付日期。

建议第一次投入只批准 P0–P1。其结束时提交继续/缩小范围/暂停的明确结论，而不是默认继续投入全部预算。

## 7. 正确性与兼容性验收

功能矩阵至少包含：

- 星空：宽视场/望远镜小视场、银河、星等与闪烁、星座线、中文/拉丁/阿拉伯文字、选中标记。
- 投影：透视、鱼眼及其他启用投影；地平线边界、极区、跨零点、屏幕边缘裁剪。
- 太阳系：月球、行星环、日月食、阴影、近距离表面与纹理切换。
- 环境：昼夜、地景透明、普通大气、ShowMySky、高动态范围与曝光。
- 系统：窗口缩放、高 DPI、多显示器、全屏、截图、后处理、球面镜视口、插件、脚本启动/退出。
- 稳定性：最小化、前后台、Surface 丢失/重建、资源热加载、长时间运行、内存不足和创建失败。

图像测试固定观察位置/时间/FOV/资源/曝光；关闭或固定随机闪烁，避免把随机变化当回归。科学坐标与投影数值用已有单元测试和专门数值断言验证；渲染差异采用像素误差与人工检查结合，文字边缘/抗锯齿允许有论证的容差。不能仅凭平均图像相似度放过“少了一颗关键天体”。

测试每种受支持设备功能组合；Geometry shader 等可选能力缺失时的替代路径也是交付内容。帧率高但天空方向、色彩或文字错了不算成功。

## 8. 能提升多少性能：条件预测而非承诺

### 8.1 首先明确哪些工作不会自动变快

本地恒星绘制已经批量提交。Vulkan 的“减少 draw call 驱动负担”不意味着能把成千上万次恒星提交降成一次——这项优化的很大一部分当前已存在。星表遍历、天文计算、CPU 投影、文字栅格化、纹理解码不会因更换 API 自动加速。

恒星实例化、文字 atlas、减少不必要的读回、缓存投影等可能有收益，但其中多项同样可在 OpenGL 实现。报告必须区分 **API 更换收益** 与 **算法/批处理优化收益**。

Khronos 的性能指导强调测量瓶颈与避免不必要的同步。应使用 GPU 时间戳与平台 profiler 观察真实执行，不能把 CPU 提交完成时间当 GPU 绘制时间。[Khronos profiling](https://docs.vulkan.org/guide/latest/profiling.html)

### 8.2 可复算的模型

稳态 CPU/GPU 有重叠时，简化为：

```text
旧帧时间 ≈ max(CPU业务时间 + CPU提交时间, GPU时间) + 串行等待
新帧时间 ≈ max(CPU业务时间 + CPU提交时间/k + 新增开销, 新GPU时间) + 新串行等待
FPS = 1000 / 帧时间毫秒
```

`k` 是提交工作的加速比，必须由测量得到；不是 Vulkan 固定常数。模型不适用于直接预测首次启动、shader 编译尖峰、输入到显示延迟或所有流水线阶段。VSync、最大帧率及电源策略还可能压住最终 FPS。

下表统一**假设**提交耗时减半（k=2）、GPU 时间不变、无帧率限制和额外串行等待；所有输入都为示例，不是本项目测量数据：

|假设场景|CPU业务/提交/GPU 毫秒|新增开销毫秒|旧→新帧时间毫秒|旧→新 FPS|FPS变化|
|---|---|---:|---|---|---:|
|GPU 受限|4 / 2 / 10|0.3|10 → 10|100 → 100|0%|
|CPU 提交受限|4 / 8 / 6|0.5|12 → 8.5|83.3 → 117.6|+41.2%|
|天文 CPU 计算受限|12 / 2 / 6|0.5|14 → 13.5|71.4 → 74.1|+3.7%|
|重 GPU 场景|3 / 3 / 20|0.5|20 → 20|50 → 50|0%|
|合成/管理开销过大|3 / 2 / 5|1.5|5 → 5.5|200 → 181.8|−9.1%|

运行 `node tools/performance-model.mjs` 可复算。模型告诉我们：**收益可能接近零，也可能在提交受限场景达到数十个百分点，集成不当还会倒退；当前不能判断 Stellarium 落在哪一行。** 在 60 Hz 锁帧且原本已稳定 60 FPS 的设备上，即使渲染更快，显示帧率仍可能不变，应观察功耗、CPU 占用和掉帧。

另一种上限检查：CPU 可优化部分占比 p，若该部分快 k 倍，则 CPU 加速比为 `1 / ((1-p)+p/k)`。p=10%、30%、50%，k=2 时分别约 1.053、1.176、1.333 倍；这仍不是整帧 FPS 的直接增幅。

**预算建议：不要以“平均帧率提升 30%”作为立项收益承诺。** 可设有条件验收目标：在 P0 确认的 CPU 提交受限场景争取 ≥20% FPS 提升，其他关键场景中位帧时间回退不超过 5%，p95/p99 不显著恶化。20%/5% 是建议项目门槛，不是预测结果，应由基线和使用需求最终决定。

### 8.3 测量方案与归因

准备四组：A 原始 OpenGL；B 只做渲染抽象的 OpenGL；C 等价算法的 Vulkan；D 增加实例化/atlas/异步上传等优化的 Vulkan。重要优化若也适用于 GL，再加 GL 优化对照，避免全部收益归给 Vulkan。

必须相同 Release 优化、Qt 版本、资源集、分辨率/DPR、MSAA、曝光、插件、时间/位置/FOV与驱动环境。**不能拿 Vulkan Release 对比旧 OpenGL Debug**：本地 `StelOpenGL.hpp` 的调试检查涉及 GL 错误查询，会污染比较。

每场景建议预热 60 秒、采样 120 秒、重复 5 次、随机交替后端；启动/冷缓存另测。桌面同时报告关闭帧率限制的吞吐与实际 VSync 使用场景，移动端记录温度、电源状态和热降频；同一设备上做配对比较。

至少六类场景：普通夜空、密集恒星/标签、放大行星与阴影、日间/ShowMySky、大型 Scenery3d、4K 高 DPI UI。只有支持同等功能时才能对比，禁用高级大气后的速度不能算等价提升。

记录 CPU update/提交/合成时间、GPU pass 时间、p50/p95/p99 帧时间、提交数量、上传字节、显存/内存峰值、shader 首次编译卡顿、启动耗时；功耗必须有可信测量手段。时间戳查询确认队列支持和 timestampPeriod，延迟回收结果，不能为了统计每帧等待 GPU。

可用 CSV 字段：`run_id,backend,build,device,driver,scene,frame,cpu_update_ms,cpu_submit_ms,cpu_composite_ms,gpu_ms,frame_ms,draws,upload_bytes,memory_bytes`。汇总时保留原始记录、重复实验分布与异常解释，不只给最高 FPS 截图。

## 9. 上游接受与长期维护

技术可行不等于官方愿意接受。当前上游仍将项目描述为 OpenGL 实时天空应用；这既不能证明会拒绝 Vulkan，也不能证明已经批准迁移。[Stellarium 上游仓库](https://github.com/Stellarium/stellarium)

在 P0 阶段向维护者提出 RFC，说明动机、Qt 最低版本影响、OpenGL 保留策略、维护人、设备/CI 资源、插件与 ShowMySky 处理方式，并询问是否愿意接收可选实验后端。未经授权，本次没有替用户发布 issue 或联系维护者。

建议提交顺序：基准/回归基础设施 → 无行为变化的抽象 → 可选后端 → 模块分批迁移 → 平台支持。不要把整个目录复制结果当成一个上游 PR。因为本地没有 Git 历史，正式开发前应选定可追溯的上游基线，将本地差异单独审阅，不能直接假定这是干净的 26.1。

如果维护者不愿承担新后端，应提前决定是否有能力维护独立分支。保留贡献文件和许可证；新增依赖及资源分发按各自条款另行核查。不要把可以研究/修改开源代码等同于获得官方品牌背书。

## 10. 本次交付与下一步

已交付：主要源码工作副本、链接资源说明、SHA-256 清单、资源独立化工具、假设性能模型、本报告。未交付：Vulkan 渲染代码、可运行的 Vulkan Stellarium、鸿蒙包、实际性能测量或官方接受承诺。

下一步优先 P0：先构建并测量现有 OpenGL Release，再做 P1 的窗口/UI 与目标设备原型。只有知道瓶颈和平台能力后，才值得决定完整迁移范围。若只做一个起步任务，选择“固定场景基线 + 最小 Vulkan/UI 原型”，不要从全局搜索替换 gl 开始。
