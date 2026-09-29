# T32 — 搜索框两段式 Esc（有文本先清空、空则返回天空）

> 任务：把 A4 里最后一条**新增交互特性**收口 —— 搜索框的浏览器式两段 Esc。
> 出处：计划文档 §9.2 路线表与四处移交表的同一条：
> `**可选特性：搜索框两段式 Esc**（有文本先清空、空则返回天空） | ⬜ 新增交互特性（非缺陷），需单独立项 + 独立判据（T29 刻意不做，见 §9.4.13）`。
> 改动面：`src/ui/qml/MainWindow.qml`（产品，**唯一**）｜`src/ui/qml/SearchPage.qml`（产品，一行
> `property alias queryFieldItem`）｜`src/ui/main.cpp`（**纯仪器**）｜`tools/t32-verify.sh`｜
> `tools/windows/wt32-{pull,build,suites}.ps1`。**C++ 产品代码一行未改、无新 `Q_INVOKABLE`**。
> 判据数：`INTERACTCHECK` **16 → 18**（追加 IT-17 / IT-18）。

---

## 1 为什么 T29 当时刻意不做

T29 修的是"焦点在输入控件里时，Esc 不许把用户从搜索页甩走"（**缺陷**）。当时在
`MainWindow.qml` 里留了这句注释：

> 为什么不顺手"清空输入框"：那是**新增交互特性**（浏览器式两段 Esc），不是修缺陷；
> 要加得单独立项 + 单独判据，别混进守卫修复里。

两条理由，一条是纪律、一条是技术：

1. **纪律**：守卫修复的判据是"页不动"；两段式的判据是"文本被清空"∧"页不动"。混在一起
   写，任何一条判据红了都分不清是**守卫坏了**还是**新特性坏了**。
2. **技术**：T29 的守卫面是"**所有**可编辑控件"。两段式**不能**用这个面（见 §2）。

## 2 🔴 关键设计决定：适用面**必须窄于**守卫面

第一版实现写的是"凡焦点项 `.text` 是字符串且带 `inputMethodComposing` 就清空"
（`focusedTextItem()`）。**这是一个会弄坏产品的设计**，读 Qt 源码后当场推翻：

```qml
// QtQuick/Controls/Basic/SpinBox.qml:31
contentItem.text: control.displayText        // ← 这是一个**绑定**
```

QML 里**给带绑定的属性赋值会销毁该绑定**。时间页有 6 个 SpinBox 字段，一律中招 ⇒
用户第一次按 Esc 就把字段的显示绑定打断（不是"清空后还能重打"，是**永久坏掉**）。

**处置**：适用面收窄为**只对搜索框**，用**对象同一性**判：

```qml
// MainWindow.qml
function focusedSearchField() {
    return (root.activeFocusItem === searchPage.queryFieldItem)
           ? searchPage.queryFieldItem : null
}
```

```qml
// SearchPage.qml（新增一行，把搜索框本体暴露出去）
property alias queryFieldItem: queryField
```

三条配套理由：

- **为什么用对象同一性、不按控件名/类型字符串判**：T17 的血泪 —— 当时用
  `className == "QQuickTextInput"` 复刻守卫口径，而真实 `TextField` 的最派生类名是
  `QQuickTextField`（子类）⇒ 守卫在真机上成了死代码、**假绿 15 个任务**。类型字符串会
  随 Qt 实现漂移，对象指针不会。
- **为什么宁可窄**：窄 = 保守 = 不碰没测过的东西。时间页因此**一行未动**，它的自检
  （`TIMEUICHECK`）也不受影响；其余可编辑控件维持 T29 语义（收下键、什么都不做）。
- **兜底不是"希望"**：窄面本身就是 IT-17 / IT-18 的前提腿（§4）。

### 2.1 IME 组合态必须排除

组合中按 Esc 是"**取消候选**"，此时 `text` 往往是**空的**（组合串在 `preeditText` 里）
⇒ 若照走"已空 ⇒ 返回天空"，用户按一次 Esc 取消候选就被甩回天空页。所以：

```qml
if (edit && edit.inputMethodComposing !== true) { ...两段式... }
// 组合态，或"焦在别的可编辑控件里"：维持 T29 —— 收下键、什么都不做
```

IT-14 会在漏掉这条时必红（§5 的 N3 负控实测）。

### 2.2 最终形态

```qml
if (event.key === Qt.Key_Escape) {
    if (!ActionRouter.canDispatchToSky()) {
        var edit = root.focusedSearchField()
        if (edit && edit.inputMethodComposing !== true) {
            if (edit.text.length > 0) {
                edit.text = ""                      // 第一段：清空
                event.accepted = true
                return
            }
            if (stack.currentIndex !== root.pageIndex["sky"])
                root.returnToSky()                  // 第二段：返回天空
            event.accepted = true
            return
        }
        event.accepted = true                       // 组合态 / 别的控件：T29 原语义
        return
    }
    if (stack.currentIndex !== root.pageIndex["sky"]) {   // 守卫放行（焦点不在输入控件）
        root.returnToSky()
        event.accepted = true
        return
    }
}
```

第一段**永远是"温和、可逆"的那个**：清空可以重打，跳页不可逆。

## 3 判据：IT-17 / IT-18

| ID | 断言 | 腿 |
|---|---|---|
| **IT-17** | 焦点在搜索框 ∧ 文本非空 ∧ Esc ⇒ 文本被清空 | ① 前提腿：注入前文本**必须非空** ② 清空腿：注入后 `text == ""` ③ 无副作用腿：页号**没动**（仍=搜索页）∧ `dispatched` 计数**等于基线**（没有借道派发动作） |
| **IT-18** | 焦点在搜索框 ∧ 文本**已空** ∧ Esc ⇒ 返回天空页 | ① 前提腿：注入前文本**必须为空**、页=搜索页 ② **判别腿**：注入时刻 `canDispatchToSky() == false` ③ 结果腿：`currentIndex` == 天空页 |

### 3.1 🔴 IT-18 没有判别腿就会**偶然通过**

失活/焦点丢失时，守卫 `canDispatchToSky()` 会返回**真**，Esc 于是走 T20 的返回分支
—— **同样回到天空页**。也就是说：**"去掉第二段"这个缺陷会被判据放过去**，只是"走运"。
判别腿把"走的是守卫路径"钉死。§5 的 N2 负控实测：去掉第二段 ⇒ IT-18 红。

### 3.2 分类表扩为 8 项，账目不变

```cpp
const char *const kFocusGatedIds[] = { "IT-05", "IT-06", "IT-13", "IT-14", "IT-15", "IT-16",
                                       "IT-17", "IT-18" };
```

- **只能追加在表尾**：`INTERACT-INTEGRITY` 自检断言"实际跳过集合 == 分类表从首个跳过项起
  的**后缀**"。追加在中间会把后缀关系破坏掉。
- **账目**：18 条 − 7 条被判据自己的相位点名跳过 = **11 条执行 + 1 条收尾自检 = `判据 12/12`**
  （与 T31 的 12/12 **执行数不变**，只是 UNAVAILABLE 从 5 条变 7 条 —— 这是账，不是巧合）。
  负控逐项点名的 7 条 = **IT-06,13,14,15,16,17,18**（IT-05 不在内：负控是在相位 7 强制置标志，
  那时 IT-05 已经跑过了）。

### 3.3 ⚠️ 表内 IT-15 从未被测出红（如实登记）

两平台探针（§4）实测的**激活依赖边界**：

- macOS ✗ = `IT-06, IT-13, IT-14, IT-17, IT-18`
- Windows ✗ = `IT-05, IT-06, IT-13, IT-16, IT-17, IT-18`
- 并集 = `{IT-05, IT-06, IT-13, IT-14, IT-16, IT-17, IT-18}`（7 条）—— **表是 8 条**。

即 **IT-15 在表内、但两平台探针都绿**（T31 时就是如此：当时表 6 条、并集 5 条）。
**保留它的理由不是"猜它依赖激活"**，而是它是**执行链上的下游相位**：IT-13（注入
preedit）→ IT-14（组合态 Esc）→ IT-15（提交）是同一条 IME 链，前两环既然跳了，第三环的
前置状态就不存在了。**代价**：环境门失败时会多跳一条本可以跑的判据（**保守方向**：少报，
不会假绿）。这条如实登记，不修饰成"并集"。

## 4 探针：边界是**平台相关**的（照 T31 口径，本轮重测）

`STELQUICK_INTERACT_FORCE_INACTIVE=1` + `STELQUICK_INTERACT_PROBE_ACTIVATION=1`
（门失败但**照跑**，用来量"哪几条真的依赖窗口激活"）。`rc=10` 是**期望**，不是回归项。

| 平台 | 读数 | 红项 |
|---|---|---|
| macOS | `判据 13/18` | `IT-06, IT-13, IT-14, IT-17, IT-18` |
| Windows | `判据 12/18` | `IT-05, IT-06, IT-13, IT-16, IT-17, IT-18` |

- **IT-17 真红**：失活 ⇒ 点击搜索框拿不到焦点 ⇒ **布场失败** ⇒ 前提腿（"注入前文本非空"）
  不成立。日志可见 `PROBE-note 失活探针：点击搜索框 4 次后焦点仍不是 TextInput`。
- **IT-18 真红**：**靠判别腿**（§3.1）。
- IT-05 / IT-16 只在 Windows 上红（与 W-T31 同因：那边窗口失活时键进不了 `keySink`）。

## 5 读数

### 5.1 macOS（Metal + MoltenVK）

一键复跑：`tools/t32-verify.sh all 5`（环境口径与 `t17..t31` 一致：MoltenVK + Metal RHI，**刻意不换**）。
二进制：`build-release/src/ui/stelQuickUI.app/Contents/MacOS/stelQuickUI`
**39339496 B**（`2026-09-29 19:53`）。⚠️ 本平台链接产物**非位级可复现**，见 §6.1。

| 组 | 期望 | 实测 |
|---|---|---|
| 正题 ×5（`..._REQUEST_ACTIVATE=1`） | `判据 18/18` + `PASS` + `rc=0` + 有 `act-note` | ✅ **pos-pass=5 env-skip=0 bad=0**；残留进程数=0 |
| 负控 ×5（`FORCE_FOCUSGATE_FAIL=1`） | `判据 12/12（另 7 条 UNAVAILABLE：IT-06,13,14,15,16,17,18）` + `UNAVAILABLE` + `rc=6` + 零 ✗ | ✅ **5/5 降级成立**：7 条逐项点名、`INTERACT-INTEGRITY` 绿、**IT-07..IT-12 真跑 6/6** |
| 探针 | `判据 13/18`，红恰好 IT-06/13/14/17/18 | ✅ 与期望逐项相等 |
| 相邻回归 10 项 | 全 `rc=0` | ⚠️ **9 项 `rc=0`；`locatecheck` `rc=10`**（`LOC-04 (b)` 0.5826° > 0.50°）→ **见 §6，已用两组对照定性为环境项，与 T32 无因果；不洗成 PASS** |
| A2 逐像素（Metal） | `rc=0` | ✅ `rc=0` |
| DYN 双路 ×5 | 各自 5/5 | ✅ **engine 5/5 + test 5/5**，`producer-readback OK` |

整体：`VERIFY_RC=1`（`FAILED=1`，即上面那一项）。证据
`docs/evidence/2026-09-29-t32-two-stage-esc/mac/`。

上表六组之外，还有**三轮代码级负控**（§5.3）。

### 5.2 Windows（原生 Vulkan）

脚本 `tools/windows/wt32-{pull,build,suites}.ps1`；`stelQuickUI.exe`
**28939264 B / md5=`CDF612C345B9A586E96C476518180790`**（`2026-09-29T19:31:32`）。
旧宿主 `stellarium.exe` **27634176 B / md5=`66C51B61582BAC065C7A7FE5ACA44A42`**
—— 与 W-T31 / W-T30 基线**逐项相同** ⇒ **S3 不需重跑**（字节级证据，不是推断）。

> 那台机器连不上 GitHub（`git fetch` 报 `Recv failure: Connection was reset`），
> `repo HEAD`（`cf738bb`）**会撒谎**。所以本轮 `wt32-build.ps1` / `wt32-suites.ps1` 的
> manifest 新增 **`SRC <file> size/md5`** 三行（`MainWindow.qml` / `SearchPage.qml` / `main.cpp`），
> 靠**源文件 md5** 而不是 repo HEAD 自证身份：

```
SRC MainWindow.qml md5=e9858e4e4ddf6bb51aa76fa9a32f340c
SRC SearchPage.qml md5=21b92f1717383d31e1ab336b8078f914
SRC main.cpp       md5=92d2fa0e07ec7b1d89eb2d828d2d69e8
```

三行与 mac 侧本地 `md5 -q` **逐项相同**。

| 组 | 实测 |
|---|---|
| 回归 10 项 | ✅ **全 `rc=0`**，**含 `locatecheck rc=0`**（`LOC-04 (b)` 0.4329°） |
| 正题 ×5 | ✅ **pos-pass=5 env-skip=0**（每跑 `18/18`、`armed=1`、`crosses=0`） |
| 负控 ×3 | ✅ **3/3 OK**：`rc=6`、`unavailNamed=7(7)`、`integrityOK=1(1)`、`ranIT07to12=6(6)`、`crosses=0(0)` |
| 探针 | ✅ `rc=10`，`crosses=[IT-05,IT-06,IT-13,IT-16,IT-17,IT-18]` **与期望逐项相等** |
| DYN 双路 ×3 | ✅ 各 `rc=0`，`producer-readback` 全 OK |
| 整批 | ✅ **无 `FATAL`、无 `BAD`** |

### 5.3 三轮**代码级**负控（macOS，判据的判别力本身）

环境级负控（`FORCE_FOCUSGATE_FAIL`）控的是**环境门**，不是**T32 特性**。要证明判据能抓住
缺陷，必须**把特性删掉**再跑：

| 轮 | 改法 | 期望 | 实测 |
|---|---|---|---|
| N1 | 去掉**第一段**（有文本也直接返回天空） | 只 IT-17 红 | ✅ `判据 17/18`，红**只有** IT-17（`text "mars" → "mars"`、`页 2 → 1`） |
| N2 | 去掉**第二段**（空文本不返回） | 只 IT-18 红 | ✅ `判据 17/18`，红**只有** IT-18（`currentIndex=2`，判别腿已钉住） |
| N3 | 去掉**组合态例外** | 只 IT-14 红 | ✅ `判据 17/18`，红**只有** IT-14（`页 2 → 1`） |

每轮都重建 + 复跑 + 留读数 + 还原；还原后正题复核 **`18/18` / `rc=0`**。
证据 `.../mac/code-negctl/`。

## 6 ⚠️ 本轮带出的两条环境发现（都不是 T32 引入的）

### 6.1 `LOC-04 (b)` 是"帧延迟量化"判据，读数**双峰**

macOS 全量跑批里 `locatecheck` 报红：`LOC-04 (b)` **0.5826°** > 阈值 0.50°。
历史同机同口径读数是 0.0143（T30）/ 0.0792（T31），Windows 0.043–0.126。
**本期两平台同时抬高**（Windows 本期 0.4329°）。

**两组对照**（证据 `.../mac/locatecheck-ab/`）：

| 组 | 二进制所含源码 | 5 次读数 |
|---|---|---|
| B | T32 全量 | 0.2896 / 0.4260 / 0.2494 / 0.2520 / 0.2525 |
| C | 仅回退两个 QML（main.cpp 仍是 T32 的） | 0.2673 / 0.2704 / 0.1868 / 0.2759 / 0.1600 |
| **D（决定性）** | **全部回退到 T31 源**（main.cpp md5 `6f9bc5da…`） | 0.3102 / 0.3162 / **0.0505** / 0.3223 / 0.3265 |

⇒ D 与 B/C **同分布** ⇒ **与 T32 源码无因果**。同源 Windows（`SRC md5` 逐项相同）本期
`locatecheck rc=0`。

**机理**（从同段日志读出来的，不是猜的）：LOC-04 的采样点是"跳变后 **0ms**" ⇒
残余角 ≈ **一次事件循环延迟内的天球转角**。同一段日志的 `LOC-05b-note` 实测"归中等待的
2.5s 内天空自转 74.26°" ⇒ **≈30°/s** ⇒ 一帧 ≈ 0.3°。所以读数天然**双峰**：
**单拍 ≈0.05°、双拍 ≈0.32°**（D 组里那条 0.0505° 就是单拍）。阈值 0.50 只压住第二档，
**没有余量**给第三拍 —— 本期一台机器三拍就越界。

**处置**：本轮**不改**判据（那是 LOCATECHECK 的事，T32 单独立项纪律），如实记为
`VERIFY_RC=1` 的一项，并把线索移交（§8）。

### 6.2 macOS 链接产物**非位级可复现**（修正既有做法）

同一份源码、同一构建目录，连续两次 relink：

```
A  39339496 B  md5=0e3e3b206802e3210f8a5c2df43ec62a  LC_UUID=A1014EFB-184B-3FAD-BF4F-159A8528FCD0
B  39339496 B  md5=f1e68c57248104cbd0d8eb4c4c6a9e5c  LC_UUID=02AE647E-E843-30C0-A83E-858C60FB919E
```

**字节数相同、md5 必然不同**（`LC_UUID` 每次链接重掷）。

> 🔴 **修正（T33-D，2026-09-29）："必然不同"过头了 ⇒ 准确说法是"md5 在两个方向上都不稳定"。**
> T33 实测：两轮**注释级**改动（`LocationCheck.hpp`）⇒ 重编 `main.cpp` + relink，
> 产物**字节数与 md5 都一字不变**（`39582584 / 28feadbf1059b389c3fafb0fb67fa9da`）。
> 加上 Windows 侧"源码没变、md5 变了"（见 `BUILD_RECORD` T33 章 §7.2），
> 结论是 **md5 只能当"产物清单指纹"，不能当"源码是否变化"的判据**。

⇒ 判"两版二进制是否同源"**不能**用 md5（macOS 上 md5 不同**推不出**源码不同）。
T22 那条"字节数 + md5"的口径要收紧为：**字节数相同 = 疑似同源；要断言同源，看源文件 md5**。
（Windows 侧本轮仍是位级可复现：两次构建都是 `CDF612C3…`。）

## 7 与既有文档的关系

- **T29**（`docs/T29_IME_KEY_GUARD.zh_CN.md`）：守卫本体，本轮**一行未动**；两段式是加在
  它之上的一个分支。IT-13..IT-16 全部保持原样通过。
- **T20**（`docs/T20_RETURN_RING.zh_CN.md`）：`returnToSky()` 是本轮第二段的**唯一实现点**，
  与工具栏按钮、T29 守卫共用；返回语义（切页 + 状态全保留）未变。
- **T31**（`docs/T31_INTERACT_GATE.zh_CN.md`）：`kFocusGatedIds` 与 `INTERACT-INTEGRITY`
  自检沿用，仅在**表尾**追加两项（§3.2）。
- **T25**（`docs/T25_INTERACT_UI_CHECK.zh_CN.md`）：判据编号与"成对/判别性对照"范式沿用。

## 8 未覆盖与移交

| 项 | 说明 |
|---|---|
| **`LOC-04 (b)` 帧延迟量化**（§6.1） | 移交：把 0ms 采样改成"等一帧"，或改成对延迟不敏感的量（N 次取最小），或照 DYN 先例报 `N/M`。**本轮不改** |
| **IT-15 在表内但两平台都测不出红**（§3.3） | 如实登记。若将来有人要收紧分类表，得先证明 IME 链可以**分段**跳 |
| 时间链路脱钩线索（T27 陷阱 15） | A4 剩余，另一项 |
| 捏合后首击失灵线索 | A4 剩余，另一项 |
| DYN 停摆根因 | A4 剩余，另一项 |

---

_证据：`docs/evidence/2026-09-29-t32-two-stage-esc/`（`mac/` 含 `locatecheck-ab/` 与
`code-negctl/`，`windows/` 为整批产物）。_
