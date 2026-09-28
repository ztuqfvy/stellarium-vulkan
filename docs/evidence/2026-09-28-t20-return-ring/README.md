# T20「返回」环 + I-REP-02 全流程回放 — 证据包

任务：A4 固定流程 I-REP-02「开机→搜月球→定位→改时间→**返回**」的**第四个环**（"返回"），
以及这条流程的 **A-alpha 出口测试**（全流程回放）。
日期：2026-09-28。一键复跑：`tools/t20-verify.sh`（`core` / `regress` / `dyn N` 三个子命令）。

**返回语义（2026-09-27 定案）**：**返回 = 切回天空视口页 + 状态全保留**（不是撤销）。
定案理由与判据设计见 `docs/T20_RETURN_RING.zh_CN.md`。

---

## 1. 判据总览（同一次连贯运行）

| # | 套件 | 文件 | rc | 结论 |
|---|---|---|---|---|
| ① | **T20 返回环 UI 端到端** | `returnuicheck-mac.txt` | 0 | **11/11 PASS**（RT-01..11） |
| ② | **T20 I-REP-02 全流程回放** | `replaycheck-mac.txt` | 0 | **10/10 PASS**（RP-01..09 含 04b） |
| ③ | T19 时间自检（回归） | `regression-timecheck.txt` | 0 | 14/14 PASS |
| ③b | T19 时间页 UI 端到端（回归） | `regression-timeuicheck.txt` | 0 | 13/13 PASS |
| ④ | T18 定位/跟踪自检（回归） | `regression-locatecheck.txt` | 0 | 14/14 PASS |
| ④b | T18 UI 端到端（回归） | `regression-locate-uicheck.txt` | 0 | 8/8 PASS（**T20 动过 `SearchPage`**） |
| ⑤ | T16 时钟纯逻辑（回归） | `regression-clockcheck.txt` | 0 | VERDICT=PASS（12 判据） |
| ⑥ | T15/T16 命令通路 + 集成（回归） | `regression-actioncheck.txt` | 0 | VERDICT=PASS（0 FAIL；**Esc 拦截与 AC-12 同址**） |
| ⑦ | T17 搜索/选择模型（回归） | `regression-searchcheck.txt` | 0 | **27/27 PASS**（**+SRC-05d，SRC-05c 口径已修正**，见 §3） |
| ⑧ | A2 静态纹理（Metal，回归） | `regression-a2-metal.txt` | 0 | VERDICT=PASS |
| ⑨ | DYN 动态帧 · 真实引擎生产者 | `regression-dyn-engine-metal.txt` + `-run1..3.txt` | **3/3 PASS** | 见 §5（**不要读成"已修好"**） |
| ⑨b | DYN · 替身生产者（判别性对照） | `regression-dyn-stub-metal.txt` + `-run1..3.txt` | **3/3 PASS** | 路径不含 T20 代码 |
| ⑩ | S3 旧宿主引擎集成（回归） | `regression-s3-stela3.txt` | 0 | 8/8 PASS（`EngineWallClock` 路径零退化） |

`rc-summary.txt` 是机器可读汇总。**DYN 那两行不是 `rc=` 而是 `N/3`**，理由见 §5。

---

## 2. 判据明细

### 2.1 `returnuicheck-mac.txt`（RT-01..11，UI 层端到端）

| 判据 | 量什么 | 实测读数 |
|---|---|---|
| RT-01 | 锚点可寻且可见 | `skyReturnButton`/`pageStack`/`navSearchButton`/`navTimeButton`/`timeAddHourButton`/`skyKeySink` **6/6**；页索引 sky=1 search=2 time=3 |
| RT-02 | 真实点击「时间（T19）」→ 切到工具页 | `currentIndex=3`（期望 3） |
| **RT-03** | **前置状态非平凡**（配对②有意义的前提） | 跟踪=true、选中=true、`stableId="Planet:Moon"` |
| **RT-04** | **配对①：页面确实切回去了** | 真实点击「返回天空」→ `currentIndex=1`（期望 1） |
| **RT-05** | **配对②：时间源纹丝不动** | 返回后引擎 JD 位移 **0.000e+00** 天（要求 <1e-12） |
| **RT-06** | **配对②：选中与跟踪一并保留** | tracking=true；`trackedName` "Moon"→"Moon"；`stableId` "Planet:Moon"→"Planet:Moon" |
| **RT-07** | **负控**：不碰时间只做一次切页往返 | 目标 AltAz 变化 **0.0000°**（上限 <0.05）⇒ 排除"切页本身就在动世界" |
| **RT-08** | **判别性对照（返回 ≠ 撤销）** | 改 1 小时后返回 → 页面=1 **且** AltAz 变化 **13.9181°**（门槛 >5.0） |
| RT-09 | Esc 起点确认 | 真实点击「搜索天体」→ `currentIndex=2` |
| RT-10 | **Esc 返回链路是活的** | 非天空页投递**真实 Esc**（被受理=true）→ `currentIndex=1` |
| RT-11 | **负控**：天空页上的 Esc 无副作用 | `currentIndex=1`（不变）且 JD 位移 0.000e+00 |

**RT-08 是这一环唯一能把"返回"与"撤销"分开的判据**：若有人把返回实现成"恢复进入
工具页前的快照"，RT-04/05/06/07 会全绿而 RT-08 会红（星空没变）。

### 2.2 `replaycheck-mac.txt`（RP-01..09，I-REP-02 全流程回放）

全程**只投递真实鼠标事件**（输入框的值走真实 `text` 属性、按钮走真实点击），不绕过任何一层。

| 判据 | 环节 | 实测读数 |
|---|---|---|
| RP-01 | 锚点 | 9 个控件 **9/9** |
| RP-02 | **开机** | 开机态 `currentIndex=1`（= 天空页）；时钟暂停；开机 JD=2461311.743954 |
| RP-03 | **搜月球（输入 + 点搜索）** | 结果 **3 条**（>0） |
| **RP-04b** | 结果列表**无重复天体** | 3 行 → 3 个不同 stableId，重复 **0** 条（**反向对照见 §3**） |
| RP-04 | 点结果行 → 选中月球 | `displayName="Moon"` / `stableId="Planet:Moon"` |
| RP-05 | **定位** | 真实点击后 `tracking=true`、`stableId="Planet:Moon"` |
| RP-06 | **改时间** | 真实点击「+1 时」→ JD 位移 **0.041667 天**（= 1/24，±30%） |
| RP-07 | **返回·页面** | 真实点击「返回天空」→ `currentIndex=1` |
| RP-08 | **返回·状态保留** | 时间位移 0.041667 天（**未被撤销**）+ tracking=true + stableId 不变 |
| RP-09 | **末态·星空（I-DYN-02 内核）** | 改 1 小时后月球 AltAz 相对改前变化 **13.9173°**（门槛 >5.0） |

**为什么末态要断言四样互相独立的量**：「流程跑完了」不能由"最后一屏看着对"来证明。
页面 / 时间 / 跟踪 / 星空各自独立，四条同时成立才说明四个环**都真的落地了**。

**RP-03/RP-04 的一处诚实记录**：搜 "Moon" 的**首行不是月球**，而是
`Ghost of the Moon Nebula`（NGC 6781）—— 引擎按**名称字典序**排，
而"按相关度排序"是 A4 里**已登记但尚未做**的一项。
所以本回放**按名字定位月球那一行**（I-REP-02 说的是"搜月球→定位"，不是"盲点第一条"），
并把"月球在第 1 行、首行是 NGC 6781"**照实打进证据**，不粉饰成"排序没问题"。

---

## 3. 🔴 回放首跑抓到的真实缺陷：搜索结果**重复行**

**现象**（首跑 `replaycheck-mac.txt` 的 FAIL 现场，已留档 `replaycheck-negctrl-nodedupe.txt` 的姊妹记录）：

```
RP-03 「搜月球」→ 结果 5 条
RP-note 结果[0] "Ghost of the Moon Nebula" / Nebula:NGC 6781
RP-note 结果[1] "Ghost of the Moon Nebula" / Nebula:NGC 6781   ← 同一个天体
RP-note 结果[2] "Moon"                     / Planet:Moon
RP-note 结果[3] "Pirate Moon Cluster"      / Nebula:NGC 1647
RP-note 结果[4] "Pirate Moon Cluster"      / Nebula:NGC 1647   ← 同一个天体
```

**根因**：`StelObjectModule::listMatchingObjects` 把**翻译名表**与**英文名表**各枚举
一遍（`objs << listAllObjects(false) << listAllObjects(true)`），只要该天体有 ≥2 个
名字里含查询串就**各产出一条**；`StelObjectMgr` 只做拼接 + 按名称字典序重排，
**不去重**。Sirius 更极端：原始 4 条全是同一个 `Star:HIP 32349 A`。

**修法**：`SearchResultsModel` 按 `stableId` 去重（保留字典序最前的一条）。
**不改引擎**：那是上游 `SearchDialog` 也在用的原语，改它会牵连旧界面；而且
"同一个 object 只该有一行"本来就是**列表模型**该保证的事。

**反向对照（证明 RP-04b 不是摆设）**：临时注掉去重 → 重建重跑 ⇒
`RP-04b` **FAIL**（5 行 → 3 个不同 stableId，重复 2 条）、**9/10**、**rc=10**
（原始记录 `replaycheck-negctrl-nodedupe.txt`）。恢复后 10/10。

### 3.1 连带修正：T17 的 SRC-05c **原口径在为缺陷背书**

原判据：`exactRows == min(exactRaw, 10)`。去重之后 `exactRows=1`、`exactRaw=4`
⇒ **正确值被判红**（`regression-searchcheck rc=10`）。也就是说这条判据把
"原始条数"当成了"唯一行数"，**它一直在为题重复行背书** —— 26 条全绿也没抓到重复行。

修正后的口径（`SearchModelCheck.cpp` SRC-05c/05d）：

```
SRC-05c 未超限时行数 = 去重后条数，上限不再额外截断
        （cap=10 → 1 行；cap=4（不截断）→ 1 行；引擎原始 4 条 ⇒ 去重掉 3 条）：OK
SRC-05d 结果内无重复天体（1 行 → 1 个不同 stableId）：OK
```

⇒ SEARCHCHECK 由 26/26 变为 **27/27**。T17 交付文档 §4.1 已加修正说明。

---

## 4. 有意为之的设计点

| 点 | 理由 |
|---|---|
| 返回**只**切 `StackLayout`，不进 ActionRouter/AppFacade | "页"是纯 UI 概念，引擎不知道有页。塞进命令层只会给单点键位路由添一个假动作 |
| 「返回天空」按钮**始终可用**（即使已在天空页） | 这样"在天空页点它必须什么都不发生"才是一条**有检验力**的负控；若绑 `enabled: !onSky`，负控退化成"点了个禁用按钮" |
| 按钮与 Esc **共用** `root.returnToSky()` | 两个入口一条路径。否则"按钮返回"与"Esc 返回"迟早漂移成两套语义 |
| Esc 在**天空页不拦**，照旧透传 ActionRouter | 全 `src/` 无 `Key_Escape` 字面量、无 `.ui` 快捷键声明 ⇒ 引擎侧空闲；但保留给后续引擎动作，不抢 |
| 逐相位给 `nextDelayMs`（纯 UI 400ms / 写引擎 1200ms） | T19 的教训：驱动器的语义是"跑完本步**之后**等"。写引擎的相位必须等世界算完；纯 UI 相位不必空等 |
| `pageIndex` 只在 QML 定义一处，C++ 读窗口属性 | 复制一份到 C++ 就会退化成"加了页面忘改另一处"的运行时错位 |

---

## 5. DYN 读数：本轮两半都 3/3，但**不要读成"已修好"**

引擎侧 49.8 fps / 全程 665~677 帧；替身侧 3/3 PASS。跑时环境
`load avg 10.66`（复核时机器较忙）、`displaysleep=2`、`mdbulkimport` 正在索引。

**这不改变 T18 的定性**：`D1-C02`/`D1-C07` 量的是"窗口有没有在渲染"，它要求**窗口被暴露**，
而这个量在本机**受环境支配**——2026-09-24 同日下午曾出现替身 4/4 FAIL、随后两侧**全程 0 帧**
（`caffeinate -dims`/`-dimsu` 均无效）。完整定性见
`../2026-09-24-t18-locate-track/dyn-ab-baseline-vs-t18.txt` §3/§6。

处置沿用 T18/T19：跑 N 次如实报 `N/3`、**不跑"直到绿"**；替身对照也失败 ⇒ 该读数是
**「仪器测不到」**，不作为退化证据，**但也不改写成 INVALID**（原始 FAIL 照实保留）。
**不得**把 `N/3` 里的任一次 PASS 当成"零退化已证明"。

---

## 6. 文件清单

```
README.md                                   本文件
rc-summary.txt                              13 项 rc/N 汇总（机器可读）
returnuicheck-mac.txt                       ① T20 返回环 UI 端到端（11/11）
replaycheck-mac.txt                         ② T20 I-REP-02 全流程回放（10/10）
replaycheck-negctrl-nodedupe.txt            ② 反向对照：注掉去重 ⇒ RP-04b FAIL、9/10、rc=10
regression-timecheck.txt                    ③ T19 时间自检（14/14）
regression-timeuicheck.txt                  ③b T19 时间页 UI 端到端（13/13）
regression-locatecheck.txt                  ④ T18 定位/跟踪（14/14）
regression-locate-uicheck.txt               ④b T18 UI 端到端（8/8）
regression-clockcheck.txt                   ⑤ T16 时钟纯逻辑
regression-actioncheck.txt                  ⑥ T15/T16 命令通路 + 集成
regression-searchcheck.txt                  ⑦ T17 模型（27/27，含新增 SRC-05d）
regression-a2-metal.txt                     ⑧ A2 静态纹理
regression-dyn-engine-metal.txt             ⑨ DYN 引擎侧汇总（3/3）
regression-dyn-engine-metal-run1..3.txt     ⑨ 原始单次输出
regression-dyn-stub-metal.txt               ⑨b DYN 替身侧汇总（3/3，判别性对照）
regression-dyn-stub-metal-run1..3.txt       ⑨b 原始单次输出
regression-s3-stela3.txt                    ⑩ S3 旧宿主引擎集成
```

**读结果的纪律**：DYN 两行是 `N/3`；其余项 `rc` 非 0 即为该套判据失败。
`replaycheck-negctrl-nodedupe.txt` **是刻意保留的 FAIL 记录**（证明 RP-04b 有检验力），
不要当成本轮有失败项。
