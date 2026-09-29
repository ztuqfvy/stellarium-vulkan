# 证据：W-T31 —— Windows 原生 Vulkan 侧复验

> 任务：T31 INTERACTCHECK 环境门降级（见 `../../README.md`、`docs/T31_INTERACT_GATE.zh_CN.md`）。
> 脚本：`tools/windows/wt31-{pull,build,suites}.ps1`｜口径：**原生 Vulkan**
> （与 macOS 的 Metal + MoltenVK **刻意不同**，这正是本轮要对照的东西）。
> 交付方式：`schtasks /it`（GUI 程序必须有桌面会话；且它**默认不是前台窗口**
> —— 恰好就是 T31 要优雅处理的那种环境）。

## 一轮跑批留五份，因为这一轮出了**三个仪器缺陷**

| 目录 | 起跑时刻 | 仓库 rev | 脚本 md5 | `stelQuickUI.exe` md5 | 它是什么 | 负控读数 |
|---|---|---|---|---|---|---|
| `t31w-suites-round1/` | 17:50:28 | `c4cdec1` | （无此字段） | `C8F5C152D8DC19D6D9BEEAD59A66A370` | **发现平台差异的那一次** | `1OK 0BAD 0BAD` |
| `t31w-suites-round2-scrapebug/` | 18:14:26 | `8df3eec` | （无此字段） | `C37136BB9EAADE81EBEBE972AADAA356` | **逗号双重包裹暴露的那一次** | `1BAD 0BAD 0BAD` |
| `t31w-suites-round3-integritybug/` | 18:25:49 | `cf738bb` | （无此字段） | `C37136BB9EAADE81EBEBE972AADAA356` | **逗号已修、`$ok`/`$OK` 仍在的那一次** | `1OK 0BAD 0BAD` |
| `t31w-suites-round-diag/` | 18:50:13 | `cf738bb` | （无此字段） | `C37136BB9EAADE81EBEBE972AADAA356` | **加诊断定位真因的那一次**（SUMMARY 里首次出现 `NEGCTL-DIAG`） | `1OK 0BAD×4` |
| `t31w-suites/` | 19:01:09 | `cf738bb` | `333dd19788ae7b300501e6a5f15d45cf` | `C37136BB9EAADE81EBEBE972AADAA356` | **最终归档**（三个缺陷全修） | `1OK 1OK 1OK` ✅ |

### 两个对照把"是仪器而不是产品"钉死

**① 同一二进制、两版解析器、两种 SUMMARY。**
`round3` 与最终轮用的是**同一个 `stelQuickUI.exe`**（`md5` 相同、`mtime` 相同
`2026-09-29T18:14:16`、`size` 相同）⇒ 两者唯一的变量是**跑批脚本的版本**。
一个被测对象、两套脚本、两种结论 —— 与 T22 的"同一二进制两种结果"同款手法。

**② 五个 `.out.txt` 的**原始字节**在两轮之间完全一致**（同 size 5800、逐字节
`od -c` 比对只有时间戳/浮点尾数差异），而 `SUMMARY` 一行 `OK` 一行 `BAD`。

> ⚠️ **`repo HEAD = cf738bb` 是过期的**，别当成"跑的是 cf738bb 的脚本"。
> 19:0x 时 Windows 那台机器**连不上 GitHub**（`Invoke-WebRequest https://github.com`
> 直接失败、`git fetch` 报 `Recv failure: Connection was reset`），所以 `E:` 检出
> 没法快进到含修正的 `de711cc`。**脚本实际执行的是 `C:\temp` 的副本**，它才是有意义的
> 那一版 —— 因此最终轮起 SUMMARY 里多了一行 **`script md5`**（仪器自证）：
> `333dd19788ae7b300501e6a5f15d45cf` == 仓库 `de711cc` 的工作树内容。
> 早先四轮没有这行，只能靠 `repo HEAD` 推断，这正是加它的原因。

## 最终读数（`t31w-suites/SUMMARY.txt` 为准）

`stelQuickUI.exe` **28922368 B / md5=`C37136BB9EAADE81EBEBE972AADAA356`**（`18:14:16` 构建）。
整批**无 `FATAL` 行**（⇒ `exit 0`）。

| 组 | 期望 | 实测 |
|---|---|---|
| 正题 ×5（带 `REQUEST_ACTIVATE`） | 至少 1 次 `pos-pass` | ✅ **pos-pass=5 env-skip=0**；逐跑 `判据 16/16` + `act-note`=1 + 门**重试 0 次** + ✗=0 |
| 负控 ×3（`FORCE_FOCUSGATE_FAIL`） | `rc=6` + 5 条逐项点名 + `INTERACT-INTEGRITY` 绿 + IT-07..12 真跑 + 零 ✗ | ✅ **3/3 全项成立**（`integrityOK=1(1)` 三跑齐） |
| 探针（`FORCE_INACTIVE` + `PROBE_ACTIVATION`） | `判据 12/16`，红项**恰好** `IT-05/IT-06/IT-13/IT-16` | ✅ 与 Windows 实测边界一致 |
| 回归 9 项 + A2 | 全 `rc=0` | ✅ 全 `rc=0` |
| DYN 双路 ×3 | 各自 3/3 | ✅ 3/3 + 3/3，`producer-readback` 六跑全 `requested==observed` |

**旧宿主（S3 判据）**：`stellarium.exe` **27634176 B /
md5=`66C51B61582BAC065C7A7FE5ACA44A42` / mtime=`2026-09-29T11:56:35`**
—— 与 **W-T30 基线逐项相同**（size / md5 / mtime 三项全等 ⇒ 文件根本没被重写）
⇒ **S3 不需重跑**。这是字节级证据，比"论证改动面不在旧宿主构建目标里"硬。

## 平台差异：本轮最有价值的**产品侧**发现

同一份探针、同一个窗口失活条件，两个平台的边界**不一样**：

| 平台 | 探针读数 | 失活下红的（⇒ 激活依赖） |
|---|---|---|
| macOS（Metal） | `判据 13/16` | IT-06 焦点守卫、IT-13/14 IME |
| Windows（原生 Vulkan） | `判据 12/16` | **IT-05 键**、IT-06、IT-13、**IT-16**（靠键的 Esc 对照） |

**根因**：Windows 窗口未激活时 `forceActiveFocus()` 拿不到 active focus ⇒ 注入的键
**根本进不了 `keySink`**（实测读数 `dispatched=0`、`lastActionId=""`）；macOS 的
`activeFocusItem` 失活时仍被设置，键照样到。所以**凡"靠注入按键"的判据在 Windows 上
天然是激活依赖的**。

⇒ **出厂分类表 = 两平台并集** `{IT-05, IT-06, IT-13, IT-14, IT-15, IT-16}`。
取舍原则：**少判一条只是覆盖率损失；把"仪器测不到"报成 FAIL 才是语义错误**
（那正是 T31 要消灭的东西）。

⚠️ **并集反过来决定门的位置**：IT-05 在相位 6、原来的门在相位 7 ⇒ 只把门放在相位 7 的话
IT-05 会以 FAIL 漏出。所以前导（相位 0）激活门**超时必须自己置降级标志**，
且**置标志 ≠ 记账**（另拆一个只置标志、不记账的 helper，防重复记账把收尾自检逼成假红）。

## 仪器自身的三个缺陷（这才是本轮花时间最多的地方）

### ① `return ,$lines` ＋ 调用点 `@( ... )` 双重包裹 → `unavailNamed=1(5)`

`,$x` 是"包成 1 元素数组、防管道展开"的惯用法，只在**调用方直接赋值**时正确。
调用点写 `$txt = @(Read-Lines -File $f)`，而 **`@()` 不展平嵌套数组** ⇒ `$txt` 只剩
**1 个元素**（那唯一元素就是行数组本体）⇒

- `@($txt | Where-Object { $_ -like "*PAT*" }).Count` **恒为 1**
  （`$_` 是数组，而 PowerShell 里 **`array -like pattern` 任一行命中即为真**）；
- `foreach ($l in $txt)` 只迭代一次，字段抽取 `($l -split " ")[2]` 得到垃圾 `=`，**且不报错**。

**修法**：函数里 `return $lines`（去掉逗号），包裹交给调用点的 `@()`（提交 `cf738bb`）。
**实测收益**：`unavailNamed` 由 `1(5)` 变 `5(5)`。

### ② `$ok` 静默覆盖 `$OK` —— `integrityOK` 只有第 1 跑对的真因

```powershell
$OK = [char]0x2713              # 判据行的对勾前缀（脚本级，第 47 行）
...
$ok = ($rc -eq 6) -and ...      # 负控块"本跑是否通过"的布尔（循环内，第 273 行）
```

**PowerShell 变量名大小写不敏感 ⇒ 两者是同一个变量。** 第 1 次循环先算 `$integrity`
（此时 `$OK` 还是对勾 → 命中 1），紧接着 `$ok = $true` **把对勾覆盖成字符串 `True`**；
从第 2 跑起匹配模式变成 `"INTERACTCHECK: True INTERACT-INTEGRITY*"` → **恒 0**。
于是 **run1 对、run2..N 全错**，而文件字节完全一致 —— 完美吻合三轮实测。

**修法**（提交 `986dbb5`）：字符类前缀起带下划线的长名 `$MARK_OK` / `$MARK_BAD`，
判据布尔改名 `$pass`。**永远别让"布尔"和"符号/字符"只差大小写。**

### ③ `-NegOnly` 是哑开关 → "只跑负控"的批次实际跑了完整 15 套件

主回归块（10 套件 + 5 次正题）的守卫是 `if (-not $ProbeOnly -and -not $DynOnly)`，
**漏了 `$NegOnly`**（探针块与 DYN 块倒是都查了）。加开关时**逐块 grep 它的名字**。
修法随 `986dbb5`。

### 一个被自己推翻的假说（也要留档）

上一轮把 `integrityOK=0` 归因为"`Start-Process -Wait` 返回后重定向文件仍滞后 ⇒ 抓取竞态"，
并据此把 `Read-Lines` 的等待策略从"锚定最后一行 `*VERDICT=*`"改成"等文件静止"。**那个
归因是错的**：锚定最后一行时它**明明存在**、而它**前面**的行反而"缺" —— 严格顺序写入下
不可能，当时就该否定。真正的竞态从未出现；`Read-Lines` 的静止等待降级为**廉价保险**
（脚本注释已改写成诚实版：保留历史 + 明确标注"不是那几个失败的修复"）。

**定位手法（本轮的真正收获）**：不再对生产者编故事，而是**给解析器自己加诊断**，
打印它实际看到的量 —— 一趟结案：

```
NEGCTL-DIAG run1: attempts=2 bytes=5800 txtCount=36 plainIntegrity=1 verdictLines=1   -> integrityOK=1
NEGCTL-DIAG run2: attempts=2 bytes=5800 txtCount=36 plainIntegrity=1 verdictLines=1   -> integrityOK=0
NEGCTL-DIAG run3: attempts=2 bytes=5800 txtCount=36 plainIntegrity=1 verdictLines=1   -> integrityOK=0
NEGCTL-DIAG run4: attempts=2 bytes=5800 txtCount=36 plainIntegrity=1 verdictLines=1   -> integrityOK=0
NEGCTL-DIAG run5: attempts=2 bytes=5800 txtCount=36 plainIntegrity=1 verdictLines=1   -> integrityOK=0
```

**五项输入全同、结论不同** ⇒ 一句话排除文件、锁定表达式。这一行现在是套件的常设部件
（不是临时脚手架），最终轮里它照常输出。

> 附带教训：**复现脚本必须复现"状态变更序列"**。本地单测那条 `-like` 表达式对五个文件
> **全给 1**（因为没执行 `$ok = ...` 那句赋值），差点据此宣布"产品与脚本都没问题"。

## 文件

| 文件 | 说明 |
|---|---|
| `SUMMARY.txt` | 跑批脚本产出的结论行（**最终判决以本文件为准**）；最终轮含 `script md5` 自证 |
| `interactcheck-run{1..5}.out.txt` | 正题逐跑完整日志（`act-note`、门"重试 0 次"、逐条判据） |
| `negctl-run{1..3}.out.txt` | 负控逐跑（5 条点名 + `INTERACT-INTEGRITY` + IT-07..12 是否真跑 + ✗ 数） |
| `probe-inactive.out.txt` | 失活探针原始日志（`判据 12/16`，rc=10 是**期望**） |
| `{clock,action,search,locate,locate-ui,time,returnui,replay,timeui}check.out.txt`、`a2-vulkan.out.txt` | 回归 9 项 + A2 |
| `dyn-{engine,test}-run{1..3}.out.txt` | DYN 双路（含 `生产者=` 回读行） |
| `*.err.txt` | 各套件 stderr（留档，便于区分"日志没写"与"程序没跑"） |

> 注：Windows 侧产出的 `.txt` 是 **CRLF**，入库时 git 会刷一屏
> `warning: CRLF will be replaced by LF` —— 无害，跨平台证据留档保留原始字节。
