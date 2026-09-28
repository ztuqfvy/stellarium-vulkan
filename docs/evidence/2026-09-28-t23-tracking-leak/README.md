# T23 证据目录 —— 跟踪标志泄漏根治（引擎侧）

日期：2026-09-28。交付文档 `docs/T23_TRACKING_LEAK_ROOTCURE.zh_CN.md`。

## 1. 文件总览

| 文件 | 内容 |
|---|---|
| `locatecheck-mac.txt` | **T23 正题·引擎/命令层**（15 条：LOC-08b 加严 + LOC-09 新增） |
| `uicheck-mac.txt` | **T23 正题·UI 层端到端**（10 条：UI-09/UI-10 新增） |
| `regression-*.txt` | 回归：T22 时间页两套 / T21 搜索 / T20 返回+回放 / T16 时钟 / T15 命令 / A2 / DYN×2 / **S3 旧宿主** |
| `rc-summary.txt` | 全量 rc 汇总 |
| `negctrl/` | **负控**：临时回退引擎修复后重跑（两套必须红 —— 它们确实红了） |
| `positive-n5/` | 正跑 N=5（两套各 5 次全绿 + 汇总） |
| `history/` | T18 时代的 LOC-08b 原始存档 —— 泄漏的**原始证据**（`清后 合取=false/引擎原始=true`） |

## 2. 判据明细（正跑 run1）

```
LOC-08b 清选中后跟踪彻底归零（进入跟踪 true；清前 合取=true/引擎原始=true
        → 清后 合取=false/引擎原始=false）：OK
LOC-09  换选归零对照（进入跟踪 true，rawBefore=true → 换选「Jupiter」后 rawAfter=false）：OK
UI-09-prep 重进跟踪（locateSelected=true，isTracking=true）：OK
UI-08   清除选中后文案=""（不得再声称正在跟踪）：OK
UI-10   真实点击「清除选中」→ isTracking=false trackedName=""：OK
```

## 3. 三代读数对照（同一判据 LOC-08b）

| 版本 | 清后 合取/引擎原始 | 判定 | 出处 |
|---|---|---|---|
| T18（缺陷在，判据弱） | false / **true** | OK（只断言合取） | `history/t18-era-locatecheck-leak-evidence.txt` |
| **T23 修复** | **false / false** | OK（加严） | `locatecheck-mac.txt` |
| **T23 负控** | **true / true** | FAIL ✓ | `negctrl/locatecheck-with-fix-reverted.txt` |

## 4. 负控读数（失败路径是活的）

```
LOCATECHECK 14/15 rc=10：LOC-08b FAIL；LOC-09 OK（对照腿不依赖修复）
UICHECK      8/10 rc=10：UI-08 FAIL（文案="正在跟踪：Moon"）、UI-10 FAIL（isTracking=true）
```

负控同时证明 `isTracking()` 简化是**加严**：泄漏不再被合取藏住
（T18 时代合取值 false 会把 `引擎原始=true` 掩盖）。

## 5. rc-summary 摘录

```
locatecheck-mac rc=0 / uicheck-mac rc=0
回归：timecheck/timeuicheck/searchcheck/returnuicheck/replaycheck/
      clockcheck/actioncheck/a2-metal 全 rc=0
regression-dyn-engine-metal 2/3 PASS（已知间歇形态；替身 3/3 PASS ⇒ 非退化）
regression-s3-stela3 rc=0   ← 动了 src/core/ 后旧宿主回归（硬要求）
```

## 6. 读结果纪律

1. 负控文件**不是失败，是证据** —— 证明判据的失败路径是活的。
2. DYN `2/3` 按 T18 起的定性读：间歇环境敏感量；替身 3/3 说明仪器测得到，
   失败的那次照实保留、不洗成 PASS。
3. 判据总数核对：LOCATECHECK 13 → **15**、UICHECK 8 → **10**（UI-08 曾被
   新相位短路，首跑 9/9 抓回 —— 见交付文档 §3.5）。
