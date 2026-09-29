# T33 观察地点页 — 证据（2026-09-29）

> 本目录 = **T33-A 地点数据面探针**（本文 §1–§7）+ **T33-C 写入面判据的全量读数**
> （本文 §8，文件在 `mac/`）。断言（成对 + 判别对照 + 非法值无副作用）在 `LocationCheck`。

---

## 0. T33-C 全量读数索引（`mac/`）

| 文件 / 目录 | 内容 |
|---|---|
| `mac/loccheck-mac-run1..5.txt` | **正题** ×5（`STELQUICK_LOC_CHECK=1`）⇒ 期望 `判据 10/10` + `PASS` + rc=0 |
| `mac/loccheck-mac-n5.txt` | 上面 5 跑的合并读数 |
| `mac/negctl-A-nodelay-gateoff-run1..5.txt` | **负控 A**（`LOC_NODELAY=1` + `LOC_GATE_OFF=1`）⇒ ⚠️ **概率性**：只断言"红项 ⊆ {LC-04, LC-04b}" ∧ "整批至少复现一次" |
| `mac/negctl-B-nodelay-gate-on-run1..5.txt` | **负控 B**（只 `LOC_NODELAY=1`）⇒ 期望 `10/10` 全绿（门兜住竞态）；⚠️ **不**要求"探测次数 ≥1" |
| `mac/negctl-C-range-gate-off.txt` | **负控 C**（摘写入路径的范围闸调用点）⇒ 期望 `8/10`、红恰好 `LC-05/LC-08`，**LC-01 仍绿** |
| `mac/negctl-D-write-noop.txt` | **负控 D**（写入 no-op、对外照旧报成功）⇒ 期望 `6/10`、红恰好 `LC-03a/03b/04/04b` |
| `mac/negctl-mac.txt` | 四组负控的合并读数 |
| `mac/probe-loc-data-mac.txt` | **探针**（`STELQUICK_LOC_PROBE=1`）⇒ `VERDICT=DONE` |
| `mac/flaky-dense-sampling-250ms.txt` | 🔴 **作案现场**：写 Beijing 后 **250 ms 读 50.161°**（= Abbeville）、**500 ms 才读 39.988°**（= Beijing）⇒ 证实"回读步读到**上一个地点**的变换矩阵" |
| `mac/replaycheck-load/` | `REPLAYCHECK` 就绪门在**负载下超时**的三组对照（见该目录 `README.md`） |
| `mac/suite-run1-contaminated/` | 第 1 轮套件的 `rc-summary` + 那次 `REPLAYCHECK` 红的原始日志 |
| `mac/suite-run2/` | 第 2 轮套件的 `rc-summary` + 全部回归日志 |
| `mac/suite-run3/` | 第 3 轮（`RETURN-UICHECK RT-08` 红） |
| `mac/suite-run4-authoritative/` | 第 4 轮（全绿） |
| **`mac/suite-run5-authoritative/`** | 第 5 轮（全绿）：`rc-summary` + `full-run.log` + 正题 ×5 + `regression-s3-stela3.txt.gz` + 三个回归日志（`FAILED=0`） |
| **`mac/suite-run6-authoritative/`** | **定稿轮**（打了 §0.1 前提补丁后的二进制 `39585192 / e1f61e28…`）：`FAILED=0` / `SCRIPT_RC=0` 全绿 |
| **`windows/`** | Windows 原生 Vulkan 整批**复验（修复后）**：`wt33-*` 脚本 + `SUMMARY.txt` + 逐套件 `.out.txt` + 构建 manifest |
| **`windows/before-fix/`** | 🔴 **修复前**的整批 —— **根因分析的现场**（见 §0.1） |
| `..` 的 `docs/T33_LOCATION_PAGE.zh_CN.md` | 交付文档（含三条新血泪） |

> **`regression-s3-stela3.txt`（mac，`496 KB`）** = 旧宿主 `stellarium` 的进程内自检：
> `A3-C01..C08` 全 PASS、**`VERDICT=PASS 8/8`**。**为什么要跑它**：原来靠"旧宿主 md5
> 逐位相同"来免跑，本轮该论据被推翻（md5 与源码异同**无因果**）⇒ 改成**真的跑一遍**。


**为什么 `LC-04` 的仪器是"当日天极"而不是 Polaris**（血泪，见交付文档 §4.2）：
Polaris 偏离天极 **0.74°**、与固定方向的夹角随时角摆动峰峰值 **~1.5°**，
而"巴黎(48.85) vs 阿布维尔(50.11)"只差 **1.26°** ⇒ **分辨不出**。
当日天极 `equinoxEquToAltAz((0,0,1))` 的 `sin` 精确等于纬度（实测 **Δ=0.000°**）。

**为什么 `LC-04` 的对照量必须换来源**（最要命的一次假绿，交付文档 §4.3）：
`facade->locationLatitude()` 与 `core->getCurrentLocation()` **同源** ⇒
写入 no-op 时两边**一起停在旧地点、自洽通过**（负控 D 下 LC-04 竟仍绿）。
改取**地点库目录纬度** `locMgr().locationForString(id)` 后才真红（Paris |Δ|=17.386°）。

---

## 0.1 🔴 T33-D 的收获：判据**隐含依赖用户配置**（`windows/before-fix/` 就是现场）

**Windows 侧正题恒为 `9/10`、红项恒为 `LC-03a`**（逐跑一致），mac 侧五批全 `10/10`。
读数 `UTCOffset 8.00 → 8.00（Δ=0.00 h）` ⇒ 写巴黎时区没跟着变。

根因：时区联动依赖**两个**前提，我们只验了 ①：

| # | 前提 | 出处 | mac | Windows |
|---|---|---|---|---|
| ① | 目标地点 `ianaTimeZone` 非空 | `StelCore.cpp:1543-1547` | ✅ | ✅ |
| ② | 引擎**未启用"自定义时区"** | 同处 `!getUseCustomTimeZone()` | ✅ `false` | 🔴 **`true`** |

② 来自**用户 config**：`localization/time_zone` 非空 ⇒ `setUseCustomTimeZone(true)`
（`StelCore.cpp:240-243`）。mac 那台**没有**这一行，Windows 那台有 `time_zone = Asia/Shanghai`。
守卫是**刻意的** ⇒ 产品正确、**判据前提不成立**。

**它同时造一个假红和一个假绿**：`LC-03a` **假红**；`LC-03b`（判别腿"时区**不变**"）**假绿**
—— 那个"不变"是被禁出来的。连带 A `7/10`、B `9/10`、C `7/10`；
**只有 D 不受影响**（不依赖时区腿，两边红项完全一致 `LC-03a/03b/04/04b`）
⇒ 反证**根因单一**。

**处置**：判据自己**读→强制→恢复**前提（步骤 1 / 步骤 11；`setUseCustomTimeZone`
**不落盘** ⇒ 还原即净），日志打印**前提自证行** `前提②：引擎 \`flagUseCTZ\` = ...`，
跑批脚本**断言这一行存在**（否则 `10/10` 不算数）。

| 目录 | 内容 |
|---|---|
| `windows/before-fix/` | 🔴 **修复前**的整批（正题 `9/10`×5、A `7/10`、B `9/10`、C `7/10`、**D `6/10` ✅**、`S3 OK`、`LOCPROBE OK`）—— 这份"现场"是根因分析的依据 |
| `windows/`（修复后） | 修复后的整批读数 |

---

# 附：T33-A 地点数据面探针

> **探针只报读数、不下 PASS/FAIL。** 它是"查事实"，不是"立断言"。

## 为什么要先探针再写 UI

T24 的血泪：合流 bundle 布局下**翻译从未加载**（`getLocaleDir` 三候选全不命中），
潜伏 **14 个任务**才被逼出来 —— 因为在它之前**没有任何判据依赖翻译名**。

地点库是**同款风险**：它依赖 `data/base_locations.bin.gz`，加载路径走
`StelFileMgr::findFile`。⇒ **先证数据面可用，再写 UI。**

## 四条结论（macOS + Metal，2026-09-29 20:2x）

### ① 数据面**可用**，且**不依赖 cwd**

| 项 | 读数 |
|---|---|
| 地点库规模 | **33501 条** / 193 区域 / 496 时区名（其中 448 个含 `/` 的 IANA 形态） |
| `findFile("data/base_locations.bin.gz")` | 命中 |
| `findFile("data/ssystem_major.ini")`（installDir 判据文件） | 命中 |

**cwd 对照（同一二进制）**：

| cwd | `installDir` | 地点库 |
|---|---|---|
| 仓库根 | `"."`（走 `STELLARIUM_DATA_ROOT` 默认值） | 33501 条 |
| `/tmp` | `/Users/ztuqfvy/qt_demo/stellarium_vulkan`（走**编译期** `STELLARIUM_SOURCE_DIR`） | 33501 条 |

⇒ 两条路径都能加载。**但注意**：源码目录兜底意味着**脱离源码树分发时另需处理**
（当前 A 阶段从源码树跑，不阻塞）。

### ② `locationForString(单名)` **不报错也不命中**，返回 `role='!'` 的无效地点

```
locationForString("Beijing") ⇒ name="Beijing" lat=0.0000 lon=0.0000 iana="" role='!'   ← 无效
locationForString("北京")     ⇒ name="北京"     lat=0.0000 lon=0.0000 iana="" role='!'   ← 无效
locationForString("Paris, Western Europe") ⇒ lat=48.8534 lon=2.3488 role='C'            ← 有效
```

库内 **Beijing 的真 key** = `getID()` = `"Beijing, Eastern Asia"`（`name + ", " + region`）。
用完整 ID 再查 ⇒ 命中（39.9075 / 116.3972 / Asia/Shanghai）。

⇒ **对外一律走 ID**；`setLocationById` 用 `isValid()` 当"找没找到"的闸，
不拿用户输入直接喂 `locationForString`。
（兜底分支出处：`StelLocationMgr.cpp:784`"All attempts to decode have failed."）

### ③ 引擎 `StelLocation::isValid()` **不校验经纬度范围**

实测：`lat=91 ⇒ true` / `lon=200 ⇒ true` / `(0,0) ⇒ true` / `role='!' ⇒ false`。

⇒ 出处 `StelLocation.cpp:296`：只查 `role=='!'` 与"经纬不同时为 0"。
**范围闸是应用层的责任**，`AppFacade::setLocationByCoordinates` 里做了。

### ④ `moveObserverTo(loc, 0.0)` 走**瞬时**分支；写后**时区联动**

`duration > 0` 是 `SpaceShipObserver` **飞行动画**（跨帧异步），`= 0` 才瞬时
（`StelCore.cpp:1550-1566`）。写后时区联动出处：`setObserver` 里
`setCurrentTimeZone(obs->getCurrentLocation().ianaTimeZone)`（`StelCore.cpp:1543-1547`），
条件是 `!getUseCustomTimeZone() && !ianaTimeZone.isEmpty()`。

实测（绵阳 → 巴黎）：

```
P4 写前 name="Mianyang (Sichuan)" lat=31.4678 lon=104.6817 iana="Asia/Shanghai" UTCOffset=+8h
P4 目标 name="Paris"              lat=48.8534 lon=2.3488   iana="Europe/Paris"
P6 写后 name="Paris"              lat=48.8534 lon=2.3488   iana="Europe/Paris"  UTCOffset=+2h
   Δlat=17.3856°  Δlon=-102.3329°  ΔUTCOffset=-6.0000 h
   判别腿：写后时区已从 "Asia/Shanghai" 变为 "Europe/Paris" ⇒ 联动确实发生
```

⚠️ **第一版探针在这条腿上是假绿的**：目标原本选 Beijing，而写前地点是绵阳 ——
**同属 Asia/Shanghai** ⇒ 即使联动整条断掉，写后时区也照样是 `Asia/Shanghai`。
换成跨时区目标（Paris）后判别腿才成立。这是 TRAPS 第 4 条（孤立断言可假绿）
在**选址**上的具体形态，也是探针自身的一次自我纠正。

## AppFacade 地点面读数（`probe-appfacade-repo.txt`）

走**公共 API**（产品实际路径），证接线不是死代码：

```
P8 读：name="Paris" id="Paris, Western Europe" lat=48.8534 lon=2.3488 alt=42.0 地点时区="Europe/Paris"
P8 非法值：lat=91 ⇒ 被拒（invalid-latitude）/ lon=200 ⇒ 被拒（invalid-longitude）/ alt=1e6 ⇒ 被拒（invalid-altitude）
P8 无副作用：三次拒绝后 id "Paris, Western Europe" ⇒ "Paris, Western Europe"（纹丝不动）
P8 ad-hoc 写东京(35.68,139.77,40m) ⇒ 成功 token=ok ⇒ 回读 lat=35.6800 lon=139.7700 alt=40.0
   地点时区=""（ad-hoc 留空）引擎时区="Europe/Paris" ⇒ **时区不动**（设计预期）
P8 findLocations("Beijing",5) ⇒ 3 条：Beijing Ancient Observatory, Eastern Asia | Beijing, Eastern Asia | Daxing (Beijing), Eastern Asia
P8 setLocationById("Beijing Ancient Observatory, Eastern Asia") ⇒ 成功 ⇒ 地点时区="Asia/Shanghai" 引擎时区="Asia/Shanghai"
P8 setLocationById(不存在的 ID) ⇒ 被拒 token=not-found
P8 计数：写入 2 次 / 拒绝 4 次
```

⚠️ **`findLocations` 的排序待做**：`"Beijing"` 的第一条是 `Beijing Ancient Observatory`
而不是 `Beijing, Eastern Asia` —— 因为 `getAll()` 是 `QMap::values()`，按 key
**字典序**（空格 `' '`(0x20) < 逗号 `','`(0x2C)）。照 T21 先例，完全匹配应优先。
留给 T33-B/C。

## 一条环境偶发（照实记录，不洗成 PASS）

同一二进制、同一 cwd（`/tmp`）连续两次跑，**第一次只输出 8 行**（停在
`runtimeApi=Metal backendOk=1`，未进 `StelMainView` 初始化），第二次正常 32 行；
随后重复 2 次均 32 行。定性：**偶发**，疑似紧接上一实例退出时的资源竞争。
⇒ T33 的批跑脚本要按"探针行数"判定是否拿到完整读数，不齐则重试。
证据：`mac/probe-cwd-tmp-first-run-8lines.txt`（8 行那份）。

## 文件清单

| 文件 | 内容 |
|---|---|
| `mac/probe-cwd-repo-v1.txt` | 探针第一版（仓库根 cwd，全量 189 行日志） |
| `mac/probe-cwd-tmp-first-run-8lines.txt` | 上面那条**偶发**的 8 行读数 |
| `mac/probe-cwd-tmp.txt` | 探针第二版（cwd=`/tmp`，含 `installDir` 走源码目录的证据） |
| `mac/probe-appfacade-repo.txt` | 探针第三版（含 P8 AppFacade 地点面读数） |
