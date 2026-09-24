# Windows 30 分钟正式长跑（合流形态 × 原生 Vulkan）证据

> 日期：2026-09-24 08:36–09:23（本机时间）
> 机器：`desktop-0pji1so`（AMD Ryzen 7 3700X + **NVIDIA GeForce RTX 4060**，驱动 591.74）
> 形态：`stelQuickUI.exe`（合流：引擎 OpenGL 3.3 core + QML **原生 Vulkan** RHI 同进程）
> 代码：`main@0f2faae`（T14 审计提交；T15 命令通路改动尚未同步到 Windows）
> 投递方式：`schtasks /it` 到交互桌面会话（SSH 会话无桌面，场景图不初始化）

## 结论：**VERDICT=PASS，rc=0，SL-C01..C11 全绿**

| 判据 | 结果 | 关键数据 |
|---|---|---|
| SL-C01 生产者零失败 | **PASS** | 测量段渲染 **89950 帧 / 失败 0**；稳态实测 **50.00 fps**（下限 40） |
| SL-C02 稳态显示帧率 | **PASS** | 50.00 fps；显示帧号 75033 → 134933（窗口 1198s） |
| SL-C03 上屏间隔（共线程口径） | **PASS** | mean 20.00 / p50 20.04 / p95 23.16 / **p99 24.58** / max 36.61 ms（门槛 p99≤60、max 档占比 ≤0.01%） |
| SL-C04 邮箱丢弃 | **PASS** | 投递 89950 / **丢弃 0**（须 0） |
| SL-C05 上传耗时 | **PASS** | 89950 次，增量均值 **0.590ms**（<5.0），最坏 14.53ms（<100） |
| SL-C06 邮箱帧龄 | **PASS** | 最大 15ms（<500） |
| SL-C07 内存斜率 | **PASS** | phys_footprint **0.083 MiB/min**（上限 1.0）；窗口内 1457.3 → 1458.8 MiB（+1.4） |
| SL-C08 环境漂移 | **PASS** | 每 60s 复核，**0 次违规** |
| SL-C09 窗口暴露 | **PASS** | 1799/1799 采样点 |
| SL-C10 降级误报 | **PASS** | 0/1799（**0.000%**，上限 1%） |
| SL-C11 GUI 线程卡顿（T13 新增） | **PASS** | 帧龄（10971 个 100ms 样本）mean 6.32 / **p95 14.00** / p99 15.00 / max 17.00 ms（门槛 p95≤100） |
| **设备丢失** | **0 次** | 与 T13 判定性实验一致——MoltenVK 专属缺陷在原生 Vulkan 上不复现 |

配置：1280×720，名义 50 fps，simRate 0.02 天/秒，起点 JD 2461307.52582，暖机 900s + 测量 1800s。

## 与 macOS（MoltenVK）T13 长跑的对照

| 维度 | macOS + MoltenVK（T13 第二战） | Windows + 原生 Vulkan（本跑） |
|---|---|---|
| 形态 | Metal RHI 跑合流（Vulkan 组合必丢设备） | **Vulkan RHI 跑合流，零设备丢失** |
| 稳态 fps | 40.30（FBO 4096 档） | **50.00**（名义即达成，1280×720） |
| 帧龄 p95（GUI 卡顿） | 14.00 ms | 14.00 ms（无差异） |
| 内存斜率 | 0.141 MiB/min | 0.083 MiB/min |
| 设备丢失 | —（Metal 配置下为 0；Vulkan 配置 4/4 丢） | **0** |

意义：**交付战场（原生 Vulkan）首次跑满 30 分钟正式长跑并全绿**，与 Mac 开发机结论一致但少了 MoltenVK 这一层风险。

## 文件清单

| 文件 | 内容 |
|---|---|
| `t13-win-30min.txt` | 全量日志（UTF-8 转码自 UTF-16LE） |
| `t13-win-30min.csv` | 逐秒 CSV（2699 行：fps/内存/暴露/降级/上传…） |
| `t13-win-30min.frames.csv` | 逐帧上屏 CSV（134981 行） |
| `t13-win-30min.rc.txt` | 退出码（0） |

## 环境备注（复用价值）

- SSH 跑 GUI 程序场景图不初始化 → `schtasks /create /it` + `/run` 投递到登录会话；
- 启动前 PATH 需含 Qt bin + Vulkan Bin + `util\spout2\x64`（SpoutLibrary.dll），否则 0xC0000135；
- PowerShell 5.1 `*>` 重定向为 UTF-16LE，归档需按 BOM 转码。
