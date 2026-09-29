# T33 — 观察地点页（A-alpha 出口判据里唯一完全缺失的"必须"项）

> 任务：把 **A-alpha 出口判据**（测试文档 `2026-09-17-03` §8 第一条）里
> "`第 3 节标'必须'的 A-alpha 功能全部可操作：拖动/缩放/选中/跟踪、时间、**地点**、搜索、信息`"
> 中**唯一一行未做**的"地点"落地。
> 改动面：`src/app/LocationProbe.{hpp,cpp}`（T33-A 数据面探针）｜`src/app/LocationCheck.{hpp,cpp}`
> （T33-C 判据）｜`src/app/AppFacade.{hpp,cpp}`（地点写入面 + 两个负控开关）｜
> `src/ui/qml/LocationPage.qml`（**新页**）｜`src/ui/qml/MainWindow.qml`（挂页 + 导航按钮）｜
> `src/ui/main.cpp`（**纯仪器**：两段相位接线）｜`src/ui/CMakeLists.txt`｜`tools/t33-verify.sh`。
> 判据：新套件 `LOCATIONCHECK` **10 条**（LC-01..LC-08，其中 LC-03/LC-04 各带一条判别腿）。

---

## 1 为什么必须先探针、再写 UI

T24 的血泪：合流形态自 T10 起**翻译从未加载**（`getLocaleDir()` 三候选在 bundle 布局下全不命中），
天体名一直退回英文，**潜伏 14 个任务**才被逼出来 —— 因为在它之前**没有任何判据依赖翻译名**。

地点库是**同款风险**：它依赖 `data/base_locations.bin.gz`，加载路径走
`StelFileMgr::findFile`，而 `installDir` 由 `STELARIUM_DATA_ROOT`（默认 `"."`）决定
⇒ **依赖启动时的 cwd**。

⇒ 处置：`STELQUICK_LOC_PROBE=1` 作为**第一相位**（在写任何 UI 之前先跑）。
探针**只报读数、不下 PASS/FAIL**（刻意不打 `PASS` —— 免得有人把它当判据）。

四条结论（完整读数见 `docs/evidence/2026-09-29-t33-location/README.md`）：

| # | 结论 | 对产品的影响 |
|---|---|---|
| ① | 数据面**可用**，且**不依赖 cwd**：**33501 条** / 193 区域 / 496 时区名 | 可以做 |
| ② | `locationForString("Beijing")` **既不报错也不命中**，返回 `role='!'`、坐标全 0 的**无效地点**（`StelLocationMgr.cpp:784` 兜底） | 🔴 **对外一律走完整 ID**（`getID()` = `"name, region"`）；`findLocations` 返回的就是 ID 列表 |
| ③ | 引擎 `StelLocation::isValid()` **不校验经纬度范围**（`StelLocation.cpp:296` 只查 `role=='!'` 与"经纬不同时为 0"） | 🔴 **范围闸是应用层的责任**（`AppFacade::isAcceptable*`） |
| ④ | `moveObserverTo(loc, 0.0)` 走**瞬时**分支（`>0` 是 `SpaceShipObserver` 飞行动画，跨帧异步）；写后**时区联动**（`StelCore.cpp:1543-1547`） | 写入用 `duration=0`；只写地点，**不动 `flagUseCTZ`** |

## 2 T33-B 产品：地点页

`src/ui/qml/LocationPage.qml`（330 行），`MainWindow.qml` 挂到索引 4 + `navLocationButton`。
`STELQUICK_PAGE=location` 可直启该页。

四个区块：**搜索地点**（喂 `findLocations` → 完整 ID 列表 → 点一条 `setLocationById`）｜
**按坐标写入**（lat/lon/alt 三个 `TextField` → `setLocationByCoordinates`）｜
**当前地点只读卡片**（名称/ID/行星/经纬高/地点时区/引擎时区）｜**状态行**。

三条分工纪律（照 T17/T19 先例）：

- **命令走 `AppFacade`，本页不碰引擎**，也不做任何坐标换算。
- **状态一律看 token**（`lastLocationRefusal`），文案只是把 token 翻成人话。
  ⚠️ `lastLocationRefusal` 在 C++ 侧**刻意声明成 `Q_PROPERTY`**（不是 `Q_INVOKABLE`）：
  写在绑定里时，只有属性才拿得到字符串并**建立依赖**；方法读到的会是**函数对象**，
  与字符串比较**恒 false**。这是 T19 的血泪。
- **连续量轮询、不建绑定**：当前地点那几项全是 `Q_INVOKABLE`，**没有 NOTIFY 就没有依赖**，
  绑定第一次求值后再不重算。⇒ 500 ms `Timer` 刷新。
- **刻意不回填输入框**：回填会打断用户正在输入的数字。只回填只读卡片 ⇒ 于是也不需要
  `syncing` 守卫。

### 2.1 写入面：单一入口 + 范围闸 + 回读验证

```cpp
// 全工程只有这里为了"用户改地点"调用 moveObserverTo（同"单点键位路由"的理由）
const char *t33ApplyLocation(StelCore *core, const StelLocation &loc, ...)
{
    core->moveObserverTo(loc, 0.0);                 // 瞬时分支
    if (!t33ReadbackMatches(core, wantLat, wantLon, wantAlt))
        return "readback-mismatch";                 // 验一遍，而不是假设
    return nullptr;                                 // nullptr = 成功
}
```

- **范围闸在写入路径的最前**（`isAcceptableLatitude/Longitude/Altitude`）：
  拒越界、拒 NaN、拒 ±inf。`std::isfinite` 同时挡 NaN 与 ±inf —— 空输入经 QML 的
  `Number()` 变成 NaN，**这是真实的用户路径**，不只是理论边界。
- **拒绝必须无副作用**：直接返回，不触碰引擎任何状态（LC-05 断言 id 纹丝不动）。
- **回读验证（照 T22 时区那套）**：引擎是"值对象 + 观察者替换"，正常路径必落；
  但验一遍比假设好 —— 引擎若因行星切换等条件走了别的分支，这里能立刻发现，
  而不是向 UI **谎报"已切换"**。
- **按坐标写入时时区 iana 刻意留空** ⇒ 引擎 `setObserver` 的
  `!ianaTimeZone.isEmpty()` 条件跳过 ⇒ **不动当前时区**（保守且可逆；
  "按经度自动配时区"是另一个设计决定，先不做）。`LocationPage.qml` 里写明了这个取舍。

## 3 T33-C 判据：10 条，成对 + 判别对照 + 可验算的物理量

| ID | 断言 | 腿 |
|---|---|---|
| **LC-01** | 坐标范围**纯谓词**：±90/±180 边界**恰好放行**、91/181 恰好拒、NaN/±inf 拒 | 恒可跑，**不需要引擎** |
| **LC-02** | 读面非空 ∧ 经纬度在范围内 | 读面 |
| **LC-03a** | 写**跨时区**地点（Paris）⇒ (a) 回读 lat/lon/name 变成目标 ∧ (b) 引擎 `UTCOffset` 跟着变 | **成对**（(b) 是**独立于地点 API 的**引擎状态） |
| **LC-03b** | 写与 Paris **同时区**、不同经纬度的地点（Abbeville）⇒ (a) 回读确实变了 ∧ (b) `UTCOffset` **不变** | **判别腿** |
| **LC-04** | **天文学恒等式「北天极高度 = 观测者纬度」**，三个地点各测一次，\|Δ\| < 0.5° | **绝对腿**（可验算，对照**地点库目录纬度**） |
| **LC-04b** | 同一仪器在三个地点的读数差 ≈ 纬度差（Paris↔Beijing 8.95°、Paris↔Abbeville 1.25°） | **判别腿**（不依赖任何绝对参考） |
| **LC-05** | 非法值被拒 ∧ token 正确 ∧ **当前地点 ID 纹丝不动** | 无副作用 |
| **LC-06** | 不存在的 ID 被拒（`not-found`）∧ 同样无副作用 | 无副作用 |
| **LC-07** | 往返：写回**起始地点** ⇒ 经纬度回到起始值（容差 1e-3） | 往返 |
| **LC-08** | 计数：写入次数 / 拒绝次数与预期一致（4 / 4） | 计数 |

### 3.1 🔴 为什么"时区联动"必须选**跨时区**地点

起始地点（引擎默认配的是**绵阳**）与 Beijing **同属 `Asia/Shanghai`** ⇒ 拿它俩比时区是
**假绿**：即使联动整条断掉，写后时区也照样是 `Asia/Shanghai`。
LC-03a 因此**必须选 Paris**（`Europe/Paris`，`ΔUTCOffset = −6.00 h`）。

这是 TRAPS 第 4 条（孤立断言可假绿）在**选址**上的形态 —— 探针第一版就在这条腿上是假绿的
（目标原本选 Beijing），是探针自己的**自我纠正**。

## 4 🔴 本轮四条新血泪

### 4.1 `delayAfter` 是"跑完**本步**之后的等待"，不是"等多久再跑本步"

本轮**主线故障**是 LC-04/LC-04b **间歇红**：同一二进制跑一次 `9/10`、一次 `8/10`，
红的项每次都恰好差"上一步写的纬度"（10.2°）。一度被误诊成"矩阵滞后 / 产品缺陷"。

真根因在 `LocationCheck::run()` 的 `tick()`：

```cpp
s.body(ctx);                                                  // 先跑本步
QTimer::singleShot(ctx->nextDelay, ctx->app, [tick]() { (*tick)(); });   // 再等
```

把**写入步**的 `delayAfter` 写成 `0` ⇒ 回读步与写入步之间**只隔一个事件循环**
⇒ **谁先醒是竞态**：帧泵 tick 先到就读到新地点、回读步先到就读到**上一个地点**的变换矩阵。

**密集采样证据**（`mac/flaky-dense-sampling-250ms.txt`）：写 Beijing 之后
**250 ms 读到 50.161°**（= Abbeville 50.105）、**500 ms 才读到 39.988°**（= Beijing 39.908）。

**处置**（三层，缺一层都还会漏）：

1. 写入步一律给 `kSettleMs = 700 ms`（**不依赖"延迟放在哪一步"这个语义**）；
2. 凡是要读**变换栈**的读取步，相位开头一律先过**有界就绪门** `awaitFresh()`
   （主动跑事件循环、就绪即测、**超时明确判红**，照血泪第 10 条 DYN 先例）；
3. 就绪门的判据放在**相位开头** —— 放在判据之后会让重试重跑判据、计数虚高。

> 背景事实：`updateTransformMatrices()`（`StelCore.cpp:1072-1083`）由**渲染循环**每帧重建，
> **"写完地点"本身不触发重算**。⇒ 任何读几何量的判据都必须等帧。

### 4.2 仪器**不要用 Polaris 当"天极"**

第一版 LC-04 用"Polaris 高度 ≈ 观测者纬度"。**读数会抖**：Polaris 赤纬 **+89.26°**，
偏离天极 **0.74°** ⇒ 它与任一固定方向的夹角**随时角摆动**，峰峰值 **~1.5°**；
而"巴黎(48.85) vs 阿布维尔(50.11)"只差 **1.26°** ⇒ **分辨不出这一对**，判据会间歇红绿。

**正解 = 用当日天极**：`equinoxEquToAltAz((0,0,1), RefractionOff)` 归一化后取 `asin(z)`。

- 当日天极**严格落在自转轴上** ⇒ `Rz(−θ)` 作用在 z 轴上为**恒等** ⇒ **与恒星时、与经度无关**
  （见 `StelObserver::getRotAltAzToEquatorial`：经度只进 `Rz`、纬度只进 `Ry`）；
- 也没有 J2000↔当日 的**岁差偏置**；
- ⇒ 读数**精确等于纬度**（实测 **Δ = 0.000°**，三点全 0），容差能收到 **0.5°**，
  而三地点之间的判别间隙 **8.95°** ⇒ **判别力 ≈ 18 倍**。

**为什么这条能证明"写入落到了坐标变换栈"（而不只是落了结构体）**：
若矩阵陈旧/未重算 ⇒ 读数停在**上一个地点**的纬度上 ⇒ **必红**（负控 A 实测就是这个签名）。

### 4.3 🔴 对照量**不能**取 `facade->locationLatitude()`（最重要的一次假绿）

LC-04 最初写的是"仪器读数 vs `facade->locationLatitude()`"。
**两者同源**（都走 `core->getCurrentLocation()`）⇒ 写入 no-op 时**两边一起停在旧地点**、
**自洽通过** ⇒ 负控 D 下 LC-04 竟**仍然绿**。

**修法**：对照量换成 **地点库目录纬度** `locMgr().locationForString(id)`（数据文件）
—— 与 `position->currentLocation`（观察者结构体）**不同源**。
修后负控 D 报 `Paris |Δ|=17.386° / Abbeville 18.637° / Beijing 8.440°`，LC-04/LC-04b 双双真红。

### 4.4 🔴 判据**隐含依赖用户配置**：`flagUseCTZ`（**只有跨平台复验才能逼出来**）

**现象**：mac 侧正题 5 批全 `10/10`；**Windows 侧正题恒为 `9/10`，红项恒为 `LC-03a`**
（逐跑一致，不是抖动）：

```
LOCATIONCHECK:   ✗ LC-03a 写入跨时区地点生效（成对）：回读 id="Paris, Western Europe" lat=48.8534 ∧ UTCOffset 8.00 → 8.00（Δ=0.00 h）
```

`UTCOffset 8.00 → 8.00` —— **写巴黎时区没跟着变**（mac 侧是 `8.00 → 2.00`）。

**根因**（读源码得出，非推断）—— 时区联动有**两个**前提，我们只验了一个：

| # | 前提 | 出处 | mac | Windows |
|---|---|---|---|---|
| ① | 目标地点 `ianaTimeZone` **非空** | `StelCore.cpp:1543-1547` 条件 `!ianaTimeZone.isEmpty()` | ✅ | ✅ |
| ② | 引擎**未启用"自定义时区"** | 同处条件 `!getUseCustomTimeZone()` | ✅ `false` | 🔴 **`true`** |

`flagUseCTZ` 从哪来？**用户 config**：

```cpp
// StelCore.cpp:240-243
QString ctz = conf->value("localization/time_zone", "").toString();
if (!ctz.isEmpty()) setUseCustomTimeZone(true);   // ← 非空 ⇒ 打开
```

- **mac** 的 `~/Library/Application Support/Stellarium/config.ini` **没有** `localization/time_zone` ⇒ `flagUseCTZ=false`；
- **Windows** 的 `%APPDATA%\Stellarium\config.ini` 有 **`time_zone = Asia/Shanghai`** ⇒ `flagUseCTZ=true`。

而 `setObserver` 里的时区联动**带 `!getUseCustomTimeZone()` 守卫**是**刻意的**（用户在
UI 里选了自定义时区，就不该被写地点覆盖）⇒ **产品行为正确，判据的前提不成立**。

**为什么这条特别危险 —— 它同时制造一个假红和一个假绿**：

| 判据 | 语义 | 在 `flagUseCTZ=true` 下 |
|---|---|---|
| `LC-03a` | 写入生效 ⊇ `ΔUTCOffset ≠ 0` | 🔴 **假红**（联动被设计跳过） |
| `LC-03b` | **判别腿**：经纬变 ∧ 时区**不变** | 🟢 **假绿**（"不变"是被禁出来的，不是测出来的） |

⇒ **LC-03b 的绿是白送的**：它本该由 `LC-03a` 的联动成立来**衬托**，前提一塌就一起塌。
连带污染：负控 A `7/10`（多一条 LC-03a）、B `9/10`、C `7/10`（窗口对但多一条）；
**只有 D 不受影响**（D 不依赖时区腿）—— 两边红项**完全一致**（`LC-03a/03b/04/04b`），
这反过来**证明根因单一**。

**处置**（两层，缺一层都还会漏）：

1. **判据自己建立前提**：跑前读 `facade->useCustomTimeZone()`，为 `true` 就
   `setUseCustomTimeZone(false)`，并在**末尾（步骤 11）恢复**（血泪第 9 条）。
   它**不落盘**（`StelCore.cpp:1761-1765` 无 `immediateSave`）⇒ 还原即净。
   日志里**必须出现** `前提②：引擎 \`flagUseCTZ\` = ...` —— 这是**前提自证**。
2. **跑批脚本断言这一行**（mac `t33-verify.sh` 与 `wt33-suites.ps1` 各加一条）：
   没有它，`10/10` **不算数**（说明跑的是旧仪器）。

> **普适形态**（TRAPS 第 44 条）：**一条判据越"跨层"，它隐含的环境前提越多。**
> 「跨时区写入」这一条同时依赖 ①目标 iana 非空、②引擎未开自定义时区 —— 而 ②
> **只存在于用户 config 里**。**单平台全绿 ≠ 前提成立**：mac 那台恰好没那一行。
> ⇒ **凡"跑之前世界该是什么样"的假设，要么在判据里显式读出来并打印，要么把它建成前提。**
> 这也是**跨平台复验的真正价值**：它换的不是操作系统，是**一份不同的用户配置**。


## 5 四组负控：各自的红项**互不相同**

四条环境变量开关（照 `..._FORCE_FOCUSGATE_FAIL` 先例，**只用于证明判据承重**）：

| 组 | 开关 | 期望 | 实测 |
|---|---|---|---|
| **A** | `..._NODELAY=1` + `..._GATE_OFF=1`（复现原始间歇红） | **只许**"红项 ⊆ {LC-04, LC-04b}" ∧ "整批**至少复现一次**"（⚠️ **概率性**，见下表后注） | ✅ 四批：`5/5` / `5/5` / **`3/5`** / **`4/5`** 红（**逐位相同**的二进制）；**红项零越界**；复现时三地点天极高度角**全读成 48.853°**（= Paris，第一个写入的那个）⇒ 正是"读到上一个地点矩阵"的完整签名 |
| **B** | 只 `..._NODELAY=1`（门兜底） | `10/10` 且零红（⚠️ **不得**要求"就绪门探测次数 ≥ 1"，见下表后注） | ✅ `5/5` rc=0、`10/10`、零红；其中 **4/5 跑**门被唤醒（`探测 N 次`）、**1/5 跑探测 0 次也算正确** ⇒ **门是承重件，但承重的证据是 A↔B 这一对，不是任何单跑的探测次数** |
| **C** | `..._RANGE_GATE_OFF=1`（摘**写入路径**的闸调用点，**谓词本体不动**） | `8/10`，红**恰好** LC-05 / LC-08，**LC-01 仍绿** | ✅ rc=10 ⇒ **"规则正确"与"规则被调用"两腿分离的实证** |
| **D** | `..._WRITE_NOOP=1`（写入 no-op，**对外照旧报成功**） | `6/10`，红**恰好** LC-03a / LC-03b / LC-04 / LC-04b | ✅ rc=10 ⇒ **活引擎腿四处承重** |

> **负控 D 的附带读数**：LC-07（往返）在 no-op 下**仍会绿** —— 起点是绵阳、写回的也是绵阳，
> "位置纹丝不动"这条**对往返判据是不可见的**。⇒ LC-07 **单独没有判别力**，
> 判别力来自 LC-03a/03b 的对照。这条如实登记在 `t33ApplyLocation` 的注释里。

### 5.1 🔴 两条**口径被实测推翻并收紧**（写下来是为了不再犯第二次）

> 第一版脚本（和 `LocationCheck.hpp` 的注释）把 A 写成"**每次都红** `8/10`"、
> 把 B 写成"**门探测次数必须 ≥ 1**"。**两条都被实测打掉**，
> 而且是**用逐位相同的二进制**打掉的 —— 所以不是"环境抖"，是口径本身错了。

**① 负控 A 是概率性的。** 它复现的**缺陷本身就是一个竞态**（回读步与写入步只隔一个
事件循环）⇒ 帧泵**有时**抢先 tick，那一跑就合法地读到**新**值、**全绿**。
三批实测（二进制 md5 一字不变）：`5/5` 红、`5/5` 红、**`3/5`** 红。
⇒ 口径改为 **"红项只许 ⊆ {LC-04, LC-04b} ∧ 整批至少复现一次"**；
零复现记 **INCONCLUSIVE**（**不洗成 PASS、也不判红** —— 硬币落反不是产品缺陷）。

**② 负控 B 不得要求探测次数 ≥ 1。** 写入延迟摘掉后，帧泵可能**恰好**在两步之间 tick
⇒ 门**第一次看就是新值** ⇒ **合法地探测 0 次**（实测该跑照样 `10/10`）。
⇒ 探测次数**降级为信息行**。
⚠️ 那么"门是不是承重件"靠什么？**靠 A↔B 这一对**：A（门关）红、B（门开）绿。
**不是**任何单跑的探测次数。

> 这两条的普适形态：**当负控复现的是一个竞态时，不能把"每次都红"写成判据**；
> 也**不能**把"承担修复的那个机制"的**内部计数**写成判据 —— 该计数本身可能因同一竞态而合法为 0。

## 6 读数

### 6.1 macOS（Metal + MoltenVK）

一键复跑：`tools/t33-verify.sh all 5`（环境口径与 `t17..t32` 一致：MoltenVK + Metal RHI，**刻意不换**）。

**第六轮（定稿轮，含 §4.4 的前提修补）读数 —— `FAILED=0` / `SCRIPT_RC=0`，全绿**
（归档 `mac/suite-run6-authoritative/`）：

| 段落 | 读数 |
|---|---|
| **正题** `LOCATIONCHECK` ×5 | **`pos-pass=5 env-skip=0 bad=0`**；每跑 `判据 10/10` + `VERDICT=PASS`；**就绪门 3 行**（`上限 3000 ms`）+ **目录纬度 1 行** + **前提②自证 1 行**齐全；残留进程 **0** |
| 负控 A ×5 | 红 **5/5** 跑，红项**恒为** LC-04/LC-04b（**零越界**）⇒ **OK**（第 ⑥ 轮运气全红；run4/run5 曾 4/5 —— **概率性**，见 §4.4） |
| 负控 B ×5 | **`5/5`** rc=0 ∧ `10/10` ∧ 零红（门被唤醒 5/5 跑）⇒ **OK** |
| 负控 C | rc=10 ∧ `8/10` ∧ 红**恰好** LC-05/LC-08 ∧ **LC-01 仍绿** ⇒ OK |
| 负控 D | rc=10 ∧ `6/10` ∧ 红**恰好** LC-03a/03b/04/04b ⇒ OK |
| 探针 `LOCPROBE` | `VERDICT=DONE`；地点库 **33501** 条 / 区域 193 / 时区名 496 |
| 相邻回归 ×9 | `time / returnui / search / action / locate / locate-ui / replay / clock / timeui` 全 `rc=0` |
| `INTERACTCHECK` | rc=0，**18/18**，窗口已激活 |
| **`S3` 旧宿主自检** | rc=0，`A3-C01..C08` 全 PASS，**`VERDICT=PASS 8/8`** |
| `A2`（Vulkan 后端） | rc=0 |
| DYN 双路 | `engine 5/5` + `test 5/5`，`producer-readback OK` |

**绝对腿与前提的自证行**（`mac/suite-run6-authoritative/loccheck-mac-run1.txt`）：

```
LOCATIONCHECK:     前提②：引擎 `flagUseCTZ` = false ⇒ 时区联动路径开放
LOCATIONCHECK:     目录纬度：Paris 48.8534° / Abbeville 50.1052° / Beijing 39.9075°
LOCATIONCHECK:   ✓ LC-04 可验算物理量（天极高度 = 观测者纬度）：观测点 |Δ| vs **目录纬度**——Paris 0.000° / Abbeville 0.000° / Beijing 0.000°（容差 0.50°）
LOCATIONCHECK:     就绪门[Paris]：等 0 ms / 探测 0 次（上限 3000 ms）⇒ 天极高度角 48.853°（目标纬度 48.853°，容差 0.50°）
```

**六批历史读数**（run1–run5 的二进制是 `39582584 / 28feadbf…`；**run6 是打了前提补丁的
`39585192 / e1f61e28…`**）：

| 批 | 正题 | 负控 A | B | C | D | 套件连跑 |
|---|---|---|---|---|---|---|
| run1 | 5/5 | **5/5** 红 | 5/5 | 8/10 | 6/10 | ❌ `REPLAYCHECK` `RP-04`（控件 `207×0`、就绪门超时）|
| run2 | 5/5 | **5/5** 红 | 5/5 | 8/10 | 6/10 | ✅ 全绿（`suite-run2/`）|
| run3 | 5/5 | **3/5** 红 | 5/5 | 8/10 | 6/10 | ❌ `RETURN-UICHECK` `RT-08` |
| run4 | 5/5 | **4/5** 红 | 5/5 | 8/10 | 6/10 | ✅ 全绿（`suite-run4-authoritative/`）|
| run5 | 5/5 | **4/5** 红 | 5/5 | 8/10 | 6/10 | ✅ 全绿（`suite-run5-authoritative/`）|
| **run6** | **5/5** | **5/5** 红 | 5/5 | 8/10 | 6/10 | ✅ 全绿（`suite-run6-authoritative/`）|

> **run1 / run3 那两处红是环境项（CPU 饥饿），不是 T33 的回归**：定性见
> `BUILD_RECORD.zh_CN.md` T33 章 §7.1 与 `mac/replaycheck-load/README.md`
> —— 隔离 ×8 全绿、人造负载（6×`yes`，load 5.30）复现 ⇒ **唯一变量是 CPU 饥饿**；
> 且案发用的是**逐位相同**的二进制 ⇒ 与 T33 无因果。
> **不做 A/B 归因**（案发率 ~1/9，统计功效不足），如实登记为**未归因开放项**。

### 6.2 Windows（原生 Vulkan，**不是** Metal/MoltenVK）

一键：`schtasks /it` 投递 `tools/windows/wt33-launch.ps1`（内部注入 `T33_PROBE_EXPECT`）→
`C:\temp\wt33-suites.ps1`；跑完**立刻** `schtasks /delete`。产物归档 `windows/`。

**修复后（含 §4.4 前提补丁）读数 —— 全绿**（`windows/t33w-suites/SUMMARY.txt` 尾行为
`[done]`，**无 `FATAL`** ⇒ 脚本自身 `$script:bad == 0`）：

| 段落 | 读数 |
|---|---|
| **正题** `LOCATIONCHECK` ×5 | **`pos-pass=5 env-skip=0 of 5`**；每跑 `rc=0 ∧ judge=10/10 ∧ 零红`，且 `gates=3(3) ∧ catlat=1(1) ∧ ctz=2(>=1)` ⇒ **仪器自证齐全** |
| 负控 A ×2 | **INCONCLUSIVE**（竞态两次都没复现）—— 见下"负控加跑" |
| 负控 B ×2 | `2/2` rc=0 ∧ `10/10` ∧ 零红 ⇒ **OK** |
| 负控 C | rc=10 ∧ `8/10` ∧ 红**恰好** `LC-05,LC-08` ∧ **LC01green=True** ⇒ OK |
| 负控 D | rc=10 ∧ `6/10` ∧ 红**恰好** `LC-03a,LC-03b,LC-04,LC-04b` ⇒ OK |
| 探针 `LOCPROBE` | `VERDICT=DONE`、`catalogScaleLines=1`（**33501 条**，与 mac 同值） |
| 相邻回归 ×10 | `clock / action / search / locate / locate-ui / time / returnui / replay / timeui / a2-vulkan` 全 `rc=0` |
| **`S3` 旧宿主自检** | `rc=0`、`verdictPASS=True` ⇒ **OK** |
| `INTERACTCHECK` ×5 | **`5/5 pos-pass`**：`rc=0 ∧ armed=1 ∧ full18=True ∧ crosses=0`（`schtasks /it` 下窗口确实拿到前台） |
| `INTERACT` 探针（强制失活） | `rc=10`，红项 `[IT-05,IT-06,IT-13,IT-16,IT-17,IT-18]` **= 预期表**（`wt33-launch.ps1` 里写死的平台口径）⇒ OK |
| DYN 双路 | `engine 3/3` + `test 3/3`，`producer-readback` 六次全 `OK` |

**修复前对照 —— 唯一偏差就是 `flagUseCTZ`**（归档 `windows/before-fix/`，作为根因分析的现场）：

| 段落 | 修复前 | 修复后 |
|---|---|---|
| 正题 ×5 | 🔴 恒 `9/10`，红项恒为 `LC-03a`（`ΔUTCOffset=0.00 h`） | ✅ `10/10` |
| 负控 A ×2 | 🔴 `7/10`，红 `[LC-03a,LC-04,LC-04b]`（**越界**） | ✅ 红项零越界（或 INCONCLUSIVE） |
| 负控 B ×2 | 🔴 `9/10`，红 `[LC-03a]` | ✅ `10/10` |
| 负控 C | 🔴 `7/10`，红 `[LC-03a,LC-05,LC-08]`（窗口对、被污染） | ✅ `8/10`，红恰好 `LC-05,LC-08` |
| 负控 D | ✅ `6/10`，红 `[LC-03a,LC-03b,LC-04,LC-04b]` | ✅ **一字不变** |
| 回归 10 套件 / `S3` / `LOCPROBE` / `INTERACT` / DYN | ✅ 全绿 | ✅ 全绿 |

> **为什么这张对照表能"反证根因单一"**：D **不依赖时区腿** ⇒ 修复前后**红项完全一致**；
> A/B/C 的偏差**每次都恰好多一条 `LC-03a`**。⇒ 偏差不是"Windows 不一样"，
> 而是**一条前提被禁用的连锁反应**。

**负控加跑**（`windows/negctl-extra/`，`-NegOnly -NegARuns 5 -NegBRuns 3 -Tag t33wneg`；
整批里 A 只跑 2 次不够判，故单跑一轮）：

| 组 | 读数 | 判读 |
|---|---|---|
| **A ×5** | **`INCONCLUSIVE`** —— 5 跑**全部全绿**（`10/10`、零红） | ⚠️ **竞态在 Windows 上没被复现**（mac 侧同一开关 5 跑里 3–5 跑红）⇒ 本批**无判别性证据**。脚本如实记 INCONCLUSIVE：**不洗成 PASS、也不判红**（硬币落反不是产品缺陷）|
| B ×3 | `3/3` rc=0 ∧ `10/10` ∧ 零红（`gateLines=3`） | OK |
| C | rc=10 ∧ `8/10` ∧ 红**恰好** `LC-05,LC-08` ∧ `LC01green=True` | OK |
| D | rc=10 ∧ `6/10` ∧ 红**恰好** `LC-03a,LC-03b,LC-04,LC-04b` | OK |

⇒ **"判据承重"在 Windows 侧由 C / D 两组独立证明**（红项集合与 mac **逐项一致**）；
**A↔B 那一对只在 macOS 上拿到**。⇒ 平台差异，如实登记为开放项（↗ §8 移交表）：
Windows 上帧泵几乎总能在回读步之前把变换栈刷新，**"门关掉"也照样读到新值** ——
**不是判据坏了，是那台机器快到让竞态窗口消失**。

> ⚠️ 顺带一条**仪器自身**的小坑：这次轮询用的探针写成
> `Select-String -Pattern '\\[done\\]' -SimpleMatch -Quiet` ⇒ 转义后成了字面 `\[done\]`，
> **永远不命中**（`-SimpleMatch` 不再解转义）⇒ 明明 `[done]` 已经落了还在轮询。
> 教训：**"判据说没有"之前先证明探针会命中**（血泪第 32/33 条）。



## 7 与既有文档的关系

- **T19 / T22**（`docs/T19_*`, `docs/T22_*`）：`Q_PROPERTY` vs `Q_INVOKABLE` 的处置、
  写入后**回读验证**的范式，本轮照抄。
- **T21 / T24**（`docs/T21_*`, `docs/T24_*`）："纯逻辑腿 + 活引擎腿"两条腿的分工，
  本轮由 LC-01（纯谓词）与 LC-05（闸被调用）实证。
- **T25 / T27 / T29 / T32**（交互判据）：**有界就绪门**、**判别腿**、**负控开关**
  三个范式沿用；T33 把它们第一次用在**几何量**（坐标变换栈）上。

## 8 未覆盖与移交

| 项 | 说明 |
|---|---|
| **负控 A 在 Windows 上不复现**（本轮新发现） | `LOC_NODELAY+GATE_OFF` 在 Windows 跑 5 次全绿 ⇒ `A↔B` 那一对只在 macOS 拿到，"门是承重件"的**跨平台**证据缺一条腿。承重性在 Windows 上由 **C/D** 独立证明（红项与 mac 逐项一致）。**要做**：① 查明为何 Windows 不复现（帧泵节拍 / 事件循环粒度）；② 或换一条**与竞态无关**的"门关掉必红"构造。单独立项 |
| **`findLocations` 排序** | `QMap::values()` 是 key **字典序**，"Beijing" 的首条是 `Beijing Ancient Observatory` 而不是 `Beijing, Eastern Asia`（空格 `0x20` < 逗号 `0x2C`）。照 T21 先例，**完全匹配应优先**。本轮**不做**，与 T21 同款处置单独立项 |
| **按经度自动配时区** | 按坐标写入时 iana 留空 ⇒ 时区不动。是刻意的保守取舍，若要做需单独立项 + 判据 |
| **地点库脱离源码树的分发** | 探针实测：cwd=`/tmp` 时靠**编译期** `STELLARIUM_SOURCE_DIR` 兜底命中。A 阶段从源码树跑，**不阻塞**；打包分发时另需处理 |
| 工具栏 / 时间链路脱钩 / `LOC-04 (b)` 帧延迟量化 / 捏合后首击 / DYN 停摆根因 | A4 剩余，见计划文档 §10.2 |

---

_证据：`docs/evidence/2026-09-29-t33-location/`（`README.md` 为 T33-A 探针结论 +
`mac/` 全量读数，含 `flaky-dense-sampling-250ms.txt` 这份"作案现场"）。_
