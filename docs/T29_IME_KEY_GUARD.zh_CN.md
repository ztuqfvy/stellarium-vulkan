# T29 — 输入法组合键与 Esc 守卫（合流形态键盘面收尾）

> 任务定性：**A4 加固**（非功能缺口）。补齐 QML 交互级测试的"输入法组合键"一项。
> 日期：2026-09-29 ｜ 提交：`<待填>` ｜ 证据：`docs/evidence/2026-09-29-t29-ime/`
> 一键复跑：`tools/t29-verify.sh` ｜ 判据：`INTERACTCHECK` **16/16**（IT-01..IT-16）

---

## 1. 结论先行

三句话：

1. **"输入法组合期间天空快捷键被抢"这个缺陷在 macOS 上不存在**——不是我们防住了，而是
   Qt 平台层在组合期间**根本不产生 `QKeyEvent`**（`qnsview_keys.mm:137` 的
   `m_composingText.isEmpty()` 闸门）。这一半是**澄清**，写进文档免得后来人凭想象去"修"。
2. **但顺着这条线查出了一个真缺陷**：`MainWindow.qml` 的 **Esc 返回分支写在焦点守卫之前**
   ⇒ 焦点在搜索框时按 Esc **直接跳页**，把用户半途的输入丢在框里。与 U-ACT-03
   「焦点在可编辑控件时不触发天空快捷键」自相矛盾——**守卫被绕过**。
3. 修法只有一行语义：**Esc 分支先问守卫**（口径仍只有一份，在 C++
   `ActionRouter::canDispatchToSky()`；QML 不复刻判断）。

顺带补上了合流形态**从未被端到端测过**的输入法通路：preedit 组合、commit 提交。

读数：正题 `INTERACTCHECK 16/16 ×5`、三轮代码级负控**各命中一条判据且互不串扰**、
回归 11 项全绿（含 A2 逐像素、DYN 引擎 3/3 + 替身 3/3）。

---

## 2. 缺陷机制（Qt 6.11.2 源码实证，完整链路）

全部行号取自 `qtdeclarative` / `qtbase` 的 **v6.11.2** tag，原文摘录见
`docs/evidence/2026-09-29-t29-ime/probe-qt-source-evidence.txt`（328 行）。

```
[用户在搜索框里按 Esc]
  │
  ├─ QQuickDeliveryAgentPrivate::deliverKeyEvent      qquickdeliveryagent.cpp:971-1001
  │    item = activeFocusItem (= 搜索框)
  │    do { e->accept(); sendEvent(item, e); }
  │    while (!e->isAccepted() && (item = item->parentItem()));     ← :994-999
  │
  ├─ QQuickTextInput::keyPressEvent                    qquicktextinput.cpp:1479-1500
  │    非 Up/Down/Left/Right ⇒ 走 processKeyEvent
  │
  ├─ QQuickTextInputPrivate::processKeyEvent           qquicktextinput.cpp:4595-4798
  │    · Esc 不匹配任何 QKeySequence 分支（全文件**无** QKeySequence::Cancel）
  │    · 落到末尾 `if (unknown) event->ignore();`                    ← :4794-4795
  │      （前置闸门是 QInputControl::isAcceptableInput：空文本 / 控制字符 → false，
  │        qinputcontrol.cpp:23-55）
  │
  ├─ 回到 deliverKeyEvent 的 do-while：isAccepted()==false ⇒ item = parentItem()
  │    …沿父链一路上行…
  │
  └─ keySink 的 Keys.onPressed（MainWindow.qml）
       ⚠️ 修复前：
         if (event.key === Qt.Key_Escape && stack.currentIndex !== sky) { returnToSky() }
         ← **写在焦点守卫之前** ⇒ 守卫被绕过 ⇒ 跳页
```

### 为什么"绕过"这件事这么刺眼

`ActionRouter::routeKey()` 的第一行就是 `if (!canDispatchToSky()) return false;`
——**守卫是"发天空快捷键"这个动作的唯一前置判断**。Esc 返回虽然被 T20 定为"纯 UI 习惯、
不进 ActionRouter"（这个决策本身没错），但它**漏掉了同一个前提**：用户此刻是不是在输入。
于是"不在 ActionRouter 里"变成了"不在守卫管辖内"，而 Esc 恰好又是输入框里最常用的键。

---

## 3. 一个**不存在**的缺陷：组合期间的按键根本到不了 App

这一节是为了**防止后来人凭想象修 bug**，所以单列。

`src/plugins/platforms/cocoa/qnsview_keys.mm:136-144`：

```objc
bool accepted = true;
if (m_sendKeyEvent && m_composingText.isEmpty()) {      // ← :137 闸门
    if (didInterpretKeyEvent ? m_sendKeyEventWithoutText : isSpecialKey(keyEvent.text))
        keyEvent.text = {};
    accepted = keyEvent.sendWindowSystemEvent(window);  // ← 唯一产出 QKeyEvent 的地方
}
```

**`m_composingText` 非空 ⇒ 这一整段被跳过 ⇒ 不产生 `QKeyEvent`。**
组合期间按空格/数字/回车选词、按 Esc 取消组合，全都到不了 Qt 应用层。

Esc 取消组合走的是 AppKit 的 `cancelOperation:`（`:185-204`）：IME 回调时把
`m_sendKeyEvent` 置 `true` 后**直接 return**（`:195-198`），最终仍被 `:137` 的
`m_composingText` 闸门挡住。

**结论**：`IT-14`（组合态注入 Esc 不得跳页）在真机上属于**"比真实更严苛"**的测试——
真机组合中它连门都进不来。那为什么还要测？两条理由：

1. **它测的是"即使到达也必须被拦"**，这是一条**不变式**。将来若有人在 QML 层加
   `Keys.onPressed` 的 pre-edit 处理、或换平台（Windows 的 IME 行为与 macOS 不同，
   TNS 未必有同样的闸门），这条不变式就是防线。
2. **它的判别力是实测出来的**，不是假定的：负控①（撤守卫）下它**真红**
   （页 2→1）。判据对"事件形状/到达与否"敏感 ⇒ 不是恒绿摆设。

---

## 4. 修法

`src/ui/qml/MainWindow.qml` 的 `keySink`（唯一改动点，语义级一行）：

```qml
if (event.key === Qt.Key_Escape) {
    if (!ActionRouter.canDispatchToSky()) {   // T29：先问守卫（口径仍在 C++）
        event.accepted = true                 // 收下但不做事：输入框里不跳页
        return
    }
    if (stack.currentIndex !== root.pageIndex["sky"]) {
        root.returnToSky()
        event.accepted = true
        return
    }
}
```

四个刻意的选择：

| 选择 | 理由 |
|---|---|
| 只问 `canDispatchToSky()`，**不在 QML 复刻判断** | T17 血泪：复刻就会漂移。守卫只有一份，在 C++ |
| `canDispatchToSky()` 早就是 `Q_INVOKABLE` ⇒ **未改 C++ 侧任何逻辑** | 修法零引擎/零命令面影响；ActionRouter.hpp/cpp 一行未动 |
| **不**顺手"清空输入框" | 那是**新增交互特性**（浏览器式两段 Esc），不是修缺陷；要加得单独立项 + 单独判据 |
| 非天空页 + 非输入框 ⇒ 行为与 T20 定案**逐字不变** | IT-16 就是这条的护栏 |

**刻意没做**的还有：没给 `ActionRouter` 加 `app.returnToSky`（T20 已论证"返回是纯 UI 概念，
塞进命令层只会多一条与引擎无关的假动作"）——那会为了修一个守卫漏洞而破坏一条既定架构决策。

---

## 5. 判据（INTERACTCHECK 12 → 16）

| 判据 | 断言 | 为什么这么设计 |
|---|---|---|
| **IT-13** | 注入 preedit → `preeditText=="yueqiu"` **且** `inputMethodComposing==true` **且** `text` 未被污染 | 三腿缺一不可：只断言第一条会放过"把 preedit 直接当正文插进去"的实现；只断言前两条会放过"preedit 与 text 混在一起"。这是合流形态输入法通路的**首次**端到端验证 |
| **IT-14** | 组合态注入 Esc → 页不切 **且** `rate` 不变 **且** `dispatched` 不增 | 三腿成对。只断言"没切页"会放过"页面没切但把 Esc 透传给了引擎"。**修复前必红的那一条** |
| **IT-15** | commit → `text == 组合前正文 + "月球"`、`composing==false`、`preeditText` 清空 | 证明 commit 真写进模型文本、组合区真归位 |
| **IT-16** | **焦点移出输入控件**后注入 Esc → 必须切回天空页 | **判别性对照**。IT-14 是否定式判据，把它实现成"搜索页总是吞掉 Esc"也会绿——那条修复会连同 T20 的返回链路一起杀掉。IT-16 就是 IT-14 的"证明它会红"（血泪第 20 条） |

### 判据**解耦**（首轮负控实测教训）

首版 IT-15 抄了一条"仍停在搜索页"的腿、IT-16 只报"前提失败"就收尾。结果负控①
（撤守卫）下 **IT-14/15/16 三条一起红**——红点糊成一片，看不出判别力落在哪。

处置：**判据各管各的前提**。IT-15 换成"`preeditText` 清空"腿（更贴主题、且独立）；
IT-16 自带**有界布场**（已在天空页则点搜索页按钮、重入本相位，最多 2 次，不虚增判据数）。
改完之后三轮负控**各命中一条**，读数干净。

---

## 6. 三轮代码级负控（各命中一条，互不串扰）

| 负控 | 手手术 | 结果 | 命中 |
|---|---|---|---|
| ① | 撤掉守卫（回到 T29 修复前的 `if (event.key === Esc && page !== sky)`） | 15/16 rc=10 | **只有 IT-14 红**（页 2→1） |
| ② | **过宽修复**：无条件 `event.accepted = true; return` | 15/16 rc=10 | **只有 IT-16 红**（`currentIndex=2`，页没切）。⚠️ 此负控下 **IT-14 是假绿** |
| ③ | 判据取值敏感性：preedit 注入值改成 `yueqiu-bogus` | 15/16 rc=10 | **只有 IT-13 红** |

- 负控② 是"否定式判据单独绿没有意义"的**实证**：同一个"Esc 不跳页"的断言，在过宽修复下
  绿得毫无意义，只有 IT-16 能拆穿。这是血泪第 20 条（T28 新增）的第二次实例。
- 负控③ 证明 IT-13 的断言**读的是真值**，不是恒真条件。
- 负控①②改 `.qml`、③改 `main.cpp`；三份文件先 `cp` 到 `/tmp/t29-bak/`，
  每轮后 `cmp`/`md5` 校验还原，`grep -c "负控"` 确认零残留。
  **绝不用 `git checkout --`**（会连未提交的本次改动一起抹掉）。

日志：`docs/evidence/2026-09-29-t29-ime/negctrl/`。

---

## 7. 仪器加固与踩坑

### 7.1 🔴 新环境门：`displaysleep=2` ⇒ 跑之前必须唤醒显示器

**实测**：本机 `pmset -g custom` 电池档 `displaysleep=2`（2 分钟息屏）。

- 现象：构建花掉几分钟后屏幕已睡，`requestActivate()` **再也拿不到焦点** ⇒
  `QGuiApplication::focusObject()` 恒 `nullptr` ⇒ `IT-06` 明确判红
  「仪器不可用」并 `exit(10)`；因为 IT-06 在相位 7，**后面 9 条判据全不跑**
  （首轮实测连续 3 跑卡在 `判据 5/6`，见 `instrument-unavailable-first-attempt.txt`）。
- 处置：`tools/t29-verify.sh` 内置 `wake_display()`——
  `caffeinate -u -t 2`（模拟用户活动唤醒）后挂 `caffeinate -dimsu -t 7200` 常驻。
  加上之后 5/5 稳定。
- ⚠️ **与既有记忆的区别**：那条"`caffeinate -dimsu` 无效——别再试"说的是**锁屏**场景；
  这里是**显示器休眠**场景，`caffeinate` **有效**。两者别混淆。
- 仪器**行为正确**：判红 + 标注仪器不可用 + **不洗成 PASS**（血泪第 17 条的既定处置）。
  真正的问题是"套件在 IT-06 就短路、后续判据不跑"——记录为可改进项（见 §9）。

### 7.2 🔴 备份快照必须在**所有编辑完成之后**取

本轮踩了一次：负控前把 `main.cpp` 备份到 `/tmp/t29-bak/`，之后**又加了两处判据解耦改动**，
再还原时把解耦一起回滚了。跑出的 5 次"全绿"其实是**旧版判据**的读数——靠日志文案差异
（IT-15 少了 `preeditText` 腿）才发现。

这是"`git checkout --` 会抹掉未提交改动"的同族陷阱，但更隐蔽：**看起来有备份、实际备份过时**。
规矩：**备份 → 编辑 → 还原**，备份永远是最后一步编辑之后的状态；还原后除了 `md5`/`cmp`，
还要**读一遍关键文案**做语义校验。

### 7.3 `QInputMethodEvent` 的拷贝赋值是 `deleted`

首版写 `QInputMethodEvent ev; ...; ev = QInputMethodEvent(preedit, attrs);` ⇒
`error: overload resolution selected deleted operator '='`。必须一次构造到位。

---

## 8. 读数

| 项 | 读数 |
|---|---|
| 正题 INTERACTCHECK | **16/16 rc=0 ×5**，5 跑 IT-13..16 读数 `md5` **逐位一致** |
| IT-13 | `preeditText="yueqiu"`、`inputMethodComposing=true`、`text=""` |
| IT-14 | 页 `2 → 2`、`rate 1 → 1`、`dispatched 1`（=基线） |
| IT-15 | `text="月球"`（=组合前正文+提交串）、`composing=false`、`preeditText=""` |
| IT-16 | `currentIndex=1`（天空页）—— 焦点移出后 Esc 仍返回 |
| 负控 ①②③ | 各 `15/16 rc=10`，各命中 IT-14 / IT-16 / IT-13 |
| 回归 | 11 项全 `rc=0`（searchcheck / actioncheck / locatecheck / locate-uicheck / timecheck / timeuicheck / returnuicheck / replaycheck / clockcheck / **a2-metal 逐像素** / **dyn-engine 3/3** + **dyn-stub 3/3**） |

**为什么回归里没有 S3 旧宿主**（T28 跑了、T29 不跑）：本轮改动面 =
`src/ui/qml/MainWindow.qml` + `src/ui/main.cpp`，**二者都不在旧宿主（stellarium）的构建目标里**，
且未动 `src/core/`、未动任何两形态共用的引擎路径 ⇒ S3 对本轮改动**不敏感**，跑了只是噪音。
（T28 跑 S3 是因为捏合链终点 `StelMovementMgr::handlePinch` 与旧宿主共用；T29 的 Esc 守卫
是纯 QML 层，没有对应的共用路径。）这是**主动论证后的取舍**，不是漏跑。

---

## 9. 后移交

| 项 | 状态 |
|---|---|
| **QML 交互级测试：时间页新增控件的视觉层** | ⬜ 仍待做（L2 Qt Quick Test 未覆盖） |
| **可改进项：套件在 IT-06 短路** | ⬜ IT-06 是"窗口激活"这类**环境门**，一旦失败后面 9 条判据全不跑。可考虑把它降级为"前置检查 + 明确标记，但继续跑不依赖焦点的判据"。**本轮不改**（改动套件控制流风险 > 收益，且会动既有多任务共用的相位编号） |
| **可选特性：搜索框两段式 Esc**（有文本先清空、空则返回） | ⬜ 新增交互特性，需单独立项 + 独立判据（本轮刻意不做，见 §4） |
| **时间链路待查线索**（帧泵推进量与 `getTimeRate()` 脱钩） | ⬜ 仍待查（T27 发现，T28/T29 未改） |
| **捏合后首击失灵线索**（`QQuickPinchHandler` native 分支不清理 `currentPoints`） | ⬜ 仍待查（T28 发现，T29 未改） |
| **DYN 显示侧停摆根因** | ⬜ 目前靠替身对照定性"仪器测不到"，未根治 |
| **Windows 侧复验**（W 支线） | 🟡 仍停在 T17；T23–T29 持续加大复验价值；阻塞点 `schtasks` 被沙箱禁用，需用户侧配合 |
