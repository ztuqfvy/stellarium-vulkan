# T26 证据目录：召回问题的引擎侧解法（2026-09-28）

## 结论

- **SEARCHCHECK 51 → 54**（新增 SRC-12/13/14 活引擎腿）：`54/54` rc=0 ×5
- 两轮代码级负控各命中预期红（两层独立检验力）
- 回归 10 项 rc=0（含 **S3 旧宿主**——动 src/core 硬要求；旧 SearchDialog 走的原语一字未动）
- DYN 引擎 3/3 + 替身 3/3；a2-metal rc=0
- ⚠️ regression-interactcheck 套件内首跑 rc=10（IT-06），复跑 **6/6 ×5** 全绿 ⇒ 定性为**间歇**（套件连跑中 Spotlight 批量索引 + 点击注入时机），非退化；复跑日志见 `interactcheck-flaky/`

## 本任务改了什么

T21/T24 时代搜索模型只能重排"引擎已返回的候选"——引擎候选集截断发生在
枚举序里（`StelObjectModule::listMatchingObjects` 每模块 `maxNbItem` 先到先得），
高相关度候选可能压根没进池。T21 明确留界："想动召回就得改引擎候选生成"。

T26 引擎新增**加性无截断原语**（语义零复刻）：
- `StelObjectModule::listAllMatchingObjects`（默认实现 = 以 `INT_MAX` 预算调用
  本模块 `listMatchingObjects`，各模块自定义匹配逻辑全部复用，只去预算）
- `StelObjectMgr::listAllMatchingObjects`（同构聚合，无每模块预算）

模型 `collect()` 改调新原语，截断移到**排序之后**——T21 留下的 recall 边界闭环。

## 读数（SRC-12..14，活引擎）

| 判据 | 内容 | 首跑读数 |
|---|---|---|
| SRC-12 | 无截断原语全量 > 旧每模块 cap=3 调用（判别对照）∧ 词首候选在池内 | 1573 > 16，词首在池 |
| SRC-13 | 模型 cap=3 → 3 行 ∧ lastRawMatchCount == 引擎全量数（**负控靶心**） | raw=1573 == 1573 |
| SRC-14 | 全量池上排序层把高相关档（完全/前缀/词首）顶到首行 | 前缀匹配 |

## 负控（两轮，各独立证明一层判据的检验力）

| 负控 | 操作 | 结果 |
|---|---|---|
| ① | 模型回退旧截断调用 `listMatchingObjects(q, cap)` | **SRC-13 红**（raw=16 ≠ 1573），SRC-12/14 不受伤，rc=10 |
| ② | 引擎新原语退化为 `listMatchingObjects(prefix, 3)` | **SRC-12 红**（16 == 16 不大于），SRC-13 绿（raw==全量双双同值——两层各由不同判据守住），rc=10 |

首跑存档：`negctrl/first-run-SRC14-too-narrow.log`（SRC-14 断言过窄——漏了
"前缀匹配"档（`MatchQuality::Prefix`），只接受 Exact/WordStart；前缀档正是
高相关档，放宽为 `≤ WordStart`。判据口径为正确性服务，不是复述首版想象）。

## 文件清单

- `searchcheck-mac-n5.txt` / `searchcheck-mac-run{1..5}.txt`：正题 ×5
- `negctrl/`：首跑 + 两轮负控
- `interactcheck-flaky/`：interactcheck 间歇定性复跑 ×5（全 6/6）
- `regression-*.txt`：回归 10 项 + DYN 引擎/替身 ×3 + S3
- `rc-summary.txt`：汇总（interactcheck 行保留 rc=10 原始读数，定性见上）
