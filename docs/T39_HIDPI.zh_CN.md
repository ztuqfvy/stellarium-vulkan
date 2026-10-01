# T39 — 高 DPI + 渲染诊断

> 状态：**已收口**（macOS，Metal/MoltenVK，2026-09-30）
> 自检：`STELQUICK_HIDPI_CHECK=1` → `HIDPICHECK:`（**12 条**：HP-00 .. HP-11）
> 探针：`STELQUICK_HIDPI_PROBE=1` → `HIDPIPROBE:`（只报读数，不下 PASS/FAIL）
> 一键复跑：`tools/t39-verify.sh`
> 证据：`docs/evidence/2026-09-30-t39-hidpi/mac/`

---

## 1 开工前的这两格

`A-1.0 范围表`原文（两格，同一轮清空）：

```
|亮度/星等、视场、投影、主题/夜视、高 DPI|基础|必须|天空夜视效果仍由旧后处理负责，避免叠加两次|
|资源路径、错误提示、渲染诊断、配置保存|最小|必须|
```

- 第一格：T37 做掉「主题/夜视」、T38 做掉「亮度/星等、视场、投影」，**「高 DPI」是本轮**
  ⇒ 这一格**至此清空**；
- 第二格：T36 只做掉「配置保存」，**「渲染诊断」是本轮**（同格余下「资源路径 / 错误提示」
  归 T41 帮助页与 T42 错误页）。

两句「必须」的字面要求不同：

- **高 DPI**：不是"有个设置项"，而是**写进去要真的生效、越界要真的挡住**；
- **渲染诊断**：不是"有个诊断页"，而是**诊断数据必须是真的**（能反映帧泵真实健康度），
  不能是装饰。

两者在合流形态**从未验证过**。按项目纪律「**先探针再写 UI**」（T24/T33/T34/T35/T37/T38
一脉相承），先做命令面探针，把"引擎到底怎么反应"变成读数，再决定产品落点与判据口径。

---

## 2 T39-A 探针：7 条实测结论

产物：`docs/evidence/2026-09-30-t39-hidpi/mac/probe-hidpi-mac.txt`（65 行读数）。
结论写进了 `src/app/HiDpiProbe.hpp` 的头注（**判据设计直接引用它**，不是凭印象）。

引擎侧的高 DPI 面（`src/core/StelApp.hpp:94-96`，三个都是 `Q_PROPERTY` + `NOTIFY`）：

| 属性 | 类型 | 含义 | 默认 | 原版控件范围 |
|---|---|---|---|---|
| `screenFontSize` | int | 天空文本字号 | 13 | **[5, 50]**（`ViewDialog` 的 SpinBox） |
| `guiFontSize` | int | GUI 面板字号 | 13 | [7, 50] |
| `screenButtonScale` | double | 按钮尺寸百分比 | 100 | **原版只有配置键，没有控件** |

派生量：`getScreenScale() = getDevicePixelsPerPixel() × screenFontSize / 13`，`getGuiScale()` 同理。

| # | 读数 | 后果 |
|---|---|---|
| ① | 三个 setter **一个都不夹取**（写 99/99/999 原样回读，还顺手 `immediateSave` **落盘**） | **范围闸必须在产品侧**（`AppFacade`；与 T38 共用同一把负控开关） |
| ② | 派生量算术全对（dpp=2、ratio=1、`getScreenScale()`=2；独立验算差 **0.000000**） | 派生量可以当判据的**独立回读**通道（陷阱 43） |
| ③ | 🔴 `getGuiFontSize()` **读的就是 `QGuiApplication::font().pixelSize()`**（绕过 setter 直改全局字体 21 ⇒ getter 读回 **21**），`setGuiFontSize()` **改的也是全局字体** | 它**不是"用户设定值"的可靠真源**（T38「读错量的名字」同族）⇒ 判据**只读写 façade 值 + 引擎 getter 独立回读** |
| ④ | 🔴 `setGuiFontSize(25)` 后 QML 视觉树 **325 项一项都不跟随** | 引擎的 `guiFontSize` 只作用于**老 QWidget 对话框**；QML UI 自有字号 ⇒ **QML 缩放必须产品侧自己做系数** ⇒ DisplayPage 上**只给 `screenFontSize` 一个控件并诚实标注作用域**（见 §3.2） |
| ⑤ | `screenFontSize` 有**强帧效应**：噪声底**逐位相同（哈希一致）** → 字号 13→40 差异 **4.054%**（Δmax **247**），还原后**逐位回到原哈希** | 帧级判据信噪比充足，且**判别对照**（还原回噪声级）天然成立 |
| ⑥ | 静置 1.5s 三条 NOTIFY 增量 **0/0/0** | 这些量**不是每帧变的连续量** ⇒ 滑块**可以安全绑定** |
| ⑦ | `FrameMailbox::Stats` 全字段可读（`published`/`dropped`/`leased`/`sizeGeneration`/`completeSlots`/`readersHeld`/`latestFrameAgeMs`/`bytesPerFrame`） | **渲染诊断有真数据面可暴露**，不需要产品侧自己造统计 |

④的**三重仪器正控**（这一段是本探针方法学上最要紧的部分 —— 负结论必须自己先被验证）：

```
Q4 基准扫描（前）：n=325｜显式 pixelSize 68 项(10~22)｜显式 pointSize 257 项(13~13)｜无字号 0 项
Q4 仪器正控①（读取链路）：显式 pixelSize=33 能读到｜显式 pointSize=37 能读到
Q4 仪器正控②（真实树人为扰动 Label_QMLTYPE_30）：跟随 1 / 不动 324（共配对 325）
Q4 正控③（app font 改 pointSize=25）：跟随 0 / 不动 325（共配对 325）
Q4 配对对比（正经 setGuiFontSize(25)）：跟随 0 / 不动 325（共配对 325）
```

三层各自独立地证明"**测不出**"不是方法失效：

1. **读取链路活着** —— 显式设 `pixelSize`/`pointSize` 都能被本探针读到；
2. **配对对比活着** —— 人为扰动真实树里的 1 个 Label，**恰好查出 1 项**（不是 0 也不是 325）；
3. **点值路径同样零跟随** —— 改 `pointSize`（而不是 `pixelSize`）也是 0/325。

再加一次**前扫**（68 项 pixelSize + 257 项 pointSize = 325，无字号项 0）⇒ 不是"只测了 pixelSize
那一支"漏掉了 pointSize 那一支。**四条合起来**，"325 项一项都不跟随"才是可下的结论。

⑤的细节读数：

```
噪声底帧①②（同状态）：差异 0（逐位相同，哈希相同）
参考帧（字号 13）→ 字号 40：差异 37360/921600（4.054%） 最大通道差=247 平均通道差=4.3657
还原字号 13：差异 0（逐位回到原哈希）
```

⑦的细节读数：

```
latestCompletedFrameNumber=326｜published=326｜dropped=0｜leased=322
sizeGeneration=2｜completeSlots=3 / 槽位 3｜readersHeld=0
latestFrameAgeMs=1｜bytesPerFrame=3686400（= 1280×720×4，可验算）
```

⚠️ `takeLatestFrame()` 会让 `leased +1`（**诊断污染被诊断的量**）⇒ 判据刻意**不报帧宽高**，
`bytesPerFrame` 改用抓帧的 `FrameSample` **独立验算**。

⚠️ 探针还留了一条 config 来源读数（陷阱 44 同族）：三个值都**真的落盘**在
`~/Library/Application Support/Stellarium-quick/config.ini` 的 `gui/screen_font_size` 等键上
⇒ 越界写入不只是"内存里脏"，是**写进用户配置**。

---

## 3 产品实现（T39-B）

### 3.1 `AppFacade` 高 DPI 面

- **6 个 `Q_PROPERTY`**：`screenFontSize` / `guiFontSize` / `screenButtonScale`（读写，共用
  NOTIFY `displayParametersChanged`）+ `screenScale` / `guiScale` / `devicePixelsPerPixel`
  （派生量，只读）；
- **7 个 `CONSTANT` 范围常量**：`screenFontSizeMin=5` / `Max=50` / `Default=13`、
  `guiFontSizeMin=7` / `Max=50`、`screenButtonScaleMin=50` / `Max=200`
  ⇒ **QML 滑块直接绑，范围不在 QML 里硬编码**（T38 同款）；
- **范围闸**：新增整数版 `clampIntoInt(int&, lo, hi)`（与 T38 的 `clampInto()` 同一套语义），
  **先夹取再写引擎**，并记 `lastDisplayRefusal`（`ok` / `not-engine` / `out-of-range`）——
  **不静默吞掉**。负控开关**复用 T38 的同一个** `STELQUICK_DISPLAY_GATE_OFF`：
  语义就是"关掉范围闸"，T38/T39 两族判据共用一把。
- **转发**：`ensureDisplayParamsForwarding()` 末尾补 3 条连接（`screenFontSizeChanged` /
  `guiFontSizeChanged` / `screenButtonScaleChanged` → bump），**与 T38 完全相同的位置与写法**。

> ⚠️ `screenButtonScale` 的 **[50, 200]** 是**产品自定** —— 原版只有配置键、没有控件，
> 没有"照抄的范围"可抄。判据与文档都**显式标注**这一点（不假装是上游语义）。

### 3.2 `DisplayPage.qml`：**只给一个控件，并诚实标注作用域**

新增「界面字号（天空文本）」GroupBox：

- `displayScreenFontSlider`（Slider，`stepSize:1` / `snapMode: Slider.SnapAlways`，
  `from`/`to` 绑 façade 范围常量，`value` 绑 `appFacade.screenFontSize`）；
- `displayScreenFontValue`（当前值标签）；
- `displayScaleStatusLabel`（**缩放状态说明**）。

**为什么不把 `guiFontSize` / `screenButtonScale` 的 SpinBox 一起搬过来**：探针结论④已经证明
引擎侧那两个量对 QML 视觉树**零影响** ⇒ 搬过来就是**假控件**（拖了没反应、或者更坏 ——
只改了一个老对话框根本不会出现的字号，同时**写穿用户 config**）。所以：

- 只做 `screenFontSize`（它**真的**有 4.054% 的帧效应，是唯一名副其实的 QML 高 DPI 项）；
- 另外两个量在 `displayScaleStatusLabel` 上**明白写着**"只作用于旧式对话框，本页不提供控件"。

> 这条是「**判据只验被声称的命题**」（TRAPS 75）在产品侧的对偶：
> **UI 只提供被验证过真的有效的控件**。搬一个零作用的滑块上去，等于在 UI 层造一个
> 永远为真的假命题。

### 3.3 `BackendInfo`：运行时诊断数据面

新增 13 个 `Q_PROPERTY`（NOTIFY `refreshed`）+ `Q_INVOKABLE void refresh()` +
`setFrameMailbox(FrameMailbox*)`：

```
frameNumber / framesPublished / framesDropped / framesLeased / completeSlots / slotCapacity /
readersHeld / latestFrameAgeMs / bytesPerFrame / sizeGeneration / runtimeDiagAvailable
```

设计要点：

- `refresh()` **只做只读搬运**（`mailbox->stats()` + `latestCompletedFrameNumber()`），
  **不做任何计算、不取帧**；
- 🔴 **刻意不报帧宽高**：拿宽高的唯一现成路径是 `takeLatestFrame()`，而它会让 `leased +1`
  —— **诊断污染被诊断的量**（探针结论⑦）。宽高改由判据用抓帧的 `FrameSample` 独立验算；
- 非合流形态（`mailbox == nullptr`）⇒ `runtimeDiagAvailable=false`，各量回 0
  ⇒ **诚实报"没有"**，不返回捏造值。

### 3.4 `DiagnosticPage.qml`：帧泵健康度

- `Timer { objectName:"diagRuntimeTimer"; interval:500; running:page.visible; triggeredOnStart:true;
  onTriggered: BackendInfo.refresh() }`
- `GridLayout` 显示帧号 / 投递·丢弃 / 取走 / 槽位 / 帧年龄 / 字节
  （objectName：`diagFrameNumber` / `diagPublishStats` / `diagLeased` / `diagSlots` /
  `diagFrameAge` / `diagBytes` / `diagRuntimeNote`）。
- ⚠️ 帧号/帧年龄**每帧都在变** ⇒ **不建绑定**，用 Timer 周期 `refresh()`
  （项目铁律：连续量一律轮询，只有离散状态才 `Q_PROPERTY` + `NOTIFY`）。

### 3.5 接线

- `main.cpp`：头注加 `STELQUICK_HIDPI_PROBE` / `STELQUICK_HIDPI_CHECK`；运行块前缀
  `HIDPIPROBE:`（PASS=DONE）/ `HIDPICHECK:`（0=PASS，10=FAIL，6=UNAVAILABLE/INCONCLUSIVE）；
  正常运行路径末尾补 `backendInfo->setFrameMailbox(&frameMailbox)`。
- 🔴 **顺手修掉一个启动参数缺陷**：`startPage` 的三元链里 `STELQUICK_PAGE` 排在**最后**，
  任何起引擎的模式都会把它吞掉 ⇒ 诊断页（它是运行时数据中心，必须**引擎活着**才有人喂数据）
  用 `STELQUICK_PAGE=diag` 根本切不过去。改为**显式指定优先**：
  `pageEnv.isEmpty() ? <原三元链> : pageEnv`。

---

## 4 🔴 首轮 FAIL 反哺：判据抓出一个真产品缺陷

首轮 `HP-08` FAIL。**这是本轮最有价值的产出**。

### 4.1 真缺陷：`Q_PROPERTY` 的 WRITE 函数不能当方法调

`DisplayPage.qml` 的滑块首版写的是：

```qml
onMoved: appFacade.setScreenFontSize(Math.round(value))   // ❌
```

`Q_PROPERTY` 的 **WRITE 函数不是 `Q_INVOKABLE`、也不是 slot** ⇒ QML 侧调用它会**静默**扔

```
TypeError: Property 'setScreenFontSize' of object AppFacade(0x...) is not a function
```

**用户症状**：拖滑块 **UI 上的数字会动**（那是 `value` 的本地值），**引擎纹丝不动**。
判据读数极具辨识度：

```
HP-08 已真实点击滑块 70% 位置 —— clickAt 返回 true｜点击后 UI value=37（点前 30）
HP-08 前半：真实点击 ⇒ façade=37（点前 30）⇒ onMoved 通路活      ← 修复后
                                        façade=30（点前 30）      ← 修复前（缺陷态）
```

治法：改**赋值语法** `appFacade.screenFontSize = Math.round(value)`。

⚠️ **这个缺陷只有 `HP-08` 的"真实点击"才抓得到**：T38 的 DP-07 只做了"引擎侧改 ⇒ UI 跟随"
（单向），没有"UI 侧交互 ⇒ 引擎跟随"这条腿；而 T38 的五个滑块恰好**用的就是赋值语法**，
所以没同病 —— 但那条腿当时根本没人验过。

### 4.2 方法缺陷：`invokeMethod("increase")` 不是"交互"

首版交互腿用 `QMetaObject::invokeMethod(slider, "increase")` 模拟用户拖动，实测**不发
`moved` 信号**（Qt 6 只在**真实鼠标输入**路径发）⇒ façade 保持 30、判据只能记 NA。
⇒ 交互腿必须**真实点击**（`clickAt` 最外层注入 `QMouseEvent`，ToolbarCheck/AC-12 同款）。

### 4.3 方法缺陷：隐藏页 / 视口外 / `isVisible()`

顺着 4.2 修下去又连着三个：

1. **隐藏页不收事件** ⇒ 必须先**真实点导航按钮**切到显示页（`clickAt(navDisplayButton)`）；
2. **`isVisible()` 不看 ScrollView 视口裁剪** ⇒ "可见"照样点了个寂寞
   （滑块 `sceneY=819`，窗口高 640）⇒ 点击前必须**滚进视口**（`contentY 0→499`）；
3. **找 Flickable 不能从 root 向下 DFS** —— StackLayout 的**所有页面**都在视觉树里，
   会先摸到别页的 Flickable ⇒ 改 `findAncestorFlickable()`，沿**视觉父链**向上找。

### 4.4 探针首轮的两处方法缺陷（已修，记在案）

- **漏冻结仿真** ⇒ "字号帧效应"算出 99.99%（全是时间流逝的污染，零判别力）；
- **只读 `pixelSize`** ⇒ 漏掉 257 项 `pointSize` 描述项（见 §2 结论④的三重正控）。

---

## 5 判据 `HIDPICHECK`（HP-00 .. HP-11，12 条）

见 `src/app/HiDpiCheck.hpp` 头注（判据清单、环境前提、负控、退出码）。

**布场**：冻结仿真（帧静定 —— 像素比较的前提）+ **大气不动**（探针已证：默认布场下天空文本
可见、效应 4.054%，比 T38 那样专门关大气更省事，也更接近用户真实看到的布场）。收尾全部还原。

| 分组 | 判据 | 内容 |
|---|---|---|
| 状态面 | HP-00 | **噪声底门**：冻结后同状态两帧**低于噪声容差**（口径有效性前置，环境门） |
| | HP-01 | **引擎面往返**：façade 写 33 ⇒ façade 读回 33 ∧ **引擎 getter 独立回读** 33 |
| | HP-02 | **范围闸（screenFontSize）**：写 99 → 50 ∧ 写 0 → 5；引擎独立回读一致 ∧ refusal=`out-of-range` |
| | HP-03 | **范围闸（guiFontSize）**：写 99 → 50 ∧ 写 0 → 7 |
| | HP-04 | **范围闸（screenButtonScale）**：写 999 → 200 ∧ 写 0 → 50（⚠️ 范围是产品自定） |
| | HP-05 | **派生量算术验算**：`getScreenScale()` == dpp × 字号/13，`getGuiScale()` 同理（独立算一遍对比） |
| UI 腿 | HP-06 | **控件齐备**：三个 objectName 找到 ∧ 滑块 `from`/`to` == façade 范围常量 ∧ `value` == 当前值（**视觉树递归** —— 陷阱 45） |
| | HP-07 | **绑定腿**：**引擎侧**改字号 ⇒ UI 滑块/标签跟随（守 T15 铁律） |
| | HP-08 | **交互后绑定仍活**：先**真实点击**（验 `onMoved` 通路 ⇒ façade 真的变），**再**引擎侧改 ⇒ UI 仍要跟随 |
| 帧级 | HP-09 | **字号帧效应（成对 + 判别对照）**：写 40 ⇒ 上游帧差异 ≥ 效应门；还原 ⇒ 帧回噪声级 |
| 数据面 | HP-10 | **诊断数据面健康**：`published>0` ∧ `dropped≤published` ∧ `completeSlots∈[0,槽位]` ∧ **`bytesPerFrame` == 宽×高×4**（用抓帧 `FrameSample` 独立验算） |
| 复原 | HP-11 | **复原**：三个 getter 全回初值 ∧ 仿真状态恢复 |

⚠️ 判据清单**就是 12 条**，`mark()` 按 **id 去重**（HP-02/03/04 各有"上沿 + 下沿"两次 mark，
同一 id）⇒ **分母是 /12**，不是把上下沿拆开数的 /15。

### 5.1 为什么 HP-08 要比 T38 DP-07 更强

Qt 6 的 `Slider` 有个已知形态：**用户交互会打破 `value` 绑定**（交互写 `value` 是命令式的，
绑定被销毁）。所以"引擎侧改 ⇒ UI 跟随"在**交互之前**成立，**不代表交互之后还成立**。
T38 的 DP-07 从没交互过 ⇒ 测不到这个形态。HP-08 先真实点击、再引擎侧改 ⇒ 才能测到
"交互是否把绑定杀死"。

实测：**交互后绑定仍活**（引擎侧写 25 ⇒ UI value=25）。这条也是 4.1 那个真缺陷的载体。

### 5.2 HP-10 为什么不报帧宽高

`FrameMailbox` 暴露宽高的唯一现成路径是 `takeLatestFrame()`（会 `leased +1`）
⇒ **诊断污染被诊断的量**。改成：`bytesPerFrame` 用抓帧的 `FrameSample.width/height`
**独立验算**（`1280×720×4 = 3686400`），`takeLatestFrame()` 一次都不调。

---

## 6 负控（证明判据承重；红项集合**两两不同**）

| 组 | 开关 | 作用 | 预期红项 | 实测 |
|---|---|---|---|---|
| A | `STELQUICK_DISPLAY_GATE_OFF=1` | 旁路 `clampIntoInt()` + `clampInto()` | **恰好** `[HP-02, HP-03, HP-04]` | rc=10 / `9/12` |
| B | `STELQUICK_DISPLAY_FWD_OFF=1` | 断引擎→façade 的**订阅** | **恰好** `[HP-07, HP-08]` | rc=10 / `10/12` |

负控 A 的读数**自带证据**（不只"红了"）：

```text
[FAIL] HP-02 范围闸（上沿）：写 99 ⇒ façade=99 ∧ 引擎=99 ∧ refusal=ok（原版 SpinBox 上限 50）
[FAIL] HP-03 范围闸（上沿）：写 99 ⇒ façade=99 ∧ 引擎=99（原版 SpinBox 上限 50）
[FAIL] HP-04 范围闸（上沿）：写 999 ⇒ façade=999 ∧ 引擎=999
```

这三行把 T39-A 探针的结论①**在判据层又坐实了一遍**：引擎 setter 真不夹取，写多少收多少
（并且会落进用户 config）。

负控 B 的读数：

```text
[FAIL] HP-07 绑定腿：引擎侧写 30 ⇒ UI 滑块 value=13 不跟随
[FAIL] HP-08 交互后绑定仍活：真实点击交互后，引擎侧写 25 ⇒ UI 滑块 value=37 不跟随（绑定已被交互杀死）
```

⚠️ HP-08 的负控读数**读起来像"绑定被交互杀死了"，但实际是"订阅被断了"** ——
因为负控把两条腿（façade→引擎 notifier→UI）里的**中间段**掐掉了，UI 停在上一次点击的 37。
判据的**句子**只描述观测（"不跟随"），**对的**；括号里的机理归因是推测，**负控下不成立**。
（真要分辨"交互杀绑定"和"订阅被断"，得看正题 —— 正题里两条都活、值=25 跟随。）

三组红项集合：`∅` / `{HP-02,HP-03,HP-04}` / `{HP-07,HP-08}` —— **两两不同**。

---

## 7 实测结果（macOS，Metal/MoltenVK，2026-09-30）

> 数值以 `docs/evidence/2026-09-30-t39-hidpi/mac/rc-summary.txt` 与各 `hidpicheck-run*.txt` 为准。

### 7.1 正题 `HIDPICHECK` ×3

`rc=0` / `判据 12/12` / 红项空。run1 全文关键读数：

```text
初值：screenFontSize=13 guiFontSize=13 screenButtonScale=100 ｜dpp=2 screenScale=2 guiScale=2
布场：仿真冻结 已由判据置停（大气**不动**）

HP-00  噪声底帧①②：有效 帧号=175/200 哈希=8242057bac501e54（**逐位相同**）Δ≤2
HP-01  façade 写 33 ⇒ façade 读回 33 ∧ 引擎 getter 独立回读 33
HP-02  写 99 ⇒ façade=50 ∧ 引擎=50 ∧ refusal=out-of-range；写 0 ⇒ 5/5
HP-03  写 99 ⇒ 50/50；写 0 ⇒ 7/7
HP-04  写 999 ⇒ 200/200；写 0 ⇒ 50/50
HP-05  screenScale 引擎 2 vs 独立算 dpp(2)×13/13 = 2（差 <1e-6）；guiScale 同
HP-06  滑块 from=5/to=50/value=13 vs façade [5,50]/13 ∧ 三个 objectName 全部找到
HP-07  引擎侧写 30 ⇒ UI 滑块 value=30 跟随
HP-08  真实点击 navDisplayButton → 滚 Flickable 0→499 → 真实点击滑块 70% 位置
       ⇒ 点击后 UI value=37（点前 30，façade=37）⇒ onMoved 通路活
       ⇒ 引擎侧写 25 ⇒ UI value=25 跟随
HP-09  字号 13→40：差异 36797/921600（3.993%）Δmax=247 ≥ 门 0.01
       还原后 vs 参考帧（判别对照）：差异 17/921600（0.002%）Δmax=1 ⇒ 回噪声级、效应可逆
HP-10  published=591 dropped=0 completeSlots=3/3 readersHeld=0
       bytesPerFrame=3686400 vs 独立验算 1280×720×4=3686400
HP-11  screenFontSize=13（初值 13）guiFontSize=13（13）screenButtonScale=100（100）｜仿真已恢复 开
```

两条**有信息**的读数：

- **HP-00 的噪声底是"逐位相同"（哈希一致）**，而 T38 布场（冻结 + 大气关）实测是
  **0.079% / Δ1** ⇒ **噪声门限不能跨布场搬**（见 §8.1）。这也是为什么 HP-00 必须在
  **本布场先量一遍**，后面的帧级判据全部以它为参照；
- **HP-09 的效应可逆性是真的**：还原后差异从 3.993% 掉回 **0.002% / Δmax=1**
  （17 个像素、亚 LSB）—— **判别对照**成立，说明 HP-09 的 3.993% 是字号引起的，
  不是"别的东西正好在动"。

### 7.2 负控

| 组 | 跑次 | rc | 判据 | 红项 |
|---|---|---|---|---|
| A `GATE_OFF` | ×2 | 10 | 9/12 | `[HP-02, HP-03, HP-04]`（两跑一致） |
| B `FWD_OFF` | ×2 | 10 | 10/12 | `[HP-07, HP-08]`（两跑一致） |

三组红项集合：`∅` / `{HP-02,HP-03,HP-04}` / `{HP-07,HP-08}` —— **两两不同**。

### 7.3 相邻回归

十五套件（time / returnui / search / action / locate / locate-ui / replay / clock /
timeui / location / toolbar / timelink / config / night / **display**）+ **INTERACTCHECK 18/18
（本批拿到系统焦点）** + A2（Metal）+ DYN 双路 ×3 + S3 旧宿主 —— **全部 `rc=0`**，
脚本层 **3 通过 / 0 失败**，汇总结论 **`SCRIPT-RC=0 FAILED=0`**。

本任务把 **T38 的 `displaycheck`** 也纳入了回归（它是本轮改动的**最近邻**：
T39 就在 `AppFacade` 的显示参数段落里加东西，同一个 `ensureDisplayParamsForwarding()`
末尾补连接 ⇒ 必须证明 T38 那 14 条没被碰坏）。实测 `rc=0`。

### 7.4 定稿轮环境与产物指纹

```text
二进制：40929912 B  2026-09-30 20:30  md5=f0e39fd324c8dfdceb0a35c64c1bd442
批次环境：up 5 days, load averages 2.59 3.62 3.91 → 4.39 4.26 4.13
环境注记：⚠️ Spotlight 批量索引（mdbulkimport）全程在跑 —— T37-X2 撕裂帧 / T37-X3 引导挂死的
          实测负载毒源。本批**仍全绿**；脚本带环境注记 + 看门狗，ENV 劣化跑出来的结果
          不许洗 PASS。
正题三次跑全程 **TypeError 计数 = 0**（缺陷修复的直接旁证）。
```

证据目录 `docs/evidence/2026-09-30-t39-hidpi/mac/` 共 **35 项 / 1.2 MB**：探针读数 + 正题 ×3
+ 负控 A×2 + 负控 B×2 + 15 套件 + INTERACTCHECK + A2 + DYN 双路 ×3 + S3 + `rc-summary.txt`。
本任务**不落帧**（判据只看读数），不需要裁剪。

---

## 8 判据标定与口径修正

### 8.1 🔴 噪声门限**不能跨布场搬**

| 布场 | 噪声底读数 |
|---|---|
| T38 布场（冻结 + **大气关**，暗天空） | 差异 **0.079%** / Δmax **1** |
| T39 布场（冻结 + **大气开**，默认布场） | 差异 **0**（**逐位相同，哈希一致**） |

同一个引擎、同一个"冻结"，换个布场噪声底**从"逐位相同"变成 0.079%**。
⇒ `kNoiseMaxDelta` 只能在**本布场**标定，HP-00 必须先量一遍，后面的帧级判据以它为参照。

T39 沿用的口径与 T38 一致（只取**幅度**、不取面积，TRAPS 74），只是数值上本布场更干净。

### 8.2 门限标定

| 常量 | 值 | 标定依据 |
|---|---|---|
| `kNoiseMaxDelta` | 2 | 沿用 T38 实测抖动幅度上限（本布场实测 0~1） |
| `kEffectMinRatio` | 0.01 | 噪声带的一个数量级以上；实测字号效应 **3.993%** ⇒ 余量近 4 倍 |

`kEffectRatio` 这门比 T38 更宽松，但**判别对照 + 成对设计**保证了它不是"什么都过"：
HP-09 的判别腿（还原后 0.002%）证明**这套取帧+比较方法对"什么都没改"是给 0 的**。

### 8.3 判据口径的一条自我约束

HP-08 负控下那句括号里的机理归因（"绑定已被交互杀死"）在负控状态下**是错的**（真因是订阅被断）
—— 但因为它是**读数注解**、不是判据条件，判据本身没错。已在此记录：**负控读数里的因果注解
不可跨状态复用**，只看它描述的那次观测。

---

## 9 本轮新血泪（已入 `TRAPS.md` 77–81）

| # | 一句话 |
|---|---|
| 77 | 🔴 **`Q_PROPERTY` 的 WRITE 函数不是 `Q_INVOKABLE`/slot** ⇒ QML 里 `obj.setXxx(v)` **静默** `TypeError`（UI 数字照动、引擎不动）⇒ 必须用**赋值语法** `obj.xxx = v`；反向：`obj.someProp` 在 Q_PROPERTY 上是**值**、在 `Q_INVOKABLE` 上是**函数对象**（T19 同族，两个方向都要小心） |
| 78 | 🔴 **`invokeMethod(control,"increase")` 不是"交互"**：Qt 6 控件只在**真实输入路径**发语义信号（`moved`）⇒ 交互腿必须**真实事件注入**；且**隐藏页不收事件**、**`isVisible()` 不看视口裁剪**、**Flickable 要从视觉父链向上找**（DFS 会摸到别页的） |
| 79 | 🔴 **负结论必须自己先被验证**："325 项零跟随"这种否定性结论，要先做**三重正控**（读取链路活着 / 配对对比恰好查出人为扰动的 1 项 / 另一条点值路径同样零跟随）+ 前扫分类计数 ⇒ 才排除"方法失效" |
| 80 | 🔴 **噪声门限不能跨布场搬**：同引擎同"冻结"，换布场从"逐位相同"变 0.079% ⇒ 环境门必须在**本布场**实测 |
| 81 | ⚠️ **诊断不得污染被诊断的量**：`takeLatestFrame()` 会让 `leased +1` ⇒ 用另一条独立路径（抓帧宽高）验算，别为了报一个数就把计数器搞脏 |

附一条产品侧的（不是陷阱，是判据）：**UI 只提供被验证过真的有效的控件**
（§3.2：`guiFontSize`/`screenButtonScale` 实测对 QML 零影响 ⇒ 不上滑块、只做文字说明）。

---

## 10 A5 里的位置与后续

- A-1.0 范围表第一格「亮度/星等、视场、投影、主题/夜视、**高 DPI**」—— **至此清空**；
- 第二格「资源路径、错误提示、**渲染诊断**、配置保存」—— 本轮做掉"渲染诊断"，
  余下「资源路径 / 错误提示」归 T41（帮助/版本/许可证页）与 T42（错误页 + "未支持项"清单）；
- 后续（沿用快照 §6 的顺序）：
  - **T40** 快捷键编辑；**T41** 帮助/版本/许可证页；**T42** 错误页 + "未支持项"清单；
  - **W-T39 Windows 跨平台复验**：✅ **已完成**（2026-10-01，见 `docs/WT37_41_WINDOWS_VERIFY.zh_CN.md`）
    —— `HIDPICHECK 10/11 INCONCLUSIVE`，**唯一 FINDING = `HP-00` 噪声底门**
    （Windows `0.178%` vs mac `0`）⇒ 两重判别（mac 换 **Vulkan** 后端补跑 ⇒ 差异 `0`；
    换时间尺度 2500ms ⇒ `0.185%` 同量级）定性为**渲染非确定/有持续变化源（平台事实）**，
    影响面仅像素级子判据 `HP-09`，其余 10/12 条在 Windows 全绿。
    🔴 顺带得出**方法论发现**：mac 侧验收实际跑在 `STELQUICK_GRAPHICS_API=metal`（代码自称
    "非验收配置"）而 Windows 是原生 Vulkan ⇒ 像素级跨平台对照带后端混淆变量（报告 §4.3）；
  - 挂起项：`#131 T37-X1`（夜视翻转引起 5 个按钮重绘）、`#134 T37-X4`（冻结期 fader 动画停摆）、
    T33 移交的 `findLocations` 排序（完全匹配优先）、`LOC-04 (b)` 帧延迟量化。

---

## 11 产物清单

| 类别 | 文件 |
|---|---|
| 探针 | `src/app/HiDpiProbe.hpp` / `.cpp` |
| 判据 | `src/app/HiDpiCheck.hpp` / `.cpp` |
| 产品 | `src/app/AppFacade.{hpp,cpp}`（高 DPI 面 + `clampIntoInt`）、`src/ui/quick/BackendInfo.{hpp,cpp}`（运行时诊断面）、`src/ui/qml/DisplayPage.qml`、`src/ui/qml/DiagnosticPage.qml` |
| 接线 | `src/ui/main.cpp`（`STELQUICK_HIDPI_PROBE` / `STELQUICK_HIDPI_CHECK` / `STELQUICK_PAGE` 优先级修复 / `setFrameMailbox`）、`src/ui/CMakeLists.txt` |
| 跑批 | `tools/t39-verify.sh` |
| 证据 | `docs/evidence/2026-09-30-t39-hidpi/mac/`（探针读数 + 正题×3 + 负控 A×2 + 负控 B×2 + 回归 + `rc-summary.txt`） |
