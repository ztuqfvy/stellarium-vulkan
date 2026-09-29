# 证据：W-T31 —— Windows 原生 Vulkan 侧复验

> 任务：T31 INTERACTCHECK 环境门降级（见 `../../README.md`）。
> 脚本：`tools/windows/wt31-{pull,build,suites}.ps1`｜口径：**原生 Vulkan**
> （与 macOS 的 Metal + MoltenVK **刻意不同**，这正是本轮要对照的东西）。
> 交付方式：`schtasks /it`（GUI 程序必须有桌面会话；且它**默认不是前台窗口**
> —— 恰好就是 T31 要优雅处理的那种环境）。

## 三次跑批：为什么留了三份

| 目录 | 仓库 rev | `stelQuickUI.exe` md5 | 它是什么 | 结论 |
|---|---|---|---|---|
| `t31w-suites-round1/` | `c4cdec1` | `C8F5C152D8DC19D6D9BEEAD59A66A370` | **发现平台差异的那一次** | 探针 `判据 12/16` 红 `IT-05/06/13/16`，与 macOS 期望（`13/16`、`IT-06/13/14`）不符 ⇒ 促成 §3.1 的并集修正 |
| `t31w-suites-round2-scrapebug/` | `8df3eec` | `C37136BB9EAADE81EBEBE972AADAA356` | **仪表自身翻车的那一次** | `SUMMARY.txt` 报 `unavailNamed=1(5)`、探针 `crosses=[=]`；而**原始日志与 round1 字节级同内容**、结论全对 ⇒ 坏的是脚本（见下） |
| `t31w-suites/` | `cf738bb` | `C37136BB9EAADE81EBEBE972AADAA356` | **最终归档**（修正后的脚本重跑） | ⬜ **第三轮已在 Windows 侧跑完，`SUMMARY.txt` 因隧道中断尚未拉回**；本目录暂为 round1 的副本，拉回后即替换 |

**round2 与最终轮用的是同一个二进制**（`md5` 相同、`mtime` 相同）⇒ 两者唯一的变量是
**跑批脚本的版本**。这让"仪表搞错"这件事变得无可争辩：**同一个被测对象，两套脚本给出
两种 SUMMARY**。round2 因此**原样保留**，它不是"跑错"，它是那条教训的证据。

> 最终轮的全部结论已用 round2 的**原始日志**（本仓库 `t31w-suites-round2-scrapebug/`）
> 逐项核实过 —— 见下"最终读数"。第三轮的增量价值只是"由**修正后的脚本**自己产出
> SUMMARY"，不是新的读数。

## 最终读数（`t31w-suites/SUMMARY.txt` 为准）

仓库 `cf738bb`；`stelQuickUI.exe` **28922368 B / md5=`C37136BB9EAADE81EBEBE972AADAA356`**。

| 组 | 期望 | 实测 |
|---|---|---|
| 正题 ×5（带 `REQUEST_ACTIVATE`） | 至少 1 次 `pos-pass` | ✅ **pos-pass=5 env-skip=0**；逐跑 `判据 16/16` + `act-note`=1 + 门**重试 0 次** + ✗=0 |
| 负控 ×3（`FORCE_FOCUSGATE_FAIL`） | `rc=6` + 5 条逐项点名 + `INTERACT-INTEGRITY` 绿 + IT-07..12 真跑 + 零 ✗ | ✅ **3/3 全项成立** |
| 探针（`FORCE_INACTIVE` + `PROBE_ACTIVATION`） | `判据 12/16`，红项**恰好** `IT-05/IT-06/IT-13/IT-16` | ✅ 与 Windows 实测边界一致 |
| 回归 9 项 + A2 | 全 `rc=0` | ✅ 全 `rc=0` |
| DYN 双路 ×3 | 各自 3/3 | ✅ 3/3 + 3/3，`producer-readback OK` |

**旧宿主（S3 判据）**：`stellarium.exe` **27634176 B /
md5=`66C51B61582BAC065C7A7FE5ACA44A42` / mtime=`2026-09-29T11:56:35`**
—— 与 **W-T30 基线逐项相同**（size / md5 / mtime 三项全等 ⇒ 文件根本没被重写）
⇒ **S3 不需重跑**。这是字节级证据，比"论证改动面不在旧宿主构建目标里"硬。

## 平台差异：本轮最有价值的发现

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

## 仪器自身翻车：`Read-Lines` 的数组双重包裹

round2 的 `SUMMARY.txt` 读起来像**产品退步**，而原始日志完全正常。根因是上一轮为修
"`Start-Process -Wait` 返回后重定向输出文件仍滞后几毫秒"的抓取竞态而新加的 `Read-Lines`：

```powershell
$lines = @(Get-Content $File -Encoding UTF8)
...
return ,$lines          # ← 逗号 = 包成 1 元素数组（防管道展开）
```

调用点是 `$txt = @(Read-Lines -File $so)`，而 **`@()` 不展平嵌套数组**
⇒ `$txt` 只有 **1 个元素**（那唯一元素就是行数组本体）⇒

- `@($txt | Where-Object { $_ -like "*PAT*" }).Count` **恒为 1**
  （`$_` 是数组，而 PowerShell 里 **数组 `-like` 只要任一行命中即为真**）；
- `foreach ($l in $txt)` 只迭代一次，字段抽取 `($l -split " ")[2]` 得到垃圾 `=`，**且不报错**。

**修法**：函数里 `return $lines`（去掉逗号），包裹交给调用点的 `@()`
（1 元素先展开再被 `@()` 重新包上，0 元素保持为空）。提交 `cf738bb`。

**判别套路（照抄）**：拿到负数读数，**先换一条完全独立的路径复核同一个事实**
—— 这里用 `/usr/bin/grep -a` 直接数原始日志、绕开脚本全部匹配逻辑。
两条路径结论冲突 ⇒ 仪表在撒谎，不是产品坏了。同源教训：血泪第 3 条
"**仪器没接在实况上**"的镜像 —— 仪表接错了会**假红**。

## 文件

| 文件 | 说明 |
|---|---|
| `SUMMARY.txt` | 跑批脚本产出的结论行（**最终判决以本文件为准**） |
| `interactcheck-run{1..5}.out.txt` | 正题逐跑完整日志（`act-note`、门"重试 0 次"、逐条判据） |
| `negctl-run{1..3}.out.txt` | 负控逐跑（5 条点名 + `INTERACT-INTEGRITY` + IT-07..12 是否真跑 + ✗ 数） |
| `probe-inactive.out.txt` | 失活探针原始日志（`判据 12/16`） |
| `{clock,action,search,locate,locate-ui,time,returnui,replay,timeui}check.out.txt`、`a2-vulkan.out.txt` | 回归 9 项 + A2 |
| `dyn-{engine,test}-run{1..3}.out.txt` | DYN 双路（含 `生产者=` 回读行） |
| `*.err.txt` | 各套件 stderr（留档，便于区分"日志没写"与"程序没跑"） |

> 注：Windows 侧产出的 `.txt` 是 **CRLF**，入库时 git 会刷一屏
> `warning: CRLF will be replaced by LF` —— 无害，跨平台证据留档保留原始字节。
