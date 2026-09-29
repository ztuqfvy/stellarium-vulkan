# T28 交付文档 — 天空页触控板捏合 → 引擎缩放端到端

- 任务：A4 加固（交互级第三块）。**T28**，2026-09-29
- 提交：见 `docs/BUILD_RECORD.zh_CN.md` 的 T28 章
- 证据：`docs/evidence/2026-09-29-t28-pinch/`（含 3 轮负控 + 探针留档）
- 一键复跑：`tools/t28-verify.sh all`

## 1. 问题：合流形态天空页捏合是死路（T25 滚轮 / T27 鼠标的同源第三块）

引擎侧的"输入面"一共三块，旧宿主三块都接了，合流形态此前**一块都没接**：

| 输入 | 旧宿主链路 | 合流形态（T28 前） |
|---|---|---|
| 滚轮 | `StelMainView::wheelEvent` → `StelApp::handleWheel` | ❌ 无 `WheelHandler`（T25 补齐） |
| 鼠标 | `mousePress/Release` → `handleClick`；`mouseMove` → `handleMove` | ❌ 无 `MouseArea`（T27 补齐） |
| **触控板捏合** | `grabGesture(Qt::PinchGesture)` → `gestureEvent` → `pinchTriggered` → `StelApp::handlePinch` | ❌ **无 `PinchHandler`（T28 补齐）** |

`src/StelMainView.cpp:371` 注册手势、`:530-548` 处理：

```cpp
grabGesture(Qt::PinchGesture);                       // :371
if (QGesture *pinch = event->gesture(Qt::PinchGesture))   // :532
    pinchTriggered(static_cast<QPinchGesture *>(pinch));
// pinchTriggered：
if (changeFlags & QPinchGesture::ScaleFactorChanged) {
    qreal zoom = gesture->scaleFactor();
    if (zoom < 2 && zoom > 0.5)                      // 健全闸
        StelApp::getInstance().handlePinch(zoom, true);
}
```

合流形态的 `SkyViewport` 头注写着"向引擎转发手势"，但实现里只有滚轮与鼠标
—— **注释承诺与实现脱节**（这是本项目反复出现的线索类型，见技能 §33.1）。

## 2. 修法：QML `PinchHandler` → `AppFacade::pinchZoom` 保真转发（语义零复刻）

```qml
// SkyTestPage.qml，挂在 SkyViewport 内
PinchHandler {
    objectName: "skyPinchHandler"   // INTERACTCHECK IT-10..12 的锚点
    target: null
    onScaleChanged: (delta) => appFacade.pinchZoom(delta)
}
```

```cpp
// AppFacade.cpp
void AppFacade::pinchZoom(double scale)
{
    if (!(scale > 0.5 && scale < 2.0))   // 与旧宿主同一道健全闸（同时挡掉 NaN/±inf）
        return;
    if (!StelApp::isInitialized())
        return;
    StelApp::getInstance().handlePinch(scale, true);
}
```

三个刻意的选择，都写进了源码注释：

- **用 `onScaleChanged(delta)` 而不是 `activeScale`**：`delta` 是**乘法变化量**
  （官方语义：`activeScale` 2→2.5 时给 1.25），与旧宿主读的
  `QPinchGesture::scaleFactor()` 逐字对应。`activeScale` 是手势内**累积量**，
  而引擎每次都以当前 `aimFov` 为基准再除一次 ⇒ 传累积量会**双重累积**，
  捏一下视场指数级塌陷。
- **`target: null` 是必须的**：target 非 null 时 `PinchHandler` 会直接改写目标项的
  `scale` —— 那是 QML 侧变换，既污染 A2 逐像素判据，又与"引擎负责缩放"双轨。
- **`started` 恒传 `true`**（与旧宿主逐字一致）：`handlePinch` 里
  `previousFov = getAimFov()` 只在 `started` 时刷新，而它上一行的
  `zoomTo(..., 0)` 是 0ms ⇒ 立即生效 ⇒ 下一次读到的就是刚写进去的值，
  天然是**增量**语义。传 `false` 会让基准停在手势起点，把增量退化成错误累积。

只接 `scale`、不接 rotation/translation：真实目标就是"缩放天空"，张角旋转/平移
在引擎里没有对应动作（旧宿主也只接了 `ScaleFactorChanged`）。顺带，Qt 官方文档
点名 macOS 触控板上原生手势的 `activeTranslation` **恒为 (0,0)** —— 那条路本来
也拿不到数据。

## 3. 真链：Qt 源码实证（不是推测）

本机 Qt **6.11.2**。从平台层到判据读数的完整链路，逐段对过源码：

```
macOS beginGestureWithEvent / magnifyWithEvent / endGestureWithEvent
  → QNativeGestureEvent(BeginNativeGesture / ZoomNativeGesture / EndNativeGesture)
        ⚠️ Zoom 的 value() 是**增量分数**（要 ×1.25 得传 0.25）
  → QQuickDeliveryAgent::event() 的 `case QEvent::NativeGesture`
        deliverSinglePointEventUntilAccepted（**单点**投递；qquickdeliveryagent.cpp:1001）
  → QQuickMultiPointHandler 显式放行 NativeGesture（qquickmultipointhandler.cpp:49）
  → QQuickPinchHandler::wantsPointerEvent / handlePointerEventImpl
        （qquickpinchhandler.cpp:341-364 / 509-535；BeginNativeGesture 才 setActive）
  → setActiveScale(activeValue × (1 + value)) → scaleChanged(delta = **乘法倍率**)
  → QML onScaleChanged → AppFacade::pinchZoom
  → StelApp::handlePinch(scale, true) → StelMovementMgr::handlePinch
  → zoomTo(previousFov / scale, 0)
```

两个佐证（说明"macOS 触控板的 pinch 确实会进 QML"）：
- Qt 官方文档在 `PinchHandler.activeTranslation` 下专门写"在某些触控板（如
  **macOS 触控板**）上原生手势不产生 translation 值" —— 不喂进来就不会有此注；
- QTBUG-109002「Dragging a target is not functional ... reproduced at least on
  macBook with touchpad」，6.4.2/6.5.0 修复。

## 4. 🔴 判据对"事件形状"敏感：首版注入全红是好事

首版注入**只发裸 `ZoomNativeGesture`**，且把 `value` 传成倍率（1.25）而非增量分数
（0.25）⇒ FOV `60.0000 → 60.0000` 纹丝不动，IT-10/11 红（存档
`probe-native-pinch-injection.log`）。修正为 **Begin → Zoom(0.25) → End** 后一次转绿。

三条可复用的教训：

1. **`BeginNativeGesture` 不是可选仪式** —— `setActive(true)` 只在它的分支里做。
2. **`QNativeGestureEvent::value()` ≠ `QPinchGesture::scaleFactor()`**（分数 vs 倍率）。
   把旧宿主的语义直接套到原生事件上会错，而且错得很安静。
3. **反过来说，这套判据不是恒绿摆设**：注入形状错了它就红 —— 这正是判据该有的性质。

### 附带发现：`event->isAccepted()` 不可作判据

注入函数返回的受理位对**滚轮 / 鼠标 / 捏合**全都是 0，而它们其实都生效了
（IT-02 滚轮 60→11.33、IT-10 捏合 60→30.72 同时"受理=0"）。原因：QML 的
`PointerHandler` 走**独占 grab**，不通过 `QEvent::accept()` 表征受理。
⇒ 判据一律读**引擎可观测的 FOV**。

## 5. 判据（INTERACTCHECK 9 → 12）

| 判据 | 内容 | 设计要点 |
|---|---|---|
| **IT-10** | 天空页原生捏开 ×1.25³ → FOV **严格变小** | 成对：`before>0` ∧ `after<before` |
| **IT-11** | 反向捏拢 ×0.8³ → FOV **回升且回到原值**（2% 内） | 方向对照 + 幅值对照。只写"回升"会让"每次捏合乘固定倍率"的实现蒙混过关（1.25³ 与 0.8³ 互逆 ⇒ 理论精确还原） |
| **IT-12** | 时间页捏合 → FOV **不动** | 页守卫负控（`PinchHandler` 只挂天空页）。⚠️ **只在 IT-10/11 绿的前提下**才有判别力，判别性由负控③b 实证 |

## 6. 负控（三轮，各命中一层）

| 负控 | 改动 | 预期红 | 实测 |
|---|---|---|---|
| ① QML 接线层 | `onScaleChanged` 掐断（保留对象与锚点） | IT-10/11 | **10/12**，IT-10/11 红、IT-12 绿 ✔ |
| ② C++ 转发层 | `AppFacade::pinchZoom` 首行 `return` | IT-10/11 | **10/12**，同上 ✔ |
| ③b 挂载位置 | handler 从天空页挪到 `StackLayout` | IT-12 | **11/12**，IT-12 断言腿红（60.0000→**30.7200**）、IT-10/11 绿 ✔ |

①与②的**观测读数完全相同**（同为 10/12、同两条红）——这恰恰说明"同一条判据链上
两处断开"是等价观测；两轮都做是为了证明**每一层都是判据的敏感点**（源码差异见归档）。

**③a（未采纳的开局，留档）**：先把 handler 挂到窗口根（`keySink`）⇒ 工具栏点击
被吞、页面切不过去，套件在 IT-12 的**前提腿**判红（`currentIndex=1，期望 3`）。
机制：`QQuickPinchHandler` 的 native 分支（`qquickpinchhandler.cpp:509-535`）**提前
return，不清理 `currentPoints`** ⇒ handler 保留上一次手势的 1 个点 ⇒ 下一次鼠标压抑
`QQuickMultiPointHandler::wantsPointerEvent` 因 `hasCurrentPoints()` 命中而返回 true
⇒ 抢走 grab。改挂 `StackLayout`（在工具栏之外）后拿到"断言腿变红"的干净读数。

> **留档提示（不在 T28 修）**：同一机制理论上也能让"天空页内 pinch 之后再点天空"
> 的首击失灵（天空页的点击由 `MouseArea` 承担，且真实用户不会在同一点连着
> pinch+click，故暂不定性为缺陷）。若后续出现"捏合后首击失灵"，从这里查。

## 7. 仪器加固（不放宽判据）

**IT-08 前提腿（T27 遗留偶发）**：×5 的第 5 跑残差 **0.6321° > 0.5°**
（定位动画 ≈1.5s，机器负载高时 4s 未收敛完）⇒ 前提不成立、套件在 IT-08 提前收尾
（只跑出 8 条判据），**T28 的新判据拿不到读数**（与捏合无关：捏合在 case 13-16，
在 IT-08 之后）。处置沿 T22/T27「有界就绪门」先例：**重发一次定位写入步 + 重入本
相位**（不 `++phase`、不调 `uiInteractMark` ⇒ 判据数不虚增），最多 2 次；仍不居中
则**明确判红**。判据阈值（0.5°）**一字不放宽**。加固后 ×5 全绿。

## 8. 读数与回归

- **INTERACTCHECK 12/12 PASS ×5**，5 跑读数**逐位一致**：
  - IT-10 `60.0000 → 30.7200`（=60/1.953125，精确）
  - IT-11 `30.7200 → 60.0000`，相对偏差 `0.00000`
  - IT-12 `60.0000 → 60.0000`
- **回归 12 项全 rc=0**：searchcheck / actioncheck / locatecheck / locate-uicheck /
  timecheck / timeuicheck / returnuicheck / replaycheck / clockcheck /
  **a2-metal 逐像素**（动过 `SkyTestPage.qml` 的硬要求）/ **DYN 引擎 3/3 + 替身 3/3** /
  **S3 旧宿主**
- 环境注记：本轮 load 低于 T27 时，DYN 由 1/3 升到 3/3（"环境敏感"结论不变，
  两次读数都照实保留）

## 9. 改动面

| 文件 | 改动 |
|---|---|
| `src/ui/qml/SkyTestPage.qml` | `SkyViewport` 内新增 `PinchHandler`（`target: null`，`onScaleChanged` → `appFacade.pinchZoom`） |
| `src/app/AppFacade.hpp` | 新增 `Q_INVOKABLE void pinchZoom(double scale)` + 完整头注 |
| `src/app/AppFacade.cpp` | 实现（健全闸 + `handlePinch(scale, true)`），新增 [[T28：触控板捏合保真转发]] 小节 |
| `src/ui/main.cpp` | INTERACTCHECK 判据 9 → 12（IT-10/11/12）；新增 `uiInteractSendNativePinch`；IT-01 加 `skyPinchHandler` 硬锚点；**IT-08 前提腿加有界重试门** |
| `tools/t28-verify.sh` | 新建（正题 ×5 + 回归 9 项 + A2 + DYN×2 + S3） |
| `docs/T28_PINCH_ZOOM_UI_CHECK.zh_CN.md` | 本文档 |
| `docs/evidence/2026-09-29-t28-pinch/` | 证据目录（含 README、3 轮负控、探针留档） |

**未改** `src/core/` 与 `src/render/` —— 引擎零改动（`handlePinch` 只被调用）。
但按纪律仍跑了 **S3 旧宿主回归**：捏合链终点与旧宿主共用同一对函数，
"未动"是事实、不是免测理由。
