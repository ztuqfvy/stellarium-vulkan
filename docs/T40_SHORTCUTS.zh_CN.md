# T40 — 快捷键编辑

> 状态：**已收口**（macOS，Metal/MoltenVK，2026-09-30）
> 自检：`STELQUICK_SHORTCUT_CHECK=1` → `SHORTCUTCHECK:`（**15 个 id / 14 条**：SC-01 .. SC-14，其中 05 拆 a/b）
> 探针：`STELQUICK_SHORTCUT_PROBE=1` → `SHORTCUTPROBE:`（只报读数，不下 PASS/FAIL）
> 一键复跑：`tools/t40-verify.sh`
> 证据：`docs/evidence/2026-09-30-t40-shortcuts/mac/`

---

## 1 开工前的那一格

`A-1.0 范围表`原文：

```
|快捷键编辑、帮助、版本与许可证页面 | 可延后 | 必须 | 中文输入焦点不能触发天空快捷键|
```

这一格有三列，得拆开看：

- **第一列**「快捷键编辑 / 帮助 / 版本与许可证」有四个页面 ⇒ **本任务只交付"快捷键编辑"**
  （帮助 / 版本 / 许可证归 T41）；
- **第二列**「可延后」是说优先级，不是说不做；
- **第三列**「中文输入焦点不能触发天空快捷键」**已经由 T29 兜住**了（`keySink` 的 Esc 分支
  先问 `ActionRouter::canDispatchToSky()`，焦点在文本输入上时 `routeKey` 整条短路）。
  ⇒ 本任务**不重做**这条约束，但**必须不去破坏它**（快捷键页自己有个搜索框）。

「必须」的字面要求：**不是"有个列表能看"，而是"改得动、改完真的生效、生效后能持久化、
还能恢复"**。这四件事在合流形态**一件都没验证过**。

按项目纪律「**先探针再写 UI**」（T24/T33/T34/T35/T37/T38/T39 一脉相承），先做命令面探针，
把"引擎到底怎么反应"变成读数，再决定产品落点与判据口径。

---

## 2 T40-A 探针：12 条实测结论

产物：`docs/evidence/2026-09-30-t40-shortcuts/mac/probe-shortcut-mac.txt`。
结论写进了 `src/app/ShortcutProbe.hpp` 的头注（**判据与产品都直接引用它**，不是凭印象）。

引擎侧这张面**全部现成，一行引擎代码都不用改**：

| 引擎 API | 形态 | 行为要点 |
|---|---|---|
| `StelAction::getShortcut()` / `getAltShortcut()` | → `QKeySequence` | 主/备两槽 |
| `StelAction::setShortcut(QString)` / `setAltShortcut` | 槽函数 | `keySequence = QKeySequence(key); emit changed();` —— **不落盘、不发组级信号** |
| `StelActionMgr::findAction(id)` | 查动作 | 按 id |
| `StelActionMgr::saveShortcuts()` | 落盘 | **`beginGroup("shortcuts"); remove("")` 整组清空重建**，只写"与出厂默认不同"的项 |
| `StelActionMgr::restoreDefaultShortcut(a)` / `restoreDefaultShortcuts()` | 恢复 | 内含 `saveShortcuts()` |
| `StelAction::matches(QKeySequence)` | 路由用 | 主备取 `qMax` |

| # | 读数 | 后果 |
|---|---|---|
| ① | `QKeySequence` 往返恒等 **14/15**。唯一不等的是 `PageUp` —— 它是 **Qt 键名别名**：`PgUp`/`PgDown` 是正名，**`PageUp`/`PageDown` 解析成空序列**；`Escape`→`Esc` | 🔴 **键名一律由 C++ 生成**（`QKeySequence(modifiers\|key).toString()`），**QML 绝不自拼**。自己拼 `PageUp` = 静默把键删掉 |
| ② | 判"多键序列"**只能用 `QKeySequence::count()`**：注册表里有个动作的主键就是 `,`（`Key_Comma`，`toString()=","`）⇒ 用"串里有逗号"判会**误计** | 实测多键序列 **12 个**（`count()>1`）+ 纯逗号键 1 个 |
| ③ | `setShortcut()` **即时生效**：回读一致 / `findActionFromShortcut` 命中 / `routeKey` 立刻可路由（`dispatchCount 0→1`）。**但只发 `changed()`、不发 `shortcutsChanged()`** | 下游（模型/UI）要**自己刷新**，别指望信号 |
| ④ | `saveShortcuts()` 落盘：**非 `shortcuts` 组零差异**（整本指纹 770 键，只新增 2 项） | **不写穿物证**；格式是 `"主键 备键"` 拼接串，任一段为空就写**字面量 `""`** |
| ⑤ | **空串 = 移除**：落盘 `"" ""` → 构造时 `split(\s+)` 出 2 段、都回解成空序列 | "清除"是**合法操作**，不是错误 |
| ⑥ | 🔴 **落盘会规范化字符串**：`Ctrl+E, Ctrl+2` → 磁盘 `Ctrl+E,Ctrl+2`（`toString().replace(" ","")` 把分隔空格删了） | 比"改没改成功"要**比序列语义**（`QKeySequence ==`），**不能比字符串**（否则假红）。⚠️ 另：`QSettings` **无需显式 sync**（sync 前后逐字符相同） |
| ⑦ | `restoreDefaultShortcut` **安全**：整组重写但按"等于默认则跳过"剔除 ⇒ **不冲别人的自定义键**（2 项 → 1 项，另一个保留） | 探针**首轮曾得出相反结论** —— 那是"两个靶撞成同一动作"的仪器缺陷，不是产品行为 |
| ⑧ | 出厂注册表**无重复键**（133 种非空键 / **0 重复**） | 表示这一事实的"冲突"列，出厂态**应为 0** |
| ⑨ | `setAllActionsEnabled(false)` 门**只挡 `pushKey`**（成对实测：门关 `false`+状态冻结 / 门开 `true`+状态动了）；**`routeKey` 不查它** | 编辑期间想屏蔽动作**不能只靠它** ⇒ 捕获态必须**自己吃键** |
| ⑩ | 注册表 **505 动作 / 15 分组**；`getText()` 中文、`getGroup()` **英文** | 分组中文标题**必须产品侧映射**（不是引擎给的） |
| ⑪ | "是否被用户改过"的真源 = `conf->contains("shortcuts/"+id)`（**与引擎构造同判据**，`StelActionMgr.cpp:55-67`）；`defaultKeySequence` 是**私有成员读不到** | UI **不显示"默认值"列**（显示不出来；假装显示就是造假）；`customized` 列用它当判据 |
| ⑫ | 配置落点 = T36 的**个人版目录** `.../Stellarium-quick/config.ini` | 改键动的是隔离目录，不碰原版 |

### 2.1 探针首轮的四类方法缺陷（已修，记在案）

| 缺陷 | 现象 | 修法 |
|---|---|---|
| **靶子撞车** | 改键靶与路由靶选成了**同一个动作**（都含 "lines" 且 checkable）⇒ S4/S5 互相覆盖键、S9 的"另一个自定义项"退化成自己 ⇒ **三条读数全失真**（含 Q7 结论翻案） | 挑选靶子时 `if (a == subject) continue;` —— **两个靶必须是不同的动作** |
| **比较口径错** | Q6 拿"磁盘整串（`主键 备键`）"与"内存单键"比 ⇒ 必然不等（**假不一致**） | 按**构造的解析路径**取首段，再比**序列语义** |
| **无效现场** | Q7 现场已被前序步骤破坏 | 重新布场 |
| **没有真组合键** | Q9 用单键测门，测不出 `pushKey` 的跨按键累积 | 用真组合键成对实测 |

⚠️ 这四条**都不是产品的问题**，是**量尺的问题**。探针存在的意义就是在这里把它们烧掉，
而不是把它们带进判据（陷阱 79「否定性结论必须自己先被验证」的同族）。

---

## 3 产品实现（T40-B）

### 3.1 `ShortcutModel`（C++ 单点，`src/app/ShortcutModel.{hpp,cpp}`）

`QAbstractListModel`，**唯一的数据面与命令面**：

| 角色 | 内容 |
|---|---|
| `actionId` / `groupKey` / `groupTitle` | 身份（`groupTitle` 走**中文映射表**，未收录**回落英文**） |
| `title` | 引擎 `getText()`（中文） |
| `primaryKey` / `altKey` | 引擎 `getShortcut()/getAltShortcut().toString()` |
| `checkable` | 引擎 `isCheckable()` |
| `customized` | `conf->contains("shortcuts/"+id)`（口径⑪，**与引擎构造同判据**） |
| `conflict` / `conflictWith` | 全表扫描算出来（主备都算；**主备自撞也算**） |

属性：`filter` / `count` / `totalCount` / `conflictCount` / `customizedCount` /
`statusText` / `statusOk` / `engineReady`。
命令：`setKey(row, which, seq)` / `restoreDefault(row)` / `restoreAll()` / `refresh()`。

**`setKey` 的关键闸**（这条是本轮唯一改产品的真缺陷修出来的，见 §4.1）：

```cpp
QString norm;
if (!seq.trimmed().isEmpty()) {
    const QKeySequence parsed(seq);
    norm = parsed.toString();
    // 🔴 关键那一半是 norm.isEmpty()：QKeySequence("PageUp").count() 是 1、toString() 是空
    if (parsed.count() < 1 || parsed.count() > 4 || norm.isEmpty()) {
        setStatus("无法识别的键位「…」（要清空请按「清除」；键名由界面采集，别手打）", false);
        return false;
    }
}
```

其它两条产品纪律：

- **空输入 = 移除**（口径⑤），走的是**同一个函数**，`norm` 留空即可；
- **改完主动 `refresh()` + `saveShortcuts()`**（口径③：引擎不发组级信号，下游必须自己刷）。

### 3.2 `ShortcutsPage.qml`（`src/ui/qml/ShortcutsPage.qml`）

本页只做**展示 + 键事件采集**，不碰引擎、不做任何键位解析。自己的三条硬规矩：

1. 🔴 **绝不自己拼键名**。捕获到按键只把 `event.key` / `event.modifiers` 交给
   `ShortcutModel.keySequenceFromEvent()`，**键名由 C++ 生成**（口径①：别名键名静默变空 =
   静默删键，且**不报错**）；
2. **捕获态必须吃键**（`event.accepted = true`）。否则事件冒泡到 `MainWindow` 的 `keySink`，
   那儿对非 Esc 键会调 `ActionRouter.routeKey` —— 用户想绑 `C`，结果顺手把"星座连线"开关了。
   吃键是唯一的防串扰手段（口径⑨：`setAllActionsEnabled` 挡不住 `routeKey`）；
3. **Esc 在捕获态 = 取消**（不写值），**非捕获态 = T20 的"返回天空"**（原样不动）。

捕获器 `keyCatcher` 是 **1×1 的 `Item`**（`objectName: shortcutKeyCatcher`）：
`visible:false` 的项**收不到焦点**，所以不能用 `visible` 藏；1×1 既能持焦点又不遮挡点击。
取消/完成后把焦点**还回**原持有者（不还的话键盘停在 1×1 上，用户以为界面卡死）。

列表用 `ListView` + `section.property: "groupKey"` 做分组表头；行内三个按钮：
主键 / 备键（点进捕获态）/ 恢复默认（`enabled: customized`）。

### 3.3 接线

- `main.cpp`：`ShortcutModel shortcutModel;` 与 `appFacade` / `actionRouter` 并列，
  `setContextProperty("ShortcutModel", &shortcutModel)`；**正常路径末尾 `refresh()`**
  （引擎是**延迟引导**的，模型必须在引导完成后灌一次数据）；
- `MainWindow.qml`：`pageIndex` 加 `"shortcuts": 6`，`StackLayout` 加 `ShortcutsPage { }`；
- `Toolbar.qml`：加 `navShortcutsButton`（**上面 7 个导航按钮的 `objectName` 一个没动**）；
- `CMakeLists.txt`：加三个 `app/` 新文件 + 一个 QML。

---

## 4 🔴 首轮 FAIL 反哺：判据抓出一个真产品缺陷

### 4.1 真缺陷：非法键名闸漏了"解析后为空"这一半

原本的闸只查 `QKeySequence::count()`。而：

```text
QKeySequence("PageUp")  →  count() = 1     （Qt 把它当"一个未知组合"存下了）
                        →  toString() = "" （解析成空序列）
```

⇒ 闸**放行**，`norm` 变成空串，写进引擎 = **静默删除该快捷键**。这正是探针①警告的形态。

修法：闸加 `|| norm.isEmpty()`。SC-10 从此红转绿，并留下**判别对照**读数：

```text
SC-10 物证：QKeySequence("PageUp") count=1｜toString=「∅空」
[PASS] SC-10 非法键名闸：「PageUp」被拒=是｜引擎键未变=是｜状态文本非空=是
```

⚠️ **这个缺陷只有"判别对照"才抓得到**：普通"设个键、读回来"的正测**永远绿**
（合法键名两边都过）。这是本项目"**判别对照必须有**"这条纪律的又一次兑现。

### 4.2 判据自身四处方法缺陷（非产品，已修）

| # | 缺陷 | 现象 | 修法 |
|---|---|---|---|
| 1 | `Ctx::finish()` 用的是 `Ctx::onDone` 成员，`run()` 里**从没给它赋值** | 14 步**全跑完**、收尾 `std::bad_function_call`、**零判据输出**（RC=134）。极难从现象反推 | `ctx->onDone = onDone;`（首行 tick 打点才定位到"只崩在收尾"） |
| 2 | 判据台账 id 用 `QStringLiteral("SC-%1").arg(i)` 生成 `SC-1..SC-14`（**无前导零**），而各 note 首词写的是 `SC-01..SC-14` | 只有 `SC-10..SC-14` 匹配、**前 9 条 mark 静默丢失** ⇒ 汇总显示"判据 5/5"却 `VERDICT=FAIL` | 台账改**显式 id 列表**（也让"拆出来的派生条"有名有姓） |
| 3 | QML 绑定的 model 与判据自建的副本**是两个实例** | 自检路径走不到正常路径末尾的 `shortcutModel.refresh()` ⇒ ListView 的 model 是**空表**、**一个 delegate 都不创建**（判据自建副本有 505 行，SC-01 照样绿 ⇒ 看不出来） | SHORTCUTCHECK 运行块里显式 `shortcutModel.refresh();` |
| 4 | `StackLayout` 隐藏页的 `ListView` **尺寸 0 ⇒ delegate 一个都不创建** | 切页后同一步里立刻找 delegate ⇒ 找不到 | 拆成 S12a（真实点导航 + **等 400ms**）/ S12b（再找控件）—— T39 同款血泪 |
| 5 | SC-13 判的是**判据自建的 model**是否跟随 | 交互发生在 QML 侧、改的是 `main.cpp` 那个 context property 实例 ⇒ "引擎对了、副本说不跟随"（引擎读数已证明链路通） | 改读 **UI 自己能观测到的地方**（按钮 `text` 属性） |

---

## 5 判据 `SHORTCUTCHECK`（SC-01 .. SC-14）

三条纪律与 T37/T38/T39 一致：**独立回读**（陷阱 43）/ **判别对照必须有** /
**等待挂写入步**（陷阱 41/69）。

| id | 判据 | 独立回读通道 |
|---|---|---|
| SC-01 | 表完整性 | 模型 `totalCount` vs 引擎 `getActionList().size()`；分组数一致 |
| SC-02 | 行数据抽样（前 10 行） | 逐项 vs 引擎 `findAction` 的 `getText()/getShortcut()/getAltShortcut()` |
| SC-03 | `customized` 口径（**全表 505 行**） | vs `conf->contains("shortcuts/"+id)` |
| SC-04 | 键名生成单点 | 6 样本恒等 + 修饰键中间态为空；**判别对照**：手拼 `PageUp` 直构=空序列 |
| SC-05a | 改键**写引擎** | 引擎 getter 独立回读 |
| SC-05b | 改键**落盘** | **另开 `QSettings` 实例**读磁盘，按构造解析路径取首段、比**序列语义**（口径⑥） |
| SC-06 | **不写穿** | 整本指纹 vs S1 基线，**跳过 `shortcuts/`** 后差异数必须为 0 |
| SC-07 | 冲突检测 | 制造（row1 设成与 row0 同键）⇒ `conflictCount>0` 且两行 conflict；**判别对照**：消解 ⇒ 归零 |
| SC-08 | 单恢复不冲别人 | row0 恢复后 `conf` 项被剔除 **且** row1 的自定义键还在（探针⑦产品级复验，**两个不同动作**） |
| SC-09 | 空串 = 移除 | 引擎键空 + 磁盘 `"" ""` 形态 + 复刻构造解析全部回解为空 |
| SC-10 | 非法键名闸 | `PageUp` 被拒 + 引擎键不变 + 状态文本非空；**判别对照**：物证先打两行 |
| SC-11 | 全部恢复 | `customizedCount` 归零 + 磁盘 `shortcuts` 组空 |
| SC-12 | UI 控件齐备 | **视觉树递归**（陷阱 45：Repeater delegate 的 QObject 父链是空的，`findChild` 扫不到）9 个控件；前置**真实点击**导航切页 |
| SC-13 | UI 交互端到端 | 真实点击导航 → 真实点击主键按钮 → **注入真实 `QKeyEvent`**（Ctrl+Alt+Shift+F7）⇒ 引擎 + **UI 按钮文字**双确认 |
| SC-14 | 复原 | `restoreAll()` 后**整本指纹 vs S1 基线差异数 = 0**（含 `shortcuts` 组） |

**为什么 SC-12 走视觉树而不是 `findChild`**：`ListView` 的 delegate 是 QML 运行时动态生成的，
**其 QObject 父链是空的** ⇒ `findChild` **永远扫不到**（而静态写的兄弟项却扫得到）。
这条是 T34 就已经烧过的陷阱 45，本轮又踩了一次同一个坑（第一版就是这么写的）。

**SC-13 的强约束**：交互腿必须**真实事件注入**（T39 血泪：`invokeMethod` 不发信号/不产生真交互），
且断言要去 **UI 自己能观测到的地方**读（别拿孤立副本当参照）。

---

## 6 负控（证明判据承重；红项集合**两两不同**）

两条负控打的都是 `setKey` 的腿：

| 组 | 开关 | 作用 |
|---|---|---|
| A | `STELQUICK_SHORTCUT_WRITE_OFF=1` | `setKey` **不写引擎**（落盘腿仍在，但**没内容可存**） |
| B | `STELQUICK_SHORTCUT_SAVE_OFF=1` | 写引擎但**不落盘** |

**实测红项集合（真读数，不许美化）**：

| 组 | rc | 判据 | 红项 |
|---|---|---|---|
| 正题 | 0 | 15/15 | `∅` |
| A | 10 | 8/15 | `[SC-05a, SC-05b, SC-07, SC-08, SC-09, SC-11, SC-13]` |
| B | 10 | 12/15 | `[SC-05b, SC-09, SC-11]` |

🔴 **红项比"靶心"大，是设计的事实，不是判据缺陷。**

本套自检是**线性步骤链**（S1 布场 → … → S14 复原），后步的**前提态由前步的写入产生**。
写引擎腿一断，"前提态"根本不存在 ⇒ 凡**经由 `setKey` 承重**（SC-05a / 07 / 09 / 13）
或**依赖前序写入态**（SC-08 / 11）的判据必然红。翻过来说，这正是我们要的：
**判据真的挂在产品路径上**（假绿才是缺陷）。

**两条腿可正交定位**（这才是负控的用处）：

```text
A \ B = [SC-05a, SC-07, SC-08, SC-13]   ← 恰是"引擎写入必须真生效"的集合
B \ A = []                              ← B 的每一项都同时被 A 打中
B     = [SC-05b, SC-09, SC-11]          ← 恰是"断言磁盘态"的集合
```

⇒ 看红项落在哪半边，就能判断坏在**写入腿**还是**落盘腿**。而"全绿"只有在两条腿都活着时
才可能出现 —— 正题的 15/15 就是这个断言。

⚠️ 别把 A 的红项理解成"SC-07/08 也坏了"：它们红是因为**它们的输入没了**。
要看某个判据"本身"是否承重，读 **A\B** 那一半。

### 6.1 首版负控设计的两处错

1. 头注曾**预测** A 恰红 `[SC-05a,SC-05b,SC-07]`、B 恰红 `[SC-05b]` —— **预测是错的**
   （实测 7 项 / 3 项）。已在头注与本文档改成**实测值**，并写清连锁机理。
   **教训**：负控的期望值**必须实跑出来再写死**，不能靠推理填（同族：陷阱 80「噪声门限不能跨布场搬」）。
2. 验证脚本首版把"正题红项"的断言写成 `-n`（要求**非空**）—— 而正题的期望是**空集**。
   ⇒ 脚本自己报了一条假失败（`4 通过 / 1 失败`）。已修，并**在脚本里留了注释记这一笔**。

---

## 7 实测结果（macOS，Metal/MoltenVK，2026-09-30）

> 数值以 `docs/evidence/2026-09-30-t40-shortcuts/mac/rc-summary.txt` 与各 `*run*.txt` 为准。

### 7.1 正题 `SHORTCUTCHECK` ×3

`rc=0` / `判据 15/15` / 红项空。run1 关键读数：

```text
布场：模型 505 行（引擎 505 动作）｜配置 .../Stellarium-quick/config.ini｜shortcuts 组基线 0 项
布场：SC 靶 row0=actionSwitch_Equatorial_Mount｜row1=actionSave_Copy_Object_Information_Global

SC-01  模型 505 vs 引擎 505 动作｜分组 15 vs 15
SC-02  行数据抽样（10 行）：逐项一致
SC-03  customized 口径（全表 505 行）：与引擎判据一致
SC-04  判别对照：QKeySequence("PageUp") 直构=「∅空」⇒ 别名键名确实静默变空，生成器口径成立
SC-04  键名生成：6 样本恒等｜修饰键中间态=✓｜别名对照=✓
SC-05  写入步：setKey(row0, 主, 「Ctrl+Alt+Shift+F9」) 返回 true
SC-05a 改键写引擎：引擎「Ctrl+Alt+Shift+F9」== 新键 ✓
SC-05b 改键落盘：磁盘原样「Ctrl+Alt+Shift+F9 ""」⇒ 首段「Ctrl+Alt+Shift+F9」序列语义 == 新键 ✓
SC-06  不写穿：改键后非 shortcuts 组差异数 = 0（基线 770 键）
SC-07  冲突检测：撞键后 count=2（row0=红 row1=红）｜消解后 count=0 ⇒ 检出与消解都成立
SC-08  恢复默认：row0 的 conf 项被剔除=是｜row1 自定义键「Ctrl+Alt+Shift+F8」保留=是
SC-09  空串移除：引擎键空=是｜磁盘项在=是 原样「"" ""」(2 段)｜复刻构造解析全部回解为空=是
SC-10  物证：QKeySequence("PageUp") count=1｜toString=「∅空」
SC-10  非法键名闸：「PageUp」被拒=是｜引擎键「」未变=是｜状态文本非空=是
SC-11  全部恢复：此前 2 个被改 ⇒ 之后 0 个｜磁盘 shortcuts 组 0 项 ⇒ 清干净
SC-12  切页步：真实点击 navShortcutsButton ⇒ 已投递
SC-12  UI 控件齐备：9 个控件全部在场（已真实点击导航切页）
SC-13  交互端到端：引擎键=「Ctrl+Alt+Shift+F7」✓｜UI 按钮文字=「Ctrl+Alt+Shift+F7」✓
SC-14  复原：整本指纹 vs 基线差异数 = 0（含 shortcuts 组） ⇒ 用户配置零污染 ✓
VERDICT=PASS
```

三条**有信息**的读数：

- **SC-05b 的磁盘形态是 `Ctrl+Alt+Shift+F9 ""`** —— 正是口径④的"`主键 备键` 拼接 + 空写
  `""`"；判据按**构造解析路径取首段 + 比序列语义**，所以不会被"规范化"绊倒（口径⑥）；
- **SC-09 的磁盘形态是 `"" ""`（2 段）** —— "空串 = 移除"在磁盘上的物证，且复刻构造解析
  两段都回解成空序列 ⇒ 重启后仍然是"无键"，不是"一个空字符串的键"；
- **SC-13 的两端读数一致**（引擎 `Ctrl+Alt+Shift+F7` = UI 按钮文字）—— 交互腿真的端到端通了，
  不是"引擎动了、UI 没动"也不是"UI 动了、引擎没动"。

### 7.2 负控

| 组 | 跑次 | rc | 判据 | 红项（每跑一致） |
|---|---|---|---|---|
| A `WRITE_OFF` | ×3 | 10 | 8/15 | `[SC-05a, SC-05b, SC-07, SC-08, SC-09, SC-11, SC-13]` |
| B `SAVE_OFF` | ×3 | 10 | 12/15 | `[SC-05b, SC-09, SC-11]` |

三组红项集合：`∅` / A / B —— **两两不同**，且 `B ⊂ A`、`A\B = {SC-05a,SC-07,SC-08,SC-13}`。

### 7.3 相邻回归

**十六套件**（time / returnui / search / action / locate / locate-ui / replay / clock /
timeui / location / toolbar / timelink / config / night / display / **hidpi**）+
**INTERACTCHECK 18/18（本批拿到系统焦点）** + A2（Metal）+ DYN 双路 ×3 + S3 旧宿主 ——
**全部 `rc=0`**，脚本层 **5 通过 / 0 失败**，汇总结论 **`SCRIPT-RC=0 FAILED=0`**。

本任务把 **T39 的 `hidpicheck`** 纳入回归（它是本轮改动的**最近邻**：T40 在 `main.cpp` 的
自检装配段落里插了一段，且动了 `Toolbar.qml` ⇒ 必须证明 T39 那 12 条没被碰坏）。实测 `rc=0`。

### 7.4 定稿轮环境与产物指纹

```text
二进制：41329752 B  2026-09-30 21:37  md5=de8b47d940aafc82c12b4d6c0bbdd8dd
批次环境：up 5 days, 23:21 → 23:29，load averages 4.13 4.23 4.78 → 2.99 3.21 4.02
环境注记：⚠️ Spotlight 批量索引（mdbulkimport）全程在跑 —— T37-X2 撕裂帧 / T37-X3 引导挂死的
          实测负载毒源。本批**仍全绿**；脚本带环境注记 + 看门狗，ENV 劣化跑出来的结果
          一律**不洗成 PASS**。
```

---

## 8 判据标定与口径

### 8.1 判据数不是 14，是**15 个 id**

SC-05 拆成 **05a（写引擎）/ 05b（落盘）** 两条独立判据 —— 因为它们是**两个不同的腿**，
负控 A/B 的正交定位就是靠这个拆分（没有它，两条负控只能红同一条 SC-05）。台账里
**显式列 15 个 id**，分母就是 **/15**。

### 8.2 SC-11 的前提是"真前提"

SC-11 断言"此前 ≥2 个被改 ⇒ 之后 0 个"。`beforeN >= 2` **不是可选的**：
没有它，`restoreAll()` 在"本来就没改过"的空态下**永远绿**。代价是这个前提**依赖前序写入**
⇒ 负控里跟着红（见 §6）。这是**有意接受**的：宁可红得诚实，不要绿得空洞。

### 8.3 UI 只提供做得到的操作

本页**不给"输入默认值"列、不给"手打键位"入口**：

- 默认值列做不了（口径⑪：`defaultKeySequence` 是私有的，读不到）；
- 手打入口做不得（口径①：手拼键名有别名陷阱，会静默删键）。

⇒ 键位**只能由界面采集**（点按钮 → 捕获 → C++ 生成）。`setKey` 里那句
"要清空请按「清除」；键名由界面采集，别手打"就是这个口径的用户可见面。

---

## 9 本轮新血泪（已入 `TRAPS.md` 83–87）

| # | 内容 |
|---|---|
| 83 | 🔴 **`QKeySequence` 的 `count()==1` 不等于"可用"**：别名键名（`PageUp`/`PageDown`）`count()=1` 但 `toString()=""` ⇒ 只查 `count` 的闸会**放行后静默删键**。闸必须**同时**查"解析后非空" |
| 84 | 🔴 **收尾回调必须是被测对象自己持有的成员**：`finish()` 用 `Ctx::onDone`，`run()` 漏赋值 ⇒ 全程跑完、收尾 `std::bad_function_call`、**零输出**（RC=134）。现象与"判据全挂"完全不像 |
| 85 | 🔴 **台账 id 必须与 note 首词逐字一致**：`arg(i)` 生成的 `SC-1` ≠ 手写的 `SC-01` ⇒ 前 9 条 mark **静默丢失**，汇总出现"判据 5/5 却 VERDICT=FAIL"这种自相矛盾读数 |
| 86 | 🔴 **判据自建副本 ≠ 被测接线实例**：QML 绑定的 model 与判据 `new` 出来的那个是**两个对象** ⇒ 自检路径刷新了副本、UI 是空表，**delegate 一个都不创建**（而 SC-01 照样绿）。交互腿的产物一律去 **UI 自己能观测的地方**读 |
| 87 | ⚠️ **负控的期望值必须实跑出来再写死**：头注/脚本里**预测**的红项集合两处都是错的（A 预测 3 项、实测 7 项；脚本把"正题应为空集"写成"应非空"）。负控的红项集合 = **实验读数**，不是推论 |

（陷阱 45/46/47/48 的"Repeater delegate 父链为空 ⇒ 必须递归视觉树"、"隐藏页 `ListView`
尺寸 0 ⇒ delegate 不创建"，本轮**又**各踩一次 —— 编号不新加，但说明这两条是高频坑。）

---

## 10 A5 里的位置与后续

- **A5 剩余**：~~T40 快捷键编辑~~ ✅ → **T41 帮助 / 版本 / 许可证**（A-1.0 范围表同一格的余下三项）
  → **T42 错误页 + "未支持项"清单**；
- **A-1.0 范围表**：`|快捷键编辑、帮助、版本与许可证页面|可延后|必须|中文输入焦点不能触发天空快捷键|`
  这一格**尚未清空**（还差帮助/版本/许可证）；
- **挂账**：W-T37 / W-T38 / W-T39 / **W-T40** Windows 跨平台复验 —— ✅ **已完成**
  （2026-10-01，见 `docs/WT37_41_WINDOWS_VERIFY.zh_CN.md`）：`SHORTCUTCHECK 15/15 PASS ×2`，
  两组负控红项 `{SC-05a,05b,07,08,09,11,13}` 8/15 与 `{SC-05b,09,11}` 12/15
  **与 mac 逐位一致**；过程中判据侧修掉一条假红（`SC-13` 合成键事件需窗口已激活，
  TRAPS 120）—— **修的是仪器，不是产品**；
- T39 附带项：`NightModeCheck` 噪声容差口径统一（仍挂账）。

---

## 11 产物清单

**新增（产品）**

- `src/app/ShortcutModel.hpp` / `ShortcutModel.cpp`
- `src/ui/qml/ShortcutsPage.qml`

**新增（探针 / 判据）**

- `src/app/ShortcutProbe.hpp` / `ShortcutProbe.cpp`（`STELQUICK_SHORTCUT_PROBE`）
- `src/app/ShortcutCheck.hpp` / `ShortcutCheck.cpp`（`STELQUICK_SHORTCUT_CHECK`）

**修改**

- `src/ui/main.cpp`（两个运行块 + context property + 正常路径 `refresh()`）
- `src/ui/CMakeLists.txt`
- `src/ui/qml/MainWindow.qml`（`pageIndex` + `StackLayout`）
- `src/ui/qml/Toolbar.qml`（`navShortcutsButton`；**原 7 个导航按钮 objectName 未动**）

**验证 / 证据**

- `tools/t40-verify.sh`
- `docs/evidence/2026-09-30-t40-shortcuts/mac/`（探针读数、正题 ×3、负控 A/B ×3、十六套件回归、`rc-summary.txt`、二进制指纹）
