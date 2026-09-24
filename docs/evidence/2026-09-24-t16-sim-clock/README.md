# T16 单一仿真时钟（时钟所有权收编）验收证据

> 日期：2026-09-24（macOS，Apple M3，Metal RHI 合流形态）
> 代码：`main` 上 T16 提交（见 `docs/BUILD_RECORD.zh_CN.md` 与 `docs/T16_SINGLE_SIM_CLOCK.zh_CN.md`）
> 环境：`VK_DRIVER_FILES=…/MoltenVK_icd.json`、`QT_VULKAN_LIB=…/libvulkan.1.dylib`、`STELQUICK_GRAPHICS_API=metal`
> 一键复跑：`./tools/t16-verify.sh`

## 一、结论速览

| 项 | 结果 |
|---|---|
| 时钟纯逻辑自检（12 项，无 GL / 无引擎） | **PASS rc=0** |
| 集成自检（12 项逻辑 + 11 项 AC，真实引擎 + 帧泵） | **PASS rc=0** |
| 回归 A2 / DYN / S3 | **三组全绿 rc=0** |
| **帧率**（本任务最重要的副产品） | **DYN 尾窗 34.9 → 49.8 fps**；90s 短窗 38.71 → 49.18 fps |

---

## 二、T16 的核心发现：合流形态此前**一直有两个 update/draw 驱动源**

### 2.1 是怎么发现的

AC-7 首跑直接读到了反例：

```
ACTIONCHECK: AC-7 唯一 update 驱动源：simClock=HostDriven，旧宿主 fpsTimer=仍在跑：FAIL
```

即 `StelMainView::fpsTimer` 在合流形态下**是活的**，而不是 T14 判定的"天然休眠"。

### 2.2 T14 的判定为什么错（方法论教训，值得留档）

T14 的"佐证"是：T13 的 30 分钟长跑日志（321 行）里 `paintGL` / `drawEnded` 各出现 **0** 次，
于是推断旧绘制路径从未执行。这条推断有两处硬伤：

1. `paintGL()` **永不执行**——源码注释自己写着 "this is actually never called because the
   QGraphicsView intercepts the paint event"。所以 `paintGL` 计数为 0 是**必然的**，不携带任何信息。
2. `drawEnded()` **根本没有日志语句**——它不可能出现在任何日志里。
   **字符串不出现 ≠ 函数没执行**，这是把"没有证据"当成了"证据表明没有"。

加上一个被忽略的事实：`LiveSkyRuntime::boot()` 里是**调了 `m_mainView->show()`** 的
（WA_DontShowOnScreen 只影响是否上屏，不影响 show 语义与绘制链的点燃）。

### 2.3 真实机制

```
boot():  m_mainView->show()
           → QWidget/QGraphicsView 绘制链
             → LegacyGraphicsItem::paint()  ← 每次都是 app.update(dt) + app.draw() 的整帧天空渲染
               → mainView->drawEnded()
                 → fpsTimer->start()        ← 自持！此后按 getDesiredFps() 持续重绘
```

于是每帧实际发生 **两次完整的引擎渲染**：一次进我们自己的离屏 FBO（→ 邮箱 → QML 上屏），
一次进旧宿主 QGraphicsView 的 widget FBO（**被丢弃，无人消费**）。后者白烧主线程与 GPU。

**帧率量化（同口径、同后端、同尺寸 1920×1280、同一 producer 代码路径）**：

| 组 | T15（含双驱动源） | T16（单驱动源） | 变化 |
|---|---|---|---|
| DYN D1-C01 生产者尾窗 | **34.9 fps** | **49.8 fps** | **+43%** |
| DYN D1-C02 显示尾窗 | 34.9 fps | 49.7 fps | +42% |
| 90s 长跑短窗 SL-C01 | 38.71 fps（当时判"环境噪声"） | **49.18 fps** | +27% |
| 90s 长跑短窗 SL-C02 | 38.23 fps | **49.11 fps** | +28% |

**→ T15 遗留的 W-T15「短窗帧率受限」的正解不是环境噪声，而是重复驱动源抢主线程。
T16 顺手闭合了它。**

### 2.4 处置

```
① StelMainView::stopLegacyFrameTimer()        ← 宿主接管那一刻显式停表（LiveSkyRuntime::start 调用）
② StelMainView::drawEnded() 内守卫            ← 主机驱动时主动停表 + 防复活（只"不自启"不够，
                                                  因为 fpsTimer 可能在接管之前就已点燃）
③ StelMainView::isLegacyFrameTimerActive()    ← 可观测判据（AC-7 用它断言）
```

---

## 三、时钟纯逻辑自检（`STELQUICK_CLOCK_CHECK=1`）—— **12/12 PASS rc=0**

`clockcheck-logic.txt`。**无窗口、无引擎、无事件循环**即可运行（这也是该分支独立存在的理由：
合流形态整体启动失败时，时钟语义仍能单独判定）。

| 项 | 断言 | 结果 |
|---|---|---|
| ST-01 | 默认态 EngineWallClock / 未锚定 / scale=1 | OK |
| ST-02 | `reset` 锚定起点 | OK |
| ST-03 | 宿主推进 dt=1s × rate=0.5 → +0.5 | OK |
| ST-04 | 三次 0.25s × rate=1 → 恰好 +0.75（推进量只与 dt 有关） | OK |
| ST-05 | scale=0 连续推进 1.0s 墙钟 → JD 零漂移 | OK |
| ST-06 | 暂停 5s 后恢复：只推进 0.2×0.5=0.1（**不补暂停期**） | OK |
| ST-07 | `jumpTo` 绝对写入后推进从新值起算（不被拽回） | OK |
| ST-08 | 负墙钟差被忽略（倒退只能由 jumpTo / 负速率表达） | OK |
| ST-09 | EngineWallClock：锚点 + 2s×0.5 = 锚点+1 | OK |
| ST-10 | 负速率倒放：dt=1s × rate=-0.5 → -0.5 | OK |
| ST-11 | 负 scale 钳为 0（不允许经由 scale 表达倒放） | OK |
| ST-12 | 模式切换不动 JD（真源不被切线重置） | OK |

---

## 四、集成自检（`STELQUICK_ACTION_CHECK=1`）—— **PASS rc=0**

`actioncheck-mac.txt`（含 T15 的 6 项与 T16 的 5 项）。

### T16 新增判据

| 判据 | 结果 | 数据 |
|---|---|---|
| AC-7 唯一 update 驱动源 | **OK** | `simClock=HostDriven`，旧宿主 `fpsTimer=未激活`（修复前为"仍在跑"） |
| AC-8 **墙钟路径未参与推进** | **OK** | 暂停态把墙钟锚点人为拉远 **1 小时** → JD 漂移 **0.000e+00 天**（若 EngineWallClock 路径仍生效，就是 +360 天的巨跳） |
| AC-9b **外部跳转不被拽回** | **OK** | `core->setJD(2462307.661)`（模拟插件）暂停态静置 0.5s → 观测 **2462307.661**（严格相等） |
| AC-9c 恢复从**新锚点**起算 | **OK** | 0.8s 推进 Δ=**0.079900** 天（期望 0.080000±30%） |
| AC-10 **速率线性贯通** | **OK** | R=0.100 → Δ=0.079900；2R=0.200 → Δ=0.164200；**比值 2.055**（期望 ≈2） |
| AC-11 **插件式 `core->setTimeRate(0)` 真冻结** | **OK** | 不经 AppFacade 直接调引擎 → 0.5s 漂移 **0.000e+00 天** |

> AC-11 是 T15/T16 的**判别性判据**：T15 形态下宿主用自己的速率字段推进，
> 插件直接调 `core->setTimeRate(0)` 是**停不住**的。AC-8 同理——
> 两者都是严格相等断言，不依赖时间窗或抖动容忍。

### T15 原有判据（全部保持）

AC-1 widget QAction 分发已拆除 **OK**；AC-2 zoom 单步 60.0000→48.0000→38.4000→48.0000 **OK**；
AC-3a 幂等零信号 **OK**；AC-3b 运行态 ΔJD=0.037900 天/0.4s **OK**；
AC-3c 暂停 0.5s JD 漂移 **0.000e+00 天** **OK**；AC-4 禁用门零副作用 **OK**；
AC-5 透传单次（ID 直呼 + Q 键各恰一次）**OK**；AC-6 焦点守卫基线 **OK**。

---

## 五、回归（要求：A2 / DYN / S3 零退化）—— **三组全绿 rc=0**

| 组 | 命令 | 结果 |
|---|---|---|
| A2 静态纹理（Metal） | `STELQUICK_GRAPHICS_API=metal STELQUICK_A2_CHECK=1` | **VERDICT=PASS rc=0**（`regression-a2-metal.txt`，11 项逐像素全过） |
| DYN 动态帧（引擎生产者 + Metal） | `… STELQUICK_DYN_CHECK=1 STELQUICK_DYN_PRODUCER=engine` | **7/7 PASS rc=0**（`regression-dyn-engine-metal.txt`；尾窗 **49.8 fps**，T15 为 34.9） |
| S3 引擎集成（旧宿主 stellarium.exe） | `STELA3_CHECK=1 ./build-release/src/stellarium` | **VERDICT=PASS 8/8 rc=0**（`regression-s3-stela3.txt`） |

**S3 全绿的意义**：它跑的是 `stellarium.exe`（**EngineWallClock 模式**，`HostDriven==false`）。
8/8 全过 ⇒ T16 对旧形态是**逐位零影响**——所有新机制只在合流宿主显式切线后生效。
（其中 A3-C05「8 帧 8 个互不相同指纹」尤其关键：证明确实是"旧路径照旧推进"，没有被打断。）

---

## 六、90s 长跑短窗冒烟（`shortsmoke-90s-battery-throttled.txt`）—— 帧率闭合，2 项短窗口径不适用

环境：电池供电 + `STELQUICK_LONGRUN_ALLOW_THROTTLED=1` 强制测量（用户指示"不用查电源，直接跑"）；
预热 30s + 测量 60s。

| 判据 | 结果 | 数据 |
|---|---|---|
| **SL-C01 生产者零失败 + 稳态 fps** | **PASS** | 2933 帧 / 失败 0；**49.18 fps**（下限 40） |
| **SL-C02 稳态显示帧率** | **PASS** | **49.11 fps**（下限 40） |
| SL-C03 上屏间隔 | FAIL（单点离群） | p99 **53.31**（门槛 61.09，**过**）；max **142.36ms** > 122.18ms 档 → 1985 帧里 1 帧越界 |
| SL-C04 邮箱丢弃 | PASS | 投递 2933 / 丢弃 **0** |
| SL-C05 上传耗时 | PASS | 均值 **0.134ms**（<5.0），最坏 9.31ms（<100） |
| SL-C06 邮箱帧龄 | PASS | max **57ms**（<500） |
| SL-C07 内存斜率 | FAIL（**短窗不适用**） | 1877.2 → 1891.3 MiB（+14.1）；60s 窗把 warmup 期分配拟合成斜率，该判据的设计口径是 30 min 稳态窗（T15 已有同类记录） |
| SL-C08 环境漂移 | PASS（降级告警） | 2 次降频违规 → 按协议降为警告 |
| SL-C09 窗口暴露 | PASS | 60/60 采样点 |
| SL-C10 降级误报 | PASS | 0/60（0.000%） |
| SL-C11 GUI 线程帧龄 | PASS | mean 0.69 / p95 **1.00** / max 76.00 ms（门槛 p95≤100） |

**SL-C03 单点离群归因**：SL-C08 明确记录本跑发生 **2 次频率降频**（电池供电、允许降频）。
一次 142ms 的主线程停顿与降频事件一致；标定口径（p99）本身是过的。
这与 T13 正式跑第 1 轮记录的 "SLC03-single-outlier" 属同一类现象
（见 `docs/evidence/2026-09-23-t13-live-longrun/17-official-run1-Metal-…-SLC03-max-outlier.log`）。

**SL-C07 归因**：短窗（60s）在预热后仍在分配引擎资源，端点增量 +14.1 MiB 不足以支撑斜率判据；
T15 短窗已有完全相同的问题记录（当时端点甚至是负增长 -0.5 MiB 却拟合出正斜率）。

> 结论：90s 短窗的**结构性判据**（帧率/丢弃/上传/帧龄/暴露/降级）全过；
> 两项 FAIL 均为短窗固有口径问题 + 电池降频，**不构成 T16 回归**，也不影响 T16 的验收判据
> （T16 的判据在 §三/§四，已全部 PASS）。

---

## 七、过程修正记录（重要，供后续复盘）

1. **AC-7 首跑 FAIL** → 发现"旧宿主天然休眠"的判定错误（§二）→ 加 `stopLegacyFrameTimer()` 主动停表
   + 守住"防复活"两条腿 → 复跑 AC-7 OK。**这是本次唯一的首跑失败，且它揭示的是真实缺陷。**
2. **T14 报告已就地打勘误**（文首 + §1.2 关键结论 ②），避免后来人继续沿用错误结论。
3. **笔误纠正**：`step()` 的 lambda 曾捕获未使用的 `ctx`、`LiveSkyRuntime` 回调曾捕获未使用的
   `this` → 编译告警，已清除（最终构建仅剩 Qt dylib 版本类告警）。
4. **日志读取教训**（写进方法论）：验证"某函数是否执行"时，**先确认该函数有没有日志**；
   没有日志就用可观测状态（如 `isLegacyFrameTimerActive()`）断言，不要用"日志里没出现"反推。

---

## 八、文件清单

| 文件 | 内容 |
|---|---|
| `clockcheck-logic.txt` | 纯逻辑自检 12 项（无 GL / 无引擎） |
| `actioncheck-mac.txt` | 集成自检（12 项逻辑 + 11 项 AC） |
| `rc-summary.txt` | 五组退出码汇总 |
| `regression-a2-metal.txt` | A2 静态纹理回归 |
| `regression-dyn-engine-metal.txt` | DYN 7 判据（引擎生产者）回归 |
| `regression-s3-stela3.txt` | S3 引擎集成回归（旧宿主形态，8/8） |
| `shortsmoke-90s-battery-throttled.txt` | 90s 长跑短窗冒烟（帧率闭合的关键证据） |
| `shortsmoke-90s.csv.gz` | 逐秒采样 |
| `shortsmoke-90s.frames.csv.gz` | 逐帧上屏采样 |
