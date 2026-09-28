# T21 证据：搜索结果相关度排序（2026-09-28）

交付文档：`docs/T21_SEARCH_RANKING.zh_CN.md`
复跑：`tools/t21-verify.sh`（`core` / `regress` / `dyn N`）

> 环境口径与 T17–T20 **完全一致**（macOS / Apple M3 / Metal RHI，`STELQUICK_GRAPHICS_API=metal`）。
> 刻意不换口径——否则跨任务读数不可比（macOS 上 Vulkan 组合必丢设备，见 T13 §7.27）。

---

## 1. 判据总览

| 判据 | 位置 | 类型 | 正向读数 | 反向对照读数 |
|---|---|---|---|---|
| SRC-06 分级逐条覆盖 | 阶段 A（纯逻辑） | 7 例 | 7/7 对 | 仍对（不触引擎） |
| SRC-07 实测场景复现 | 阶段 A（纯逻辑） | 1 条 | 首行 = Moon | 仍对 |
| SRC-07b 同分次序 | 阶段 A（纯逻辑） | 1 条 | `Moon > Pirate > Ghost` | 仍对 |
| SRC-08 全序可复现 | 阶段 A（纯逻辑） | 1 条 | PASS | 仍对 |
| SRC-08b 比较器自洽 | 阶段 A（纯逻辑） | 1 条 | PASS | 仍对 |
| SRC-09 内建判别性对照 | 阶段 A（纯逻辑） | 1 条 | 字典序首行 ≠ Moon | 仍对 |
| **SRC-11 活引擎端到端** | 阶段 B（真实引擎） | 1 条 | **首行 = Planet:Moon** | **FAIL（实得 NGC 6781）** |
| **RP-04a 回放首行即月球** | I-REP-02 回放 | 1 条 | **PASS** | FAIL |
| SRC-01..05 / V1..V9 | 回归 | 25 条 | PASS | PASS |

**总判据数**：SEARCHCHECK 34 条（T20 时 27 条，本轮 +7）；REPLAYCHECK 11 条（T20 时 10 条，+RP-04a）。

---

## 2. 判据明细

### 2.1 纯逻辑腿 SRC-06..09（不触引擎、不用 fixture ⇒ 恒可跑）

```
SRC-06  PASS 匹配分级逐条覆盖 7 例全对（完全/前缀/词首/子串/不匹配，且大小写无关）
SRC-07  PASS 实测场景复现：搜「Moon」首行 = Moon（修复前是 Ghost of the Moon Nebula）；
             完整顺序 [Moon > Pirate Moon Cluster > Ghost of the Moon Nebula]
SRC-07b PASS 同分次序 = Moon > Pirate Moon Cluster > Ghost of the Moon Nebula
             （两条 Nebula 都是词首匹配，再按名字长度定序）
SRC-08  PASS 打乱输入顺序后排序结果不变（全序、可复现）
SRC-08b PASS 比较器自洽（非自反 + 不对称）—— 传非法比较器给 std::sort 是 UB
SRC-09  PASS 内建判别性对照：同批候选按纯字典序排（= 修复前引擎的排法）
             首行是「Ghost of the Moon Nebula」而不是 Moon
```

**SRC-07 的数据来源**：三个名字与类型**原样照抄** T20 回放证据 `2026-09-28-t20-return-ring/replaycheck-mac.txt`
的三条 RP-note（`Ghost of the Moon Nebula` / `Moon` / `Pirate Moon Cluster`）——这样"排序修好了什么"
可以直接拿 T20 的原始观测对照。

### 2.2 活引擎腿 SRC-11

```
SRC-11  PASS 活引擎端到端：搜「Moon」首行 = Planet:Moon
             （实得 Planet:Moon，匹配质量 完全匹配）；
             完整顺序 [Moon > Pirate Moon Cluster > Ghost of the Moon Nebula]
```

对照 T20 的同一条路径（当时还没有 SRC-11）：

```
（T20）RP-note 结果[0] "Ghost of the Moon Nebula" / english="Snowglobe Nebula" / Nebula:NGC 6781
（T20）RP-note 结果[1] "Moon" / english="Moon" / Planet:Moon
（T20）RP-note 月球在第 1 行（0 基）；首行是 "Ghost of the Moon Nebula"
```

### 2.3 回放 RP-04a（判据**强度提高**的见证）

T20：`RP-04` = 按名字找到月球那一行再点（并把"首行并非月球"照实打印，不据此判红）——**绕过问题**。
T21：`RP-04a` = **首行必须就是月球**——**断言问题已解决**。把排序改坏它立刻红。

---

## 3. 🔴 反向对照（注掉排序 ⇒ 判据必须红）

**手法**：把 `SearchResultsModel::collect()` 里的 `std::stable_sort` 包进 `#if 0`，重建重跑。

```
SEARCHCHECK: 33/34 PASS（含纵向判据 V1–V9）
SEARCHCHECK: PASS SRC-09 内建判别性对照：…（纯逻辑判据不受影响）
SEARCHCHECK: FAIL SRC-11 活引擎端到端：搜「Moon」首行 = Planet:Moon
             （实得 Nebula:NGC 6781，匹配质量 词首匹配）；
             完整顺序 [Ghost of the Moon Nebula > Moon > Pirate Moon Cluster]
SEARCHCHECK: VERDICT=FAIL
rc=10
```

证据：`searchcheck-negctrl-noranking.txt`

**这批读数证明了两件事**：

1. **对照跑回了修复前的世界**：实得顺序 `[Ghost of the Moon Nebula > Moon > Pirate Moon Cluster]`
   与 T20 的原始观测**逐字一致** ⇒ 对照有效，不是"随便红了一条"。
2. **两条腿互补、不可互相替代**：注掉排序后 **SRC-06..09 全部仍然 PASS**——纯逻辑判据
   根本测不到"排序没被调用"。若当初只写纯逻辑判据，这个缺陷会以"34/34 全绿"的形式溜过去。

**恢复确认**：`grep -rn "临时反向对照\|#if 0" src/` 为空；与备份 `cmp` 逐字节一致。

---

## 4. 有意为之的设计点

| 设计 | 理由 |
|---|---|
| 排序放在**模型层**（`SearchRanker`），不改引擎 | `StelObjectMgr::listMatchingObjects` 是共用原语，旧 `SearchDialog` 也在用（`SearchDialog.cpp:985`）；且"结果该怎么排"本就该由列表模型决定 |
| `SearchRanker` 是**纯逻辑**（只依赖 `QString`） | 两种形态都能编；可被自检**穷尽覆盖**分级规则，不需要拉起引擎 |
| 尾键用**字典序**兜底 | 只为了让**全序**成立；否则同键元素顺序未定义，"搜两次顺序不一样"会成为神出鬼没的 bug |
| `typeWeight` 只做**次要**因子 | 永远排在 `quality` 之后 ⇒ 分级/类型判错也不会破坏主序 |
| 中文**不判词首** | 中文没有词边界，互相之间都算字母 ⇒ 自然落到 `Substring`；按子串匹配更符合直觉 |
| 去重保留**相关度最高**那行（T21 改） | 两行的 `name` 可能是翻译名 vs 英文名，保留哪行决定用户看到的名字 |
| **不改** `SearchPage.qml`、**不动** delegate 的 46px 行高 | 顺序变化自动生效，无需改 QML；而回放判据按行高算场景坐标投递鼠标事件，改高度会**静默点偏** |
| SRC-09 做成**内建**判别性对照 | 把"判别性对照"从"跑一次 A/B"变成**每次运行都验一遍**的属性 |

---

## 5. 能力边界（⚠️ 别读成"搜索变好了"）

引擎的候选集截断发生在**排序之前、按遍历顺序**（`StelObjectModule.cpp:67-68` 是"累计到
`maxNbItem` 就 `break`"）⇒ **高相关度候选可能压根没进候选集**。

本层只在**已返回候选内**重排：解决 **precision@k**，不解决 **recall**。
判据因此只断言"已知候选的相对顺序"，**不断言召回完整**。

---

## 6. DYN 与文件清单

### 6.1 DYN（环境敏感量，处置同 T18–T20）

`regression-dyn-engine-metal*.txt` / `regression-dyn-stub-metal*.txt`，跑 3 次 + 替身对照。

**本轮读数：引擎 3/3 + 替身 3/3**。跑时环境：`load averages: 3.54 7.62 7.18`、
`displaysleep=2 分钟`、Spotlight `mdbulkimport` 在索引。

读数采信规则见 `tools/t21-verify.sh` 头注：**不得**把 `N/3` 里的任一次 PASS 当成"零退化已证明"；
替身也败 ⇒ 记"仪器测不到"，不洗成 PASS、也不改写成 INVALID。

### 6.1b 回归读数汇总

| 套件 | 读数 |
|---|---|
| `SEARCHCHECK`（T21 正题） | **34/34 PASS** rc=0 |
| `REPLAYCHECK`（含 RP-04a） | **11/11 PASS** rc=0 |
| `RETURNUICHECK` | **11/11 PASS** rc=0 |
| TIMECHECK / TIMEUICHECK | **14/14 / 13/13** |
| CLOCKCHECK / ACTIONCHECK | **PASS / PASS（0 FAIL）** |
| LOCATECHECK / UICHECK | **14/14 / 8/8** |
| A2 / S3 | **PASS / 8/8** |

### 6.2 文件清单

| 文件 | 内容 |
|---|---|
| `searchcheck-mac.txt` | **T21 正题**：34/34 PASS（含 SRC-06..11） |
| `searchcheck-negctrl-noranking.txt` | 🔴 反向对照：注掉排序 ⇒ 33/34、SRC-11 FAIL、rc=10 |
| `replaycheck-mac.txt` | I-REP-02 回放 11/11（含 **RP-04a 首行即月球**） |
| `returnuicheck-mac.txt` | T20 返回环 11/11（回归） |
| `regression-searchcheck.txt` | 搜索模型回归（`regress` 段） |
| `regression-timecheck.txt` / `regression-timeuicheck.txt` | T19 时间环回归 |
| `regression-locatecheck.txt` / `regression-locate-uicheck.txt` | T18 定位回归 |
| `regression-clockcheck.txt` / `regression-actioncheck.txt` | T16/T15 回归 |
| `regression-a2-metal.txt` / `regression-s3-stela3.txt` | A2 / S3 回归 |
| `regression-dyn-*.txt` | DYN 引擎 + 替身，各 3 次 |
| `rc-summary.txt` | 全量退出码汇总 |

### 6.3 读结果纪律

1. **SRC-06..09 恒可跑、与星表数据无关**；SRC-11 依赖 SolarSystem 数据，无数据时记 INFO **跳过**
   （环境条件，不判 FAIL）——**别把"跳过"读成"通过"**。
2. **排序 = precision@k**。"首行对了"不等于"该有的都有了"。
3. 只有 `OK/FAIL` 是稳定量；绝对数值是"当时那一跑"的。
