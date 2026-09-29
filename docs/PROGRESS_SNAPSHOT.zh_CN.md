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
| **A4 核心交互页面（A-alpha）** | 工具栏、搜索、天体信息、时间、**地点**、拖动缩放、焦点管理 | 🟡 **7 项里 5 项完成，2 项未做**（见 §2） |
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
| **地点** | ❌ **完全缺失** | `src/app/` 与 `src/ui/` 内**无任何地点写入 API**；`AppFacade` 无 lat/lon/alt/名称接口；`MainWindow` 四页（diag/sky/search/time）**无地点页**。代码里 `getCurrentLocation()` 只被**读**过一处用途：家园行星守卫（`AppFacade.cpp:857`、`LocateCheck.cpp:497`） |
| **工具栏** | ❌ **仍是占位** | `MainWindow.qml:171` 自己的注释：`// 页头：A2 开发期的临时页切换器。A4 起由真实工具栏取代，届时删除本行。` —— 现在仍是 4 个跳页按钮（`navDiagButton` / `navSkyButton` / `navSearchButton` / `navTimeButton`） |

**结论**：`A4` 在计划文档里被记为"功能缺口已收口（A-alpha 达成）"（§9.1 / §9.4.x 多处），
但按**测试文档自己的出口判据**，`地点` 是标"必须"的项且**一行未做** ⇒
**该结论超前，应改为"A4 主体完成，地点页与真实工具栏待补"**。
（A4 状态行列举的"余下"只有加固/已知问题，未把这两项计入 —— 记录层面的漏项。）

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

一键复跑：`tools/t16-verify.sh` … `tools/t32-verify.sh`（每个任务一个，用法 `all N`）；
Windows 侧 `tools/windows/wt29…wt32-*.ps1`。

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

### T33 地点页 —— **推荐先做**

- **理由**：A-alpha 出口判据里**唯一完全缺失**的"必须"项；不做，A-alpha 不成立，
  A5（全部页面）也起不来。改动面清晰、无外部阻塞。
- **改动面**：`AppFacade` 地点面（读：名称 / 经纬度 / 高度 / 所属行星 / 时区联动；
  写：单点入口转发 `StelCore::moveObserverTo(StelLocation)`）+ 地点列表来源 +
  `LocationPage.qml` + `LocationCheck` 判据 + `tools/t33-verify.sh` + Windows 复验。
- 🔴 **先做探针（不确定性最高处）**：`StelLocationMgr` 的城市库在**合流 bundle 布局**下能否
  加载 —— 这是 **T24 翻译从未加载的同款风险**（`getLocaleDir` 三候选全不命中，潜伏 14 个
  任务才被逼出来）。地点库同样是数据文件依赖，**先证它加载得起来再写 UI**。
- **判据设计**：成对（写地点 ⇒ **可验算的物理量**随动：地平高度角 / 时角 / 地平线；
  不写则纹丝不动）+ 判别对照 + 非法值（纬度 >90、高度荒谬）拒绝且**无副作用** +
  写入单点入口。⚠️ 注意 1847-12-01 之前引擎 `getUTCOffset` **不看时区名**、按经度算 LMST
  （`StelCore.cpp:1655`）⇒ 别把时钟挪到古代再测地点×时区联动。

### T34 真实工具栏 + 显示开关（A4 真收口）

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
