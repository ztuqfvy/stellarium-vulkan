# 证据：T31 INTERACTCHECK 环境门降级（"标记但继续"）

> 任务：计划文档 §9.4.14 的"可改进项（本轮不改）" —— 环境门一失败就短路后面 9 条判据。
> 文档：`docs/T31_INTERACT_GATE.zh_CN.md`（§9.4.16 为计划文档侧）
> 一键复跑：`tools/t31-verify.sh all 5`

## 目录

| 目录 | 内容 |
|---|---|
| `mac/` | macOS + Metal + MoltenVK 全量读数（`tools/t31-verify.sh all 5`） |
| `windows/README.md` | Windows 原生 Vulkan 复验的**总说明**：五轮对照表 + 平台差异 + 三个仪器缺陷 |
| `windows/t31w-suites/` | Windows **最终归档**（三个仪器缺陷全修；SUMMARY 含 `script md5` 自证） |
| `windows/t31w-suites-round1/` | **发现平台差异**的那一轮（探针 `12/16`、红 `IT-05/06/13/16`，与 macOS 预期不符） |
| `windows/t31w-suites-round2-scrapebug/` | 仪器缺陷①暴露的那一轮（`,$lines` 双重包裹） |
| `windows/t31w-suites-round3-integritybug/` | 仪器缺陷②暴露的那一轮（`$ok` 覆盖 `$OK`；**与最终轮同一个二进制**） |
| `windows/t31w-suites-round-diag/` | **加诊断定位真因**的那一轮（SUMMARY 首现 `NEGCTL-DIAG`） |

> **round3 与最终轮的 `stelQuickUI.exe` 是同一个文件**（`size`/`md5`/`mtime` 三项全等）
> ⇒ 两者唯一的变量是**跑批脚本的版本**：同一被测对象、两套脚本、两种 SUMMARY。
> "是仪器而不是产品"由此无可争辩。

## 读数摘要（`mac/rc-summary.txt` 为准）

二进制 `build-release/src/ui/stelQuickUI.app/Contents/MacOS/stelQuickUI`
**39338088 B / md5=`bad9c90fad1b2975722d1fc16403d6dc`**（2026-09-29 18:03，含"两平台并集"修正）。
`tools/t31-verify.sh all 5` ⇒ **`VERIFY_RC=0`**（`FAILED=0`）。

| 组 | 期望 | 实测 |
|---|---|---|
| 正题 ×5（`..._REQUEST_ACTIVATE=1`） | `判据 16/16` + `PASS` + `rc=0` + `act-note` | ✅ **pos-pass=5 env-skip=0 bad=0**；逐跑门**重试 0 次**、✗=0；**残留进程数=0** |
| 负控 ×5（`..._FORCE_FOCUSGATE_FAIL=1`） | `判据 12/12（另 5 条 UNAVAILABLE：IT-06,IT-13,IT-14,IT-15,IT-16）` + `VERDICT=UNAVAILABLE` + `rc=6` + 零 ✗ | ✅ **5/5**：5 条逐项点名=5、`INTERACT-INTEGRITY` 绿=1、**IT-07..IT-12 真跑 6/6** |
| 探针（`..._FORCE_INACTIVE=1 ..._PROBE_ACTIVATION=1`） | `判据 13/16`，红项**恰好** IT-06 / IT-13 / IT-14 | ✅ 红项 `[IT-06,IT-13,IT-14]`（rc=10 是**期望**，它不是回归项） |
| 相邻回归 9 项 | 全 `rc=0` | ✅ `timecheck`/`returnuicheck`/`searchcheck`/`actioncheck`/`locatecheck`/`locate-uicheck`/`replaycheck`/`clockcheck`/`timeuicheck` |
| A2 逐像素 | `rc=0` | ✅ `rc=0` |
| DYN 双路 ×5 | 各自 5/5 | ✅ **engine 5/5 + test 5/5**，`producer-readback OK` |

> ⚠️ 同一二进制的更早一次跑批出现过 `dyn-test 4/5` —— 掉的那跑是**已知显示侧停摆**
> （`D1-C02` 尾窗 0.0 fps + `D1-C07`，同跑 `D1-C01` 51.1 fps）。DYN 是**环境敏感量**，
> 按 T27/T29 先例报 `N/5` + 替身对照，**不洗成 PASS**。本次复跑 5/5。

## 文件

| 文件 | 说明 |
|---|---|
| `rc-summary.txt` | 本轮全部结论行（脚本 `tee` 产物，**唯一权威**） |
| `interactcheck-mac-run{1..5}.txt` | 正题逐跑完整日志（含 `act-note`、门"重试 0 次"、逐条判据） |
| `interactcheck-mac-n5.txt` | 正题 5 跑的 `INTERACTCHECK:` 行汇总 |
| `negctl-mac-run{1..5}.txt` / `negctl-mac-n5.txt` | 负控逐跑 + 汇总（含 `rc`/点名数/完整性自检/IT-07..12 是否真跑/✗ 数） |
| `probe-inactive-mac.txt` / `probe-inactive-mac-summary.txt` | 失活探针原始日志 + 读数汇总（`判据 13/16`，红=`[IT-06,IT-13,IT-14]`） |
| `regression-*.txt` | 9 项相邻回归 + A2 逐像素 |
| `regression-dyn-{engine,test}-metal-run{1..5}.txt` | DYN 双路逐跑（含生产者回读行） |
| `regression-dyn-{engine,test}-metal.txt` | DYN 双路汇总 |

## 这组证据要说明什么

1. **降级成立**：门失败不再短路 —— 5 条激活依赖判据被**逐条点名**记 UNAVAILABLE，
   其余 **11 条照跑**（负控里 IT-07..IT-12 `6/6` 真实出现；旧逻辑下这 6 行**根本不存在**），
   收尾 `rc=6` 与 `VERDICT=UNAVAILABLE`。
2. **口径没变**：正题 `判据 16/16` 与 T29/T30 **逐项一致** ⇒ 改的只有失败路径。
3. **分类表是被**测**出来的**：探针在"真实失活"窗口上把 16 条全跑一遍，
   红项恰好是表里的三条 ⇒ 表的内容有实测背书（另两条"探针下绿但仍算依赖"的理由见文档 §3）。
4. **仪器自身可复现**：`act-note` 逐跑可见 + 门"重试 0 次" + 跑完**残留进程 0**
   ⇒ 前台提升确实生效、且不再有幽灵实例。

## 覆盖边界（如实记录）

- 探针只探**窗口激活**这一个环境维度；`FORCE_INACTIVE` 的模式可复用作其它"环境前提"的探针模板。
- `INTERACT-INTEGRITY` 只在**门失败路径**上执行 —— 正题里分类表**不被校验**
  （"表写对了没有"在正题下没有观测点）。覆盖靠两条腿：探针证明表的**内容**、
  负控证明表的**落地**。若改成"改表同时改错守卫"，两条腿都在同一个人手里，属"人祸"边界。
- Windows 侧（W-T31）读数见 **`windows/README.md`**：**五轮**跑批的完整对照（首轮 = 发现平台
  差异、第二轮 = 逗号双重包裹、第三轮 = `$ok`/`$OK` 碰撞、第四轮 = 加诊断定位、最终轮 = 归档），
  外加三个仪器缺陷的机理与"给解析器自己加诊断"的定位手法。
