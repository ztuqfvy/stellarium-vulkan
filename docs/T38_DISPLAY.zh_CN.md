# T38 — 显示参数（亮度/星等 · 视场 · 投影）

> 状态：**已收口**（macOS，Metal/MoltenVK，2026-09-30）
> 自检：`STELQUICK_DISPLAY_CHECK=1` → `DISPLAYCHECK:`（**14 条**：DP-00 .. DP-11）
> 探针：`STELQUICK_DISPLAY_PROBE=1` → `DISPLAYPROBE:`（只报读数，不下 PASS/FAIL）
> 一键复跑：`tools/t38-verify.sh`
> 证据：`docs/evidence/2026-09-30-t38-display/mac/`

---

## 1 开工前的这一格

`A-1.0 范围表`原文：

```
|亮度/星等、视场、投影、主题/夜视、高 DPI|基础|必须|天空夜视效果仍由旧后处理负责，避免叠加两次|
```

- T37 做掉「主题/夜视」（结论：合流形态下引擎旧后处理**物理不可达**，落点改 Qt Quick 侧效果层）；
- **T38 做前三项**（高 DPI 归 T39）；
- 「必须（基础级）」意味着：不是"有按钮就行"，而是**写进去要真的生效、越界要真的挡住、非法值要真的拒绝**。

三项在合流形态**从未验证过**。按项目纪律「**先探针再写 UI**」（T24/T33/T34/T35/T37 一脉相承），
先做命令面探针，把"引擎到底怎么反应"变成读数，再决定产品落点与判据口径。

---

## 2 T38-A 探针：7 条实测结论

产物：`docs/evidence/2026-09-30-t38-display/mac/probe-display-mac.txt`（51 行读数）。
结论写进了 `src/app/DisplayProbe.hpp` 的头注（**判据设计直接引用它**，不是凭印象）。

| # | 读数 | 后果 |
|---|---|---|
| ① | `StelSkyDrawer` 的四个数值 setter **一个都不夹取**（写 4/3/5/12.5 原样落库，还顺手 `immediateSave` 落盘） | **范围闸必须在产品侧**（`AppFacade`） |
| ② | `setFov(double)` **自带 `qBound(min,max)` 夹取**，而 **maxFov 是投影的函数** | `maxFieldOfView` 必须是**活属性**，滑块上限跟着投影走 |
| ③ | `setCurrentProjectionTypeKey("乱码")` **不报错**：`qWarning` 后静默落到 `ProjectionStereographic` **并落盘** | 投影写入**必须白名单闸** |
| ④ | 🔴 `getLimitMagnitude()` **不是**用户设定值 —— 它是引擎按大气/光污染/瞳孔适应**算出的有效限制**（白天实测 **-4.44**，与 `customStarMagLimit=12.5` 无关） | 判据**只读写** `getCustomStarMagnitudeLimit()` + `getFlagStarMagnitudeLimit()`。生效点是星表人工截断（`ZoneArray.cpp:449`：`cutoffMag = customStarMagLimit * 1000`） |
| ⑤ | 星等/亮度的**视觉**效应只在星星可见时存在 | 判据布场**必须大气置关**（否则"本来就没有星星"会被当成"星星消失了"——T37 同款血泪） |
| ⑥ | 静置 1.5s 内 rel/fov/proj 的 NOTIFY 增量 **0/0/0** | 这些量**不是每帧变的连续量** ⇒ 滑块**可以安全绑定**，不会重算风暴 |
| ⑦ | 星等步进动作已注册（中文"增加/降低恒星极限星等"），trigger 后 `customStarMagLimit ±0.1` | 与 T34「命令路径单点 = 引擎 action id」一致，产品侧不复制语义 |

②的细节读数（`maxFov` 随投影）：

```
Perspective 120 ｜ Stereographic 235 ｜ Fisheye 360 ｜ EqualArea 360
Mercator 270 ｜ Miller 270 ｜ Orthographic 180 ｜ CylinderFill 180
Hammer / Mollweide / Sinusoidal / Cylinder 185
```

③的细节读数：**非法 key 不报错**，而且 `immediateSave("projection/type")` 会把兜底值**落盘** ——
只靠"检查返回值"是挡不住的（引擎返回 void），必须自己拿清单比对。

原版 `ViewDialog` 的控件范围（照抄语义与范围，不重新发明）：

| 控件 | 范围 | 步进 | 语义 |
|---|---|---|---|
| `starRelativeScaleDoubleSpinBox` | [0.25, 5.0] | 0.05 | 相对星点亮度 |
| `starScaleRadiusDoubleSpinBox` | [0.05, 10.0] | 0.05 | 星点尺寸 |
| `starLimitMagnitudeDoubleSpinBox` | [0, 21] | 0.1 | 极限星等（**配一个 CheckBox**） |
| `starLimitMagnitudeCheckBox` | — | — | 连 `flagStarMagnitudeLimit` |

---

## 3 产品实现（T38-B）

### 3.1 `AppFacade` 显示面

- **10 个 `Q_PROPERTY`**：`starRelativeScale` / `starAbsoluteScale` / `starMagnitudeLimit` /
  `starMagnitudeLimitEnabled` / `lightPollutionLuminance` / `minFieldOfView` / `maxFieldOfView` /
  `projectionTypeKey` / `projectionTypeName` / `projectionTypeKeys`，
  全挂同一个 NOTIFY `displayParametersChanged`（五个数值项是**固定小集合**、设置页一屏全见）。
- **8 个 `CONSTANT` 范围常量**（`starRelativeScaleMin/Max` 等）⇒ QML 滑块直接绑，范围不在 QML 里硬编码。
- **范围闸**：`clampInto()` 先夹取再写引擎，并记 `lastDisplayRefusal`（`ok` / `not-engine` /
  `out-of-range` / `unknown-projection`）。**不静默吞掉**。
- **白名单闸**：`setProjectionTypeKey()` 拿 `projectionTypeKeys()` 比对，不在清单里 ⇒ 拒绝且**不写引擎**。
- **转发**：`ensureDisplayParamsForwarding()` 懒连 6 个引擎 NOTIFY（5 个 `StelSkyDrawer` +
  `currentProjectionTypeKeyChanged` + `currentFovChanged` → 兼发 `fieldOfViewChanged`）。
- **`setFieldOfViewNow()` 走 `setFov`**，不走 `zoomTo(deg, 0.4s)` —— 后者是 T28 捏合/滚轮拍的动画，
  滑块拖一次发一次动画会打架。

### 3.2 `DisplayPage.qml` + 路由

- 4 个 GroupBox：星点亮度（相对 / 尺寸 / 夜空背景亮度〔对数滑块 `log(1e-6)..log(1e-1)`〕）、
  极限星等（CheckBox + Slider，Slider `enabled` 绑 flag）、视场（Slider **上限绑 `maxFieldOfView`**）、
  投影（`Repeater` over `projectionTypeKeys`）。
- 投影按钮**刻意不 `checkable`**：AbstractButton 的 `checkable` 会命令式写 `checked`，**销毁绑定**
  （T32/T34 同族）—— 高亮改由 `highlighted: current` 驱动。
- `MainWindow.qml`：`pageIndex` 加 `"display": 5`，StackLayout 加 `DisplayPage {}`；
  `Toolbar.qml` 加 `navDisplayButton`（"显示"，既有 6 个导航按钮 objectName 未动）。

### 3.3 🔴 产品缺陷：`projectionTypeKeys` 从 `Q_INVOKABLE` 改成 `Q_PROPERTY`

**这是本轮最有价值的一条**，由判据 DP-06 抓出来（首轮 **0/12**）：

- 合流形态的加载顺序是**先 `engine.load()`（QML 起来）→ 后 `LiveSkyRuntime::boot()`（引擎起来）**；
- QML 里 `model: appFacade.projectionTypeKeys()` 是**函数式绑定**，**不读任何属性** ⇒ 没有依赖
  ⇒ **只求值一次**，而那一次正好在引擎起来之前（`isInitialized()` 为假 ⇒ 返空表）
  ⇒ **12 个投影按钮一个都不出现，且永不自愈**；
- 真机同路径同病（不只是自检里空）。

治法：`Q_PROPERTY(QStringList projectionTypeKeys READ ... NOTIFY displayParametersChanged)`，
并由 `ensureDisplayParamsForwarding()` 末尾**主动 `emit` 一次"开机唤醒"** ——
绑定的依赖才算接上（同一处 emit 顺手把 5 个数值项从开机时的 `-1` 刷成真值）。

⚠️ 这次 emit **刻意放在 `FWD_OFF` 旁路之前**：负控 B 只该打红 DP-07（订阅断了），
不该连 DP-06（开机唤醒）一起打红 —— 两个负控各管一件事。

同族但**不同病**：T19 那个是"`Q_INVOKABLE` 在绑定里读到**函数对象**、与字符串比较恒 false"
（**类型**坑）；这个是**求值时机**坑 —— 属性类型、返回类型全对，结果照样空。

---

## 4 🔴 首轮 FAIL 反哺：三条都不是"调参"能修的

首跑 **10/13**。三条 FAIL 全部指向真实缺陷，没有一条能靠改门槛糊过去。

### 4.1 DP-06 0/12 → 产品缺陷（见 §3.3）

### 4.2 DP-11 红 → "参考帧"基准被污染

数据面 DP-01..DP-03 改过的 `absoluteStarScale` / `lightPollutionLuminance` /
`flag+customStarMagLimit` **没还原**就去抓"参考帧 A'" ⇒ 参考帧被光污染顶成
`nonBlack 0.8301 → 1.0000` ⇒ DP-09 / DP-10 / DP-11 **全比在一个假基准上**。

治法：`Ctx::restoreAllDisplayParams()`，**抓参考帧之前强制全量还原**（不是只还原"上一步改的那一项"）。

> 教训：数据面步骤"顺手改过的每一个量"都是后续帧级判据的**隐含前提**（陷阱 44 的同族）。

### 4.3 DP-08 红且**逐位相同** → `delayAfter` 挂错步

`delayAfter` 的语义是"**跑完本步之后等**"（陷阱 41/69）。首轮把延迟挂到了**读步**：

```text
写步 {0}   → 读步 {400}      ← 错：写后 0ms 就抓帧 ⇒ 拿到上一次的帧缓冲（哈希都相同）
写步 {400} → 读步 {...}      ← 对
```

症状极具辨识度：**差异 0、哈希相同**。已挪到写步（陷阱 41/69 的同族第三次）。

---

## 5 判据 `DISPLAYCHECK`（DP-00 .. DP-11，14 条）

见 `src/app/DisplayCheck.hpp` 头注（判据清单、环境前提、负控、退出码）。

**布场**：冻结仿真（帧静定）+ **大气置关**（让星星可见 —— 星等判据的前提）。收尾全部还原。

| 分组 | 判据 | 内容 |
|---|---|---|
| 数据面 | DP-00 | **噪声底门**：冻结后同状态两帧必须静定（口径有效性前置） |
| | DP-01 | **状态面往返**：5 项 façade 写 → **引擎 getter 独立回读**（陷阱 43：不复述自己的读法） |
| | DP-02 | **范围闸**：写 99 ⇒ 引擎收到**上界** ∧ refusal=`out-of-range`；写合法值 ⇒ `ok` |
| | DP-03 | **投影 12 key 往返 + maxFov 活属性**：12/12 一致 ∧ maxFov 取到 **≥3 个不同值** |
| | DP-04 | **投影白名单闸**：写乱码 ⇒ 拒绝 ∧ 引擎 key **不变** ∧ refusal=`unknown-projection` |
| | DP-05 | **`lastDisplayRefusal` 必须是 `Q_PROPERTY` ∧ 有 NOTIFY**（元对象层断言） |
| UI 腿 | DP-06 | **控件齐备**：7 个单件 + 12 个 `proj_*`（**视觉树递归** —— 陷阱 45：Repeater delegate 的 QObject 父链是空的，`findChild` 扫不到） |
| | DP-07 | **绑定腿**：引擎侧改 `relativeStarScale` ⇒ UI 滑块 value 跟随 |
| 帧级 | DP-08 | **视场生效**：改视场 ⇒ 上游帧差异 ≥ 门（成对） |
| | DP-08b | **判别对照（承重）**：同一套取帧+比较方法对 `actionShow_Atmosphere` 必须测出显著差异 ⇒ 证明方法活着、"测不出东西"不成立 |
| | DP-09 | **投影生效**：切 `ProjectionFisheye` ⇒ 上游帧显著变化（成对） |
| | DP-10a | **静置对照（承重）**：状态不变时连抓两帧，**亮像素计数**漂移 ≤ 门 ⇒ 新引入的方向量在噪声下稳定 |
| | DP-10b | **星等截断生效（成对 · 方向量）**：极限星等 LOW（下界 0）↔ HIGH（15）亮像素数之差 ≥ 门 |
| | DP-11 | **复原**：引擎 getter 全回初值 ∧ 复原帧 vs 参考帧 A' 低于噪声容差 |

### 5.1 为什么 DP-10 用"亮像素方向量"而不是"全图差异占比"

星点像素**占比天然小**，全图占比会被地面/背景/地平线稀释 ⇒ 用"≥1%"当门既可能误判 FAIL、
也可能靠别的东西凑够 1% 变成假绿。改成 **LOW↔HIGH 成对方向量**后：

- **不依赖"初值长什么样"**（不拿参考帧当对照）；
- 方向单一：只有星表截断能解释这个差；
- 并且配了 **DP-10a 静置对照**，先证明这个**新引入的量**在"什么都不改"时是稳的。

---

## 6 负控（证明判据承重；红项集合**两两不同**）

| 组 | 开关 | 作用 | 预期红项 |
|---|---|---|---|
| A | `STELQUICK_DISPLAY_GATE_OFF=1` | 旁路 `clampInto()` 与投影白名单闸 | **恰好** `[DP-02, DP-04]` |
| B | `STELQUICK_DISPLAY_FWD_OFF=1` | 断引擎→façade 的**订阅**（不拦开机唤醒） | **恰好** `[DP-07]` |

负控 A 的读数**自带证据**（不只"红了"）：

```text
[FAIL] DP-02 范围闸：写 99 ⇒ 引擎收到上界 99（不是 99）∧ refusal=ok
[FAIL] DP-04 白名单闸：写 accep ted=true ∧ 引擎 key ProjectionCylinderFill→ProjectionStereographic（**变了**）
```

这两行同时把 T38-A 探针的①③**在判据层又坐实了一遍**：引擎 setter 真不夹取、非法 key 真会静默兜底。

负控 B 的读数：`UI 滑块 value=1`（停在开机唤醒那一刻的值）⇒ DP-07 的绑定腿确实承重。

---

## 7 实测结果（macOS，Metal/MoltenVK，2026-09-30）

> 数值以 `docs/evidence/2026-09-30-t38-display/mac/rc-summary.txt` 与各 `displaycheck-run*.txt` 为准。

### 7.1 正题 `DISPLAYCHECK` ×3

`rc=0` / `判据 14/14` / 红项空。run1 全文关键读数：

```text
初值：relativeStarScale=1 absoluteStarScale=1 lightPollutionLuminance=0.000143438
      customStarMagLimit=0 flagStarMagnitudeLimit=false fov=60 projection=ProjectionStereographic
布场：已冻结仿真 + 大气置关（原值 开）

DP-00  噪声底：725/921600（0.079%）最大通道差=1 平均通道差=0.0008
DP-02  写 99 ⇒ 引擎收到上界 5（不是 99）∧ refusal=out-of-range；写回合法值 ⇒ ok
DP-03  12/12 key 往返一致 ∧ maxFov 取到 6 个不同值（120/180/185/235/270/360）
DP-04  写乱码 ⇒ accepted=false ∧ 引擎 key **未变** ∧ refusal=unknown-projection
DP-06  单件 7/7；proj_* 共 12 个；displayProjectionRepeater.count=12
DP-07  引擎侧改 3.25 ⇒ UI 滑块 value=3.25
参考帧A'（全量还原后）：非黑=0.8301 亮像素(≥96)=2460
DP-08  视场 60→30：差异 596541/921600（64.729%）
DP-08b 判别对照（actionShow_Atmosphere）：差异 100.000%  ⇒ 方法活着
DP-09  投影 → Fisheye：差异 187275/921600（20.321%）
DP-10a 静置对照：亮像素 0→0（漂移 0 ≤ 门 100）
DP-10b LOW(0)↔HIGH(15)：亮像素 0↔2416（差 2416 ≥ 门 500）
DP-11  复原帧 vs 参考帧A'：差异像素 0（逐位相同）
```

两条**有信息**的读数：

- `DP-08b` 判别对照 **100%**（全图 921600 像素全变）—— 说明这套取帧+比较方法对这个场景的灵敏度极高，
  DP-08 的 64.7%、DP-09 的 20.3% 都是**真信号**而不是"勉强过门"；
- `DP-10 HIGH 帧 vs 参考帧A'` **逐位相同**（哈希相同）—— 说明**初值本身就是"全开"**
  （`flag=false` 时走引擎自算的有效限制，比 15 更松）。这条读数反过来证明 LOW 的截断是真的：
  同一个 `customStarMagLimit` 从 0 拉到 15，亮像素从 **0** 回到 **2416**。

### 7.2 负控

| 组 | 跑次 | rc | 判据 | 红项 |
|---|---|---|---|---|
| A `GATE_OFF` | ×2 | 10 | 12/14 | `[DP-02, DP-04]`（两跑一致） |
| B `FWD_OFF` | ×2 | 10 | 13/14 | `[DP-07]`（两跑一致） |

三组红项集合：`∅` / `{DP-02,DP-04}` / `{DP-07}` —— **两两不同**。

### 7.3 相邻回归

十四套件（time / returnui / search / action / locate / locate-ui / replay / clock /
timeui / location / toolbar / timelink / config / **night**）+ INTERACTCHECK 18/18 +
A2（Metal）+ DYN 双路 ×3 + S3 旧宿主 —— 全 `rc=0`，**`SCRIPT-RC=0 FAILED=0`**（脚本层 3/0）。

### 7.4 定稿轮环境与产物指纹

```text
二进制：40637720 B  2026-09-30 19:01  md5=30f4e313eb382615792ca2b1866e0176
批次环境：up 5 days, load averages 6.57 5.35 4.69
环境注记：⚠️ Spotlight 批量索引（mdbulkimport）在跑 —— T37-X2 撕裂帧 / T37-X3 引导挂死的
          实测负载毒源。本批**仍全绿**；脚本带环境注记 + 看门狗，ENV 劣化跑出来的结果
          不许洗 PASS。
```

证据目录 `docs/evidence/2026-09-30-t38-display/mac/` 共 **34 项 / 1.1 MB**
（探针读数 + 正题 ×3 + 负控 A×2 + 负控 B×2 + 14 套件 + INTERACTCHECK + A2 + DYN 双路 ×3
+ S3 + `rc-summary.txt`）。本任务**不落帧**（判据只看读数），所以不需要像 T37 那样裁剪。

---

## 8 判据标定与口径修正

### 8.1 🔴 噪声容差只能拿**幅度**，不能拿**面积**

| 读数点 | 差异面积 | 最大通道差 |
|---|---|---|
| DP-00（刚冻结、静止天空，两帧相隔 300ms） | **0.079%** | 1 |
| "对照量还原核对"（大气开关来回后同一状态） | **0.557%** | 2 |

**面积涨了 7 倍，幅度只从 1 到 2。** ⇒ 冻结下的亚 LSB 抖动**面积会随场景状态变**。

治法：`belowNoise()` 只看 `maxDelta ≤ kNoiseMaxDelta(2)`；面积只留给 DP-00 当
"帧到底静没静下来"的粗门（`kNoiseGateRatio = 0.02`）。把面积当必要条件会造出**假红**。

### 8.2 ⚠️ DP-10 撤掉一条**自造的伪命题**

第一版 DP-10 除了"LOW↔HIGH 亮像素差 ≥ 门"，还额外要求"**HIGH 帧 vs 参考帧 A' 差异占比 ≥ 1%**"。
实测 HIGH 与参考帧 **逐位相同** ⇒ 必红。

问题不在门槛，在**命题**：那条附加条件实在验证"15 与初值不同"，而**我从来没有声称过这件事**
（初值本来就是"全开"）。判据只能验被声称的命题，多验一条就是自己制造红项。

⇒ 拆成 **DP-10a 静置对照**（证明新引入的"亮像素计数"在噪声下稳定）+ **DP-10b 成对方向量**（被声称的命题）。

### 8.3 门限标定

| 常量 | 值 | 标定依据 |
|---|---|---|
| `kNoiseMaxDelta` | 2 | 实测抖动幅度上限（两处读数都是 1~2） |
| `kNoiseGateRatio` | 0.02 | DP-00 粗门：引擎没冻住的话差异面积接近全图 |
| `kEffectMinRatio` | 0.01 | 噪声带（≤0.1%）的一个数量级以上；实测结构性变化都在几成量级 |
| `kBrightThreshold` | 96 | 星点即使小也画得很亮（叠到饱和），96 能把"星"与"背景/暗天光"分开 |
| `kBrightNoiseMaxDelta` | 100 | DP-10a 静置漂移门（实测 0） |
| `kMagLimitMinDelta` | 500 | 实测信号 2416，取 1/5；同时是噪声绝不可能造出的量级 |
| `kMagLimitHigh` | 15.0 | 默认星表到 ~11 等，15 已等价"全开"，留余量避免卡在星表边界 |

---

## 9 本轮新血泪（已入 `TRAPS.md` 71–76）

| # | 一句话 |
|---|---|
| 71 | 🔴 **「求值时机」型绑定失效**：QML 先于引擎加载，函数式绑定只求值一次、拿空表、永不重算（同 T19 不同病） |
| 72 | 🔴 **"参考帧"的基准必须干净**：抓参考帧前要**全量还原**所有被改过的量 |
| 73 | 🔴 **`delayAfter` 只能挂在写步**：挂在读步 ⇒ 写后 0ms 抓帧 ⇒ **差异 0、哈希相同**（陷阱 41/69 同族第三次） |
| 74 | 🔴 **冻结下的亚 LSB 抖动：面积会随状态变、幅度不会** ⇒ 噪声判据只能拿幅度 |
| 75 | ⚠️ **判据只许验"被声称的命题"** —— 多验一条就是自造红项；新引入的**量**必须配静置对照 |
| 76 | 🔴 **近名 getter 的语义差**：`getLimitMagnitude()`（引擎按环境算的**有效**限制）≠ `getCustomStarMagnitudeLimit()`（**用户设定**）⇒ 读错量的名字 = 结论必错 |

---

## 10 A5 里的位置与后续

- A-1.0 范围表这一格：**亮度/星等、视场、投影 → 已完成**（T38）；剩「高 DPI」归 **T39**。
- 后续（沿用快照 §6 的顺序）：
  - **T39** 高 DPI + 渲染诊断（`capabilities` / `status` 预留）；
  - **T40** 快捷键编辑；**T41** 帮助/版本/许可证页；**T42** 错误页 + "未支持项"清单；
  - **W-T38** Windows 跨平台复验：✅ **已完成**（2026-10-01，见 `docs/WT37_41_WINDOWS_VERIFY.zh_CN.md`）
    —— `DISPLAYCHECK 14/14 PASS ×2`，两组负控红项 `{DP-02,DP-04}` / `{DP-07}` **与 mac 逐位一致**；
  - 挂起项：`#131 T37-X1`（夜视翻转引起 5 个按钮重绘）、`#134 T37-X4`（冻结期 fader 动画停摆）、
    T33 移交的 `findLocations` 排序（完全匹配优先）。

---

## 11 产物清单

| 类别 | 文件 |
|---|---|
| 探针 | `src/app/DisplayProbe.hpp` / `.cpp` |
| 判据 | `src/app/DisplayCheck.hpp` / `.cpp` |
| 产品 | `src/app/AppFacade.{hpp,cpp}`（显示面）、`src/ui/qml/DisplayPage.qml`、`MainWindow.qml`、`Toolbar.qml` |
| 接线 | `src/ui/main.cpp`（`STELQUICK_DISPLAY_PROBE` / `STELQUICK_DISPLAY_CHECK`）、`src/ui/CMakeLists.txt` |
| 跑批 | `tools/t38-verify.sh` |
| 证据 | `docs/evidence/2026-09-30-t38-display/mac/`（探针读数 + 正题×3 + 负控 A×2 + 负控 B×2 + 回归 + `rc-summary.txt`） |
