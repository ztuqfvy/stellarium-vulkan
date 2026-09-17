# 开发计划一：QML 界面重写与 Qt Quick Vulkan 后端

日期：2026-09-17｜状态：待实施｜工作目录：`/Users/ztuqfvy/qt_demo/stellarium_vulkan`

后续：[计划二：Stellarium 天空原生 Vulkan 渲染](2026-09-17-02-native-vulkan-sky-development.zh_CN.md)。

## 1. 项目决定与完成边界

这是个人维护的 Stellarium 分支，不以官方接受、合并或保留原有 UI 形态为前提。已决定使用 QML 和 Vulkan，不再设置“性能收益不够就放弃 Vulkan”的决策门槛。技术关卡用于发现问题、调整方案和缩小单次交付，不改变最终目标。

本计划完成的是：**新的 QML 应用界面由 Vulkan 绘制，但天空仍由旧 OpenGL 渲染器产生，通过一个明确限期使用的画面传递层显示。** 计划二才去掉 OpenGL 天空。界面布局可以重新设计，不必逐像素照搬桌面工具栏。

本轮仅编写计划；下文新类、目录、目标和测试名都是拟建项目，不代表已经实现。证据来自本地源码和 Qt 官方文档，未进行本项目 QML/Vulkan 原型运行。GitNexus 当前无索引；不声称有图谱覆盖。CLion 只读查询未取得可用返回，未据此假定目标副本已有 IDE 构建配置。

阶段版本定义：

|版本|用户能做什么|是否算计划一完成|
|---|---|---|
|A-demo|Vulkan QML 窗口、假数据界面、静态天空图|否，只证明 UI 工具链|
|A-alpha|真实旧天空进入 QML，时间/地点/搜索/视角可操作|否，核心可玩里程碑|
|A-1.0|约定的个人版 UI 全部 QML 化，旧天空稳定供帧，接口和回归资料交接齐全|是；仍不能称天空 Vulkan 化|

“个人版 UI 全部”定义在第 3 节；不等于把上游所有科学分析对话框和插件全部重写。未纳入功能必须明确列出，不以隐藏旧窗口冒充完整迁移。

## 2. 架构选择：第一阶段为什么需要中转

已查证：`QQuickFramebufferObject` 只适用于 Qt Quick 的 OpenGL 后端，不能用于 Vulkan。这排除了“把原天空 FBO 原样塞进 Vulkan QML”的捷径。[Qt QQuickFramebufferObject](https://doc.qt.io/qt-6/qquickframebufferobject.html)

同一 QQuickWindow 内的原生绘制需要与场景图使用相同 API；后端要在创建窗口前选定。显式请求 Vulkan，并在场景图初始化后检查实际 API，不能凭环境变量或 UI 能显示就认定成功。[Qt QQuickWindow](https://doc.qt.io/qt-6/qquickwindow.html)

### 2.1 默认过渡路线

```text
QML 面板/手势 → AppFacade → 现有 C++ 天文与控制逻辑
                                  ↓
                        LegacySkyHost（旧 GL 上下文）
                                  ↓ 天空专用 RGBA 帧
                           有界的 CPU 帧中转
                                  ↓ 上传
                    SkyViewport（Qt Quick Vulkan 纹理）
                                  ↓
                         同窗口 QML 面板合成
```

这是工程设计，不是本机已验证方案。先验证**同进程、独立旧 GL 宿主 + 新 QQuickWindow**；必要时用辅助进程隔离旧渲染宿主。独立 QOpenGLContext 与 Vulkan 共存，不等于给两个普通 QQuickWindow 选择不同后端。

第一版仍使用 QApplication，允许 Widgets 作为旧宿主内部依赖；不能一开始改为仅 QGuiApplication 并删除 Widgets。所有 QML 控件和公开交互使用新界面，旧 QWidget 不进入新 QML 窗口的控件树。

画面路径先采用 RGBA8 内存帧，不用逐帧 PNG/JPEG 编码、不写图片文件、不用变更图片 URL 的缓存技巧传帧。上传在场景图线程进行，可先用 QQuickItem/QSGImageNode 与 `createTextureFromImage()` 验证，再按实测优化纹理更新。传递层的接口不能暴露 GLuint，便于计划二替换。

### 2.2 过渡路线的限制与备选

- 隐藏旧窗口不保证继续绘制。必须实现显式帧驱动，证明离屏目标和上下文有效；不能只调用 hide() 后等 paint 事件。
- 现有真实绘制入口在 `StelRootItem::paint()`，不是简单的 `paintGL()`；取帧位置在天空及必要后处理结束后、旧工具栏绘制前。
- 同进程原型失败时，先隔离为辅助进程：版本化命令/状态协议 + 有界共享内存帧环；锁、进程退出、尺寸变化和帧世代均需处理。此分支预计增加 1–3 工程周。
- 双窗口“QML 控制窗 + 旧天空窗”只作调试工具，不算最终单窗口 A-1.0。
- 跨 GL/Vulkan 外部内存共享不是默认计划，不能假设在 macOS 和鸿蒙都能实现。
- 同一 QWidget 顶层树中混合 Vulkan QQuickWidget 和 QOpenGLWidget 有后端冲突，不作为备选捷径。[Qt QQuickWidget：Graphics API Support](https://doc.qt.io/qt-6/qquickwidget.html)

**过渡性能预算（待测目标）：** 默认 1280×720、30 帧/秒天空；性能不足允许明确显示为 15 帧/秒预览，不隐藏降级。UI 自身按显示刷新调度，但共享 GUI 线程被 GL 阻塞时 UI 也可能卡顿，要实测，不承诺自动达到 60 FPS。

1080p RGBA8 的读回加上传，在 30 帧/秒下约 0.50 GB/s 逻辑数据量；4K60 约 3.98 GB/s。计算为 `宽×高×4×FPS×2`，不是物理总线实测，未计其他复制。统一内存也不免除同步成本。因此 A-1.0 不追求 4K60，也不用于证明 Vulkan 天空性能。

## 3. 第一阶段 UI 范围

|功能|A-alpha|A-1.0|说明|
|---|---|---|---|
|天空拖动、缩放、选中、取消、跟踪|必须|必须|含触控、鼠标、输入遮挡|
|暂停、实时、加减速、日期时间设置|必须|必须|区分 UTC、时区、显示历法；不重新发明天文时间算法|
|观察地点、经纬度、高度|必须|必须|先手工/本地列表；设备定位权限不是桌面入口前置条件|
|本地天体搜索、结果列表、定位|必须|必须|先不接远程搜索服务|
|选中天体信息|必须|必须|通过只读模型提供；不让 QML 持有失效天体指针|
|星座线/名称、网格、地景、大气等开关|基本开关|约定功能完整|开关状态必须与引擎双向同步|
|亮度/星等、视场、投影、主题/夜视、高 DPI|基础|必须|天空夜视效果仍由旧后处理负责，避免叠加两次|
|资源路径、错误提示、渲染诊断、配置保存|最小|必须|配置使用独立个人版目录，避免修改原程序设置|
|快捷键编辑、帮助、版本与许可证页面|可延后|必须|中文输入焦点不能触发天空快捷键|
|高级天文计算、脚本控制台、全部插件设置|不做|默认不纳入|列入后续清单；需要时逐模块增加，不静默打开旧对话框|

开始实施时清点 `src/gui/*.ui`、旧 action 和插件 UI，形成“迁移/暂不支持/后续”台账。本计划表是个人版初始范围，不可据此声称原项目所有界面已覆盖。

## 4. 已核实的源码边界

以下为 source-derived 事实；行号以本地 26.1 快照为准，不代表上游最新提交。

|现有位置|已知耦合|计划中的处理|
|---|---|---|
|`src/StelMainView.cpp:398–431`|GL 上下文中同时 update/draw，再交还 QPainter|提取旧宿主帧驱动及天空取帧；A 不把这些调用搬到 QML 渲染线程|
|`src/core/StelApp.cpp:469–496`|init 要求当前 GL context、读取主视图 DPR、创建纹理管理器|A 保留旧宿主；用窗口服务接口隔离新 UI 需要的信息|
|`src/core/StelActionMgr.cpp:66–70`|action 创建时取 StelMainView 并注册 QAction|先复用动作语义，拆掉必须注册到 QWidget 的假设；统一快捷键路由|
|`src/core/StelGuiBase.hpp:36`|GUI 初始化接口依赖 QGraphicsWidget|不把 QML 伪装成旧 QWidget；新增独立 facade，旧 GUI 仅留兼容宿主|
|根 `CMakeLists.txt:510–521`|ENABLE_SCRIPT_QML 是脚本引擎，GUI 模式只有 Standard/None|新增 Qt Quick 应用目标，不把现有 QML 脚本开关当 UI 开关|
|`src/CMakeLists.txt:590–617`|stelMain 包含 GUI/Widgets/GL 依赖|先渐进拆目标；不能把 stelMain 当现成无图形天文库|

可复用业务入口（先通过 facade 包装，所有修改在对象所属线程执行）：

|业务|当前入口|必须保留的语义|
|---|---|---|
|时间|`StelCore.hpp:493,499` 的 setJD/getJD；`:592` 的 setTimeRate|JD 为 UT；时间速率单位是 Julian day/second，不能直接把“60倍”传成60|
|地点|`StelCore.hpp:322,400` 的 getCurrentLocation/moveObserverTo|使用位置值对象；复用观察者切换逻辑|
|搜索/选择|`StelObjectMgr.hpp:78–105` 的 findAndSelect/listMatchingObjects|处理空结果、类型与稳定标识；过期结果丢弃|
|定位/跟踪|`StelMovementMgr.hpp:193,265,291` 的跟踪、移动和 zoomTo|复用 `SearchDialog.cpp:1468–1484` 中当前观察星球不可普通跟踪的判断|
|显示选项|`StelPropertyMgr.hpp:324–342` 的属性访问|仅开放白名单；检查只读、类型、缺失值及是否有 NOTIFY|

已有 `ENABLE_TESTING` 和 `buildTests`，源码含 testDates、testRefraction、testStelProjector 等。它们用于保护计算行为，不替代新增 QML 交互和 GPU 测试；本次未运行这些测试。

## 5. 拟建接口、目录与线程约定

拟建目录：`src/app/`（应用接口与状态）、`src/ui/qml/`（页面组件）、`src/ui/quick/`（C++ 场景图项）、`src/render/legacy/`（旧天空宿主与帧桥）、`tests/ui/`、`tests/integration/`。先新增后替换，不整体移动现有 src。

|接口/对象（拟建）|责任|禁止事项|
|---|---|---|
|AppFacade|暴露时间/地点/选择/显示选项的稳定接口|不暴露 GL/Vulkan 句柄，不让 QML 直接遍历模块单例|
|ActionRouter|动作 ID、可用/选中状态、快捷键和输入焦点|同一次按键不能同时触发新旧 QAction|
|SearchResultsModel / ObjectInfoModel|增量列表、请求编号、稳定对象标识|过期搜索结果不能覆盖新查询；不传悬空裸指针|
|ViewportState|逻辑尺寸、物理尺寸、DPR、可绘区域、相机状态|不能把屏幕像素与 QML 逻辑像素混用|
|SkyViewport|显示天空、转发手势、提供 backend/ready/error 状态|QML 页面不关心天空来自 CPU 帧还是 VkImage|
|LegacyFrame / FrameMailbox|帧编号、状态编号、尺寸、格式、行步长、色彩、方向、世代|生产者不得覆盖消费者正在读取的数据|

线程与生命周期：

1. GUI/仿真线程持有现有 QObject 模块与业务状态，执行有序命令；A 的 GL init/update/draw 仍在所属上下文线程执行。
2. CPU 帧以不可变快照传递；两到三个槽位，只保留最新完整帧，忙时丢旧帧而不积压。限制内存，记录丢帧数和帧年龄。
3. Qt Quick 渲染线程只读取快照、更新纹理；不访问可变的 Stellarium 模块。
4. 输入携带 viewport 与 frame/state ID；缩放或尺寸世代变化后拒绝旧坐标，防止点击位置偏移。
5. 关闭时停止生产者→停命令→解除场景图资源→在所属上下文释放旧 GL 资源。最小化/恢复不能无限累积帧。

这些线程约定是本项目设计。Qt 场景图与 QML 对象可位于不同线程，所以需要明确同步边界。[Qt Scene Graph and Rendering](https://doc.qt.io/qt-6/qtquick-visualcanvas-scenegraph.html)

## 6. 开发任务与依赖

工程周指熟悉 C++/Qt 的开发者全职投入量，不是对个人日历工期的承诺。学习、设备和供应商等待另计。按任务产物验收，不按日期硬推进。

|任务|依赖|工作内容与交付|通过条件|估算|
|---|---|---|---|---:|
|A0 基线与独立工程|无|记录源码清单；实施时建立个人 Git 基线；独立配置/缓存；旧版 Release 构建；选定并锁定 Qt6 补丁版本|旧版可运行，原目录和设置不受影响；可复现构建记录|1–2 周|
|A1 Vulkan QML 宿主|A0|新增 Quick/QuickControls2/Qml 模块和最小界面；显式选择 Vulkan；显示实际 GPU/API|实际 API 为 Vulkan；缩放、关闭、重开无错；失败不静默回退 Metal/GL|1–2 周|
|A2 旧天空帧桥原型|A1|保留旧宿主，隔离天空目标，读回/上传；验证隐藏宿主帧驱动；限制尺寸和队列|连续动态天空与 QML 按钮同窗；方向/颜色/DPR 正确；无不断增长的队列|2–3 周|
|A3 业务接口与输入路由|A2|AppFacade、模型、ActionRouter；时间地点搜索选择；去除重复更新循环|每个命令只执行一次；UI/状态一致；只有一个仿真时钟|2–3 周|
|A4 核心交互页面|A3|工具栏、搜索、信息、时间、地点、拖动缩放、焦点管理|完成固定“开机→搜月球→定位→改时间→返回”流程，形成 A-alpha|2–3 周|
|A5 设置与完整个人版 UI|A4|第3节 A-1.0 页面、配置迁移、夜视、快捷键、错误页；旧 UI 禁止作为隐式回退|纳入范围的功能均从 QML 操作；未支持项有清单和提示|2–4 周|
|A6 回归、真机入口与交接|A5|测试、帧桥统计、资源打包说明、接口冻结；记录鸿蒙探测结果|第8节验收清单通过，输出可运行包与计划二输入资料|1–2 周|

主线约 **11–19 工程周**；辅助进程备选额外 1–3 周。不是完整上游界面逐项复刻估算。建议第一次只实施 A0–A2，再根据实际耦合重估后续。

Qt 版本策略：新分支只计划 Qt6，候选基线为 6.8 或更高、且桌面/目标设备均可获得的一个固定版本；A0 选择具体补丁版本后锁定。不假定当前安装包已包含 Quick、Vulkan 或鸿蒙 QPA。文档中的更新 API 要与锁定版本头文件核对，不能照抄较新版本专有接口。

## 7. 鸿蒙与 macOS 探测（不提前做天空迁移）

- 本机 macOS 的 Vulkan 通过 MoltenVK 接入 Metal；记录它是转译路径，但实际 Qt Quick API 仍必须为 Vulkan，不能直接选择 Metal 后算完成。[MoltenVK](https://github.com/KhronosGroup/MoltenVK)
- A1 就检查目标鸿蒙设备能否运行最小 QML/Vulkan 窗口，包括前后台、触摸、DPR、交换链恢复。`VK_OHOS_surface` 规范存在不代表当前 Qt QPA 已实现。[OHOS Surface 规范](https://docs.vulkan.org/refpages/latest/refpages/source/VK_OHOS_surface.html)
- 若设备不具备可用旧 OpenGL 路径，A 阶段完整帧桥产品可以先在桌面交付；鸿蒙记录为“QML Vulkan 原型通过/平台阻塞”，不冒称完整应用已适配。B 阶段 Vulkan 天空再接入已验证宿主。
- 若 Qt QPA 不能呈现 Vulkan，新增平台适配工作包并重估。不能把此问题延后到全部 UI 写完。

## 8. 验收与测试

以下为拟新增测试，不是现存已通过测试。

- 构建：全新目录 Release 构建；旧版对照目标仍可单独构建；Qt/SDK/资源版本有记录。
- 后端：启动日志、诊断页与渲染接口三者一致；故意禁用 Vulkan 后给出明确错误，不将其他后端视为成功。
- 功能：第3节 A-1.0 范围全部验收；日期/时间/地点改变后，画面与对象信息使用对应状态。
- 交互：中文输入不移动天空；弹窗遮挡不穿透；触摸取消、滚轮、快速 resize、DPR 变化后选择位置正确。
- 桥接：默认720p持续运行30分钟；记录平均/p95帧年龄、读回/上传时间、队列长度和内存；内存随时间不持续增长。吞吐门槛在 A2 基线后冻结，不临时降低后宣称达标。
- 生命周期：关闭/重开、最小化/恢复和资源重建各循环100次，无崩溃、死锁和悬空纹理；手机前后台在支持设备上实测。
- 配置：损坏配置可恢复；个人版不覆盖原版配置；只读资源链接不被生成工具改写。
- 自动测试：Facade 单元测试、QML Qt Quick Test、模型/焦点测试、固定场景图像和输入回放；场景图线程与 GPU 验证错误单独统计。

## 9. 交给计划二的硬性契约

第一阶段结束必须提供：

1. 单一 SkyViewport 和稳定 AppFacade；QML 文件中无 GL/Vulkan 特定调用。
2. 可复现旧 GL 对照应用与固定场景、设置、资源清单。
3. 帧格式文档：逻辑/物理尺寸、原点/方向、颜色空间、透明度、帧/状态/尺寸世代标识。
4. 统一命令路由、唯一仿真时钟、明确 QObject 归属与可复制状态结构。
5. 功能范围和旧 GUI/action 处理台账；禁止计划二再顺便重写 UI。
6. 分开的性能记录：原 GL 基线、A 阶段 GL+帧桥+QML Vulkan、桥接独立成本。
7. 在 A 阶段预留 renderer capabilities/status，B 可以显示模块支持程度，不改页面协议。

B 将 SkyViewport 的数据来源从 CPU 帧换成原生 Vulkan 纹理；QML 页面、命令、模型和布局不应再次重写。A 不要求提前设计完所有 Vulkan DrawPacket，也不做恒星/行星 shader 移植。

## 10. 风险处理与第一批执行任务

最高风险是旧 GL 宿主无法可靠离屏驱动、GUI 与 GL 同线程造成卡顿、动作系统耦合、目标设备 Qt 平台支持。每项在 A0–A3 解决，不留到美化界面之后。

前五个可执行任务：①验证原 Release 构建与资源；②做纯 QML Vulkan 窗口及诊断页；③实现一张静态 RGBA 图进入 SkyViewport；④实现旧天空动态供帧；⑤仅接入暂停/继续与视场调整。完成这条纵向链路，再展开全部页面。

`stellarium_vulkan` 中九个大型资源目录目前是指向原项目的符号链接；修改或打包资源前须按根 `README_VULKAN.zh_CN.md` 独立化，避免通过链接改到原项目。实施前建立版本控制是未来任务，本次没有初始化 Git、安装依赖或改生产代码。
