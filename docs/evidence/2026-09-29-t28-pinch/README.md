# T28 证据 — 天空页触控板捏合 → 引擎缩放端到端

macOS + Metal（`VK_DRIVER_FILES=MoltenVK`、`QT_VULKAN_LIB`、`STELQUICK_GRAPHICS_API=metal`），
口径与 t17..t27 一致，**刻意不换**。一键复跑：`tools/t28-verify.sh all`。

## 目录

| 文件 | 内容 |
|---|---|
| `interactcheck-mac-n5.txt` | 正题 INTERACTCHECK ×5 全量日志（12 条判据/次） |
| `interactcheck-mac-run1..5.txt` | 单次原始输出 |
| `probe-native-pinch-injection.log` | 首版注入（裸 Zoom、value 传成倍率）的探针输出 —— IT-10/11 红，留作"判据对事件形状敏感"的过程证据 |
| `negctrl/negctrl1-qml-wiring-cut.log` | 负控①：QML `onScaleChanged` 掐断 → IT-10/11 红、IT-12 绿（10/12） |
| `negctrl/negctrl2-cpp-forward-cut.log` | 负控②：`AppFacade::pinchZoom` 变 no-op → 同上（10/12） |
| `negctrl/negctrl3a-window-level-handler-swallows-click.log` | 负控③a（**未采纳的开局**）：handler 挂窗口根 → 工具栏点击被吞，套件在 IT-12 **前提腿**判红（11/12，读数不干净） |
| `negctrl/negctrl3b-stack-level-handler-it12-red.log` | 负控③b（**采用的**）：handler 挂 `StackLayout` → IT-12 **断言腿**红（60.0000→30.7200），IT-10/11 仍绿（11/12） |
| `regression-*.txt` | 回归 12 项（search/action/locate×2/time×2/return/replay/clock/A2/S3/DYN×2） |
| `regression-dyn-{engine,stub}-metal*` | DYN 引擎 vs 替身（各 3 次） |
| `rc-summary.txt` | 全部退出码汇总 |

## 结论（读数）

- **INTERACTCHECK 12/12 PASS ×5**（判据 9 → 12，新增 IT-10/11/12），且 **5 跑读数逐位一致**：
  - IT-10 原生捏开 ×1.25³ → FOV **60.0000 → 30.7200**（=60/1.953125，精确；应严格变小）
  - IT-11 反向捏拢 ×0.8³ → FOV **30.7200 → 60.0000**，相对偏差 **0.00000**（<%2）
    ⇒ 方向 + 幅值双断言（1.25³ 与 0.8³ 互逆 ⇒ 理论精确还原）
  - IT-12 时间页捏合 → FOV **60.0000 → 60.0000**（不动，页守卫）
- **回归 12 项全 rc=0**：searchcheck（T26）、actioncheck、locatecheck/locate-uicheck（T18）、
  timecheck/timeuicheck（T19/T22）、returnuicheck（T20）、replaycheck（I-REP-02）、
  clockcheck（T16）、**a2-metal 逐像素**（动过 SkyTestPage.qml 的硬要求）、
  **S3 旧宿主**（捏合链终点 `StelApp::handlePinch` → `StelMovementMgr::handlePinch`
  与旧宿主共用，旧宿主行为必须零变化）
- **DYN：引擎 3/3 + 替身 3/3**（本轮环境负载低，与 T27 时的 1/3 不同——同一"环境敏感"
  结论，读数照实保留）

## 真链（Qt 源码实证，不是推测）

```
macOS beginGestureWithEvent / magnifyWithEvent / endGestureWithEvent
  → QNativeGestureEvent(BeginNativeGesture / ZoomNativeGesture / EndNativeGesture)
        ⚠️ Zoom 的 value() 是**增量分数**（要 ×1.25 得传 0.25）
  → QQuickDeliveryAgent::event() 的 `case QEvent::NativeGesture`
        （deliverSinglePointEventUntilAccepted —— **单点**投递，qquickdeliveryagent.cpp:1001）
  → QQuickMultiPointHandler 显式放行 NativeGesture（qquickmultipointhandler.cpp:49）
  → QQuickPinchHandler::wantsPointerEvent / handlePointerEventImpl
        （qquickpinchhandler.cpp:341-364 / 509-535；`BeginNativeGesture` 才 setActive）
  → setActiveScale(activeValue × (1 + value)) → scaleChanged(delta = **乘法倍率**)
  → QML onScaleChanged(delta) → AppFacade::pinchZoom(scale)
  → StelApp::handlePinch(scale, true) → StelMovementMgr::handlePinch
  → zoomTo(previousFov / scale, 0)
```

**旧宿主对照**：`StelMainView::grabGesture(Qt::PinchGesture)` → `gestureEvent` →
`pinchTriggered` → `StelApp::handlePinch(gesture->scaleFactor(), true)`，
健全闸为 `0.5 < zoom < 2`（`src/StelMainView.cpp:371/530/538-548`）。合流形态此前
**QML 侧一行 pinch 代码都没有**（`SkyViewport` 头注写着"向引擎转发手势"，
实现里只有滚轮与鼠标——T25/T27 补齐前两块，pinch 是第三块）。

## 三个必须留档的发现

### 1. 🔴 判据对"事件形状"敏感 —— 首版注入全红是**好事**

首版注入只发**裸 `ZoomNativeGesture`**，且把 `value` 传成倍率（1.25）而非增量分数
（0.25）⇒ FOV 60→60 纹丝不动，IT-10/11 红（见 `probe-native-pinch-injection.log`）。
修正为 **Begin → Zoom(0.25) → End** 后一次转绿。这说明：
- `BeginNativeGesture` 不是可选的仪式 —— `setActive(true)` 只在它的分支里做；
- `QNativeGestureEvent::value()` 与 `QPinchGesture::scaleFactor()` **不是同一个量**
  （前者是分数，后者是倍率），把旧宿主的 `scaleFactor()` 语义直接套到原生事件上会错；
- 反过来说，这套判据**不是恒绿**的摆设：注入形状错了它就红。

### 2. ⚠️ "事件受理位"不可作判据（QML PointerHandler 不调 `accept()`）

注入函数的返回值（`event->isAccepted()`）对**滚轮 / 鼠标 / 捏合**全都是 0，
而它们其实都生效了（IT-02 滚轮 FOV 60→11.33、IT-10 捏合 60→30.72 同时"受理=0"）。
原因：QML 的 `PointerHandler` 走**独占 grab**，不通过 `QEvent::accept()` 表征受理。
⇒ 判据一律读**引擎可观测的 FOV**，不读受理位。

### 3. 🔴 页守卫负控（IT-12）**只在 IT-10/11 绿的前提下**才有判别力

整链全死时 IT-12 也是绿的（负控①②实证：那条路径下 IT-10/11 红、IT-12 绿）。
它的判别力由**负控③b** 实证 —— 把 handler 从天空页挪到 `StackLayout`（覆盖所有页），
IT-12 立刻红（时间页 FOV 60.0000→30.7200）。这是血泪第 4 条（孤立断言可假绿）的
又一次应用：**负控判据必须自带"证明它会红"的对照**。

## 负控（三轮，各命中一层）

| 负控 | 改动 | 预期红 | 实测 |
|---|---|---|---|
| ① QML 接线层 | `onScaleChanged` 掐断（保留对象与锚点） | IT-10/11 | **10/12**，IT-10/11 红、IT-12 绿 ✔ |
| ② C++ 转发层 | `AppFacade::pinchZoom` 首行 `return` | IT-10/11 | **10/12**，同上 ✔（与①同形态但在另一层，源码差异见归档） |
| ③b 挂载位置 | handler 从天空页挪到 `StackLayout` | IT-12 | **11/12**，IT-12 断言腿红（60.0000→30.7200）、IT-10/11 绿 ✔ |

**③a（未采纳的开局，留档）**：先把 handler 挂到窗口根（`keySink`）⇒ 工具栏点击被吞、
页面切不过去，套件在 IT-12 的**前提腿**判红（`currentIndex=1，期望 3`）。
机制：原生手势分支**不清理 `currentPoints`**（`qquickpinchhandler.cpp:509-535` 的
native 分支提前 return），于是 handler 保留了上一次手势的 1 个点 ⇒ 下一次鼠标压迫
`QQuickMultiPointHandler::wantsPointerEvent` 因 `hasCurrentPoints()` 命中而返回 true
⇒ 抢走 grab。**副作用提示（留档，不在 T28 修）**：该行为对"天空页内 pinch 之后再点
天空"也可能生效，但天空页的点击链路由 `MouseArea` 承担、且真实用户不会在同一个点
上连着 pinch+click，暂不定性为缺陷；若后续出现"捏合后首击失灵"的现象，从这里查。

## 仪器加固（不放宽判据）

**IT-08 前提腿（T27 遗留偶发）**：×5 的第 5 跑残差 **0.6321° > 0.5°**（定位动画
≈1.5s，机器负载高时 4s 没收敛完）⇒ 前提不成立、套件在 IT-08 提前收尾（只跑出 8 条
判据），**T28 的新判据拿不到读数**（与捏合无关：捏合在 case 13-16，在 IT-08 之后）。
处置沿 T22/T27「有界就绪门」先例：**重发一次定位写入步 + 重入本相位**
（不 `++phase`、不调 `uiInteractMark` ⇒ 判据数不虚增），最多 2 次；仍不居中则
**明确判红**。判据阈值（0.5°）**一字不放宽**。加固后 ×5 全绿。
