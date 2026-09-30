# T27 证据 — 天空页鼠标（点击选中 / 拖拽平移 / 右键反选）端到端

macOS + Metal（`VK_DRIVER_FILES=MoltenVK`、`QT_VULKAN_LIB`、`STELQUICK_GRAPHICS_API=metal`），
口径与 t17..t26 一致，**刻意不换**。一键复跑：`tools/t27-verify.sh all`。

## 目录

| 文件 | 内容 |
|---|---|
| `interactcheck-mac-n5.txt` | 正题 INTERACTCHECK ×5 全量日志（9 条判据/次） |
| `interactcheck-mac-run1..5.txt` | 单次原始输出 |
| `negctrl/negctrl1-mousearea-disabled.log` | 负控①：QML MouseArea `enabled:false` → IT-07/08/09 红 |
| `negctrl/negctrl2-mapping-degraded.log` | 负控②：坐标映射退化为等尺寸 → IT-08/09 红、IT-07 绿 |
| `negctrl/first-run-no-discriminating-power.log` | 首跑：IT-07 在无拖拽链路下"绿"⇒ 判据无判别力（存档） |
| `regression-*.txt` | 回归 10 项（search/locate×2/action/time×2/return/replay/clock/A2/S3） |
| `regression-dyn-{engine,stub}-metal*` | DYN 引擎 vs 替身（各 3 次） |
| `rc-summary.txt` | 全部退出码汇总 |
| `t27-run6.log` | 映射修复后 IT-08/09 首次转绿（含当时 DIAG 输出，留作过程证据） |

## 结论（读数）

- **INTERACTCHECK 9/9 PASS ×5**（判据 6 → 9，新增 IT-07/08/09）
  - IT-07 冻结时间后左拖 160px → 视线转 **18.16°**（>0.2°），再右拖 → 两次增量
    点积 **-0.9981**（<0，几乎完美反向）⇒ 平移链活且方向正确
  - IT-08 月球居中（角距 0.0000°）→ 清选中 → 真实点击视口中心 → 选中 **"Moon"**
  - IT-09 右键 press+release → 选中 true → **false**（反选，与 IT-08 成对）
- **回归 10 项全 rc=0**：searchcheck（T26）、locatecheck/locate-uicheck（T18）、
  actioncheck、timecheck/timeuicheck（T19/T22）、returnuicheck（T20）、replaycheck
  （I-REP-02）、clockcheck（T16）、**a2-metal 逐像素**（动过 SkyTestPage.qml 的
  硬要求）、**S3 旧宿主**（鼠标入口与旧宿主共用 `StelApp::handleClick/handleMove`）
- **DYN：引擎 1/3 + 替身 1/3**（load average 3.6–4.2，Spotlight 批量索引在跑）
  ⇒ 替身同败 = **"仪器测不到"**（环境负载），非退化；原始读数照实保留。

## 两个必须留档的发现

### 1. 引擎投影视口与 QML 窗口**不同构**（坐标映射的必要性）

DIAG 实测：`引擎投影视口 2560×1440 dppp=2 月球投影=(1280.0,720.0) 点击=(480.0,364.0)`。
合流形态引擎渲染固定 `renderSize`（默认 1280×720）帧再缩放显示到窗口（QML 逻辑
960×728）⇒ **不能像旧宿主那样只做 `h-1-y` 翻转**（旧宿主 rect 与引擎视口等尺寸，
翻转即完整适配）。修法：`skyEnginePos = (x/wq·wp/dppp, (1-y/hq)·hp/dppp)`
（比例映射 + y 翻转 + 除以 dppp，因 `handleClick/handleMove` 内部会乘回）。
**实证**：负控②把映射退化为等尺寸 ⇒ IT-08/09 立刻红。

### 2. 🔴 仿真时间推进把"拖拽效果"淹没（判据污染源）

DIAG 实测：HostDriven 帧泵在 **400ms 内推进 0.61 天**仿真时间（≈1.5 天/秒），
而同一时刻 `engineRate` 读数只有 10（simJD 差 0.61 天 vs rate=10 该走的 4.6e-5 天，
**差 4 个数量级**——帧推进与 rate 脱钩，见"待查线索"）。视线锁定地平坐标时
J2000 视线随 JD 转 ⇒ **自然漂移 200°/s**，400ms 就漂 80°。旧口径"视线转过 >0.2°"
因此在**拖拽链路完全断开**时仍然绿（首跑负控实测 46.58°，见 `negctrl/
first-run-no-discriminating-power.log`）——孤立断言无判别力（血泪第 4 条）。

> 🔴 **上面这段的"脱钩 / 差 4 个数量级"已被 T35 推翻（2026-09-30，
> `docs/T35_TIMELINK.zh_CN.md` + `../2026-09-30-t35-timelink/`）。**
> **观察是真的，解释是错的**：
> ① `getTimeRate()` 的单位是 **JDay/sec**（`src/core/StelCore.hpp:595`），
> 那个 `× JD_SECOND` 是**凭空引入的 1/86400 因子**；
> ② `engineRate=10` 是 **INTERACTCHECK 的 IT-05 自己注入 L 键**（`increaseTimeSpeed()`，
> ×10 阶梯）抬上去的**移动靶**；
> ③ "400ms" 是**相位名义 delay**，从未与 ΔJD 同刻测过真实窗口。
> 同刻量齐三个量后 `ΔJD == 窗口 × rate × scale` 成立（TL-01 相对偏差 **0.19%..1.40%**／5 跑、
> 恒星时绝对腿残差 **+0.0000°**）；"0.61 天"是对的，只有"÷0.4s"和那个 rate 读数不对。
> 下文的**处置（冻结仿真时间 + 双向拖拽反向对照）不受影响**——视线随 JD 漂这件事本身
> **是真的**（T35 探针 Q6d 实测 ΔRA 与 ΔLST 同阶），只是速率读数需换算。

处置：①判据段内**冻结仿真时间**（`setSimulationPaused(true)` → `ISimPacing::
setSimScale(0)`；门内实测 simJD 推进 **0.000000000 天**，段尾还原——血泪第 9 条）；
②同相位内**双向拖拽反向对照**（共模漂移同向叠加，反向断言抵抗残余漂移）。

### 待查线索（不在 T27 范围，如实记录）

帧泵推进量（1.5 天/秒）与 `getTimeRate()`（=10）**不一致**。若这是设计（帧推进
走固定步长，rate 只管别的语义）则需在产品文档说明；若不是，则是一个独立的
时间链路缺陷——**建议单独立项核查**（不在本轮偷偷改）。

> ✅ **已结清（T35，2026-09-30）**：既不是设计，也不是缺陷，是**测量口径**问题
> （单位 / 移动靶 / 名义窗口）。产品侧**零语义改动**，T27 的处置不需回改。
> 结论与证据：`docs/T35_TIMELINK.zh_CN.md`、`../2026-09-30-t35-timelink/README.md`。

## 负控（两轮，各命中一层）

| 负控 | 改动 | 预期红 | 实测 |
|---|---|---|---|
| ① 链路层 | `MouseArea.enabled:false` | IT-07/08/09 | IT-07/08/09 全红；IT-05 不受伤 ✔ |
| ② 适配层 | `skyEnginePos` 退化为 `h-1-y` | IT-08/09（映射守卫） | IT-08/09 红，**IT-07 仍绿** ✔（两层分工实证：IT-07 守"链路活"，IT-08 守"映射正确"） |

## 仪器加固（不放宽判据）

IT-06 焦点腿偶发假红，根因**不是**"守卫失效"而是 **窗口未获得系统焦点**
（`isActive=false` ⇒ `QGuiApplication::focusObject()` 恒为 nullptr ⇒ 守卫前提
不存在；判据注入的按键是 `sendEvent` 直达窗口，绕过系统焦点所以引擎照样收到）。
处置：①点击后**校验焦点对象是 QQuickTextInput**（有界重试 2 次）；②窗口未激活时
`requestActivate` 有界重试 3 次，仍失活则**明确判红并标注"仪器不可用"**，附当前
焦点类名 —— **不洗成 PASS、不改写成 INVALID**（同 DYN 处置纪律）。
