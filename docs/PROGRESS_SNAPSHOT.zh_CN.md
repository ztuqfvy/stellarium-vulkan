# 进度总览（截至 2026-09-30 傍晚，**T38 后**盘点）

> 口径基线：T34 收口提交 `9b04cdd` + T35 提交 `d68cc32` + W 支线 + **T36（A5 第一项）**
> + **T37（A5 第二项：夜视闭环）** + **T38（显示参数：亮度/星等 · 视场 · 投影）**。
> 上一版快照基线是 `9b04cdd`（T34 收口）；T33 见 `docs/T33_LOCATION_PAGE.zh_CN.md`，
> **T34 见 `docs/T34_TOOLBAR.zh_CN.md`**，**T35 见 `docs/T35_TIMELINK.zh_CN.md`**，
> **W-T34/W-T35 跨平台复验见 `docs/WT35_WINDOWS_RECHECK.zh_CN.md`**，
> **T36 配置目录隔离见 `docs/T36_CONFIG_ISOLATION.zh_CN.md`**，
> **T37 夜视闭环见 `docs/T37_NIGHTMODE.zh_CN.md`**，
> **T38 显示参数见 `docs/T38_DISPLAY.zh_CN.md`**。
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
| **A5 设置与完整个人版 UI** | A-1.0 全部页面、**配置迁移**、夜视、错误页 | 🔄 **进行中**（**T36** 配置隔离/播种 + **T37** 夜视闭环 + **T38** 显示参数 完成；余 T39–T42，见 §6） |
| A6 回归与交接 | 全量回归、帧桥统计、资源打包、接口冻结 | ⬜ 未开始 |
| A7 平台适配（独立包） | 鸿蒙真机探测 / 打包分发 / Win·Linux 顺手验证 | ⬜ 前置 A6 或真机 |

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
> **T38** 补上亮度·星等 / 视场 / 投影 —— **只剩高 DPI**（T39）。
> 这些项归 A5 的设置页，**不在 A-alpha 出口第一条的"必须"清单里**。

---

## 3. 判据套件清单（当前真值）

| 套件 | 判据数（最后一号） | 触发 env | 说明 |
|---|---|---|---|
| `A2CHECK` | 12 探针逐像素 | `STELQUICK_A2_CHECK` | 静态图管线；macOS 走 Metal（验收目标 Vulkan 受上游缺陷阻塞） |
| `DYNCHECK` | 7（D1-C01..C07） | `STELQUICK_DYN_CHECK` | 双生产者：`STELQUICK_DYN_PRODUCER=engine\|test` |
| `ACTIONCHECK` | 11（AC-01..AC-11） | `STELQUICK_ACTION_CHECK` | 命令单点 + 速率贯通 + 幂等 |
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

一键复跑：`tools/t16-verify.sh` … **`tools/t38-verify.sh`**（每个任务一个，用法 `all N`；
**`t33-verify.sh` 起含 `S3` 段**：旧宿主 `stellarium` 的进程内自检 `STELA3_CHECK`，
8 条判据、要求 `VERDICT=PASS`；**`t34-verify.sh` 含四组负控 + 探针 + 10 套件 + S3 + A2 + DYN**；
**`t35-verify.sh` 含三组负控 + 探针 + **11 套件**（多了 `toolbarcheck`）+ INTERACTCHECK + S3 + A2 + DYN**；
**`t36-verify.sh` 含 S1/S2 两场景 + 两组负控 + **12 套件**（多了 `timelinkcheck`）
+ INTERACTCHECK + A2 + DYN 双路；⑤ 收尾做"**合流形态**全程原目录零改动"核对，
⑥ **把 S3 旧宿主挪到核对之后单独跑**（写原版目录是它应有的行为，见 §6 T36 血泪②）**，
`core N` 段即含以上全部）；**`t37-verify.sh` 含正题×3 + 负控×3 + **13 套件**（多了 `nightcheck`）**；
**`t38-verify.sh` 含正题×3 + **两组负控×2** + **14 套件** + INTERACTCHECK + S3 + A2 + DYN 双路**。
Windows 侧 `tools/windows/wt29…wt33-*.ps1`
（`wt33-launch.ps1` 是给 `schtasks /it` 用的启动器，负责注入 `T33_PROBE_EXPECT`）。

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



### 之后（按优先级）

`LOC-04 (b)` 帧延迟量化 → `REPEATCHECK` 就绪门预算 → 捏合后首击 → DYN 停摆根因；
另加 T33 移交的 `findLocations` 排序（照 T21 完全匹配优先）、
"负控 A 在 Windows 不复现"的定性、**T37-X4**（仿真冻结期间 fader 动画停摆，
NC-05 的有界容忍来源）、**T37-X1**（夜视翻转引起 5 个工具栏按钮重绘，功能正确、
纯性能线索）、**`NightModeCheck` 的噪声口径并入 T39 统一**（它仍持"占比<0.1% ∧ Δ≤2"
的旧式；风险有界 —— NC-02 是环境门，误触退化成 rc=6 而非 FAIL）。

**A5 剩余**（T38 之后）：**T39 高 DPI + 渲染诊断** → T40 快捷键编辑 →
T41 帮助/版本/许可证 → T42 错误页 + "未支持项"清单。
再往后：A6（回归与交接、接口冻结）。**W-T37 / W-T38 Windows 复验待排期**
（T33/W-T35 已证明跨平台复验能逼出真缺陷）。

---

## 7. 这份快照要不要更新

每完成一个任务，**只改 §1 状态列、§2 表格、§3 判据数**三处即可；
§5 的账目只增不删（除非真结清）。
