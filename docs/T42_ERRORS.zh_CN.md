# T42 — 状态与错误页 + "未支持项"清单（A5 收口项）

> 证据：`docs/evidence/2026-10-01-t42-errors/mac/`｜脚本：`tools/t42-verify.sh`｜前置：T36（配置隔离）、T39（诊断页）、T41（宿主接管）

## 1. 一句话结论

**A-1.0 第二格与 A5「错误页」就此收口**：资源路径表 + 错误页（含真实失败腿）+ 未支持项清单 + 4 个老窗口动作改为"明确提示"，交付判据 `ERRORCHECK`（A 组 15 条 / B 组 5 条 / 负控三腿正交）。**判据/冒烟共抓出 2 处真缺陷（qFatal 崩溃路径、⌥B 观测列表无人接管）与 1 处判据自身缺陷（自净不彻底写穿真实配置）**，均已修。

## 2. T42 的范围来路（三处文档口径的并集，不是发明）

| 来路 | 原文 | 本任务承接 |
|---|---|---|
| A-1.0 第二格 | `资源路径、错误提示、渲染诊断、配置保存｜最小｜必须` | 前两项（后两项 T36/T39 已交付） |
| A5 | `A-1.0 全部页面、配置迁移、夜视、错误页` | 「错误页」 |
| 开发指导 `:230` | 旧 UI 禁止隐式回退；**未支持项有清单和明确提示** | 未支持项清单 + 4 动作改提示 |
| A-1.0 末行 | 高级天文计算、脚本控制台、全部插件设置**不做**；**不静默打开旧对话框** | 清单内容 + 接管改提示 |

## 3. T42-A 探针（`ErrorProbe`，57 行读数，`probe-error-mac.txt`）

**推翻自己 1 处假设、证实 T41 1 处推断、发现 T41 1 处遗漏：**

1. 🔴 **推翻**："StelDialog 弹窗只动内存" —— 错。弹窗后把尺寸写进 `QSettings` 的 `DialogSizes/<Name>`（实测 `+DialogSizes/AstroCalc`、`+DialogSizes/ObservingList`）⇒ 探针加 S11 自净 + 手工回滚历史污染。
2. ✅ **证实**：未接管动作 `trigger()` 真的会弹老窗口 —— F10 天文计算 QWidget 11→708（**Δ697**）+ 可见 QDialog Δ1；⌥B 观测列表 →766（**Δ58**）。
3. 🔴 **T41 遗漏**：Windows 组真身 **10 个成员**（9 个 `actionShow_*_Window_Global` + **`actionShow_ObsList_Window_Global`（⌥B 观测列表）**—— T41 从未提及）。T41 接管 6 个、F2/F10/F12 刻意不接管 ⇒ 未接管共 **4 个**。
4. **qFatal 前置缺陷**：`StelFileMgr::init()` 用户目录不可创建时 `makeSureDirExistsAndIsWritable` 抛 `std::runtime_error` ⇒ **`qFatal`**（`src/core/StelFileMgr.cpp:90-92`）⇒ SIGABRT，任何 UI 都没机会出现。
5. **可注入性**：`STEL_USERDIR`（`StelFileMgr.cpp:69-77`）指向"父路径不是目录"的位置（如 `/dev/null/xxx`）即得**真实**失败 —— 判据用真失败，不造假桩。
6. `getScreenshotDir()` / `getObsListDir()` 当前形态返**空串**（惰性创建）⇒ 路径表必须如实区分 `unset` 态，不能一律画绿。
7. `fileLocations` 是 private ⇒ 只能用公开 API `findFileInAllPaths()` 作可观测代理。

## 4. T42-B 产品

### 4.1 `ErrorModel`（只读数据面，`src/app/ErrorModel.{hpp,cpp}`）

- **与 T39 诊断页分工**：诊断页 = 实时性能面（每帧变的连续量，Timer 轮询）；本页 = **静态健康面**（点"重新检查"才重算）。
- **状态表 4 行**：引擎引导 / 图形后端 / 配置目录 / 天空帧通路。引导结果由 main.cpp 注入（`setBootResult`，**失败原因原文照收**，不重新措辞）；后端判定**只有一份**（T41 教训：`BackendInfo` 不自判）。
- **资源路径 8 行**：用户目录/程序资源/缓存/截图/观测列表/语言资源/配置文件/日志文件；四态 `ok/readonly/missing/unset`。
- **未支持项 7 条**（白名单式 `kUnsupportedWindows[]`，不从注册表自动发现）：
  1. 设置（F2）—— A-1.0『不做』插件设置
  2. 天文计算（F10）—— A-1.0『不做』高级天文计算
  3. 脚本控制台（F12）—— A-1.0『不做』脚本控制台
  4. 观测列表（⌥B）—— 个人版未纳入（**T41 漏掉的成员**）
  5. 全部插件设置 —— 注册表里数出六组插件动作总数
  6. 快捷键手势（legacy 10 条）—— T41 已定案
  7. 字体/按钮缩放两控件 —— T39 探针证零作用，不搬
  - 显示名与键位**从注册表真源取**（`getText()` + `getShortcut().toString(NativeText)`）；注册表里找不到就显示警告字段（清单与真源漂移的诚实性标记）。
- `openPath(kind)` **白名单制**（log/config/userDir/installDir/cacheDir，未知 kind 拒收 —— T41 `openExternal` 先例）。

### 4.2 老窗口动作处置：接管 + 明确提示（解 T41 的两难）

T41 只接管 6 个有 QML 页的动作，F2/F10/F12 刻意不接管（无对应页 ⇒ 接管 = "按了没反应"）。T42 解掉两难：**接管 + 弹「未支持」对话框** —— 同时满足"不静默打开旧对话框"、"未支持项有明确提示"、且不落入 T41 拒绝接管的理由。`MainWindow.qml` 增 `unsupportedActions` 表 + `unsupportedDialog`（含"前往状态页"按钮）。

### 4.3 qFatal → 可操作的失败

`LiveSkyRuntime::boot()` 在 `StelFileMgr::init()` **之前**加用户目录可创建性预检（`STEL_USERDIR` 显式设置时检 raw + `-quick` 两级）；失败走 `setBootResult` ⇒ **错误页展示真实原因**，startPage 切到 error。main.cpp 的 liveEngine 失败处理同步改走错误页通道（不再直接 `return 8`）。

### 4.4 页面与接线

`ErrorPage.qml`（`objectName:"errorPage"`）：**操作行放页首**（陷阱 92 教训：跳转按钮沉底用户点不到）；三块（状态/路径/未支持）全用 **Repeater** 立即创建（陷阱 45/46 家族教训）；`Toolbar.qml` 加 `navStatusButton`（原 8 个 objectName 一个没动）。

## 5. T42-C 判据：ERRORCHECK（两条腿同一二进制）

**A 组 15 条**（引擎可用）：EC-01 路径表 8 行｜EC-02 userdir `-quick` 后缀｜EC-03 cfg/log 就位｜EC-04 状态表无 error 行｜EC-05 无错不喊狼来了｜EC-06 未支持 7 条｜EC-07 注册表交叉验算 4/4｜EC-08 接管表 10 个｜EC-09 4 动作 trigger ⇒ Δ0+提示 4/4+配置零变化｜EC-10 判别对照（撤销接管 ⇒ Δ697 真弹）｜EC-11 UI 行 4/8/7｜EC-12 操作按钮几何可达｜EC-13 剪贴板诊断｜EC-15 openPath 白名单闸｜EC-14 复原。

**B 组 5 条**（失败腿）：EC-01 hasError+headline｜EC-02 detail 与注入原因一致｜EC-03 错误块 UI 可见｜EC-04 引擎引导行=error｜EC-05 失败时路径仍可展示（排查线索）。

**负控三腿**（期望值实跑出来再写死，两轮逐位一致）：

| 负控 | 红项 | rc | 域 |
|---|---|---|---|
| A `STELQUICK_ERROR_TAKEOVER_OFF=1` | EC-08,09,14 | 5 | 接管域 |
| B `STELQUICK_ERROR_PATHS_OFF=1` | EC-01,02,03,04,11 | 5 | 路径域 |
| C `STELQUICK_ERROR_UNSUPPORTED_OFF=1` | EC-06,07,11 | 5 | 清单域 |

正交：A∩B=∅、A∩C=∅、B∩C={EC-11}（共享 UI 行判据，note 里子段可分）。⚠️ `STELQUICK_ERROR_OPEN_OFF` **不是负控**（无判据因它变红），是 EC-15 受理半边的无副作用布场开关。

## 6. 判据抓出/修掉的东西

1. 🔴 **真产品缺陷**：`qFatal` 崩溃路径（§4.3）—— 失败腿证实修后不再 SIGABRT（rc=0、5/5）。
2. 🔴 **T41 遗漏**：⌥B 观测列表无人接管 —— 收编进接管 + 提示。
3. 陷阱 85 **第三次重演**：`mark()` 只写 details 没写台账 ⇒ 14 条全 PASS 却 `VERDICT=FAIL(0/14)` ⇒ 按 note 首词回写台账 + `finish()` 报"未记账 id"自证。
4. EC-13 首轮红：`QMouseEvent` localPos 传了按钮局部坐标，`QQuickWindow` 用窗口坐标 ⇒ 点击落在左上角 ⇒ 两处都传 scenePos（T40 ShortcutCheck 同款正确做法）。
5. 🔴 **判据自净不彻底写穿真实配置**：EC-09 首版只判不净，负控 A 下 EC-10 早退无人代还 ⇒ `DialogSizes/Configuration 765,605→770,656` 残留真实 config.ini ⇒ EC-09 补"本步自净"+ 脚本加 **config 零污染门**（全程 md5 前后一致）。附带教训：`grep -A 12` 截断显示差点让人误判"4 个键被删"—— 损失面必须全文件 diff 定。

## 7. 验证与回归（mac，`tools/t42-verify.sh all`，2026-10-01 11:05）

**SCRIPT-RC=0 FAILED=0**（`rc-summary.txt`）：

- **正题 ERRORCHECK ×3**：rc=0、判据 15/15、红项空 ×3。
- **失败腿 ×2**（`STEL_USERDIR=/dev/null/xxx` 真实失败）：rc=0、判据 5/5 —— 引导失败被预检接住（**不再 qFatal/SIGABRT**），错误页展示真实原因。
- **负控 A/B/C 各 ×2**：红项逐位一致且恰中本域（§5 表）；判别对照的 QWidget Δ=399+697+60+58=1214 与探针逐项对上。
- **三腿正交定位成立**；**config 零污染门**全程 md5 一致（与探针备份 `/usr/bin/diff` 零差异）。
- **回归 24 项全 rc=0**：18 自检套件（timecheck…helpcheck）+ INTERACTCHECK 18/18 + A2 + **DYN 双路 3/3+3/3** + S3 旧宿主。

### 7.1 收口轮抓出的三件仪器/口径事（都已修，判据一并受益）

1. **脚本 `red_ids` 正则缺陷**：`ERRORCHECK: +\[FAIL\]`（要求冒号后空格）不匹配 `ERRORCHECK:[FAIL]`（无空格）⇒ 负控红项提取恒空（rc/判据计数都对、红项 `[]`）——首轮批跑 10 处脚本层红全是它。**教训**：解析器的格式假设要先对真实输出验一遍（陷阱 63 同族）。
2. **HC-14 口径校正**（TRAPS 11 镜像）：T41 预期"接管 6 + F2/F10/F12 刻意未接管"；T42 接管这 4 个 ⇒ HelpCheck 预期改 10 全量。修后 helpcheck 17/17。
3. 🔴🔴 **布场昼夜确定性**（TRAPS 97，白天首次批跑逼出，TRAPS 80 深层变体）：开机 JD=墙钟 ⇒ ①白天大气关满屏日照卫星（冻结仿真照样动，DP-00 Δ220 vs 夜间 Δ1）②跳 JD 后瞳孔适应 log 律爬秒级（HP-00 43%/Δ20）③白天大气开 = 上游帧纯渐变**无标签**（HP-09 效应 4.054%→0.001，探针标定值是夜里测的）。**帧转储肉眼物证**（`STELQUICK_PROBE_DUMP_DIR`）。治法：DisplayCheck/HiDpiCheck 布场钉**固定夜 JD**（2461000.4167，记原值收尾还原）+ 布场步 delayAfter 6500ms 给适应律收敛。修后白天单跑 hidpi 12/12、display 14/14，最终批跑全绿。

## 8. 下一步

A-1.0 全部格子与 A5 清零 ⇒ **A6（回归与交接、接口冻结）**。移交项不变：T37-X4（冻结期 fader 停摆）、T37-X1（夜视翻转 5 按钮重绘）、`NightModeCheck` 噪声口径、T33 `findLocations` 排序、W-T37..W-T41 Windows 复验、`LOC-04(b)` 帧延迟量化。
