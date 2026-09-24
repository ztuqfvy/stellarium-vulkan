# T18 证据包 — 定位与跟踪（A4 固定流程的"定位环"）

日期：2026-09-24 · 提交：见仓库 `T18` 提交 · 交付文档：`docs/T18_LOCATE_TRACK.zh_CN.md`

一键复跑：`./tools/t18-verify.sh all`（或 `core` / `regress` / `dyn <N>` 分跑）

---

## 1. 判据总览

| # | 项目 | 文件 | rc | 结论 |
|---|---|---|---|---|
| ① | T18 定位/跟踪自检（C++ 侧） | `locatecheck-mac.txt` | 0 | **14/14 PASS**（LOC-01..08 + fixture/info/note） |
| ①b | T18 **UI 层端到端**自检（最外层注入） | `uicheck-mac.txt` | 0 | **8/8 PASS**（UI-01..08；真实鼠标事件 → 引擎状态改变） |
| ② | T16 时钟纯逻辑自检（回归） | `regression-clockcheck.txt` | 0 | 12/12 PASS（ST-01..12） |
| ③ | T15/T16 命令通路 + 集成自检（回归） | `regression-actioncheck.txt` | 0 | 27 个判据全 PASS，**0 FAIL**（含 AC-12 键盘挂载点） |
| ④ | T17 搜索/选择模型自检（回归） | `regression-searchcheck.txt` | 0 | 26/26 PASS（**关键：T18 改了 `selectByStableId`**） |
| ⑤ | A2 静态纹理（Metal，回归） | `regression-a2-metal.txt` | 0 | 12 探针 PASS / 失败 0 项 |
| ⑥ | DYN 动态帧 · 真实引擎生产者（Metal，回归） | `regression-dyn-engine-metal.txt` + `-run1..3.txt` | **0/3 PASS**（低负载时 2/3） | **环境敏感，非退化证据**：见 §3 / §3.1 |
| ⑥b | DYN 动态帧 · **替身生产者**（判别性对照） | `regression-dyn-stub-metal.txt` + `-run1..3.txt` | **0/3 PASS** | 替身路径**不含 T18 代码**却同样失败 ⇒ 与本任务无关 |
| ⑦ | S3 旧宿主引擎集成（`stellarium.exe`，回归） | `regression-s3-stela3.txt` | 0 | 8/8 PASS（`EngineWallClock` 路径零退化） |

`rc-summary.txt` 是机器可读的汇总。**注意 DYN 那两行不是 `rc=` 而是 `N/3`**，理由见 §3。
⑥/⑥b 必须**成对读**：只读⑥会在"今天全败"时误判成退化（本次真踩到）。

### 1.1 ①b 为什么必须存在（否则整份证据有个洞）

`LocateCheck`（①）全程走 `AppFacade` 的 **C++ 公共 API**——它证明得了"定位逻辑对"，
但**证明不了** QML 里 `onClicked: appFacade.locateSelected(true)` 是活的。名字写错、
按钮没接上，①照样 14/14 全绿。

T15 正是这么栽的：`Keys` 是 **`Item`** 的附加属性，挂在 `ApplicationWindow`（继承 `Window`）
上从未生效，QML 键盘路由整段是死代码，而 `AC-5` 抓不到（它直接调 `routeKey`）——
埋了两个任务才被人肉发现。

所以 ①b **从最外层注入**：按 `objectName` 找到真实 QML 控件 → 向 `QQuickWindow` 投递
真实鼠标按下/抬起 → 断言引擎状态真的改变；反向再读 QML 控件的真实属性
（`enabled` / `Label.text`）证明 C++→QML 的绑定也活着。8 条判据含一条**负控**
（UI-06：点在非按钮控件上，跟踪状态须纹丝不动），否则 UI-04 的 PASS 可能只是"点哪都算"。

---

## 2. T18 自检的 14 项判据

命中 fixture：`Moon`（`stableId=Planet:Moon`）—— 刻意选月球，因为 A4 的固定流程
**I-REP-02 就是"开机→搜月球→定位→改时间→返回"**。

| 判据 | 内容 | 实测 |
|---|---|---|
| LOC-01 | 家园行星守卫谓词（同名拒绝 / 异名放行 / 空串不误拒 / 大小写严格） | ✔ 四情形全对 |
| LOC-02 | 无选中时 `setTracking(true)` 拒绝，理由 `no-selection` | ✔ `isTracking=false` |
| LOC-fixture | `searchObjects` 能命中并选中月球 | `Planet:Moon` |
| LOC-info | 基准 JD 与视口中心偏移（跟踪期望夹角 = \|偏移\|） | JD 2461307.70589；偏移 **0.0000°** |
| LOC-03a | `locateSelected(true)` 落地，理由 `ok` | ✔ |
| LOC-03b | 自动移动结束后锁定夹角 | **0.0475°**（期望 0 ± 0.50） |
| LOC-04 (a) | 跳 0.25 天后目标 AltAz 变化（**证明世界确实动了**） | **127.67°**（门槛 > 5.0） |
| LOC-04 (b) | 跟踪中"目标→视向"夹角 | **0.0747°**（期望 0 ± 0.50） |
| LOC-05a | `locateSelected(false)` 只归中不跟踪 | ✔ `isTracking=false` |
| LOC-05 (a2) | 不跟踪时同样的跳变下目标 AltAz 变化（对照组信号存在） | **127.19°**（门槛 > 5.0） |
| LOC-05 (c) | 不跟踪时夹角 → 跳变后夹角（**判别性对照**） | **32.97° → 158.17°**（与 0.0747° 对照） |
| LOC-06 | `setTracking(false)` 解锁定且 `trackedName` 归空 | ✔ |
| LOC-07 | 幂等重选**不打断**跟踪 | ✔（修复前会断，见交付文档 §3 缺陷 ②） |
| LOC-08 | 家园行星端到端：选中 `Earth` → 拒绝，理由 `home-planet` | ✔ 未进入跟踪 |
| LOC-08b | 清选中后 `isTracking` 归零（**合取真值 vs 引擎原始标志**） | 清前 **合取=true/引擎原始=true** → 清后 **合取=false/引擎原始=true** |

`LOC-05b-note`：归中等待的 2.5s 内天空自转 **82.02°**。**这是 note，不计入判据数**——
它存在的意义是防止读者把"只归中不跟踪后残留 33°"误读成缺陷：自检的 `simRate=0.1 天/秒`
⇒ 墙钟 **1 秒 = 天空自转 36°**，自动移动 1.5s 追完之后视向即固定，天空继续转，余量由此而来。

`LOC-08b` 是**这条判据最有价值的一行**：它同时打印合取真值与引擎原始标志，精确复现了
引擎的"跟踪标志泄漏"（清选中后 `getFlagTracking()` 仍为 `true`）——既证明了缺陷真实存在，
也证明了本层读**合取真值**是必需的而不是多此一举。

---

## 3. DYN 的 `N/3`：为什么它不是"洗绿"，而是一次定性实验

首轮全量回归时 DYN 出了 `rc=8`：

```
D1-C02 FAIL 显示 尾窗稳态 0.0 fps（下限 20.0）；全程推进 0 帧
D1-C07 FAIL 降速窗内 degraded=false（须 true）/ 恢复后 degraded=false（须 false）
```

而 T15/T16/T17 三次同项都是 7/7 PASS，所以先要回答"是退化还是环境"。做完的判别性实验：

| 实验 | 做法 | 结果 |
|---|---|---|
| **换生产者**（低负载） | 同一份 T18 二进制，去掉引擎（替身生产者） | **PASS**（尾窗 28.0 fps、推进 223 帧、降级正确）⇒ 该次 QML/消费者路径没问题 |
| **换二进制** | `git stash` 掉 T18 全部改动 → 重建 T17 基线 → 跑 9 次；再恢复 T18 → 重建 → 跑 9 次 | 基线 **6 PASS / 3 FAIL**；T18 **6 PASS / 3 FAIL** ⇒ **失败率相同（33%）** |
| **换生产者**（高负载，见 §3.1） | 同一份二进制、同一时段，替身 vs 引擎 | 替身 **4/4 FAIL**、引擎 **0/3 FAIL** ⇒ 停摆**与引擎共存无关** |

结论：**本机既有的环境敏感现象**（QML 侧场景图偶发/常发停摆；生产者照跑 50fps、
邮箱帧龄个位数毫秒，日志里**没有任何**丢设备痕迹）。原始数据、失败形状、复现步骤
全部在 **`dyn-ab-baseline-vs-t18.txt`**。

### 3.1 ⚠️ 修正："替身必 PASS" 只在低负载下成立（2026-09-24 13:2x 复测）

上面第一行那个"换生产者 ⇒ PASS"是**当时的条件**，不是普遍规律。同日 13:2x 复测时：

- 机器进入高负载（Spotlight `mdbulkimport` 批量索引 + `system_installd`，load avg ≈ 5.2），
  替身生产者 **4/4 FAIL**（推进 73/80/84/138 帧后停摆）；
- 13:3x 两侧都变成**全程 0 帧**（一帧都没渲），且 `caffeinate -dimsu`（顶住显示器/系统
  休眠 + 声明用户活动）**无效** ⇒ 最可能是窗口未被暴露（会话锁屏 / 显示器睡眠）。

**这反而给出了比 A/B 更干净的论证**：替身生产者的链路上**不含任何 T18 代码**
——DYN 用 `startPage="sky"`，连 `SearchPage` 都不加载——它照样失败，说明停摆与
本次改动**在代码层面就无因果关系**，不必依赖统计。

由此确立的读数采信规则（已写进 `tools/t18-verify.sh` 的 `env_note`）：

- D1-C02/C07 量的是"窗口有没有在渲染"，它要求窗口被暴露。**若同条件的替身对照也失败
  （尤其"全程 0 帧"），该读数反映的是"仪器测不到"，不是"被测程序失败"**；
- 这类读数**不作为退化证据**，但**也不改写成 INVALID**——原始 FAIL 照实保留，
  只在同一文件里加这条定性（不洗绿、也不掩盖）。

因此 `tools/t18-verify.sh` 的 DYN 段现在**同时跑两半**（`regression-dyn-engine-metal`
与 `regression-dyn-stub-metal`）并各报 `N/3`，附 `env_note` 环境注记。只报引擎那一半
会被"今天的单侧读数"带偏——这正是本次踩到的坑。

因此：

1. `tools/t18-verify.sh` 的 DYN 段各跑 3 次、如实报 `N/3`，**不跑"直到绿"**；
2. **零退化的依据是**（a）低负载下同口径 A/B 失败率相同，**以及**（b）替身路径
   不含 T18 代码却同样失败；不是某一次的绿色——报告里不许写成"DYN 3/3 通过"；
3. 失败样本照实归档在 `-run*.txt` 里，不丢弃、不改名成 INVALID；
4. 机械旁证：T18 对 `main.cpp` 的改动只有 5 个 hunk（include / `startPage` 三元式 /
   两条 ActionRouter 注册 / 新增 `locateCheck` 分支 / 新增 `uiCheck` 分支），
   **DYN 分支逐字节未变**；`SkyViewport.*`、`DynFrameCheck.*`、`LiveSkyRuntime.*`
   一个字节都没碰。

值得记一笔的形态：低负载三次里有一次是 `D1-C02 FAIL 但 D1-C07 PASS`（显示先跑了 343 帧、
跨过降速窗之后才停摆）⇒ **C07 并非 C02 的冗余项**，两者覆盖的时段不同。

---

## 4. 与 Windows 侧的关系

Windows 上同形态（合流 × 原生 Vulkan）的 30 分钟长跑里 `SL-C09 窗口暴露 = 1799/1799`、
`SL-C10 降级误报 = 0/1799`，**没有出现过这种停摆**。所以 §3 那条是本机（M3 / macOS /
Metal RHI）特有现象，不是"引擎 + QML 同进程"的普遍缺陷。见
`docs/evidence/2026-09-24-windows-30min/README.md` §2。

---

## 5. 文件清单

| 文件 | 内容 |
|---|---|
| `locatecheck-mac.txt` | T18 自检全量日志（14/14） |
| `uicheck-mac.txt` | **UI 层端到端自检全量日志（8/8）**：objectName 锚点 + 窗口真实鼠标事件 |
| `regression-clockcheck.txt` | T16 时钟自检（12/12） |
| `regression-actioncheck.txt` | T15/T16 命令通路（27 判据 0 FAIL） |
| `regression-searchcheck.txt` | T17 模型自检（26/26） |
| `regression-a2-metal.txt` | A2 静态纹理（12 探针） |
| `regression-dyn-engine-metal.txt` | DYN（引擎）三次汇总 + 环境注记 + 采信规则 |
| `regression-dyn-engine-metal-run{1,2,3}.txt` | DYN（引擎）逐次原始日志（含 FAIL） |
| `regression-dyn-stub-metal.txt` | **DYN（替身，判别性对照）**三次汇总；替身不含 T18 代码 |
| `regression-dyn-stub-metal-run{1,2,3}.txt` | DYN（替身）逐次原始日志 |
| `regression-s3-stela3.txt` | S3 旧宿主集成（8/8） |
| `dyn-ab-baseline-vs-t18.txt` | **DYN 定性的 A/B 实验记录**（T17 基线 vs T18，各 9 次）+ §6 高负载复测 |
| `rc-summary.txt` | 机器可读汇总 |
