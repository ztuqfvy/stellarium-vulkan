# REPLAYCHECK 在负载下超时 —— 一轮"环境敏感"实证（2026-09-29）

> 目的：把套件连跑里 `regression-replaycheck rc=10` 这一项**定性**，而不是洗成 PASS，
> 也不是顺手赖给环境。

## 现象

`tools/t33-verify.sh all 5`（第 1 轮）在跑到第 9 个引擎进程时，`REPLAYCHECK` 报红：

```
REPLAYCHECK: RP-note 结果列表尚未完成布局（207×0）—— 有界等待就绪（≤25×100ms）
REPLAYCHECK: RP-note 等待 2500ms 后结果列表仍未完成布局（207×0）⇒ 判红，**不洗成 PASS**
REPLAYCHECK: RP-04 结果列表未完成布局（控件 207×0）—— 仪器没接上 …：FAIL
REPLAYCHECK: 判据 2/3
REPLAYCHECK: VERDICT=FAIL
```

原始日志：`../suite-run1-contaminated/regression-replaycheck.txt`。

**当时的现场负载**：

```
21:19  up 4 days, 23:03, 1 user, load averages: 3.65 4.53 4.72
mdbulkimport 在跑          ← Spotlight 批量索引（`t33-verify.sh` 的 env_note 会点名它）
```

`207×0` 这个签名**不是新的**：T22 的 `flaky-layout-207x0` / `layout-gate` 两组证据
（`docs/evidence/2026-09-28-t22-time-page/`）已把它定性为"Qt Quick 的布局/polish 由
**渲染循环**驱动，重压下读几何会早于 polish"，处置是**改判据协议**（有界就绪门、超时明确判红）
—— 判据本身不动，也**不洗成 PASS**。

## 三组读数

| 组 | 条件 | 读数 |
|---|---|---|
| **① 套件连跑**（案发） | load ≈ 3.65 + `mdbulkimport`，第 9 个引擎进程 | ❌ `2/3`（RP-04 红，`207×0`） |
| **② 隔离重测 ×8** | 同一时段、单跑、`mdbulkimport` 仍在 | ✅ **8/8 `11/11`**，`207×0` **0 次** |
| **③ 人造负载 ×3** | 6 个 `/usr/bin/yes` 打满 CPU（load 冲到 **5.30**） | ❌ `8/11` / ❌ `2/3`（**`207×0` 命中 3 次**）/ ✅ `11/11` |

**② vs ③ 的对照是这一轮的关键**：同一二进制、同一 `mdbulkimport` 环境，
**唯一变量是 CPU 饥饿** —— 拥挤时 `207×0` 稳定复现，空闲时 8/8 全绿。

⇒ **机理成立**：就绪门的时间预算（2500 ms）在 CPU 饥饿下不够用。

## 定性（不越界）

- **能做**的结论：这是**仪器（有界就绪门）在负载下的时间预算问题**，签名与 T22 同款，
  **不是** REPLAYCHECK 的判据逻辑变了、也不是回放流程断了（RP-01 锚点 9/9、RP-02 页号
  都对；失败点纯粹是"控件还没拿到尺寸"）。
- **不能**做的结论：**本轮没有做 T33 的 A/B 归因**（"多挂了第 5 页是否加重了首帧布局"）。
  理由是**统计功效不够**：案发率 ~1/9，A/B 各跑十几次也分不出分布差异。
  ⇒ 如实登记为**未归因的开放项**，不移交给"T33 引入的回归"。

## 文件

| 文件 | 内容 |
|---|---|
| `run1..8.txt` | 组②：隔离重测 8 次（全 `11/11`） |
| `load1..3.txt` | 组③：人造负载 3 次（`8/11` / `2/3` / `11/11`） |
| `../suite-run1-contaminated/` | 组①：案发时的 `rc-summary.txt` + `regression-replaycheck.txt` |
