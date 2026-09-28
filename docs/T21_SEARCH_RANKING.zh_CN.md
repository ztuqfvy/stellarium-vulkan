# T21：搜索结果相关度排序（A4 正题）

> 交付日期：2026-09-28 ｜ 分支 `main` ｜ 前置：T17（搜索模型）/ T20（返回环 + 去重缺陷）
> 证据目录：`docs/evidence/2026-09-28-t21-search-ranking/`
> 复跑：`tools/t21-verify.sh`（`core` / `regress` / `dyn N`）

---

## 1. 任务与通过条件

**任务**：把搜索结果从"引擎给什么顺序就是什么顺序"改成**按相关度排序**——用户输入完整名称时，那个天体必须在第一条。

这是 T17 就登记、A4 反复列为待办的项。头注里当时写的是"更进一步的排序策略（相关度/类型分组）留给 A4 搜索页，不在本层臆造"。

**为什么现在做**：T20 的回放（I-REP-02）把它从"体验优化"变成了"实测缺陷"——
实测搜 `Moon` 的结果是：

```
[0] "Ghost of the Moon Nebula"   Nebula:NGC 6781
[1] "Moon"                       Planet:Moon
[2] "Pirate Moon Cluster"        Nebula:NGC 1647
```

用户输入的是**完整名称**，第一条却是名字里带 Moon 的星云。T20 当时的处置只能是"按名字定位月球那一行"并把"首行并非月球"照实打进证据——即**绕过问题**。

**通过条件**：

| # | 条件 | 判据 | 结果 |
|---|---|---|---|
| 1 | 搜 `Moon` 首行 = `Planet:Moon`（活引擎） | SRC-11 | ✅ |
| 2 | 分级规则逐条正确（完全/前缀/词首/子串） | SRC-06 | ✅ 7/7 例 |
| 3 | 同分次序（类型权重 → 名字长度）生效 | SRC-07b | ✅ |
| 4 | 排序可复现（打乱输入不变序）+ 比较器自洽 | SRC-08 / 08b | ✅ |
| 5 | **判据有检验力**（内建判别性对照） | SRC-09 | ✅ |
| 6 | **反向对照**：注掉排序，判据必须红 | SRC-11 FAIL / rc=10 | ✅ |
| 7 | 回归零退化（同一后端 Metal） | 见 §4 | ✅ |
| 8 | I-REP-02 回放判据**升级**为"首行即月球" | RP-04a | ✅ |

---

## 2. 设计

### 2.1 先把事实钉死：引擎上游是**自相矛盾**的

这不是"我觉得应该排个序"，而是引擎两处源码各干一半、互相抵消：

| 层 | 源码 | 行为 |
|---|---|---|
| 模块级 | `StelObjectModule::listMatchingObjects`（`StelObjectModule.cpp:45-75`） | **确实**按相关度排过：完全匹配被单独拎出来 `result.prepend(fullMatch)`（第 72-73 行），其余按名称 `std::sort`。头注也承诺 `by order of relevance`（`StelObjectModule.hpp:70`）。 |
| 聚合级 | `StelObjectMgr::listMatchingObjects`（`StelObjectMgr.cpp:595-611`） | 把各模块结果 `result += matchingObj` 拼接后，**无条件**按名称字典序 `std::sort`（第 609 行）。 |

⇒ **模块级那个 `prepend` 被聚合级的 `sort` 整个抹掉**。调用方拿到的是纯字典序，与头注承诺的"按相关度"不符。这就是上面 `[0]` 是星云的根因。

**为什么不在引擎里改**：`StelObjectMgr::listMatchingObjects` 是**共用原语**，旧 GUI 搜索框也在用（`SearchDialog.cpp:985`）。在引擎里改排序会牵连旧界面；而且"结果该怎么排"本来就该由**列表模型**决定（视图要什么序，模型给什么序）。

### 2.2 分级规则（`SearchRanker`）

纯逻辑类（只依赖 `QString`），两种形态都能编，可被自检直接穷尽覆盖：

| 级别 | 值 | 含义 | 例（查询 `Moon`） |
|---|---|---|---|
| `Exact` | 0 | 名称与查询**完全相同** | `Moon` |
| `Prefix` | 1 | 名称**以**查询开头 | `Moonlight` |
| `WordStart` | 2 | 命中点落在**某个词的开头** | `Pirate Moon Cluster`、`Ghost of the Moon Nebula` |
| `Substring` | 3 | 任意位置含查询 | `Honeymoon`（前邻是字母 `y` ⇒ 不算词首） |
| `None` | 4 | 不匹配（哨兵） | `Sirius` |

**tie-break 次序**：`quality` → `typeWeight` → `nameLength` → `name`（字典序）。

- 大小写不敏感（与引擎 `matchObjectName` 的口径一致：它用 `Qt::CaseInsensitive`）。
- 词边界判定 = "前一字符不是字母/数字"。**中文刻意不判词首**（中文没有词边界，互相之间都算字母 ⇒ 自然落到 `Substring`，按子串匹配更符合直觉）。
- `typeWeight`：太阳系天体 0 / 恒星 1 / 深空 2 / 其余 3。它是**次要**因子（永远排在 `quality` 之后），所以分级判错也不会破坏主序。
- 尾键用 `QString::operator<`（UTF-16 码点序，确定性）⇒ **全序**，同输入必得同输出。

### 2.3 ⚠️ 能力边界：只解决 precision@k，不解决 recall

引擎的候选集截断发生在**排序之前、按遍历顺序**：`StelObjectModule.cpp:67-68` 是"累计到 `maxNbItem` 就 `break`"，遍历的是 `listAllObjects(false) << listAllObjects(true)` 的原始顺序。

⇒ **高相关度的候选可能压根没进候选集**。本层只能在**已返回的候选内**重排。

这条必须写清楚，否则"排序做好了"会被误读成"搜索变好了"。判据因此只断言"已知候选的相对顺序"，**不断言召回完整**。想动召回就得改引擎的候选生成，不在本层。

### 2.4 连带改点：去重时保留**相关度最高**的那一行

引擎把同一个天体给**多行**（翻译名表 + 英文名表各枚举一遍，`StelObjectModule.cpp:52`），且 `StelObjectMgr` **不去重**。本层按 `stableId` 去重时，"保留哪一行"会决定用户**看到的名字**（两行的 `name` 可能是翻译名 vs 英文名）。

T17–T20 保留的是"遍历顺序最先"那条（因聚合层排过字典序，等价于"字典序最前"）；**T21 起改为按相关度比**。否则会出现"去重留下了低相关度的那个名字，排序再准也白搭"。

---

## 3. 判据：两条腿，**互补且不可互相替代**

这是本轮最值得记下来的设计。

| 腿 | 判据 | 覆盖什么 | 覆盖**不了**什么 |
|---|---|---|---|
| 纯逻辑 | SRC-06..09 | 分级规则、同分次序、稳定性、比较器自洽 | **证明不了 `collect()` 调了它** |
| 活引擎 | SRC-11 | `collect()` 真的执行了排序（搜 Moon 首行 = Planet:Moon） | 证明不了规则细节（只有一个采样点） |

**缺任何一条都有假绿空间**：规则再对、没人调用是白搭；调用对了、规则写错也白搭。

**反向对照的实测数据恰好证明了这个互补性**（§4.2）：注掉 `collect()` 里的 `stable_sort` 后，**SRC-06..09 全部仍然 PASS**，只有 **SRC-11 FAIL**。如果当初只写了纯逻辑判据，这个缺陷会以"34/34 全绿"的形式溜过去。

### 3.1 内建判别性对照（SRC-09）

把同一批候选按**纯字典序**排一遍（= 修复前引擎的排法），首行必须**不是** `Moon`：

```
SRC-09 PASS 同批候选按纯字典序排（= 修复前引擎的排法）首行是
           「Ghost of the Moon Nebula」而不是 Moon
           ⇒ 本组判据确实能区分两种排序，SRC-07 有检验力
```

若这条 FAIL，说明该场景两种排法给同样结果、SRC-07 **测不出任何东西**——那时该**换数据**，而不是改判据。这条把"判别性对照"从"跑一次 A/B"变成了**每次运行都验一遍**的属性。

### 3.2 RP-04a：判据强度是**提高**的

T20 的回放里，RP-04 是"按名字找到月球那一行再点它"，并把"首行并非月球"照实打印（不据此判红）。T21 起改为：

```
RP-04a 首行即月球 Planet:Moon（相关度排序生效）—— 实得 Planet:Moon
```

从"绕过已知问题"变成"**断言问题已解决**"。把 T21 的排序改坏，它立刻红——判据没被放宽，是收紧了。

---

## 4. 验收

（读数见 §4.1 / §4.2；原始证据在 `docs/evidence/2026-09-28-t21-search-ranking/`）

### 4.1 正向：SEARCHCHECK 34/34 PASS

关键行：

```
SRC-06 PASS 匹配分级逐条覆盖 7 例全对（完全/前缀/词首/子串/不匹配，且大小写无关）
SRC-07 PASS 实测场景复现：搜「Moon」首行 = Moon（修复前是 Ghost of the Moon Nebula）；
           完整顺序 [Moon > Pirate Moon Cluster > Ghost of the Moon Nebula]
SRC-07b PASS 同分次序 = Moon > Pirate Moon Cluster > Ghost of the Moon Nebula
SRC-08 PASS 打乱输入顺序后排序结果不变（全序、可复现）
SRC-08b PASS 比较器自洽（非自反 + 不对称）
SRC-09 PASS 内建判别性对照：字典序首行是「Ghost of the Moon Nebula」而不是 Moon
SRC-11 PASS 活引擎端到端：搜「Moon」首行 = Planet:Moon（实得 Planet:Moon，匹配质量 完全匹配）
```

### 4.2 🔴 反向对照：注掉排序 ⇒ rc=10，SRC-11 FAIL

把 `collect()` 里的 `std::stable_sort` 注掉（`#if 0`）后重建重跑：

```
SEARCHCHECK: 33/34 PASS
SEARCHCHECK: FAIL SRC-11 活引擎端到端：搜「Moon」首行 = Planet:Moon
             （实得 Nebula:NGC 6781，匹配质量 词首匹配）；
             完整顺序 [Ghost of the Moon Nebula > Moon > Pirate Moon Cluster]
SEARCHCHECK: VERDICT=FAIL        rc=10
```

**两点值得单独记**：

1. 实得顺序 `[Ghost of the Moon Nebula > Moon > Pirate Moon Cluster]` **与 T20 的原始观测逐字一致**——对照跑回了修复前的世界，说明对照有效。
2. **SRC-06..09 仍全绿**（那是纯逻辑判据，不受 `collect()` 影响）⇒ 只有活引擎腿能抓到"没被调用"。

证据：`searchcheck-negctrl-noranking.txt`。恢复后反查 `grep -rn "临时反向对照\|#if 0" src/` 为空、与备份逐字节 `cmp` 一致。

### 4.3 回归（零退化）

| 套件 | 读数 |
|---|---|
| `REPLAYCHECK`（含新增 RP-04a） | **11/11 PASS** rc=0 |
| `RETURNUICHECK` | **11/11 PASS** rc=0 |
| TIMECHECK / TIMEUICHECK | **14/14 / 13/13** |
| CLOCKCHECK / ACTIONCHECK | **PASS / PASS（0 FAIL）** |
| LOCATECHECK / UICHECK | **14/14 / 8/8** |
| A2 / S3 | **PASS / 8/8** |
| DYN（引擎 + 替身） | **3/3 + 3/3**（跑时 load avg 3.54、`displaysleep=2` 分钟、`mdbulkimport` 在索引——**不改变 T18 的环境敏感定性**） |

口径与 T17–T20 一致：macOS + Metal RHI，**刻意不换**。

---

## 5. 改动清单

| 文件 | 改动 |
|---|---|
| `src/app/SearchRanker.hpp` / `.cpp` | **新增**。纯逻辑排序器：分级 + tie-break + 全序比较。头注写清上游自相矛盾、为什么只在模型层做、能力边界。 |
| `src/app/SearchResultsModel.hpp` | `Row` 加 `quality` 观测字段；新增 `QualityRole`；重写"排序口径"头注（T17 记录事实 → T21 落地修复）；`RankRole` 语义从"行序"改为"相关度序"。 |
| `src/app/SearchResultsModel.cpp` | `collect()` 重构为 ①按 stableId 去重（保留**相关度最高**）②相关度排序 ③截断。 |
| `src/app/SearchModelCheck.hpp` / `.cpp` | 新增 SRC-06..09（纯逻辑，阶段 A）+ SRC-11（活引擎，阶段 B 末尾）；头注补判据清单。 |
| `src/ui/main.cpp` | REPLAYCHECK：RP-04 拆为 **RP-04a（首行即月球，断言）** + 原点击/选中断言；判据数 10 → 11。 |
| `src/ui/CMakeLists.txt` | 登记 `SearchRanker.hpp/.cpp`。 |
| `tools/t21-verify.sh` | 新增。SEARCHCHECK 提为 core 段首位（T21 正题）。 |
| `docs/evidence/2026-09-28-t21-search-ranking/` | 新增。 |

**刻意没改**：`SearchPage.qml`。delegate 只读 `model.name / objectType / englishName`，顺序变化自动生效；**并刻意不动 delegate 的 46px 行高**——回放判据是按行高算场景坐标投递鼠标事件的，改高度会静默点偏。

---

## 6. 移交 A4

本轮做完后，A4 剩余：

- **拼音 / 类型分组排序**：本轮只做"名称相关度 + 类型权重"。拼音需要拼音表，类型分组（行星/恒星/深空分节）是 UI 层决策，都留待后续。
- 显示历法 / MJD、速率 GUI、时区选择器。
- QML 交互级测试补键盘滚轮与视觉层。
- 跟踪标志泄漏根治（引擎侧）。
- DYN 停摆根因。
- 引擎 `listMatchingObjects` 不去重（**记录在案，不改**——共用原语，旧 SearchDialog 也在用）。
- **召回问题**：见 §2.3，候选集截断在排序之前 ⇒ 本层无法解决。

---

## 7. 复现与两条读结果纪律

```sh
/opt/homebrew/bin/cmake --build build-release --target stelQuickUI -j 8
tools/t21-verify.sh all
```

**纪律一**：`SEARCHCHECK` 里的 SRC-06..09 是纯逻辑判据，**恒可跑**、与星表数据无关；SRC-11 依赖 SolarSystem 数据，无数据时记 INFO 跳过（环境条件，不判 FAIL）。别把"跳过"读成"通过"。

**纪律二**：排序解决的是 **precision@k**。看到"首行对了"不等于"该有的都有了"——召回问题在候选生成那一侧，不在本轮范围。
