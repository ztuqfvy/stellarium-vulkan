# W-T37..W-T41 Windows 跨平台复验（夜视 / 显示参数 / 高 DPI / 快捷键 / 帮助，并批）

> 提交：判据修补见本轮提交 ｜ 证据：`docs/evidence/2026-10-01-w-t37-41/{win,mac}/`
> 复验日期 2026-10-01（定稿轮 `22:10:57 → 22:18:24`，7m27s；Windows `DESKTOP-0PJI1SO`，二进制 md5 `1AFBDE02…`）
>
> 一句话：**五个套件全部跑通**（正题 5×2 轮 + 9 组负控 + Q-WIN-01…05），
> 负控红项集合**与 mac 逐位一致**；过程中抓出 **7 条仪器/判据缺陷**（6 条会制造**假红**、
> 1 条会让整套负控**测空气**）并全部修复；
> **唯一真·跨平台差异是 `HP-00` 噪声底门**（Windows 0.178% vs mac 0），
> 且已用**换来源的判别对照**排除了后端解释、定性为"渲染非确定/有持续变化源"。

---

## 1 结论先行

| 项目 | 结果 |
|---|---|
| Windows 侧门禁 | `launcher exit=8`（`VERDICT: bad=4 findings=2`）—— 4 个 bad 与 2 个 finding **同一根因 `HP-00`**（2 个 HP 正题 + 2 个 HP 负控） |
| **T37 `NIGHTCHECK`** | 2/2 轮 `judge=6/6 VERDICT=PASS rc=0 reds=0` ⇒ **与 mac 一致** |
| **T38 `DISPLAYCHECK`** | 2/2 轮 `judge=14/14 VERDICT=PASS rc=0 reds=0` ⇒ **与 mac 一致** |
| **T39 `HIDPICHECK`** | 2/2 轮 `judge=10/11 VERDICT=INCONCLUSIVE rc=6`，红 `{HP-00}` ⇒ ❌ **唯一 FINDING** |
| **T40 `SHORTCUTCHECK`** | 2/2 轮 `judge=15/15 VERDICT=PASS rc=0 reds=0` ⇒ **与 mac 一致**（修前 `14/15 {SC-13}`） |
| **T41 `HELPCHECK`** | 2/2 轮 `judge=17/17 VERDICT=PASS rc=0 reds=0` ⇒ **与 mac 一致**（修前 `15/17 {HC-05,HC-13}`） |
| **负控 9 组** | 全部 `live=2~4`（**真进了 live 模式**）；8 组 `REDSET-OK` 与 mac **逐位一致**；剩 1 组（`hidpicheck-*`）除 `HP-00` 外亦一致 |
| Q-WIN-01/02 | `maxStallMs=19 maxResizeMs=11`（门 250） ✅ |
| Q-WIN-03 | 三阶段跑完（`steps=54`） ✅ |
| Q-WIN-04 | `maxFrameGapMs=63`（门 250） ✅ |
| Q-WIN-05 | `wall=6.3s`（5s 目标 + 预算 ≤2s；mac 5.37s） ✅ |
| Q-WIN-06 | **macOS 专属**（`applyMacOsVulkanWorkaround()` 整体在 `#ifdef Q_OS_MACOS` 内），刻意不进 Windows 脚本 |
| 仪器/判据缺陷 | **7 条**（脚本面 4 + 判据面 3）—— 全部修复并复验 |
| 真·跨平台差异 | **1 条**（`HP-00`），已定性、判据正确自保 |
| 验收后端口径 | ⚠️ **两端不一致**（mac 跑 Metal = 代码里的"非验收配置"；Windows 原生 Vulkan）⇒ 见 §4.3 |

---

## 2 主线：**先修仪器，再谈产品**（7 条，分两类）

本轮首跑的正题读数里出现了 **3 个红项**（`SC-13`、`HC-05`、`HC-13`），
按项目纪律「红项不同即 FINDING、不许重试到变绿」，逐条查了根因 ——
**三条全是仪器/判据缺陷，没有一条是产品缺陷**。另有 **4 条脚本缺陷**让整套负控等于没跑。

### 2.1 判据面（3 条，都会制造**假红**）

| # | 症状 | 根因 | 修法 |
|---|---|---|---|
| B1 | `HC-05` 报「`__FILE__` 不是预期的 `/src/app/` 形态」 | MSVC 的 `__FILE__` 是 `E:\…\src\app\HelpCheck.cpp`（**反斜杠**），而判据写死 `indexOf("/src/app/HelpCheck.cpp")` ⇒ 恒 `-1` | 推根前把 `\` 归一化成 `/`（正斜杠路径 Qt 在 Windows 上照样打得开） |
| B2 | `HC-13` 报「UI 文本 17992 字符 == 数据面 licenseText（不一致）」，**长度却完全相同** | `licenseText` 是 qrc 资源**原样字节**（`QString::fromUtf8(readAll())`，不做行尾规范化）；Windows 检出的 `COPYING` 是 **CRLF（18332 字节 / CR=340）**，而 UI 走的 `TextArea`（`QTextDocument`）会把 `\r\n` 归一化成 `\n` ⇒ 读回 **17992** 字符。`18332 − 340 = 17992` 精确吻合 | 比较**归一化行尾后**的内容，并把**两侧原始长度都打进 note**（只归一化平台行尾，不藏实质差异） |
| B3 | `SC-13` 报「引擎键=「Ctrl+M」✗｜UI 按钮文字=「按键…（Esc 取消）」✗」—— 点击生效、已进捕获态，但键纹丝不动 | 无头/后台会话里 `QQuickWindow` **可能未激活**，`QQuickWindowPrivate::deliverKeyEvent` 没有可路由的 `activeFocusItem` ⇒ 合成键盘事件**静默消失** | 判据先 `requestActivate()` + `processEvents()`，并把 `isActive` / `activeFocusItem` 打进 note（**万一仍失败，这行就是判据自己的判别依据**） |

> B3 修后 `SC-13` **两轮一致通过**，布场 note 实测 `window->isActive=1｜activeFocusItem=navShortcutsButton`
> （mac 侧对照为 `isActive=1｜activeFocusItem=skyKeySink`）。
> 注意：`ShortcutCheck.cpp` 是**判据/探针**代码，**不是产品 UI** —— 这是修仪器，不是改产品。

### 2.2 脚本面（4 条，其中 1 条让**整套负控测空气**）

| # | 症状 | 根因 | 修法 |
|---|---|---|---|
| **A1** | 首跑负控**卡死**：第一个负控输出写完（656 B）后进程不退，`Start-Process -Wait` **阻塞 13 分钟**，后面两个负控排队 | 负控缺主开关（见 A2）⇒ 落到"普通 GUI 启动"路径 ⇒ **永不退出**；而 `-Wait` **没有超时** | 改手动 `WaitForExit(n)` + **看门狗**，超时 `Kill()` 并报 `rc=124`（与 mac `qwin-check.sh run_gui` **同一语义**） |
| **A2** | 负控输出里**没有 `LIVESKY:` 行**，只有非 live 路径的 `A2: 静态测试图案投递失败：视口仍无效（0x0）` | 负控只设了**断开关**（`STELQUICK_*_OFF`），**没设主开关**（`STELQUICK_*_CHECK`）⇒ 不进判据模式 ⇒ **一条判据都没跑** | 期望表加 `Main=` 字段，调用改 `-Var $n.Main -Extra @{ $n.Var = "1" }`；并新增**仪器自证门**：负控日志里没有 `LIVESKY:` 直接判 BAD |
| **A3** | 看门狗改造后，**每一个 `rc` 都是空的** ⇒ 全部套件与负控被判 BAD（而它们的判据其实全是 PASS） | `Start-Process -PassThru` 的 `ExitCode` **只在同时给 `-Wait` 时才填充**（文档行为）—— 与 A1 的看门狗**直接冲突** | 弃用 `Start-Process`，改 `[System.Diagnostics.Process]::Start()` + 异步读 + `WaitForExit(n)`：`ExitCode` 可靠（下一轮全部 `rc=0` / `rc=10`，与 mac 逐位一致） |
| **A4** | 归档的 `.out.txt` 里**中文全变 `?`**（判据 id 是 ASCII 所以判定没受影响，但**证据不可读**） | `Start-Process -RedirectStandardOutput` 把子进程的原始 UTF-8 字节交给 .NET，**按 console 代码页（936/GBK）解码**；`[Console]::OutputEncoding` 对它**无效**（实测） | 同一个 .NET Process 方案里设 `StandardOutputEncoding = UTF8(无 BOM)`，落盘用 `[IO.File]::WriteAllText` |

> **A2 是这轮最贵的**：它的症状不是"红"，而是"**安静地什么都没测**"。
> 首跑的负控"全绿"完全是假的（判据数为 `-/-`）。教训与 WT35 的"修法是实例级不是类级"同族 ——
> **负控必须证明自己真的进了被测路径**，否则它只是一个绿色的空壳。

---

## 3 定稿轮读数（Windows `22:10:57 → 22:18:24`）

### 3.1 仪器自证（缺一条，下面的绿色都不算数）

```
SRC src\app\NightModeCheck.cpp  md5=efb7e45f…      ← 与 mac 逐位一致
SRC src\app\NightModeCheck.hpp  md5=b8e609eb…
SRC src\app\NightModeProbe.cpp  md5=751bec82…
SRC src\app\NightModeProbe.hpp  md5=20af6b6e…
SRC src\ui\shaders\nightmode.frag md5=21a279dc…
SRC src\app\DisplayCheck.cpp    md5=3c823cf9…      ← 与 mac 一致
SRC src\app\HiDpiCheck.cpp      md5=1ce7dc82…      ← 与 mac 一致（含本轮新增判别对照）
SRC src\app\ShortcutCheck.cpp   md5=17a7272a…      ← 与 mac 一致（含本轮 SC-13 布场修补）
SRC src\app\HelpCheck.cpp       md5=68035036…      ← 与 mac 一致（含本轮两处修补）
SRC src\app\AppFacade.cpp       md5=3f044d10…
SRC src\ui\main.cpp             md5=f6034661…
SRC src\ui\qml\MainWindow.qml   md5=129404dc…
exe md5 = 1AFBDE023DE5EC85BA5A9BE479D81A73   size = 30790144 B
```

⚠️ 本机的 `repo HEAD` **会撒谎**（那台连不上 GitHub，树靠 scp 更新）—— 以上 `SRC` 哈希
在**同步前**已在 mac 侧算好并逐条比对通过，这才算"送源同源"。
另：本轮 83 个文件的首次同步里，`NightModeCheck.*` / `NightModeProbe.*` / `nightmode.frag`
曾被误写成 `NightCheck.*` / `NightOverlay.qml`（**这两个文件从来不存在**，夜视是 `MainWindow.qml` 里的内联 ShaderEffect）
—— 清单里的 `MISSING` 一度被误读为"同步漏了"，实为**清单本身写错**。

### 3.2 正题 5 套件 × 2 轮

```
SUITE nightcheck-pos-run1     rc=0  elapsed=21.6s  →  6/6   PASS          reds=0  OK
SUITE nightcheck-pos-run2     rc=0  elapsed=21.7s  →  6/6   PASS          reds=0  OK
SUITE displaycheck-pos-run1   rc=0  elapsed=27s    →  14/14 PASS          reds=0  OK
SUITE displaycheck-pos-run2   rc=0  elapsed=27s    →  14/14 PASS          reds=0  OK
SUITE hidpicheck-pos-run1     rc=6  elapsed=32.7s  →  10/11 INCONCLUSIVE  reds=1  BAD   {HP-00}
SUITE hidpicheck-pos-run2     rc=6  elapsed=32.6s  →  10/11 INCONCLUSIVE  reds=1  BAD   {HP-00}
SUITE shortcutcheck-pos-run1  rc=0  elapsed=15.3s  →  15/15 PASS          reds=0  OK
SUITE shortcutcheck-pos-run2  rc=0  elapsed=15.4s  →  15/15 PASS          reds=0  OK
SUITE helpcheck-pos-run1      rc=0  elapsed=15.7s  →  17/17 PASS          reds=0  OK
SUITE helpcheck-pos-run2      rc=0  elapsed=15.7s  →  17/17 PASS          reds=0  OK
```

**两轮逐位一致**（陷阱 87：期望值必须实跑两轮一致才写死）。

### 3.3 负控 9 组（与 mac 基线逐条对照）

```
nightcheck-effoff     rc=10  5/6   FAIL          live=4   reds={NC-03②}                     REDSET-OK
displaycheck-gateoff  rc=10  12/14 FAIL          live=4   reds={DP-02,DP-04}                REDSET-OK
displaycheck-fwdoff   rc=10  13/14 FAIL          live=4   reds={DP-07}                      REDSET-OK
hidpicheck-gateoff    rc=6   7/11  INCONCLUSIVE  live=4   reds={HP-00,HP-02,HP-02,HP-03,
                                                              HP-03,HP-04,HP-04}   FINDING-EXTRA {HP-00}
hidpicheck-fwdoff     rc=6   8/11  INCONCLUSIVE  live=4   reds={HP-00,HP-07,HP-08}  FINDING-EXTRA {HP-00}
shortcutcheck-writeoff rc=10 8/15  FAIL          live=2   reds={SC-05a,SC-05b,SC-07,SC-08,
                                                              SC-09,SC-11,SC-13}                REDSET-OK
shortcutcheck-saveoff rc=10  12/15 FAIL          live=2   reds={SC-05b,SC-09,SC-11}          REDSET-OK
helpcheck-takeoveroff rc=10  14/17 FAIL          live=2   reds={HC-14,HC-15,HC-16}           REDSET-OK
helpcheck-licenseoff  rc=10  15/17 FAIL          live=2   reds={HC-04,HC-13}                 REDSET-OK
```

- **8/9 组红项集合与 mac 逐位一致**（`NC-03②` 是双段判据，`②` 由码点 `U+2461` 构造，
  因为脚本必须保持纯 ASCII —— 教训：字符串比较若写成 `"NC-03"` 会在**每一轮**报一对
  无意义的 `FINDING-MISSING` + `FINDING-EXTRA`）。
- `hidpicheck-*` 两组除 `HP-00` 外亦一致 —— **同一个根因的两次体现**，不是两个独立问题。
- ⚠️ `T39` 的两组负控**刻意复用 T38 的开关**（`HiDpiCheck.hpp` 就是复用的）—— 这是**设计**，不是笔误。

### 3.4 Q-WIN-01…05（窗口交互回归，测试文档 §6.4）

```
SUITE qwin-windowtest   rc=0  elapsed=4.5s
  WINDOWTEST: steps=54 maxStallMs=19 maxResizeMs=11 maxFrameGapMs=63 门=250ms VERDICT=PASS
  Q-WIN-01/02  maxStallMs=19  maxResizeMs=11   （门 250ms）
  Q-WIN-04     maxFrameGapMs=63               （门 250ms）
SUITE qwin-autotest     rc=0  wall=6.3s       （mac: 5.37s；预算 5..7s）
```

Q-WIN-03（隐藏/显示 ×3 = A1 的"关闭/重开代理"本体）由 `steps=54` 的三阶段完成行 + 无停滞体现；
**诚实性声明**（与 mac 同款）：源码未提供"恢复后画面正确"的客观量，本项只判**序列跑完 + 无停滞**。

---

## 4 跨平台对照：哪些该一致、哪些本来就该不同

### 4.1 逐位一致（产品口径）

| 面 | Windows | mac |
|---|---|---|
| T37 夜视 `NIGHTCHECK` | `6/6 PASS` | `6/6 PASS` |
| T38 显示参数 `DISPLAYCHECK` | `14/14 PASS` | `14/14 PASS` |
| T40 快捷键 `SHORTCUTCHECK` | `15/15 PASS` | `15/15 PASS` |
| T41 帮助/版本/许可证 `HELPCHECK` | `17/17 PASS` | `17/17 PASS` |
| 负控红项集合 | 8/9 组逐位一致 | — |
| Q-WIN-01/02/03/04/05 | 全 PASS（19/11/63ms、6.3s） | 全 PASS（46/43/60ms、5.45s） |

⇒ **T37/T38/T40/T41 的判据在 Windows 上成立**，且**负控的判别力也在 Windows 上成立**
（这是比"正题绿"更强的结论：仪器在目标机上同样能红）。

### 4.2 本来就该不同（平台事实，不是缺陷）

- `COPYING` 行尾：mac **LF**（17992 B）/ Windows **CRLF**（18332 B）⇒ 判据已按 §2.1-B2 归一化。
- 键名文字、字体度量、`QKeySequence::toString()` 等原生文本面 —— 与 T40/T41 的既有口径一致。
- **Q-WIN-06 不存在于 Windows**：它是 macOS 专属判据（`#ifdef Q_OS_MACOS`），已在脚本头注写明**防止后人"补回来"**。

### 4.3 ⚠️ **验收后端两端不一致**（本轮的**方法论**发现）

- `src/main.cpp:5094` 的代码语义：**只有非 Vulkan 后端才打印「诊断对照模式——请求后端 X（非验收配置）」**
  ⇒ **「验收配置」= Vulkan**。
- 但 `tools/a6-verify.sh:46` 无注释地 `export STELQUICK_GRAPHICS_API=metal`
  ⇒ **mac 侧 A0–A6 的验收结论全部跑在「非验收配置」（Metal）下**，且所有 mac 证据文件都带那行"非验收配置"。
- Windows 侧无覆盖 ⇒ **原生 Vulkan**（W-T29 证据里明确记着 `backend = native Vulkan (no STELQUICK_GRAPHICS_API override)`）。

**因此**：任何**像素级**判据的跨平台对照都带一个**后端混淆变量**。
本轮的做法是**不猜、补对照** —— 在 mac 上用 **Vulkan** 后端补跑 `HIDPICHECK`（见 §5），
结果 `12/12 PASS`、噪声底 `差异=0`，**干净地排除了"后端差异"这一解释**。

> 移交：`a6-verify.sh` 那行 `export STELQUICK_GRAPHICS_API=metal` 应当在文档里写明**为什么**
> （推测是为规避 Q-WIN 在 MoltenVK 下的卡死，见 `tools/qwin-check.sh` 头注），
> 否则下一个人会以为 mac 的验收跑的就是代码所称的"验收配置"。

---

## 5 唯一 FINDING：`HP-00` 噪声底门（Windows 平台事实）

### 5.1 现象（两轮逐位一致，不是抖动）

```
噪声底帧①：帧号=447 尺寸=1280x720 非黑=1.0000 哈希=d3c735b9805b3950
噪声底帧②：帧号=472 尺寸=1280x720 非黑=1.0000 哈希=4749eb612e672eb1
[FAIL] HP-00：差异像素=1638/921600（0.178%）最大通道差=32 平均通道差=0.0055 哈希不同
        包围盒=(5,0)-(1271,449) 高度8带=468/376/363/328/103/0/0/0  ⇒ 超出容差（Δ≤2）
[NA ] HP-09 噪声底门没过，帧级判据不做（⇒ 整套 INCONCLUSIVE）
VERDICT=INCONCLUSIVE   rc=6
```

- **判据行为完全正确**：噪声底不合格时**拒绝**给出像素级结论（`HP-09` 转 `NA`），
  **没有**把不确定洗成 PASS。这正是 `HP-00` 存在的意义。
- 差异集中在**上半屏**（`y<450`，越往下越少），平均通道差仅 `0.0055` —— 像"少量像素的持续扰动"。
- mac 对照：**逐位相同**（`差异像素=0（逐位相同，哈希相同）`）。

### 5.2 判别一：**换后端**（排除"后端差异"解释）

mac 侧用 **Vulkan** 后端（= 代码所称"验收配置"，也是 Windows 用的后端）补跑同套件：

```
runtimeApi=Vulkan  backendOk=1  device=Apple M3
噪声底帧①：帧号=469 哈希=74b6d2d097f397aa
噪声底帧②：帧号=494 哈希=74b6d2d097f397aa          ← 与① 逐位相同
[PASS] HP-00：差异像素=0（逐位相同，哈希相同）
VERDICT=PASS   rc=0
```

⇒ **mac 在 Vulkan 下噪声底同样为 0** ⇒ `HP-00` 的 Windows 红项**与后端无关**。

### 5.3 判别二：**换时间尺度**（区分"收敛不足"与"持续变化"）

本轮在 `HiDpiCheck.cpp` 新增 S3b **判别对照步**（只在 HP-00 红时才有信息量）：

```
HP-00 判别对照：帧② → +2500ms 帧③ 差异 差异像素=1704/921600（0.185%）最大通道差=31
        平均通道差=0.0066 哈希不同 包围盒=(5,0)-(1270,449) 高度8带=552/387/333/321/111/0/0/0
        ⇒ 仍在变 ⇒ 渲染非确定或有持续变化源
```

- 500ms 间隔（帧①→②，约 25 帧）差异 `0.178%`；
- **2500ms 间隔（帧②→③）差异 `0.185%` —— 同量级** ⇒ **不是收敛不足**。
- mac 同一步：`差异=0 ⇒ 已静定`。

### 5.4 结论与移交

- **定性**：Windows（NVIDIA GL 渲染路径）下，**冻结状态的引擎上游帧存在持续的非确定性**
  （或有一个每帧都在变的上半屏渲染源）。这是**平台事实**，不是 QML/Vulkan 重写引入的缺陷。
- **影响面**：仅 `HIDPICHECK` 的**像素级子判据**（`HP-09` 字号帧效应）。其余 10/12 条
  （façade 往返 / 范围闸 / 白名单闸 / 生成量验算 / 控件齐备 / 绑定腿 / 交互后绑定 / 诊断数据面 / 复原）
  **全部在 Windows 上通过**。
- **移交项**：若要把 `HIDPICHECK` 纳入 Windows 常规回归，需先处置噪声底 ——
  可选方向：(a) 给 `HP-00` 加平台门（Windows 用更宽的幅度容差，并**明写理由**）；
  (b) 让帧级子判据改用"成对差分下界"而非绝对值；**不允许**直接放宽门槛了事。

---

## 6 本轮新血泪（仪器面 7 条，详见 `TRAPS.md` 114–120）

| 编号 | 一句话 |
|---|---|
| **114** | 给 GUI 套件加看门狗时，**别用 `Start-Process`** —— 它的 `-PassThru.ExitCode` **只在同时给 `-Wait` 时才填充**，而 `-Wait` 正是要看门狗的理由。症状是"每个 rc 都空 ⇒ 全判 BAD"，而判据其实全绿 |
| **115** | **负控必须同时设"主开关 + 断开关"**。只设断开关 ⇒ 不进判据模式 ⇒ 落到普通 GUI 启动路径 ⇒ **永不退出**（看门狗救不了语义）+ **一条判据都没跑**。加"必须出现 `LIVESKY:`"的仪器自证门 |
| **116** | MSVC 的 `__FILE__` 是**反斜杠**；任何"从 `__FILE__` 推根"的判据在 Windows 上会**恒假红**（推根前先归一化分隔符） |
| **117** | qrc 文本资源的**行尾**在 Windows 检出下是 CRLF，而 QML `TextArea`（`QTextDocument`）会归一化 ⇒ **逐字比较假红**。差额 = `\r` 个数，是个很好用的指纹 |
| **118** | `Start-Process -RedirectStandardOutput` **经 console 代码页解码** ⇒ 归档证据全毁（判据 id 是 ASCII 所以判定没受影响，**很容易漏过去**）。`[Console]::OutputEncoding` 对它**无效**，必须用 .NET Process 的 `StandardOutputEncoding` |
| **119** | 跨平台对照前先问：**两端的"验收配置"是不是同一个东西？** mac 跑 Metal（代码自称"非验收配置"）、Windows 跑 Vulkan ⇒ 像素级对照带混淆变量。治法是**补对照**而不是"假设它不影响" |
| **120** | 合成键盘事件需要窗口**已激活**：无头/后台会话里 `QQuickWindow` 未激活 ⇒ `deliverKeyEvent` 无 `activeFocusItem` ⇒ 事件**静默消失**。症状极像"产品改键失效"（点击生效、UI 已进捕获态），**必须**打印 `isActive`/`activeFocusItem` 才能分清楚 |

---

## 7 复现

```sh
# ── mac 侧（口径与 T17–T41 一致：Metal；补 Vulkan 对照时 unset 覆盖）────────
cd /Users/ztuqfvy/qt_demo/stellarium_vulkan
export VK_DRIVER_FILES=/opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json
export QT_VULKAN_LIB=/opt/homebrew/opt/vulkan-loader/lib/libvulkan.1.dylib
export STELQUICK_GRAPHICS_API=metal          # ← 代码语义里这是"非验收配置"，见 §4.3
BIN=./build-release/src/ui/stelQuickUI.app/Contents/MacOS/stelQuickUI
STELQUICK_HIDPI_CHECK=1 "$BIN"               # 也: NIGHT_CHECK / DISPLAY_CHECK / SHORTCUT_CHECK / HELP_CHECK
unset STELQUICK_GRAPHICS_API                 # 补 Vulkan 对照（= Windows 用的后端）
STELQUICK_HIDPI_CHECK=1 "$BIN"

# ── Windows 侧（必须走 schtasks /it：SSH 会话无桌面会话）───────────────────
scp tools/windows/wt37-41-*.ps1 ztuqfvy@127.0.0.1:'C:/temp/'
ssh ztuqfvy@127.0.0.1 "powershell -NoProfile -ExecutionPolicy Bypass -File C:\\temp\\wt37-41-build.ps1"   # 增量构建
ssh ztuqfvy@127.0.0.1 "powershell -NoProfile -ExecutionPolicy Bypass -File C:\\temp\\wt37-41-run.ps1"     # 建/跑/收 schtasks
#   wt37-41-run.ps1 自带：删残留 → 建 /it 任务 → 跑 → 等 launcher 退出行 → 删任务 → 扫残留 → 打 SUMMARY
#   单个 120s 看门狗，超时 Kill 并报 rc=124

# ── 收尾：残留自查（只读 → 加 -Clean 才删）──────────────────────────────────
scp tools/windows/win-residue-scan.ps1 ztuqfvy@127.0.0.1:'C:/temp/'
ssh ztuqfvy@127.0.0.1 "powershell -NoProfile -ExecutionPolicy Bypass -File C:\\temp\\win-residue-scan.ps1"
ssh ztuqfvy@127.0.0.1 "powershell -NoProfile -ExecutionPolicy Bypass -File C:\\temp\\win-residue-scan.ps1 -Clean"
#   -Clean 只删 -Match 命中的「本轮」产物；早期轮次与别的项目的文件只报告不删
#   本轮实测：11 项删除、RESIDUE-VERDICT = CLEAN（0 计划任务 / 0 进程 / 0 残留）
```

**判读入口**：`docs/evidence/2026-10-01-w-t37-41/win/SUMMARY.txt`（含 SRC 自证 + 逐套件读数 + 红项集合对照）。

⚠️ **操作坑（已处置，留档防复发）**：`tools/a6-verify.sh` 把每轮的收口摘要固定写到
`docs/evidence/2026-10-01-a6-regress/mac/rc-summary.txt`，因此**单独跑 `qwin` 目标会覆盖 `all`
那一轮的摘要**。本案把本轮摘要另存进 `docs/evidence/2026-10-01-w-t37-41/mac/`，
并把 `rc-summary.txt` 还原回 T47 全量回归版本。根治（给摘要名加 target 后缀）未做，列为待办。

