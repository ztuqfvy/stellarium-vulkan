# T22 证据包 —— 时间页收尾（显示历法 / MJD / 速率 GUI / 时区选择器）

- **交付文档**：`docs/T22_TIME_PAGE_COMPLETE.zh_CN.md`
- **一键复跑**：`tools/t22-verify.sh all`（或 `core` / `regress` / `dyn 3`）
- **口径**：macOS + Metal RHI（**与 T17–T21 刻意一致**，便于跨任务比较读数）
- 本轮**没有动 `src/core/`**（全是 `src/app` + `src/ui`）⇒ 引擎库不需要全量重编

---

## 1. 判据总览（2026-09-28 单次连贯运行，全绿）

| 套件 | 判据 | 读数 |
|---|---|---|
| `timecheck-mac`（**T22 正题**） | 21（新增 `TC-15..TC-21`） | **21/21 PASS** rc=0 |
| `timeuicheck-mac`（**T22 正题**） | 19（新增 `UI-12..UI-16`） | **19/19 PASS** rc=0 |
| `returnuicheck-mac`（T20 回归，本轮被门修过） | 11 | **11/11 PASS** rc=0 |
| `replaycheck-mac`（T20 全流程回放） | 11 | **11/11 PASS** rc=0 |
| `regression-searchcheck-core` / `-searchcheck` | SRC-01..11 | rc=0 / rc=0 |
| `regression-locatecheck` / `-locate-uicheck` | 14 / 8 | rc=0 / rc=0 |
| `regression-clockcheck` / `-actioncheck` | —— | rc=0 / rc=0 |
| `regression-a2-metal` / `regression-s3-stela3` | —— | rc=0 / rc=0 |
| `regression-dyn-engine-metal` | D1-C02/C07 ×3 | **3/3 PASS** |
| `regression-dyn-stub-metal`（判别性对照） | 同上 ×3 | **3/3 PASS** |

> `rc-summary.txt` 是机器汇总。**判"零退化"只看正跑文件**，不看负控。

## 2. 判据明细（T22 新增的 12 条）

### 2.1 纯逻辑 + 引擎一致腿（`timecheck-mac`，`TC-15..TC-21`）

- `TC-15` **MJD 与 JD 同源**：恒等式残差 `0.00e+00 天`（<1e-12）；写 `MJD=61311.250000`
  落地 `true`、JD 残差 `0.00e+00 天`（<1e-9）。
  另附 `TC-15-note`：同一时刻引擎快照 `core->getMJDay()=61311.968894` vs 本层读数同值
  —— **只记录不判红**（正常帧序下本就相等，硬造差异的判据是摆设；它的价值是
  "哪天 `setJD` 不再同步 `JD.first`，这里先显形"）。
- `TC-16` **换历边界成对**：JD `2299160.5 → julian`、`2299161.0 → gregorian`。
  `TC-16-note` 记下"时钟已还原到现代时刻"——否则下面 ③ 会假红（见 §4 第 2 条）。
- `TC-17` **非法时区被拒**：`No/Such_Zone_xyz` → 落地 `false`。
- `TC-18` **时区偏移成对**：`Africa/Abidjan → 0.0000 h`、`Antarctica/Casey → 8.0000 h`
  （两者必须相差 8 小时；两个都是**无夏令时**区 ⇒ 与"跑在哪一天"无关）。
  `TC-18-note`：复原为 `Asia/Shanghai`。
- `TC-19` **速率仪表接在实况上（成对）**：引擎设 `4.16666667e-02` → facade 读数一致；
  再走引擎**动作**（不经 `AppFacade`，等价用户按 `L`）→ 引擎 `4.16666667e-01`
  且**读数跟上**。
- `TC-20` **速率文本换算**：`x1.0` / `x3600（1.00 时/秒）` / `x86400（1.00 天/秒）`
  （照抄 `StelGuiItems.cpp:885-907` 的四档跳档）。
- `TC-21` **方向 token**：`forward` / `stopped` / `backward`。`TC-21-note`：速率已复原。

### 2.2 UI 端到端腿（`timeuicheck-mac`，`UI-12..UI-16`）

- `UI-12` 新控件锚点 **7/7** 可寻且可见（MJD 行/历法行/速率行/方向行/时区组合框/
  自定义时区勾选/速率「+」按钮）。
- `UI-13` **MJD 投影行** `61677.659034` vs C++ 侧 `61677.659034`（差 `6.87e-08 天`），
  且与 JD 行同源（`JD=2461678.159034`）。
- `UI-14a/b` **历法行两值**：跳到 `JD=2299160.5` 报 `儒略历`，现代时刻报 `格里高利历`
  —— 同一控件两个值 ⇒ 是**活的投影**，不是常量。
- `UI-15` **真实点击**「+」按钮（`@(911.5,376.0)`，走 `ActionRouter` 透传
  `actionIncrease_Time_Speed`）：引擎 `1.000000e-01 → 1.000000e+00`（确实变了=true），
  速率行 `x8640（2.40 时/秒）` → `x86400（1.00 天/秒）`（跟着变了=true）。
- `UI-16` **时区组合框回填**：`setTimeZoneId("Africa/Abidjan")` 后，组合框当前项
  与 C++ 侧 `timeZoneId()` 一致（命令写入 → 轮询把控件带回实况）。

## 3. 反向对照（证明判据有检验力）★

把 `AppFacade::timeRate()` 改回**读本地缓存** `m_timeRate`，重建、跑同一批：

| 文件 | 读数 |
|---|---|
| `timecheck-negctrl-cachedrate.txt` | **18/21**、**rc=10**；`TC-19`/`TC-20`/`TC-21` 三条红 |
| `timeuicheck-negctrl-cachedrate.txt` | **18/19**、**rc=10**；`UI-15` 红 |

`TC-19` 原样读数：引擎 `4.16666667e-02 → 4.16666667e-01`（**变了 10 倍**），
而 facade 读数**恒为 1.0**。`UI-15` 更彻底：读缓存时连"引擎变了"都看不到
—— 仪表就是那个瞎了的仪表。

> 这两份**不是 FAIL 样本，是判据有效性的证据**。跑完已恢复（与备份逐字节一致、
> `grep -rn "m_timeRate;" src/app/AppFacade.cpp` 反查无残留）。

## 4. 记录在案、但**不**判红的三条

1. `TC-15-note` 引擎 `getMJDay()` 与真源一致 —— 正常帧序下本该相等，见 §2.1。
2. `TC-18` 首跑假红（两个时区同偏移 `6.9789 h`）—— 根因是 `TC-16` 把时钟留在 **1582 年**，
   而引擎 `getUTCOffset` 在 `JD < TZ_ERA_BEGINNING`（1847-12-01）时**不看时区名**、
   改按经度算 LMST。修法是 `TC-16` 测完立刻还原现代时刻。**通用教训**：
   判据若自己改了环境，必须**在同一步内恢复**，否则后面测出的是**假红**。
3. **搜索召回**问题（候选截断在排序之前，属引擎侧）—— 记录在案，本轮不改。

## 5. 全量首跑的两套红：**间歇**，已定性并堵住假红 ★★

`returnuicheck` / `replaycheck` 首跑红（`RT-08`、`RP-04/05/06/08/09`），
几何读数 `207×0`（健康时 `934×341`）。完整过程与全部原始样本见：

- **`flaky-layout-207x0/`** —— A/B/C/D 四组样本 + 探针读数。
  决定性对照：**同一份源码、同尺寸的二进制**先 3/3 FAIL、后 6/6 PASS
  ⇒ **间歇，不是本轮引入的退化**。
- **`layout-gate/`** —— 处置（有界"布局就绪门"）的**两条腿**证据：
  正跑 8+8 次 `门触发=0`（不改正常路径）+ 负控（强制恒 `false` ⇒ 两套都 rc=10、
  报文明说"仪器没接上……也**不作 PASS**"）。

**不做的事**：不跑"直到绿"、不把 FAIL 洗成 PASS、不改判据口径。
**做了的事**：改判据**协议**（点击前有界等布局就绪，上限 2.5s，超时明确判红）。

## 6. 文件清单

| 文件 | 说明 |
|---|---|
| `timecheck-mac.txt` / `timeuicheck-mac.txt` | T22 正题（21 + 19） |
| `*-negctrl-cachedrate.txt` | 反向对照（★证据，非 FAIL） |
| `returnuicheck-mac.txt` / `replaycheck-mac.txt` | T20 回归（本轮被门修过） |
| `regression-*.txt` | 其余回归（search / locate / clock / action / a2 / s3） |
| `regression-dyn-engine-metal*.txt` / `-stub-metal*.txt` | DYN 正跑 + 替身对照，各 3 次留档 |
| `flaky-layout-207x0/` | 首跑两套红的定性过程与原始样本（见 §5） |
| `layout-gate/` | 布局就绪门的正跑 + 负控（见 §5） |
| `rc-summary.txt` | 机器汇总 |

## 7. 读结果的三条纪律

1. `*-negctrl-cachedrate.txt` **不是 FAIL，是证据**。
2. `DYN` 的 `N/3` 必须连**替身对照**一起读；任一次绿都不是"零退化已证明"；
   替身也败 ⇒ 记"仪器测不到"，不洗成 PASS、也不改写成 INVALID。
3. `returnuicheck` / `replaycheck` 若报 `207×0`（或"…尚未就绪"），先看有没有
   `门触发` 读数；门是**有界**的，超时才判红 ⇒ 说明确实是仪器没接上，
   **照实归档，不改判据、不重跑到绿**。
