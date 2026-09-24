# T16 单一仿真时钟落地（时钟所有权收编）

> 基线：`main@f9f99b6`（T15 完成）。
> 上游审计：`docs/T14_UPDATE_CLOCK_AUDIT.zh_CN.md`（T14 §2 时钟审计、§5 移交清单第 2 条）。
> 证据目录：`docs/evidence/2026-09-24-t16-sim-clock/`。
> 目标（开发计划原文）：**时钟所有权收编（引擎时钟是唯一真源，QML 侧只读投影）；
> 去除重复更新循环 —— 同一时刻只有一个 update 驱动源；T14 盘点出的重复源全部拆除或接管。**

---

## 1. 落地前的真实缺口

T14 的结论是"合流形态**事实上**已是宿主单点写时钟"。T16 开工前先把这个"事实上"拆开，
发现它由三根不成立的支柱撑着——每一根都是"恰好没出事"，不是"机制上不会出事"：

| # | 缺口 | 具体表现 | 后果 |
|---|------|----------|------|
| ① | **JD 是闭式公式的产物，不是被拥有的状态** | 宿主每帧 `setJD(jd0 + sim×rate)`（T15 后为 `m_jdAccum += dtWall×rate×scale`） | 任何外部 `core->setJD`（插件/脚本/GUI/引擎动作）**在下一帧被无条件覆盖**——跳转"闪一下就被拽回去"。`StelMovementMgr` 的时间步进键（年/日/时/分）就是这条路 |
| ② | **暂停靠"用速率 0 把墙钟乘没"** | `setTimeRate(0)` + 宿主自己的速率字段 | 语义是 hack：`updateTime` 仍每 tick 读墙钟、算一次无用乘加；速率字段被占用，插件调 `setTimeRate` 与架空方案互相打架 |
| ③ | **"只有一个驱动源"靠"旧宿主恰好没跑"——而它其实一直在跑** | `fpsTimer` 的自持循环（`drawEnded` → `start(fpsTimer)` → 重绘 → `drawEnded`）**在合流形态下是活的** | 每帧发生**两次完整的引擎渲染**：一次进我们的离屏 FBO（→ 邮箱 → QML 上屏），一次进旧宿主 QGraphicsView 的 widget FBO（**被丢弃，无人消费**）。白烧主线程与 GPU：实测帧率 **34.9 → 49.8 fps（+43%）** |

### 1.1 缺口 ③ 的原始误判与纠正（方法论留档）

T14 判定"合流形态 `fpsTimer` 天然休眠"，依据是 T13 那份 30 分钟长跑日志
（`docs/evidence/2026-09-23-t13-live-longrun/18-official-run2-...-PASS-11of11.log`，321 行）
中 `paintGL` / `drawEnded` 各出现 **0** 次。这条推断有两处硬伤：

1. **`paintGL()` 永不执行**——源码注释自己写着 "this is actually never called because the
   QGraphicsView intercepts the paint event"。计数为 0 是必然的，**不携带任何信息**。
2. **`drawEnded()` 根本没有日志语句**——它不可能出现在任何日志里。
   **字符串不出现 ≠ 函数没执行**：把"没有证据"当成了"证据表明没有"。

而被忽略的事实是：`LiveSkyRuntime::boot()` 里是**调了 `m_mainView->show()`** 的
（`WA_DontShowOnScreen` 只影响是否上屏，不影响 show 语义与绘制链的点燃）。

**T16 用可观测状态直接证伪**：`STELQUICK_ACTION_CHECK=1` 首跑即读到

```
ACTIONCHECK: AC-7 唯一 update 驱动源：simClock=HostDriven，旧宿主 fpsTimer=仍在跑：FAIL
```

**教训**：验证"某函数是否执行"时，先确认该函数**有没有日志**；没有日志就用可观测状态
（如 `isLegacyFrameTimerActive()`）断言，不要用"日志里没出现"反推。

---

## 2. 设计：把 JD 的所有权从"公式"搬到"状态"

### 2.1 新实体 `StelClockController`（`src/core/StelClockController.hpp/.cpp`）

core 层的纯逻辑类（无 GL / 无 QObject / 无定时器），**全部状态只有 4 个字长**：

```
Mode   m_mode   —— EngineWallClock | HostDriven
double m_jd     —— 唯一真源
bool   m_valid  —— 是否已锚定
double m_scale  —— 推进比例（0=暂停）
```

四条语义承诺：

1. **单一真源**：两模式下的当前仿真时间都落在 `m_jd`。
2. **单一写入**：`jumpTo()` 是**唯一**的外部跳转入口。`StelCore::setJD/setJDE` 调它，
   于是插件/脚本/GUI/引擎动作**零改造**地"跳转立即生效且不被拽回"。
3. **单一推进**：`advanceHostDriven(dt, rate)`（宿主帧泵）与
   `advanceEngineWallClock(anchor, elapsed, rate)`（旧形态墙钟）互斥，由 `m_mode` 决定。
4. **暂停是冻结推进、不是改速率**：`scale=0` 时速率保留，恢复不掉速、不补暂停期时间。

**刻意不搬 `timeSpeed`**：速率是引擎既有公共状态（700+ 处读取、`timeRateChanged` 信号、
RemoteSync/插件/脚本依赖），搬进控制器只会制造第二份真相。控制器只管"推进与重锚"，
速率由调用方每 tick 传入。这一取舍直接换来**插件零改造**：
插件调 `core->setTimeRate(60)` 在 HostDriven 下自然变成"宿主按 60 倍推进"，
插件无需知道宿主存在。

### 2.2 `StelCore` 的接管面（对外只加 7 个方法，不改任何既有签名）

```cpp
bool     isSimClockHostDriven() const;      // 模式查询
void     setSimClockHostDriven(bool);       // 切线（幂等，切入时以当前 JD 锚定起点）
void     advanceSimClock(double dtWall);    // 宿主帧泵推进（仅 HostDriven 生效）
void     setSimClockScale(double);          // 0=暂停
double   getSimClockScale() const;
double   getSimClockJD() const;             // 只读投影（QML 侧口径）
QString  getSimClockModeName() const;       // 诊断/自检
```

三处既有函数的改动（**引擎侧全部改动**）：

| 函数 | 改动 | 理由 |
|------|------|------|
| `setJD` / `setJDE` | 加 `simClock.jumpTo(...)` | 外部跳转重锚——缺口 ① 的修复点 |
| `updateTime` | 按模式分支：HostDriven 取 `simClock.jd()`，**不读墙钟**；EngineWallClock 原样 | 缺口 ② 的修复点 |

`revertTimeDirection`/`toggleTimeSpeed`/`setZeroTimeSpeed`/`setTimeNow`/`setTodayTime`/
`setMJDay`/`resetSync` **一律不动**——它们的语义在新模型下自动正确
（`setTimeRate(0)` 真的把推进停住；`setTimeNow()` 真的跳到"现在"）。

### 2.3 宿主侧：从"持有时钟"退化为"投影"

`LiveSkyRuntime` 删掉了 T15 引入的三个状态字段：

```
- double m_jdAccum;    // 累计 JD        → 上移为引擎的单一真源
- double m_lastSim;    // dtWall 来源    → 不再需要（dt 由 LegacySkyHost 给出）
- double m_simScale;   // 推进比例        → 上移为引擎的单一真源
```

`ISimPacing` 的四个方法变成对引擎时钟的**纯投影**（读写的都是 `StelCore`）：

```cpp
void   setSimScale(double s)  { m_core->setSimClockScale(s); }
double simScale() const       { return m_core->getSimClockScale(); }
void   setSimRate(double r)   { m_core->setTimeRate(r); }
double simRate() const        { return m_core->getTimeRate(); }
```

渲染回调从 `core->setJD(m_jdAccum); stelApp.update(dt); draw();`
改为 `core->advanceSimClock(dt); stelApp.update(dt); draw();`
——**宿主只负责"推进"，不再直接写时钟值**。

启动顺序（顺序是纪律，不能换）：

```
① core->setSimClockHostDriven(true)   以引擎当前 JD 锚定起点 + 切模式
② core->setTimeRate(simRate)          速率复用引擎既有字段（插件零改造的前提）
③ core->setSimClockScale(1.0)         新实例起步必为运行态
④ 起帧泵                              帧泵是唯一推进源，起之前推进量为 0
```

### 2.4 缺口 ③ 的拆除：把双驱动源收敛为单点

**真实机制**（T16 实测确认）：

```
boot():  m_mainView->show()
           → QWidget/QGraphicsView 绘制链
             → LegacyGraphicsItem::paint()   ← 每次都是 app.update(dt) + app.draw() 的整帧天空渲染
               → mainView->drawEnded()
                 → fpsTimer->start()         ← 自持！此后按 getDesiredFps() 持续重绘
```

处置（两条腿缺一不可）：

```cpp
// ① 宿主接管那一刻显式停表（LiveSkyRuntime::start 调用）
m_mainView->stopLegacyFrameTimer();

// ② drawEnded() 内守卫：主机驱动时**主动停表** + 防复活
if (引擎已初始化 && core->isSimClockHostDriven()) {
    if (fpsTimer->isActive()) fpsTimer->stop();   // ← 只"不自启"不够！
    emit frameFinished();
    return;
}
if (!fpsTimer->isActive()) fpsTimer->start();
```

> 为什么必须有 ①：`fpsTimer` 完全可能在 `setSimClockHostDriven(true)` **之前**
> 就已由 `boot()` 的 show → paint 链条点燃。此时 ② 的"不自启"对它毫无作用——
> 首跑 AC-7 FAIL 就是这个原因，补上 ① 后复跑 OK。**这是 T16 唯一一次首跑失败，
> 而它揭示的是真实缺陷而非测试瑕疵。**

另加可观测判据 `StelMainView::isLegacyFrameTimerActive()`（AC-7 用它断言）。
旧形态（`HostDriven==false`）路径逐位不变——S3 8/8 PASS 佐证。

**帧率量化（同口径、同后端 Metal、同尺寸 1920×1280、同一 producer 代码路径）**：

| 组 | T15（双驱动源） | T16（单驱动源） | 变化 |
|---|---|---|---|
| DYN D1-C01 生产者尾窗 | **34.9 fps** | **49.8 fps** | **+43%** |
| DYN D1-C02 显示尾窗 | 34.9 fps | 49.7 fps | +42% |
| 90s 长跑短窗 SL-C01 | 38.71 fps（T15 当时判"环境噪声"） | **49.18 fps** | +27% |
| 90s 长跑短窗 SL-C02 | 38.23 fps | **49.11 fps** | +28% |

**→ T15 遗留的 W-T15「短窗帧率受限」的正解不是环境噪声，而是重复驱动源抢主线程。
T16 顺手闭合了它。**

---

## 3. 墙钟读取点甄别收口（T14 §2.3 表的结论）

T14 留了一张"待甄别"表，T16 逐点给结论：

| 位置 | T14 初判 | T16 结论 |
|------|----------|----------|
| `StelCore.cpp:2303/2307`（`updateTime`） | 核心收编点 | ✅ **已收编**——HostDriven 下该路径不执行 |
| `StelCore.cpp:2346`（`resetSync` 的锚点写入） | — | **保留**：HostDriven 下锚点只用于模式回切，不参与推进 |
| `StelCore.cpp:125`（构造器锚点初值） | — | **保留** |
| `StelCore.cpp:1765/1772/1777/1792/1796`（`setTimeNow`/`setTodayTime`/`getIsTimeNow`） | — | **保留**：语义就是"用户要回到现在"，本就该读墙钟；走 `setJD` → `jumpTo` 后自动合规 |
| `StelApp.cpp:250/321`（`startMSecs`、RNG 种子） | 待甄别 | **保留**：与仿真时间无关 |
| `StelApp.cpp:1427/1433`（`getTotalRunTime`/`getAnimationScale`） | 待甄别 | **保留**：**动画/运行时钟**，不是仿真时钟（两条轴天然不同） |
| `StelUtils.cpp:1126`（`getJDFromSystem`） | 保留 | **保留**：纯函数 |
| `StelMainScriptAPI.cpp:910/937/981` | 随控制器接管 | **保留**：脚本 API 名就是 `getJDFromSystem`，语义是"系统时间"，读墙钟正确 |
| `SolarSystem.cpp:778` | 待甄别 | **排除（红鲱鱼）**：是导出文件名的时间戳 |
| `gui/ObsListDialog.cpp:181/742/1517` | 随对话框拆除 | **归 T17** |

**结论**：真正的收编点只有 `updateTime` 一处，其余全部是"天然该读墙钟"或"非仿真时钟"。
T14 担心的"多源墙钟"并不存在——**真正的重复源是"宿主自持的 JD 副本"，T16 已拆除**。

---

## 4. 顺手修掉的两个真实缺陷（T16 的副产品）

### 4.0 头号缺陷：合流形态的双 update/draw 驱动源（帧率 +43%）

见 §1.1 与 §2.4。摘要：旧宿主 `fpsTimer` 在合流形态下一直是活的，
每帧多跑一次完整的引擎渲染（进 widget FBO、无人消费）。
拆除后同口径帧率 **34.9 → 49.8 fps**。同时闭合了 T15 遗留项 W-T15。

### 4.1 时间步进键失效（缺口 ①）

`StelMovementMgr` 的时间步进（`StelMovementMgr.cpp:552-570`，年/日/时/分各键 →
`core->setJD(core->getJD() + n)`）在 T15 形态下会被宿主下一帧的 `setJD(m_jdAccum)`
覆盖——按键"没反应"。T16 后 `setJD` 重锚真源，按键生效。

### 4.2 插件/脚本的时间跳转被拽回（缺口 ①）

同上路径。特别是 `Vts`/`TimeNavigator`/`Satellites` 三个插件的 `core->setJD`——
T16 后**插件零改造**即正确（AC-9b 用"模拟插件直呼 `core->setJD`"断言这一点）。

---

## 5. 验证

### 5.1 判据清单

**逻辑层（无 GL / 无引擎，`STELQUICK_CLOCK_CHECK=1`）**：`StelClockController::selfTest`
12 项，覆盖默认态 / 锚定 / 推进量只由 dt 决定 / 暂停冻结 / 恢复不补时间 / 跳转重锚 /
负 dt 拒绝 / 旧形态等价 / 负速率倒放 / 负 scale 钳位 / 切线不动真源。

**集成层（真实引擎 + 帧泵，`STELQUICK_ACTION_CHECK=1`）**：

| 判据 | 断言 | 门槛 |
|------|------|------|
| AC-7 | 驱动源单点：`simClock==HostDriven` 且 `fpsTimer` 未激活 | 布尔 |
| AC-8 | **墙钟路径未参与推进**：暂停态下把墙钟锚点人为拉远 1 小时，JD 仍严格不变 | ≤1e-12 |
| AC-9a/9b | **外部跳转重锚**：`core->setJD(+1000天)`（模拟插件）暂停态静置 0.5s 仍严格成立 | ≤1e-12 |
| AC-9c | 恢复后从**新锚点**起算，推进量 ≈ R×0.8s | ±30% |
| AC-10 | **速率线性贯通**：R 与 2R 两窗口推进量之比 ≈ 2 | [1.5, 2.6] |
| AC-11 | **插件式 `core->setTimeRate(0)` 真冻结**（不经 AppFacade） | ≤1e-12 |

> AC-8 的设计要点：它是**严格相等断言**（不依赖时间窗/抖动容忍），因为
> EngineWallClock 路径若还在跑，1 小时 × rate=0.1 = **360 天**的巨跳，不可能被误判成"没跳"。
> AC-11 是 T15/T16 的**判别性判据**：T15 形态下宿主用自己的速率字段推进，
> 插件直接调 `core->setTimeRate(0)` 是**停不住**的。

### 5.2 结果（全部 PASS）

| 项 | 结果 |
|---|---|
| 时钟纯逻辑自检（12 项，无 GL / 无引擎） | **PASS rc=0** |
| 集成自检（12 项逻辑 + 11 项 AC） | **PASS rc=0** |
| 回归 A2 / DYN / S3 | **三组全绿 rc=0**（S3 8/8 ⇒ 旧形态逐位零影响） |
| 帧率（副产品） | DYN 尾窗 34.9 → **49.8 fps**；90s 短窗 38.71 → **49.18 fps** |

关键数据：AC-8 墙钟锚点拉远 1 小时后 JD 漂移 **0.000e+00 天**；
AC-9b 跳转值严格相等（2462307.661）；AC-10 速率比值 **2.055**；
AC-11 插件式 `setTimeRate(0)` 后漂移 **0.000e+00 天**。

完整日志与逐项明细见 `docs/evidence/2026-09-24-t16-sim-clock/README.md`。

**过程记录**：唯一一次首跑失败是 AC-7（双驱动源），它揭示的是**真实缺陷**；
T14 报告就此打了勘误（§1.1）。详见证据 README §七。

---

## 6. 代码改动清单

| 文件 | 改动 |
|------|------|
| `src/core/StelClockController.hpp` `.cpp` | **新增**。控制器 + 纯逻辑自检（12 项） |
| `src/core/StelCore.hpp` | 含控制器头（值成员）；加 7 个公开方法；加 `simClock` 私有成员 |
| `src/core/StelCore.cpp` | `setJD`/`setJDE` 加 `jumpTo`；`updateTime` 按模式分支；7 个新方法实现 |
| `src/StelMainView.hpp` `.cpp` | 加 `isLegacyFrameTimerActive()` + **`stopLegacyFrameTimer()`**；`drawEnded()` 加唯一驱动源守卫（主动停表 + 防复活） |
| `src/ui/LiveSkyRuntime.hpp` `.cpp` | 删 `m_jdAccum`/`m_lastSim`/`m_simScale`；加 `m_core`；ISimPacing 改投影；回调改 `advanceSimClock`；启动顺序 4 步 |
| `src/app/AppFacadeCheck.hpp` `.cpp` | 新增 AC-7..AC-11；t0 跑逻辑自检；时间线延长到 4400ms |
| `src/ui/main.cpp` | 新增 `STELQUICK_CLOCK_CHECK` 独立分支（**无窗口/无引擎**即可跑） |
| `src/CMakeLists.txt` | `stellarium_lib_SRCS` 加控制器两个文件 |
| `src/ui/CMakeLists.txt` | 独立工程形态补编控制器（**同一形态下只能有一份定义**，与 FrameMailbox 同纪律） |

**未改动**：所有插件（Vts/TimeNavigator/Satellites/…）、脚本引擎、GUI 对话框、
`StelMovementMgr`、`StelApp`、`SpecificTimeMgr` —— 这是"插件零改造是硬约束"的兑现。

---

## 7. 遗留与移交

**给 T17（SearchResultsModel / ObjectInfoModel）**
1. GUI 时钟写入对话框 ×6（`DateTimeDialog`/`SearchDialog`/`AstroCalcDialog`×2/
   `AstroCalcAlmanacWidget`/`ObsListDialog`）在 QML 形态下由模型 + ActionRouter 等价替代；
   替代后它们全部走 `setJD` → `jumpTo`，**无需任何时钟侧改动**。
2. `AppFacade::julianDay()` 目前读 `core->getJD()`（引擎字段，每 tick 由时钟同步）。
   若 T17 需要"帧内任意时刻都精确"的 JD，改读 `core->getSimClockJD()`（时钟真源）。
3. `StelMovementMgr` 的输入转发（鼠标/滚轮）仍待接：T14 §3.1 的 4 个
   `StelApp::handle*` 白名单入口是唯一路径。
