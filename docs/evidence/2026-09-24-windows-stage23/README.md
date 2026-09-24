# Windows 阶段 2/3 证据（2026-09-24，第二轮验证收官）

> 判定性问题：**「引擎(OpenGL) × QML(Vulkan) 同进程丢设备」是 macOS/MoltenVK 专属，还是 Qt 通用缺陷？**
> **答案：MoltenVK 专属，Qt 无辜。** 原生 Vulkan（RTX 4060）+ 真实引擎同进程，
> 短窗长跑 11/11 全绿、0 次设备丢失。风险项关闭，Vulkan 交付路径确认畅通。

## 阶段 2：引擎根 MSVC 构建（首次成功）

代码 `98dee2c`。四道坎，全部远程解决：

| # | 坎 | 解法 |
|---|---|---|
| 1 | 缺上游资产目录（data 等 12 个，gitignore） | Windows 直克隆上游 + junction 替代软链 + data 实体副本 |
| 2 | `ADD_SUBDIRECTORY(Planes)` 不存在 | 资产 vintage 错误——见下，checkout 对齐后 Planes 自然而然存在 |
| 3 | 插件调用 `StelApp::getFlagUseDecDegreesCoords` 等 API 不存在 | **上游 vintage 錯配**：fork core 是 5-24~8-14 之间快照，Sep-17 master 已拆分该 API。用 blob 指纹（`git log --find-object`）交叉定窗，checkout `22c8f8ed`（2026-07-10，多文件窗口交点） |
| 4 | `src/ui/main.cpp` 裸引 `<unistd.h>`（MSVC 无此头，实测未用 POSIX 符号） | `98dee2c`：平台守卫 `io.h` |

**vintage 定位方法论**（可复用）：fork 的 core 文件取 blob 哈希 → 上游仓库
`git log --find-object=<blob>` 得每文件存续区间 → 多文件区间取交点 → checkout 交点内 commit。
本次：StelApp.hpp [5-24, 8-14) ∩ StelApp.cpp [5-24, 6-26) ∩ StelCore.hpp [6-05, 7-10) ∩ SkyGui.hpp [5-02, 8-21) → **6-26 ~ 7-10**，取 7-10 前最后 commit `22c8f8ed`。

上游克隆：`E:\Qt_demo\stellarium-upstream`（--filter=blob:none --shallow-since=2026-09-05 后加深到 5 月）。
构建：VS 自带 CMake 4.3.1，生成器 "Visual Studio 18 2026"，`ENABLE_STELQUICKUI=ON STELQUICKUI_WIDGETS_HOST=ON`。
产物：`build-win\src\Release\stellarium.exe` + `build-win\src\ui\Release\stelQuickUI.exe`（各 26.4 MB）。

**运行时 DLL 坑**：合流 exe 依赖 `SpoutLibrary.dll`（在 `util\spout2\x64\`，须加 PATH），
PATH 模板：`E:\Qt\6.11.2\msvc2022_64\bin;E:\Qt_demo\stellarium-vulkan\util\spout2\x64;E:\Vulkan\SDK\Bin`。

## 阶段 3：判定性实验（3a + 3b）

### 3a DYN 真实引擎生产者（Vulkan）

rc=8，**FAIL 1/7，但关键项全绿**：
- D1-C01 PASS 稳态 40.9 fps（门槛 30）、C02 显示 PASS、C03 published=632 dropped=0、C05 上传 max 1.60ms、C07 降级联动 PASS
- **0 次设备丢失**
- D1-C04 FAIL：首帧流水建立 **1125ms**（门槛 500，Mac 187ms）——启动性能差异（引擎 boot 慢：星表/插件初始化），非共存缺陷。记为已知偏差，后续可单独优化。

### 3b 短窗长跑（Vulkan，热身 120s + 测量 180s）

**rc=0，11/11 PASS**：

| 判据 | 结果 |
|---|---|
| SL-C01 帧流 | 9000 帧 / 0 丢弃，50.00 fps |
| SL-C02 显示 | 50.00 fps（显示 8983 帧 / 生成 14933 帧窗） |
| SL-C03 帧龄 | mean 20.00 / p50 20.06 / p95 23.07 / p99 24.43 / max 34.87 ms |
| SL-C04 生成失败 | 0 |
| SL-C05 上传 | mean 0.595ms / max 7.74ms |
| SL-C06 邮箱帧龄 | 16ms |
| SL-C07 内存斜率 | 0.377 MiB/min（<1.0；1455.4→1455.8 MiB） |
| SL-C08 设备丢失 | **0 次** |
| SL-C09/10 降级 | 180/180 采样正常，0/180 降级 |
| SL-C11 帧龄采样 | p95 15ms（1097 样本） |

注：50fps 说明该机显示器为 50Hz（vsync 基准不同，判据为相对口径照常工作）。

## 文件清单

- `r2_3a_dyn_engine.txt` / `r2_3a.rc.txt` — 3a 日志与返回码
- `r2_3b_longrun.txt` / `r2_3b.rc.txt` — 3b 日志与返回码
- `t13-win.csv` / `t13-win.csv.frames.csv` — 3b 逐窗与逐帧数据
- `configure_win_r2.txt` / `build_win_r2.txt` — 最终成功轮的配置与构建日志
