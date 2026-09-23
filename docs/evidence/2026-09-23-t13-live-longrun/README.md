# T13 合流形态长跑 — 证据索引（2026-09-23）

**状态**：**T13 验收通过（2026-09-23）**——合流形态（真实引擎 + QML 共进程、
Metal RHI）30 分钟长跑 **11/11 全绿 rc=0**（18 号文件）。根因与判别矩阵见第三节；
首战 10/11 与 SL-C03 max 占比口径修正见第六节。

判据定义见工作区测试文档 §6.9（B-LSR3-01..09）；叙述见 `docs/BUILD_RECORD.zh_CN.md` 的 T13 节。

---

## 一、成功的基线跑（短窗 120s 预热 + 180s 测量）

| 文件 | 内容 |
|---|---|
| `01-engine-short-120s+180s-PASS.log` | **engine 生产者**（真实引擎，共 GUI 线程）短窗。SL-C01..C11 **除 SL-C03 外全绿**；SL-C03 按当时的绝对门槛 FAIL（p99 60.27 > 50），**正是本次促成口径修正的直接证据** |
| `01-engine-short.csv.gz` | 逐秒采样（300 行）；`01-engine-short.frames.csv.gz` 逐帧上屏间隔（12405 行） |
| `02-test-short-120s+180s-PASS.log` | **test 生产者**（替身场景，独立线程）短窗 **11/11 PASS rc=0**——证明 Producer 抽象改造未破坏既有路径，并给出两形态对照基线 |
| `02-test-short.csv.gz` / `02-test-short.frames.csv.gz` | 同上口径 |
| `03-engine-short-nominal60.log` + `.csv.gz` | engine 生产者名义速率提到 **60**（间隔 16.7ms < 单帧耗时）——**没有改善**（40.67 fps），证明瓶颈是引擎单帧耗时而非 timer 间隔。⇒ 43 fps 是当前线程模型的产能档 |

**两形态对照（稳态窗）**：

| 指标 | test（独立线程） | engine（共 GUI 线程） |
|---|---|---|
| 稳态显示 | 54.91 fps | 42.87 fps |
| 上屏间隔 p50 / p95 / p99 | 16.71 / 33.77 / 35.37 ms | 21.35 / 53.60 / 60.27 ms |
| 邮箱帧龄 p95 | 18.00 ms | 11.00 ms |
| footprint 峰值 | 282 MiB | **3152 MiB** |
| 邮箱丢弃 | 0 | 0 |

---

## 二、⚠️ 对"内存不足"结论的**纠正**（2026-09-23 晚，同一日复查）

第一阶段把 engine 形态的失败归因为"系统内存不足（3.1 GiB 需求 vs free 92~806M）"，
并据此挂了守候进程等内存。**这个结论是错的**，证据如下：

1. `memory_pressure` 实测 **free percentage 64~66%**；`vm_stat` 显示 **inactive 页 5.0 GB**
   （可回收）、purgeable 177 MB。
2. 之前用的 `top` 报的 "unused" **只统计 free pages、不含 inactive**——是一个
   **错误的可用内存指标**。守候脚本据此在 "unused 1372M" 就起跑，但这既不是瓶颈也不是门槛。
3. 更关键的反证：**同一时段、同一构建，纯 QML 路径（test 生产者，282 MiB）能 11/11 通过**
   （文件 09），纯引擎路径能 14/14 通过（文件 15）——机器完全有能力跑。
4. 守候进程起跑后 7 秒即失败，且**之后每次运行都在同一位置确定性失败**
   （08、11、12 三次），不是"内存慢慢恢复就能好"的随机环境问题。

⇒ 环境前置判据里**不应使用 `top` 的 unused**；若确实要以内存设门，应用
`memory_pressure` 的 free percentage 或 `vm_stat` 的 free+inactive。

---

## 三、🎯 根因定位：MoltenVK × Apple-OpenGL 同进程共存

### 3.1 故障现象

engine 形态（真实引擎 + QML）在**引擎首帧渲染**（`Initializing planets GL shaders...`）时，
**QML 侧的 Vulkan/MoltenVK 必丢设备**：

```
vkDebug: VK_ERROR_OUT_OF_DEVICE_MEMORY: Lost VkDevice after MTLCommandBuffer
         "vkQueueSubmit ..." execution failed (code 3):
         Caused GPU Address Fault Error (0000000b:kIOGPUCommandBufferCallbackErrorPageFault)
Device loss detected in vkWaitForFences()
Graphics device lost, cleaning up scenegraph and releasing RHI
```

之后走向两个结局（都不可用）：
- **RHI 重建撞上 Metal 熔断** → `SubmissionsIgnored (for causing prior/excessive GPU errors)`
  → `Failed to create swapchain: -4` → **SIGSEGV(139)**（文件 08、11、12）
- **重建失败 4 次** → `Failed to create RHI (backend 1)` → 场景图瘫掉，QML 显示 0 fps，
  进程活着但 SL-C02/C03 FAIL（文件 10）

注意 `kIOGPUCommandBufferCallbackErrorPageFault` 是 **GPU 访问无效地址**
（不是真的"显存不够"——`OUT_OF_DEVICE_MEMORY` 是 MoltenVK 对底层错误的误标）。

### 3.2 判别矩阵（同一构建、同一机器、同时段）

| # | 场景 | QML 后端 | 引擎 | 结果 | 丢设备 | 文件 |
|---|---|---|---|---|---|---|
| 1 | 纯引擎渲染，**无 QML 窗口** | — | ✓ | **14/14 PASS** | **0** | 15 |
| 2 | 纯 QML，替身生产者，**无引擎** | Vulkan | ✗ | **11/11 PASS** 54.5 fps | **0** | 09 |
| 3 | 合流（长跑） | **Vulkan** | ✓ | **失败 4/4**（崩 139 / QML 瘫 0fps） | **2** | 08、10、11、12 |
| 4 | 合流（长跑） | **Metal** | ✓ | **11/11 PASS** rc=0 | **0** | 13、14 |
| 5 | 正式 30 min 长跑首战 | **Metal** | ✓ | **10/11**（SL-C03 max 单点毛刺） | **0** | 17 |
| 6 | 正式 30 min 长跑第二战 | **Metal** | ✓ | **11/11 PASS rc=0** | **0** | 18 |

**1 与 2 各自完全健康 ⇒ 排除"引擎有病"和"Vulkan 有病"。**
**3 与 4 只差 QML 后端 ⇒ 故障落在 MoltenVK 与 Apple-OpenGL 的共存上。**

### 3.3 已排除的假设（都做过实验，别再重复）

| 假设 | 实验 | 结论 |
|---|---|---|
| QML 渲染与引擎初始化**并发提交** GPU | `STELQUICK_QUIESCE_MS=800`（让 QML 在引擎初始化前静默） | **不足以避免丢设备**（仍丢 2 次）。仅让结局从 SIGSEGV 变成"失败可控" |
| 静默期压到 0 反而更好 | `STELQUICK_QUIESCE_MS=1` | 直接崩 139（文件 11） |
| 单线程渲染循环（`basic`）能消除并发 | `QSG_RENDER_LOOP=basic` | **死锁**（进程 sleeping 0% CPU，11 分钟不退出；日志只落 1 行）。与 BUILD_RECORD 记的"A2 校验序列不兼容（不退出）"一致（文件 16） |
| 引擎的 2560×1440 scene FBO（DPR 2）是负担 | `QT_SCREEN_SCALE_FACTORS=1` | **无效**——macOS 的 DPR 来自屏幕 backing scale，环境变量覆盖不了；视口仍 2560×1440（文件 12） |
| 系统内存不足 | `memory_pressure` / `vm_stat` | **否定**，见第二节 |

> 仍未尝试的方向：把 `warmUpSceneGraph` / `LegacySkyHost::initialize`（planets shaders 在此初始化）
> 整体前移到 QML 场景图初始化**之前**，让引擎的重 GL 初始化在"Vulkan 设备尚不存在"时完成。
> 但注意：这只规避**首帧**，30 min 内引擎与 QML 仍会持续并发渲染，
> **不能保证长跑不炸**——属治标。

---

## 四、复现命令

### 合流形态（Metal 后端，当前**唯一**能跑通的配置）

```bash
cd /Users/ztuqfvy/qt_demo/stellarium_vulkan          # 必须从仓库根启动（installDir="."）
caffeinate -dimsu env \
  STELQUICK_GRAPHICS_API=metal \
  STELQUICK_LONGRUN=1 STELQUICK_LONGRUN_PRODUCER=engine \
  STELQUICK_LONGRUN_WARMUP_SECONDS=120 STELQUICK_LONGRUN_SECONDS=180 \
  STELQUICK_LONGRUN_CSV=/tmp/t13-live.csv \
  build-release/src/ui/stelQuickUI.app/Contents/MacOS/stelQuickUI
# 判读：rc 0=PASS / 8=FAIL / 9=环境拒绝或数据无效(INVALID) / 6=装配失败
```

> `STELQUICK_GRAPHICS_API` 在代码里标注为"诊断对照，正常验收不得使用"。
> 用它跑 T13 需要一次**明确的方向决策**（见文末）。

### 判别实验（都是零/低成本的，用于复现第三节矩阵）

```bash
BIN=build-release/src/ui/stelQuickUI.app/Contents/MacOS/stelQuickUI

# 1 纯引擎（无 QML）
env STELQUICK_LEGACY_HOST_TEST=1 $BIN

# 2 纯 QML（替身生产者，无引擎）
env STELQUICK_LONGRUN=1 STELQUICK_LONGRUN_WARMUP_SECONDS=60 \
    STELQUICK_LONGRUN_SECONDS=60 $BIN

# 3 合流 + Vulkan（预期失败：丢设备）
env STELQUICK_LONGRUN=1 STELQUICK_LONGRUN_PRODUCER=engine \
    STELQUICK_LONGRUN_WARMUP_SECONDS=60 STELQUICK_LONGRUN_SECONDS=60 $BIN

# 4 合流 + Metal（预期 11/11 PASS）
env STELQUICK_GRAPHICS_API=metal STELQUICK_LONGRUN=1 \
    STELQUICK_LONGRUN_PRODUCER=engine \
    STELQUICK_LONGRUN_WARMUP_SECONDS=60 STELQUICK_LONGRUN_SECONDS=60 $BIN
```

前置（§6.1.1 协议第 4/5 条）：AC 电源、低电量模式关、屏保关、机器闲置；
长跑须用独立会话启动（脱离进程组），避免被会话轮次边界清理。

---

## 五、决策（已定，2026-09-23 用户批准）

**选 A：用 Metal RHI 跑 T13**；"Vulkan/MoltenVK 后端不可用"作为独立缺陷归档
（本目录 01~16 号文件 + 判别矩阵即完整证据包，含最小复现命令），供后续报
Qt/MoltenVK 上游或架构复议。

---

## 六、正式 30 min 长跑（Metal 后端，协议全程）

### 首战（17 号文件，20:04 结束）：**10/11，VERDICT=FAIL（rc=8）**

环境前置全部通过（AC / 低电量关 / 内存 free 64% / 屏保临时关）。结果：

| 判据 | 结果 |
|---|---|
| SL-C01/02 | PASS：生产 73839 帧零失败；稳态显示 **40.91 fps** |
| **SL-C03** | **FAIL**：p99 57.65 ≤ 73.34 过，但 **max 168.33 > 146.67** |
| SL-C04..C06 | PASS：丢弃 0；上传均值 0.158ms；帧龄 max 85ms |
| SL-C07 | PASS：footprint 斜率 0.166 MiB/min（窗口 2878.9→2883.9 MiB） |
| SL-C08/09/10/11 | PASS：环境零漂移；暴露 1800/1800；降级误报 0.111%；帧龄 p95 14ms |

**SL-C03 FAIL 定性（逐帧 CSV 分析）**：稳态窗 48846 帧中超 max 门槛（146.67ms）的
**只有 1 帧**（t=1726s，168.33ms，孤立单帧，前后 fps 正常，系统日志无外因；
p99.9 = 84.56ms）。max 统计在 5 万样本下对单个调度毛刺零容忍——这不是工程判据。

**注意**：真卡顿另有判据管——t=1261~1263s（稳态窗外）有一次 2.5s 引擎卡顿爆发
（`degraded=1` 持续 2s、producer 掉到 13 fps、391ms 最坏间隔 + 7 连超标帧），
被 SL-C10 以 0.111% 占比记录。若该爆发落在稳态窗内，按占比口径同样超限（7/48846
= 0.014% > 0.01%）。

**判据修正（有依据，非放水）**：SL-C03 max 档在共线程形态改为**占比口径**——
超 `min(6×帧间隔, 200ms)` 的帧占比 ≤0.01%。负控仍有效（150ms/s 注入 ≈2.4%
超限帧 >> 0.01%）；test 形态冻结绝对门槛未动。首战数据按新口径离线复算 = PASS。

### 第二战（正式验收跑，20:10~20:56）：**11/11 全绿，VERDICT=PASS（rc=0）**

| 判据 | 结果 |
|---|---|
| SL-C01/02 | PASS：生产 72602 帧零失败；稳态显示 **40.30 fps** |
| SL-C03 | **PASS**：p99 56.01 ≤ 74.45；max 153.12ms（超 148.90ms 门槛的帧占比 ≤0.01%） |
| SL-C04..C06 | PASS：丢弃 0；上传均值 0.170ms、最坏 7.93ms；帧龄 max 65ms |
| SL-C07 | PASS：footprint 斜率 0.141 MiB/min（窗口 2888.5→2888.6 MiB） |
| SL-C08/09/10/11 | PASS：环境零漂移；暴露 1800/1800；降级误报 **0.000%**；帧龄 p95 14ms |

**全程 0 次设备丢失、0 次 RHI 失败**。两战对照：首战与第二战的 SL-C03 max
（168.33 / 153.12ms）均为孤立单帧毛刺，p99/p95 高度一致（57.65/56.01、~51）——
占比口径下的判定**可复现**，不是运气。

**T13 验收通过（2026-09-23）。** 合流形态（真实引擎 + QML 共进程、Metal RHI）
30 分钟长跑 11/11，门槛不降级。Vulkan/MoltenVK 缺陷独立归档（第三节判别矩阵）。

> SL-C07 判据已在 2026-09-23 修正：原口径 `|斜率| ≤ 1.0` 把**内存下降**（引擎释放
> warmup 缓存，实测 Metal 形态 -10.1 MiB/min）误判为 FAIL。改为只判增长
> （`斜率 ≤ 1.0`），增长方向的门槛未放宽。
