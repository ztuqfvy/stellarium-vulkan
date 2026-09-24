# T19 交付报告 — 改时间（A4 固定流程的"改时间环"）

- 任务：A4 固定流程 I-REP-02「开机 → 搜月球 → 定位 → **改时间** → 返回」的第三个环。
  A3 正题（T14–T17）与"定位环"（T18）已于 2026-09-24 收官。
- 日期：2026-09-24
- 证据：`docs/evidence/2026-09-24-t19-time-ring/`（一键复跑 `tools/t19-verify.sh`）
- 一句话结论：**AppFacade 时间命令面 + TimePage + 两套自检（C++ 侧 14/14、UI 端到端 13/13）
  全部落地；过程中用最外层注入的 UI 判据抓到一条用户可见的真实缺陷并修复，并附反向对照证明
  该判据真能失败。**

---

## 1. 任务与通过条件

A-alpha 对时间的要求是明确的：**"区分 UTC、时区、显示历法；不重新发明天文时间算法"**，
且 I-DYN-02 要求"改日期后星空位置变化可验证"。据此定的自定通过条件：

| # | 条件 | 为什么这么定 |
|---|---|---|
| 1 | 能按**本地日历**写时刻（年/月/日/时/分/秒 六条路径），且**往返恒等** | "看起来写进去了"不是判据；必须逐字段读回相等 |
| 2 | 与**旧 `DateTimeDialog` 的换算逐位一致** | "不重新发明算法"的机械证据，不是口头承诺 |
| 3 | 本地 ↔ UT 的双向换算**不搞反**（历史经典 bug：输入 12:00 回显 20:00） | 偏移方向错一次就够了；要有往返判据钉住 |
| 4 | 非法输入（13 月 / 32 日 / 25 时）被拒，**且时钟一个字节都不动** | "拒绝"必须无副作用，否则拒绝本身成了破坏 |
| 5 | 改时间后**星空确实动了**，且**可逆** | I-DYN-02 的直接要求；且必须能与"没动"区分开 |
| 6 | 写入**不被帧泵拽回**（T16 单一仿真时钟的保证要在写入路径上仍然成立） | 跳转若被下一帧覆盖，UI 会显示一个引擎并不认可的值 |
| 7 | **UI 接线是活的**：六个自旋框、应用/重置/现在、步进按钮、状态行绑定 | 见 §2.6；C++ 侧自检结构上测不到这些 |
| 8 | 全部判据自动化、一键复跑；回归零退化 | 与前几个任务同一纪律 |

范围外（明确移交 A4，§6）：显示历法（农历/儒略历切换）、MJD、时间速率 GUI、时区选择器。

---

## 2. 设计

### 2.1 先读旧实现：把"×6"钉死在旧对话框的六条写入路径上

计划里说的"改时间对话框 ×6"指的是旧 `src/gui/DateTimeDialog.ui` 里 `dateTimeTab` 的
**六个自旋框**（`spinner_year/month/day/hour/minute/second`），它们的提交路径是同一个
`makeValidAndApply()` → `newJd()` → `core->setJD()`。

新页照抄这个口径，但**只改一处**：旧对话框"改一个框就立刻写时钟"，新页改成**显式「应用」**。
理由不是审美，是**可测**：

> 自动应用没有干净的反向控制；而"改了框但没点应用 → 时钟必须不动"恰好是一条天然的**负控**。

这条负控在 UI 判据里就是 `UI-06`（§4.1b）。

### 2.2 写入路径只有一条，且唯一真源在引擎

```
QML 6 个自旋框 ──(onClicked「应用」)──> AppFacade::setLocalDateTime(y,m,d,h,mi,s)
                                            │
                                            ├─ ① QDate::isValid 范围校验（非法 → invalid-date，无副作用）
                                            ├─ ② StelUtils::getJDFromDate() 本地 → JD
                                            ├─ ③ cjd -= core->getUTCOffset(cjd)/24.0   本地 → UT
                                            └─ ④ setJulianDay(cjd)  ← **唯一绝对写入入口**
                                                     └─ core->setJD() → JD.first + simClock.jumpTo()
```

关键取舍：

- **`setJulianDay()` 是唯一的绝对写入入口**，`setLocalDateTime()` 只是它前面的日历/时区换算。
  这样"写入"这件事在代码里只有一个落点，判据也只需要盯一处。
- **超范围提前拒绝**：`StelCore::updateTime()` 会把 JD 钳到
  `[-71328212.5, 74769924.499988]`。越界时**提前返回 `out-of-range`**，
  好过"写进去、下一帧被悄悄改掉" —— 后者会让 UI 显示一个引擎并不认可的值，且没人知道为什么。
- **范围校验用 `QDate::isValid`**：引擎的 `getJDFromDate` 在 1582 年前的分支**不做校验**，
  会静默给出垃圾 JD。所以校验口径必须与它内部的一致（`y <= 0 ? y-1 : y`），
  但**不能**把校验交给它。
- **时区偏移取在校正前的 cjd 上**（`cjd -= getUTCOffset(cjd)/24`，而不是校正后再取）。
  这是旧对话框的口径，**照抄不"改进"** —— 两处差 1 小时以内的时区偏移只在 DST 边界日不同，
  不为它引入新的不兼容。

### 2.3 读侧：连续量轮询，离散状态才用属性通知

只读投影（`utcOffsetHours` / `localDateTimeText` / `utcDateTimeText` / `localDateTimeField`）
**刻意不做 `Q_PROPERTY`**：JD 每帧都在变，挂 NOTIFY 会让 QML 每帧重建绑定 → 刷屏。
与 `MainWindow.qml` 里 `fovLabel` 同惯例：**离散状态**才用属性通知（如 T18 的 `tracking`），
**连续量**一律 300 ms 轮询。

本地/UT **双日历并排暴露**，是为了让"区分 UTC、时区"这件事**可被看见**（A-alpha 的原文要求），
而不只是内部实现正确。

### 2.4 `julianDay()` 改读 `getSimClockJD()`（结清 T18 留下的移交项）

T18 移交时记过一条："`julianDay()` 改读 `getSimClockJD()`"。本轮结清：

- T16 之后 `simClock` 是**唯一真源**，而 `getJD()`（= `JD.first`）只在
  `StelCore::updateTime()` 里被从 `simClock` 同步 ⇒ **等于上一帧的快照**。
- 语义上"当前时刻"应当读真源。改完之后，QML 的 JD 投影与 `setJulianDay()` 的写入
  **读的是同一个量**，写入即刻可见，不再依赖帧的到达。

### 2.5 星空随动：判据必须成对，且"世界动了"要有物理量级

沿用 T18 的硬约束（孤立断言"夹角 ≈ 0"可以假绿：世界没动 / 视向锁在赤道坐标 / 定到了别的对象上）：

- **A 断言"世界确实动了"**：暂停态跳 0.25 天后目标 AltAz 变化 **> 5°**；
- **B 判别性对照"写回同一个 JD"**：变化必须 **< 0.05°**（证明 A 不是测量噪声）；
- **C 可逆性"跳回原时刻"**：夹角必须复原 **< 0.05°**（排除"改时间把状态写坏了"）。

三者缺一不可：只有 A 无法排除"任何操作都会让夹角乱动"；只有 B 无法证明 A 的量级有意义。

### 2.6 QML 接线必须**从最外层**验证：为什么另立一套 `TIME_UI_CHECK`

`TimeCheck`（`src/app/TimeCheck.cpp`）走的全是 `AppFacade` 的 C++ 公共 API。它证明得了
"换算与写入逻辑对"，但**证明不了**以下任何一条是活的：

```qml
onClicked: timePage.applyFields()          // 六个自旋框的 ×6 写入路径
onClicked: appFacade.setTimeNow()
onClicked: timePage.refill()
onClicked: ActionRouter.trigger("actionAdd_Solar_Day")   // 步进透传
color: appFacade.lastTimeRefusal === "ok" ? ... : ...    // 状态行绑定
```

名字写错、按钮没接上、绑定读到函数对象 —— `TimeCheck` 照样 14/14 全绿。项目为此付过学费：
T15 把 `Keys`（**`Item`** 的附加属性）挂在 `ApplicationWindow`（继承 `Window`）上，
挂载从未生效、QML 键盘路由整段是死代码，而 `AC-5` 抓不到（它**直接调** `routeKey`），
埋了两个任务才被人肉发现。

所以 T19 沿用 T18 的 `STELQUICK_UI_CHECK` 手法立了 `TIME_UI_CHECK`：

1. 按 `objectName` 找**真实 QML 控件**（找不到即接线/命名断了）；
2. 向 `QQuickWindow` 投递**真实鼠标按下/抬起**（不是 `emit clicked()`、也不是直接调 AppFacade）；
3. 断言**引擎状态精确改变**（JD 落到公式算出的那个值，不是"变了就行"）；
4. 反向读 QML 控件的**真实属性**（`SpinBox.value` / `Label.text`）证 C++→QML 绑定活；
5. 含一条**负控**（UI-06）。

**这一套首跑就抓到了一条真实缺陷**，见 §3 问题 ①。这就是它存在的意义。

---

## 3. 落地过程中发现并修掉的问题

### 问题 ①（🔴 用户可见的真实缺陷）`lastTimeRefusal` 缺 `Q_PROPERTY`，导致"写入成功 = 空白 + 红字"

`TimePage.qml` 的状态行：

```qml
color: appFacade.lastTimeRefusal === "ok" ? "#2e7d32" : "#c62828"
text:  appFacade.lastTimeRefusal === "ok" ? "时间已按写入生效。" : appFacade.timeRefusalText()
```

而修前 `AppFacade.hpp` 里只有：

```cpp
Q_INVOKABLE QString lastTimeRefusal() const;   // ← 纯方法，不是属性
```

在 QML 里，**只有 `Q_PROPERTY` 才会拿到字符串**；纯 `Q_INVOKABLE` 方法读到的是
**函数对象**，`=== "ok"` **恒为 false**。于是：

- 「时间已按写入生效。」这一支是**死代码**；
- 而 `timeRefusalText()` 在 token == "ok" 时**返回空串**；
- ⇒ **写入成功反而显示"空白 + 红字"**，用户会以为失败了。

T18 的 `lastLocateRefusal` 就是正确形态（`Q_PROPERTY ... NOTIFY`）。修法照它：

```cpp
Q_PROPERTY(QString lastTimeRefusal READ lastTimeRefusal NOTIFY lastTimeRefusalChanged)
```

并把 `setTimeRefusal()` 改成"值有变化才 emit"（与 T18 的 `setRefusal` 同纪律）；
计数仍在早退**之前**累加，不因"值没变"而漏计一次拒绝。

**反向对照**（`timeuicheck-negctrl-broken-property.txt`）：把该 `Q_PROPERTY` **临时注掉**后
重建重跑 ⇒ `UI-09a` / `UI-09b` **双双 FAIL**、**11/13**、**rc=10**：

```
UI-09a 写入成功后状态行文案=""（token=ok，要求非空且声称已生效）：FAIL
UI-09b 非法写入后状态行文案=""（token=invalid-date，不得再声称已生效）；JD 位移 0.00e+00 天：FAIL
```

UI-09b 的文案**也是空串**，因为**没有 NOTIFY 依赖，绑定根本不重算**，一直停在首次求值那一刻
的 `timeRefusalText()`（当时 token=ok ⇒ 空串）。一个对照同时把两个缺陷显形了。
恢复后 3/3 稳定 13/13。

### 问题 ②（⚠️ 我自己写的驱动器坑）`delayAfter` 挂错了步骤 ⇒ "世界没动"是**假失败**

首跑 TC-09/TC-11 FAIL（AltAz 变化 0.0000°、残差 85.6289°）。**根因在自检的驱动器，不在被测代码。**

驱动器的语义是"**跑完本步之后**等 `delayAfter` 毫秒"，所以"等引擎把新时刻算进去"的等待
**必须挂在写入步**上。我把它挂在了读取步上，于是写入 → 读取的实测间隔是 **0 ms**，
读到的是**上一个时刻**的星空。

定位手法（值得复用）：给每个探针加一行 `note`，打印

- `stopwatch.elapsed()`（相邻探针的**实测间隔**）
- 三个时间源：`simClockJD` / `core->getJD()` / `facade->julianDay()`
- 原始 AltAz 三分量

一眼就看出：**三个时间源都已同步到新值、而 AltAz 恒为"上一个 JD 的值"**，再对一眼
"写入 → 读取实测间隔 0 ms"，问题立刻收敛到驱动器。

> 顺带一条容易上当的事实：`StelCore::setJD()` 是**直接写 `JD.first`** 的（不需要帧），
> 所以"时间源已跳到新值"**证明不了**引擎重算过 —— 必须另找"帧有没有跑"的证据。
> 这也是修好后要求在判据行里写明"等了多少毫秒"的原因：否则下一次没人知道这个数是怎么来的。

修正后 3/3 稳定 14/14。

### 问题 ③（顺手修的既有缺陷，T18 页）`SearchPage` 的 refusal 文案缺依赖

`SearchPage.qml` 的状态行调用 `appFacade.locateRefusalText()`，但**没有读
`lastLocateRefusal`**。QML 只把"绑定里**实际读过**的属性"登记成依赖 ⇒ token 变化时那段
不会重算（refusal 文案停在旧值）。修法：在绑定里真的读一下 token 建立依赖。

T18 的 `regression-locate-uicheck.txt` 8/8 全绿，证明改动没破坏原有接线。

### 问题 ④（证据质量）`const char*` 中文字面量被 `fromLatin1` 解码成乱码

`TimeCheck.cpp` 里两张表（`kNames` / `kWhat`）是 UTF-8 的 `const char*`，取值处我写了
`QString::fromLatin1(...)`，于是 `常规日期` 显示成 `å¸¸è§æ¥æ`、`闰日` 显示成 `é°æ¥`。
源码本身是干净 UTF-8，问题只在解码那一步。改成 `QString::fromUtf8` 并在表旁注明取值口径。
（这是本项目第 N 次踩 Unicode 解码口径，见记忆里的 PS 5.1 `*>>` 双编码那条同族。）

### 问题 ⑤（顺手修的既有缺陷，T18 页）自检报告每条判据打印两遍

T18 已修掉 `LocateCheck` 的"`qDebug()` + `details.append()` 双写"；本轮的 `TimeCheck`
从一开始就按"只 append、由 `main.cpp` 单点打印"写，`timecheck-mac.txt` 里每条判据恰好一次。
`AppFacadeCheck` 保持原样以维持跨任务可比性（T18 已记明）。

---

## 4. 验收

### 4.1 自检：`timecheck` 14/14 PASS（rc=0，连跑 3 次稳定）

> 下表读数取自 **2026-09-24 15:28 的复核跑**（`docs/evidence/2026-09-24-t19-time-ring/timecheck-mac.txt`）。
> `TC-12`/`TC-13` 含墙钟量，逐轮会有小幅抖动（如 TC-13 在 `2.00e-04` ~ `1.00e-04` 之间），
> **只有 OK/FAIL 是稳定量**；下文凡出现具体数值的判据均同此。

| 判据 | 实测读数 |
|---|---|
| TC-01/02/03 往返恒等（常规 / 跨年边界 / 闰日） | 写什么读回什么，三组全等 |
| TC-04 判别性：三组读回**互不相同** | 三组各不相同 |
| TC-05 与旧对话框公式**逐位一致** | 2461307.770833333 vs 同值，差 **0.000e+00**（容差 1e-9） |
| TC-06 UTC 往返（偏移 +8.00 h） | ΔJD = **0.000e+00** |
| TC-07 `utcOffsetHours` 与 `core->getUTCOffset` 一致 | 8.000000 vs 8.000000 |
| TC-08 非法输入被拒且无副作用 | 13 月 / 32 日 / 25 时 → `invalid-date`；ΔJD = 0 |
| TC-14 超范围 JD 被拒 | `1e12` → `out-of-range`；ΔJD = 0 |
| **TC-09 世界确实动了**（暂停态跳 0.25 天后等 1200ms） | **85.6289°**（门槛 > 5°） |
| **TC-10 判别性对照**（写回同一 JD） | **0.0000°**（上限 < 0.05°） |
| **TC-11 可逆**（跳回原时刻） | 残差 **0.0000°** |
| TC-12 "现在"（含判别性对照） | 跳开后差 46287.31 天；`setTimeNow` 后 0.000e+00 |
| TC-13 写入不被帧泵拽回（运行态） | JD 差 0.5600 vs 目标+漂移差 **1.00e-04**（容差 3.2e-2） |

**TC-09 的 85.6289° 是可验算的，不是"不为零就算过"**：0.25 天 = 90° 时角；天体在 90° 时角
位移下的角距 = `arccos(sin²δ)`。反解 ⇒ `sin²δ = 0.0763` ⇒ δ ≈ **16.0°**，正是月球在
2026-09-24 附近的赤纬量级。

> 对照：`LocateCheck` 同样跳 0.25 天却读到 **127.74°** —— 因为它**不暂停时钟**，
> 1200 ms 等待里时钟又走了 0.12 天（合计 0.37 天，>90° 时角）。两个数各自自洽。

### 4.1b UI 层端到端自检：`timeuicheck` 13/13 PASS（rc=0，连跑 3 次稳定）

| 判据 | 实测读数 |
|---|---|
| UI-01 锚点可寻且可见 | 6 个自旋框 6/6；5 个按钮 5/5；状态行 + JD 行均在 |
| UI-02 C++ → QML **回填**绑定活 | C++ 写 2030-06-15 09:30:00 → 框内同值 |
| UI-03 **判别性**：换写不同时刻 | 改写 1999-01-01 00:00:00 → 框内同值（排除"写死"） |
| UI-04 自动回填被 `dirty` 守卫挡住 | 等 500ms（≥400ms 周期），框内仍是用户改的 2011-11-11 11:11:11 |
| **UI-05 真实点击「应用」→ JD 落在公式值上** | JD = 2455876.632766203，差 **0.00e+00** 天（容差 1e-6） |
| **UI-06 负控**：改框**不点应用** → 时钟不许动 | JD 位移 **0.00e+00**（<1e-12）；框内仍是 2022-02-22 22:22:22 |
| UI-07 「重置」只回填、**不写时钟** | 回到引擎当前值；JD 位移 0.00e+00 |
| UI-08 「现在」→ JD = 系统时刻 | 差 5.81e-06 天；跳转前 46287.30 天（判别性成立） |
| **UI-09a** 成功后状态行声称已生效 | 「时间已按写入生效。」← **抓问题 ① 的那条** |
| UI-09b 非法写入后改口且时钟未动 | 「日期/时间字段超出范围（如 13 月、32 日、25 时）——已忽略。」；ΔJD = 0 |
| UI-10a JD 投影与 C++ 同源 | "2461307.810757"，差 5.5e-08 天 |
| UI-10b 时钟 +365.25 天后投影**跟着变** | 差 0.00e+00（不是常量） |
| UI-11 步进透传链活着（QML→ActionRouter→引擎） | 真实点击「+1 天」→ 位移 **0.999999960** 天（容差 1e-6） |

点击几何全部打进证据（「应用」`@(52.5,447.0)` 69×32、窗口 960×640；负控靶点
`@(223.0,518.5)` 状态行 410×13），否则事后无法判断"点的是不是那个按钮"。

### 4.2 回归（全部 rc=0）

| 套件 | 结论 |
|---|---|
| `regression-clockcheck`（T16 时钟纯逻辑） | 12/12 PASS —— **T19 的时间换算建在它上面，最关键的一套** |
| `regression-actioncheck`（T15/T16 命令通路） | **VERDICT=PASS**（27 判据 0 FAIL）—— `AppFacade` 又被扩展过 |
| `regression-searchcheck`（T17 两个模型） | 26/26 PASS |
| `regression-locatecheck`（T18 定位/跟踪） | 14/14 PASS |
| `regression-locate-uicheck`（T18 UI 端到端） | 8/8 PASS —— **T19 动过 `SearchPage` 的绑定，此项必有** |
| `regression-a2-metal` | VERDICT=PASS |
| `regression-dyn-engine-metal` | **3/3 PASS**（见 §4.4：**不要读成"已修好"**） |
| `regression-dyn-stub-metal`（判别性对照） | 3/3 PASS |
| `regression-s3-stela3`（旧宿主集成） | 8/8 PASS |

### 4.3 构建

`cmake --build build-release` 全绿（只有既有的 macOS 部署目标警告，与 T18 相同）。
`src/ui/CMakeLists.txt` 新增 `../app/TimeCheck.{hpp,cpp}` 与 `qml/TimePage.qml` 两项注册。

### 4.4 DYN 读数：本轮两半都 3/3，**但这不改变 T18 的定性**

引擎侧 49.8~49.9 fps / 全程 670~697 帧 / 上传 579~583 次（mean 0.23 ms、max 3.58 ms）；
替身侧 50~52 fps / 267~291 帧。两半 **3/3 PASS**。
（取自 2026-09-24 15:28–15:31 复核跑；跑时 `mdbulkimport` 正在索引、`displaysleep=2`，
绝对帧率只作参考，**跨轮只有 PASS/FAIL 可比**。）

`D1-C02`/`D1-C07` 量的是"窗口有没有在渲染"，**要求窗口被暴露**，该量在本机**受环境支配**
（同日 13:2x 曾出现替身 4/4 FAIL、随后两侧**全程 0 帧**，`caffeinate -dims`/`-dimsu` 均无效）。
完整定性见 `docs/evidence/2026-09-24-t18-locate-track/dyn-ab-baseline-vs-t18.txt` §3/§6。

处置沿用 T18：跑 N 次如实报 `N/3`、**不跑"直到绿"**；替身对照也失败 ⇒ 该读数是
**「仪器测不到」**，不作为退化证据，**但也不改写成 INVALID**。
**不得**把 `N/3` 里的任一次 PASS 当成"零退化已证明"。
`tools/t19-verify.sh` 的 `env_note()` 会把 `uptime` + `displaysleep` + 是否有 `mdbulkimport`
写进汇总，否则"今天 3/3、早上 0/3"事后无法解释。

---

## 5. 改动清单

### 新增（4 项）

| 文件 | 内容 |
|---|---|
| `src/app/TimeCheck.{hpp,cpp}` | C++ 侧自检 14 条（线性步骤表驱动器；**等待挂在写入步上**有专门注释说明） |
| `src/ui/qml/TimePage.qml` | 时间页：6 个自旋框 + 应用/重置/现在 + JD/UT/本地三行投影 + 4 个步进按钮；`syncing`/`dirty` 双守卫 |
| `tools/t19-verify.sh` | 一键复跑（`core` / `regress` / `dyn N`）；DYN 段**同时跑引擎与替身两半** + `env_note` |
| `docs/evidence/2026-09-24-t19-time-ring/` | 证据包（含反向对照样本 `timeuicheck-negctrl-broken-property.txt`） |

### 修改（6 项）

| 文件 | 改了什么 |
|---|---|
| `src/app/AppFacade.hpp` | ① 新增 `setJulianDay` / `setLocalDateTime` / `setTimeNow` 三条写命令；② 新增 `utcOffsetHours` / `localDateTimeText` / `utcDateTimeText` / `localDateTimeField` 四条只读投影；③ `lastTimeRefusal` **提升为 `Q_PROPERTY`** + 新增 `lastTimeRefusalChanged` 信号（问题 ①）；④ `julianDay()` 文档改为"读 `getSimClockJD()`" |
| `src/app/AppFacade.cpp` | ① `julianDay()` 改读 `getSimClockJD()`；② 落地三条写命令与四条投影（含"超范围提前拒绝"与 `QDate::isValid` 校验口径）；③ `setTimeRefusal()` 改为"值变化才 emit"、计数不因值相同而漏计；④ 新增 `formatJd()` 供双日历文本使用 |
| `src/ui/main.cpp` | ① `timeCheck` / `timeUiCheck` 两个环境开关；② `startPage` 含 `"time"`；③ 新增 `STELQUICK_TIME_UI_CHECK` 执行分支；④ 新增 `uiTime*` 家族（13 条判据 UI-01..11）与 `UiTimeCheck`；⑤ 加 `core/StelUtils.hpp` 头（算期望 JD，**不另发明换算**；口诀：`fromUtf8` 而非 `fromLatin1`） |
| `src/ui/qml/MainWindow.qml` | 新增"时间（T19）"导航按钮；`pageIndex` 表加 `"time": 3`；StackLayout 加 `TimePage { }` |
| `src/ui/qml/SearchPage.qml` | 状态行绑定**真的读一下 `lastLocateRefusal`** 建立依赖（问题 ③）；判定改为 token 自己判"ok 就不说话" |
| `src/ui/CMakeLists.txt` | 注册 `../app/TimeCheck.{hpp,cpp}` 与 `qml/TimePage.qml` |

规模：`main.cpp` +585/-3、`AppFacade.cpp` +234/-1、`AppFacade.hpp` +90/-2、
`MainWindow.qml` +9/-2、`SearchPage.qml` +6/-1、`CMakeLists.txt` +4/-0。

---

## 6. 移交 A4（不假装完成）

| 项 | 状态 |
|---|---|
| 固定流程 I-REP-02：**定位环**（T18） | ✅ 已收（含 UI 端到端） |
| 固定流程 I-REP-02：**改时间环**（T19） | ✅ 已收（C++ 侧 14/14 + UI 端到端 13/13） |
| 固定流程 I-REP-02：**返回环** | ⬜ 仍未做（"返回"语义待定义：回到天空页？还是回到上一个状态？） |
| 排序策略 | ⬜ 产品决策，宜后置 |
| 显示历法（农历 / 儒略历切换）、MJD、时间速率 GUI、时区选择器 | ⬜ 本轮明确范围外；`TimePage` 已留口径（双日历并排），加时只需扩 `AppFacade` 投影 |
| QML 交互级测试（L2 Qt Quick Test） | 🟡 部分结清：时间页的"应用/重置/现在/步进"四条按钮链路已可复跑。仍未覆盖：**直接编辑自旋框的文本输入**（判据用 `setProperty` 设值，等价于用户转动步进器，但不等于输入法/键入路径）、滚动、视觉层（人工） |
| DYN 显示侧停摆的根因 | ⬜ 倾向"窗口未被暴露"（会话锁屏 / 显示器睡眠 / 遮挡 → `isExposed()` 变化）。**已试过且无效**：`caffeinate -dims` / `-dimsu` —— 下次别重复试 |
| 引擎侧跟踪标志泄漏的根治 | ⬜ T18 移交项，仍未动（本层已按合取真值读，属"上层规避"） |

---

## 7. 复现

```sh
cd ~/qt_demo/stellarium_vulkan
export VK_DRIVER_FILES=/opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json
export QT_VULKAN_LIB=/opt/homebrew/opt/vulkan-loader/lib/libvulkan.1.dylib
export STELQUICK_GRAPHICS_API=metal

# 一键复跑（判据 + 回归；DYN 跑 3 次并同时跑引擎/替身两半）
./tools/t19-verify.sh

# 单独跑自检
STELQUICK_TIME_CHECK=1 ./build-release/src/ui/stelQuickUI.app/Contents/MacOS/stelQuickUI
# → TIMECHECK: 判据 14/14 / VERDICT=PASS（rc=0）
STELQUICK_TIME_UI_CHECK=1 ./build-release/src/ui/stelQuickUI.app/Contents/MacOS/stelQuickUI
# → TIMEUICHECK: 判据 13/13 / VERDICT=PASS（rc=0）；最外层注入，窗口需能渲染
```

### 读结果的两条纪律

- `DYN` 那两行是 `N/3` 而不是 `rc=`。**不要**把其中某一次绿色当成"零退化已证明"；
  依据是同口径 A/B + 替身路径不含本任务代码。
- `DYN` **必须把引擎与替身两行一起读**：替身也失败（尤其"全程 0 帧"）⇒ 那是**仪器测不到**，
  不是被测程序失败。跑 DYN 前先确认机器空载、**会话未锁屏、显示器亮着**。
