# T27 交付文档 — 天空页鼠标（点击选中 / 拖拽平移 / 右键反选）端到端

- 日期：2026-09-29
- 归属：A4 加固（QML 交互级其余：T25 键盘/滚轮之后的"鼠标"半边）
- 判据：`INTERACTCHECK`（`STELQUICK_INTERACT_UI_CHECK=1`）**6 → 9 条**，`9/9 PASS ×5`
- 一键复跑：`tools/t27-verify.sh all`
- 证据：`docs/evidence/2026-09-29-t27-mouse-nav/`

---

## 1. 问题：合流形态天空页鼠标是死路（T25 滚轮的同源缺陷）

探查即发现（与 T25 滚轮死路同一手法——**链路完整性盘点**）：

```
$ grep -rn "MouseArea|DragHandler|onWheel" src/ui/qml/
（天空页零命中；仅有的 onClicked 全在按钮/列表上）
```

- 旧宿主链路：`src/StelMainView.cpp` 的 `mousePressEvent/mouseReleaseEvent →
  StelApp::handleClick`、`mouseMoveEvent → StelApp::handleMove`
  （press 置 `isDragging` + 记 previousX/Y；release 无拖拽时 `findAndSelect`；
  右键 release = `deselection`；Ctrl+拖拽 = `dragTimeMode` 改时间）。
- 合流形态：`SkyViewport` 的头注明写"向引擎转发手势"，**实现里一行鼠标代码都没有**；
  `handleClick/handleMove` 又是 private ⇒ 与 T25 同款死路，潜伏 15+ 任务。

后果：**点击选不中天体、拖拽不能转天空、右键不能反选**——用户可用的一级交互缺失。

## 2. 修法：QML MouseArea → AppFacade 保真转发（语义零复刻）

```
SkyTestPage.qml  MouseArea（acceptedButtons: Left|Right）
   onPressed/onPositionChanged/onReleased
        ↓（真实坐标/按键/修饰键；button 也透传——引擎按 button 分左右键语义）
AppFacade::skyMousePress / skyMouseRelease / skyMouseMove
        ↓（坐标空间适配，见 §3）
StelApp::handleClick（合成 QMouseEvent）/ handleMove(x, y, buttons)
```

- 缩放倍率、Ctrl+拖拽改时间、右键反选、双击定位等语义**全部在引擎侧**，
  AppFacade 只做"合成事件 + 坐标适配"，一条语义都不复刻（T15 铁律：复刻 = 双轨）。
- `button` 必须透传：`handleMouseClicks` 按 `event->button()` 分派（右键 release =
  反选、左键 release = 选中对象）；硬编码左键会把右键反选变成"点哪选哪"。
- QML 侧硬约束不受影响：MouseArea 不渲染内容 ⇒ A2 逐像素探针不被污染（回归证实）。

## 3. 🔴 坐标空间适配：引擎视口与 QML 窗口不同构

DIAG 实测（`intercheck-mac-run1.txt`）：

```
引擎投影视口 2560×1440  dppp=2  月球投影=(1280.0,720.0)  点击=(480.0,364.0)
```

- 合流形态引擎渲染**固定 renderSize 帧**（默认 1280×720）再缩放显示到窗口
  （QML 逻辑 960×728）；投影视口 = `renderSize × dppp` = 2560×1440。
- 旧宿主的 `convertMouseEvent` 只做 `y = rect.height()-1-y` —— 那是因为旧宿主
  rect 与引擎视口**等尺寸**。合流形态不能照抄，否则点击落偏（实测偏 320px）。
- 修法（`skyEnginePos`）：

```cpp
QPointF(x / wq * prj->getViewportWidth()  / dppp,
        (1.0 - y / hq) * prj->getViewportHeight() / dppp)
```

  比例映射 + y 翻转 + 除以 dppp（`handleClick/handleMove` 内部会乘回）。等尺寸时
  自然退化为旧宿主的 `h-1-y`。
- **实证**：负控②把映射退化为等尺寸 ⇒ IT-08/09 立刻红。

## 4. 🔴 判据污染源：仿真时间推进淹没"拖拽效果"

首跑 IT-07 判据写的是"视线转过 >0.2°"。**负控①（拖拽链路整段断开）下它竟然绿**
（46.58°，见 `negctrl/first-run-no-discriminating-power.log`）——**孤立断言无判别力**。

定位过程（DIAG 二分法）：

1. `turnLeft/turnRight` 加 TMP 日志 → **零命中**（不是键控转向）；
2. `flagEnableMoveAtScreenEdge` 默认 **false**（不是屏幕边缘转向）；
3. 改相位延迟 400ms → 50ms：漂移 **80.58° → 14.66°** ⇒ **比例关系**，是时间累积；
4. 读引擎真值：`engineRate=10` 但 `simJD` 差 **0.61 天/0.4s**（≈1.5 天/秒）——
   **HostDriven 帧泵推进与 rate 读数脱钩**；视线锁地平 ⇒ J2000 视线随 JD 以
   200°/s 转 ⇒ 拖拽的十几度被彻底淹没。

> 🔴 **本条第 4 点的"脱钩"结论已被 T35 推翻（2026-09-30，`docs/T35_TIMELINK.zh_CN.md`）。**
> **观察是真的，解释是错的**，三处口径各自致错：
> ① `getTimeRate()` 的单位是 **JDay/sec**（`src/core/StelCore.hpp:595`），不是"× 实时倍率"；
> ② `engineRate` 在 INTERACTCHECK 内是**移动靶**——套件自己的 **IT-05 注入 L 键**
> 走 `increaseTimeSpeed()`（×10 阶梯）把 rate 抬上去，"帧泵推进量"与那个读数本就不是一个东西；
> ③ "400ms" 是**相位名义 delay**，从未与 ΔJD 同刻测过真实窗口。
> 同刻量齐三个量后：`ΔJD == 窗口 × rate × scale` 成立（TL-01 相对偏差 **0.19%..1.40%**／5 跑，
> 恒星时绝对腿残差 **+0.0000°**）。
> "0.61 天"这个数**是对的**——它等于"某个真实窗口 × 某个真实 rate"；
> "1.5 天/秒"只能由"0.61 ÷ 名义 0.4s"得到，是**用错窗口 + 读错单位**的商。
> **下面第 3 点的"比例关系，是时间累积"与本文档的处置（冻结仿真时间 + 双向拖拽反向对照）
> 完全不受影响**——那两条对"视线会随时间漂"的判断从机理上是对的（T35 探针 Q6d 实测
> `getViewDirectionJ2000` 的 ΔRA 与 ΔLST 同阶 ⇒ 视线**确实**随 JD 转），只是**速率**读数需要换算。

处置（两条同时上）：

1. **段内冻结仿真时间**：`setSimulationPaused(true)` → `ISimPacing::setSimScale(0)`；
   门内实测 `simJD 推进 = 0.000000000 天`（DIAG 读数），段尾**还原**（血泪第 9 条：
   判据改动的环境必须段内恢复）；
2. **同相位双向拖拽反向对照**：左拖 160px 读增量、再右拖读增量，断言两次增量
   **点积 < 0**（共模漂移同向叠加，反向断言抵抗残余）。实测点积 **-0.9981**。

> 附带事项：帧泵推进量（1.5 天/秒）与 `getTimeRate()`（10）不一致，**如实记录为
> 待查线索**（见证据 README），不在本轮偷偷改——若属设计需文档说明，若非则单独立项。
>
> ✅ **已结清（T35，2026-09-30）**：不是缺陷，也不是"设计需说明"——是**测量口径**问题
> （单位 / 移动靶 / 名义窗口，见本节上方 🔴 框）。产品侧**零语义改动**。
> 结论与证据：`docs/T35_TIMELINK.zh_CN.md`、`docs/evidence/2026-09-30-t35-timelink/`。

## 5. 判据（INTERACTCHECK 6 → 9）

| 判据 | 断言 | 判别性设计 |
|---|---|---|
| IT-07 | 冻结时间后左拖 160px → 视线转 >0.2° **且**再右拖的两次增量点积 <0 | 成对（基线有效）+ 反向对照（单向下不成立） |
| IT-08 | 月球居中（<0.5°）→ 清选中 → 真实点击视口中心 → 选中 == "Moon" | 前提腿（居中 + 清选中）+ 结果腿（映射正确性守卫） |
| IT-09 | 右键 press+release → 反选 | 与 IT-08 成对（选中 true → false） |

## 6. 负控（两轮，各命中一层）

| 负控 | 改动 | 预期 | 实测 |
|---|---|---|---|
| ① 链路层 | `MouseArea.enabled:false` | IT-07/08/09 红 | 三条全红；IT-05（键盘）不受伤 ✔ |
| ② 适配层 | `skyEnginePos` 退化为等尺寸 | IT-08/09 红 | IT-08/09 红、**IT-07 仍绿** ✔ |

负控②的"IT-07 仍绿"不是漏网，而是**两层分工的实证**：IT-07 守"链路活"（增量方向
与映射无关），IT-08 守"映射正确"。要覆盖映射正确性，必须有 IT-08 这类**绝对位置**判据。

## 7. 仪器加固（不放宽判据）

IT-06 焦点腿在后台连续跑时偶发红。定位：**窗口未获得系统焦点**
（`isActive=false` ⇒ `focusObject()` 恒 nullptr ⇒ 守卫前提不存在；判据按键是
`sendEvent` 直达窗口，绕过系统焦点，所以引擎照样收到 L 键 ⇒ 假红）。
处置：①点击后校验焦点对象是否是 `QQuickTextInput`（有界重试 2 次）；
②窗口未激活时 `requestActivate` 有界重试 3 次，仍失活则**明确判红并标注
"仪器不可用（窗口未激活）"+ 当前焦点类名**——不洗成 PASS、不改写成 INVALID
（同 DYN 处置纪律）。

## 8. 读数与回归

- **INTERACTCHECK 9/9 rc=0 ×5**
- **回归 10 项全 rc=0**：searchcheck（T26）、locatecheck + locate-uicheck（T18）、
  actioncheck、timecheck + timeuicheck（T19/T22）、returnuicheck（T20）、
  replaycheck（I-REP-02）、clockcheck（T16）、**a2-metal 逐像素**（动过
  SkyTestPage.qml 的硬要求）、**S3 旧宿主**（鼠标入口与旧宿主共用）
- **DYN：引擎 1/3 + 替身 1/3**（load 3.6–4.2 + Spotlight 索引）⇒ 替身同败 =
  仪器测不到（环境），非退化；原始读数照实保留，不改写

## 9. 改动面

| 文件 | 改动 |
|---|---|
| `src/ui/qml/SkyTestPage.qml` | 新增 MouseArea（左/右键转发；页守卫天然成立） |
| `src/app/AppFacade.hpp/cpp` | 新增 `skyMousePress/Release/Move` + `skyEnginePos` 坐标空间适配 |
| `src/ui/main.cpp` | INTERACTCHECK 加 IT-07/08/09（含时间冻结、双向对照、DIAG）+ IT-06 焦点/激活门 |
| `tools/t27-verify.sh` | 正题 ×5 + 回归 + DYN 一体化复跑 |

**未改 `src/core`**（鼠标入口本身就是 `StelApp` 的既有 public-ish 路径，靠 T25 已加的
friend 即可）；S3 旧宿主回归仍照跑，证明旧宿主行为零变化。
