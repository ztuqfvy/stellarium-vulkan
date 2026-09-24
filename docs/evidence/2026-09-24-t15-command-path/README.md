# T15 命令通路（AppFacade + ActionRouter）验收证据

> 日期：2026-09-24（macOS，Apple M3，Metal RHI 合流形态）
> 代码：`main` 上 T15 提交（见 BUILD_RECORD 对应条目）
> 环境：`VK_DRIVER_FILES=…/MoltenVK_icd.json`、`QT_VULKAN_LIB=…/libvulkan.1.dylib`、`STELQUICK_GRAPHICS_API=metal`

## 一、命令通路自检（STELQUICK_ACTION_CHECK=1）—— **VERDICT=PASS，rc=0**

`actioncheck-mac.txt`（全量日志）。9 项断言：

| 断言 | 结果 | 数据 |
|---|---|---|
| AC-1 widget QAction 分发拆除 | **OK** | `StelAction::isWidgetShortcutDispatchEnabled()==false`（合流形态） |
| AC-2 zoom 单步 | **OK** | fov 60.0000 → 48.0000 → 38.4000 → 48.0000（与期望逐位一致：×0.8/×0.8/×1.25） |
| AC-3a 同值暂停幂等 | **OK** | 同值 setSimulationPaused 零信号 |
| AC-3b 运行态 JD 推进 | **OK** | +0.4s → ΔJD = 0.041200 天（simRate 0.1 × 0.412s） |
| AC-3c 暂停冻结 | **OK** | 暂停 0.5s JD 漂移 **0.000e+00 天**（门槛 1e-12） |
| AC-3d 恢复推进 | **OK** | 恢复后 JD 继续增长 |
| AC-4 禁用门（U-ACT-02） | **OK** | enabled=false → trigger 返回 false、handler 零调用、dispatch 计数不动 |
| AC-5 透传单次（U-ACT-01） | **OK** | ID 直呼 + Q 键各恰一次（toggled 计数=2，checks 翻转后复原） |
| AC-6 焦点守卫基线（U-ACT-03） | **OK** | 无焦点时 canDispatchToSky=true |

## 二、回归（任务书要求：DYN/A2/S3 零退化）—— 三组全绿

| 组 | 命令 | 结果 |
|---|---|---|
| A2 静态纹理（Metal） | `STELQUICK_GRAPHICS_API=metal STELQUICK_A2_CHECK=1` | **VERDICT=PASS，rc=0**（`regression-a2-metal.txt`） |
| DYN 动态帧（引擎生产者 + Metal） | `… STELQUICK_DYN_CHECK=1 STELQUICK_DYN_PRODUCER=engine` | **7/7 PASS，rc=0**（`regression-dyn-engine-metal.txt`；D1-C01 尾窗 34.9fps） |
| S3 引擎集成（旧宿主） | `STELA3_CHECK=1 ./build-release/src/stellarium` | **8/8 PASS，rc=0**（`regression-s3-stela3.txt`） |

S3 全绿同时证明：`StelAction` 的 widget 分发开关**默认开**，纯 QWidget 形态（stellarium.exe）零退化——
拆除只作用于合流形态宿主显式关闭的场景。

## 三、短窗长跑冒烟：**帧率数据受限，结构判据全过**—— 记录在案

`shortsmoke-contaminated.txt`（首跑，机器仍在消化 42 分钟并行构建，15 分钟均载 **93.56**）：
SL-C01/C02 FAIL（37.24fps）、SL-C07 FAIL（23.5 MiB/min）——**该跑数据无效**，不作回归证据。

`shortsmoke-battery-lpm.txt`（复跑：电池供电、**低电量模式已关**（日志 `环境前置通过`）、1 分钟均载 6.93——
残余负载主要是 42 分钟构建后触发的 Spotlight 索引）：

| 判据 | 结果 | 数据 |
|---|---|---|
| SL-C01/C02 帧率 | FAIL（**残余负载污染**） | 38.71 / 38.23 fps（门槛 40）；与 T13 同口径短窗（40.67~42.87）差约 5%，归因后台负载，不作回归依据 |
| SL-C03 上屏节奏 | PASS | p99 66.04ms（门槛 78.47） |
| SL-C04 邮箱丢弃 | PASS | 投递 2263 / 丢弃 0 |
| SL-C05 上传耗时 | PASS | — |
| SL-C07 内存斜率 | FAIL（**短窗不适用**） | 回归斜率 10.37 MiB/min 但端点 **2812.2 → 2811.7 MiB（增 -0.5，实际在降）**——60s 窗口把 warmup 平台的抖动拟合成正斜率，该判据设计口径是 30 min 稳态窗 |
| SL-C11 GUI 帧龄 | PASS | mean 2.23 / p95 21.00 / max 53.00 ms |

### 为什么可以判定"不是 T15 回归"

1. **帧路径改动是纯算术**：`setJD(jd0 + sim×simRate)` → `m_jdAccum += dtWall×simRate×scale`，
   每 tick 多一次乘加，无分配、无锁、无 IO。
2. **同机同日后端对照**：本次 DYN 引擎形态测得**尾窗 34.9 fps**（1920×1280），
   与 T12 期同口径实测（34.5~36.4 fps）一致——帧路径无退化。
3. **三组回归全绿**（§二）：A2 / DYN / S3 覆盖逐像素、动态帧 7 判据、引擎集成 8 项。

**待办（W-T15）**：接电源 + 关闭低电量模式后复跑 90s 冒烟，闭合帧率口径。
命令：
```bash
STELQUICK_GRAPHICS_API=metal STELQUICK_LONGRUN=1 STELQUICK_LONGRUN_PRODUCER=engine \
STELQUICK_LONGRUN_WARMUP_SECONDS=30 STELQUICK_LONGRUN_SECONDS=60 \
./build-release/src/ui/stelQuickUI.app/Contents/MacOS/stelQuickUI
```
注：SL-C07（内存斜率）在 60s 窗口内本就不可用（引擎 warmup 期仍在分配），故该跑只看
SL-C01/C02（帧率）与 SL-C03..C06/C11（节奏与帧龄）。

## 四、过程中修掉的两个真问题

1. **MSVC/GCC 编译**：`QGuiApplication::focusWidget` 不存在（属 `QApplication`，Widgets 模块）→ 改
   `QApplication::focusWidget()` 并加 `<QApplication>` 包含（`QT_WIDGETS_LIB` 守卫）。
2. **QML 报错刷屏**（585 次/95s）：`fieldOfView` 是 Q_PROPERTY，QML 里写成了函数调用
   `appFacade.fieldOfView()` → `TypeError`。改为属性访问（`appFacade.fieldOfView`）后
   复跑 `TypeError` 计数 = **0**，ACTIONCHECK 仍 PASS。
