# T25 — 交互级（键盘/滚轮）UI 层端到端判据 + 两个真缺陷修复

任务性质：A4 加固项（非功能缺口）。原计划是"补键盘/滚轮的 QML 交互级测试"，探查即发现两条交互面各有潜伏缺陷，判据先行实抓后一并修复。

## 1. 缺陷一（修复）：合流形态滚轮缩放是死路

**现象**：stelQuickUI 里滚动滚轮，视场纹丝不动。T10 合流以来一直如此，此前无任何判据依赖滚轮 ⇒ 潜伏 15 个任务。

**根因**：旧宿主的滚轮链路是 `StelMainView::wheelEvent → StelApp::handleWheel`（引擎把事件分发给各模块）；合流形态的 QML 里**没有任何 WheelHandler/onWheel**，`StelApp::handleWheel` 又是 private ⇒ 这条链从未接过。与 T15 键盘死代码是同款缺陷——键盘在 T17 修了，滚轮是同一交互面剩下的另一半。

**修法**（语义零复刻）：
- `SkyTestPage.qml`：SkyViewport 挂 `WheelHandler`，`onWheel` 转发 `wheel.angleDelta/modifiers` → `appFacade.wheelZoom(...)`。页守卫天然成立（SkyViewport 只在天空页可见）。
- `AppFacade::wheelZoom(dx, dy, modifiers)`（新增 Q_INVOKABLE）：合成 `QWheelEvent` → `StelApp::handleWheel()` **保真转发**。缩放倍率、Ctrl+滚轮改时间等 modifiers 语义全在引擎侧，QML/AppFacade 复刻任何一条都是双轨。
- `StelApp.hpp`：加 `friend class stelapp::AppFacade`（唯一对上游的改动；friend 声明行为零变化）。不做 friend 就只能自己复刻 `handleWheel` 的模块分发循环 = 双轨。

## 2. 缺陷二（修复）：焦点守卫在真实搜索框上是死代码

**现象**：INTERACTCHECK 首跑 IT-06 红——搜索框聚焦后注入 L 键，`timeRate` 1→10（引擎动作穿透）。

**根因**：`ActionRouter::canDispatchToSky()`（U-ACT-03）用 `metaObject()->className()` **精确比较** `"QQuickTextInput"`。Qt Quick Controls 的 TextField 最派生类名是 `QQuickTextField`（QQuickTextInput 的子类），精确匹配不命中 ⇒ 守卫对真实输入框失效，用户在搜索框打字会触发天空快捷键。而 U-ACT-03 的 C++ 直调判据构造的是 TextInput 原语（className 恰好等于 `"QQuickTextInput"`）⇒ **假绿了 15 个任务**。这是"判据别为上游现状背书"（T20）与"孤立断言可假绿"（血泪第 4/8 条）的又一实例：判据的输入是理想化原语，测不到真实控件形态。

**修法**：`className == "QQuickTextInput"` → `inherits("QQuickTextInput") || inherits("QQuickTextArea")`。子类命中、原语也命中（inherits 含自身），旧判据不受影响（actioncheck 回归 rc=0 佐证）。

## 3. 仪器陷阱（首跑踩中，已入档）

IT-06 复用 T20 的 `uiReturnSendKey` 注入键盘——它内部 `keySink->forceActiveFocus()` 把焦点**从搜索框抢走**，等于仪器亲手拆掉守卫前提 ⇒ 修完守卫 IT-06 仍红。加"不抢焦点"注入变体 `uiInteractSendKeyNoFocusGrab`。定性：T14 血泪第 3 条（"仪器没接在实况上"）的变体——这次是仪器**主动破坏**被测前提。规则：**注入函数的隐式副作用必须与判据前提对齐**。

## 4. 判据（INTERACTCHECK，env `STELQUICK_INTERACT_UI_CHECK=1`，6 条）

| 判据 | 断言 | 性质 |
| --- | --- | --- |
| IT-01 | 交互锚点齐备（keySink/pageStack/viewport/nav*/queryField） | 前提 |
| IT-02 | 天空页真实滚轮 dy=+1200 → FOV 60→11.33 严格变小 | 成对（before>0 + 变小） |
| IT-03 | 反向滚 → FOV 回升到 60 | 方向对照（只会单向的假链会红） |
| IT-04 | 时间页同滚轮 → FOV 纹丝不动 | 页守卫负控 |
| IT-05 | 天空页真实 L 键 → timeRate 0.1→1 ∧ dispatched=actionIncrease_Time_Speed | 成对（信号 + 引擎状态） |
| IT-06 | 搜索框聚焦后同 L 键 → timeRate 不变 ∧ dispatched 不发 | 焦点守卫（U-ACT-03 的端到端腿） |

键盘键位选 `L`（引擎 `actionIncrease_Time_Speed`）：副作用可从 `timeRate` 直接观测，且 `dispatched` 信号可断言"发的是对的动作"。

## 5. 验证与回归（全绿，证据见同目录 README）

- INTERACTCHECK 6/6 ×5（rc=0）；两轮代码级负控各命中预期红（滚轮断开 → IT-02/03 红；守卫失效 → IT-06 红且 IT-05 不受伤）。
- 回归 10 项全 rc=0：actioncheck / locatecheck / locate-uicheck / timecheck / timeuicheck / returnuicheck / replaycheck / searchcheck / clockcheck / **a2-metal**（动过 SkyTestPage.qml ⇒ A2 逐像素必查）。
- DYN 引擎 3/3 + 替身 3/3；**S3 旧宿主 rc=0**（动了 `src/core/StelApp.hpp` ⇒ 硬要求）。

## 6. 移交

- Esc 返回链路（RT-09/10/11）已由 T20 覆盖，本套件不重复。
- 触控板双指捏合（pinch）、拖拽平移、输入法组合键仍无判据——列入后续加固项（QML 交互级的剩余部分）。
- Windows 侧复验价值进一步上升：本轮动了 `src/core`（friend）+ 宿主层（WheelHandler）+ 守卫（inherits）。
