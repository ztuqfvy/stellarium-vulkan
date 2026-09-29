# W-T30 —— Windows 侧复验（原生 Vulkan）

> 交付依据：`docs/T30_TIME_VISUAL.zh_CN.md`；主证据：`../mac/`
> 工具：`tools/windows/wt30-{pull,build,suites}.ps1`（三者**均已在提交 `ccd0cde` 里**）
> 口径：Windows 原生 Vulkan（**不是** Metal/MoltenVK）；隧道 `127.0.0.1:2222`（UU远程端口映射）

## 为什么必须跑这一趟

`T30` 的改动面是 `src/ui/main.cpp` —— **宿主层**。按 W 支线规矩（宿主层改动要跨平台复验），
必须在 Windows 上重跑一遍。而且这里有一条**只有 Windows 才测得出来**的理由：

> 字体度量不同。Windows 的 CJK 回退字体与 macOS 不是同一套 ⇒ 单行文本的实际宽度不同 ⇒
> 时间页面板里"列宽是否被挤"的结论**在 macOS 上正确不代表在 Windows 上也正确**。
> 这正是 `UI-17`（几何非退化）与 `UI-18`（不被祖先裁掉）要判的东西。

## 二进制口径

| 产物 | size | md5 | mtime |
|---|---|---|---|
| `build-win/src/ui/Release/stelQuickUI.exe` | `28917248 B` | `2BB6FBD49DFD3262B9C31DA9996087DF` | `2026-09-29T16:23:09` |
| `build-win/src/Release/stellarium.exe`（旧宿主） | `27634176 B` | `66C51B61582BAC065C7A7FE5ACA44A42` | `2026-09-29T11:56:35` |

**旧宿主逐位不变** —— 与 `docs/evidence/2026-09-29-w-t29/suites/` 里记下的 W-T29 基线
（`t29w-build-manifest.txt`：`27634176 B / 66C51B61582BAC065C7A7FE5ACA44A42 / 11:56:35`）
**完全相同**（size、md5、mtime 三项一致 ⇒ 文件根本没被重写）。

⇒ 这就是 `S3 旧宿主不需要重跑` 的**字节级证据**：T30 的改动没有触及旧宿主。
（对比：`stelQuickUI.exe` 由 W-T29 的 `28884480 B / 838A7B55…` 变为 `28917248 B / 2BB6FBD4…`
—— 变了，符合"改了 main.cpp"的预期。）

## 文件清单

| 文件 | 内容 |
|---|---|
| `t30w-pull.log` | 仓库快进：`before=ccd0cde` / `fetch_rc=0` / `merge_rc=0` / `after=ccd0cde`（幂等） |
| `t30w-build.log` | VS 自带 cmake 增量构建（ALL_BUILD / Release）全过程 |
| `t30w-build.rc` | `rc=0`；`t30w-build-manifest.txt` 记 `elapsed_min=0.3` + 两枚 exe 的 size/md5/mtime |
| `suites/SUMMARY.txt` | **跑批总账**：26 个套件逐项 rc + TIMEUI/NEGCTL 断言 + 6 次生产者回读 |
| `suites/*.out.txt` / `*.err.txt` | 每个套件的原始 stdout/stderr（`Start-Process -RedirectStandardOutput` 直写字节，**不过 PowerShell 转码**） |
| `suites/a2-vulkan.png` | A2 抓帧（原生 Vulkan 基线） |

### 过程诚实声明（一次顺序失误）

`wt30-*.ps1` **已经在提交 `ccd0cde` 里**。我先用 `scp` 上传了三份，结果它们成了
**untracked 文件**，`git merge --ff-only` 直接报
`error: The following untracked working tree files would be overwritten by merge` 并 **Aborting**（`merge_rc=1`）。

处置：删掉这三份 untracked → 内联跑 `git merge --ff-only origin/main` ⇒ 成功（`eec8187 → ccd0cde`，
脚本从提交里出来）。之后为拿到**干净的脚本日志**又跑了一次 `wt30-pull.ps1`（此时已 up to date，
`Already up to date.` / `merge_rc=0` / `after=ccd0cde`）⇒ 归档的就是这一次的日志。

**教训**：脚本一旦入库，就**别再用 scp 上传** —— 上传只会制造 untracked 阻挡自己的 merge。

## 读数

**26 个套件全部符合预期**（`[done] 2026-09-29T16:34:47`，无 `FATAL`）：

| 组 | 读数 |
|---|---|
| 8 项基础相邻回归 | `clockcheck` / `actioncheck` / `searchcheck` / `locatecheck` / `locate-uicheck` / `timecheck` / `returnuicheck` / `replaycheck` —— 全 `rc=0` |
| **正题 TIMEUICHECK ×5** | 每跑 `rc=0` + `criteria27of27=True` |
| `interactcheck` ×5（T29 正题） | 全 `rc=0` |
| **A2 逐像素（原生 Vulkan）** | `rc=0`、`VERDICT=PASS` |
| **负控 NEGCTL ×3** | 每跑 `rc=10`，`fails=[UI-17,UI-20,UI-21,UI-22,UI-23]`、`missing=[]`、`unexpected=[]` |
| **DYN 双路** | `engine` 3/3 + `test` 3/3，**6 次生产者回读**全 `OK`（`requested=engine observed=engine` ×3、`requested=test observed=test` ×3） |

### 与 macOS 的读数对照（这正是跑这一趟的价值）

| 项 | macOS（Metal） | Windows（原生 Vulkan） |
|---|---|---|
| `UI-17` 最小 `w×h` | `51.0×24.0` | **`17.0×19.0`** |
| `UI-18` 页容器 | `960×552` | **`960×568`** |
| `UI-19` 比对对数 / 重叠 | `351` / `0` | 同 |
| `UI-21` 文本内容 | 七行非空 | 同（除 JD 数值随时钟） |
| `UI-23` 七行对比度 | 最小 `5.13:1`，7 项 ≥4.5 | **逐位相同**（`5.75 / 21.00 / 21.00 / 9.39 / 21.00 / 5.13 / 5.13`） |
| 负控 `UI-23` 触发值 | `1.00:1(#ffffff/#ffffff)` | 同 |
| A2 后端 | Metal | Vulkan（另一条基线，`PASS`） |

- `UI-17` 最小尺寸 `51×24` → `17×19`、页容器高 `552` → `568`：**平台差异真实存在**（字体度量 + 面板布局），
  而两条判据在两边都给出"**非退化 / 零越界**"的结论 ⇒ `T30` 的抗挤压结论**跨平台成立**。
- `UI-23` 的对比度**逐位相同**是合理的：颜色来自 QML 声明（`#1565c0` 等），与平台字体无关。
- Windows 的 A2 是**另一条基线**（Vulkan），不能与 macOS 的 Metal 基线互比；它自己 `PASS` 即可。
  其中半透明块那条读数 `期望(150,150,150) 实际(128,128,128) 偏差22` 仍判 `PASS`
  —— 属既有 A2 口径的容差范围，**不是本轮引入**。

## 收尾

- 三个 `schtasks` 任务已删除（`T30BUILD` / `StelQC_t30w_suites`）。**另外清掉了 W-T29 遗留的
  `StelQC_t29w_build`** —— 那是 `/sc once /st 23:59`，不删的话**今晚 23:59 会自己跑一次构建**。
  查询确认已无 `T30|StelQC|t29w|t30w` 残留任务。
- 结果文件全部拉回本目录（`suites/`，58 个文件 / 516 KB）。
