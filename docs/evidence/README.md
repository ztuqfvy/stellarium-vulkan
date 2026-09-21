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
| `2026-09-21-run_autotest-matrix-windows-vulkan-d3d11-opengl.txt` | `d717056` 修复版 `run_autotest.cmd matrix` 的三后端原始 stdout，附负控实验与结论 |

文件格式约定：正文为未经修饰的原始捕获；仅首尾由脚本注入**出处头**（提交号、
主机、驱动版本、被测 exe 的 sha256、采集方式）与**结论段**，便于审计追溯。
