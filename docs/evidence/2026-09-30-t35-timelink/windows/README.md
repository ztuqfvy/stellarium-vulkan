# 证据：W-T35 Windows 跨平台复验（T34 工具栏 + T35 时间链路，并批）

> 产品文档：`docs/WT35_WINDOWS_RECHECK.zh_CN.md`
> 仪器事故另档：`../windows-instrument/` ｜ 送源对账：`../windows-src-sync/`
>
> 一句话：**定稿轮全绿**（`launcher exit=0`、零 FATAL）；T34/T35 的**全部红项集合与 mac 逐位一致**。
> 目录里的 `SUMMARY.broken-instrument-NOT-EVIDENCE.txt` 是**首轮（仪器坏）**的汇总，
> **不是产品证据**，留档只为对照。

## 0 机器与通路（实测）

| 项 | 值 |
|---|---|
| 主机 | `DESKTOP-0PJI1SO` |
| 检出 | `E:\Qt_demo\stellarium-vulkan`（⚠️ 不是 `E:\stellarium`） |
| 构建目录 | `build-win`（⚠️ 不是 `build-release`） |
| 产物 | `build-win\src\ui\Release\stelQuickUI.exe` |
| 图形栈 | **原生 Vulkan**，`NVIDIA 591.74`，`3.3.0` |
| PowerShell | 5.1.19041.6157 |
| 通路 | UU远程隧道 `127.0.0.1:2222` + `schtasks /it` |

## 1 定稿轮（`12:45:26 → 12:59:10`，13m44s）

### 1.1 仪器自证

```
script md5 = d53c59586f51ee22bfdb1b597441baa0
repo HEAD = cf738bb                （⚠️ 撒谎：fetch 不通，树靠 scp 更新）
exe size = 29282816 B   mtime = 2026-09-30T12:26:43.1855555+08:00
exe md5  = 7687513E293BAA23298231B4E7B46348
11 项 SRC <file> md5 全 MATCH（清单见 SUMMARY.txt 头部）
```

### 1.2 汇总

| 段 | 结果 |
|---|---|
| 相邻回归 11 套 | 全 rc=0（`clockcheck` 1.0s .. `timeuicheck` 26.2s） |
| `S3 旧宿主 stellarium` | rc=0，`verdictPASS=True` |
| `A2 逐像素`（`a2-vulkan`） | rc=0 |
| T34 正题 ×3 | `12/12 VERDICT=PASS`，`red=[]`，`tb09found=1`，`covered=1`；`bad=0 of 3` |
| T34 负控 A/B/C/D ×1 | rc=10，判据 `9/12` `10/12` `11/12` `11/12`，红项 `[TB-07,TB-09,TB-12]` / `[TB-09,TB-12]` / `[TB-10]` / `[TB-10]`+`coveredFalse` |
| T35 正题 ×3 | `7/7 VERDICT=PASS`，`red=[]`，`leak=0`，`restore=1`，`gate=0`，独立复算 TL-01/TL-07 `True`；`bad=0 of 3` |
| T35 负控 A/B/C ×1 | rc=10，判据 `6/7` `4/7` `6/7`，红项 `[TL-01]` / `[TL-01,TL-02,TL-05]` / `[TL-04]` |
| `TOOLBARPROBE` | rc=0 `VERDICT=DONE`，`Q1=1`，`Q2=12` |
| `TIMELINKPROBE` | rc=0 `VERDICT=DONE`，`placeholderLeak=0`，`missingAnchors=[]` |
| `INTERACTCHECK` ×3 | rc=0 `18/18`，`armed=1`，`degraded=False` |
| `INTERACT` 失活探针 | rc=10，红项 `[IT-05,IT-06,IT-13,IT-16,IT-17,IT-18]` = 期望（W-T31/W-T32 实测边界） |
| DYN `engine` ×3 / `test` ×3 | 全 rc=0，`producer-readback OK` |
| 就绪门 | `window-gate hits=0` |
| **总** | **零 FATAL 行；`launcher exit=0`** |

### 1.3 逐条读数（正题）

```
TOOLBAR run1/2/3: rc=0 judge=12/12|PASS red=[] tb09found=1(>=1) covered=1(>=1) -> pos-pass
TIMELINK run1: rc=0 judge=7/7|PASS red=[] leak=0(0) restore=1(>=1) gate=0
               recomputedTL01=True recomputedTL07=True -> pos-pass
  recheck TL-01: W=2.9973 dJD=0.6    rate=0.2 pred=0.59946 ratio=1.0009 rel_dev=0.09% OK
  recheck TL-07: dJD=0.20005 dLST=72.2152 expected=72.2152 residual=0 tol=1 OK
TIMELINK run2: ... W=3.0006 dJD=0.6    pred=0.60012 ratio=0.9998 rel_dev=0.02%
               dJD=0.2 dLST=72.1971 expected=72.1971 residual=0
TIMELINK run3: ... W=2.9992 dJD=0.5998 pred=0.59984 ratio=0.9999 rel_dev=0.01%
               dJD=0.2 dLST=72.1971 expected=72.1971 residual=0
```

## 2 首轮（仪器坏，`12:27:18 → 12:41:06`，`launcher exit=97`）

**不是产品证据**，留档用于对照"仪器缺陷长什么样"：

```
  TOOLBAR run1: rc=0 judge=12/12|PASS red=[] tb09found=1(>=1) covered=1(>=1) -> pos-pass
  TOOLBAR run2: rc=0 judge=|PASS      red=[] tb09found=1(>=1) covered=1(>=1) -> BAD
  TOOLBAR run3: rc=0 judge=/|PASS     red=[] tb09found=1(>=1) covered=1(>=1) -> BAD
  TIMELINK summary: pos-pass=0 env-skip=0 bad=3 of 3
FATAL TIMELINKCHECK never produced a full 7/7 run -- positive case not exercised
FATAL 13 expectation check(s) failed
```

注意：**只有 `judge=` 一个字段坏**，`rc` / `red=` / 探针 / 独立复算 / `INTERACTCHECK` 全对，
且**从第 2 次调用起**才坏。机理与修复见 `../windows-instrument/README.md`。

## 3 计划任务残留清理

| 任务 | 来源 | 处置 |
|---|---|---|
| `StelQC_t35w_build` | 本轮构建轮 | 已删 |
| `StelQC_t35w_suites` | 本轮跑批 | 已删（轮询脚本在检测到 `launcher exit=` 后自动删） |
| `StelQuickT17Build` | **2026-09-24 遗留僵尸** | 已删（XML 与删除日志见 `schtasks-residue/`） |
| `StelQuickT18LongRun` | **2026-09-24 遗留僵尸** | 已删（同上） |
| `t17winbuild` | **更早遗留僵尸**（小写名，第一遍筛漏了） | 已删；输出：`成功: 计划的任务 "t17winbuild" 被成功删除。` |

清理后全量扫描（`schtasks /query /fo csv` 按**任务名**筛，不靠状态字段——本地化会乱码）：

```
=== every scheduled task whose name mentions our project ===
--- end (nothing above = clean) ---
```

## 4 文件清单

```
SUMMARY.txt                                定稿轮汇总（**权威**）
SUMMARY.broken-instrument-NOT-EVIDENCE.txt 首轮（仪器坏）汇总，**非证据**
t35w-launch.log                            launcher 日志（⚠️ 子进程输出被 *>> 绞成 NUL，见产品文档 §6.4）
<suite>.out.txt                            33 条套件的原始 stdout（UTF-8，判据行在此）
<suite>.err.txt                            同期 stderr（Stellarium 常规启动日志，无异常）
schtasks-residue/*.xml / *.del.log         两个僵尸任务的定义与删除日志
```

⚠️ `t35w-launch.log` 只有 242 字节 —— `*>>` 把子进程输出绞成了 NUL。
**证据链没有缺口**：`SUMMARY.txt` 与逐套件 `.out.txt` 都是套件脚本直接 `Out-File` 落盘的，
不经过 launcher 的重定向。
