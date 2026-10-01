# `C:\temp` 遗留件抢救归档（2026-10-01）

## 这是什么

Windows 侧 `C:\temp` 是本次 Stellarium QML/Vulkan 重写全程的**临时工作目录**（scp 落脚点 +
计划任务输出根）。T17 → W-T41 各轮次的脚本与产物都堆在这里，收尾时整目录清理。

删之前做了一次**逐文件内容核对**（不看文件名，只看内容哈希）：

| 口径 | 数量 |
|---|---|
| `C:\temp` 递归文件总数 | 691 |
| 内容与 `docs/evidence/**` **逐位相同** | 638 |
| 本目录抢救（归档里确实没有的） | **53** |
| 合计 | 691 |

> 核对方法说明：第一次按**文件名 + md5** 匹配只命中 16 个 —— 是**方法错了**不是归档漏了
> （归档时大量文件改了名，且 `.csv`/`.frames.csv` 一律 gzip）。改成**纯内容哈希**（并额外算一遍
> CRLF 归一化后的哈希，再解压全部 `.gz` 参与比对）后，命中数从 16 → 638。
> **教训：判"归档全不全"不能用文件名当钥匙。**

## 53 件的内容分类

| 类别 | 代表文件 | 为什么没在归档里 |
|---|---|---|
| 早期 Windows 构建日志 / manifest | `t17-win-build.log`、`t20-win-build.log`、`t29w/t31w/t32w/t35w-build.log` + `*-build-manifest.txt` | 结论已进 `docs/T*.md`，原始日志当时没归档 |
| 早期 Windows 长跑文本报告 | `t18-win-30min.txt`（28 KB） | 同轮的 `.csv` / `.frames.csv` 已归档为 `.gz`，纯文本报告漏了 |
| **入库前的临时命名脚本** | `t29w-win-suites*.ps1`、`t31w-suites.ps1`、`win-suites.ps1`、`win-longrun.ps1`、`judge-diag.ps1` | 正式版已按 `wt<NN>-*.ps1` 命名进了 `tools/windows/`，这些是**改名前的旧副本** |
| 一次性垫片 | `pull.ps1` / `pull2.ps1`、`t31w-*.cmd`、`t32w-probe.cmd`、`probe.out` | 用完即弃 |
| 源码同步指纹 | `wt35-win-md5.txt`、`wt35-win-objhash.txt`、`wt35-filelist.txt` | W-T35 那次跨平台同步的哈希台账 |
| 非本项目残留 | `tang_run.txt`（21 B）、`cjpm_*.txt` / `tang_*.txt`（0 B，2026-04-07 仓颉工具链遗留） | 与 Stellarium 无关 |

## 复验口径

本目录文件对 Windows 原件做过**逐位 md5 复验：53/53 相同，0 不同，0 缺失**。
打包体积 91 164 B（`ctemp-salvage.tar.gz`，md5 `FB022A51CAB216DC53C64ED397321173`）。

## 清理动作

`C:\temp` 已**整目录清空**（含早期 86 项轮次产物 + 18 项非本项目文件）。
清理前确认：0 个 `StelQC_*` 计划任务、0 个 `stelQuickUI` 残留进程。

清理脚本 `tools/windows/win-residue-scan.ps1` 保留在仓库 —— 它是**收尾自查工具**
（`-Clean` 只删本轮桶，早期轮次只报不删），下个阶段收尾时继续用。
