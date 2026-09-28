# `flaky-layout-207x0` —— 全量回归首跑两套红的定性过程（**照实归档，不洗成 PASS**）

这里放的是 T22 全量回归首跑出现 `returnuicheck-mac rc=10` / `replaycheck-mac rc=10`
之后的**全部原始样本**。结论：**间歇，且根因在仪器侧（Qt Quick 布局 polish 被拖后），
不是本轮改动引入的退化。** 但失败样本**原样保留**，只加定性。

## 症状

| 项 | 失败读数 | 健康读数 |
|---|---|---|
| `replaycheck` | `结果列表第 0 行（列表 207×0，行高 46.0）` → 点击落空 → `RP-04/05/06/08/09` 连带红 | `列表 934×341`，`RP-04..09` 全 OK |
| `returnuicheck` | `RT-08 … 目标 AltAz 变化 0.0000°（门槛 >5.0）` | `13.9482°` |

共同点：**"真实点击没落在目标上"**。`207×0` = 该 `ListView` 还是"布局前尺寸"。

## 样本

| 文件 | 源码状态 | 读数 |
|---|---|---|
| `A-HEAD-source-green-replay.txt` / `-return.txt` | **全 HEAD**（`git stash` 后重建） | `934×341`、**11/11 PASS** |
| `B-t22-source-FAIL-run1..3.txt` | **T22 全量** | `207×0`、**6/11 FAIL**，3/3 |
| `C-timepage-head-FAIL-run1..3.txt` | T22 的 C++ + **HEAD 的 `TimePage.qml`** | `207×0`、**7/11 FAIL**，3/3 ⇒ **`TimePage.qml` 排除** |
| `D-same-source-as-C-PASS-summary.txt` | **与 C 逐字节同源**，重建后二进制 `38577576 B`（与 C 失败那次同尺寸） | `934×341`、**11/11 PASS**，6/6 |
| `E-probe-ancestor-chain.txt` | 带一次性探针（打印 `resultList` 祖先链） | 健康链完整（见下） |
| `00-读数汇总.txt` | —— | 上表机器汇总 |

**决定性的一步是 C 与 D**：同一份源码、同一个尺寸的二进制，先 3/3 FAIL、后 6/6 PASS
⇒ 结果不由代码决定。

## 探针读数（`E-probe-ancestor-chain.txt`，健康时）

```
窗口 960×640 ← 祖先链（近→远）：
QQuickListView[934x341@0,82]  QQuickColumnLayout[934x536@0,0]
QQuickRowLayout[944x536@8,8]  SearchPage_QMLTYPE_39[960x552@0,0]
QQuickStackLayout[960x552@0,88]  QQuickColumnLayout[960x640@0,0]  …
```

失败时**同一条代码路径**读到 `207×0` —— 链路本身没变，只是 polish 还没发生。

## 为什么判"仪器侧"

- **数据层全绿**：`RP-03` 结果 3 条、`RP-04b` 无重复、`RP-04a` 首行即 `Planet:Moon`；
- **几何层**是唯一红的，而几何由渲染/polish 驱动；
- 同一时段 DYN 归档记 `load averages: 6.02 → 11.55` + Spotlight `mdbulkimport` 在批量索引，
  且 **`真实引擎 0/3` 的同时 `替身对照 0/3`** —— 按既定规则即"**仪器测不到**"；
- 重跑时（`load averages: 4.50`、同一台机）**`DYN 3/3 + 替身 3/3`**，全量 14 项 `rc=0`。

## 处置（见 `../layout-gate/`）

改判据**协议**、不改判据：点击前加有界"布局就绪门"（`25 × 100ms`），
超时**明确判红**并写明"仪器没接上：既不作退化证据，也**不作 PASS**"。
