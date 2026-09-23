# T13 合流形态长跑 — 证据索引（2026-09-23）

**状态**：代码就位并验证（test 形态 11/11 PASS），**engine 形态正式 30 min 长跑未完成**
——被环境内存不足阻塞。本目录是当时全部原始输出，按时间顺序编号。

判据定义见工作区测试文档 §6.9（B-LSR3-01..09）；叙述见 `docs/BUILD_RECORD.zh_CN.md` 的 T13 节。

## 一、成功的基线跑（短窗 120s 预热 + 180s 测量，各 5 分钟）

| 文件 | 内容 |
|---|---|
| `01-engine-short-120s+180s-PASS.log` | **engine 生产者**（真实引擎，共 GUI 线程）短窗。SL-C01..C11 **除 SL-C03 外全绿**；SL-C03 按当时的绝对门槛 FAIL（p99 60.27 > 50），**正是本次促成口径修正的直接证据** |
| `01-engine-short.csv.gz` | 逐秒采样（300 行）；`01-engine-short.frames.csv.gz` 逐帧上屏间隔（12405 行） |
| `02-test-short-120s+180s-PASS.log` | **test 生产者**（替身场景，独立线程）短窗 **11/11 PASS rc=0**——证明 Producer 抽象改造未破坏既有路径，并给出两形态对照基线 |
| `02-test-short.csv.gz` / `02-test-short.frames.csv.gz` | 同上口径 |
| `03-engine-short-nominal60.log` + `.csv.gz` | engine 生产者名义速率提到 **60**（间隔 16.7ms < 单帧耗时）——**没有改善**（40.67 fps），证明瓶颈是引擎单帧耗时而非 timer 间隔。⇒ 43 fps 是当前线程模型的产能档 |

**两形态对照（稳态窗，本次核心数据）**：

| 指标 | test（独立线程） | engine（共 GUI 线程） |
|---|---|---|
| 稳态显示 | 54.91 fps | 42.87 fps |
| 上屏间隔 p50 / p95 / p99 | 16.71 / 33.77 / 35.37 ms | 21.35 / 53.60 / 60.27 ms |
| 邮箱帧龄 p95 | 18.00 ms | 11.00 ms |
| footprint 峰值 | 282 MiB | **3152 MiB** |
| 邮箱丢弃 | 0 | 0 |

## 二、失败的跑（GPU 设备丢失，环境内存不足）

| 文件 | 内容 |
|---|---|
| `04-engine-short-GPU-DEVICE-LOST.log` | engine 短窗**重跑**：预热最初几秒 `VK_ERROR_OUT_OF_DEVICE_MEMORY` + `kIOGPUCommandBufferCallbackErrorPageFault`，`SIGABRT(134)`，逐秒 CSV **0 行** |
| `05-neg-stall-GPU-DEVICE-LOST.log` | 负控（`STELQUICK_LONGRUN_STALL_MS=150`）同样 GPU 设备丢失。**注意：该负控本身会长时间占住 GUI 线程，在内存紧张环境下不建议使用**——可红性证据待补 |
| `06-probe-dyn-engine-same-crash.log` | **判别证据**：走 **T12 既有路径**（`STELQUICK_DYN_CHECK=1` + `STELQUICK_DYN_PRODUCER=engine`，该分支代码本次未改动）在同一时段**同样崩溃、同一位置** ⇒ 与 T13 改动无关，是环境问题 |
| `07-engine-short-RETRY-still-GPU-DEVICE-LOST.log` | 内存部分回落（unused 92M→806M、compressor 7.0G→5.5G）后**再试一次**：仍在引擎初始化阶段设备丢失（6 处记录）。⇒ **806M 不足以承载 3.1GiB 需求**，须真正释放内存后再跑 |

**环境读数（崩溃时段）**：`PhysMem: 15G used / 92M unused`、`compressor 6740M`、
`swap used 2165M`。两次**成功**的 engine 跑是在 `free 1479M / compressor 5073M` 时。
⇒ engine 形态需约 **3.1 GiB 连续可用内存**（test 形态仅 282 MiB）。

## 三、复现命令

```bash
cd /Users/ztuqfvy/qt_demo/stellarium_vulkan          # 必须从仓库根启动（installDir="."）
caffeinate -dimsu env \
  STELQUICK_LONGRUN=1 STELQUICK_LONGRUN_PRODUCER=engine \
  STELQUICK_LONGRUN_WARMUP_SECONDS=900 STELQUICK_LONGRUN_SECONDS=1800 \
  STELQUICK_LONGRUN_CSV=/tmp/t13-live.csv \
  build-release/src/ui/stelQuickUI.app/Contents/MacOS/stelQuickUI
# 判读：rc 0=PASS / 8=FAIL / 9=环境拒绝或数据无效(INVALID) / 6=装配失败
```

前置（§6.1.1 协议第 4/5 条）：AC 电源、低电量模式关、屏保关、机器闲置、
**内存充足**（合流形态新增，见上）；长跑须用独立会话启动（脱离进程组），
避免被会话轮次边界清理。
