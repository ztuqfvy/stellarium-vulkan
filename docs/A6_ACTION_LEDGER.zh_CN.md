# A6 功能台账：引擎 action / 旧 GUI 的处理账（契约第 5 条）

> **出处**：开发计划一 §7 契约第 5 条 ——「**功能范围和旧 GUI/action 处理台账**；
> 计划二不得顺手重写 UI」；测试文档 §8 A-1.0 出口 ——「未支持项有清单和提示」
> （开发指导文档 §A5 同款要求）。
>
> **用途**：这份账是**改动前置**。任何 UI 改动都要先在本文登记"动的是哪一类"，
> 未登记即视为越界（这就是"计划二不得顺手重写 UI"的**可执行形式**）。
>
> **本文是合并件**：把 T15 / T34 / T41 / T42 四处分散的记录并成一张账，
> 数字都能在对应任务文档与判据里复核。

---

## 0. 判定规则（先说清"什么算已接"）

| 类别 | 判定 | 说明 |
|---|---|---|
| **可触发** | `ActionRouter::trigger("<引擎 action id>")` 返回成功，引擎状态真的翻转 | 全注册表**所有**动作都满足 —— 这是 `ActionRouter` 的透传语义 |
| **有专属 QML 界面** | QML 里有控件直接映射到该功能（按钮/开关/输入） | 见 §2 |
| **已接管（防静默弹窗）** | 该 action 的 `trigger()` 与 `routeKey()` **都不转发引擎**，改发 `hostActionRequested` 给 QML | 见 §3 —— **`routeKey` 必须同查表**，只接管 `trigger` 是假修复（T41 陷阱 88） |
| **明确未支持** | 无 QML 界面，但接管后弹「未支持」提示，**不静默打开旧 QWidget 对话框** | 见 §4 |

---

## 1. 注册表全貌（T15 / T34-A 探针实测）

| 量 | 实测值 | 来源 |
|---|---|---|
| 引擎动作总数 | **505** | `TOOLBARPROBE`（T34-A）；`SHORTCUTCHECK` SC-01 复核「模型 505 vs 引擎 505」 |
| 动作分组数 | **15** | `TOOLBARPROBE` Q10 |
| 分组之一「Windows」成员数 | **10** | `ERRORPROBE` Q7（`getActionList("Windows")`）|
| `getText()` 语言 | 中文（本机 `zh_CN`） | `TOOLBARPROBE` Q1 |

> **505 个动作全都可被 `trigger()`**（引擎透传，产品侧零 C++ 语义复制）。
> 下面几节说的是"**其中哪些有 QML 界面 / 哪些被接管**"，不是"哪些能触发"。

---

## 2. 有专属 QML 界面的（T34 / T38 / T39 / T40）

### 2.1 导航按钮（`Toolbar.qml`，10 个）

`navSkyButton` / `navSearchButton` / `navTimeButton` / `navLocationButton` /
`navDisplayButton` / `navDiagButton` / `navShortcutsButton` / `navHelpButton` /
`navAboutButton` / `navStatusButton`

> ⚠️ **读法**：`navDisplayButton` / `navDiagButton` / `navShortcutsButton` /
> `navHelpButton` / `navAboutButton` / `navStatusButton` 这几个是**页面导航**，
> 不直接映射某个引擎 action（它们切的是 QML 页）；前 4 个（天空/搜索/时间/地点）
> 对应的引擎窗口动作见 §3。

### 2.2 显示开关（`Toolbar.qml`，**12 个**，**刻意都不 `checkable`**）

> ⚠️ **这里必须写清，否则会被误读**：这 12 个开关**不是** `checkable: true` 的按钮。
> `Toolbar.qml:162` 有明确注释「⚠️ 刻意**不用** `checkable: true`：AbstractButton 在
> 点击路径里会**命令式写 `checked`** ⇒ **销毁绑定**」（T32 同族陷阱）。
> 勾选态一律由 **`displayTogglesRevision` token + `actionChecked()` 回读**驱动，
> 命令只走 `ActionRouter.trigger(<引擎 action id>)`。
> ⇒ 计划二接手时：**别把它们改成 checkable**，那会把"状态来自引擎"改成
> "状态来自点击"，两者一旦不一致就再也发现不了。

| # | action id | 显示名 |
|---|---|---|
| 1 | `actionShow_Constellation_Lines` | 连线 |
| 2 | `actionShow_Constellation_Labels` | 名称 |
| 3 | `actionShow_Constellation_Art` | 插图 |
| 4 | `actionShow_Equatorial_Grid` | 赤道网格 |
| 5 | `actionShow_Azimuthal_Grid` | 地平网格 |
| 6 | `actionShow_Ground` | 地面 |
| 7 | `actionShow_Cardinal_Points` | 方位点 |
| 8 | `actionShow_Atmosphere` | 大气 |
| 9 | `actionShow_Nebulas` | 深空天体 |
| 10 | `actionShow_Planets_Labels` | 行星标签 |
| 11 | `actionShow_Planets_Orbits` | 行星轨道 |
| 12 | `actionShow_Night_Mode` | 夜间模式 |

状态回读 = `displayTogglesRevision` **token** + `actionChecked()`（T15 铁律：
绑定只登记"绑定表达式里实际读过的属性"）。按钮**刻意不 `checkable`**
（`AbstractButton` 会命令式写 `checked` ⇒ 销毁绑定，T32 同族）。

### 2.3 参数面（不是 action，是产品侧 façade）

| 面 | 覆盖 | 任务 |
|---|---|---|
| 时间 | 日期/时刻/时区/速率/历法 | T19 / T22 / T30 |
| 地点 | 搜索地点 / 按坐标写入 / 当前地点 | T33 |
| 显示参数 | 亮度/星等、视场、**12 个投影 key** | T38 |
| 高 DPI | `screenFontSize` / `guiFontSize` / `screenButtonScale` | T39 |
| 快捷键 | 全表 505 行的改键/恢复/冲突检测 | T40 |
| 夜视 | 夜视开关闭环 | T37 |

---

## 3. 接管表（`ActionRouter::hostTakeoverIds()`，**10 个**）

「接管」= 该动作的 `trigger()` **与** `routeKey()`（键盘路径）**都不转发引擎**，
改由宿主处理。**两条路径必须同查表**——只接管 `trigger()` 会留下键盘后门
（T41 陷阱 88 的原文结论）。

| # | action id | 默认键 | 宿主行为 |
|---|---|---|---|
| 1 | `actionShow_Help_Window_Global` | F1 | 切到 `help` 页 |
| 2 | `actionShow_Search_Window_Global` | F3 | 切到 `search` 页 |
| 3 | `actionShow_SkyView_Window_Global` | F4 | 切到 `display` 页 |
| 4 | `actionShow_DateTime_Window_Global` | F5 | 切到 `time` 页 |
| 5 | `actionShow_Location_Window_Global` | F6 | 切到 `location` 页 |
| 6 | `actionShow_Shortcuts_Window_Global` | F7 | 切到 `shortcuts` 页 |
| 7 | `actionShow_Configuration_Window_Global` | F2 | 弹「未支持」提示（**设置**） |
| 8 | `actionShow_AstroCalc_Window_Global` | F10 | 弹「未支持」提示（**天文计算**） |
| 9 | `actionShow_ScriptConsole_Window_Global` | F12 | 弹「未支持」提示（**脚本控制台**） |
| 10 | `actionShow_ObsList_Window_Global` | ⌥B | 弹「未支持」提示（**观测列表**） |

来源：QML 侧 `MainWindow.qml` 的 `hostActionPages`（6 项）+ `unsupportedActions`（4 项）；
C++ 侧 `ActionRouter::setHostTakeover()`。

> **为什么 7–10 不直接不管**：T42-A 探针**实测**过 —— 不接管时 `trigger()` 会
> **真的弹出旧 QWidget 对话框**（F10 ⇒ QWidget 计数 Δ697；⌥B ⇒ Δ58），
> 而 A-1.0 明文要求「**不静默打开旧对话框**」。
> ⇒ 两难（"接管 = 按了没反应" vs "不接管 = 静默开旧窗口"）的解法是
> **接管 + 弹明确的「未支持」提示**。

---

## 4. 未支持项清单（「状态与错误」页展示，7 条）

| # | 项 | 说明 |
|---|---|---|
| 1 | 设置窗口（F2） | 旧 QWidget 对话框，不迁移；按 F2 弹提示 |
| 2 | 天文计算（F10） | 同上 |
| 3 | 脚本控制台（F12） | 同上 |
| 4 | 观测列表（⌥B） | 同上（**此项是 T41 的遗漏，T42-A 探针发现**） |
| 5 | 全部插件设置 | Oculars / Satellites / Solar System Editor / Meteor Showers / Bright Novae / Exoplanets 六组的动作**可触发但无专属设置页** |
| 6 | legacy 手势 10 条 | 旧 GUI 的鼠标手势（`scope=legacy`），QML 侧未实现 |
| 7 | 高 DPI 两控件 | `guiFontSize` / `screenButtonScale` 对 QML 视觉树**零跟随**（T39 探针：325 项零跟随）⇒ 只提供被验证有效的控件（`screenFontSize`） |

来源：`HelpModel`（手势 23 条 = sky 13 + legacy 10；外链 7 条）、`ErrorModel::rebuildUnsupported()`
（未支持 7 条）、`src/app/ErrorModel.cpp` 的 `kUnsupportedWindows[]` 白名单。

> 🔑 **清单不是凭记忆写的 —— 它自己带真源与漂移自查**（契约第 5 条要的正是这个）：
> - ① 的**显示名与键位**由 `mgr->findAction(id)->getText()` / `getShortcut()`
>   **从注册表取**；取不到时该行会被追加
>   「⚠️ 注册表里未找到该动作（清单与真源不符，需复核）」⇒ **漂移会自己露头**。
> - ② 的「插件动作 N 个」也是 `mgr->getActionList(组).size()` **数出来的**，
>   不是硬编码数字。
> - legacy 手势 10 条 = 脚本控制台 3 + 天文计算 7（T41 定案），帮助页逐条标 `scope=legacy`。
> - ④ 的理由来自 T39 探针实测（`guiFontSize`/`screenButtonScale` 对 QML 视觉树
>   **325 项零跟随**）⇒ 刻意不提供控件，而不是"忘了做"。
>
> ⇒ 计划二接手时：**这几个数字与名字都该继续从真源取**，别改成字面量。

---

## 5. 「不静默打开旧对话框」的覆盖率自查

| 检查项 | 结论 | 判据 |
|---|---|---|
| Windows 组 10 个成员**全部**处置（6 页跳转 + 4 提示） | ✅ | `ERRORCHECK` EC-08「接管表 10 个」 |
| 接管后 4 个动作 `trigger()` **不弹**旧窗口 | ✅ | `ERRORCHECK` EC-09：QWidget Δ=0 + 提示出现 4/4 + 配置零变化 |
| 判别对照：**撤销接管**后旧窗口真的会弹 | ✅ | `ERRORCHECK` EC-10：Δ697 > 0 |
| 键盘路径也被接管（不是只接管 `trigger`） | ✅ | `HELPCHECK` HC-16（`routeKey` 同查表）；T41 陷阱 88 |
| 老对话框**不再写** `DialogSizes/*` | ✅ | `ERRORCHECK` EC-09「配置写入 0 项」+ 脚本 config 零污染门 |

---

## 6. 计划二开工时的登记要求

任何 UI 改动先回答三个问题并登记到本文件：

1. **动的是哪一类**？导航按钮 / 显示开关 / 参数面 / 接管表 / 未支持清单。
2. **是否触碰页面协议**？触碰 7 个上下文属性或 `SkyViewport` 的属性名与语义 ⇒ **禁止**
   （契约第 7 条）。
3. **是否需要回归**？动 §2.2 的 12 个开关 ⇒ 必须复跑 `TOOLBARCHECK`；
   动接管表 ⇒ 必须复跑 `HELPCHECK` + `ERRORCHECK`。这是 `tXX-verify.sh` 里"最近邻回归"
   规矩的延续（每个任务把上一个任务的紧邻套件纳入回归）。
