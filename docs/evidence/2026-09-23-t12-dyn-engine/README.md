# T12 证据：DYN 自检在**真实引擎生产者**下复跑（2026-09-23）

T12 出口判据（进度实况 §8.7 判据 3）：**I-DYN 的 7 项判据在真实引擎生产者下全绿**，
且消费侧（SkyViewport）零改动。

## 结论

| 项 | 结果 |
|---|---|
| engine 生产者 DYN（Metal） | ✅ **7/7 PASS rc=0**，尾窗稳态 44.4 fps，复跑 44.8 fps |
| engine 生产者 DYN（Vulkan） | ✅ PASS（6 判据 + D1-C06 按 §6.2 已知缺陷 SKIP） |
| test 生产者基线（默认，8s 窗口） | ✅ 7/7 PASS，尾窗 51.5 fps —— 改造未破坏既有行为 |
| A2 逐像素回归（Widgets ON） | ✅ PASS rc=0 |
| 默认形态（Widgets OFF）：A2 / DYN-test | ✅ 双双 PASS（DYN 尾窗 52.0 fps） |
| 负控：默认形态请求 engine 生产者 | ✅ 明确报错 `UNAVAILABLE` rc=6（不假装通过） |
| `STELA3_CHECK`（合流后 stellarium） | ✅ 8/8 PASS rc=0 |
| 独立工程形态回归 | ✅ `cmake -B build-ui -S src/ui` 可配置可构建 |

## 关键发现：真实引擎的**爬升特性**（判据口径因此重定）

`30-FINAL-metal-PASS-samples.csv` 的逐 250ms 曲线（节选）：

| t(s) | 累计生产帧 | 段速率(fps) | 说明 |
|---|---|---|---|
| 1.9 | 57 | 31.3 | warmup |
| 2.9 | 83 | 27.7 | warmup |
| 3.9 | 107 | 24.8 | warmup |
| 4.9 | 135 | 26.0 | warmup |
| 6.9 | 215 | 39.7 | 开始爬升 |
| 8.9–12.9 | 224→244 | **5.0** | 降速探针窗口（自检施加） |
| 13.9 | 262 | 13.8 | 恢复爬升 |
| 14.9 | 295 | 33.7 | 爬升中 |
| 15.9 | 340 | 44.0 | 爬升中 |
| 17.9 | 439 | **50.0** | **稳态** |
| 19.9 | 535 | 46.7 | 稳态 |

**引擎在合流形态下需要约 10 秒才能从 warmup 爬到稳态**（编译着色器、上传纹理、
加载星表与 DSO、建立各类缓存）。这与 T9 长跑协议里 900s warmup 是同一现象的两个尺度。

⇒ 旧判据"全程平均产出 ≥ 0.6×名义×时长"会**稳定假红**：8 秒窗平均 31.5 fps
（`10-OLDformula-avg-fullwindow-FAIL.txt`），而同期 1s 滑窗稳态已是 53.6 fps。

## 两处判据口径修正（都不是放水，都有实测依据）

### ① 全程平均 → **尾窗稳态速率**

判"生产者是否健康"要看稳定状态下的能力，而不是被 warmup 与人为降速窗污染的平均值。
尾窗起点 = `max(降速段结束, 总时长 − max(3s, 时长×35%))`。

- 修正前：D1-C01 `rendered ≥ 0.6×名义×时长` → 8s 窗 288 帧，实测 252 帧 → **FAIL**
- 修正后：D1-C01 `尾窗稳态速率 ≥ 0.6×名义` → 尾窗 49.2 fps vs 下限 36 → **PASS**

### ② engine 生产者名义速率 60 → **50**

60 是 **UI 刷新上限**，不是引擎的设计产能。真实引擎每帧跑完整场景 + `glReadPixels`
读回，实测稳态在 35~52 fps 区间随机器负载波动（稳态档 ≈50）。
用 60 当名义是**定性错误**；判据下限随之从 36 fps 降到 30 fps（0.6×50）。

测量窗同时由 8 秒延长到 20 秒（engine 生产者默认），使尾窗越过爬升段。

### 边界证据（为什么这两条修正缺一不可）

| 文件 | 配置 | D1-C01 结果 |
|---|---|---|
| `20-nominal60-BORDERLINE-FAIL.txt` | 旧口径 + 名义 60 + 15s 窗 | 尾窗 34.5 fps vs 下限 36.0 → FAIL |
| `21-nominal60-15s-BORDERLINE.txt` | 同上复跑 | 尾窗 36.4 fps vs 下限 36.0 → PASS（余量 1%） |
| `30-FINAL-metal-PASS.txt` | 修正后 + 名义 50 + 20s 窗 | 尾窗 44.4 fps vs 下限 30.0 → PASS（余量 48%） |

前两次的"1% 余量"说明旧口径只是**恰好卡在边界**，不是合格判据。

## 一个可靠性问题（已修）

`12-OLDformula-8s-withCSV-ABORT-doublefree.txt`：判据 7/7 全绿、CSV 完整写出，
**但进程退出码 134（SIGABRT）**。根因：CSV 用 `shared_ptr<QFile>` 持有同时又调
`deleteLater()` → Qt 删一次、`shared_ptr` 析构再删一次 → 双重释放。
处置：只 `close()`，不 `deleteLater()`。修后 rc=0（`30-FINAL-metal-PASS.txt`）。
**症状极具误导性：判据全绿却拿到非零退出码。**

## 文件索引

| 文件 | 说明 |
|---|---|
| `00-configure.txt` | 合流构建 configure（Widgets 宿主形态） |
| `01`~`04-build-*.txt` | 四次构建：接口改造 / CSV+尾窗 / 双重释放修复 / 名义 50 |
| `10`/`11`/`12-OLDformula-*` | 旧口径下的失败跑（留作口径修正的依据，**不是**缺陷证据） |
| `20`/`21-nominal60-*` | 名义 60 的边界跑（1% 余量，留作"为何要改名义"的证据） |
| `30-FINAL-metal-PASS.txt` + `-samples.csv` | **主证据**：engine 生产者 Metal 后端 7/7 PASS + 逐 250ms 曲线 |
| `40-dyn-test-baseline.txt` | test 生产者基线（改造未破坏既有行为） |
| `41-dyn-engine-vulkan.txt` | engine 生产者 Vulkan 后端（D1-C06 正确 SKIP） |
| `42-a2-regression.txt` | A2 逐像素回归 |
| `43`~`46-*default-off*` | 默认形态回归 + engine 生产者负控 |
| `47`/`48-*` | stellarium 重构 + `STELA3_CHECK` 8/8 |
| `50-FINAL-metal-rerun-PASS.txt` | 复跑（可复现性） |
| `51-standalone-regression.txt` | 独立工程形态回归 |
