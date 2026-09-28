# T22 — 时间页收尾：显示历法 / MJD / 速率 GUI / 时区选择器

> 日期：2026-09-28
> 前置：T19（改时间环）、T20（返回环 + 回放）、T21（搜索相关度排序）
> 证据：`docs/evidence/2026-09-28-t22-time-page/`
> 复跑：`tools/t22-verify.sh`（`core` / `regress` / `dyn N` / 无参 = 全部）

## 1. 任务与通过条件

计划文档里 A4 挂着的最后四项缺口：**显示历法 / MJD / 速率 GUI / 时区选择器**。
它们全部落在时间页（`TimePage.qml`）与 `AppFacade` 上 —— 与 T19 的写入链路同源，
一次收尾。

通过条件（全部满足）：

1. 四项功能可用，且**每一项都有引擎/旧界面的口径来源**，不新发明算法；
2. `TIMECHECK` 与 `TIMEUICHECK` 全绿；
3. **速率判据必须是成对断言**（"引擎确实变了" ∧ "仪表跟着变了"），
   并有**反向对照**证明它会红；
4. 回归零退化（口径与 T17–T21 同一后端，**刻意不换**）；
5. 证据、文档、构建记录、计划文档同步更新。

## 2. 设计与口径来源

### 2.1 速率：修掉"仪表没接在实况上"（🔴 本轮唯一的功能性缺陷）

**改前的实现**（`AppFacade::timeRate()`）：

```cpp
return m_timeRate;    // 本地缓存
```

`m_timeRate` 只在 `setTimeRate()` 里更新。而引擎有**六个**速率动作
（`StelCore.cpp:314-321`）：

| action id | 键 | 处理器 |
|---|---|---|
| `actionIncrease_Time_Speed` | `L` | `increaseTimeSpeed()` |
| `actionDecrease_Time_Speed` | `J` | `decreaseTimeSpeed()` |
| `actionIncrease/Decrease_Time_Speed_Less` | `Shift+L` / `Shift+J` | `…Less()` |
| `actionSet_Real_Time_Speed` | `K` | `toggleRealTimeSpeed()` |
| `actionSet_Time_Rate_Zero` | `7` | `setZeroTimeSpeed()` |
| `actionToggle_Time_Rate_Zero` | `9` | `toggleTimeSpeed()` |
| `actionSet_Time_Reverse` | `0` | `revertTimeDirection()` |

它们走的是 `StelCore::increaseTimeSpeed()` 一类，**完全不经过 `AppFacade`**
⇒ 用户按一次 `L`，引擎真的加速了，而 QML 读到的数纹丝不动。

这是本仓库反复复发的同一类病（T14/T15 血泪第 3 条：*仪器没接在实况上*）。

**改法**：读 `ISimPacing::simRate()` —— 其实现即 `core->getTimeRate()`
（`LiveSkyRuntime.cpp:302-305`），所以走它就是走引擎的 `timeSpeed`
（T16 定案的"速率留在引擎"）。无引擎/未挂 `ISimPacing` 时才回退缓存。

**连带**：`timeRateText()` 与 `timeDirection()` 都建立在 `timeRate()` 之上，
所以这一处修正让**三条判据**（TC-19 / TC-20 / TC-21）联动 —— 反向对照里
它们**一起红**，这正是"一处真源修正"的特征。

### 2.2 速率控制：走引擎既有动作，不另开写路径

六个按钮全部 `ActionRouter.trigger(引擎既有 action id)`，**不新增 `setTimeRate`
调用点**。理由与 T15 的"单点键位路由"同源：另开一条写路径就会与快捷键双轨，
两边的状态迟早对不上（而且 `actionSet_Real_Time_Speed` 的按钮勾选状态是引擎在维护）。

速度文本的换算口径**照抄** `StelGuiItems.cpp:885-907`，一档不改：

```
factor = |rate| / StelCore::JD_SECOND      → 秒/秒
初始单位 分/秒（value = factor/60）
  value ≥ 60      → 时/秒
  value ≥ 24      → 天/秒
  value ≥ 365.25  → 年/秒
倍数 ≤ 60 时只显示 `x<倍数>`，> 60 才带括号里的换算值
```

### 2.3 时区：两个**静默失败**是这里独有的坑

引擎侧有两处"不报错但不生效"，都必须堵：

| 坑 | 源码 | 症状 |
|---|---|---|
| ① `setCurrentTimeZone` 只接受 `getAllTimezoneNames()` 里的名字 | `StelCore.cpp:1717-1725` | 名单外的**只打一条 qWarning 就不设置**，"选了没用" |
| ② 名字不能被 `QTimeZone` 解析时，`getUTCOffset` 落回系统本地时区 | `StelCore.cpp:1637` | "选了个偏 8 小时的时区，结果一直是本机时区" |

处置：

- `availableTimeZoneIds()` 取**引擎名单 ∩ Qt 可解析**的交集 ——
  刻意**不是**简单照抄 `LocationDialog::populateTimeZonesList` 的
  `QTimeZone::availableTimeZoneIds()` 一把梭（那会把坑 ① 的项摆进下拉框）；
- `setTimeZoneId()` **回读验证** `getCurrentTimeZone() == tz` 才算成功 ——
  只看 `QTimeZone::isValid()` 就返回 true 等于向 UI 谎报"已切换"，**比不切换更坏**；
- 写入路径照抄旧界面：只写时区、**不动** `flagUseCTZ`（`LocationDialog.cpp:187-191`
  里两者本是分开的），QML 侧"选了就生效"显式再调一次 `setUseCustomTimeZone(true)`。

### 2.4 MJD：与 JD **同源**（⚠️ 刻意不用 `StelCore::getMJDay()`）

引擎有 `getMJDay()`（`StelCore.cpp:1280`），看起来是现成的：

```cpp
double StelCore::getMJDay() const { return JD.first - 2400000.5; }
```

但它读 `JD.first` —— 那是**上一帧快照**。T19 已经因为同一理由把 JD 读侧从
`getJD()` 改成 `getSimClockJD()`（`AppFacade.hpp` 里那段注释）；这里若图省事
调 `getMJDay()`，就把那个坑从 JD 挪到 MJD 上，而且**更隐蔽**（数值只差几毫秒，
肉眼与单次断言都抓不到）。

所以：**读** = `julianDay() - 2400000.5`（从同一个真源导出）；
**写** = `setJulianDay(mjd + 2400000.5)`，复用它的范围校验与 token 语义
（写入路径仍然只有一条）。

> 判据侧的处理：`TC-15` 断言**恒等式**与写入残差；另附一条 note 记录同刻的
> `core->getMJDay()` 作参照，**不据此判红** —— 因为 `setJD` 会同步写 `JD.first`
> （`StelCore.cpp:1245`）、帧末 `updateTime` 也会同步，正常帧序下两者相等，
> 硬造差异的判据是摆设。

### 2.5 显示历法：照抄换历边界

判定照抄旧界面 `DateTimeDialog.cpp:251`（`if (jd < 2299161) → 儒略历`），
该字面量即 `StelUtils.cpp:848` 的 `JD_GREG_CAL`（1582-10-15）——
引擎的日期换算内部就用它（`getDateFromJulianDay` 在 `julian >= JD_GREG_CAL`
走格里历分支，否则走儒略历）。**判定用真源 `julianDay()`**，否则换历那一刻会晚一帧。

### 2.6 为什么这四项的电平全部走**轮询**

TimePage 头注 ③ 写得很直白：那批新接口是 `Q_INVOKABLE`，**不是属性**。
写在 QML 绑定里读到的是**函数对象**（恒 false），而且没有 NOTIFY 就没有依赖、永不重算。

离散量（时区、DST 开关）本想用 `Q_PROPERTY`，但它们的**变化源在引擎**
（用户在旧界面/按快捷键改），本类并不知情，转发信号要额外接线；
而 300 ms 轮询读到的只是一个字符串，成本可忽略。

配套的守卫：`syncing` —— 回填期间挡住 `CheckBox.toggled` 的反向写入
（`toggled` 在**程序赋值时也会发**，与 SpinBox 的 `valueChanged` 同类性质；
不挡就是"回填 → 又写一次引擎"的反馈环）。

组合框那条回填是安全的：程序赋值 `currentIndex` **不发** `activated`
（只有用户交互才发），所以回填不会误触发写入。

## 3. 三个真实问题

### 3.1 🔴 速率读数与引擎漂移（已修，见 §2.1）

上面已述。反向对照：`timecheck-negctrl-cachedrate.txt`（**18/21**、rc=10，
TC-19/20/21 三条红）+ `timeuicheck-negctrl-cachedrate.txt`（**18/19**、rc=10，UI-15 红）。

TC-19 的原样读数：

```
TC-19 速率仪表接在实况上（成对）：引擎设 4.16666667e-02 → facade 1.00000000e+00（一致=false）；
      引擎动作后引擎 4.16666667e-01 / facade 1.00000000e+00（引擎确实变了=true，读数跟上=false）：FAIL
```

引擎变了 10 倍，仪表**恒为 1.0**。

### 3.2 ⚠️ 首跑 TC-18 的假红：引擎在 1847 年之前不看时区名

首跑 `TC-18` FAIL，读数：

```
时区写入生效且偏移成对（Africa/Abidjan → 6.9789 h；Antarctica/Casey → 6.9789 h）—— FAIL
```

两个时区给出**同一个偏移**，看起来像"时区写入没生效"。**根因是我的判据顺序**：

`getUTCOffset` 里有一道 `JD >= TZ_ERA_BEGINNING`（1847-12-01，`StelCore.cpp:1655`）——
不成立时它**完全不看时区名**，改按观察地点经度算 LMST。而 `TC-16` 刚把时钟挪到
**1582 年**测历法，没挪回来。`6.9789 h` = 经度 104.7° 的地方平太阳时。

这不是缺陷，是引擎的既定语义（标准时区 1847-12-01 才启用）。
修法：`TC-16` 测完**立刻还原到现代时刻**，并把这条写进 note。
修后 `Africa/Abidjan → 0.0000 h` / `Antarctica/Casey → 8.0000 h`，差正好 8 小时。

**这条留档的原因**：它演示了一个通用错误 —— 判据自己把环境改坏（改了时钟），
后面的判据就在**错误的语境**里测，报出来的红是**假红**。
定位手法就是问一句："这个 FAIL 的数值（6.9789 h）从哪来的？"它不等于任何预期的偏移，
于是往上游找，找到了前一步对时钟的污染。

### 3.3 ⚠️ 时区两个静默失败（实现侧已堵，见 §2.3）

不是本轮跑出来的新缺陷，而是在**写实现时读源码**发现的：
`setCurrentTimeZone` 的名单校验与 `getUTCOffset` 的 `!tzValid` 回退
都是"不报错但不生效"。若不堵，UI 会显示"已切换到 X"而引擎用的是系统时区。

### 3.4 ⚠️ 全量回归首跑两套红：**间歇**，不是本轮退化（已按先例堵住假红）

首跑 `returnuicheck-mac rc=10`（RT-08）与 `replaycheck-mac rc=10`
（RP-04/05/06/08/09 连带红）。定性过程按本项目纪律走，**没有**"跑直到绿"：

1. **换生产者对照**：`RP-03` 结果 3 条、`RP-04b` 无重复、`RP-04a` 首行即 `Planet:Moon`
   —— **数据层全绿**，红的只在"几何/点击"这一层；
2. **同口径 A/B 换二进制**：全 HEAD 源码重建 → `列表 934×341`、11/11 PASS。
   于是一度判断"是本轮引入"，转入二分；
3. **二分（源码级）**：只回退 `TimePage.qml` → **仍 `207×0`** ⇒ **TimePage 排除**。
   剩下的 C++ 说不通：`TimeCheck.*` 只有 `STELQUICK_TIME_CHECK` 模式会用；
   `main.cpp` 的改动全在 `UiTimeCheck` 内；`AppFacade.*` 是 **227 增 / 0 删**的纯追加
   —— 三者都不动 `SearchPage` 的布局；
4. **一次性探针**（打完即撤）：把 `resultList` 的**祖先链几何**打出来。健康时完整：
   `ListView[934x341] ← ColumnLayout[934x536] ← RowLayout[944x536] ←
   SearchPage[960x552] ← StackLayout[960x552@0,88]`，窗口 `960×640`；
   失败时**同一代码路径**读到 `207×0`；
5. **决定性对照**：把源码恢复到与第 3 步**逐字节相同**的状态（`TimePage.qml` = HEAD），
   重建后二进制 **38 577 576 字节（与失败那次同尺寸）**，再跑 **6/6 PASS**。
   ⇒ **同一份源码、同一个二进制、两种结果 ⇒ 间歇，不是本轮引入的退化。**

**根因（在仪器侧）**：Qt Quick 的布局/polish 由**渲染循环**驱动。`StackLayout` 刚切页时，
新页里的控件先以"布局前尺寸"存在，要等下一次 polish 才拿到真实几何。当时本机正被
Spotlight 批量索引（`mdbulkimport`）重压 —— DYN 归档记着 `load averages: 6.02 → 11.55` ——
polish 被拖到相位定时器之后 ⇒ 判据读到 `207×0`，并照这个坐标投递**真实点击** ⇒ 点击落空
⇒ 连带整串红。同一时段的 DYN 也印证：`真实引擎 0/3` **且 `替身对照 0/3`**，
按既有规则这正是"**仪器测不到**"。

**处置：改判据协议、不改判据**（沿 T18/T19 的 DYN 先例）。新增有界**布局就绪门**
`uiLayoutReady()` + `kUiLayoutWaitTries`：

- 门放在**本相位所有 `uiReplayMark` 之前**。原因是门用"停在本相位重试"实现，
  若放在判据之后，每次重试都会把前面的判据重跑一遍、计数虚高
  —— 负控实测出现过 `判据 80/81` 这种虚高读数，已修（修后负控为 `判据 2/3`）；
- 上限 `25 × 100ms = 2.5s`；超时**明确判红**并写明
  "仪器没接上：既不作退化证据，也**不作 PASS**"（不静默放行、不洗成 PASS）；
- **负控**（临时把 `uiLayoutReady` 强制恒 `false`）：`REPLAYCHECK` 与 `RETURNUICHECK`
  都按预期红、`rc=10`，报文明说"仪器没接上" ⇒ 失败路径不是死代码；
- 健康环境下 **8 + 8 次全程 `门触发=0`** ⇒ 门不改变正常路径的任何行为。

**给后续任务的护栏**：凡是"切页/切换后立刻点新页面里的控件"的判据，
都要先过这道门，否则重压下会周期性假红。

## 4. 验收

### 4.1 正题

| 套件 | 判据 | 读数 |
|---|---|---|
| `timecheck-mac` | 21（新增 TC-15..TC-21） | **21/21 PASS**，rc=0 |
| `timeuicheck-mac` | 19（新增 UI-12..UI-16） | **19/19 PASS**，rc=0 |

关键读数（详见证据 README §2）：

- `TC-15` MJD 恒等式残差 **0.00e+00 天**；写 MJD 后 JD 残差 **0.00e+00**
- `TC-16` JD 2299160.5 → `julian`；JD 2299161.0 → `gregorian`
- `TC-18` `Africa/Abidjan → 0.0000 h`；`Antarctica/Casey → 8.0000 h`
- `TC-19` 引擎 `4.16666667e-02` → `4.16666667e-01`，facade 全程一致
- `UI-13` MJD 行 `61677.638015` vs C++ `61677.638015`（差 6.39e-08 天）
- `UI-14a/b` `儒略历` ↔ `格里高利历`（同一控件两值 ⇒ 活投影）
- `UI-15` 真实点击「+」→ 引擎 `1.000000e-01` → `1.000000e+00`；
  速率行 `x8640（2.40 时/秒）` → `x86400（1.00 天/秒）`
- `UI-16` 组合框当前项 = `Africa/Abidjan`（= C++ 侧）

### 4.2 反向对照（证明判据有检验力）

见 §3.1 与证据 README §3。

### 4.3 回归

见 `rc-summary.txt`（口径与 T17–T21 一致：macOS + Metal RHI，**刻意不换**）。
本轮**没有动 `src/core/`**（全是 `src/app` + `src/ui`）⇒ 引擎库不需要全量重编。

## 5. 改动清单

| 文件 | 改动 |
|---|---|
| `src/app/AppFacade.hpp` | T22 段：MJD / 历法 / 时区 / 速率的声明与口径注释；新增 `m_timeZoneWriteCount`、`m_tzCache` |
| `src/app/AppFacade.cpp` | 🔴 `timeRate()` 改读引擎；新增 12 个方法；`<QTimeZone>` / `<QDateTime>` / `StelLocationMgr.hpp` |
| `src/app/TimeCheck.hpp` | 判据清单追加 TC-15..TC-21 与设计说明 |
| `src/app/TimeCheck.cpp` | 新增步骤 12（七条判据 + 两条 note） |
| `src/ui/qml/TimePage.qml` | 时区选择器（ComboBox + 两个 CheckBox）、MJD 行、历法行、速率行 + 方向行、七个速率按钮；轮询扩展；头注补 T22 段 |
| `src/ui/main.cpp` | `UiTimeCheck` 新锚点与跨相位变量；UI-12..UI-16；判据数 13 → 19；`<QDateTime>` / `<QTimeZone>`；**新增布局就绪门** `uiLayoutReady()`/`kUiLayoutWaitTries` 并接进 `REPLAYCHECK` case 2 与 `RETURNUICHECK` case 5（见 §3.4） |
| `tools/t22-verify.sh` | 新建（从 t21 派生，正题换成 T22 两套） |
| `docs/evidence/2026-09-28-t22-time-page/` | 新建证据目录（含反向对照两份） |
| `docs/T22_TIME_PAGE_COMPLETE.zh_CN.md` | 本文件 |
| `docs/BUILD_RECORD.zh_CN.md` | 追加 T22 章 |
| `docs/evidence/README.md` | 索引补 T22 行 |
| 计划文档 §2 / §9.1 / §9.2 / 新增 §9.4.6 | 进度更新 |

## 6. 移交 A4 剩余

A4（A-alpha）此轮后已无"功能缺口"，剩下的是**加固与已知问题**：

- **拼音排序 / 类型分组**（T21 排序的下一层）：需要引入拼音表，属独立小工程；
- **召回问题**（候选截断在排序之前，属引擎侧，记录在案不改）；
- **QML 交互级测试**补键盘滚轮与视觉层；
- **跟踪标志泄漏根治**（引擎侧：`unSelect()` 先清选中再发信号）；
- **DYN 停摆根因**（环境敏感量，报 `N/3` + 替身对照）；
- **Windows 侧复验**（W 支线仍停在 T17；T18–T22 的宿主层改动尚未在 Windows 上跑过自检 ——
  阻塞点：GUI 自检需交互桌面会话，而投递到交互会话要 `schtasks`，被当前沙箱策略禁用）。

## 7. 复现与读结果纪律

```bash
cd /Users/ztuqfvy/qt_demo/stellarium_vulkan
/opt/homebrew/bin/cmake --build build-release --target stelQuickUI -j 8
./tools/t22-verify.sh all          # 或 core / regress / dyn 3
```

**读结果的三条纪律**：

1. `*-negctrl-cachedrate.txt` **不是 FAIL，是证据** —— 它们证明本轮判据真的会红。
   判断"零退化"只看正跑文件（`timecheck-mac` / `timeuicheck-mac`）与 `rc-summary.txt`。
2. `DYN` 的 `N/3` 必须连**替身对照**一起读；任一次绿都不是"零退化已证明"。
   替身也败 ⇒ 记"仪器测不到"，不洗成 PASS、也不改写成 INVALID。
3. `returnuicheck` / `replaycheck` 若报 **`207×0`**（或 `RT-note 时间页…尚未就绪`），
   先看本轮有没有 `门触发` 读数 —— 门是**有界**的（2.5s），超时才判红，说明确实是
   仪器没接上而非被测行为；**照实归档，不改判据、不重跑到绿**（详见 §3.4）。
