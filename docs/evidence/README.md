# docs/evidence —— 验收原始输出存档

本目录存放**机器产出的原始捕获**，用于给验收结论留证。
对应纪律见 [`docs/WINDOWS_BUILD.zh_CN.md`](../WINDOWS_BUILD.zh_CN.md) 第 9 节：
> 把 `run_autotest.cmd matrix` 的**完整 stdout 原始文本**存盘并随提交入库
> （例如 `docs/evidence/` 下带日期文件名），不要只写结论矩阵。

## 为什么必须存原始输出

本次复核（2026-09-21）发现验收工具**自身有缺陷**，"三后端全 PASS"这条结论
在修复前不可信：

1. `run_autotest.cmd` 的 `matrix` 分支 `setlocal` 未开 `EnableDelayedExpansion`，
   却在 `for` 块内读 `!ERRORLEVEL!` → 逐后端退出码被打印成字面量；
2. 块尾无条件 `set RC=0` → 无论成败都报 `EXIT CODE = 0`。

两者叠加等于**永久绿灯**。已在 `d717056` 修复。因此"结论有据"必须落到原始
stdout 上，而不能只写一份人工整理的结论矩阵。

## 如何重新生成

**前置：代码页必须在父进程设成 UTF-8。** 不要在捕获作用域内调 `chcp`——
`chcp` 会让 `cmd.exe` 打印本地化版权横幅（CP936 字节），把两种编码混进本应
纯 UTF-8 的流里，导致文件无法用单一编码解码。

```powershell
# PowerShell 中：
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
chcp 65001 | Out-Null
.\run_evidence_matrix.cmd docs\evidence\YYYY-MM-DD-run_autotest-matrix-<范围>.txt
```

`run_evidence_matrix.cmd` 负责：清空所有 `STELQUICK_*` 变量（第 9 节白名单纪律）、
用 `< nul` 绕开 `run_autotest.cmd` 结尾的 `pause`、把 stdout+stderr 原样落盘、
并把 `run_autotest.cmd` 的真实返回码追加到文件末尾。

负控实验（证明聚合真的会报警，而不是无条件绿灯）：

```powershell
.\tools\negctl\run_negctl.cmd > docs\evidence\negctl-output.txt 2>&1
# 期望看到：EXIT CODE = 5 与 [negctl] RESULT: PASS - aggregation caught the fault
```

## 写入 .cmd 文件的硬规则（踩过坑，务必遵守）

`.cmd` 文件被 `cmd.exe` 按 **OEM 代码页（简体中文系统为 936/GBK）**逐行解析，
**即使中文写在 `rem` 注释里也不安全**：

- 误解码后的字节可能**跨行拼接**，残余片段会被**当作命令执行**，
  往 stdout 里灌入 `'xxx' is not recognized as an internal or external command`；
- 旧版 `run_evidence_matrix.cmd` 就因此**在写出文件前直接中止**，
  并且运行时会污染证据。

**规则：仓库内的 `.cmd` 一律只用 ASCII**（注释写英文）。校验方式：

```bash
python -c "b=open('run_evidence_matrix.cmd','rb').read(); print(sum(1 for x in b if x>=0x80))"
# 必须输出 0
```

## 本目录内容

| 文件 | 说明 |
|---|---|
| `2026-09-21-run_autotest-matrix-windows-vulkan-d3d11-opengl.txt` | `run_autotest.cmd matrix` 三后端原始 stdout + 负控原始输出；整份由 `tools/evidence/collect.ps1` 生成（不再有一次性注入命令） |
| `2026-09-23-t10-build-merge/` | T10 构建合流的本地验收原始输出（configure 三态、双目标构建、链接事实、`stellarium` sha256 不变性、两种构建形态回归、A1 四项、A2/DYN/S3 自检、一次 INVALID 首跑）。索引见该目录 `README.md` |
| `2026-09-23-a3-host-probe/` | A3 前置探针（T11 起手前）：`QApplication` 承载 `QQuickWindow` + 引擎无头引导同进程共存。含**首发崩溃现场**（静态库 qrc 未注册 → `qFatal` → SIGABRT 134）、Metal/Vulkan 两大后端 PASS、默认形态回归、负控、`stellarium` 字节不变性复查。索引见该目录 `README.md` |
| `2026-09-23-t11-live-runtime/` | T11 引擎共进程帧驱动 `LiveSkyRuntime`：探针 C-05/06/07（借上下文装配 / 两帧读回 1ms / 内容非空且随 JD 变化）、LIVE_ENGINE 冒烟（Metal + Vulkan，抓帧真实星空）、A2/DYN/S3 回归、默认形态隔离、`frames/*.png`。索引见该目录 `README.md` |
| `2026-09-23-t12-dyn-engine/` | T12 DYN 判据在**真实引擎生产者**下复跑：主证据 `30-FINAL-metal-PASS.*`（7/7 PASS + 逐 250 ms 采样 CSV）、Vulkan 对照（D1-C06 SKIP）、test 基线、形态不匹配负控（rc=6）、A2/S3/独立工程回归；另留**新旧判据口径对照**（`10/11/12-OLDformula-*`、`20/21-nominal60-*`）与**双重释放崩溃现场**（`12-*-ABORT-doublefree`）。索引见该目录 `README.md` |
| `2026-09-22-stelt9-smoke-20s.txt` | T9 一致性冒烟（20 s）。 |
| `2026-09-22-stelt9-probe-lpm-INVALID-throttled.txt` | T9 探针首跑，因机器处于**低电量模式被降频**判为 **INVALID**（不是被测程序失败）。 |
| `2026-09-23-consumer-dyn-bridge/` | 消费侧动态帧桥：Metal 8 s PASS、A2 回归、低帧率负控、Vulkan 6 s。 |
| `2026-09-23-sky-longrun/` | 长跑（旧宿主形态）：`run.log` + 逐秒/逐帧 CSV + 50 s 冒烟 + 5 fps 负控。 |
| `2026-09-23-sky-longrun-INVALID-screensaver/` | 同上前身，因**屏幕保护打断**判定 **INVALID**（留证以示不采信）。 |
| `2026-09-23-sky-longrun-PARTIAL-session-killed/` | 会话被中断的 **PARTIAL** 记录。 |
| `2026-09-23-t13-live-longrun/` | T13 合流形态长跑与根因定位全过程（19 份编号证据）：多次**丢设备现场**（`04/05/07/08`）、纯 QML 与纯引擎对照（`09/15`）、静默期与 DPR 实验（`10/11/12`）、**Metal 后端 11/11 PASS**（`13/14`）、两次正式跑（`17` 含 SL-C03 单个离群点、`18` 11/11 PASS）与 **MVK resume 实验无效**（`19`）。索引见该目录 `README.md` |
| `2026-09-23-windows-stage1/` | Windows 支线阶段 1：A2 / DYN / D3D11 三后端 + `vulkaninfo` 摘要 + A2 抓帧 PNG。索引见该目录 `README.md` |
| `2026-09-24-t14-audit/` | T14 更新循环与时钟审计的 **grep 原始记录**（结论见 `docs/T14_UPDATE_CLOCK_AUDIT.zh_CN.md`）。 |
| `2026-09-24-t15-command-path/` | T15 命令通路：ACTIONCHECK 自检、A2/DYN/S3 回归、短冒烟（含一次**低电量模式污染**的留证）。索引见该目录 `README.md` |
| `2026-09-24-t16-sim-clock/` | T16 单一仿真时钟：CLOCKCHECK 纯逻辑自检 + 命令通路回归 + 90 s 冒烟（含一次**电池降频**留证）。索引见该目录 `README.md` |
| `2026-09-24-t17-models/` | T17 两个模型：SEARCHCHECK 26/26、ACTIONCHECK（含新 AC-12）、CLOCKCHECK/A2/DYN/S3 回归、三轮构建日志（`.log.gz`）。索引见该目录 `README.md` |
| `2026-09-24-t18-locate-track/` | T18 定位与跟踪：LOCATECHECK **14/14**、**UICHECK 8/8**（UI 层端到端：objectName 锚点 + 窗口真实鼠标事件 + 负控）、SEARCHCHECK 26/26 等零退化回归，**另含 DYN 的定性记录**（`dyn-ab-baseline-vs-t18.txt`：T17 基线 6/9 与 T18 6/9 失败率相同；§6 记录同日高负载复测下**替身与引擎同时全败**——替身路径不含 T18 代码，故判定与本次改动无因果）。DYN 因此改为**跑引擎与替身两半、各报 `N/3`**，并按 `env_note` 记录环境。索引见该目录 `README.md` |
| `2026-09-24-windows-stage23/` | Windows 支线阶段 2/3：合流构建与判定性实验（`r2_3a_dyn_engine.log`、`r2_3b_longrun.log`、逐秒/逐帧 CSV）。索引见该目录 `README.md` |
| `2026-09-24-windows-30min/` | Windows **30 分钟正式长跑 ×2**：`t13-win-30min.*`（W-T13，`0f2faae`）与 `t17-win-30min.*`（**W-T17，`6bce85d`，SL-C01..C11 全绿 rc=0**），附 §3 日志编码恢复配方。索引见该目录 `README.md` |

> 上表自 T13 起曾中断未补（原表只到 T12）；2026-09-24 按各目录的实际内容回填，
> 描述只陈述文件里能直接看到的事实。
| `../../tools/evidence/collect.ps1` | **本目录证据的唯一规范生成器**（见下） |

文件格式约定：正文为未经修饰的原始捕获；仅首尾由脚本注入**出处头**（提交号、
主机、驱动版本、被测 exe 的 sha256、采集方式）与**结论段**，便于审计追溯。

### 生成器已入库（2026-09-21 补，同日修正并本地实测）

首版证据的出处头与负控段是**临时命令**注入的，那些命令当时没入库——也就是说
换个人无法复现同一份文件，这本身是一处证据链缺口。现补 `tools/evidence/collect.ps1`：

```powershell
# 从仓库根目录执行；默认输出 docs\evidence\<日期>-run_autotest-matrix-windows-vulkan-d3d11-opengl.txt
powershell -ExecutionPolicy Bypass -File tools\evidence\collect.ps1
```

它把整份文件一次生成：元数据头 → 第一段真实 matrix 原始输出 → 第二段负控原始输出 → 结论段。
另外它会在跑之前**主动拒绝**验收禁用变量（`STELQUICK_A2_IGNORE_SGWAIT`、
`STELQUICK_RENDER_WORKAROUND=basic-loop`），把第 9 节的纪律做成硬闸门而不是口头约定。

#### 自检与 VERDICT 行

生成器必须能说"不"。文件头第一行是 `VERDICT`，由脚本从两段原始文本里**重新推导**，
不靠人读：

| VERDICT | 判定条件 | 脚本退出码 |
|---|---|---|
| `COMPLETE` | 三个后端标记齐全 + 有 `EXIT CODE` 行；负控返回 5 且打印 `RESULT: PASS` | 0 |
| `NOT USABLE AS EVIDENCE` | 上述任一条不成立 → 文件记录了一次没跑完的运行，**不得当证据引用** | 1 |

> 别混淆：脚本退出码只表示**这份记录可不可信**，不是被测程序的结论。
> 被测程序的结论看 `dut result` 行（即 `run_autotest.cmd matrix` 的返回码）。
> 换句话说，被测程序失败但记录完整时，退出码仍是 0——那份红色结果依然是有效结论。

#### 本地实测（2026-09-21）

该脚本首版在 macOS 上编写、**从未执行过**，这是当时的已知风险。已用 PowerShell 7.6.6
在 macOS 上**实际执行**其组装路径（Windows 专有的 `Get-CimInstance` / `nvidia-smi` /
`cmd /c` 用桩替换），四个用例全部符合预期：

| 用例 | 期望 | 实测 |
|---|---|---|
| 完整跑分 | `COMPLETE` / exit 0 | ✓ |
| matrix 缺一个后端段 | `NOT USABLE` / exit 1 | ✓ |
| 负控返回 0 | `NOT USABLE` / exit 1 | ✓ |
| `-SkipNegctl` | `NOT USABLE` / exit 1 | ✓ |

组装产物：UTF-8 无 BOM、可用单一编码解码、**0 个非 ASCII 字节**（ASCII-only 规则贯彻）。

过程中实测发现并修掉三处：

1. **`dirty` 标志自我污染。** 脚本把自己的产物（未提交的 `docs/evidence/*.txt`）算进工作区
   差异，于是从**第二次运行起恒为 `YES`**。一个恒亮的告警等于没有告警，而且会训练读者
   忽略那一行。现排除 `docs/evidence/`，并把排除范围**写在那一行上**——排除必须可见，
   不能静默。已在真实仓库验证：伪造一个未提交证据文件时判定仍为 `no`，而真实脏改动照报。
2. **exe 路径硬依赖 `build-ui\deploy`。** `deploy` 目标只在配置时找到 `windeployqt` 才存在，
   硬依赖会把"没装 windeployqt"变成一次白跑的往返。现按 `deploy` → `Release` → `Debug` →
   `build-ui` 顺序回退，并把**实际选中的路径**写进文件头。
3. **`@()` 的类型陷阱。** `$x = @([IO.File]::ReadAllLines(...))` 会把 `string[]` 重新类型化为
   `Object[]`，`List[string].AddRange()` 随即拒绝绑定并抛错。必须不加 `@()` 直接赋值。
   这条是本轮改动引入、又在本地执行时抓到的——**只做语法检查发现不了，跑一遍才行**。

> ✅ **已真机验证（2026-09-21，Windows 10 19045）**：原先无法离机验证的两处——`chcp`
> 与控制台的交互、两条 `cmd /c "... > file 2>&1"` 捕获行——实跑均正常。产物
> UTF-8 无 BOM、可用单一编码解码、**0 行污染**（无缩写版权横幅、无 `is not recognized`）。
> 脚本同样遵守 ASCII-only 规则（非 ASCII 字节数为 0）。
>
> 唯一残留：`run_autotest.cmd` 结尾 `pause` 的提示语 `Press any key to continue . . .`
> 被原样捕获进 SEGMENT 1。`< nul` 只让它不再阻塞，并不抑制这行提示。它是**真实原始输出**，
> 不影响任何判据，因此保留不做修饰。

#### 真机首跑抓到的缺陷：`dut result` 永远变不了红（同日修复）

留证链上有三个数字，别混：生成器退出码（记录可不可信）、`dut result` 行（被测程序结论，
取自 runner 的**进程返回码**）、SEGMENT 1 正文里的 `EXIT CODE = N`（被测程序自己打印的聚合码）。

真机实测发现第三个早修好了，**第二个恒为 `0`**：`run_autotest.cmd` 走到 `:end` 后是
`echo.` → `pause` → `endlocal` → 文件结束，**RC 没有作为进程返回码带出去**，而那个 `echo.`
已把 `ERRORLEVEL` 重置为 0。用桩 DUT（固定返回 2）实测对照：

| | 脚本打印 | 进程返回码 |
|---|---|---|
| 修复前（`838b1b7`） | `EXIT CODE = 2` | `0` ← 红被吞成绿 |
| 修复后（`:end` 改为 `endlocal & exit /b %RC%`） | `EXIT CODE = 2` | `2` ✓ |

所以修复前 `dut result` 行**不具备变红的能力**，是比 `d717056` 所修"matrix 恒报 0"
高一层的同类缺陷。它一直没暴露，是因为 `run_negctl.cmd` 复刻逻辑时**自带 `exit /b`**——
负控能红、被测程序不能红，这种不对称恰好把缺陷盖住了。

回归检查：`tools\negctl\check_rc_propagation.cmd` —— 跑**真的** `run_autotest.cmd`
（不是副本），桩 DUT 返回 2，断言"打印码 == 进程返回码"。
实测：修复前副本 `RESULT: FAIL - printed=2 process=0 / exit 1`；修复后 `PASS / exit 0`。

