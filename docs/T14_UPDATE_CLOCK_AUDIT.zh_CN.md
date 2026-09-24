# T14 更新循环与时钟审计报告（纯盘点，不改行为）

> 审计基线：`main@f02ab6c`（2026-09-24）。
> 方法：全工程 grep + 关键函数全文精读 + 与合流形态实测行为（T11–T13 长跑）交叉验证。
> 目的：为 T15（AppFacade/ActionRouter）与 T16（单一仿真时钟）划定准确的接管面，每个触发源给出 **保留 / 拆除 / 接管** 判定。
> 配套证据：`docs/evidence/2026-09-24-t14-audit/`（原始 grep 记录，含命令与行号）。
>
> ⚠️ **勘误（2026-09-24，T16 落地时发现）**：本文 §1.2 的关键结论 ②
> 「合流形态下 `fpsTimer` 天然休眠」**不成立**。T16 的 AC-7 首跑用新加的
> `StelMainView::isLegacyFrameTimerActive()` 直接读到 **`fpsTimer` 仍在跑**。
> 当时的"佐证"（T13 长跑日志中 `paintGL`/`drawEnded` 各 0 次）无效：
> `paintGL()` 因 QGraphicsView 拦截 paint 而**永不执行**（源码注释已说明），
> `drawEnded()` 则**根本没有日志语句**——字符串不出现 ≠ 函数没执行；
> 且 `LiveSkyRuntime::boot()` 里是**调了 `m_mainView->show()`** 的。
> 真实结论：合流形态此前一直有**两个** update/draw 驱动源，
> T16 已将其收敛为单点（见 `docs/T16_SINGLE_SIM_CLOCK.zh_CN.md` §2.4）。
> 本文其余部分（§2 时钟审计、§3 冲突面、§4 速查表）经 T16 逐条复核，**仍然有效**。


---

## 1. update() 触发源全景

### 1.1 纯 QWidget 形态（stellarium.exe，旧宿主）

| # | 触发源 | 机制 | 位置 | 判定 |
|---|--------|------|------|------|
| W1 | `fpsTimer` | `QTimer(PreciseTimer, 1000/desiredFps)` → `fpsTimerUpdate()` → `singleShot(0, glWidget, update())`（`updateQueued` 去重）→ widget 重绘 → `drawBackground()` 内 `app.update(dt)` + `app.draw()` | StelMainView.cpp:654-657 / 1583-1590 / 429-430 | **接管**——QML 形态由宿主帧泵替代（见 1.2）；它是旧形态唯一节拍源 |
| W2 | `drawEnded()` 动态调频 | 每帧按 `getDesiredFps()` 回设 interval；macOS 上 `qMax(5ms, …)`（LP#2778 触控板 workaround） | StelMainView.cpp:1526-1545 | **拆除（随 W1）**——QML 形态帧率由场景图 vsync + 邮箱供给决定，无需引擎侧节拍器 |
| W3 | `cursorTimeoutTimer` | QTimer singleShot，鼠标静止隐藏光标 | StelMainView.cpp:659-663 / 1548 | **拆除**——光标管理属 QWidget 形态职责，QML 形态由 QML 层光标策略接管 |
| W4 | `screensaverInhibitorTimer` | QTimer 心跳抑制屏保 | StelMainView.cpp:2043-2045 | **保留**——语义独立于帧循环，QML 形态同样需要；归属 AppFacade 托管 |
| W5 | 模块辅助定时器 | LandscapeMgr::messageTimer（消息清除）、HipsMgr `singleShot(0)`（延迟加载）、LabelMgr/MarkerMgr 内部 QTimer（临时对象过期）、StelVideoMgr（视频帧）、SimbadSearcher（网络超时）、StelLocationMgr_p | src/core/ 各文件（证据包 grep-1） | **保留**——全部 GUI 线程 QTimer，与 QML 事件循环天然共存，语义独立于帧驱动 |

**关键结论 ①**：旧形态的帧驱动是**单源单链**——`fpsTimer` 是唯一节拍器，其余都是它的从动（paint 回调内的 update+draw）。这大幅简化 T15/T16 的接管：只要帧泵单点接管，不存在多路并发 update 竞争。

### 1.2 合流形态（stelQuickUI.exe，QML 宿主，当前实现）

| # | 触发源 | 机制 | 位置 | 判定 |
|---|--------|------|------|------|
| M1 | LiveSkyRuntime 帧泵 | `QTimer(GUI 线程, 名义 fps 间隔)` → `pumpTick()` → `sim = QElapsedTimer 墙钟` → `renderOneFrame(sim)` | LiveSkyRuntime.cpp:154-158 / 167-176 | **保留（现状即真源）**，T16 升级为 ClockController 单点 |
| M2 | 渲染回调 | `core->setJD(jd0 + sim×simRate)` → `stelApp.update(dt)` → `stelApp.draw()`（离屏 FBO） | LiveSkyRuntime.cpp:141-146 | **保留**——dt = 相邻两次 sim 差（墙钟、非负钳制，LegacySkyHost.cpp:413-421）；T16 后 dt 改由仿真时钟口径给出 |
| M3 | 引擎时钟禁用 | `setTimeRate(0)` 架空引擎内部墙钟推进 | LiveSkyRuntime.cpp:136-139 | **保留（合法化为 T16 正式机制）**——见 §2.4 |
| M4 | SkyViewport 刷新 | 邮箱帧到达 → `item->update()` → 场景图 sync/render；`frameSwapped` 仅测量用 | SkyViewport.cpp:47-94 | **保留** |
| M5 | 测量设施 | DynFrameCheck 采样 QTimer、A2FrameCheck 轮询 QTimer、SkyLongRun 100ms 采样 + frameSwapped | src/ui/*Check*.cpp / SkyLongRun.cpp | **保留**——验收设施，不进生产装配（main.cpp 按环境变量分支） |

**关键结论 ②（⚠️ 已被 T16 推翻，见文首勘误）**：~~合流形态下 `fpsTimer` **天然休眠**——`new StelMainView(confSettings)` 从不 show，`drawEnded()` 永不执行，`fpsTimer` 永不 start（T13 长跑 0 冲突佐证）。引擎侧不存在"僵尸定时器偷偷 update"的风险。~~

**更正后的结论**：合流形态下 `fpsTimer` **确实在跑**（`boot()` → `m_mainView->show()` → QWidget 绘制链 → `drawEnded()` → `fpsTimer->start()`，之后自持）。即合流形态存在**两个 update/draw 驱动源**（宿主帧泵 + 旧节拍器），
这是 T16 要拆除的真实缺陷，而非"天然不存在"的风险。T16 的处置：`StelMainView::stopLegacyFrameTimer()`
（宿主接管时显式停表）+ `drawEnded()` 内的停表守卫（防复活）+ AC-7 判据（可观测）。

### 1.3 场景图侧

- 引擎代码**无** `beforeRendering/afterAnimating/beforeSynchronizing` 钩子（全工程 grep 零命中，证据包 grep-2）——引擎不感知 QSG 帧相位，全部驱动来自宿主。
- `QQuickWindow::frameSwapped` 仅用于消费侧计量（SkyLongRun/main.cpp 测量段）。

---

## 2. 时钟审计（StelCore）

### 2.1 推进机制——引擎时钟与墙钟强耦合

`StelCore::updateTime(deltaTime)`（StelCore.cpp:2299-2337）：

```
JD.first = jdOfLastJDUpdate + (QDateTime::currentMSecsSinceEpoch()
           - milliSecondsOfLastJDUpdate) / 1000.0 * (realTimeSpeed ? JD_SECOND : timeSpeed);
```

- **每 tick 直接读墙钟** × `timeSpeed`；**`deltaTime` 参数不参与 JD 推进**（只用于 observer 寿命/运动积分）。
- 边界：JD clamp 到 ±200000 年；`computeDeltaT`（UT→TT）；`position->update` + `locationChanged`；`solsystem->computePositions(JDE)`。
- `resetSync()`（StelCore.cpp:2332-2338）同样读墙钟对齐 `jdOfLastJDUpdate`。

**这是 T16 的第一落点**：仿真时钟唯一真源化，必须把"每帧 setJD"（M2）正式化为 ClockController，并决定 `updateTime` 的墙钟路径在 QML 形态下的合法状态（现状 timeRate=0 架空 = 事实上已单点，但属"约定"而非"机制"）。

### 2.2 时钟写入方清单

| 层 | 写入方 | 写入方式 | 触发时机 | 判定 |
|----|--------|----------|----------|------|
| core | LiveSkyRuntime（我们的宿主） | 每帧 `setJD(jd0+sim×simRate)` | 帧驱动 | **接管（T16 真源）** |
| core | StelMovementMgr | 事件驱动 `setJD`（滚轮/键盘时间步进，年/日/时/分） | 用户事件 | **接管**——经 T15 输入转发链，写入仍走 ClockController 单点 |
| core | SpecificTimeMgr | `setTodayTime/setTimeRate/setJD` | 快捷动作 | **接管**（同上，T17 范围按需） |
| core | SolarEclipseComputer | `setJD/setTimeRate` | 插件式计算 | **保留观察**——非交互路径，T16 甄别 |
| core | Planet | `setJD/setTimeRate` 调用 | 待甄别 | **保留观察**（证据包 grep-4 定位到具体行后复核） |
| gui | DateTimeDialog / SearchDialog / AstroCalcDialog / AstroCalcChart / AstroCalcAlmanacWidget / ObsListDialog | QWidget 对话框写时钟 | 用户交互 | **拆除（QML 形态）**——T17 起由 QML 模型 + ActionRouter 等价替代 |
| scripting | StelMainScriptAPI（setJD/setJDE/setTimeRate ×scriptRate）、StelScriptMgr | 脚本驱动的时钟跳转/变速 | 脚本执行 | **接管**——同插件：经 ClockController 单点后脚本语义自动合规；脚本引擎本身无需改造 |
| plugins | Vts / TimeNavigator（含 PlanetaryEventsMgr、Dialog）/ Satellites（含 Dialog） | `core->setJD/setTimeRate` | 插件交互 | **接管**——插件不感知宿主，时钟写入统一过 ClockController 后自然合规；无插件改造需求 |

注：Calendars 插件的 `setJD` 是其自有类方法（非 core 时钟写入），已甄别排除。

### 2.3 墙钟直接读取点（T16 收编甄别表）

| 文件 | 用途初判 | T16 处置 |
|------|----------|----------|
| src/core/StelCore.cpp | updateTime 推进 + resetSync | **核心收编点**（见 2.1） |
| src/core/StelApp.cpp | 待逐点甄别 | T16 甄别 |
| src/core/StelUtils.cpp | 工具函数（getJDFromSystemTime 等） | **保留**——纯函数，调用方决定语义 |
| src/core/modules/SolarSystem.cpp | 待逐点甄别 | T16 甄别 |
| src/scripting/StelMainScriptAPI.cpp | 脚本时钟 API（write 侧） | 随 ClockController 接管 |
| src/gui/ ×2 | QWidget 侧 | 随对话框拆除 |

### 2.4 合流形态时钟现状（T16 的起点）

```
setTimeRate(0)                    ← 架空引擎墙钟推进（updateTime 仍会算 JD，但 timeSpeed=0
                                    时 JD 冻结在 jdOfLastJDUpdate——注意：墙钟×0 仍每帧算，
                                    无害但 T16 可让 ClockController 显式跳过）
每帧 setJD(jd0 + sim × simRate)   ← 唯一写入点（真源在宿主 QElapsedTimer）
```

**结论**：合流形态事实上已是"宿主单点写时钟"，T16 的工作是**把事实机制化**：ClockController（AppFacade 内）持有 sim 真源，帧泵、输入事件、动作全部经它写 JD；引擎 `updateTime` 墙钟路径降级为"纯 QWidget 形态专用"。

---

## 3. 交互与快捷键冲突面

### 3.1 引擎输入入口（4 个，全部遍历模块）

| 入口 | 位置 | 说明 |
|------|------|------|
| `StelApp::handleClick(QMouseEvent*)` | StelApp.cpp:1184 | → 各模块 `handleMouseClicks` |
| `StelApp::handleWheel(QWheelEvent*)` | StelApp.cpp:1213 | → 各模块 `handleMouseWheel` |
| `StelApp::handleMouseMoves(x, y, buttons)` | StelApp.cpp:1244 | 短路式（首个消费即停） |
| `StelApp::handleKeys(QKeyEvent*)` | StelApp.cpp:1251 | → 各模块 `handleKeys` |

这 4 个是 T15 输入转发的**唯一白名单入口**：QML 事件 → AppFacade → 这 4 个入口，引擎侧零改动。

### 3.2 StelActionMgr 的 QWidget 假设（T15 拆除对象）

- `StelAction::qAction = new QAction(this)`（StelActionMgr.cpp:67），`mainView->addAction(qAction)`（:69）——**挂死在 StelMainView 这个 QWidget 上**。
- 合流形态中 StelMainView 从不 show：QML 的 QQuickWindow 与它不是同一 QWindow，QShortcut/QAction 的按键分发**到不了引擎动作**。
- 冲突面：未来 QML 快捷键如果直接注册 `QKeySequence`，会与引擎内 700+ 个 StelAction 的既有键位**静默双轨**（引擎收不到 ≠ 没注册）。**T15 的 ActionRouter 必须单点**：QML 键事件 → StelActionMgr::matches（:181 已有匹配逻辑可复用）→ 触发 StelAction，禁止另建键位表。

### 3.3 QML 宿主交互现状

- `src/ui/quick/`（SkyViewport 等）**零事件处理**：无 Key/Mouse/Wheel 处理代码（证据包 grep-3 零命中）。
- 即：合流形态当前是"能看不能摸"。T15 起从 ActionRouter（键盘）开始补，鼠标/滚轮按 3.1 白名单转发。

---

## 4. 总判定速查表

| 源 | 判定 | 去向 |
|----|------|------|
| fpsTimer + drawEnded 调频（W1/W2） | 接管→**拆除**（T16 落实） | QML 帧泵（M1）替代。⚠️ 注意：本表原写"仅 QWidget 形态"，但 T16 实测该路径在合流形态**仍在跑**（见文首勘误）；T16 用 `stopLegacyFrameTimer()` + `drawEnded()` 守卫将其真正拆除 |
| cursorTimeoutTimer（W3） | 拆除 | QML 光标策略 |
| screensaverInhibitorTimer（W4） | 保留 | AppFacade 托管 |
| 模块辅助定时器（W5） | 保留 | 不动 |
| LiveSkyRuntime 帧泵 + setJD（M1/M2/M3） | 保留→机制化 | T16 ClockController |
| SkyViewport 刷新（M4） | 保留 | — |
| 测量设施（M5） | 保留 | 仅验收装配 |
| 引擎输入 4 入口 | 接管 | T15 输入转发白名单 |
| StelActionMgr QWidget 假设 | 拆除 | T15 ActionRouter 单点 |
| GUI 时钟写入对话框 ×6 | 拆除 | T17 QML 等价物 |
| 插件时钟写入 ×3 插件 | 接管（无需改插件） | ClockController 单点自然覆盖 |
| StelCore::updateTime 墙钟路径 | 收编 | T16：QWidget 形态专用；QML 形态 ClockController 显式写 |

---

## 5. 移交清单

**给 T15（AppFacade + ActionRouter）**
1. ActionRouter 单点：QML 键事件 → `StelActionMgr::matches` → StelAction；禁止双轨键位表（§3.2）。
2. 输入转发白名单 = StelApp 的 4 个 handle* 入口（§3.1），引擎侧零改动。
3. AppFacade 托管 screensaverInhibitorTimer；拆除 cursorTimeoutTimer 的 QML 形态职责。

**给 T16（单一仿真时钟）**
1. ClockController 单点写 JD（现 LiveSkyRuntime M2 升格），帧泵/输入/动作全部经它。
2. `updateTime` 墙钟路径定性为 QWidget 形态专用；甄别 StelApp.cpp / SolarSystem.cpp 墙钟读取点（§2.3 表）。
3. `setTimeRate(0)` 架空方案机制化；`resetSync` 的墙钟对齐在 QML 形态下的语义需明确（或禁用）。
4. 插件零改造是硬约束（§2.2）——ClockController 必须在 core 层兜底。
