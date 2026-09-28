# T20「返回」环 + I-REP-02 全流程回放（A4 第四个环 / A-alpha 出口测试）

日期：2026-09-28。
证据包：`docs/evidence/2026-09-28-t20-return-ring/`。
一键复跑：`tools/t20-verify.sh`（`core` / `regress` / `dyn N`）。

---

## 1. 任务与通过条件

A4 的通过条件是固定流程 **I-REP-02「开机→搜月球→定位→改时间→返回」全程可用**
（软件测试文档 §5.3）。T17 交付"搜月球"、T18 交付"定位"、T19 交付"改时间"，
**"返回"这一环此前是空的**，且它的语义从未被定义过（测试文档只写了一句话，
指导文档里根本没有）。

**通过条件**：

1. 返回语义有明确、可执行的**定义**，且落进代码与文档（不只写在计划里）；
2. 返回**可用**：按钮与键盘两条入口都能真的把用户带回天空视口页；
3. 返回**不改动任何状态**：时间、选中、跟踪一概保留（"返回"不是撤销）；
4. 返回**不是撤销**这一点必须**可判别**（有一条判据能把两者分开）；
5. 页面上没有"点哪儿都没反应"的死控件（按钮与 Esc 都必须真的生效）；
6. I-REP-02 **全流程可自动回放**，且末态用互相独立的量证明五个环都真的落地；
7. 全量回归**零退化**。

---

## 2. 设计

### 2.1 语义定案（2026-09-27，用户拍板）

**返回 = 切回天空视口页 + 状态全保留。**

不是撤销：不改时间、不取消选中、不解除跟踪。三条理由：

1. I-REP-02 是"改完时间**回来看**"的用户旅程，而 I-DYN-02 的判据
   （改日期后星空位置变化**可验证**）恰好落在返回后的天空页上
   —— 一条判据同时覆盖两条用例。
2. 旧版工具窗（`DateTimeDialog` 等）本来就是**瞬态**的：关掉即回到天空。
   返回 ≠ 回退到过去的状态。
3. 页面切换是**纯 UI 层**状态（`StackLayout`），引擎不知道有"页"这回事。

### 2.2 唯一实现点

```qml
// MainWindow.qml
function returnToSky() {
    stack.currentIndex = root.pageIndex["sky"]
}
```

「返回天空」按钮与 `Esc` 键**共用**这一个函数。两个入口一条路径 ——
否则"按钮返回"与"Esc 返回"迟早漂移成两套语义。

**为什么不走 ActionRouter/AppFacade**：`ActionRouter` 是为"引擎命令单点路由"存在的
（复用 `StelAction::matches()`，避免 700+ 既有键位双轨）。"切页"与引擎无关，
把它塞进命令层只会给那条铁律添一个假动作。**状态之所以"全保留"，正是因为这条路径
根本没碰引擎** —— 这是设计上的必然，不是巧合。

`Esc` 的拦截写在 `keySink`（T17 修好的那个可聚焦 `Item`）上：

- **非天空页**：拦下 → `returnToSky()` → `event.accepted = true`；
- **天空页**：**不拦**，照旧透传 `ActionRouter`（全 `src/` 无 `Key_Escape` 字面量、
  无 `.ui` 快捷键声明 ⇒ 引擎侧空闲，实测 `routeKey` 返回 false 无副作用；
  但保留给后续引擎动作，不抢）。

### 2.3 为什么这一环**只能**做 UI 层判据

"返回"的**全部实现**就是上面那一句 QML。C++ 侧没有任何可断言的对象 ——
没有引擎命令、没有 AppFacade 方法、没有状态字段。唯一的观测面是
**真实控件 + 真实事件 + `pageStack.currentIndex`**。

这正是 T15/T18/T19 反复踩的同一条线：**不接线就测不到**。所以本层的两套判据
（`RETURN_UI_CHECK` / `REPLAY_CHECK`）都从最外层注入：按 `objectName` 找真实控件、
向窗口投递真实鼠标/键盘事件、再反向读 QML 的真实属性。

### 2.4 逐相位给等待（T19 教训的直接落地）

T19 的血泪：驱动器的语义是"**跑完本步之后**等 `delayAfter` 毫秒"，
把"等引擎重算"的等待挂在**读取步**上 ⇒ 写入→读取实测间隔 0 ms。

T20 的驱动器因此**逐相位显式给 `nextDelayMs`**，不给一个统一的"够大"的数：

| 相位类型 | 等待 | 理由 |
|---|---|---|
| 纯 UI（点按钮、读 `currentIndex`） | 400 ms | 只需让 QML 布局 polish 与重绑定发生 |
| **写完引擎**（`+1 时`） | **1200 ms**（`kUiReturnWorldSettleMs`） | `getAltAzPosAuto` 依赖 `getJD()`，而它只在 `updateTime()` 里刷新 ⇒ 必须让帧跑过一轮 |
| 定位（1.5s 平滑移动） | 1800 ms | `autoMoveDuration` 默认 1.5s，要让"定位"真的走完 |

### 2.5 判据成对 + 一条判别性对照

| 角色 | 判据 | 为什么必须有 |
|---|---|---|
| **前置** | RT-03 状态非平凡 | 没有它，RT-05/06 的 PASS 可能只是"本来就什么都没有"（空转） |
| **配对①** | RT-04 页面确实切回去了 | 读 `pageStack.currentIndex` |
| **配对②** | RT-05（JD 位移 <1e-12）+ RT-06（tracking/选中保留） | 证明"状态全保留" |
| **负控** | RT-07 不碰时间只切页往返 → AltAz < 0.05° | 排除"切页本身就在动世界" |
| **判别性对照** | **RT-08 改 1 小时 → 返回 → AltAz > 5°** | **唯一能把"返回"与"撤销"分开的判据**：若返回被实现成恢复快照，RT-04/05/06/07 全绿而 RT-08 会红 |
| **负控** | RT-11 天空页上的 Esc 无副作用 | 证明 Esc 拦截没有过度 |

### 2.6 I-REP-02 回放怎么设计

**全程只投递真实鼠标事件**，不绕过任何一层：

```
开机(停在天空页) → 点「搜索天体」→ 搜索框写 "Moon" → 点「搜索」
→ 点「月球」那一行 → 点「定位并跟踪」→ 点「时间（T19）」
→ 点「+1 时」→ 点「返回天空」→ 断言末态
```

**末态断言四样互相独立的量**，因为"流程跑完了"不能由"最后一屏看着对"来证明：

| 量 | 判据 | 覆盖的环 |
|---|---|---|
| 页面在天空页 | RP-07 | 返回 |
| JD 确实被改了且保留 | RP-08 | 改时间 + 返回不是撤销 |
| 仍在跟踪月球 | RP-08 | 定位 + 返回不清选中 |
| 星空位置真的变了 | RP-09 | **I-DYN-02 的内核** |

---

## 3. 三个真实问题（回放首跑逐个抓到）

### 3.1 🔴 搜索结果**重复行**（真实缺陷，已修）

搜 "Moon" 出 5 条，其中 2 对是**同一个天体**（NGC 6781 两次、NGC 1647 两次）。

**根因**：`StelObjectModule::listMatchingObjects` 把**翻译名表**与**英文名表**
各枚举一遍（`objs << listAllObjects(false) << listAllObjects(true)`），
只要该天体有 ≥2 个名字含查询串就**各产出一条**；`StelObjectMgr` 只做拼接 +
按名称字典序重排，**不去重**。Sirius 更极端：原始 4 条全是同一个 `Star:HIP 32349 A`。

**修法**：`SearchResultsModel` 按 `stableId` 去重（保留字典序最前的一条）。
**不改引擎**：那是上游 `SearchDialog` 也在用的原语，改它会牵连旧界面；
而且"同一个 object 只该有一行"本来就是**列表模型**该保证的事。

**反向对照**：临时注掉去重 → 重建重跑 ⇒ `RP-04b` FAIL（5 行 → 3 个不同 stableId，
重复 2 条）、**9/10**、**rc=10**（`replaycheck-negctrl-nodedupe.txt`）。恢复后 10/10。

### 3.2 ⚠️ T17 的 `SRC-05c` **原口径在为缺陷背书**

原判据：`exactRows == min(exactRaw, 10)`。去重之后 `exactRows=1`、`exactRaw=4`
⇒ **去重后的正确值被判红**（`regression-searchcheck rc=10`）。

也就是说这条判据把"原始条数"当成了"唯一行数"—— 它一直在**为题重复行背书**，
所以 26 条全绿也抓不到重复行。**这正是"判据全绿 ≠ 行为正确"的又一例**。

修正后的口径（`SearchModelCheck.cpp`）：

```
SRC-05c 未超限时行数 = 去重后条数，上限不再额外截断
        （cap=10 → 1 行；cap=4（不截断）→ 1 行；引擎原始 4 条 ⇒ 去重掉 3 条）
SRC-05d 结果内无重复天体（1 行 → 1 个不同 stableId）
```

⇒ SEARCHCHECK 由 26/26 变为 **27/27**。T17 交付文档 §4.1 已加修正说明。

### 3.3 ⚠️ 「首行不是月球」= 排序策略缺口（**不是**本次判红）

搜 "Moon" 的**首行是 `Ghost of the Moon Nebula`**，月球在第 **1** 行（0 基）。

引擎按**名称字典序**排（`StelObjectMgr::listMatchingObjects` 最后一步 sort），
而"按相关度/类型分组/拼音排序"是 A4 里**已登记但尚未做**的一项
（计划文档「排序策略」）。

**处置**：I-REP-02 说的是"**搜月球**→定位"，**不是**"盲点第一条"。
所以回放**按名字定位月球那一行**，并把"月球在第 1 行、首行是 NGC 6781"
**照实打进证据**，不粉饰成"排序没问题"，也不据此把判据洗红/洗绿。

---

## 4. 验收

### 4.1 返回环 UI 端到端：`RETURNUICHECK` 11/11 PASS（rc=0）

| 判据 | 实测读数 |
|---|---|
| RT-01 锚点 6/6 且返回按钮可见 | 页索引 sky=1 search=2 time=3 |
| RT-02 点「时间（T19）」→ 切到工具页 | `currentIndex=3` |
| RT-03 前置状态非平凡 | tracking=true、选中=true、`stableId="Planet:Moon"` |
| RT-04 点「返回天空」→ 切回 | `currentIndex=1` |
| RT-05 返回后 JD 位移 | **0.000e+00** 天（<1e-12） |
| RT-06 跟踪/选中保留 | tracking=true；"Moon"→"Moon"；`Planet:Moon`→`Planet:Moon` |
| RT-07 负控：切页往返不动世界 | AltAz 变化 **0.0000°**（<0.05） |
| **RT-08 判别性对照：返回 ≠ 撤销** | 改 1 小时后返回 → **13.9181°**（>5.0） |
| RT-09 Esc 起点 | `currentIndex=2` |
| RT-10 真实 Esc → 切回 | 被受理=true；`currentIndex=1` |
| RT-11 负控：天空页 Esc 无副作用 | `currentIndex=1`（不变）；JD 位移 0.000e+00 |

### 4.2 I-REP-02 全流程回放：`REPLAYCHECK` 10/10 PASS（rc=0）

| 判据 | 实测读数 |
|---|---|
| RP-01 锚点 9/9 | —— |
| RP-02 开机态 = 天空页 | `currentIndex=1`；开机 JD=2461311.743954（时钟已暂停） |
| RP-03 搜月球 | 结果 **3 条** |
| **RP-04b 无重复天体** | 3 行 → 3 个不同 stableId，重复 0 |
| RP-04 点月球行 → 选中 | `displayName="Moon"` / `Planet:Moon` |
| RP-05 定位 | `tracking=true` / `Planet:Moon` |
| RP-06 改时间 | JD 位移 **0.041667** 天（= 1/24） |
| RP-07 返回·页面 | `currentIndex=1` |
| RP-08 返回·状态保留 | 位移 0.041667 天（未撤销）+ tracking=true + stableId 不变 |
| RP-09 末态·星空（I-DYN-02） | AltAz 变化 **13.9173°**（>5.0） |

### 4.3 回归：零退化

| 套件 | 读数 |
|---|---|
| TIMECHECK | 14/14 PASS |
| TIMEUICHECK | 13/13 PASS |
| LOCATECHECK | 14/14 PASS |
| LOCATE-UICHECK | 8/8 PASS |
| CLOCKCHECK | VERDICT=PASS |
| ACTIONCHECK | VERDICT=PASS（0 FAIL；**Esc 拦截与 AC-12 同址，必查**） |
| **SEARCHCHECK** | **27/27 PASS**（+SRC-05d，SRC-05c 口径已修正） |
| A2 静态纹理 | VERDICT=PASS |
| S3 旧宿主集成 | 8/8 PASS |

### 4.4 DYN 读数：本轮两半都 3/3，但**不要读成"已修好"**

引擎侧 49.8 fps / 全程 665~677 帧；替身侧 3/3 PASS。跑时 `load avg 10.66`、
`displaysleep=2`、`mdbulkimport` 正在索引。

**这改变不了 T18 的定性**（环境敏感/间歇）。处置沿用：跑 N 次报 `N/3`、
**不跑"直到绿"**；替身对照也失败 ⇒ 记"仪器测不到"，不作退化证据，
**但也不改写成 INVALID**。**不得**把任一次 PASS 当成"零退化已证明"。
零退化的依据是同口径 A/B + **替身路径不含本任务代码**（DYN 用 `startPage="sky"`
且不点任何按钮）。

---

## 5. 改动清单

| 文件 | 改动 |
|---|---|
| `src/ui/qml/MainWindow.qml` | 新增 `returnToSky()`（唯一实现点）+ `skyReturnButton`（始终可用）+ `pageStack` 三个页导航按钮的 `objectName` + `keySink` 拦 `Esc`（非天空页） |
| `src/ui/qml/SearchPage.qml` | 新增锚点 `searchQueryField` / `searchGoButton` / `searchResultList`（回放要真的"输入 + 点搜索 + 点结果行"） |
| `src/ui/main.cpp` | 新增 `RETURN_UI_CHECK`（11 条）与 `REPLAY_CHECK`（10 条）两套最外层注入判据；新增 `startPage` 分支、环境变量说明；`uiReturnSendKey`/`uiReturnAltAz`/`uiPageIndexOf` 等辅助 |
| **`src/app/SearchResultsModel.cpp`** | **按 `stableId` 去重**（修 3.1 的真实缺陷） |
| **`src/app/SearchModelCheck.cpp`** | **SRC-05c 口径修正 + 新增 SRC-05d**（修 3.2 的"判据为缺陷背书"） |
| `tools/t20-verify.sh` | 新增一键复跑（`core` / `regress` / `dyn N`） |
| `docs/T17_SEARCH_OBJECT_MODELS.zh_CN.md` | §4.1 加口径修正说明（26/26 → 27/27） |
| `docs/BUILD_RECORD.zh_CN.md` | 追加 T20 章 |
| `docs/evidence/README.md` | 新增证据目录索引行 |

---

## 6. 移交 A4（A-alpha 之后）

| 项 | 状态 |
|---|---|
| ~~搜索/信息页~~ | ✅ T17 |
| ~~定位与跟踪~~ | ✅ T18 |
| ~~改时间（×6 写入）~~ | ✅ T19 |
| ~~返回环~~ | ✅ **T20（本任务）** |
| **I-REP-02 全流程回放** | ✅ **T20 完成** —— A-alpha 出口测试可跑（10/10） |
| 排序策略（相关度 / 类型分组 / 拼音） | ⬜ **仍待做**。T20 实测暴露了它的必要性：搜 "Moon" 首行是 `Ghost of the Moon Nebula`，月球在第 1 行 |
| 显示历法 / MJD / 速率 GUI / 时区选择器 | ⬜ 仍待做 |
| QML 交互级测试（L2 Qt Quick Test） | 🟡 继续部分结清：T20 补了**结果行点击**（T18 移交清单里点名未覆盖的那条）、**页导航按钮**、**Esc 键**。仍未覆盖：输入法、滚轮、时间页视觉层 |
| 跟踪标志泄漏的**根治**（引擎侧） | ⬜ 仍待做：本层读合取真值只是绕开 |
| DYN 显示侧停摆的根因 | ⬜ 仍待做：已定性为**环境敏感**；倾向"窗口未被暴露"。**已试过且无效**：`caffeinate -dims`/`-dimsu` |
| 引擎 `listMatchingObjects` 不去重 | ⬜ 记录在案**不改**：本层已按 `stableId` 去重；上游 `SearchDialog` 用的是同一个原语，若将来要向上游报缺陷，本条是素材 |

---

## 7. 复现与两条读结果的纪律

```sh
cd /Users/ztuqfvy/qt_demo/stellarium_vulkan
./tools/t20-verify.sh core      # ① + ② + T15..T19 的 core 回归
./tools/t20-verify.sh regress   # A2/DYN/S3/T17 回归
./tools/t20-verify.sh dyn 3     # 只补跑 DYN（N 次）
./tools/t20-verify.sh           # 全部
```

**纪律一**：DYN 两行是 `N/3` 不是 `rc`；其余项 `rc` 非 0 即失败。
`replaycheck-negctrl-nodedupe.txt` 是**刻意保留的 FAIL 记录**（证明 RP-04b 有检验力），
不属于本轮失败项。

**纪律二**：跑 DYN 前确认机器空载、**会话未锁屏、显示器亮着**
（T13 踩过：`2026-09-23-sky-longrun-INVALID-screensaver`）。
