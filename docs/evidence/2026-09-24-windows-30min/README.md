# Windows 30 分钟正式长跑（合流形态 × 原生 Vulkan）证据

> 本目录含**两次**独立长跑：
> - **W-T13**（§1，`main@0f2faae`）—— 首次在交付战场（原生 Vulkan）跑满 30 分钟；
> - **W-T17**（§2，`main@6bce85d`）—— 补上 T15/T16/T17 同步后的复验欠账。
> 两次判据项完全相同（SL-C01..C11），可直接并排比。

---

## 1. W-T13：首次 30 分钟（合流形态 × 原生 Vulkan）

> 日期：2026-09-24 08:36–09:23（本机时间）
> 机器：`desktop-0pji1so`（AMD Ryzen 7 3700X + **NVIDIA GeForce RTX 4060**，驱动 591.74）
> 形态：`stelQuickUI.exe`（合流：引擎 OpenGL 3.3 core + QML **原生 Vulkan** RHI 同进程）
> 代码：`main@0f2faae`（T14 审计提交；T15 命令通路改动尚未同步到 Windows）
> 投递方式：`schtasks /it` 到交互桌面会话（SSH 会话无桌面，场景图不初始化）

### 1.1 结论：**VERDICT=PASS，rc=0，SL-C01..C11 全绿**

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

### 1.2 与 macOS（MoltenVK）T13 长跑的对照

| 维度 | macOS + MoltenVK（T13 第二战） | Windows + 原生 Vulkan（本跑） |
|---|---|---|
| 形态 | Metal RHI 跑合流（Vulkan 组合必丢设备） | **Vulkan RHI 跑合流，零设备丢失** |
| 稳态 fps | 40.30（FBO 4096 档） | **50.00**（名义即达成，1280×720） |
| 帧龄 p95（GUI 卡顿） | 14.00 ms | 14.00 ms（无差异） |
| 内存斜率 | 0.141 MiB/min | 0.083 MiB/min |
| 设备丢失 | —（Metal 配置下为 0；Vulkan 配置 4/4 丢） | **0** |

意义：**交付战场（原生 Vulkan）首次跑满 30 分钟正式长跑并全绿**，与 Mac 开发机结论一致但少了 MoltenVK 这一层风险。

### 1.3 文件清单（W-T13）

| 文件 | 内容 |
|---|---|
| `t13-win-30min.txt` | 全量日志（UTF-8 转码自 UTF-16LE） |
| `t13-win-30min.csv.gz` | 逐秒 CSV（2699 行：fps/内存/暴露/降级/上传…） |
| `t13-win-30min.frames.csv.gz` | 逐帧上屏 CSV（134981 行） |
| `t13-win-30min.rc.txt` | 退出码（0） |

---

## 2. W-T17：同步 T15/T16/T17 后的复验长跑

> 日期：2026-09-24 **12:16:32 – 13:01:45**（本机时间）
> 机器：同 `desktop-0pji1so`（Ryzen 7 3700X + RTX 4060）
> 形态：`stelQuickUI.exe`（合流：引擎 OpenGL 3.3 core + QML **原生 Vulkan** RHI 同进程）
> 代码：`main@6bce85d`（T17 提交——**本跑正是"T15/T16/T17 已在 Windows 侧复验"的凭据**）
> 投递方式：`schtasks /create /tn StelQuickT18LongRun /it` + `/run`（同 W-T13）

### 2.1 为什么要有这一跑

T13 的 30 分钟长跑用的是 `main@0f2faae`，之后 T15（命令通路）、T16（单一仿真时钟，
拆掉重复渲染驱动源，帧率 +43%）、T17（两个模型）都只在本机 macOS 上验过。
Windows 是交付战场，且 T16 动的是**帧率相关**的核心，所以"同步后重跑一次同样剂量的
长跑"是必须结清的欠账——不能拿 Mac 的结论代替。

### 2.2 结论：**VERDICT=PASS，rc=0，SL-C01..C11 全绿**

| 判据 | 结果 | 本跑数据 | 与 W-T13 对比 |
|---|---|---|---|
| SL-C01 生产者零失败 | **PASS** | 测量段渲染 **89950 帧 / 失败 0**；稳态 **50.00 fps**（下限 40） | 帧数完全相同（两次都是精准 50fps × 1799s） |
| SL-C02 稳态显示帧率 | **PASS** | **50.00 fps**；显示帧号 75034 → 134934（窗口 1198s） | 一致 |
| SL-C03 稳态上屏间隔 | **PASS** | mean 20.00 / p50 19.98 / p95 22.19 / **p99 33.05** / max 58.24 ms | p95 22.19→22.19 持平；p99 24.58→33.05（仍远低于 60 门槛） |
| SL-C04 邮箱丢弃 | **PASS** | 投递 89950 / **丢弃 0**（须 0） | 一致 |
| SL-C05 上传耗时 | **PASS** | 89864 次，增量均值 **0.595ms**（<5.0），最坏 13.84ms（<100） | 0.590→0.595，无变化 |
| SL-C06 邮箱帧龄 | **PASS** | 最大 **5ms**（<500） | 15→5，更好 |
| SL-C07 内存斜率 | **PASS** | **0.091 MiB/min**（上限 1.0）；1410.8 → 1412.2 MiB（+1.5） | 0.083→0.091，同量级 |
| SL-C08 环境漂移 | **PASS** | 每 60s 复核，**0 次违规** | 一致 |
| SL-C09 窗口暴露 | **PASS** | **1799/1799** 采样点 | 一致 |
| SL-C10 降级误报 | **PASS** | **0/1799（0.000%）** | 一致 |
| SL-C11 GUI 线程卡顿 | **PASS** | 帧龄（10971 个 100ms 样本）mean 2.57 / **p95 9.00** / p99 12.00 / max 17.00 ms | p95 14.00→9.00，更好 |
| **设备丢失** | **0 次** | 日志内 grep 不到 `vkDebug` / `VK_ERROR` / `device lost` / `swapchain` | 一致 |

配置：1280×720，名义 50 fps，simRate 0.02 天/秒，**暖机 900s + 测量 1800s**。

逐秒 CSV 独立复核（`t17-win-30min.csv.gz`，2699 行 = 900 暖机 + 1799 测量）：

| 项 | 值 |
|---|---|
| 测量段 `exposed=0` 的采样点 | **0**（窗口全程暴露） |
| 测量段 `degraded=1` 的采样点 | **0** |
| 测量段 `producer_fps` 均值 / 最小 | **50.00 / 48.73** |

### 2.3 意义

- **T16 的"拆掉重复渲染驱动源"在交付战场同样成立**：稳态 50.00 fps 精准达成、
  上屏间隔 mean 20.00ms（= 1/50s），且三项抖动指标（帧龄 p95、上传均值、内存斜率）
  都不比 T13 差。
- 与 §1 并排看：**判据口径完全一致**，所以"零退化"这句话在 Windows 侧是有凭据的，
  不是"换台机器换个口径"。

### 2.4 文件清单（W-T17）

| 文件 | 内容 |
|---|---|
| `t17-win-30min.txt` | 全量日志（UTF-8 恢复，见 §3 编码警告） |
| `t17-win-30min.csv.gz` | 逐秒 CSV（2699 行：`t_s,phase,displayed_frames,published,dropped,mailbox_age_ms,upload_count,upload_sum_us,upload_max_ms,rss_kb,footprint_kb,exposed,degraded,producer_fps,producer_rendered,producer_failed`） |
| `t17-win-30min.frames.csv.gz` | 逐帧上屏 CSV（134980 行） |
| `t17-win-30min.rc.txt` | 退出码（0） |

---

## 3. 环境备注（复用价值）

- SSH 跑 GUI 程序场景图不初始化 → `schtasks /create /it` + `/run` 投递到登录会话；
- 启动前 PATH 需含 Qt bin + Vulkan Bin + `util\spout2\x64`（SpoutLibrary.dll），否则 0xC0000135；
- `tools/t18-win-longrun.ps1` 把 T13 那次**临时手写、用完即丢**的长跑 wrapper 固化进仓库，
  参数化（`-WarmupSeconds` / `-MeasureSeconds`）以便先跑 8 秒冒烟验证管线本身。

### ⚠️ 日志编码：`*>>` 重定向比"UTF-16LE"更麻烦（W-T17 实测修正）

W-T13 的备注只写了"PowerShell 5.1 `*>` 为 UTF-16LE，归档需按 BOM 转码"。W-T17 实测发现
更细的一层，**照 W-T13 的做法会得到乱码**：

| 现象 | 说明 |
|---|---|
| 文件**不是**以 BOM 开头 | 前 66 字节是 wrapper 自己写的 ASCII 头（`[start] … warmup=900s measure=1800s\r\n`），之后才是**无 BOM** 的 UTF-16LE 正文 |
| 正文里的中文**双重错位** | exe 输出 UTF-8，PowerShell 按 **CP936** 解码后再写成 UTF-16LE。且同一条 `STELLRUN:` 行内**混排**了两种来源（例：`环境前置通过（` 是 UTF-8 被误读，而 `桌面平台：无电池供供电…` 是 CP936 原样保留） |
| 后果 | 直接 `decode('utf-16-le')` 得到 `鐜鍓嶇疆閫氳繃`；再 `encode('gbk').decode('utf-8')` 会因个别字符不在 GBK 反查表而抛错 |

**可用的恢复配方**（本次归档所用）：

```python
d = open('raw.txt','rb').read()
cut = d.find(b'\r\n') + 2                       # ASCII 头到此为止
head = d[:cut].decode('ascii', errors='replace')
body = d[cut:].decode('utf-16-le', errors='replace')
fixed = body.encode('gb18030', errors='replace').decode('utf-8', errors='replace')
open('out.txt','w',encoding='utf-8').write(head + fixed)
```

用 `gb18030`（GBK 超集）而不是 `gbk`，否则会 `illegal multibyte sequence` 中断。
即便如此，个别字符仍不可复原（原始解码阶段已被替换掉）——**判据 ID、`PASS/FAIL`、
所有数值都是 ASCII，完全不受影响**，这也是判据可读的根本原因。

**下次的更好做法**：投递脚本里把 exe 输出显式转成 UTF-8 落盘，例如
`& $exe 2>&1 | Out-File -Encoding utf8 $out`，
或先 `[Console]::OutputEncoding = [Text.UTF8Encoding]::new()` 再重定向。
