# 进度总览（截至 2026-10-01，**T47 收口 = A-1.0 达成** + **尾巴清零** 盘点）

> 口径基线：T34 收口提交 `9b04cdd` + T35 提交 `d68cc32` + W 支线 + **T36（A5 第一项）**
> + **T37（A5 第二项：夜视闭环）** + **T38（显示参数）** + **T39（高 DPI + 渲染诊断）**
> + **T40（快捷键编辑）** + **T41（帮助/版本/许可证）** + **T42（状态/错误页，A5 收口）**
> + **A6 回归与交接（T43–T47）**。
> 上一版快照基线是 `9b04cdd`（T34 收口）；T33 见 `docs/T33_LOCATION_PAGE.zh_CN.md`，
> **T34 见 `docs/T34_TOOLBAR.zh_CN.md`**，**T35 见 `docs/T35_TIMELINK.zh_CN.md`**，
> **W-T34/W-T35 跨平台复验见 `docs/WT35_WINDOWS_RECHECK.zh_CN.md`**，
> **W-T37..W-T41 跨平台复验见 `docs/WT37_41_WINDOWS_VERIFY.zh_CN.md`**（2026-10-01 尾巴攻坚），
> **T36 配置目录隔离见 `docs/T36_CONFIG_ISOLATION.zh_CN.md`**，
> **T37 夜视闭环见 `docs/T37_NIGHTMODE.zh_CN.md`**，
> **T38 显示参数见 `docs/T38_DISPLAY.zh_CN.md`**，
> **T39 高 DPI + 渲染诊断见 `docs/T39_HIDPI.zh_CN.md`**，
> **T40 快捷键编辑见 `docs/T40_SHORTCUTS.zh_CN.md`**，
> **T41 帮助/版本/许可证见 `docs/T41_HELP.zh_CN.md`**，
> **T42 状态/错误页见 `docs/T42_ERRORS.zh_CN.md`**，
> **A6 六件套见 `docs/A6_*.zh_CN.md`**（帧桥统计 / 帧格式 / 交接冻结 / 动作台账 /
> 出口终审 / 资源打包 / 接口冻结 + `T44_LIFECYCLE` / `T45_CONFIG_SAFETY`）。
> 环境口径与 T17–T34 一致、**刻意不换**：macOS + Metal（`VK_DRIVER_FILES=MoltenVK`、
> `QT_VULKAN_LIB`、`STELQUICK_GRAPHICS_API=metal`）；Windows 侧原生 Vulkan。
> 本文是**单页快照**，细则看各 `docs/T*.zh_CN.md` 与 `docs/evidence/`。

---

## 1. 里程碑总表

| 里程碑 | 内容 | 状态 |
|---|---|---|
| A0 基线与独立工程 | git 基线、旧版 Release 构建、Qt 补丁版本锁定 | ✅ 完成 |
| A1 Vulkan QML 宿主 | 显式后端、诊断页、后端校验（失败不静默回退） | ✅ 完成 |
| A2 旧天空帧桥 | 静态 12/12 + 动态 7/7 + 30min 长跑 | ✅ 完成 |
| A3 业务接口与输入路由 | AppFacade / ActionRouter / 模型 / 单一仿真时钟 / 键位路由 | ✅ 完成（T14–T17） |
| **A4 核心交互页面（A-alpha）** | 工具栏、搜索、天体信息、时间、**地点**、拖动缩放、焦点管理 | ✅ **7/7 完成**（**T34 收口**，见 §2） |
| **A5 设置与完整个人版 UI** | A-1.0 全部页面、**配置迁移**、夜视、错误页 | ✅ **完成**（**T36** 配置隔离/播种 + **T37** 夜视闭环 + **T38** 显示参数 + **T39** 高 DPI/渲染诊断 + **T40** 快捷键编辑 + **T41** 帮助/版本/许可证 + **T42** 状态/错误页 + "未支持项"清单，见 §6） |
| **A6 回归与交接** | 全量回归、帧桥统计、生命周期、配置安全、资源打包、接口冻结、出口终审 | ✅ **完成**（**T43** 帧桥统计 + **T44** 生命周期 + **T45** 配置安全 + **T46** 接口冻结 + **T47** 全量回归 + A-1.0 出口 7 条终审，见 §6；**达成 A-1.0**） |
| A7 平台适配（独立包） | 鸿蒙真机探测 / 打包分发 / Win·Linux 顺手验证 | ⬜ 前置真机；**W-T37..W-T41 Windows 复验 ✅ 已完成**（见 §6 尾巴段） |

---

## 2. A4 逐项核对：**T34 后 `A-alpha` 出口第一条成立**

**判据出处**（`开妇相关文档/2026-09-17-03-软件测试文档` §8 A-alpha 出口，第一条）：

> `[ ]` 第 3 节标"必须"的 A-alpha 功能全部可操作（拖动/缩放/选中/跟踪、时间、**地点**、搜索、信息）

**范围出处**（`2026-09-17-01-…-development.zh_CN.md` §3 第一阶段 UI 范围）：
`观察地点、经纬度、高度` 的 A-alpha 列 = **必须**，说明列写明"先手工/本地列表；
设备定位权限不是桌面入口前置条件"。

| A4 范围项 | 状态 | 依据 |
|---|---|---|
| 搜索 | ✅ | T17 + T21（相关度排序）+ T24（拼音）+ T26（召回全量化） |
| 天体信息 | ✅ | T17 `ObjectInfoModel`（主键 = stableId，不持指针） |
| 时间 | ✅ | T19（×6 写入）+ T22（历法/MJD/速率/时区）+ T30（视觉层） |
| 拖动缩放 | ✅ | T25（滚轮）+ T27（鼠标拖拽/点击/右键）+ T28（捏合） |
| 焦点管理 | ✅ | T25（`inherits` 修守卫）+ T29（IME 组合态）+ T32（两段式 Esc） |
| **地点** | ✅ **T33** | **先探针后写 UI**（T24 翻译从未加载的同款风险）：地点库 33501 条可加载。`LocationPage.qml`（搜索地点 / 按坐标写入 / 当前地点卡片）+ `AppFacade` 地点写入面（**单点入口** `moveObserverTo` + 范围闸 + **回读验证**）+ `LOCATIONCHECK` **10 条**（含**绝对腿**「天极高度 = 观测者纬度」，Δ=0.000°） |
| **工具栏** | ✅ **T34** | `Toolbar.qml` **取代** A2 临时页切换器（导航 6 按钮 `objectName` 原样保留 + 12 个显示开关）｜命令路径**单点**：按钮 → `ActionRouter.trigger(<引擎 action id>)` → **引擎透传**（T34 产品侧**零 C++ 语义复制**）｜状态回读走 `displayTogglesRevision` token + `actionChecked()`（T15 铁律）｜判据 `TOOLBARCHECK` **12 条**（含 UI 点击腿、绑定重算腿、落点几何自证）｜探针 Q1 实测注册表 **505 动作 / 15 分组**、12/12 checkable、`getText()` 中文 |

**结论（T34 后）**：A4 的 7 项**全部完成** ⇒ 测试文档 §8 的 **A-alpha 出口第一条**
（"标'必须'的 A-alpha 功能全部可操作"）**成立**。此前"何时算达成"的口径
（T33 前"A4 主体完成、只差真实工具栏"）**到此结清**，不再有保留条款。

> 仍**不属于** A4 的部分（留 A5）：A-alpha 表里标"**基本开关 / 基础**"的项（星座线·名称、
> 网格、地景、大气；亮度·星等、投影、主题·夜视、高 DPI）。T34 已把其中 **12 个显示开关**
> 做成 QML 承载面（含"状态与引擎双向同步"），**T37** 补上主题·夜视（Qt Quick 侧效果层），
> **T38** 补上亮度·星等 / 视场 / 投影，**T39** 补上**高 DPI** —— ⇒ **该格至此清空**
> （「基础 | 必须」列里"No QML GUI"的项**已全部落地**）。
> 这些项归 A5 的设置页，**不在 A-alpha 出口第一条的"必须"清单里**。

---

## 3. 判据套件清单（当前真值）

| 套件 | 判据数（最后一号） | 触发 env | 说明 |
|---|---|---|---|
| `A2CHECK` | 12 探针逐像素 | `STELQUICK_A2_CHECK` | 静态图管线；macOS 走 Metal（验收目标 Vulkan 受上游缺陷阻塞） |
| `DYNCHECK` | 7（D1-C01..C07） | `STELQUICK_DYN_CHECK` | 双生产者：`STELQUICK_DYN_PRODUCER=engine\|test` |
| `ACTIONCHECK` | 12（AC-01..AC-11 + AC-12b 自净） | `STELQUICK_ACTION_CHECK` | 命令单点 + 速率贯通 + 幂等；AC-12b 保证注入动作翻回用户原值（TRAPS 112） |
| `CLOCKCHECK` | 12（ST-01..ST-12） | `STELQUICK_CLOCK_CHECK` | 单一仿真时钟 |
| `SEARCHCHECK` | 14（SRC-01..SRC-14） | `STELQUICK_SEARCH_CHECK` | 含纯逻辑腿 + 活引擎召回腿（1573 条全量池） |
| `LOCATECHECK` | 15（LOC-01..LOC-15） | `STELQUICK_LOCATE_CHECK` | 定位/跟踪 |
| `UICHECK`（定位页） | 10（UI-01..UI-10） | `STELQUICK_UI_CHECK` | 定位按钮/状态 |
| `TIMECHECK` | 21（TC-01..TC-21） | `STELQUICK_TIME_CHECK` | 时间写入 + 时区 + 速率真源 |
| `TIMEUICHECK` | 27（UI-01..UI-27） | `STELQUICK_TIME_UI_CHECK` | 含 T30 七类视觉判据（零尺寸/被裁/重叠/不可见/文本空/点不到/对比度） |
| `RETURNUICHECK` | 11（RT-01..RT-11） | `STELQUICK_RETURN_UI_CHECK` | 返回环（页面切回 + 状态全保留） |
| `REPLAYCHECK` | 11（RP-01..RP-11） | `STELQUICK_REPLAY_CHECK` | **I-REP-02 固定流程回放 = A-alpha 出口测试** |
| `INTERACTCHECK` | 18（IT-01..IT-18） | `STELQUICK_INTERACT_UI_CHECK` | 键盘/焦点/IME/两段式 Esc；含**第三态 UNAVAILABLE**（T31） |
| `LOCATIONCHECK` | 10（LC-01..LC-08） | `STELQUICK_LOC_CHECK` | **T33** 地点写入面：范围纯谓词 / 读面 / 写入生效（成对）/ 时区联动（判别腿）/ **天极高度=纬度**（绝对腿）+ 三点差分 / 非法值无副作用 / not-found / 往返 / 计数。**五批正题均 `10/10`** |
| `TOOLBARCHECK` | 12（TB-01..TB-12） | `STELQUICK_TOOL_CHECK` | **T34** 真实工具栏：写入腿（4 代表开关 trigger → **模块 getter** 翻转）/ 往返复原 / `actionToggled` 信号腿 / revision 订阅腿 / not-found 负控 / **判别负控**（registry 命令不碰 revision）/ UI 腿（12 按钮全找到 ∧ 态一致）/ **真实鼠标点击腿** / **绑定重算腿**（双采样）。**五批正题均 `12/12`** |
| （探针）`LOCPROBE` | 只报读数、**不打 PASS** | `STELQUICK_LOC_PROBE` | T33-A 数据面探针（地点库规模 / `locationForString` 语义 / `isValid` 边界 / 时区联动） |
| （探针）`TOOLBARPROBE` | 只报读数、**不打 PASS** | `STELQUICK_TOOL_PROBE` | **T34-A** 命令面探针（注册表规模 505/15 / 12 候选逐项 checkable·checked·text·key / trigger→getter 翻转 / `actionToggled` 观测 / revision 读数） |
| `TIMELINKCHECK` | 7（TL-01..TL-07） | `STELQUICK_TIMELINK_CHECK` | **T35** 仿真时间链路定性：读面同源 / **链路自洽**（ΔJD==窗口×rate×scale）/ rate 多档线性 / 窗口线性（**真实墙钟**，非按帧固定步长）/ 冻结不补 / 速率阶梯跟变 / **恒星时绝对腿**（ΔLST==ΔJD×360.9856° + 下游 `j2000ToAltAz` 落点重算）。**五批正题均 `7/7`** |
| `CONFIGCHECK` | 8（CFG-01..CFG-08） | `STELQUICK_CONFIG_CHECK` | **T36** 个人版配置目录隔离：隔离读数自洽 / 目录独立 / **配置写侧落点（`QSettings::fileName()`，最强）** / 日志落点 / 配置种子完整（双叉）/ 写侧不触原目录（**三半成对**）/ 产品路径落点（`ActionRouter.trigger` 真路径）/ 播种清单完整。**不启帧泵、无就绪门**（全同步事实）。**三场景正题均 `8/8`**（首次 / 非首次 / **全新机器**） |
| `NIGHTCHECK` | 6（NC-01..NC-05） | `STELQUICK_NIGHT_CHECK` | **T37** 夜视闭环：状态面（**引擎 getter 独立回读**）/ 噪声底门（**低于噪声容差**，非逐位相同；超限 ⇒ 整套 UNAVAILABLE）/ 效果面**成对**（① 上游低于噪声容差 —— 大气已关 ⇒ 引擎夜视反应成 no-op；② 下游视口 ≥90% ∧ 均值>1.0，**伪证守门=①**）/ 判别对照（承重）/ 往返复原（无全帧级红移；异常帧 ⇒ INCONCLUSIVE）。**负控**：`STELQUICK_NIGHT_EFFECT_OFF=1` ⇒ 恰好红 `[NC-03②]`。**正题 3/3 `6/6`、负控 3/3 恰中** |
| （探针）`TIMELINKPROBE` | 只报读数、**不打 PASS** | `STELQUICK_TIMELINK_PROBE` | **T35-A** 时间链路探针（Q1 三个读面同源 / Q2·Q3 窗口原始读数 / Q4 阶梯台账 0.1→1→10→100 / Q5·Q5b 冻结与恢复 / Q6b 恒星时恒等式 / Q6c 下游重算 / Q6d **视线随 JD 转的机理核验**） |
| （探针）`NIGHTPROBE` | 只报读数、**不打 PASS** | `STELQUICK_NIGHT_PROBE` | **T37-A** 夜视链路数据面探针（引擎状态面 / 宿主链面 `property("nightMode")` / 帧静定噪声底 / 上游·下游两条独立读回路径 OFF↔ON 差异 / 判别性对照）。⚠️ 探针头条"上游逐位相同"事后被证伪（等待挂读步，TRAPS 69）—— 但**帧号留痕**才使翻案成为可能 |
| `DISPLAYCHECK` | **14**（DP-00..DP-11） | `STELQUICK_DISPLAY_CHECK` | **T38** 显示参数：噪声底门 / 状态面往返（**引擎 getter 独立回读**）/ **范围闸**（引擎 setter 不夹取 ⇒ 闸门在 façade）/ 投影 12 key 往返 + **maxFov 活属性**（随投影取到 6 个不同值）/ **投影白名单闸** / `lastDisplayRefusal` 必须是 Q_PROPERTY / UI 控件齐备（**视觉树递归**，陷阱 45）/ 绑定腿 / 视场生效（成对）+ **判别对照** / 投影生效（成对）/ **DP-10a 静置对照 + DP-10b LOW↔HIGH 方向量** / 复原。**两组负控**：`STELQUICK_DISPLAY_GATE_OFF=1` ⇒ 恰红 `[DP-02,DP-04]`；`STELQUICK_DISPLAY_FWD_OFF=1` ⇒ 恰红 `[DP-07]`。**正题 3/3 `14/14`、负控各 2/2 恰中** |
| （探针）`DISPLAYPROBE` | 只报读数、**不打 PASS** | `STELQUICK_DISPLAY_PROBE` | **T38-A** 显示参数命令面探针（初值台账 / 四个 setter 的**夹取语义** / 星等语义四问 / 步进动作 / `setFov` 夹取边界 / 12 投影 key 往返 + maxFov 随投影 / **非法 key 落点** / NOTIFY 静置增量）。7 条实测结论见 `docs/T38_DISPLAY.zh_CN.md` §2 |
| `HIDPICHECK` | **12**（HP-00..HP-11） | `STELQUICK_HIDPI_CHECK` | **T39** 高 DPI + 渲染诊断：噪声底门（环境门）/ 引擎面往返（**引擎 getter 独立回读**）/ **三处范围闸**（`screenFontSize` [5,50]、`guiFontSize` [7,50]、`screenButtonScale` [50,200] 产品自定；各含上沿+下沿）/ **派生量算术验算** / UI 控件齐备（**视觉树递归**）/ 绑定腿 / **交互后绑定仍活**（**真实点击**验 `onMoved` ⇒ 再引擎侧改 ⇒ UI 仍跟随）/ **字号帧效应**（成对 + 判别对照）/ **诊断数据面健康**（`bytesPerFrame` 用抓帧 `FrameSample` **独立验算**）/ 复原。**两组负控**：`STELQUICK_DISPLAY_GATE_OFF=1` ⇒ 恰红 `[HP-02,HP-03,HP-04]`（9/12）；`STELQUICK_DISPLAY_FWD_OFF=1` ⇒ 恰红 `[HP-07,HP-08]`（10/12）。**正题 3/3 `12/12`、负控各 2/2 恰中** |
| （探针）`HIDPIPROBE` | 只报读数、**不打 PASS** | `STELQUICK_HIDPI_PROBE` | **T39-A** 高 DPI + 渲染诊断数据面探针（三个 setter 的**夹取语义** / 派生量独立验算 / config 来源 / 🔴 `getGuiFontSize()` 读的是**全局字体** / 🔴 `setGuiFontSize` 对 QML 视觉树 **325 项零跟随**〔**三重仪器正控**〕/ `screenFontSize` **帧效应 4.054%** + 还原逐位复原 / `FrameMailbox::Stats` 全字段 / NOTIFY 静置增量）。7 条实测结论见 `docs/T39_HIDPI.zh_CN.md` §2 |
| `SHORTCUTCHECK` | **15 个 id / 14 条**（SC-01..SC-14，05 拆 a/b） | `STELQUICK_SHORTCUT_CHECK` | **T40** 快捷键编辑：表完整性（模型 505 vs 引擎 505） / 行数据抽样（逐项 vs 引擎）/ `customized` 口径（全表 505 行 vs `conf->contains`）/ **键名生成单点**（含**别名判别对照** `PageUp` 直构=空序列）/ **改键写引擎**（引擎 getter 独立回读）/ **改键落盘**（**另开 `QSettings` 实例**读磁盘 + 按构造解析路径取首段 + **比序列语义**）/ **不写穿**（整本指纹跳过 `shortcuts/` 差异=0）/ 冲突检测（制造 ⇄ **判别消解**）/ 单恢复不冲别人（**两个不同动作**）/ 空串移除（磁盘 `"" ""` 形态 + 复刻构造解析）/ **非法键名闸**（`PageUp` 被拒 + 引擎未变 + 状态文本非空）/ 全部恢复 / UI 控件齐备（**视觉树递归**，陷阱 45）/ **UI 交互端到端**（真实点击导航 → 真实点击主键按钮 → **注入真实 `QKeyEvent`** ⇒ 引擎 + UI 按钮文字双确认）/ 复原（整本指纹=0）。**两组负控**：`STELQUICK_SHORTCUT_WRITE_OFF=1` ⇒ 红 `[SC-05a,SC-05b,SC-07,SC-08,SC-09,SC-11,SC-13]`（8/15）；`STELQUICK_SHORTCUT_SAVE_OFF=1` ⇒ 红 `[SC-05b,SC-09,SC-11]`（12/15）。**正题 3/3 `15/15`、负控各 3/3 命中且两腿正交（`A\B`=引擎写入生效组）** |
| （探针）`SHORTCUTPROBE` | 只报读数、**不打 PASS** | `STELQUICK_SHORTCUT_PROBE` | **T40-A** 快捷键命令面探针（Q1 `QKeySequence` 往返恒等 14/15 + **键名别名对照** + 组合个数 / Q2 `setShortcut` 即时性 + **信号面**（只发 `changed`）+ `routeKey` 真触发 / Q3·Q3b·Q4 **落盘 + 不写穿 + 多键序列规范化** / Q5 空串移除 / Q6 重启等价（**比序列语义**）/ Q7 单恢复波及面 / Q8 出厂无重复键 / Q9 `actionsEnabled` 门只挡 `pushKey` / Q10 注册表 505·15 / Q11 平台键名格式 / Q12 还原）。**12 条实测结论见 `docs/T40_SHORTCUTS.zh_CN.md` §2** |
| `HELPCHECK` | **17**（HC-01..HC-17） | `STELQUICK_HELP_CHECK` | **T41** 帮助/版本/许可证：数据面自洽（手势表 23 行 scope 计数、贡献者去重+排序）/ **GPL 编进 qrc**（资源存在 + 内容 md5 对磁盘） / **许可证加载**（标题 + `Version 2`）/ 版本面（**走 `StelUtils::getApplicationVersion()`**，⚠️ `QCoreApplication::applicationVersion()` 是 `1.0.0`）/ 系统行（API/平台/编译器非空）/ **接管表**（6 个 == 预期 + F2/F10/F12 **刻意未接管**对照）/ **接管生效（trigger）**（`triggered=1` ∧ `hostActionRequested Δ=1` ∧ `index=7` ∧ **QWidget Δ=0**）+ **判别对照**（撤销接管 ⇒ QWidget **Δ=46** ⇒ 正题 Δ=0 是接管的功劳）/ **键盘路径（`routeKey`）**（同查表，T41 陷阱 88 的核心）/ 帮助页控件（手势行 23/23｜外链行 7/7｜可见 legacy 标记 10/10 —— **视觉树递归**，陷阱 45）/ 跳转互跳 / 关于页控件（版本行 9/9｜系统行 6/6｜贡献者 delegate 8 条）/ **许可证端到端**（Dialog 可见 ∧ UI 文本 17992 字符 == 数据面）/ 复原（配置指纹=0）。**两组负控**：`STELQUICK_HELP_TAKEOVER_OFF=1` ⇒ 恰红 `[HC-14,HC-15,HC-16]`（14/17）；`STELQUICK_HELP_LICENSE_OFF=1` ⇒ 恰红 `[HC-04,HC-13]`（15/17）。**正题 3/3 `17/17`、负控各 3/3 命中且两腿正交（`A\B`=接管域、`B\A`=许可证域）** |
| （探针）`HELPPROBE` | 只报读数、**不打 PASS** | `STELQUICK_HELP_PROBE` | **T41-A** 帮助/版本/许可证探针（93 行读数：`src/gui/` 是否编进来 / 8 个窗口动作是否在册 / F1 `trigger()` 会不会弹老对话框 / 版本面五个来源 / `COPYING` 在不在 bundle 里 / 贡献者表规模与重复）。**推翻三处开工推断**——见陷阱 88/89 与 `docs/T41_HELP.zh_CN.md` §2 |
| `ERRORCHECK` | **A 组 15 / B 组 5** | `STELQUICK_ERROR_CHECK`（失败腿加 `STEL_USERDIR=/dev/null/xxx`） | **T42-C** 状态与错误页（**两条腿同一二进制**，失败腿用 `STEL_USERDIR` 造**真实**失败）：A 组 = 路径表 8 行/problem 0 / `-quick` 后缀 / cfg+log 就位 / 状态表无 error / 无错不报错 / 未支持 7 条 / 注册表交叉验算 4/4 / 接管表 10 / **4 动作 trigger ⇒ Δ0+提示+配置零变化** / **判别对照**（撤销接管 ⇒ Δ697 真弹）/ UI 行 4/8/7 / 按钮几何可达 / 剪贴板诊断 / **openPath 白名单闸（EC-15）** / 复原。B 组 = hasError / detail 与注入原因一致 / 错误块 UI / 引导行=error / 失败时路径仍可展示。**负控三腿**（实跑写死）：`ERROR_TAKEOVER_OFF` 恰红 `[EC-08,09,14]`、`ERROR_PATHS_OFF` 恰红 `[EC-01..04,11]`、`ERROR_UNSUPPORTED_OFF` 恰红 `[EC-06,07,11]`；A∩B=A∩C=∅、B∩C={EC-11}（子段可分）；⚠️ `ERROR_OPEN_OFF` **不是负控**（红不了 = 不承重）。正题 3/3 `15/15`、失败腿 2/2 `5/5`、负控各 2/2；**config 零污染门**（脚本全程 md5） |
| （探针）`ERRORPROBE` | 只报读数、**不打 PASS** | `STELQUICK_ERROR_PROBE` | **T42-A** 错误页探针（57 行读数：七目录事实 / 隔离运行期读数 / 关键文件 / 失败面 + `STEL_USERDIR` 可注入性 / 未接管动作 trigger 实测 / 未支持项真源 / 可操作性 / **Q9 净写入自证**）。**推翻 1**（老对话框写 `DialogSizes/*`）、**证实 1**（F10 Δ697 / ⌥B Δ58）、**发现 T41 遗漏 1**（⌥B 观测列表）——见 `docs/T42_ERRORS.zh_CN.md` §3 |
| `LIFECHECK` | **13**（LF-02a..f / LF-03a..d / LF-04a..c） | `STELQUICK_LIFECYCLE_CHECK` | **T44** 生命周期（P-LIF-02/03/04 桌面判据面）：恢复首帧 / 尺寸世代（同尺寸重设 Δ=0）/ 重建收敛 / 最小化稳态 `windowStates()` 必含 `WindowMinimized`（LF-02f 钉死 hide() 代理）。**负控三条实跑写死**：NOSIZE⇒{03a,04a}｜NOSETTLE⇒{02b,03a,03c,04a,04c}｜HIDE⇒{02f}。⚠️ 陷阱 102：`delayAfter` 时序写反 ⇒ 改名 `waitBefore` |
| `CFGHEALTHCHECK` | **12**（CH-01..CH-12） | `STELQUICK_CFGHEALTH_CHECK` | **T45** 配置健康：损坏五形态（截断/截尾/乱码/空洞/坏节名）全部 severity=3 + 引导修复（`.corrupt` 备份→重建）+ `none` sev=1 不误修 + 健康读**必须 `StelIniFormat`**（陷阱 103）。**负控四条写死**：ISOLATE_OFF(full@orig)⇒{CH-02,03,05,06}｜STATUS_OFF⇒{CH-03,04,05}｜REPAIR_OFF⇒rc=139（承重铁证）｜DRIFT⇒{CH-07} |

一键复跑：`tools/t16-verify.sh` … **`tools/t41-verify.sh`**（每个任务一个，用法 `all N`；
**`t33-verify.sh` 起含 `S3` 段**：旧宿主 `stellarium` 的进程内自检 `STELA3_CHECK`，
8 条判据、要求 `VERDICT=PASS`；**`t34-verify.sh` 含四组负控 + 探针 + 10 套件 + S3 + A2 + DYN**；
**`t35-verify.sh` 含三组负控 + 探针 + **11 套件**（多了 `toolbarcheck`）+ INTERACTCHECK + S3 + A2 + DYN**；
**`t36-verify.sh` 含 S1/S2 两场景 + 两组负控 + **12 套件**（多了 `timelinkcheck`）
+ INTERACTCHECK + A2 + DYN 双路；⑤ 收尾做"**合流形态**全程原目录零改动"核对，
⑥ **把 S3 旧宿主挪到核对之后单独跑**（写原版目录是它应有的行为，见 §6 T36 血泪②）**，
`core N` 段即含以上全部）；**`t37-verify.sh` 含正题×3 + 负控×3 + **13 套件**（多了 `nightcheck`）**；
**`t38-verify.sh` 含正题×3 + **两组负控×2** + **14 套件**（多了 `displaycheck`）+ INTERACTCHECK + S3 + A2 + DYN 双路**；
**`t39-verify.sh` 含正题×3 + **两组负控×2** + **15 套件** + INTERACTCHECK + S3 + A2 + DYN 双路**
（⚠️ T39 把 **T38 的 `displaycheck`** 纳入回归：T39 就在 `AppFacade` 显示参数段落里加东西、
同一个 `ensureDisplayParamsForwarding()` 末尾补连接 ⇒ 它是最紧的**最近邻**）；
**`t40-verify.sh` 含正题×3 + **两组负控×3** + **16 套件**（多了 `hidpicheck`）
+ INTERACTCHECK + S3 + A2 + DYN 双路，并多一条 **两腿正交定位**断言
（`A\B` = 引擎写入生效组、`B\A` 应为空 ⇒ 红项集合不只是"不同"，还指向不同的腿）**
（⚠️ T40 把 **T39 的 `hidpicheck`** 纳入回归：T40 在 `main.cpp` 自检装配段落插了一段、
且动了 `Toolbar.qml` ⇒ 必须证明 T39 那 12 条没被碰坏）；
**`t41-verify.sh` 含正题×3 + **两组负控×3** + **17 套件**（多了 `shortcutcheck` ——
T41 动了 `ActionRouter::routeKey()`，正是 T40 SC-14 交互腿的路径）
+ INTERACTCHECK + S3 + A2 + DYN 双路，并含同款 **两腿正交定位**断言
（`A\B` = **接管域**、`B\A` = **许可证域**，应无交集）**。
（⚠️ T41 把 **T40 的 `shortcutcheck`** 纳入回归：T41 在 `main.cpp` 自检装配段落又插了一段、
动了 `ActionRouter`（新增 `routeKey` 查表分支）与 `Toolbar.qml` ⇒ 必须证明 T40 那 14 条没被碰坏）。
Windows 侧 `tools/windows/wt29…wt33-*.ps1`
（`wt33-launch.ps1` 是给 `schtasks /it` 用的启动器，负责注入 `T33_PROBE_EXPECT`）。

**A6 一键收口 = `tools/a6-verify.sh`**（2026-10-01 起）：
`all`（T43 短窗 + T44 生命周期×2 + T45 形态矩阵×2 + 四条负控 + T46 冻结校验 +
**24 套件回归** + INTERACT + A2 + DYN 双路 + S3 旧形态 + config 零污染门 + 计划任务残留扫描）；
`longrun`（帧桥全量长跑 900+1800s，**独占环境 45 分钟**，与其它 target 互斥）；
`t43` / `t44 2` / `t45 2` / `t46` / `regress` 分段可跑。
配套 `tools/a6-lifecycle-100.sh`（P-LIF-01 进程级 ×100 ONESHOT）、
`tools/a6-interface-freeze.sh`（`--check` = 冻结校验，清单 = `docs/A6_INTERFACE_FREEZE.zh_CN.md`）、
`tools/a6-cfg-inject.sh`（配置形态注入器）。

---

## 4. 构建与环境口径

- **macOS 构建目录 = `build-release`（根构建）**，产物
  `build-release/src/ui/stelQuickUI.app/Contents/MacOS/stelQuickUI`。
  `build-ui` 是**独立工程形态**（无 `STELQUICK_HAS_ENGINE`），编它必失败。
  判别：`grep CMAKE_HOME_DIRECTORY <dir>/CMakeCache.txt`。
- `cmake` 不在沙箱 PATH，用 `/opt/homebrew/bin/cmake`。
- Windows：`tools/windows/wt32-{pull,build,suites}.ps1`，长跑走 `schtasks /it`；
  `/sc once` 跑完**必须 `/delete`**。
- ⚠️ **macOS 链接产物非位级可复现**（同源 relink 字节数相同、`LC_UUID` 重掷 ⇒ md5 必不同）
  ⇒ 判"两版是否同源"**看源文件 `md5 -q`**。
- ⚠️ 那台 Windows **连不上 GitHub**（`git fetch` → `Connection was reset`）⇒ 批跑 SUMMARY 里
  的 `repo HEAD` **会撒谎**；身份靠 manifest 的 **`SRC <file> md5`** 三行自证。

---

## 5. 未洗成 PASS 的账（环境/线索，逐条在案）

| 项 | 现状 | 定性 |
|---|---|---|
| `LOC-04 (b)` 帧延迟量化 | macOS `0.5826° > 0.50°` 红（回归里唯一一项） | 三组对照（B T32 全量 / C 仅回退两 QML / **D 全部回退到 T31 源**）**同分布** ⇒ 与 T32 无因果。机理 = 采样点在"跳变后 0ms"⇒ 残余角 ≈ 一次事件循环内的天球转角（≈30°/s ⇒ 一帧≈0.3°），读数**双峰**（0.05 / 0.32），阈值无余量 |
| 时间链路脱钩 | ✅ **已结清（T35，2026-09-30）** | **不是缺陷、也不是"设计需说明"——是测量口径问题**。同刻量齐三个量后 `ΔJD == 真实窗口 × rate × scale` 成立（TL-01 偏差 **0.19%..1.40%**／5 跑，恒星时绝对腿残差 **+0.0000°**）。三个数各有出处：①`getTimeRate()` 单位是 **JDay/sec**（`StelCore.hpp:595`），README 的 `× JD_SECOND` 是凭空引入的 1/86400；②`engineRate=10` 是**套件自己的 IT-05 注入 L 键**（`increaseTimeSpeed()`）抬上去的**移动靶**；③"400ms"是相位**名义** delay，从未与 ΔJD 同刻测过。⇒ **时间类判据口径可信**。见 `docs/T35_TIMELINK.zh_CN.md` |
| 捏合后首击失灵 | `QQuickPinchHandler` native 分支不清理 `currentPoints` | T28 发现；真实用户不在同点连做两事，**未定性为缺陷** |
| DYN 显示侧停摆 | 环境敏感，与 T18 无关（替身路径同样失败） | 倾向"窗口未被暴露"；`caffeinate -dims/-dimsu` **已试过无效，别再试** |
| 上游引擎缺陷（记录在案不改） | `listMatchingObjects` 不去重；聚合层 `std::sort` 抹掉模块级相关性 | 本层已各自绕过；将来报上游时是素材 |

---

## 6. 下一步候选（按优先级）

### ~~T33 地点页~~ —— ✅ **已完成**（`docs/T33_LOCATION_PAGE.zh_CN.md`）

按"**先探针、再写 UI**"落地：`LOCPROBE` 先证数据面（地点库 33501 条 / 193 区域 / 496 时区名
可加载、且**不依赖 cwd**），再写 `LocationPage.qml` + `AppFacade` 地点写入面，
判据 `LOCATIONCHECK` **10 条**。**判据设计比原计划更强**：原计划打算用"地平高度角 / 时角"，
实际落到**天文学恒等式「北天极高度 = 观测者纬度」** —— 用**当日天极**（不是 Polaris）
作仪器，读数与恒星时、经度**无关**，实测 Δ = **0.000°**，判别力 ≈ 18 倍。

⚠️ 本轮四条新血泪已入 `TRAPS.md`：① 步骤表 `delayAfter` 是"跑完**本步**之后"的等待
（写成 0 ⇒ 回读步与写入步同事件循环 ⇒ **竞态**，间歇红）；② **Polaris 偏离天极 0.74°、
抖动峰峰值 ~1.5°** ⇒ 分辨不出"巴黎↔阿布维尔"这 1.25° 的差，**不能当"天极"仪器**；
③ **对照量必须换来源**（见下）；④ **判据隐含依赖用户配置**（见下）。
另收紧了三条**仪器口径**：**负控若复现的是竞态，不能要求"每次都红"**；
**"承担修复的机制"的内部计数不能当判据**；**md5 与"源码是否变化"无因果（两个方向都被打掉）**。

**定稿轮 run6（mac）`FAILED=0` 全绿**，新增 **S3 旧宿主自检段落**（`8/8 PASS`）与**前提自证**断言。

🔴 **Windows 复验（T33-D）完成，并逼出本轮最大发现**：Windows 那台 `config.ini` 有
`localization/time_zone = Asia/Shanghai` ⇒ 引擎启动即 `flagUseCTZ=true`
（`StelCore.cpp:240-243`）⇒ `setObserver` 的时区联动**按设计跳过** ⇒ 正题恒 `9/10`
（`LC-03a` **假红**）而 `LC-03b`（判别腿"时区**不变**"）**假绿**。
**判据原先隐含依赖这件用户配置** ⇒ 已改为**判据自己读→强制→恢复前提**并在日志打印，
跑批脚本**断言这一行存在**。修复后 Windows 整批**全绿**，负控 C/D 红项与 mac **逐项一致**
（修复前唯一差异每次都是"恰好多一条 `LC-03a`"⇒ **反证根因单一**）。
⚠️ 唯一遗留：**负控 A（竞态复现）在 Windows 五次都没复现** ⇒ 记 `INCONCLUSIVE`
（**不洗成 PASS 也不判红**），"门承重"的跨平台证据由 **C/D** 提供。**已移交。**

🔴 最要命的一次**假绿**：LC-04 的对照量原取 `facade->locationLatitude()`，与
`core->getCurrentLocation()` **同源** ⇒ 写不进去时两边一起停在旧地点、**自洽通过**
（负控 D 下仍绿）。改取**地点库目录纬度**后才真红（Paris |Δ|=17.386°）。
**教训**：对照量必须**换来源**。

### T34 真实工具栏 + 显示开关 —— ✅ **已完成**（`docs/T34_TOOLBAR.zh_CN.md`）

`MainWindow.qml` 的 A2 临时页切换器**已删除**（注释里承诺的那一句"届时删除本行"兑现），
换成 `Toolbar.qml`：**第一行**导航（6 个按钮 `objectName` 一字未动，回归判据照旧锚着它们）
+ 渲染后端标签；**第二行** 12 个显示开关。

- **产品侧零 C++ 语义复制**：点击 → `ActionRouter.trigger(<引擎 action id>)` → 注册表未命中
  → **引擎透传**（`findAction → StelAction::trigger`）。快捷键走 `routeKey` 落**同一条**
  `StelAction` ⇒ 按钮态 / 引擎态 / 快捷键态只有一份真源。
- **状态回读**走 `displayTogglesRevision` token + `actionChecked()`（T15 铁律：
  **只调方法不读 token 的绑定永远不重算**）；TB-12 就是这条铁律的直接判据。
- 探针实测注册表 **505 动作 / 15 分组**、12/12 checkable、`getText()` **中文**、快捷键
  `C/V/R/E/Z/G/Q/A/D/Alt+P/O/Ctrl+N`。
- 判据 `TOOLBARCHECK` **12 条**；四组负控红项：A `REV_OFF`=[TB-07,09,12]、
  B `TOKEN_OFF`=[TB-09,12]、C `CLICK_OFF`=[TB-10]、D `LAYOUT_BREAK`=[TB-10]+`covered=false`。
- **定稿轮（mac）`FAILED=0` 全绿**：正题 5/5、四组负控全 OK、探针 OK、
  10 套件 + `INTERACTCHECK` **18/18（本批拿到焦点）** + S3 + A2 + DYN 全 rc=0。

⚠️ 本轮五条新血泪（详见 `docs/T34_TOOLBAR.zh_CN.md` §5 与 `TRAPS.md`）：
① **Repeater delegate 的 QObject 父链是空的** ⇒ `QObject::findChild` **永远扫不到**
（静态按钮却能扫到 —— 同款 API 两种结果）；② **裸 `Rectangle` 根 `implicitHeight=0`
+ 不裁剪 = 假绿掩护**（布局全乱、按钮照画、存在性判据全绿）；③ 🔴 **`childAt` 会撒谎**
（按钮明明在落点上、点击也生效，仍一路返回根内容控件）⇒ 改用**几何覆盖**自证；
④ `Flow` 的 `implicitWidth` 是单行宽度 ⇒ 与另一个 `fillWidth` 项**对分**后竖成 12 行、
把工具栏撑到 446px；⑤ 连续起停会让 **Metal 掉设备**（无判据输出 ≠ 判据失败）。

### T35 仿真时间链路"脱钩"定性 —— ✅ **已完成**（`docs/T35_TIMELINK.zh_CN.md`）

T27 移交的**优先级最高**的线索（它决定所有时间类判据口径是否可信）**已结清**：

- **结论**：不存在"推进量与 rate 脱钩"。`ΔJD == 真实墙钟 × rate × scale` 成立
  （TL-01 相对偏差 **0.19%..1.40%**／5 跑，恒星时**绝对腿**残差 **+0.0000°**）。产品侧**零语义改动**。
- **三处口径污染**逐一定位（单位 / 移动靶 / 名义窗口），并把 T27 文档、T27 证据 README、
  `main.cpp` IT-07 注释三处**就地标注**（不删原文：观察是真的，解释是错的）。
- 顺带定性：**T27"视线锁地平 ⇒ 随 JD 漂"的机理是对的**——真机理在 `updateVisionVector`
  的 mountFrame 锁定分支（探针 Q6d 实测 ΔRA 与 ΔLST 同阶）；错的只是速率读数。
- 判据 `TIMELINKCHECK` **7 条**；三组负控红项**两两不同**：A `BREAK`=[TL-01]、
  B `RATE_IGNORED`=[TL-01,02,05]、C `FREEZE_LEAK`=[TL-04] ⇒ 三条腿各自承重。
- **定稿轮（mac）`FAILED=0` 全绿**：正题 5/5、三组负控全 OK、探针 OK、
  11 套件 + `INTERACTCHECK` **18/18（本批拿到焦点）** + S3 + A2 + DYN 双路 5/5 + `producer-readback`。

⚠️ 本轮六条新血泪（详见 `docs/T35_TIMELINK.zh_CN.md` §8 与 `TRAPS.md`）：
① 🔴 Qt **`.arg()` 与 printf 风格混用** ⇒ `%.4f` **静默原样输出**（探针/判据首跑全线踩中）；
② 🔴 macOS awk 里 **`exp` 是内建函数名**，不能当变量 ⇒ **语法错误、零输出**，
脚本自证门"静默变空"看起来像"没输出"而不是"算错了"；
③ 🔴 **负控的"落点"和"形状"一样重要**——第一版负控 C（解冻后补回冻结期）形状完全正确，
但"补"的那一脚落在 `arm()` **开窗之前** ⇒ 判据照绿；
④ 🔴 **就绪门太紧 ⇒ 假红**：批次连跑 11 套件时 `REPLAYCHECK` 的 `RP-04` 读 `207×0` 假红，
单独复跑两次都 `11/11` ⇒ `kUiLayoutWaitTries` 2.5s → **6s**（**断言不动**，只放就绪门）。
⑤ 🔴 **断言里不许塞"受调度影响"的量**：TL-02 初版把"两窗长度差 ≤20%"当断言，定稿轮被调度抖动打红（档A 名义 1.5s → 实测 **2.19s**）⇒ 假红 1/5 跑；改成**速率归一化**比较后免疫（长度差降为读数）。
⑥ 🔴 **治本：给"窗口型"读数加有界就绪门** —— 事件循环被饿住时**定时器与帧泵同源停摆**，窗口被拖长、ΔJD 又少涨（两量被同一抖动污染）⇒ 该窗无分辨力。**W 超名义 25% ⇒ 本窗不可判 ⇒ 重起窗重测**（预算 3 次；`if (r.suspect) return;`）。门是**单边**的、且**真缺陷不改墙钟** ⇒ 不掩护真缺陷。

### W-T34 + W-T35 Windows 跨平台复验（**并批**）—— ✅ **已完成**（`docs/WT35_WINDOWS_RECHECK.zh_CN.md`）

T33 证明过"换平台复验能逼出真缺陷"，这是**两条线索的跨平台收口**：

- **Windows 侧全绿**：`launcher exit=0`、零 FATAL。`TOOLBARCHECK` 正题 **3/3 `12/12 PASS`**、
  `TIMELINKCHECK` 正题 **3/3 `7/7 PASS`**；两个套件**全部负控红项与 mac 逐位一致**
  （TB：`[TB-07,09,12]` / `[TB-09,12]` / `[TB-10]` / `[TB-10]`+`covered=false`；
  TL：`[TL-01]` / `[TL-01,02,05]` / `[TL-04]`）；11 邻套件 + S3 + A2 + 探针 2/2
  + `INTERACTCHECK` 3×18/18 + DYN 双路 6/6 + 就绪门触发 0 次。
- **首轮报过 13 条期望校验失败 —— 是纯仪器缺陷，不是产品缺陷**：
  `$judge`（调用点结果）覆盖了拼正则用的 `$JUDGE`（**PS 变量名大小写不敏感**，
  **W-T31 已踩过一次，但那次的修法是实例级不是类级**）。
  鉴别特征：**只有"靠运行时变量拼正则"的那一个字段坏**、**从第 2 次调用起才坏**。
- **类级修法**：正则里**零非 ASCII**（`\s+[^\s]+\s+` 通用跳过）+ 结果变量改名 `$judgeTxt`。
- **三方闭环**证明修好：①旧仪器在 mac 上复现**逐字相同**的 `12/12|PASS → |PASS → /|PASS`
  （⇒ 语义问题，不是 PS 版本怪癖）；②新仪器对全部真实日志 **97/97 PASS**；
  ③新增 `REPEATED CALLS` 断言**承重**（(a) 证明它在旧代码上会红）。
- 送源同源改用 **`git hash-object`**（裸 MD5 跨平台比的是 **CRLF/LF 行尾**）：
  **94 项 / 90 同源 / 0 内容不同 / 4 缺失**（4 个全是 mac 专用 `.sh`）。
- 收尾扫出并清掉 **3 个僵尸计划任务**（`StelQuickT17Build` / `StelQuickT18LongRun` / `t17winbuild`）。

⚠️ 本轮六条新血泪（详见 `docs/WT35_WINDOWS_RECHECK.zh_CN.md` §6 与 `TRAPS.md` 59–64）：
① 🔴🔴 **"修法是实例级不是类级"** ⇒ `$ok/$OK` 的坑换个门又进来；② 🔴 裸 MD5 跨平台比行尾；
③ ⚠️ UU远程前端重启 = 隧道复活，**`nc -z` 报 PORT-OPEN 不能当隧道判据**（要读 SSH banner）；
④ ⚠️ `win.sh -ps '多行脚本'` 静默失效 / `*>>` 把子进程输出绞成 NUL（技能里记过，照样再踩）；
⑤ ⚠️ 收尾必须扫**全部**计划任务残留；⑥ ⚠️ 探针冒号后空格宽度**逐探针不同**（1 vs 3），锚点用 `\s+`。

### T36 个人版配置目录隔离 + 首次播种（**A5 第一项**）—— ✅ **已完成**（`docs/T36_CONFIG_ISOLATION.zh_CN.md`）

进 A5 前先探针，结果坐实**真缺陷**：合流形态直接用引擎**默认**用户目录 = **原版 Stellarium 的目录**。

- **探针实测 3 处变化**：`config.ini` mtime（**md5 不变**）、`log.txt` `10308→10387 B`
  （原版日志被顶掉）、`modules/Oculars/ocular.ini` mtime。⚠️ `config.diff` **0 行**
  ⇒ **"内容没变"≠"文件没被碰"**，按内容比对会得出错误结论。
- **三条写穿路径**：`immediateSave()`（本机 `immediate_save_details=true` ⇒ 翻个开关就落盘）／
  `StelLogger::init(userDir + "/log.txt")` 截断覆盖／模块 `findFile(..., Writable|File)` 原地改写。
- **不能靠"原目录当只读回退"**：`Writable` 只表示**那个文件**可写，模块会命中原版副本并改写
  ⇒ 必须先**播种**，让个人版目录里什么都有、原目录彻底退出搜索路径
  （`setUserDir` 是 `replace(0,…)` 不是 append）。
- **落点** `<引擎默认目录>-quick`（**同级**，不是子目录）⇒ "原目录零改动"是**结构性**成立。
- 🔴 **修的过程中牵出第二条腿**：空用户目录 ⇒ 引擎 **SIGSEGV（rc=139）**，
  崩在 `LandscapeMgr: initialized Cache` 之后。**分块播种二分**证明崩因是"缺 `config.ini`"
  （只放 `stars`/`modules`/`data` 都崩，只放 `config.ini` 就正常）⇒ 补**兜底腿**
  （从 `data/default_cfg.ini` 拷，对齐上游 `src/main.cpp:398-403`），且**不随** `MIGRATE_OFF` 关闭。
- 判据 `CONFIGCHECK` **8 条**；两组负控红项**两两不同**：A `ISOLATE_OFF`=
  `[CFG-01,02,03,04,06,07]`（2/8）、B `MIGRATE_OFF`=`[CFG-05,08]`（6/8）。
- **定稿轮（mac）`FAILED=0` 全绿**：S1 首次 8/8、S2 非首次 8/8、**全新机器 8/8（rc=0，此前 139）**、
  12 套件 + `INTERACTCHECK` **18/18** + S3 + A2 + DYN 双路 3/3+3/3 全 rc=0、
  **收尾"合流形态全程原目录零改动 = 0 差异"**；脚本层 **23/0**。

⚠️ 本轮三条新血泪（详见 `docs/T36_CONFIG_ISOLATION.zh_CN.md` §7 与 `TRAPS.md` 65–67）：
① 🔴 **判据的条件必须与判据的名字同义** —— `note` 字段语义混用（异常 vs 正常信息）
⇒ CFG-01 在**非首次启动和负控 B 下假红**；修法是**拆字段**不是删条件；
② 🔴 **外层判据的作用域必须等于被测对象的作用域** —— 收尾"全程零改动"把 **S3 旧宿主**
圈了进去，而旧宿主**不走**隔离引导、**它就是"原版程序"**，写原版目录是应有行为
⇒ 4 个文件报红是**范围划错的假红**（修法：S3 挪到核对之后单跑，只验 rc，影响如实留证）；
③ 🔴 **判据在"缺前提"的机器上会退化成平凡真** —— 原目录无 `config.ini` 时，
CFG-06/07 的原目录侧"天然成立"（空串不含哨兵 / 两个 `<unreadable>` 相等）
⇒ 补**第三半**"原本不存在 ⇒ 跑完仍不许出现"。
附两条仪器教训：⚠️ **stdout 块缓冲 / stderr 不缓冲** ⇒ 混流时"最后一行"不可信，
判 crash 点必须**分流失**；⚠️ `findFile` 的 `Writable` **只保证"那个文件可写"**。

### T37 设置页骨架 + 夜视闭环 —— ✅ **已完成**（`docs/T37_NIGHTMODE.zh_CN.md`）

**A-1.0 的前提性假设被证伪并重新落地**（「夜视仍由旧后处理负责、避免叠加两次」）：

- **引擎旧夜视后处理物理不可达**：`NightModeGraphicsEffect` 是挂 QGraphicsItem 的
  **纯 GL** effect，只在 QGraphicsView 场景渲染循环里被调；合流宿主 `WA_DontShowOnScreen`
  + 帧走 `LegacySkyHost` 自建 FBO + Metal RHI ⇒ 实验：禁用该 effect **行为不变**。
- **三轮反转才定案**（⚠️ 第一轮探针头条是假绿、第二轮把引擎正常语义误判成缺陷）：
  ① 探针"上游 OFF↔ON 逐位相同"= 等待挂读步读到旧帧（帧号 178==178 实锤，TRAPS 69）；
  ② 修掉等待后见"翻转夜视 ⇒ 上游黑帧"，初判"引擎夜视反应有害"（用户拍板方案 A）；
  ③ **判别实验**（对照量换 `actionShow_Atmosphere`）证明：黑天空 = 夜视 → **大气整层
  退场**（三处 `if (getVisionModeNight()) return;`，**原版语义**）+ 白昼亮度模型压住
  星星；**夜视开 + 大气关 = 漂亮星空+银河** ⇒ 引擎无病，**方案 A 不需要执行**。
- **产品落点**：`keySink.layer` + ShaderEffect（`.qsb` 预编译，公式逐字复刻引擎
  `lum=max(r,g,b)→(lum,0.3lum,0)`）—— 夜视由 Qt Quick 侧承担**唯一一次**红移。
  🔴 Qt 6.11 的 ShaderEffect **只认 `.qsb`**，内联 GLSL 的失败形态是**整个 layer
  不渲染 ⇒ 全白屏**（不是"效果不生效"）。
- 判据 `NIGHTCHECK` **6 条**；负控 `NIGHT_EFFECT_OFF` 恰好红 `[NC-03②]`。
  关键口径：噪声底用"**低于噪声容差**"（冻结下引擎渲染有亚 LSB 抖动）、NC-03②
  均值门 **1.0**（暗星空下滤镜均值实测 ~14.5，不能用 50）、NC-05 用"无全帧级红移"
  （fader 余辉有界容忍）。
- **定稿轮（mac）`FAILED=0` 全绿**：正题 3/3 `6/6 PASS`、负控 3/3 恰中、
  13 套件 + `INTERACTCHECK 18/18` + S3 + A2 + DYN 双路 3/3+3/3 全 rc=0；脚本层 2/0。

⚠️ 本轮新血泪（`TRAPS.md` **69/70**）：① 🔴 **"效果判据"的三种伪装**——等待挂读步
⇒ 旧帧假绿（效果帧必须打帧号并断言推进）；黑帧差异（~157）与滤镜效果（~162）同量级
⇒ 效果判据可被"内容消失"伪装（伪证守门 = 上游不变）；冻结 ≠ 逐位相同（亚 LSB 抖动）。
② 🔴 **ad-hoc 内联命令漏传模式变量** ⇒ app 落回普通 GUI 模式 ⇒ "引导挂死"幻影
（T37-X3）；铁证手法 = 用日志行反推运行模式；**跑自检一律走 verify 脚本**。



### T38 显示参数（亮度/星等 · 视场 · 投影）—— ✅ **已完成**（`docs/T38_DISPLAY.zh_CN.md`）

**A-1.0 范围表「亮度/星等、视场、投影 | 基础 | 必须」这一格落地。** 先探针再写 UI：

- **T38-A 探针 7 条实测结论**（决定产品落点与判据口径）：
  ① 四个数值 setter **一个都不夹取**（原样落库 + `immediateSave`）⇒ **范围闸必须在 façade**；
  ② `setFov` **自带夹取**，且 **maxFov 是投影的函数**（120/180/185/235/270/360 六种）
  ⇒ `maxFieldOfView` 必须是**活属性**，滑块上限跟着投影走；
  ③ 投影非法 key **不报错**、静默落到 Stereographic **并落盘** ⇒ 必须**白名单闸**；
  ④ 🔴 `getLimitMagnitude()` **不是**用户设定值（是引擎按大气/光污染/自适应的**有效**限制，
  白天实测 -4.44）⇒ 判据只读写 `customStarMagLimit` + `flag`；
  ⑤ 星等/亮度的**视觉**效应只在星星可见时存在 ⇒ 布场必须**大气置关**；
  ⑥ 静置期 NOTIFY 增量 **0** ⇒ 这些量**可以安全绑定**。
- **产品**：`AppFacade` 显示面（10 个 `Q_PROPERTY` 共用一个 NOTIFY + 8 个范围常量 +
  范围闸 + 白名单闸 + 6 条引擎转发）+ `DisplayPage.qml`（星点亮度 / 极限星等 / 视场 / 投影）
  + 工具栏 `navDisplayButton` + 路由；范围照抄原版 `ViewDialog`。
- 🔴 **判据抓出一个真产品缺陷**（本轮最有价值的产出）：`AppFacade::projectionTypeKeys()`
  原本是 **`Q_INVOKABLE`**，而合流形态**先 `engine.load()`、后 `boot()`** ⇒ QML 里
  `model: facade.projectionTypeKeys()` 这种**函数式绑定不读任何属性 ⇒ 只求值一次**，
  那一次引擎还没起来 ⇒ 拿到空表 ⇒ **12 个投影按钮一个都不出现且永不自愈**。
  已改 `Q_PROPERTY + NOTIFY` + 引导完成时"**开机唤醒**"emit（TRAPS 71）。
- 判据 `DISPLAYCHECK` **14 条**（DP-00..DP-11）；**两组负控**红项 `[DP-02,DP-04]` / `[DP-07]`
  **两两不同**；负控读数**自带证据**（"写 99 ⇒ 引擎收到 99"、"乱码 ⇒ 引擎真的被兜底改 key"）。
- **另一条口径修正**：冻结下的亚 LSB 抖动**面积会随场景状态变、幅度不会**
  （实测 0.079%/Δ1 vs 0.557%/Δ2）⇒ 噪声判据**只能取幅度**（TRAPS 74；T37 旧口径同时作废）。
- **定稿轮（mac）`FAILED=0` 全绿**：正题 3/3 `14/14 PASS`、负控各 2/2 恰中、
  14 套件 + `INTERACTCHECK 18/18` + S3 + A2 + DYN 双路 3/3+3/3 全 rc=0
  （产物 `40637720 B / md5=30f4e313eb382615792ca2b1866e0176`）。
- 本轮新血泪：`TRAPS.md` **71–76**（求值时机型绑定失效 / 参考帧基准必须干净 /
  `delayAfter` 只能挂写步 / 噪声只取幅度 / 判据只验被声称的命题 / 近名 getter 语义差）。



### T39 高 DPI + 渲染诊断（**A5 第三项**）—— ✅ **已完成**（`docs/T39_HIDPI.zh_CN.md`）

**A-1.0 两格同一轮清空**：第一格「…、**高 DPI** | 基础 | 必须」⇒ **此格清空**；
第二格「资源路径、错误提示、**渲染诊断**、配置保存 | 最小 | 必须」（T36 已做「配置保存」）
⇒ 本轮做掉「渲染诊断」（余「资源路径 / 错误提示」归 T41/T42）。先探针再写 UI：

- **T39-A 探针 7 条实测结论**：
  ① 三个 setter **一个都不夹取**（写 99/99/999 原样回读 + `immediateSave` **落盘**用户 config）
  ⇒ **范围闸必须在 façade**；
  ② 派生量算术全对（`getScreenScale() = dpp × 字号/13`）；
  ③ 🔴 `getGuiFontSize()` **读的就是全局字体**、`setGuiFontSize()` **改的也是全局字体**
  ⇒ 它**不是"用户设定值"的可靠真源**（T38 近名 getter 同族）；
  ④ 🔴 `setGuiFontSize(25)` 后 QML 视觉树 **325 项一项都不跟随**（**三重仪器正控**验证完备：
  读取链路能读到 / 真实树人为扰动**恰查 1 项** / **点值路径**同样 0 跟随 + 前扫分类计数
  68+257=325）⇒ 引擎 `guiFontSize` **只作用于老 QWidget 对话框** ⇒ QML 缩放必须产品侧自己做系数；
  ⑤ `screenFontSize` **强帧效应**：噪声底**逐位相同**（哈希一致）→ 13→40 差异 **4.054%**，
  还原**逐位回到原哈希**；⑥ 静置 NOTIFY 增量 **0** ⇒ 可安全绑定；
  ⑦ `FrameMailbox::Stats` 全字段可读 ⇒ **渲染诊断有真数据面**。
- **产品**：`AppFacade` 高 DPI 面（6 个 `Q_PROPERTY` + 7 个 `CONSTANT` 范围常量 + 整数版
  范围闸 `clampIntoInt()`〔复用 `STELQUICK_DISPLAY_GATE_OFF`〕+ 3 条引擎转发）
  + `DisplayPage.qml`「界面字号（天空文本）」组 + **`BackendInfo` 运行时诊断面**（13 个
  `Q_PROPERTY` + `Q_INVOKABLE refresh()` + `setFrameMailbox()`）+ `DiagnosticPage.qml`
  「运行时诊断（帧泵健康度）」组（Timer 周期 refresh —— 连续量**不建绑定**）。
- 🔴 **UI 只提供被验证过真的有效的控件**（本轮产品侧口径）：探针④已证 `guiFontSize` /
  `screenButtonScale` 对 QML **零影响** ⇒ **不搬那两个 SpinBox 上来**（搬过来就是**假控件**：
  拖了没反应、还顺手写穿用户 config），只在 `displayScaleStatusLabel` 上**文字说明作用域**。
  这是「判据只验被声称的命题」（TRAPS 75）在产品侧的**对偶**。
- 🔴 **判据抓出一个真产品缺陷**（本轮最有价值的产出）：`DisplayPage.qml` 的 `onMoved` 首版写
  `appFacade.setScreenFontSize(...)` —— **`Q_PROPERTY` 的 WRITE 函数不是 `Q_INVOKABLE`/slot**
  ⇒ QML 侧 **静默** `TypeError: Property 'setScreenFontSize' … is not a function`
  ⇒ **拖滑块 UI 数字会动、引擎纹丝不动**。已改**赋值语法** `appFacade.screenFontSize = …`。
  ⚠️ 这条**只有 HP-08 的"真实点击"腿**才抓得到（T38 的 DP-07 只做了单向"引擎侧改 ⇒ UI 跟随"，
  且 T38 五个滑块恰好用的是赋值语法）⇒ **UI 腿判据两个方向都要有**。
- 判据 `HIDPICHECK` **12 条**（HP-00..HP-11）；**两组负控**红项 `[HP-02,HP-03,HP-04]` /
  `[HP-07,HP-08]` **两两不同**；负控读数**自带证据**（"写 99 ⇒ **引擎=99**"，探针①在判据层复现）。
- **顺带修掉**：`STELQUICK_PAGE` 原排在 `startPage` 三元链**最后**，任何起引擎的模式都会吞掉它
  ⇒ 诊断页（**必须引擎活着**才有人喂数据）根本切不过去 ⇒ 改为**显式指定优先**。
- **定稿轮（mac）`FAILED=0` 全绿**：正题 3/3 `12/12 PASS`、负控各 2/2 恰中、
  **15 套件**（含 **`displaycheck`** 最近邻回归）+ `INTERACTCHECK 18/18` + S3 + A2 +
  DYN 双路 3/3+3/3 全 rc=0（脚本层 **3/0**；产物 `40929912 B /
  md5=f0e39fd324c8dfdceb0a35c64c1bd442`；批次期 `mdbulkimport` 在跑，仍全绿）。
- 本轮新血泪：`TRAPS.md` **77–82**（WRITE 函数不是方法 / `invokeMethod("increase")` 不是交互
  〔+隐藏页·视口裁剪·DFS 找 Flickable〕/ **否定性结论必须自己先被验证** /
  **噪声门限不能跨布场搬** / **诊断不得污染被诊断的量** / UI 只提供被验证过真的有效的控件）。

### T40 快捷键编辑 —— ✅ **已完成**（`docs/T40_SHORTCUTS.zh_CN.md`）

**A-1.0 范围表**：`|快捷键编辑、帮助、版本与许可证页面 | 可延后 | 必须 | 中文输入焦点不能触发天空快捷键|`。
第三列那条约束**已由 T29 兜住**（`keySink` 的 Esc 分支先问 `ActionRouter::canDispatchToSky()`）
⇒ 本轮交付**第一列那个页面本体**（「帮助/版本/许可证」归 T41）。先探针再写 UI：

- **T40-A 探针 12 条实测结论**（全现成，引擎一行不改）：① `QKeySequence` 往返恒等 **14/15**，
  唯一不等的 `PageUp` 是 **Qt 键名别名**（正名 `PgUp`；`PageUp`/`PageDown` 解析成**空序列**）
  ⇒ 🔴 **键名一律由 C++ 生成**、QML 绝不自拼；② 判"多键序列"**只能用 `count()`**
  （注册表有动作的主键就是 `,`，用"串里有逗号"判会误计）；③ `setShortcut()` **即时生效**但
  **只发 `changed()`、不发 `shortcutsChanged()`** ⇒ 下游自己刷新；④ `saveShortcuts()`
  **只动 `shortcuts` 组**（非该组零差异=不写穿物证）；⑤ **空串 = 移除**；
  ⑥ 🔴 **落盘会规范化字符串**（`Ctrl+E, Ctrl+2`→`Ctrl+E,Ctrl+2`）⇒ 比"改没改成功"
  要**比序列语义**不能比字符串；⑦ 单恢复**不冲别人的键**（⚠️ 探针首轮因两靶撞成同一动作
  得出过相反结论）；⑧ 出厂注册表**无重复键**；⑨ `actionsEnabled` 门**只挡 `pushKey`**、
  `routeKey` 不查它 ⇒ 捕获态必须**自己吃键**；⑩ 注册表 **505 动作 / 15 分组**、
  `getGroup()` 是英文 ⇒ 分组中文标题产品侧映射；⑪ "是否被改过"真源 =
  `conf->contains("shortcuts/"+id)`（与引擎构造同判据），`defaultKeySequence` 私有 ⇒ **不显示"默认值"列**；
  ⑫ 落点 = T36 个人版目录。**探针首轮四类方法缺陷**（靶撞车 / 比较口径错 / 无效现场 /
  无真组合键）已逐处修，**含一条结论翻案**。
- **产品**：`ShortcutModel`（`QAbstractListModel`，**唯一数据面+命令面**：读表 / 改键 /
  冲突计算 / 恢复 / 落盘；含 11 个 role、8 个属性、9 个可调方法）
  + `ShortcutsPage.qml`（**只做展示 + 键事件采集**；三条硬规矩：绝不自己拼键名 /
  捕获态必须吃键 / Esc 捕获态=取消·非捕获态=返回天空）+ 导航按钮 + 路由 + context property。
- 🔴 **判据抓出一个真产品缺陷**（本轮最有价值的产出）：**非法键名闸漏了"解析后为空"这一半** ——
  `QKeySequence("PageUp").count()` 是 **1**、`toString()` 却是**空** ⇒ 闸放行、`norm` 变空串
  ⇒ 引擎侧等于**静默删除该快捷键**（正是探针①警告的形态）。修法：闸加 `|| norm.isEmpty()`。
  ⚠️ **只有判别对照抓得到**：普通"设个键、读回来"的正测**永远绿**。
- 🔴 **判据自身五处方法缺陷**（非产品）：① `Ctx::finish()` 用的 `Ctx::onDone` **漏赋值**
  ⇒ 14 步全跑完、收尾 `std::bad_function_call`、**零判据输出**（RC=134）；② 台账 id
  `arg(i)` 生成 `SC-1` ≠ note 首词 `SC-01` ⇒ **前 9 条 mark 静默丢失**（症状：
  "判据 5/5 却 VERDICT=FAIL"）；③ 判据**自建 model 副本**与被 QML 绑定的那个是**两个实例**
  ⇒ 自检刷新了副本、**UI 是空表**、delegate 一个都不创建；④ `StackLayout` 隐藏页 `ListView`
  **尺寸 0** ⇒ 切页后必须**等 ≥1 帧**才有 delegate（T39 同款）；⑤ 交互腿的产物要去
  **UI 自己能观测的地方**读（按钮 `text`），别拿孤立副本当参照。
- 判据 `SHORTCUTCHECK` **15 个 id / 14 条**（SC-01..SC-14，05 拆 a/b）；
  **两组负控**：A `WRITE_OFF` 红 `[SC-05a,SC-05b,SC-07,SC-08,SC-09,SC-11,SC-13]`（8/15）、
  B `SAVE_OFF` 红 `[SC-05b,SC-09,SC-11]`（12/15），正题 `∅` ⇒ **两两不同**。
  🔴 **红项比靶心大是步骤链的事实、不是判据缺陷**（写腿一断 ⇒ 后步前提态不存在 ⇒ 连锁红），
  且**两腿正交可定位**：`A\B = {SC-05a,SC-07,SC-08,SC-13}`（引擎写入生效组）、
  `B\A = ∅`、`B = 磁盘态组`。**首版负控期望值是"推理填的"，实测全错 —— 已按实跑读数改写**。
- **定稿轮（mac）`FAILED=0` 全绿**：正题 3/3 `15/15 PASS`、负控各 3/3 命中、
  **16 套件**（含 **`hidpicheck`** 最近邻回归）+ `INTERACTCHECK 18/18` + S3 + A2 +
  DYN 双路 3/3+3/3 全 rc=0（脚本层 **5/0**；产物 `41329752 B /
  md5=de8b47d940aafc82c12b4d6c0bbdd8dd`；批次期 `mdbulkimport` 在跑，仍全绿）。
- 本轮新血泪：`TRAPS.md` **83–87**（`count()==1` ≠ 可用 / **收尾回调必须是成员** /
  **台账 id 必须与 note 首词逐字一致** / **判据自建副本 ≠ 被测接线实例** /
  **负控期望值必须实跑出来再写死**）。陷阱 45/46（delegate 父链为空、隐藏页尺寸 0）
  **本轮又各踩一次** —— 编号不新加，但说明是高频坑。


### T41 帮助/版本/许可证 —— ✅ **已完成**（`docs/T41_HELP.zh_CN.md`）

**A-1.0 范围表**：`|快捷键编辑、帮助、版本与许可证页面 | 可延后 | 必须 | …|` 的**后三项**。
先探针再写 UI：**T41-A 探针 93 行读数推翻三处开工推断**——① `src/gui/` **是被编译的**
（在 `stelMain` 静态库里）⇒ 老 QWidget GUI 子系统整个随行；② 8 个窗口动作**全在册**且
`getGroup()`="Windows" 是真读数；③ 🔴 **F1 `trigger()` 真的弹出老 QWidget 对话框**
（QWidget 12→**58，Δ46 常驻**，`StelDialog` 只隐藏不销毁）⇒ **必须接管**。另证：
🔴 **`COPYING` 不在 bundle 里**（探针 `findFile` 命中只因 cwd 恰是源码树）⇒ GPL 须编 qrc；
⚠️ `QCoreApplication::applicationVersion()` 是 `1.0.0` ⇒ 版本走 `StelUtils`。

- **产品**：`ActionRouter::setHostTakeover(id,on)` + `hostActionRequested(id)`——命中**不转发
  引擎 trigger**、改交 QML，本类**不知道"页"**；⚠️ **`routeKey()` 同查表**（只接管 trigger =
  假修复）；接管仍计 `dispatchCount` + 发 `dispatched(id,true)`（U-ACT-01 不变）。注册
  **F1/F3/F4/F5/F6/F7** 六个；**F2/F10/F12 刻意不接管**（无 QML 页，移交 T42）。
  `HelpModel` 只读数据面（手势 23 = sky13+legacy10、`scope` 诚实性字段；外链 7 走**白名单**
  `openExternal`；贡献者 246→**245** 去重；**GPL 全文 17992 B 编进 qrc**）+
  `HelpPage.qml`（索引 7）/`AboutPage.qml`（索引 8）+ Toolbar **+2 导航按钮**（原 8 个
  objectName 未动）+ `qt_add_resources(stelquickui_license)`。
- 🔴 **真缺陷 ×2（均已修）**：① `BackendInfo::applyRuntimeApi` **自判 `backendOk` 写死
  "Vulkan"**（T39 遗留 —— 转 Metal 后恒 false，状态色全错）⇒ 改 `applyBackendResult()` 由
  main.cpp 传入（判定只有一份）；② `helpGotoShortcutsButton` 沉底 y=1036 且页面不可滚
  ⇒ **用户自己都点不到** ⇒ 挪页首（陷阱 92："被测物的问题" vs "观测姿势的问题"）。
- 判据 `HELPCHECK` **17 条**（HC-01..17）；负控 A `TAKEOVER_OFF` 红 `[HC-14,15,16]`、
  负控 B `LICENSE_OFF` 红 `[HC-04,13]`，**两腿正交**。🔴 **判据自身 4 处方法缺陷**：
  负控 B 首版期望又是推理填的（陷阱 87 第二次重演，HC-05 验"编进 qrc"≠"加载"腿）/
  **HC-16 判别对照恢复段把被负控打断的路径自己修好了 ⇒ 假绿**（恢复只到**布场态**，
  陷阱 80 对偶）/ 基线混启动瞬态（排除 QSplashScreen/QProgressBar）/ 残留 QDialog 连带红。
- **定稿轮（mac）最终全绿**：正题 3/3 `17/17`、负控各 3/3、**17 套件**（含
  **`shortcutcheck`** 最近邻——T41 动了 `routeKey`，正是 T40 SC-14 交互腿路径）
  + `INTERACTCHECK 18/18` + A2 + S3 全 rc=0；**DYN 双路 3/3+3/3**（C02 推进
  681/657/658 / 269/271/267 帧）。首轮 DYN 0/3+0/3 定性为**环境受阻**（全屏浏览器
  独占 Space ⇒ 窗口 occluded ⇒ 场景图停摆；**运行中抓屏 = 直接物证**；替身腿同款红
  排除产品 + `A2_TRACE` 数到 8 秒 3 次 `updatePaintNode`）。⚠️ **抓帧型判据照绿 =
  假绿掩护**（`grabWindow` 强制渲染绕过遮挡）。⇒ **TRAPS 90–93**。
- 批次期 load 12.44 + `mdbulkimport` 在跑（16 套件仍全绿）；复跑时 load 2.91。

### T42 状态/错误页 + "未支持项"清单 —— ✅ **已完成**（`docs/T42_ERRORS.zh_CN.md`）

A-1.0 第二格（资源路径 + 错误提示）与 A5「错误页」收口。先探针再写 UI：**T42-A 探针 57 行读数
推翻 1 处开工假设**（老对话框把尺寸写进 `DialogSizes/*` ⇒ 判据必须自净）、**证实 T41 推断**
（F10 Δ697 / ⌥B Δ58 真弹老窗口）、**发现 T41 遗漏**（⌥B 观测列表从未被接管）。产品 =
`ErrorModel`（静态健康面：状态 4 行 / 路径 8 行四态含 `unset` / 未支持 7 条从注册表真源取）
+ `ErrorPage.qml`（操作行页首 —— 陷阱 92 教训）+ **4 个老窗口动作接管改"未支持"提示**
（解 T41 两难：既不静默开旧对话框、也不"按了没反应"）+ **`boot()` 用户目录可创建性预检**
（🔴 真缺陷：`StelFileMgr::init()` 用户目录不可创建 ⇒ `qFatal`/SIGABRT；`STEL_USERDIR`
可注入真实失败 ⇒ 判据失败腿 5/5 走真失败路径，不造假桩）。判据 `ERRORCHECK`
**A 组 15 / B 组 5**，负控三腿实跑写死且正交；**config 零污染门**（EC-09 只判不净曾把
`DialogSizes/Configuration` 残留进真实 config —— 已修 + 脚本 md5 门）。
判据自身陷阱 85 三演（mark 没写台账 ⇒ 14/14 却 FAIL(0/14)）。
🔴 **收口轮白天批跑逼出 TRAPS 97**：布场昼夜不确定（卫星真实 UTC 移动 / 瞳孔适应 log 律
爬秒级 / 白天无标签 ⇒ HP-09 假红、DP-00 INCONCLUSIVE）⇒ DisplayCheck/HiDpiCheck 布场钉
固定夜 JD + 6.5s 适应收敛；同轮修掉脚本 `red_ids` 正则缺陷与 **HC-14 口径**（T41"预期 6"
⇒ T42 后 10）。定稿轮 **SCRIPT-RC=0 FAILED=0**：正题 3/3 `15/15` + 失败腿 2/2 `5/5` +
负控各 2/2 恰中 + 回归 24 项全 rc=0（含 `helpcheck` 17/17、DYN 3/3+3/3、INTERACT 18/18）。

### T43 A6-A 帧桥统计 + 性能三分账 —— ✅ **已完成**（`docs/T43_FRAME_BRIDGE.zh_CN.md` + `docs/A6_FRAME_BRIDGE_STATS.zh_CN.md`）

两轮 45min 长跑（run1 有 agent 在场 = 污染对照 / run2 **零操作**）。**SL-C03 FAIL 七层判别全记录**
（污染✗｜系统更新✗｜周期✗｜成段停顿✗｜环境字段✗｜临界排队✗｜采集口径✗）⇒ **铁证**：
≥60ms 帧 0.51%→3.16%（6.5×），主干 p90 归一化三家一致 ⇒ T13..T42 产品代码偶发 GUI 线程尖峰
（引擎产能没退化，Windows 无尾部）⇒ 二分立 **#160**（计划二前必做）。**管辖权裁定（陷阱 99）**：
SL-C03 属 T13 B-LSR3 **不在 A-1.0 出口 7 条**；P-BRG-03=A2 冻结 40fps（42.87 PASS）⇒
**P-BRG-01..04 全绿**。TRAPS 98（`grep -v '//'` 假绿）/ 99 / 100 / 101。

### T44 A6-B 生命周期回归 P-LIF-01..04 —— ✅ **已完成**（`docs/T44_LIFECYCLE.zh_CN.md`）

**A1 起挂的欠账清零**（`main.cpp` 的 hide() 关闭代理注释为证）。判据 **13 条全绿**
（LF-02a..f / LF-03a..d / LF-04a..c，`STELQUICK_LIFECYCLE_CHECK=1`）；P-LIF-01 进程级 ×20 = 20/20
（×100 ONESHOT 接线完整）；内存预算 32 MiB（4 轮实测 2×）。负控三条实跑写死
（NOSIZE⇒{03a,04a}｜NOSETTLE⇒{02b,03a,03c,04a,04c}｜HIDE⇒{02f}）。🔴 **陷阱 102**：
`delayAfter` 时序写反 ⇒ 采样在动作后 0ms ⇒ 一整套"互相矛盾"的读数；治法 = 改名 `waitBefore`。
LF-02f 把 A1 的 hide() 代理钉死（真最小化 `windowStates()` 必含 `WindowMinimized`）。

### T45 A6-C 配置/数据安全 P-CFG-01..04 —— ✅ **已完成**（`docs/T45_CONFIG_SAFETY.zh_CN.md`）

`CFGHEALTHCHECK` **12 条** + 形态注入器 `a6-cfg-inject.sh`（7 形态，`readonly` 刻意移出矩阵 ——
`findFile(New)` 会污染仓库根）+ 引导修复腿（`.corrupt` 备份→重建）。**负控四条两轮逐位一致写死**：
ISOLATE_OFF(full@orig)⇒{CH-02,03,05,06}｜STATUS_OFF⇒{CH-03,04,05}（修复照常 —— "修复是正确性，
不是可观测性"）｜**REPAIR_OFF⇒rc=139 无输出（修复腿承重铁证）**｜DRIFT⇒{CH-07}。
🔴 **真缺陷 103**：健康读用了 plain `QSettings::IniFormat`，与引擎 `StelIniFormat` 对同一文件
**分歧 83 键**（771 vs 688）⇒ 已改 `StelIniFormat`，"写 config.ini 必须 StelIniFormat"升为纪律。
收口第一批曾把真实个人版 config 改写丢 81 键（零污染门抓住），已从 T42 备份恢复（用户值完整）；
触发方未决=移交，5 组对照实验见 `docs/evidence/2026-10-01-a6-configsafety/mac/ANOMALY-config-rewrite-2026-10-01.md`。TRAPS **103–106**。

### T46 A6-D 接口冻结 + 交接契约 —— ✅ **已完成**（`docs/A6_HANDOFF_FREEZE.zh_CN.md` + `docs/A6_INTERFACE_FREEZE.zh_CN.md`）

🔴 **冻结清单此前不可信**：生成器只取第一个 `^class` ⇒ 5 处假类名（连 `AppFacade` 都不在清单上）
+ 2 处纯 struct 头整段漏项 ⇒ 修（跳前向声明 + 列全部顶层类型 + 认 struct 提字段）。
新增 `--check` 冻结校验（对比范围止于实测读数之前 —— 陷阱 108）；t46 段重写为单一口径
（旧正则按文件计数且不剥注释，命中 10 个"禁止暴露 GL/Vulkan 句柄"**说明注释**文件 ⇒ 必假红且从未跑过）。
🔴 **零污染门量具自证修复**（陷阱 110）：`md5` 不在 PATH 时 pre==post 都是 `(missing)` 仍打 ✅
⇒ 两侧须 32 位 hex，否则 UNAVAILABLE+rc=1。负控 A–D 两轮逐位一致（含 D 证冻结校验独立承重）。
交接契约 7 项：5 完全达成 + 2 部分达成（残余明写：单命令固定场景快照、capabilities 具体项）。
TRAPS **107–110**。

### T47 A6-E 全量收口 + A-1.0 出口终审 —— ✅ **已完成**（`docs/A6_A1.0_EXIT_AUDIT.zh_CN.md`）

`tools/a6-verify.sh all`（T43 短窗 + T44 ×2 + T45 矩阵×2 + 负控 + T46 + 24 套件 + INTERACT + A2 +
DYN 双路 + S3 旧形态）+ `longrun` 长跑刷新帧桥统计 + **A-1.0 出口 7 条逐条终审**（7/7 达成）
+ 正式声明：**天空仍由旧 OpenGL 渲染，不得称天空已 Vulkan 化**（声明全文见出口审计文档）。

**收口实测（2026-10-01，最终二进制 md5=4cf585dd）**：
- `all` ⇒ **SCRIPT-RC=0 FAILED=0 PASS=12**：T43 短窗 11/11 全绿；T44 LIFECHECK 2/2 +
  P-LIF-01 进程级 ×100；T45 矩阵 12/12 + 资源链接 + CONFIGCHECK；T46 四条（冻结校验
  IF-CHECK OK）；24 套件回归全 rc=0（含 INTERACT 18/18）；**负控七条与台账逐位一致**
  （T44 三条：NOSIZE⇒{LF-03a,LF-04a}｜NOSETTLE⇒{LF-02b,03a,03c,04a,04c}｜HIDE⇒{LF-02f}；
  T45 四条：ISOLATE_OFF⇒{CH-02,03,05,06}｜STATUS_OFF⇒{CH-03,04,05}｜REPAIR_OFF⇒rc=139 无输出｜DRIFT⇒{CH-07}）；
  **config 零污染门 ✅（56d81fb 前后一致）**。
- `longrun` ⇒ **SL-C01..C11 全绿 VERDICT=PASS**：49.95 fps、p99 57.73 ≤60.06（**SL-C03 PASS**）、
  丢弃 0、内存斜率双口径 0.024/−0.114、窗口暴露 1800/1800、报告已刷新。
  **短窗红长跑绿的差异**留档 `T43 §6.1/§6.2`；#160 维持不变。
- 收口过程中抓到并修复 **2 个真缺陷 + 1 个判据口径缺陷**（TRAPS 112/113）：
  ① AC-12 端到端按键注入把用户 `viewing/flag_cardinal_points` 永久改写（判据不自净）
  ⇒ 新增 **AC-12b 自净**（沿同路径翻回原值 + 断言末态==初值）；
  ② SL-C07 单口径回归斜率在摆动噪声下两轮横跨门槛（0.756/1.931）⇒ **双口径交叉**
  （回归+端点都超限才判增长）；③ T43 短窗判据与管辖权裁定不一致（必假红）⇒ 改
  「允许集合」口径（11 条判据齐全 + 红项 ⊆ {SL-C03}，允许集合内红项如实打印）。

### 之后（按优先级）

**A6（回归与交接、接口冻结）**—— ~~A5 已清零~~ **A6 已收口（T43–T47）**。加固穿插：`LOC-04 (b)` 帧延迟量化 → `REPEATCHECK` 就绪门预算
→ 捏合后首击；另加 T33 移交的 `findLocations` 排序（照 T21 完全匹配优先）、
"负控 A 在 Windows 不复现"的定性、**T37-X4**（仿真冻结期间 fader 动画停摆）、
**T37-X1**（夜视翻转引起 5 个工具栏按钮重绘，纯性能线索）、
**`NightModeCheck` 的噪声口径并入 T39 统一**（⚠️ **顺带项，仍未做**，风险有界）。
~~DYN 停摆根因~~ **已结**（T41-D：全屏应用遮挡 ⇒ occluded ⇒ 场景图停摆，TRAPS 93；
不是代码问题，无产品动作）。
**W-T37 / W-T38 / W-T39 / W-T40 / W-T41 Windows 复验 —— ✅ 已完成**（2026-10-01「尾巴攻坚」，
报告 `docs/WT37_41_WINDOWS_VERIFY.zh_CN.md`，证据 `docs/evidence/2026-10-01-w-t37-41/`）。
五套件全部搬上 Windows 跑通：`NIGHTCHECK 6/6`、`DISPLAYCHECK 14/14`、`SHORTCUTCHECK 15/15`、
`HELPCHECK 17/17`（各 ×2 轮逐位一致，与 mac 一致）；`HIDPICHECK 10/11 INCONCLUSIVE` ⇒
**唯一 FINDING = `HP-00` 噪声底门**（Windows `0.178%` vs mac `0`），已两重判别
（换后端 Vulkan ⇒ mac 差异 `0`；换时间尺度 2500ms ⇒ `0.185%` 同量级）定性为
**渲染非确定/有持续变化源（平台事实）**，影响面仅像素级子判据 `HP-09`。
过程抓出 **7 条仪器/判据缺陷**（首跑 3 红项 `SC-13`/`HC-05`/`HC-13` + 4 条脚本缺陷，
**全是仪器的病、无产品缺陷**；其中"负控只设断开关没设主开关"让**整套负控测空气**）
⇒ 固化 `TRAPS.md` **114–120**。
🔴 **方法论发现**：mac 侧验收实际跑在 `STELQUICK_GRAPHICS_API=metal`（代码自称"非验收配置"），
Windows 侧原生 Vulkan ⇒ 像素级跨平台对照带后端混淆变量（报告 §4.3，已列移交项）。
**#159** 恢复 Q-WIN-01..06 窗口交互套件进回归 —— **✅ 完成**（`tools/qwin-check.sh`，
正题 6/6 + 负控 1/1，已接入 `a6-verify.sh qwin` 并并入 `regress`）。
**#160** 二分 SL-C03 尾部膨胀根因（**计划二开工前必做**，留下的**唯一**欠账）。
**下一步主线 = 计划二（原生 Vulkan 天空）**，入口契约见 `docs/A6_HANDOFF_FREEZE.zh_CN.md`。



---

## 7. 这份快照要不要更新

每完成一个任务，**只改 §1 状态列、§2 表格、§3 判据数**三处即可；
§5 的账目只增不删（除非真结清）。
