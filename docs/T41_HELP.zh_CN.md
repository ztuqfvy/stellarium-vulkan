# T41 — 帮助 / 版本 / 许可证页（A5 第五项）

> 日期：2026-09-30｜分支：`main`｜产物：`stelQuickUI`
> 出处：A-1.0 范围表倒数第二格 `|快捷键编辑、帮助、版本与许可证页面 | 可延后 | 必须 | 中文输入焦点不能触发天空快捷键|`
> —— 第三列约束已由 T29 兜住；「快捷键编辑」由 T40 交付；**本轮交付第一列剩下的三项**。

---

## 1. 一句话结论

帮助/版本/许可证三页全落地：**GPL 全文编进 qrc**（bundle 里根本没有 COPYING）、
**F1..F7 六个老窗口动作被宿主接管**（不接管 F1 真的会弹出老式 QWidget 对话框 —— 探针实测），
`HELPCHECK` **17/17 PASS**，负控两腿（接管域 / 许可证域）红项正交可定位。
顺带抓出 **1 个 T39 时期引入的真缺陷**（`BackendInfo` 把 backendOk 判定写死成 `"Vulkan"`）。

## 2. T41-A 探针：三处开工推断被实测推翻

探针 `HelpProbe.{hpp,cpp}`（`STELQUICK_HELP_PROBE`，只读、零写入、不启帧泵），三轮迭代
93 行读数（`docs/evidence/2026-09-30-t41-help/mac/probe-help-mac.txt`）。开工前我的推断是
「`src/ui/CMakeLists.txt` 里零个 `gui/` 条目 ⇒ 老 QWidget GUI 不存在 ⇒ F1 是没主的键 ⇒
帮助入口只能自建」——**三项全错**：

| # | 推断 | 实测 |
|---|---|---|
| ① | `src/gui/` 没编进本形态 | **编了** —— 在 `src/CMakeLists.txt:330-348` 的 `stelMain` 静态库里，`stelQuickUI` 链接它 ⇒ 整个 QWidget GUI 子系统随库进来 |
| ② | 8 个窗口动作不在注册表 | **全在，且是中文**：`actionShow_Help_Window_Global` 组=Windows 键=F1 文本=「说明」⇒ T40 看到的 "Windows" 组是真读数 |
| ③ | 老对话框起不来 | **F1 真的会弹出老 QWidget 对话框**：`trigger()` 实测 QWidget 总数 **12 → 58（Δ46）** + 可见 `QDialog`；且 `setVisible(false)` **只隐藏不销毁** ⇒ 46 个 widget 常驻 |

⇒ **T41 必须「接管」F1，不能「并存」**——否则 QML 界面下按 F1 就蹦出老式对话框，
"QML 重写"就白做了。

### 其余实测口径（T41-B 的实现输入）

- **版本面全现成**：`Stellarium 26.1+` / `26.1.273-4132aeb [main]` / `26.1` / `26.0`；
  编译期宏（`PACKAGE_VERSION`、`STELLARIUM_BUIDING_VERSION`〔**上游拼写如此，少一个 L**〕、
  `STELLARIUM_COPYRIGHT` 等）在 `stelQuickUI` 编译单元全部可见。
- ⚠️ `QCoreApplication::applicationVersion()` 是 **`1.0.0`**（没设成产品版本）⇒ 版本页必须走
  `StelUtils::getApplicationVersion()`。
- 🔴 **`COPYING` 不在 bundle 里**（`Contents/` 只有 Info.plist/MacOS/translations，
  `Contents/Resources/` 目录都不存在）；`findFile("COPYING")` 命中「./COPYING」**只因
  cwd 恰好是源码树根** ⇒ **分发时必然失效** ⇒ GPL 全文必须**编进 qrc**。
- 贡献者名单 **246 条、去重后 245**（原表自带 1 条精确重复）；排序是**大小写不敏感**
  （首项 `adalava`，不是默认码点排序）。
- 帮助页两半数据源：`<tr>` 40 处 / `hotkeyTextWrapper(` 14 处 ⇒ 一半是**没有 action 的手势**
  （只能硬编码），另一半**来自引擎注册表** ⇒ 后者 T40 的 `ShortcutModel` 已覆盖且可编辑，
  **不复制影子表**。
- 翻译链路健康（`Keys`→「快捷键」等）；`Copyright` 保持英文是对的（老代码明说
  not suitable for translation）。

## 3. T41-B 产品

### 3.1 宿主接管（`ActionRouter` 新机制）

- `setHostTakeover(actionId, on)`：命中接管表的 ID 时 **不转发引擎 `trigger()`**（这正是
  "抑制老对话框"的实现），改发 `hostActionRequested(id)` 由 QML 决定去哪 —— 本类
  **不知道"页"是什么**（"页"是纯 UI 概念，T20 已定过案）。
- `routeKey` **同样查表**：探针 Q8 已证 F1 经 `findActionFromShortcut` 能命中 ⇒ 只接管
  trigger 而不接管键盘路径 = **假修复**。
- 接管仍计入 `dispatchCount` 并发 `dispatched(id, true)` —— "每命令恰执行一次"的
  既有不变量不变（U-ACT-01 语义不变）。
- **注册表**（main.cpp）：F1→help、F3→search、F4→display、F5→time、F6→location、
  F7→shortcuts，共 6 个。**F2 设置 / F10 天文计算 / F12 脚本控制台刻意不接管**
  （没有对应 QML 页 ⇒ 接管到不存在的页只是把"弹老对话框"换成"按了没反应"），
  记入 T42「未支持项」清单。
- QML 侧映射表 = `MainWindow.qml::hostActionPages`（唯一真源）。

### 3.2 `HelpModel`（只读数据面）

- 手势表 23 条（`{group, action, keys, scope}`）：
  **sky 13**（视图导航 3 + 时间操纵 5 + 选择标记 5）+ **legacy 10**（脚本控制台 3 +
  天文计算 7）。`scope` 是**诚实性字段**：legacy = 老式窗口内局部键，本形态没有那个
  窗口 ⇒ UI 红字标注「旧式窗口内，本形态未接入」，**不假装可用**。
- 键名一律 `QKeySequence::NativeText` 生成（macOS 显示 ⌘）—— T40 铁律同源。
- 外链 7 条（老 HelpDialog 的 links 节原样），同时是 `openExternal()` 的**白名单**
  —— 不给 QML 任意开 URL 的口。
- 版本/系统行：引擎引导后 `refresh()` 填（走 `StelUtils`）；UI 用 `ready` 显示空态。
- 贡献者 245 条（先精确去重再 CaseInsensitive 排序）。
- GPL 全文：构造时从 `:/StelQuickUI/COPYING` 读（340 行 / 17992 B，与磁盘 md5 一致）。

### 3.3 页面与接线

- `HelpPage.qml`（索引 7）：摘要 + 跳转快捷键页按钮（**页首**，见 §4 缺陷②）+ 分组
  手势表 + 延伸阅读。列表全用 `Repeater`（**立即创建**，避开"隐藏页 ListView 尺寸 0
  ⇒ delegate 不创建"的 T40 坑）。
- `AboutPage.qml`（索引 8）：版本行/系统行/渲染后端/版权 + GPL 全文弹窗 +
  贡献者 ListView（245 条惰性）。
- `Toolbar.qml`：+`navHelpButton` / `navAboutButton`（**原 8 个导航按钮 objectName 未动**）。
- CMake：`HelpModel`/`HelpCheck`/`HelpPage.qml`/`AboutPage.qml` 入构建；
  `qt_add_resources(stelquickui_license)` 把仓库根 `COPYING` 编到 `/StelQuickUI/COPYING`
  （与 `HelpModel::licenseResource()` 逐字对应 —— 判据 HC-05 拿两边比）。

## 4. 本轮判据/冒烟抓出的真缺陷

1. **`BackendInfo::applyRuntimeApi` 自判 backendOk**（T41-B 冒烟抓帧抓到）：旧实现
   `m_backendOk = (apiName == "Vulkan")` —— 判据字符串**写死**，T39 转 Metal 后恒 false
   ⇒ 工具栏标签/诊断页/关于页状态色**全错**（C++ 日志明明 `backendOk=1`）。修法：
   BackendInfo 不再自判，main.cpp 经 `applyBackendResult(name, ok)` 传入自己的判定
   （`api == wantedApi`）—— **判定只有一份，别复制**（T34「产品侧零语义复制」的教义）。
2. **跳转按钮沉底**（HC-11 判据首轮实抓）：`helpGotoShortcutsButton` 中心 y=1036 >
   窗口 640 ⇒ **用户也点不到** ⇒ 按钮挪到页首（查键位是高频动作，入口必须在首屏）。

## 5. T41-C 判据：HC-01..HC-17

成对 + 判别对照 + 复原（`app/HelpCheck.{hpp,cpp}`，`STELQUICK_HELP_CHECK=1`）：

- **数据面**：HC-01 版本行（含 `applicationVersion="1.0.0"` 的**源分离对照**）/ HC-02
  系统行 / HC-03 贡献者（245、无重复、首项 adalava）/ HC-04 许可证内容 / HC-05 资源
  （`licenseResource()` == `:/StelQuickUI/COPYING` 且 **md5 == 磁盘 COPYING**，`__FILE__`
  推根）/ HC-06 手势表结构（sky 13 + legacy 10）/ HC-07 外链与白名单（**表外必拒**
  对照）/ HC-08 键名平台化（⌘）。
- **UI 面**：HC-09 导航按钮（含"原有 8 个一个不少"回归）/ HC-10 帮助页控件
  （手势行 23/23、**可见** legacy 标记 10/10）/ HC-11 跳转快捷键页 / HC-12 关于页控件 /
  HC-13 许可证弹窗端到端（**UI 文本 == 数据面**）。
- **接管**（核心）：HC-14 接管表（6 个逐字比 + **F2/F10/F12 刻意不在**）/ HC-15 trigger
  路径（F1 ⇒ 信号 +1、index=7、**QWidget Δ=0**；**判别对照**：撤销接管再 trigger ⇒
  **Δ46 老对话框真的弹出** —— 证明"接管在起作用"，与探针 Q14 数值完全一致）/
  HC-16 键盘路径（`routeKey(F1,0)` 同样被接管）/ HC-17 复原（配置零污染 + 可见顶层回基线）。

### 判据自身四处方法缺陷（非产品，均已修）

1. HC-11/HC-13 首轮 **NA**：按钮沉底点不到（中心 y=1036/879 > 窗口 640，陷阱 47 的
   几何覆盖先验拦住）⇒ 判据加 **`ensureVisible()`**（沿父链找 Flickable，把 `contentY`
   滚到目标居中；滚动后单独一步 + 400ms）。帮助页按钮是**产品缺陷**（挪页首），
   许可证按钮在页中下部是**合理布局**（修仪器不修产品）。
2. HC-17 首轮红：基线可见顶层混入 **QSplashScreen/QProgressBar**（启动瞬态）⇒
   快照排除这两类。
3. 🔴 **HC-16 在负控 A 下假绿**：HC-15 判别对照的恢复段无条件
   `setHostTakeover(id,true)`，把被负控打断的路径**自己修好了**（hostActionRequested
   0→1、index=7，看着像接管生效 —— 其实是判据刚注册的接管在干活）。修法：恢复
   只恢复到**布场时的状态**（"诊断不得污染被诊断的量"的又一面）。
4. HC-17 被 HC-16 透传弹出的可见 QDialog 连带红 ⇒ HC-16 末尾补
   "每步自己收尾"的清理 —— 后面的判据只反映自己那件事。

## 6. 三腿读数（mac，`tools/t41-verify.sh all 3`）

| 腿 | rc | 判据 | 红项 |
|---|---|---|---|
| 正题 ×3 | 0 | **17/17** | `∅` |
| 负控 A `TAKEOVER_OFF` ×3 | 10 | 14/17 | `[HC-14,HC-15,HC-16]`（**接管域**） |
| 负控 B `LICENSE_OFF` ×3 | 10 | 15/17 | `[HC-04,HC-13]`（**许可证域**） |

- **两腿正交**：`A\B = {HC-14,15,16}`、`B\A = {HC-04,13}`、**无交集** —— 看红项落哪
  半边即知坏在接管还是许可证。
- ⚠️ **HC-05 在负控 B 下不红是对的**：它验"GPL **编进 qrc** 了没有"（资源存在性 +
  md5 对磁盘），负控 B 打的是"**加载**"这条腿 —— 两件事。首版预期（推理填的）把
  HC-05 写进 B 的红项，实测没有，按实跑改写（T40「负控期望值必须实跑出来再写死」
  教训的重演）。

## 7. 验证脚本与回归

`tools/t41-verify.sh`：正题×3 + 两组负控×3 + **17 套件** + INTERACTCHECK + A2 + DYN
双路 + S3 + **两腿正交定位断言**；沿用看门狗 + 显示器休眠门 + Spotlight 环境注记。
T41 改动面（ActionRouter / BackendInfo / main.cpp / Toolbar / MainWindow）⇒
toolbarcheck / actioncheck / **shortcutcheck**（T40 的套件 —— `routeKey` 正是它
SC-14 交互腿的路径，与 T40 把 `hidpicheck` 纳入回归同理）是最近邻，均在回归列表。

**定稿轮读数**：正题 3/3 `17/17 PASS`、负控各 3/3 命中、**16 套件全 rc=0**（此轮跑的
回归列表尚缺 `shortcutcheck`，已补进脚本待复跑）+ `INTERACTCHECK 18/18` + A2 + S3 全 rc=0；
**DYN 双路 0/3 + 0/3 —— 定性为环境受阻，非 T41 回归**，证据链：

1. **替身腿（`test` 纯 Qt 生产者，完全不经过引擎与 T41 的任何代码）同款同位红**
   （C02+C07），⇒ 与产品改动无关；
2. `STELQUICK_A2_TRACE=1` 实测：8 秒全程 `updatePaintNode` **只被调了 3 次**
   （`traceLimit=12` 没跑满）⇒ 场景图**停摆**，不是"慢"；
3. 🔴 **直接物证（运行中 `screencapture` 抓屏）**：整块屏幕被**全屏浏览器**占据，
   stelQuickUI 窗口**根本不在可见空间里** ⇒ macOS 判定窗口 occluded ⇒ Qt Quick
   **不渲染被遮挡的窗口**（按需渲染没东西可画）；
4. 而 `grabWindow` 型判据**照绿**（A2CHECK 逐像素 12/12、C06 抓帧 1920×1280 两帧不同）
   —— 强制抓帧绕过遮挡 ⇒ **假绿掩护**，与陷阱 46 同族。

对照：T40 定稿轮（21:54，load 3.37）DYN 双路 3/3+3/3、`上传 count=588 / 推进 673 帧`；
本轮（23:39，load 12.44）`上传 count=2 / 推进 0 帧`。**处置：不洗 PASS**（脚本 FAILED=1
留在证据里）。

**复跑收口（2026-10-01 00:02，遮挡解除后）—— 全绿**：前台已无全屏应用
（load 2.91、Spotlight 空闲）。`shortcutcheck` rc=0（**15/15**，routeKey 最近邻）、
**DYN engine 3/3 + stub 3/3**（C02 全程推进 **681/657/658** / **269/271/267** 帧，
对比受阻时 0 帧）。首轮 0/3 记录**原样保留**在证据里，复跑读数归档于
`regression-dyn-*-metal-recheck*.txt` + `rc-summary.txt` 补跑段。**T41 至此收官**。

## 8. 未支持项（移交 T42）

- F2 设置窗口 / F10 天文计算窗口 / F12 脚本控制台窗口：**无 QML 对应页** ⇒ 未接管，
  按 F 键会弹出老式 QWidget 对话框（T41-A Q14 已证）。
- legacy 手势 10 条（脚本控制台 3 + 天文计算 7）：键只在老式窗口内局部有效，本形态
  按了没反应（帮助页已红字标注）。
- T42 主题 = 错误页 + 「未支持项」清单（旧 UI 禁止隐式回退）⇒ 正是收编这三项的地方。

## 9. 下一步

**T42 错误页 + "未支持项"清单**（A-1.0 最后一格）→ A6（回归与交接、接口冻结）。
待排期：W-T37/T38/T39/T40 Windows 复验、`LOC-04(b)` 帧延迟量化、T37-X1/X4、
`NightModeCheck` 噪声口径、T33 `findLocations` 排序。
