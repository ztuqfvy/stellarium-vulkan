# 2026-10-01 W-T37..W-T41 Windows 跨平台复验 — 证据

判读与结论见 **`docs/WT37_41_WINDOWS_VERIFY.zh_CN.md`**。

## `win/` — 目标机 `DESKTOP-0PJI1SO`，二进制 md5 `1AFBDE023DE5EC85BA5A9BE479D81A73`

定稿轮 `2026-10-01 22:10:57 → 22:18:24`（7m27s）。

| 文件 | 说明 |
|---|---|
| `SUMMARY.txt` | **判读入口**：SRC 送源自证 + exe 指纹 + 逐套件读数 + 负控红项集合对照 |
| `nightcheck-pos-run{1,2}.out.txt` | T37 正题 ×2 轮 |
| `displaycheck-pos-run{1,2}.out.txt` | T38 正题 ×2 轮 |
| `hidpicheck-pos-run{1,2}.out.txt` | T39 正题 ×2 轮（含本轮新增的 `HP-00 判别对照` note） |
| `shortcutcheck-pos-run{1,2}.out.txt` | T40 正题 ×2 轮（含本轮新增的 `SC-13 布场` 诊断 note） |
| `helpcheck-pos-run{1,2}.out.txt` | T41 正题 ×2 轮 |
| `neg-*.out.txt` | 负控 9 组（`live=` 计数证明**真进了 live 模式**） |
| `qwin-*.out.txt` | Q-WIN-01..05 |
| `*.err.txt` | 各次运行 stderr（原样） |
| `t37w-build-manifest.txt` / `t37w-build.log` / `t37w-build.rc` | 增量构建：rc、耗时、exe 指纹、SRC 哈希（`t37w-build.rc` = `rc=0`） |
| `t37w-launch.log` / `t37w-launch.out.txt` / `t37w-launch.err.txt` | schtasks 往返：launcher 退出码（`8`）、逐套件 `SUITE …` 汇总行、stderr（0 B） |
| `residue-scan-clean.txt` | 收尾残留自查（`tools/windows/win-residue-scan.ps1 -Clean`）⇒ `RESIDUE-VERDICT = CLEAN` |
| `wt-cleanup-report.txt` | 收尾前的只读残留点检（原 `C:\temp` 报告，本目录归档副本） |

**编码**：全部 `.out/.err.txt` 经 `.NET Process` + `StandardOutputEncoding(UTF-8)` 落盘，**中文可读**。
早期两轮的证据走 `Start-Process` 路径（中文全为 `?`）**未保留** —— 判据 id 是 ASCII 所以当时的判定正确，
但作为归档没有价值（见报告 §2.2-A4）。

**删除前已核**：本轮 `C:\temp` 上 11 个产物（`t37w-*` / `wt37-*` / `wtcleanup.ps1` / `wt-cleanup-report.txt`）
在删除前与本目录**逐文件 md5 比对，46/46 逐位相同**，确认归档完整后才清；
早期轮次（`t29w…t35w` 等 86 项）**只报告不删** —— 其归档未经本脚本核验，不由它处置。

## `mac/` — 对照机（Apple M3）

| 文件 | 口径 | 说明 |
|---|---|---|
| `helpcheck.txt` | Metal | 判据改动（`__FILE__` 归一化 + 行尾归一化）后的 mac 复验 ⇒ `17/17 PASS` |
| `hidpicheck.txt` | Metal | 判据改动（新增判别对照步）后 ⇒ `12/12 PASS`，噪声底 `差异=0` |
| `shortcutcheck.txt` | Metal | 判据改动（`requestActivate` 布场）后 ⇒ `15/15 PASS` |
| `hidpicheck-vulkan.txt` | **Vulkan** | **换后端对照**：判别 `HP-00` 是否与后端相关 ⇒ 同样 `12/12 PASS`、差异 0 |
| `a6-verify-qwin-rc-summary.txt` | Metal | `tools/a6-verify.sh qwin` 那一轮的收口摘要（见下方 ⚠️） |

⚠️ Metal 是代码 `src/main.cpp:5094` 所称的「**非验收配置**」（验收配置 = Vulkan），
而 `tools/a6-verify.sh:46` 用的正是 Metal —— 详见报告 §4.3。

⚠️ **已知的归档副作用（本案已处置）**：`tools/a6-verify.sh` 把每轮的收口摘要固定写到
`docs/evidence/2026-10-01-a6-regress/mac/rc-summary.txt` ⇒ **单独跑 `qwin` 会覆盖 `all` 那一轮的摘要**。
本案把这一轮的摘要另存为本目录的 `a6-verify-qwin-rc-summary.txt`，并把 `rc-summary.txt`
还原回 T47 全量回归版本（`git show HEAD:…`）。**下一个人注意**：跑 `qwin` 目标前先把
`rc-summary.txt` 挪走，或给摘要加 target 后缀（未改脚本，属待办）。

