# 开发计划二：Stellarium 天空真正迁移到原生 Vulkan

日期：2026-09-17｜状态：待实施｜工作目录：`/Users/ztuqfvy/qt_demo/stellarium_vulkan`

前置：[计划一：QML 界面与 Vulkan 后端](2026-09-17-01-qml-ui-vulkan-development.zh_CN.md)。

## 1. 目标与非目标

用户已决定实现自己的 QML + Vulkan Stellarium，不再等待上游认可。本阶段不重新讨论是否采用 Vulkan，而是将旧天空生产者逐步替换成原生 Vulkan，实现可维护的个人版。

**完成标志不是界面日志显示 Vulkan，而是恒星、天空标签、行星等纳入范围的天空内容本身由 Vulkan 命令、资源和着色器生成，正常显示路径不再创建旧 GL 天空上下文，不再逐帧从 GL 读回画面。**

保留原 GL 程序作独立对照，不要求最终产品维护两套运行时后端。复用 Qt Quick 的 Vulkan 设备/队列/呈现机制也属于原生 Vulkan 天空实现，不要求为证明“原生”而另造窗口和交换链。在 macOS 上经 MoltenVK 转为 Metal 的实际后端须清楚记录。

本计划只写设计与任务，尚未实现；新增类型、目录、测试均为拟建。当前资料是本地源码检查与官方接口资料，不是已通过的本项目可行性原型。没有 GitNexus 索引；所有代码结论标为源码导出，不声称完整调用图分析。

## 2. 启动条件与两阶段衔接

开始完整 B 阶段前，A-1.0 应交付：稳定 SkyViewport/AppFacade、输入坐标协议、实际 Vulkan QML 窗口、旧 GL 基线、功能台账、独立配置及资源、可重现测试场景。没有这些条件时补齐 A，不一边大改 UI 一边大改渲染。

两份计划共同保留：

- QML 页面、搜索模型、时间地点接口、选择状态和快捷键路由。
- 逻辑尺寸与物理尺寸、DPR、viewport 世代、帧/状态版本号。
- 天文计算、星表格式、时间系统和 CPU 双精度数据，除非独立测试证明必须改造。

唯一主要替换：`LegacySkyHost → CPU帧桥 → SkyViewport`，变为 `SceneSnapshot → NativeVulkanSky → VkImage → SkyViewport`。

## 3. 正式接入路线：Qt Quick 管窗口，天空管自己的资源

默认选择**天空离屏 Vulkan 纹理导入 Qt Quick**。Qt 官方有直接创建 VkImage 并在 Quick 场景显示的例子，因此该接入机制有官方依据；Stellarium 的多 pass 逻辑仍需自行实现。[Qt Vulkan Texture Import](https://doc.qt.io/qt-6/qtquick-scenegraph-vulkantextureimport-example.html)

`QNativeInterface::QSGVulkanTexture::fromNative()` 在渲染线程、场景图初始化后包装 2D RGBA 图像，需提供真实当前 layout，且不拥有底层 VkImage；应用负责其内存与生命周期。[Qt QSGVulkanTexture](https://doc.qt.io/qt-6/qnativeinterface-qsgvulkantexture.html)

```text
GUI/仿真线程：AppFacade → 天文更新 → 不可变 SceneSnapshot
                                          ↓ 同步交接
Qt Quick 渲染线程：上传准备 → Vulkan 天空各 pass
                                          ↓
                            天空输出 VkImage（同一设备）
                                          ↓ barrier / 采样
                       SkyViewport + QML 面板 → Qt 呈现
```

### 3.1 资源和调度责任

|资源/职责|所有者|边界|
|---|---|---|
|窗口、主交换链、Qt 帧调度|Qt Quick|天空不独立 acquire/present，不擅自重置 Qt 命令池|
|Qt 暴露的 instance/device/queue/current command buffer|借用|不销毁，不跨帧缓存当前命令缓冲句柄，不任意从 worker 提交同一队列|
|天空纹理、深度/阴影目标、缓冲、管线、descriptor|NativeVulkanSky|自己创建和按 GPU 完成状态回收|
|QSG 纹理包装/节点|Quick 适配层|包装对象与底层图像分别管理，重建时不出现旧句柄|
|仿真模块和 QML 模型|GUI/仿真线程|渲染线程只能读取稳定快照或经过明确同步的副本|

B1 原型复用 Qt 的同一 VkDevice 和图形队列；从 QSGRendererInterface 获取资源，核对返回值的句柄指针类型及有效期。不允许另建 VkDevice 后直接传入不属于该设备的 VkImage。[Qt QSGRendererInterface](https://doc.qt.io/qt-6/qsgrendererinterface.html)

离屏 pass 和上传在 Qt 主 render pass 开始前记录；按 Qt 原生命令集成规则使用 `beginExternalCommands()/endExternalCommands()`，在 begin 后重新取得当前命令缓冲。主 pass 内绘制的时机不同，不可在已经打开的 pass 内再随意开启另一个 pass。[Qt Vulkan Under QML](https://doc.qt.io/qt-6/qtquick-scenegraph-vulkanunderqml-example.html)

初期不采用三套路线并行开发。全屏 underlay/QSGRenderNode 可以作为未来减少一次全屏采样的优化；QQuickRhiItem 也不作为当前默认路线，避免把“原生 Vulkan 天空”改成另一个泛图形封装项目。若后续采用 QRhi 封装，必须另写设计变更并验证目标没有被替换。

### 3.2 同步与输出契约

- B1 先只用图形队列，不以独立 transfer/compute queue、多线程录制、descriptor indexing 为前提。
- 从查询到的 API/扩展/格式能力建立最小功能集；不把 Vulkan 1.3、dynamic rendering、geometry shader、GPU FP64 设成默认硬要求。
- VkImage 从颜色写入到采样必须有正确 stage/access/layout 依赖；跨帧重新写入前也要确认上次采样完成。若引入其他队列，再设计所有权转移。
- 使用每帧槽位与资源世代管理。`frameSwapped` 和 `afterFrameEnd` 不是 GPU 完成证明；B1 必须落实与 Qt 帧槽复用或显式完成标记对应的退休队列，并用验证层/压力测试证明，不用固定“等两帧”猜测安全。
- resize/sceneGraphInvalidated 时停止旧设备资源使用；新设备世代重新创建。全局等待只允许有理由的关闭/重建，不进入每帧主循环。
- 第一版对 Quick 输出可采样 RGBA8；HDR 可在天空内部浮点目标计算后 tone map，待能力验证后再扩展最终显示格式。颜色空间、alpha 和方向沿用 A 的显示契约。
- 普通显示无 CPU 读回；截图是显式低频操作，走独立异步路径。

以上是本项目设计要求，显式内存可见性和执行依赖需依据 Vulkan 规范实现。[Khronos synchronization](https://docs.vulkan.org/spec/latest/chapters/synchronization.html)

## 4. 先拆生命周期，不能只替换 draw

建议将旧的 init/update/draw/deinit 职责逐步拆为：

1. CPU 初始化/数据加载：无 GL/Vulkan context 也能完成。
2. CPU 仿真：天体位置、可见性、时间推进，保持原模块依赖顺序。
3. 渲染准备：生成不可变 FrameScene、几何、标签、材质参数和资源请求。
4. GPU 准备：上传、创建/更新管线和图像，在渲染线程执行。
5. 命令记录：显式目标与状态，保持必要绘制顺序。
6. 资源退休：GPU 不再使用后释放；CPU 对象销毁不能直接销毁仍在使用的 GPU 资源。

这是渐进边界，不要求一次把全部模块重写成 ECS 或全新引擎。先做一条贯通恒星的链路，再扩大覆盖。

|源码证据（本地快照）|隐含风险|具体任务|
|---|---|---|
|`src/core/StelApp.cpp:469,837`|init 与 update 不是无 GPU 环境的纯业务入口|分离上下文查询、配置/数据初始化与 GPU 初始化；无 GL 宿主启动测试|
|`src/core/modules/LandscapeMgr.cpp:424,537`|update 会创建/加载大气、调用 computeColor|将大气 GPU 任务排入 prepare/pass，不留在仿真线程|
|`AtmospherePreetham.cpp:153`、`AtmosphereLightweight.cpp:91,487,633`|除 ShowMySky 外另两种大气也有 GL 资源/绘制|三种大气分别列迁移状态，不能只禁用 ShowMySky 就称解除依赖|
|`src/core/StelTexture.cpp:62,209,245,348`|bind 懒上传、等待加载、析构删GL纹理、查询旧主窗口能力|拆 CPU decode/上传队列/就绪句柄/延迟释放，测试取消与快速切换资源|
|`src/core/StelProjector.hpp:168–175` 与 `RefractionExtinction.cpp:281,450`|数学参数接口混入 QOpenGLShaderProgram/uniform|保留计算，导出后端无关参数与 shader 变体键|
|`src/core/StelModuleMgr.cpp:208`|模块 update/draw 各有顺序依赖|记录现有顺序；第一版不为批处理全局重排|
|`src/core/StelSkyDrawer.cpp:435,467`|恒星已批量提交，每星CPU展开六顶点|先等价移植；实例化作为独立优化，不假定原版每星一次提交|
|`src/core/StelPainter.cpp:134,714,730`|当前上下文、共享静态状态、QPainter文字纹理|先单线程准备；保留文字整形与栅格化，替换GPU缓存|
|`src/core/StelCore.cpp:555`|可保留上一帧颜色用于累积效果|支持星轨时新增持久历史图；不能依赖交换链上一帧内容|
|`src/core/modules/Planet.cpp`、`StelViewportEffect.cpp`、插件|行星阴影、后处理、视口变形及独立GL路径|逐项专用 pass；未迁移项明确禁用，不允许空画面静默成功|

表中未写目录前缀的大气文件在 `src/core/modules/`，RefractionExtinction 位于 `src/core/`。这些已定位事实仍不构成全部插件逐函数审计。

## 5. 拟建模块与数据契约

沿用 A 的 `src/app/`、`src/ui/quick/`，新增 `src/render/common/`、`src/render/vulkan/`、`data/shaders/vulkan/` 与 `tests/render/`，路径在实施时按工程约定确定。

- `SceneSnapshot`：simulationVersion、视口世代、时间/观察者/相机、可见对象和已准备的绘图数据；不含可变 QObject 裸指针。
- `RenderResources`：资源 ID + generation，管理 CPU 描述与 GPU 状态；失效句柄有诊断。
- `RenderPacket`：顶点/索引范围、材质、投影参数、混合/深度/裁剪、资源句柄、顺序层；不得继承上一模块隐藏 GL 状态。
- `NativeVulkanSky`：渲染资源和 pass 的实现，与 QML 页面无关。
- `QuickVulkanBridge`：借用 Qt 设备/帧上下文、SkyViewport 输出、场景图生命周期；不容纳天文计算。
- `RendererCapabilities`：模块支持、图像格式、可选功能与错误状态；方便 QML 禁用不支持功能。

shader 清单必须覆盖 `data/shaders/`、C++ 字符串、投影正反变换和折射变体。原生 Vulkan 使用 SPIR-V；Qt UI 自定义 ShaderEffect 若有需要则按 Qt Shader Tools 流程，二者资产不可混用。缓存键至少包括 shader 内容、宏/投影变体、设备与驱动、格式及相关管线状态；缓存失效不崩溃。[Qt QSB](https://doc.qt.io/qt-6/qtshadertools-qsb.html)

CPU 天文计算继续保持现有 double 精度。先保留恒星 CPU 投影与顶点布局，第一版不同时加入 GPU 天文计算、compute culling、mesh shader 或多队列。优化应在等价结果建立后单独测量。

天空标签与 QML 面板文字是两回事：旋转/重力标签、投影位置、混合次序仍属于天空渲染。不要为每颗恒星创建一个 QML Text；先复用现有 QPainter/QImage 文字栅格化，仅替 GPU 纹理与绘制。

## 6. 交付范围：真实 Vulkan MVP、个人版、全量版

|功能组|B-MVP|B-1.0 个人可玩版|长期全量等价|
|---|---|---|---|
|真实恒星、相机、时间地点、选择标记、基础标签|必须|必须|必须|
|透视及主要交互投影|至少透视|纳入个人范围的投影|全部现有投影与视口变形|
|星座/网格/方位、银河、深空对象及纹理|可延后|必须|必须|
|日月行星、行星环、基础阴影/食现象|不做|必须逐项验证，不用占位图冒充|完整高级材质与全部边界|
|常用地景、Preetham大气、夜视、截图|不做|必须|全部变体|
|Lightweight大气|不做|扩展里程碑|必须|
|ShowMySky|不做|扩展里程碑|必须单独移植或取得可用依赖|
|星轨/HDR高级流程/球面镜/Scenery3d/输出插件|不做|按个人需求单列扩展|全部纳入才可称全量等价|

B-MVP 可以先显示无大气的夜空，并明确“部分功能未迁移”。B-1.0 范围是计划默认目标，可以后续增加功能；不能在缺项后自行降低标准并宣称原计划完成。

ShowMySky 当前调用路径绑定 GL，外部 CalcMySky 依赖不是改调用名即可迁移。先保留其数据/算法边界研究，独立制定 renderer 移植任务；禁用此功能是范围选择，不是移植成果。

## 7. 工作包、依赖和估算

|任务|依赖|核心产物|通过标准|熟练工程周|
|---|---|---|---|---:|
|B0 冻结契约与审计|A-1.0|接口版本、GL依赖清单、模块/功能矩阵、基准场景|知道每项功能由谁处理；旧基线可复现|1–2|
|B1 原生Vk纹理接入|B0|同设备 VkImage 示例、Qt合成、资源与帧槽生命周期|Vulkan图形与QML同窗；resize/重建压力测试、验证层无错误|1–2|
|B2 CPU/GPU边界拆分|B1|仿真快照、资源队列、投影参数、旧窗口服务解除|已覆盖核心可在无GL上下文下初始化/更新；未覆盖模块显式关闭|3–5|
|B3 Vulkan基础资源与shader|B2|缓冲/纹理/descriptor/管线、SPIR-V、上传、绘图基元、计时|triangle/line/sprite/text可用；无逐帧全局等待；资源回收正确|3–5|
|B4 恒星夜空MVP|B3|星表可见性与批次、相机、选择、基础标签|天空来自Vulkan、无CPU图像桥；固定场景位置/亮度对照通过|2–4|
|B5 基础星图补齐|B4|星座网格、银河/深空、文字、主要投影|模块顺序和投影边界正确；交互不退化|2–4|
|B6 太阳系与环境|B5|日月行星/环/阴影、地景、Preetham、夜视/截图|逐功能图像/数值验收，得到B-1.0候选|4–7|
|B7 去GL、设备验证与性能|B6|产品目标剥离GL宿主/桥、目标设备构建、压力测试、基准报告|第8节通过，发布个人可玩版|2–4|

默认 B-1.0 主线约 **18–33 工程周**；其中 B-MVP 约10–18周。与计划一合计约 **29–52 工程周**，这是熟练工程量，不是“一个初学者一年一定做完”的承诺。首次原型后必须按实际代码耦合重估。

扩展包单独预算：Lightweight 约2–4周；ShowMySky 约4–10周或更多，取决于依赖改造；高级投影/星轨/Scenery3d及其他插件约4–10周或更多。范围很大且存在未知，不把三者并入 MVP 工期承诺。

鸿蒙已有可用 Qt Vulkan 宿主时复用 A 的探测成果；如果需要实现/修复 QPA、Surface、SDK工具链或平台生命周期，应另立平台任务。这些不确定工作未含在主线估算中。最终目标不变，但可先交付桌面 Vulkan 版本，不能用桌面运行代替鸿蒙验收。

## 8. 正确性、无 GL 与稳定性验收

### 8.1 正确性

- 固定观察时间/位置/FOV/星表/曝光和随机种子；先关闭闪烁，再测随机/动态效果。
- 保留现有日期、折射、投影、天文计算测试；新增 GL基线与Vulkan的对象ID、屏幕坐标、亮度及图像对照。
- 坐标校正只在一处定义：Y方向、深度范围、正反面、纹理方向、相机矩阵；覆盖地平线、极区、投影断缝、极小FOV和DPR变化。
- 保持透明绘制与模块计算顺序；只能在已证明可交换的批次内排序。
- 字体测试含中文、阿拉伯文、旋转标签、字体/语言改变；QML界面字正常不能证明天空标签正常。
- 行星与大气独立验收，不能用“整张图平均误差低”掩盖小天体丢失。容差按特征制定，并记录原因。

### 8.2 证明天空确实走 Vulkan

- NativeVulkanSky 的 command buffer/GPU捕获包含恒星、标签、行星等已支持功能的真实 draw，不只是贴一张旧天空截图。
- 发布运行路径不初始化 LegacySkyHost，不创建旧天空 GL context，不调用逐帧 glReadPixels；动态加载依赖和插件也要审计。
- 新产品构建目标去除对 legacy 渲染对象的链接；macOS 旧目标硬编码的 OpenGL framework 不能照搬到新目标。Qt库自身可能携带GL能力，不等于应用仍调用GL天空，二者区别报告。
- 产品目标应在不提供旧天空GL上下文的测试环境启动并完成全部纳入场景；Vulkan不可用时报错，不能回落旧天空后仍显示“Vulkan”。
- 旧 GL 仅保存在独立对照目标/基线，不删除原项目，也不把不支持插件偷偷接回 GL。

### 8.3 资源与生命周期

- Vulkan 验证层无未解释错误；release性能测试关闭验证层与额外GL调试检查。
- 连续30分钟场景切换无内存持续增长；大资源下载取消、纹理释放、shader缓存失效、设备/窗口重建均覆盖。
- resize、DPR变化、最小化/恢复、关闭/重开各100次；目标手机前后台与低内存单独验证。
- UI不会在GPU等待期间无限阻塞；内存上限、上传预算、资源请求取消与错误提示可观测。
- 截图明确“仅天空/含UI”两种语义；普通显示不因为截图支持而每帧读回。星轨如纳入使用持久历史图并定义重置规则。

## 9. 性能测试：不给固定提速承诺，但必须提供结果

本阶段必做 Vulkan，因此性能是优化和验收指标，不是取消项目的条件。若速度不及旧版，记录原因并优化，而不是用更低画质掩盖差距。

至少保留四个可区别的实验对象：

|对象|用途|
|---|---|
|原 OpenGL Release|原始性能与视觉基线|
|A阶段 GL + CPU帧桥 + Vulkan QML|衡量过渡产品，不作为唯一基线|
|B阶段等价算法 Vulkan + 同一QML|评估真实迁移与合成成本|
|B阶段优化后的 Vulkan|测实例化、缓存、异步上传等新增优化|

去掉 CPU 帧桥的提升与原 OpenGL→Vulkan 的提升分别报告。因 UI 已重写，整应用差异同时含 UI 成本；需要同时给天空 pass、CPU准备/提交、UI合成和端到端帧时间，不能把总差异都归因于 API。

同设备、同 Release 编译、同资源/质量/分辨率/DPR/MSAA/帧率策略；六类固定场景覆盖夜空、密集标签、行星阴影、普通大气、重纹理、4K UI。ShowMySky 未移植前不得对该场景做等价比较。

预热60秒、采样120秒、重复5次并交替顺序；测p50/p95/p99、CPU/GPU时间、上传字节/显存/内存、冷启动与首次shader卡顿。GPU时间戳延迟读取，不为统计而阻塞主循环。[Khronos profiling](https://docs.vulkan.org/guide/latest/profiling.html)

建议目标（不是测量结论）：个人主要设备1080p夜空争取60 FPS；其他同画质关键场景相对原GL中位帧时间回退不超过5%，p95/p99需解释。设备或场景不同，应在B0依据基线冻结目标。未达标应标记性能待优化，不能据此声称技术失败或虚构提速。

旧报告中的条件模型仍有效：若GPU或天文CPU工作受限，收益可能接近0；只有提交开销占比较大时才可能明显提升。当前没有实测百分比。也不再将固定性能提升作为用户已经决定迁移的前提。

## 10. 风险、收敛策略与首个实现闭环

|风险|收敛方式|
|---|---|
|Qt借用设备/命令缓冲生命周期错误|B1先证明，复用官方纹理示例思路；不提前展开所有天体|
|旧update隐藏GL依赖|B2按模块去上下文测试；未覆盖模块禁用并显示原因|
|shader变体爆炸/运行时编译卡顿|先盘点可达组合、离线编译常用组合、缓存并定义失效|
|同时改算法和API无法定位错图|先等价数据与顺序，优化独立提交和对照实验|
|完整大气/插件拖住基础可玩版本|MVP、B-1.0、扩展分开；缺项清晰标记|
|新手同时学Qt/C++/Vulkan导致返工|一次完成一条纵向链路，保存可运行里程碑，不开展全局重命名/重构|

首个 B 阶段闭环：在 A 窗口中显示原生 VkImage 测试图 → 持续resize不报错 → 使用同一输入协议拖动测试视图 → 加载真实星表和CPU投影 → 用Vulkan画第一批恒星 → 证明完全不启动旧GL宿主。完成后再开始行星、大气和全部插件。

这两份计划替代旧报告中“默认继续保留旧UI/等待官方态度/根据性能再决定要不要Vulkan”的建议；旧报告保留作历史技术调查。后续功能取舍以本两阶段范围和实施后的实测记录为准。
