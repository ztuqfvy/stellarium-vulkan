# T26：召回问题的引擎侧解法（候选池全量化）

日期：2026-09-28 ｜ 分支 `main` ｜ 证据：`docs/evidence/2026-09-28-t26-recall/`

## 1. 任务定性

A4 加固项"召回问题"（T21/T24 明确留下的边界）。**召回**（recall）与排序
（precision@k）是两件事：

- T21 修的是"结果里有的东西排对"——引擎聚合层无条件字典序把模块级完全匹配
  优先序抹掉，`SearchRanker` 在模型层重排（不改引擎，共用原语）。
- T24 修的是"拼音查询进不了候选集"——`yueqiu` 与"月球"字面永不匹配，加第二
  条候选源（拼音分支）。
- **T26 修的是最后一条**：引擎候选集截断发生在**排序之前、按枚举顺序**
  （`StelObjectModule::listMatchingObjects` 的"累计到 `maxNbItem` 就 break"），
  高相关度候选可能压根没进池。排序层再强也无法凭空召回。

## 2. 修法：加性无截断原语（语义零复刻）

### 2.1 为什么不是"传个大的 maxNbItem"

模型把 `cap` 传给旧原语即可让截断形同虚设——但那是**魔法常数 + 语义混淆**：
`maxNbItem` 的"每模块上限"语义还在，任何模块未来加一个绝对早退，召回就悄悄
裂开。要的是**契约显式化**：一个名字就叫"无截断"的原语。

### 2.2 为什么不是"模型自己枚举 listAllObjects"

T24 拼音分支已经这么干过一次（那是迫不得已——拼音匹配引擎根本没有）。若字面
匹配也改成模型侧自己 `contains`，就会**复刻引擎匹配语义 = 双轨**（T15 铁律），
而且 `StarMgr`/`NebulaMgr` 各有自有匹配逻辑（专名表、M/IC/NGC/Mel 编号表），
绕开原语反而**丢召回**。

### 2.3 实际做法（最小侵入）

```
StelObjectModule::listAllMatchingObjects(objPrefix, useStartOfWords)   // 新虚拟
    ⇒ 默认实现：listMatchingObjects(objPrefix, INT_MAX, useStartOfWords)
      ——各模块自定义匹配逻辑全部复用（StarMgr 预算递减、NebulaMgr 编号表循环
        在 INT_MAX 下自然跑满），唯一行为差异是去掉枚举序截断。

StelObjectMgr::listAllMatchingObjects(objPrefix, useStartOfWords)      // 新聚合
    ⇒ 与 listMatchingObjects 同构（逐模块 + 字典序），无每模块预算；不去重
      （翻译/英文双表行为保持，模型层 stableId 去重不变）。
```

模型 `collect()` 一行换原语；`cap` 截断移到排序之后（原有逻辑不变）。
**旧 SearchDialog 走的 `listMatchingObjects` 一字未动**（S3 回归 rc=0 佐证）。

改动面：`StelObjectModule.{hpp,cpp}`、`StelObjectMgr.{hpp,cpp}`（引擎，加性），
`SearchResultsModel.{hpp,cpp}`（接线 + 头注更新），`SearchRanker.hpp`
（能力边界注：T21 写下、T26 闭环），`SearchModelCheck.cpp`（+3 判据）。

### 2.4 首跑实测读数（SRC-12 判别对照）

查询 `"al"`：无截断原语全量 **1573** 条 vs 旧每模块 cap=3 调用 **16** 条——
**截断真的丢过 99% 的候选**。这不是理论边界，是每天发生在搜索框里的现实。

## 3. 判据：SEARCHCHECK 51 → 54

| 判据 | 断言 | 设计要点 |
|---|---|---|
| SRC-12 | `listAllMatchingObjects("al")` 全量 > `listMatchingObjects("al", 3)` ∧ 词首候选在池内 | 判别对照：证明旧截断真的丢过 + 新池里有高相关档 |
| SRC-13 | `searchObjects("al", 3)` → 3 行 ∧ `lastRawMatchCount()` == 引擎全量数 | **负控靶心**：孤立断言"行数=cap"证明不了来源，raw 与引擎侧读数**成对**才判别 |
| SRC-14 | cap=5 首行质量 ∈ {完全, 前缀, 词首} | recall 的收益落到 precision@1（全序由 SRC-08 保证，首行可确定断言） |

查询选 `"al"`：星名 Al* 恒多（星名表 core 自带，不依赖 SolarSystem），
且此段在 PINY 语言还原之后执行。

**首跑修正**：SRC-14 原断言只接受 Exact/WordStart，实跑首行是**前缀档**
（`MatchQuality::Prefix`——"al" 对 "Albireo" 整名前缀命中，比词首更优）。
放宽为 `≤ WordStart`（枚举序单调，越前越优）。判据为正确性服务，不为首版
想象背书。

## 4. 负控（两轮，各独立证明一层）

| 轮 | 操作 | 预期 | 实测 |
|---|---|---|---|
| ① | 模型回退旧截断调用 | SRC-13 红，其余绿 | ✅ raw=16≠1573 红，SRC-12/14 绿，rc=10 |
| ② | 引擎原语退化为 `listMatchingObjects(prefix, 3)` | SRC-12 红 | ✅ 16==16 不大于，rc=10（SRC-13 绿——raw==全量双双同值，**两层各由不同判据守住**，这正是双轮负控的价值） |

## 5. 读数汇总

- SEARCHCHECK **54/54** rc=0 ×5（`searchcheck-mac-n5.txt`）
- 负控 ①②各 rc=10、各命中预期红（`negctrl/`）
- 回归 10 项 rc=0：actioncheck、locatecheck、locate-uicheck、timecheck、
  timeuicheck、returnuicheck、replaycheck、clockcheck、a2-metal、
  **S3 旧宿主**（动了 src/core 的硬要求）
- DYN 引擎 3/3 + 替身 3/3（既有间歇形态）
- ⚠️ regression-interactcheck 套件内首跑 rc=10（IT-06），复跑 **6/6 ×5** 全绿
  ⇒ 定性**间歇**（套件连跑中 Spotlight 批量索引干扰点击注入时机），非退化；
  复跑日志归档 `interactcheck-flaky/`。

## 6. 边界与遗留

- **性能**：全量枚举每键一次。实测 SEARCHCHECK 单跑无感；`"al"` 全量 1573 条
  的枚举成本 = 各模块哈希表/数组一次线性扫描 + 每命中一次哈希查 ID，毫秒级。
  若未来超大盘（如加载完整 Gaia 命名）出现卡顿，再做惰性/分批，不预优化。
- **召回的最后一块**仍在：引擎候选池现在全量，但**拼音分支**的候选生成在模型
  层全量枚举——已与字面候选同池排序，无截断问题。召回问题至此**闭环**。
- A4 剩余：交互级其余（pinch/拖拽/输入法/视觉层）、DYN 停摆根因、Windows 复验。
