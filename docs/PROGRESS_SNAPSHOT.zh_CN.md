# 进度总览（截至 2026-09-29 晚，T32 后盘点）

> 口径基线：`main @ 3f7b4a0`（工作区干净、与 `origin/main` 逐字节同步）。
> 环境口径与 T17–T32 一致、**刻意不换**：macOS + Metal（`VK_DRIVER_FILES=MoltenVK`、
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
| **A4 核心交互页面（A-alpha）** | 工具栏、搜索、天体信息、时间、**地点**、拖动缩放、焦点管理 | 🟡 **7 项里 6 项完成，1 项未做**（只差**真实工具栏**，见 §2） |
| A5 设置与完整个人版 UI | A-1.0 全部页面、配置迁移、夜视、错误页 | ⬜ 未开始 |
| A6 回归与交接 | 全量回归、帧桥统计、资源打包、接口冻结 | ⬜ 未开始 |
| A7 平台适配（独立包） | 鸿蒙真机探测 / 打包分发 / Win·Linux 顺手验证 | ⬜ 前置 A6 或真机 |

---

## 2. 🔴 A4 逐项核对：`A-alpha` 尚未真正达成

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
| **工具栏** | ❌ **仍是占位** | `MainWindow.qml:171` 自己的注释：`// 页头：A2 开发期的临时页切换器。A4 起由真实工具栏取代，届时删除本行。` —— 现在仍是 4 个跳页按钮（`navDiagButton` / `navSkyButton` / `navSearchButton` / `navTimeButton`） |

**结论（T33 后更新）**：`地点` 这一项**已由 T33 补齐**（本节表格上一版把它记为"❌ 完全缺失"，
现已改为 ✅）。A4 的 7 项里 **6 项完成**，**只剩 `工具栏`**（`MainWindow.qml:171` 的临时页切换器
仍待 T34 取代）。

⇒ 计划文档 §9.1 / §9.4.x 多处写的"A-alpha 达成"仍**超前**，正确读法是
**"A4 主体完成，真实工具栏待补（T34）"**。**在 T34 落地前，A-alpha 出口判据第一条
（"标'必须'的 A-alpha 功能全部可操作"）不成立。**

> 另注：A-alpha 表里标"基本开关 / 基础"的项（星座线·名称、网格、地景、大气；亮度·星等、
> 投影、主题·夜视、高 DPI）**没有 QML GUI**。引擎 `StelAction` 经 `ActionRouter.routeKey`
> 键位透传**可触达**，但"开关状态必须与引擎双向同步"（§3 说明列）没有 UI 承载面。
> 归 A4 收尾或 A5，需在下一步显式决策。

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
| （探针）`LOCPROBE` | 只报读数、**不打 PASS** | `STELQUICK_LOC_PROBE` | T33-A 数据面探针（地点库规模 / `locationForString` 语义 / `isValid` 边界 / 时区联动） |

一键复跑：`tools/t16-verify.sh` … `tools/t33-verify.sh`（每个任务一个，用法 `all N`；
**`t33-verify.sh` 起含 `S3` 段**：旧宿主 `stellarium` 的进程内自检 `STELA3_CHECK`，
8 条判据、要求 `VERDICT=PASS`）；
Windows 侧 `tools/windows/wt29…wt33-*.ps1`（`wt33-launch.ps1` 是给 `schtasks /it` 用的启动器，
负责注入 `T33_PROBE_EXPECT`）。

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
| 时间链路脱钩 | HostDriven 帧泵 **400ms 推 0.61 天**（≈1.5 天/秒）与 `getTimeRate()` 读数 10 **脱钩** | T27 发现；**待定性**（若属设计需文档说明，否则独立缺陷） |
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

### T34 真实工具栏 + 显示开关（A4 真收口，**现在只剩这一项**）

- 取代 `MainWindow.qml:171` 的临时页切换器；显示开关（星座线·名称 / 网格 / 大气 / 地景）
  优先走 `ActionRouter.trigger(<引擎 action id>)` 透传 —— **可能零 C++ 改动**，
  但需先盘点引擎 action ID 台账（顺带履行 A4 计划里的"清点旧 action"要求）。
- 依赖：无硬依赖，可与 T33 并行或串行。

### T35+ A4 线索类（择机）

时间链路脱钩（影响时间类判据口径可信度，建议优先）→ `LOC-04 (b)` 帧延迟量化 →
捏合后首击 → DYN 停摆根因。

### 之后

A5（设置与完整个人版 UI：夜视、配置迁移、错误页）→ A6（回归与交接、接口冻结）。

---

## 7. 这份快照要不要更新

每完成一个任务，**只改 §1 状态列、§2 表格、§3 判据数**三处即可；
§5 的账目只增不删（除非真结清）。
