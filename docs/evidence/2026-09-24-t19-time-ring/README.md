# T19「改时间」环 — 证据包

任务：A4 固定流程 I-REP-02「开机→搜月球→**定位**→**改时间**→返回」的**第三个环**（"改时间"）。
日期：2026-09-24。一键复跑：`tools/t19-verify.sh`（`core` / `regress` / `dyn N` 三个子命令）。

---

## 1. 判据总览（同一次连贯运行）

| # | 套件 | 文件 | rc | 结论 |
|---|---|---|---|---|
| ① | T19 时间自检（C++ 侧） | `timecheck-mac.txt` | 0 | **14/14 PASS**（TC-01..14） |
| ①b | T19 **UI 层端到端**自检 | `timeuicheck-mac.txt` | 0 | **13/13 PASS**（UI-01..11） |
| ② | T18 定位/跟踪自检（回归） | `regression-locatecheck.txt` | 0 | 14/14 PASS |
| ②b | T18 UI 端到端（回归） | `regression-locate-uicheck.txt` | 0 | 8/8 PASS（**T19 动过 `SearchPage` 的绑定，此项必有**） |
| ③ | T16 时钟纯逻辑（回归） | `regression-clockcheck.txt` | 0 | 12/12 PASS |
| ④ | T15/T16 命令通路 + 集成（回归） | `regression-actioncheck.txt` | 0 | **VERDICT=PASS**（27 判据 0 FAIL） |
| ⑤ | T17 搜索/选择模型（回归） | `regression-searchcheck.txt` | 0 | 26/26 PASS |
| ⑥ | A2 静态纹理（Metal，回归） | `regression-a2-metal.txt` | 0 | VERDICT=PASS |
| ⑦ | DYN 动态帧 · 真实引擎生产者 | `regression-dyn-engine-metal.txt` + `-run1..3.txt` | **3/3 PASS** | 见 §6（**不要读成"已修好"**） |
| ⑦b | DYN · 替身生产者（判别性对照） | `regression-dyn-stub-metal.txt` + `-run1..3.txt` | **3/3 PASS** | 路径不含 T19 代码 |
| ⑧ | S3 旧宿主引擎集成（回归） | `regression-s3-stela3.txt` | 0 | 8/8 PASS（`EngineWallClock` 路径零退化） |

`rc-summary.txt` 是机器可读汇总。**DYN 那两行不是 `rc=` 而是 `N/3`**，理由见 §6。

---

## 2. 判据明细

### 2.1 `timecheck-mac.txt`（TC-01..14，C++ 侧）

| 判据 | 量什么 | 实测读数 |
|---|---|---|
| TC-01/02/03 | 本地日历 **往返恒等**（常规 / 跨年边界 / 闰日三组） | 写什么读回什么，三组全等 |
| TC-04 | **判别性对照**：三组读回必须**互不相同** | 2026-09-24 14:30:00 / 2026-12-31 23:59:59 / 2024-02-29 12:00:00 |
| TC-05 | 与**旧 `DateTimeDialog::newJd()` 公式逐位一致** | 本层 2461307.770833333 vs 旧公式同值，差 **0.000e+00** 天（容差 1e-9） |
| TC-06 | UTC 往返（读本地字段 → 原样写回） | ΔJD = **0.000e+00** 天（偏移 +8.00 h） |
| TC-07 | `utcOffsetHours()` 与 `core->getUTCOffset` 一致 | 8.000000 vs 8.000000 h（Asia/Shanghai） |
| TC-08 | 非法输入被拒且**无副作用** | 13 月 / 32 日 / 25 时 → `invalid-date`；时钟 ΔJD = 0.000e+00 |
| TC-14 | 超范围 JD 被拒 | `setJulianDay(1e12)` → false / `out-of-range`；ΔJD = 0 |
| **TC-09** | **世界确实动了**（暂停态跳 0.25 天后目标 AltAz 变化 > 5°） | **85.6289°** ← 见下方"这个数为什么恰好对" |
| **TC-10** | **判别性对照**：写回**同一个** JD → 变化 < 0.05° | **0.0000°** |
| **TC-11** | **可逆性**：跳回原时刻 → 夹角复原 < 0.05° | 残差 **0.0000°** |
| TC-12 | "现在"（含判别性对照） | 跳开后与系统差 46287.30 天（>100，对照成立）；`setTimeNow` 后差 0.000e+00 |
| TC-13 | 写入**不被帧泵拽回**（运行态） | 跳 0.50 天，实测漂移 0.0600 天；JD 差 0.5598 vs 目标+漂移差 **2.00e-04**（容差 3.2e-2）；未被拽回=true |

**TC-09 的 85.6289° 为什么是"物理上恰好对"**：0.25 天 = 90° 时角；天体在 90° 时角位移下
的角距 = `arccos(sin²δ)`。反解 85.6289° ⇒ `sin²δ = 0.0763` ⇒ δ ≈ **16.0°** ——
正是月球在 2026-09-24 附近的赤纬量级。不是巧合，也不是"只要不为零就算过"。

> 对照：`LocateCheck` 同样是跳 0.25 天却读到 **127.74°**。差别在于 LocateCheck **不暂停时钟**，
> 1200 ms 等待里时钟又走了 0.12 天，合计 0.37 天（>90° 时角）⇒ 位移可以超过 90°。
> TimeCheck 在冻结区执行，所以量到的是**纯 0.25 天**的那一份。两个数各自自洽。

### 2.2 `timeuicheck-mac.txt`（UI-01..11，最外层注入）

手法与 T18 的 `STELQUICK_UI_CHECK` 完全一致：`objectName` 找真实控件 → 向 `QQuickWindow`
投递**真实鼠标按下/抬起** → 断言**引擎状态精确改变** → 反向读 QML 控件的真实属性。

| 判据 | 量什么 | 实测读数 |
|---|---|---|
| UI-01 | 锚点可寻且可见 | 6 个自旋框 6/6；应用/重置/现在/−1天/+1天 5/5；状态行与 JD 行都在 |
| UI-02 | **C++ → QML 回填**绑定是活的 | C++ 写 2030-06-15 09:30:00 → 框内同值 |
| UI-03 | **判别性**：换一个不同的时刻，框必须跟着变 | 改写 1999-01-01 00:00:00 → 框内同值（排除"某字段写死"） |
| UI-04 | 改框后**自动回填被 `dirty` 挡住** | 等 500 ms（≥ 400 ms 回填周期），框内仍是用户改的 2011-11-11 11:11:11 |
| **UI-05** | **真实点击「应用」→ 引擎 JD 落到公式算出的那个值** | JD=2455876.632766203，期望同值，差 **0.00e+00** 天（容差 1e-6） |
| **UI-06** | **负控**：改了框**不点应用**、只点非按钮控件 → 时钟必须一个字节都不动 | JD 位移 **0.00e+00** 天（< 1e-12）；框内仍是 2022-02-22 22:22:22 |
| UI-07 | 真实点击「重置」→ 只回填、**不写时钟** | 框内回到引擎当前值 2011-11-11 11:11:11；JD 位移 0.00e+00 |
| UI-08 | 真实点击「现在」→ JD = 系统当前时刻 | 与系统差 5.81e-06 天（容差 1e-3）；跳转前是 46287.30 天（判别性成立） |
| **UI-09a** | 写入成功后状态行**声称已生效** | 文案 = 「时间已按写入生效。」（token=ok）← **这一条抓到了真实缺陷，见 §3** |
| UI-09b | 非法写入后状态行**改口**且时钟未动 | 文案 = 「日期/时间字段超出范围（如 13 月、32 日、25 时）——已忽略。」；JD 位移 0 |
| UI-10a | JD 只读投影（300 ms 轮询）与 C++ 侧同源 | 文本 "2461307.801355"，差 3.0e-07 天 |
| UI-10b | 时钟 +365.25 天后投影行**跟着变**（不是常量） | 2461673.051355，差 0.00e+00 |
| UI-11 | 步进按钮透传链活着（QML → ActionRouter → 引擎 StelAction） | 真实点击「+1 天」→ JD 位移 **0.999999960** 天（容差 1e-6） |

几何都打进证据（`@(52.5,447.0)`「应用」按钮 69×32、窗口 960×640；负控靶点 `@(223.0,518.5)`
状态行 410×13），否则事后无法判断"点的是不是那个按钮"。

断言里带上"等了多少毫秒"（UI-04 的 500 ms、TC-09 的 1200 ms），证据才自解释。

---

## 3. 🔴 首跑抓到的真实缺陷：`lastTimeRefusal` 缺 `Q_PROPERTY`

**不是** T18 留下的，是 T19 自己的新代码；**被 UI 判据的最外层注入抓到**。

```
Q_INVOKABLE QString lastTimeRefusal() const;     // ← 只有这一句（修前）
```

TimePage.qml 的状态行：

```qml
color: appFacade.lastTimeRefusal === "ok" ? "#2e7d32" : "#c62828"
text:  appFacade.lastTimeRefusal === "ok" ? "时间已按写入生效。" : appFacade.timeRefusalText()
```

在 QML 里，**只有 `Q_PROPERTY` 才会拿到字符串**；纯 `Q_INVOKABLE` 方法读到的是**函数对象**，
`=== "ok"` **恒为 false**。于是：

- 「时间已按写入生效。」分支是**死代码**；
- 而 `timeRefusalText()` 在 token=="ok" 时**返回空串**；
- ⇒ **写入成功反而显示"空白 + 红字"**，用户会以为失败了。

**修法**（照 T18 的 `lastLocateRefusal` 的正确形态）：

```cpp
Q_PROPERTY(QString lastTimeRefusal READ lastTimeRefusal NOTIFY lastTimeRefusalChanged)
```

并把 `setTimeRefusal()` 改成"值有变化才 emit"（与 T18 的 `setRefusal` 同纪律），
计数照旧在早退之前累加，不因"值没变"而漏计。

**反向对照**（`timeuicheck-negctrl-broken-property.txt`）：把上面那行 `Q_PROPERTY`
**临时注掉**后重建重跑 ⇒ `UI-09a`/`UI-09b` **双双 FAIL**、**11/13**、**rc=10**：

```
UI-09a 写入成功后状态行文案=""（token=ok，要求非空且声称已生效）：FAIL
UI-09b 非法写入后状态行文案=""（token=invalid-date，不得再声称已生效）；JD 位移 0.00e+00 天：FAIL
```

注意 UI-09b 的文案**也是空串**——因为**没有 NOTIFY 依赖，绑定根本不重算**，一直停在首次
求值那一刻的 `timeRefusalText()`（当时 token=ok ⇒ 空串）。这一个对照同时把两个缺陷显形了。
恢复 `Q_PROPERTY` 后 3/3 稳定 13/13。

> 这条正是 `STELQUICK_TIME_UI_CHECK` 存在的理由：`timecheck-mac.txt` 全程走 AppFacade 的
> C++ 公共 API，**结构上测不到**这段绑定 —— 它在 14/14 全绿的同时，UI 上这条状态行是坏的。

---

## 4. ⚠️ 首跑踩到的**自己的**坑：`delayAfter` 挂错了步骤

TC-09/TC-11 首跑 FAIL（AltAz 变化 0.0000°、残差 85.6289°）。**原因在自检的驱动器，
不在被测代码。**

驱动器的语义是「跑完本步之后等 `delayAfter` 毫秒」（`QTimer::singleShot` 挂在步体之后），
所以"等引擎把新时刻算进去"的等待**必须挂在写入步**上。我把它挂在了**读取步**上，
于是：

| 相邻探针 | 实测间隔 | 应有间隔 |
|---|---|---|
| 写入（step 3）→ 读 AltAz（step 4） | **0 ms** | 1200 ms |
| step 4 → step 6 | 1501 ms | — |
| step 6 → step 8 | 1501 ms | — |

⇒ 读取发生在写入后 **0 ms**，读到的是**上一个时刻**的星空，于是"世界没动"变成**假失败**。
而 85.6289° 那个"残差"恰好就是"滞后一拍"的直接证据：它等于修正后 TC-09 的**同一个数**。

定位手法（值得复用）：给每个探针加一行 `note` 打印 `stopwatch.elapsed()` + 三个时间源
（`simClockJD` / `core->getJD()` / `facade->julianDay()`）+ 原始 AltAz 三分量。
一眼就看出 **三个时间源都已同步、而 AltAz 恒为"上一个 JD 的值"**，再对一下相邻探针的
**实测间隔是 0 ms**，问题立刻收敛到驱动器而不是引擎。

> 附带的教训：`StelCore::setJD()` 是**直接写 `JD.first`** 的（不需要帧），所以
> "时间源已跳到新值"**证明不了**引擎重算过 —— 必须另找"帧有没有跑"的证据。
> 这也是为什么修好后要**在判据行里写明等了多少毫秒**：否则下一次没人知道这个数是怎么来的。

修正后 3/3 稳定 14/14。

---

## 5. 顺手修掉的既有缺陷（T18 页，同族）

`SearchPage.qml` 的定位状态行调用 `appFacade.locateRefusalText()`，但**没有读
`lastLocateRefusal`**。QML 只把"绑定里**实际读过**的属性"登记成依赖 ⇒
token 变化时那段不会重算（refusal 文案会停在旧值）。

修法：在绑定里真的读一下 token 建立依赖，并用 token 自己判"ok 就不说话"：

```qml
text: {
    var refusal = appFacade.lastLocateRefusal   // 建立依赖（必须真的读到）
    if (appFacade.tracking)
        return "正在跟踪：" + appFacade.trackedName
    return refusal === "ok" ? "" : appFacade.locateRefusalText()
}
```

T18 的 `regression-locate-uicheck.txt` 8/8 全绿，证明改动没破坏原有接线。

---

## 6. DYN 读数：本轮两半都 3/3，但**不要读成"已修好"**

本轮引擎侧 49.8~49.9 fps / 全程 670~697 帧 / 上传 579~583 次（mean 0.23 ms、max 3.58 ms）；
替身侧 50~52 fps / 267~291 帧。两半 **3/3 PASS**。
（读数取自 2026-09-24 15:28–15:31 的复核跑；跑时 `mdbulkimport` 正在索引、`displaysleep=2`，
故绝对帧率仅供参考，**跨轮只有 PASS/FAIL 可比**。）

**这不改变 T18 的定性**：`D1-C02`/`D1-C07` 量的是"窗口有没有在渲染"，它要求**窗口被暴露**，
而这个量在本机**受环境支配**——2026-09-24 同日下午曾出现替身 4/4 FAIL、随后两侧**全程 0 帧**
（`caffeinate -dims`/`-dimsu` 均无效）。完整定性见
`../2026-09-24-t18-locate-track/dyn-ab-baseline-vs-t18.txt` §3/§6。

处置沿用 T18：跑 N 次如实报 `N/3`、**不跑"直到绿"**；替身对照也失败 ⇒ 该读数是**「仪器测不到」**，
不作为退化证据，**但也不改写成 INVALID**（原始 FAIL 照实保留）。
**不得**把 `N/3` 里的任一次 PASS 当成"零退化已证明"。
`tools/t19-verify.sh` 的 `env_note()` 会把 `uptime` + `displaysleep` + 是否有 `mdbulkimport`
写进汇总，否则"今天 3/3、早上 0/3"事后无法解释。

跑 DYN 前先确认：机器空载、**会话未锁屏、显示器亮着**。

---

## 7. 文件清单

| 文件 | 内容 |
|---|---|
| `timecheck-mac.txt` | T19 时间自检全量（14/14） |
| `timeuicheck-mac.txt` | T19 UI 端到端自检全量（13/13，含点击坐标注记） |
| `timeuicheck-negctrl-broken-property.txt` | **反向对照**：临时注掉 `Q_PROPERTY` 后的失败样本（11/13、rc=10） |
| `regression-locatecheck.txt` | T18 定位/跟踪自检（14/14） |
| `regression-locate-uicheck.txt` | T18 UI 端到端（8/8）—— `SearchPage` 被本任务改过，必有此项 |
| `regression-clockcheck.txt` | T16 时钟自检（12/12） |
| `regression-actioncheck.txt` | T15/T16 命令通路（27 判据 0 FAIL） |
| `regression-searchcheck.txt` | T17 模型自检（26/26） |
| `regression-a2-metal.txt` | A2 静态纹理（Metal） |
| `regression-dyn-engine-metal.txt` + `-run1..3.txt` | DYN 真实引擎生产者（3/3 + 环境注记） |
| `regression-dyn-stub-metal.txt` + `-run1..3.txt` | DYN 替身生产者（判别性对照） |
| `regression-s3-stela3.txt` | S3 旧宿主集成（8/8） |
| `rc-summary.txt` | 机器可读汇总 |
