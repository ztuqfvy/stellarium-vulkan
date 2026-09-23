# Windows 阶段 1 证据（2026-09-23，第二轮验证）

> 远程执行通道：UU远程 SSH（Mac → Windows 10，desktop-0pji1so，
> Ryzen 7 3700X + RTX 4060）。GUI 程序经 schtasks `/it` 投递到交互桌面会话运行。

## 环境

| 项 | 值 |
|---|---|
| GPU | NVIDIA GeForce RTX 4060 |
| Vulkan API | 1.4.325（SDK header 357，E:\Vulkan\SDK） |
| 驱动 | NVIDIA 591.74（探针解码显示 79.296.0，同一驱动的两种编码口径） |
| 编译器 | MSVC，VS 18（2026 系，E:\VisualStudio\CanPin），CMake 4.3.1（VS 自带） |
| Qt | 6.11.2 msvc2022_64（E:\Qt） |
| 代码 | da692f0（ee8b2da + NOMINMAX 修复） |
| 生成器 | Visual Studio 18 2026，x64，Release |

## 构建首战修一道：NOMINMAX（da692f0）

首次 MSVC 构建在 `SkyLongRun.cpp(186/623/625)` 报 C2589/C3878/C2760——
`windows.h` 的 `min`/`max` 宏击穿 `std::min/std::max`。修复：两个长跑文件
`#define NOMINMAX` 后再 include windows.h。macOS 侧零影响（Q_OS_WIN 分支）。

## 结果

| 组 | 内容 | 返回码 | 判定 |
|---|---|---|---|
| 1a | A2 静态纹理逐像素（Vulkan） | 0 | **PASS**：12 探针 0 失败，期望色与实测全等，抓帧 `a2_vulkan_r2.png` |
| 1b | DYN 动态帧自检（Vulkan） | 0 | **PASS 7 判据 0 失败**（C06 按已知缺陷 SKIP）：稳态 59.8 fps、丢弃 0、首帧 187ms、上传 mean 0.84ms max 1.29ms |
| 1c | DYN 动态帧自检（d3d11） | 8 | **FAIL 1/7**：D1-C01 35.0 fps < 门槛 36.0；复跑 33.4 fps 仍 FAIL |

## 1c d3d11 复跑定性：非方差，是后端真实帧率

复跑 33.4 fps，与首跑 35.0 同档（±2）。同机 Vulkan 59.8 fps。其余 6 判据全过
（显示、丢弃 0、首帧 199ms、上传 max 1.59ms、C06 逐像素 PASS、降级联动 PASS）。

特征：~30ms 帧时间 ≈ 两个 vsync 周期，符合 D3D11 交换链（双缓冲 + SyncInterval=1）
排空节奏——CPU 排队不够深时呈"隔一个 vblank 上屏"模式。**d3d11 是兜底后端非交付
目标，此发现记档不阻塞**；若后续要把 d3d11 转正，需先解决交换链排队深度。

## SSH 会话跑 GUI 的坑（方法论）

SSH 直跑 GUI 程序：窗口不在交互会话，场景图 5s 不初始化 → A2CHECK=UNAVAILABLE（rc=6）。
解法：`schtasks /create /it` + `/run` 投递到已登录桌面会话。回传走 `*> log` +
`echo %errorlevel% > rc`（PowerShell 5.1 重定向默认 UTF-16LE，归档时需转 UTF-8）。

## 文件清单

- `r2_1a_a2.txt` / `r2_1a.rc.txt` — 1a A2 Vulkan 日志与返回码
- `r2_1b_dyn.txt` / `r2_1b.rc.txt` — 1b DYN Vulkan
- `r2_1c_d3d11.txt` / `r2_1c_d3d11_retry.txt` / `r2_1c.rc.txt` / `r2_1c_retry.rc.txt` — 1c 两次
- `a2_vulkan_r2.png` — A2 抓帧（960x640，渲染后端角标 Vulkan）
- `vulkaninfo_summary.txt` — vulkaninfo --summary（UTF-8 转码）；`_raw.txt` 原始字节
