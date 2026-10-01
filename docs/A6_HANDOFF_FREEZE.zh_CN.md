# A6 交接冻结：计划二硬性契约 7 项逐条核对

> **出处**：开发计划一 §7「交给计划二的硬性契约（A-1.0 时逐项核对）」，
> 共 7 条；测试文档 §8「A-1.0 出口」要求「计划二交接契约 7 项逐项核对签字」。
>
> **本文的性质**：不是宣传，是**核对表**。每条写三样东西：
> ① 契约要求什么；② **当前证据在哪**（可复核的命令/文件）；③ **残余与诚实标注**
> （哪一半没做到、为什么、留给谁）。
>
> 配套交付物：
> - `docs/A6_FRAME_FORMAT.zh_CN.md`（契约第 3 条）
> - `docs/A6_INTERFACE_FREEZE.zh_CN.md`（由 `tools/a6-interface-freeze.sh` 生成的公开面清单）
> - `docs/A6_FRAME_BRIDGE_STATS.zh_CN.md`（契约第 6 条，由 `tools/a6-frame-bridge-report.sh` 生成）
> - `docs/A6_ACTION_LEDGER.zh_CN.md`（契约第 5 条的台账）
> - `docs/A6_RESOURCE_PACKAGING.zh_CN.md`（A6 点名的"资源打包说明"）
> - `docs/A6_A1.0_EXIT_AUDIT.zh_CN.md`（出口 7 条终审）
> - 任务文档：`docs/T43_FRAME_BRIDGE.zh_CN.md`、`docs/T44_LIFECYCLE.zh_CN.md`、
>   `docs/T45_CONFIG_SAFETY.zh_CN.md`

---

## 契约 1｜单一 SkyViewport 和稳定 AppFacade；QML 文件中无 GL/Vulkan 特定调用

| 分句 | 现状 | 证据 |
|---|---|---|
| **单一 SkyViewport** | ✅ 全工程 QML 里只有**一处** `objectName: "skyViewport"`（`SkyTestPage.qml:24`） | `grep -rn 'objectName: "skyViewport"' src/ui/qml/` ⇒ 1 命中 |
| **稳定 AppFacade** | ✅ 以 `setContextProperty("appFacade", …)` 注入，T15 起未改名；命令一律走 `ActionRouter` | `src/ui/main.cpp:5481`；`ACTIONCHECK` 11 条 |
| **QML 无 GL/Vulkan 调用** | ✅ CI grep 门禁：`src/ui/qml/*.qml` 内无 `VulkanInstance`/`QSGRendererInterface`/`VkDevice`/`graphicsApi` 等记号 | `tools/a6-interface-freeze.sh` §5 输出当前实测行数（应为 0） |

> ⚠️ **门禁自身修过一次"假绿"**（陷阱 98，已实跑负控证实）：首版 §5 用
> `grep -E '<记号>' | grep -v '禁止' | grep -v '不得' | grep -v '//'` 排注释行，
> 但 `grep -v '//'` 匹配**整行** ⇒ `GLuint m_fb = 0;   // 旧帧缓冲` 这种**真违规**
> 被一起滤掉。`/tmp` 造样本实测：旧逻辑报 1 行（漏 GLuint 行），新逻辑报 2 行。
> **改法**：先 `sed -E 's|//.*$||'` 剥注释再匹配记号（纯注释行剥完变空，自然不匹配）。
> ⇒ **"零命中"这类否定性证据，必须先证明仪器在违规样本上会报红**（陷阱 20/79）。

**页面协议入口（B 阶段不得改名/改语义）**：7 个上下文属性
`appFacade` / `ActionRouter` / `searchResults` / `objectInfo` / `ShortcutModel` /
`HelpModel` / `ErrorModel`，外加 `qmlRegisterType` 注册的 `SkyViewport`。

**残余**：`SkyViewport` 是**旧天空的替身入口**；计划二会用原生 Vulkan 天空替换它。
替换时**页面协议（属性名与语义）不得变** —— 这是契约第 7 条对本条的反向约束。

---

## 契约 2｜可复现旧 GL 对照应用与固定场景、设置、资源清单

| 要素 | 现状 | 证据 |
|---|---|---|
| **旧 GL 对照应用 ①（完整宿主）** | ✅ 同一 CMake 工程产出 `stellarium`（QWidget + 旧 GL 全功能版）；进程内自检 `STELA3_CHECK` 8 条 | `tools/t16-verify.sh` 起的 `S3` 段；`build-release/src/Release/stellarium` |
| **旧 GL 对照应用 ②（可计量产帧）** | ✅ `LegacyLongRun`（`stelt9`）：引擎 GL 出图 + 读回，逐帧 CSV（`render_ms`/`readback_ms`/`total_ms`），**无 QML 消费者** | `docs/evidence/2026-09-22-stelt9-longrun-30min.txt` + `-baseline-frames.csv.gz` |
| **固定场景** | ⚠️ **部分**：帧类判据的固定参数已明确 —— 离屏 **1280×720**、`devicePixelRatio=1.0`、`simRate=0.02`（长跑）；显示/高 DPI 类判据**钉固定夜 JD 2461000.4167**（T42 收口轮，记原值还原） | `SkyLongRun.hpp` 选项表；`DisplayCheck.cpp:353`、`HiDpiCheck.cpp:289` |
| **固定设置** | ✅ 个人版配置目录隔离（T36，落点 `<引擎默认用户目录>-quick`）+ 播种/兜底；全部行为开关走 `STELQUICK_*` 环境变量（**总表见 `PROGRESS_SNAPSHOT §4`**） | `docs/T36_CONFIG_ISOLATION.zh_CN.md` |
| **资源清单** | ✅ `docs/vulkan/source-manifest.json`（九个资源目录的逐文件 sha256）+ `tools/source-snapshot.mjs verify` | `docs/vulkan/source-manifest.json` |

**残余（诚实标注）**：
1. **没有"一条命令产出固定场景快照"的单一入口。** 现状是"参数分散在各自的判据/脚本里，
   每个判据自己钉参数"。要交给计划二的**场景快照**应当是一份机器可读的参数文件
   （尺寸/DPR/仿真速率/JD/地点/FOV/投影/开关集合）+ 一张参考帧图。
   **本轮不新建**（避免在 A6 末端引入未验证的新设施），列为**计划二开工前的第一件事**。
2. 固定场景目前是**按判据族各钉一份**，不是全局唯一 —— 这点必须写明，
   否则计划二会误以为存在一个"全局默认场景"。

---

## 契约 3｜帧格式文档（逻辑/物理尺寸、原点/方向、颜色空间、透明度、帧/状态/尺寸世代标识）

✅ **完成** ⇒ 独立交付物 **`docs/A6_FRAME_FORMAT.zh_CN.md`**。

该文档把原本散落在源码注释与构建记录里的事实集中成可交付物，逐条给出
**权威源（源码行）+ 实测证据（判据/证据文件）**，并列出**计划二不得改的 4 项冻结清单**。

其中最关键、最容易搞错的一条：**OpenGL 原点在左下、帧契约要求左上，
本项目在 `LegacySkyHost` 里真的做了行序重排**（不是只改方向标记），
消费侧因此使用 `NoTransform`。两侧口径必须同时改，否则画面上下颠倒。

---

## 契约 4｜统一命令路由、唯一仿真时钟、明确 QObject 归属与可复制状态结构

| 要求 | 实现 | 判据 |
|---|---|---|
| **统一命令路由** | `ActionRouter`：QML/快捷键/工具栏**单点**入口；产品侧**零 C++ 语义复制**（按钮 → `trigger(<引擎 action id>)` → 引擎透传） | `ACTIONCHECK` 11 条（`STELQUICK_ACTION_CHECK`）；`docs/T34_TOOLBAR.zh_CN.md` |
| **唯一仿真时钟** | `StelClockController`：真源 `m_jd`，唯一写入 `jumpTo()`，速率留引擎 `timeSpeed`；`getJD()` 是**上一帧快照** | `CLOCKCHECK` 12 条；`docs/T16_SINGLE_SIM_CLOCK.zh_CN.md` |
| **明确 QObject 归属** | 模型只持 `stableId`（`getType()+":"+getID()`），**不持引擎裸指针**；QML 不遍历模块单例 | 编码规范 §5 硬性禁区；`docs/T17_SEARCH_OBJECT_MODELS.zh_CN.md` |
| **可复制状态结构** | 时间/地点/显示/夜视等的状态面均为**可复制值**（`Q_PROPERTY` / 只读数据面），跨线程按不可变快照传递 | `FrameMailbox`（不可变帧快照）；`ErrorModel`/`HelpModel`（只读数据面） |

---

## 契约 5｜功能范围和旧 GUI/action 处理台账；计划二不得顺手重写 UI

台账见独立交付物 **`docs/A6_ACTION_LEDGER.zh_CN.md`**（一次性把三处分散的记录合并）：
T15 注册表盘点（505 动作 / 15 分组）→ T34 工具栏（12 显示开关 + 6 导航）→
T41 帮助/版本/许可证（接管 6 个窗口动作）→ T42 未支持项（**接管 + 弹「未支持」提示** 4 个）。

**一句话口径**（三处判定合并后的结论）：

- **已接 QML**：注册表里**所有**动作都可通过 `ActionRouter::trigger(id)` 触发（引擎透传）；
  其中 **32 个**有专属 QML 界面（导航 6 + 显示开关 12 + 接管页 6 + 提示 4 + 参数面若干）。
- **刻意未接界面、但已接管以防静默弹窗**：`F2 设置` / `F10 天文计算` / `F12 脚本控制台` /
  `⌥B 观测列表` —— 按下后弹**明确的「未支持」提示**，**不静默打开旧 QWidget 对话框**。
- **未支持项清单**：7 条（4 个老窗口 + 插件设置 + legacy 手势 10 条 + T39 两控件），
  在「状态与错误」页有清单与说明。

**「计划二不得顺手重写 UI」的落实方式**：把上面这份台账作为**改动前置**——
任何 UI 改动都要先在台账上登记"动的是哪一类"，未登记即视为越界。

---

## 契约 6｜分开的性能记录：原 GL 基线 / A 阶段组合 / 桥接独立成本

✅ **完成** ⇒ 独立交付物 **`docs/A6_FRAME_BRIDGE_STATS.zh_CN.md`**
（由 `tools/a6-frame-bridge-report.sh` 从原始 log + CSV 机械复算生成，可随二进制重跑刷新）。

三分账定义：
- **原 GL 基线**：`LegacyLongRun`（引擎 GL 出图 + 读回，无 QML 消费者）
- **A 阶段组合**：`SkyLongRun`（producer=engine；引擎 GL + 帧桥 + QML Vulkan 上屏）
- **桥接独立成本**：GPU→CPU `readback` + CPU→GPU `upload`（这两笔在原版直接上屏时**不存在**）

---

## 契约 7｜预留 renderer capabilities/status 通道，B 阶段不改页面协议

| 分句 | 现状 | 证据 |
|---|---|---|
| **status 通道** | ✅ `BackendInfo`（`src/ui/quick/BackendInfo.hpp`）以 `Q_PROPERTY` 暴露：`deviceName` / `vulkanVersion` / `driverVersion` / `portabilityDriver` / `runtimeApiName` / `backendOk`，加**运行期帧桥诊断**（`framesPublished`/`framesDropped`/`completeSlots`/`slotCapacity`/`readersHeld`/`latestFrameAgeMs`/`bytesPerFrame`/`sizeGeneration`） | `DiagnosticPage.qml` 消费；`HIDPICHECK` HP-11「诊断数据面健康」用抓帧 `FrameSample` **独立验算** `bytesPerFrame` |
| **capabilities 通道** | ⚠️ **部分**：当前暴露的是**后端事实 + 运行期状态**，**没有**"能力协商"层（如某扩展/shader 特性是否可用） | 诚实标注：契约要的是"**通道**"（预留），通道已由 `BackendInfo` 占位 |
| **B 阶段不改页面协议** | ✅ 落实方式 = **只加新属性，不改既有属性的名字与语义**。`BackendInfo` 的属性全部 `CONSTANT` 或 `NOTIFY`，计划二增补能力项时**新增** `Q_PROPERTY` 即可 | 见 `docs/A6_INTERFACE_FREEZE.zh_CN.md`（生成的公开面清单） |

**残余**：`capabilities` 的**具体项**未定（要等计划二知道"需要知道什么"）——
这是**有意留白**，不是遗漏。契约要求的是通道，不是内容；内容随计划二增补。

---

## 7 项核对总结

| # | 契约 | 判定 | 交付物 / 残余 |
|---|---|---|---|
| 1 | 单一 SkyViewport + 稳定 AppFacade + QML 无 GL/Vulkan | ✅ | grep 门禁；`A6_INTERFACE_FREEZE` §1/§5 |
| 2 | 可复现对照应用 + 固定场景/设置/资源清单 | ⚠️ **部分** | 对照应用/设置/资源清单齐；**缺"单命令固定场景快照"** ⇒ 列为计划二开工第一件事 |
| 3 | 帧格式文档 | ✅ | `A6_FRAME_FORMAT.zh_CN.md` |
| 4 | 统一命令路由 / 唯一仿真时钟 / QObject 归属 | ✅ | `ACTIONCHECK` / `CLOCKCHECK` / `T17` |
| 5 | 功能范围与旧 GUI/action 台账 | ✅ | `A6_ACTION_LEDGER.zh_CN.md` |
| 6 | 性能三分账 | ✅ | `A6_FRAME_BRIDGE_STATS.zh_CN.md` |
| 7 | renderer capabilities/status 通道 | ⚠️ **部分**（通道齐、内容有意留白） | `BackendInfo`；增补只加新属性 |

**结论**：7 项中 **5 项完全达成**，**2 项部分达成且残余已明写**（第 2 条的"单命令固定场景快照"、
第 7 条的"capabilities 具体项"）。两条残余都**不阻塞**计划二开工，且都指定了归属与时机。
