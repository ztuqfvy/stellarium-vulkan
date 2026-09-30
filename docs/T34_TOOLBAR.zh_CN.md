# T34 — 真实工具栏 + 显示开关（A4 的**最后一项**）

> 任务：把 `MainWindow.qml` 里 A2 开发期的**临时页切换器**换成真实工具栏，
> 并补上 A-alpha 表里标"**基本开关**"的 QML 承载面 —— 显示开关的**双向同步**。
> 改动面：`src/ui/qml/Toolbar.qml`（**新**）｜`src/ui/qml/MainWindow.qml`（挂载）｜
> `src/app/AppFacade.{hpp,cpp}`（`displayTogglesRevision` token + 三个读侧 `Q_INVOKABLE` +
> `ensureDisplayForwarding()` 懒订阅）｜`src/ui/quick/BackendInfo.{hpp,cpp}`（三个负控开关）｜
> `src/app/ToolbarProbe.{hpp,cpp}`（T34-A 探针）｜`src/app/ToolbarCheck.{hpp,cpp}`（T34-C 判据）｜
> `src/ui/main.cpp`（**纯仪器**：两段相位接线）｜`src/ui/CMakeLists.txt`｜`tools/t34-verify.sh`。
> 判据：新套件 `TOOLBARCHECK` **12 条**（TB-01..TB-12，含 UI 点击腿与绑定重算腿）。

---

## 1 为什么这一项卡在最后

`2026-09-17-03`（测试文档）§8 的 A-alpha 出口**第一条**是
"第 3 节标'必须'的 A-alpha 功能**全部可操作**（拖动/缩放/选中/跟踪、时间、地点、搜索、信息）"。

T33 落地"地点"后，A4 范围表里**只剩工具栏是占位**（`MainWindow.qml:171` 的临时页切换器，
注释自陈"A4 起由真实工具栏取代"）。更关键的是 §3 说明列对开关类的要求：

> "开关状态必须与引擎**双向同步**"

键位透传（`ActionRouter.routeKey`）只能做到**单向触达**：能按 C 开星座线，但**按钮上看不见状态**。
⇒ 这条要求必须有 UI 承载面 + 状态回读，T34 就是它。

## 2 T34-A 探针：先把命令面摸清楚（**先探针、再写 UI**）

`STELQUICK_TOOL_PROBE=1`，`TOOLBARPROBE:` 前缀，**只报读数、不打 PASS**（免得被当判据）。
五项读数（全文见 `docs/evidence/2026-09-30-t34-toolbar/README.md`）：

| # | 读数 | 结论 |
|---|---|---|
| Q1 | 引擎动作注册表 **505 个动作 / 15 个分组** | 注册表规模足够；候选清单不依赖"猜名字" |
| Q2 | 12 个候选**逐项** `checkable / checked / text / key` | **12/12 checkable**；`getText()` 返回**中文**（T24 翻译链路的红利）；快捷键 C/V/R/E/Z/G/Q/A/D/Alt+P/O/Ctrl+N |
| Q2b | `ActionRouter.isAvailable(id)` | 12/12 在路由器侧可见 |
| Q3 / Q3b | `trigger(id)` 后**回读模块 getter**（**独立路径**，不复刻 StelAction） | 4/4 翻转 |
| Q4 / Q4b | `StelActionMgr::actionToggled` 观测 + `AppFacade.displayTogglesRevision` | 发射 4 次；revision = 4 ⇒ Facade 订阅生效 |

⇒ 关键结论：**开关点击可以完全走已有透传路径**（`ActionRouter.trigger(<引擎 action id>)`）⇒
**T34 产品侧零 C++ 改动**（新增的 C++ 全是"读侧投影 + 仪器"）。

## 3 T34-B 产品：工具栏

`Toolbar.qml`（两段式）：

- **第一行**：导航区（返回天空 + 诊断/天空/搜索/时间/地点）+ 右侧渲染后端标签。
  ⚠️ 六个导航按钮的 `objectName` **一字未动**（`skyReturnButton` / `navDiagButton` / …）——
  `RETURNUICHECK` / `INTERACTCHECK` / `AppFacadeCheck` 的"最外层注入"判据都锚着它们。
- **第二行**：**12 个显示开关**（Flow，吃满宽度、窄了换行）。清单照桌面底栏：
  连线 / 名称 / 插图 / 赤道网格 / 地平网格 / 地面 / 方位点 / 大气 / 深空天体 / 行星标签 / 行星轨道 / 夜间模式。

### 3.1 命令路径：单点真源

```
按钮 onClicked → ActionRouter.trigger(引擎 action id)
                    │ 注册表未命中
                    ↓
              引擎透传（findAction → StelAction::trigger）
```

键盘快捷键走 `routeKey` 也落**同一条** `StelAction` ⇒ 按钮态 / 引擎态 / 快捷键态
**天然只有一份真源**。QML **不复刻**任何开关语义（不查、不翻、不记账），
只做两件事：**派发 id** 与**显示回读值**。

### 3.2 为什么按钮**不用** `checkable: true`

`AbstractButton` 在点击路径里会**命令式写** `checked`（`nextCheckState → setChecked`），
而 QML 里对**带绑定的属性**赋值会**销毁绑定**（T32 在 `SpinBox.contentItem.text` 上踩过同族坑）
⇒ 第一次点击后按钮态就**永远不跟引擎**了。
规避：普通按钮，引擎态由自定义属性 `engineOn` 驱动视觉（`●/○` 前缀 + `font.bold`），
"谁写 `checked`"这条路径**不存在**，绑定就不会被弄坏。

> 附：本机 QML 走 **native（macOS）style**，控件定制被拒
> （`The current style does not support customization`，实测 12 个按钮**各刷一条告警**且
> `background` 覆盖**静默失效**）⇒ 状态改用字重 + 前缀符号表达，**不碰** `background`。

### 3.3 为什么回读要 revision token（T15 铁律的又一次应用）

12 个开关逐个做成 `Q_PROPERTY + NOTIFY` 会让通知矩阵翻 12 倍。改用：

```cpp
Q_PROPERTY(int displayTogglesRevision READ ... NOTIFY displayTogglesRevisionChanged)
Q_INVOKABLE bool actionChecked(const QString &id) const;
```

`StelActionMgr::actionToggled` 每发射一次，revision +1（`ensureDisplayForwarding()` 懒订阅）。
QML 绑定**必须真的读 token**：

```qml
property bool engineOn: {
    appFacade.displayTogglesRevision            // ← 这一行不能删
    return appFacade.actionChecked(modelData.actionId)
}
```

QML 绑定只登记"**表达式里实际读过**"的属性。`actionChecked()` 是 `Q_INVOKABLE` **方法、不是属性**
—— 只调它不读 token，引擎翻转后绑定**永远停在首帧**（不是"晚点重算"，是**没有重算的时机**）。
TB-12 就是这条铁律的**直接判据**。

## 4 T34-C 判据：12 条

| 判据 | 内容 | 腿 |
|---|---|---|
| TB-01..04 | 4 个代表开关 `trigger` 后**模块 getter 翻转**（独立回读路径） | 写入 |
| TB-05 | 第二轮 trigger 后 4/4 **回到写前值**（往返一致） | 写入 |
| TB-06 | `actionToggled` 对 4 个 id **各发射 ≥1 次** | 信号 |
| TB-07 | `displayTogglesRevision` **净增 ≥ 2×4** | 订阅 |
| TB-08 | **负控**：不存在的 id ⇒ 拒绝 ∧ revision 不动 ∧ 不发射 | 判别 |
| TB-09 | 12 个按钮**全部找到** ∧ `engineOn` 与引擎**逐个一致** | UI |
| TB-10 | **真实鼠标点击**星座连线 ⇒ 引擎翻转（落点覆盖自证，见 §5.3） | UI 点击 |
| TB-11 | **判别负控**：注册表命令（`app.togglePause`）**不碰**显示开关 ⇒ revision Δ=0 | 判别 |
| TB-12 | 按钮 `engineOn` **跟随引擎**（点击后 + 复原后**双采样**） | 绑定 |

### 4.1 🔴 点击腿为什么必须选**初始为 false** 的开关

负控制造的是"绑定冻结在创建时刻的值"。首版点击腿选 `actionShow_Ground`（boot 初值 **true**）：
点击后引擎翻成 false，而冻结值恰好也是 **false** ⇒ **撞车**，负控**不红**（假绿）。
换成 boot 初值 **false** 的星座连线后，同一缺陷必红。教训：**负控的判据口径要挑"不会撞车"的样本**。

### 4.2 🔴 TB-12 为什么是**双采样**

同理，单次采样也会撞车（REV_OFF 实测一度假绿）。改成**点两次**：翻转后采一次、复原后再采一次。
一个**冻结值**不可能同时等于 `true` 和 `false` 两个引擎态 ⇒ 必有一次不匹配 ⇒ 负控必红。

## 5 本轮新血泪（5 条，全是"判据全绿但界面是坏的"）

### 5.1 Repeater delegate 的 **QObject 父链是空的**

`Repeater` 的 delegate 只 `setParentItem(Flow)`，**QObject parent = null** ⇒
`QObject::findChild`（沿 **QObject** 父链走）**永远扫不到它们**；而静态声明的导航按钮
QObject 父链正常 ⇒ **同一个 API 两种结果**，极易误判成"按钮不存在"。
实测症状：TB-09 报 `0/12 找到`（三轮仪器诊断才挖到）。
修法：先 `findChild`（对静态按钮有效），未命中再定位 `toolbarToggleFlow`、沿**视觉树
`QQuickItem::childItems()` 递归**。

### 5.2 🔴 裸 `Rectangle` 根 ⇒ `implicitHeight = 0` ⇒ **假绿掩护**

旧临时切换器是 `RowLayout`（自带 implicit 尺寸），换成 `Rectangle` 后 ColumnLayout
不知道该给多高 ⇒ 给 **0**：内层 `RowLayout` 高度 **-12**、开关区溢出窗口右侧（Flow 宽 1263 > 窗口 948）。
而 **`Rectangle` 默认不裁剪** ⇒ 按钮**照样画出来**，TB-09 的存在性/可见性判据**全绿**。
真实点击落在根内容控件上（Toolbar h=0 ⇒ `contains()` 恒 false）⇒ TB-10 红，
而其余 11 条照绿 —— 这就是**假绿掩护**的样子。
修法：`implicitHeight: col.implicitHeight + 12`（显式回喂）。

### 5.3 🔴 `childAt` **会撒谎**（第二条仪器面血泪）

想用 `contentItem()->childAt(落点)` 做"点击有没有砸中按钮"的自证 —— **错了**：
按钮明明在 (48,58)、点击**也确实生效**（TB-10 绿、CLICK_OFF 下变红），
`childAt` 仍一路返回根级 `ApplicationWindowContentControl`（返回的是 **contentItem 自己**、
不是它的后代）。
⇒ 改用**几何必要条件**：落点必须落在按钮**及其全部祖先**的矩形内
（`TB-10 落点覆盖：covered=…`）。这条自证**承重**已实测：把 `implicitHeight` 摘掉复现 5.2 的缺陷，
它立刻报 `covered=**false**（未覆盖的祖先：QQuickColumnLayout,Toolbar_QMLTYPE_1）`
—— **精准点名**到工具栏与其内层布局。已固化为负控 D。

### 5.4 `Flow` 不能直接当 `RowLayout` 的 `fillWidth` 子项

`Flow` 的 `implicitWidth` 是**单行排完**的宽度（实测 1263）⇒ 行布局整体溢出；
更坏的是若同时存在第二个 `fillWidth` 项（撑开的 filler），两者**对分剩余宽度**
⇒ 开关区只剩 159px、竖成 **12 行**、`implicitHeight` **回喂**把工具栏撑到 **446px**
（占 640 窗口的 **70%**）。
修法：开关区 `Flow` **独占一行**、吃满宽度；导航行里**只留一个** filler。

### 5.5 环境：连续起停会让 Metal 掉设备

密集跑批时出现 `VK_ERROR_OUT_OF_DEVICE_MEMORY / Device lost`、
`kIOGPUCommandBufferCallbackErrorPageFault` ⇒ 该跑**无判据输出**。
停几秒重试即恢复。⇒ **无判据输出 ≠ 判据失败**，脚本按 rc 与判据行双重判定，**不洗成 PASS**。

## 6 负控：四组，红项**互不相同**（除 C/D 同集但证据不同）

| 组 | 环境变量 | 机制 | 期望 rc / 判据 | 红项 |
|---|---|---|---|---|
| A | `STELQUICK_TOOL_REV_OFF=1` | `AppFacade` **不订阅** `actionToggled` | 10 / 9-12 | **TB-07, TB-09, TB-12** |
| B | `STELQUICK_TOOL_TOKEN_OFF=1` | QML `engineOn` 绑定**不读** revision token | 10 / 10-12 | **TB-09, TB-12** |
| C | `STELQUICK_TOOL_CLICK_OFF=1` | 按钮 `onClicked` **不派发** | 10 / 11-12 | **TB-10** |
| D | `STELQUICK_TOOL_LAYOUT_BREAK=1` | `implicitHeight` 打回 **0**（复现 5.2） | 10 / 11-12 | **TB-10** + `covered=false` |

- **A vs B 分离**：A 关订阅（revision 恒 0 ⇒ TB-07 也红），B 只关"绑定读 token"
  （revision 照常涨 ⇒ TB-07 仍绿）⇒ 两条腿各自承重。
- **C vs D 分离**：C 是"派发被摘"（引擎**没动**，`covered=true`），
  D 是"落点落空"（引擎**没动**，`covered=false`）⇒ "点击腿"与"落点自证"各自承重。

## 7 读数（macOS + Metal/MoltenVK，`tools/t34-verify.sh core 5`，**FAILED=0**）

### 7.1 正题 `TOOLBARCHECK` ×5

**5/5** `判据 12/12 VERDICT=PASS`，残留进程 0。逐条（run1）：

```
前提：12 个候选动作 present=12/12 checkable=12/12
前提：revisionBase=0 写前值 4 个（4/12 入写入腿）
前提：boot 初值 Lines=true, Grid=false, =false, Mode=false
✓ TB-01..04 写入生效：getter 逐个翻转
✓ TB-06 信号腿：actionToggled 发射 4/4 个 id
✓ TB-07 revision 腿：净增 8 ≥ 8
✓ TB-05 往返复原：4/4 回到写前值
✓ TB-08 负控：trigger(actionShow_Nonexistent_T34) ⇒ executed=false revision Δ=0 toggled Δ=0
✓ TB-11 判别负控：registry(app.togglePause) executed=true revision Δ=0（必须为 0）
✓ TB-09 UI 腿：按钮 12/12 找到、态一致 12/12
  TB-10 落点覆盖：covered=true（未覆盖的祖先：<无>）
  TB-10 落点：center=(48.0,58.0) 按钮 scene(0,0)=(6.0,42.0) 84x32
✓ TB-10 UI 点击腿：真实点击星座连线 ⇒ 引擎 true ⇒ false（翻转）
✓ TB-12 绑定重算腿：点击后 engineOn=false vs 引擎=false；复原后 engineOn=true vs 引擎=true
```

### 7.2 四组负控（红项与期望**逐位一致**）

| 组 | rc | 判据 | 红项实测 | 旁证 |
|---|---|---|---|---|
| A `REV_OFF` | 10 | 9/12 | `TB-07, TB-09, TB-12` | revision 净增 0 |
| B `TOKEN_OFF` | 10 | 10/12 | `TB-09, TB-12` | TB-07 仍绿（revision 照常涨） |
| C `CLICK_OFF` | 10 | 11/12 | `TB-10` | `covered=true`（落点对、派发被摘） |
| D `LAYOUT_BREAK` | 10 | 11/12 | `TB-10` | `covered=**false**`，点名 `QQuickColumnLayout,Toolbar_QMLTYPE_1` |

### 7.3 探针 `TOOLBARPROBE`

`VERDICT=DONE`；Q1 = **505 个动作 / 15 个分组**；Q3b 对照 **4/4 行**；Q4 发射 **4 次**、
`displayTogglesRevision = 4`。

### 7.4 相邻回归

10 套件（time / returnui / search / action / locate / locate-ui / replay / clock / timeui /
location）**全 rc=0**；`INTERACTCHECK` **rc=0（18/18，窗口已激活）**——本批**拿到**焦点，
不是 ENV-SKIP；S3 旧宿主 rc=0（8 条）；A2 逐像素 rc=0。

### 7.5 仪器自证（缺一条，上面的 12/12 就不算数）

- `前提：12 个候选动作 present=12/12 checkable=12/12` ⇒ 动作注册面**真的在**（不然判据空跑全过）。
- `✓ TB-09 … 按钮 12/12 找到` ⇒ Repeater delegate 的视觉树查找**真的接上**（不是"找不到就当没有"）。
- `TB-10 落点覆盖：covered=true` ⇒ 点击落点是**几何上站得住**的。
- 负控 C/D 是这三条的**判别对照**：C 摘派发、D 砸布局 ⇒ 各自的红项不同 ⇒ 自证不是摆设。

> ⚠️ 坑：探针 Q3b 的**值**不能进判据（`写后 getter=false` 的**条数**在 1..4 之间浮动 ——
> 引擎把开关态落盘，boot 初值逐跑可变）。判据只认"**4 条对照行**"这件结构性事实。
> 这与全套 TB-* 一律取**写前快照**当对照是同一个道理。

## 8 与既有文档的关系

- 取代 `MainWindow.qml` 的 A2 临时页切换器（注释里承诺的那一句"届时删除本行"已兑现）。
- 履行 A4 计划里"**清点旧 action**"的要求：探针 Q1/Q2 给出注册表规模与 12 个候选的
  `checkable/checked/text/key` 台账。
- `TOOLBARCHECK` 是第 14 个判据套件（前 13 个见计划文档 §10.3）。

## 9 未覆盖与移交

| 项 | 说明 |
|---|---|
| 工具栏**图标/主题** | 现为文字 + `●/○`（native style 拒绝定制，见 §3.2 附注）；A 阶段不阻塞 |
| `actionShow_*` 之外的开关（亮度/星等/投影/主题/高 DPI） | A-alpha 表里标"基础"，本项未列入 12 开关；如需覆盖走同一透传路径 |
| 工具栏在**窄窗口**下的行数 | 现为 2 行（960 宽）；更窄时 Flow 继续换行，未设最小宽度门 |
| `findLocations` 排序 / 时间链路脱钩 / 捏合后首击 / DYN 停摆根因 | 计划文档 §10.2 的余项，与本项无关 |
