# 2026-09-23｜T11 引擎共进程帧驱动（LiveSkyRuntime）证据索引

T11 = 里程碑 A3 第一步：真实引擎在 QML 进程内产帧并投递 FrameMailbox，
消费侧（SkyViewport）零改动。前置：T10 合流（8869732）+ A3 前置探针（bc50b42）。

## 判定：PASS

| 项 | 结果 | 证据 |
|---|---|---|
| C-05 借引擎上下文装配离屏宿主（kBorrowed + 外部管理上下文） | ✅ PASS（GL 4.1 Metal，Apple M3） | `10-coexist-c05c07-metal.txt` |
| C-06 真实引擎帧驱动 + 离屏读回 | ✅ 请求=2 发布=2 失败=0，1280x720，读回 1ms/帧 | 同上 |
| C-07 帧内容非空且随 JD 变化 | ✅ 非黑比例 1.0000/1.0000，两帧哈希不同（JD +0.25 天） | 同上 + `frames/probe-frame-sim*.png` |
| LIVE_ENGINE 冒烟（Metal，暖机修复后） | ✅ rc=0，runtimeApi=Metal，引擎引导 0ms，抓帧为真实星空 | `21-...metal-warm.txt` + `frames/live-engine-6s-metal.png` |
| A2 回归（Widgets 宿主形态） | ✅ VERDICT=PASS | `30-a2-regression.txt` |
| DYN 回归（Widgets 宿主形态） | ✅ VERDICT=PASS | `31-dyn-regression.txt` |
| STELA3_CHECK（stellarium 主目标，LegacySkyHost 修改后） | ✅ 8/8 PASS rc=0 | `33-stela3check.txt` |
| 默认形态（Widgets OFF）回归 | ✅ A2/DYN PASS，LIVESKY 输出 0 行（空实现无泄露） | `34/35/36-*.txt` |

## 关键过程发现（两处返工）

1. **`QOpenGLContext::doneCurrent()` 会把 `surface()` 清空** → 借用模式下
   `LegacySkyHost::renderOneFrame` 自己 `makeCurrent(context->surface())` 拿到 nullptr，
   首跑报"renderOneFrame: 无可用绘制表面"（请求=2 发布=0，C-06/C-07 FAIL；
   该首跑输出未单独留档，失败文本见 BUILD_RECORD 节选）。
   处置：`LegacySkyHostConfig` 新增 `contextManagedExternally`（默认 false，kOwn 路径
   行为不变）；借用模式下宿主只**校验** current 不自行切换。三处统一处理
   （renderOneFrame / withContextCurrent / setTargetSize）。
2. **QML 后端漂移**：不暖机直接引导引擎 → `runtimeApi=Vulkan`（请求的是 metal），
   触发 MoltenVK 静态纹理黑屏缺陷，视口全黑（`20-live-engine-smoke-metal.txt`）。
   根因：QML 场景图首次渲染（RHI 设备创建）发生在引擎 GL 上下文创建之后。
   处置：LIVE_ENGINE 在 boot 前先暖机（`window->update()` 主动请求重绘直到
   `isSceneGraphInitialized()`，再 settle 300ms）——与 A3 探针相同的顺序纪律。
   修复后 `runtimeApi=Metal`（`22-live-nongine-metal.txt` 为无引擎对照，
   `21-...metal-warm.txt` 为修复后）。

## 交付物

- `src/ui/LiveSkyRuntime.{hpp,cpp}`：真实引擎进程内帧驱动（GUI 线程 QTimer 分片；
  双宏守卫，独立工程/默认形态编译为空）。
- `src/render/legacy/LegacySkyHost.{hpp,cpp}`：`contextManagedExternally` 支持。
- `src/ui/main.cpp`：`STELQUICK_LIVE_ENGINE=1` 模式（复用 `STELQUICK_LIVE_FPS/SIZE`，
  新增 `STELQUICK_LIVE_SIMRATE`，JD 速率天/秒）；退出路径 `_exit` 纪律。
- `src/ui/CMakeLists.txt`：LiveSkyRuntime 进源列表。

## 画面证据

- `frames/probe-frame-sim0.png` / `probe-frame-sim100.png`：探针读回（白天雪山全景，JD 差 0.25 天）。
- `frames/live-engine-6s-metal.png`：LIVE_ENGINE 第 6 秒窗口抓帧
  （黄昏星空 + 全景 + 方位标记，QML Metal 上屏，引擎 GL 4.1 产帧）。

## 已知边界（留给 T12）

- 帧泵未做长跑计量（T13 项）；`STELQUICK_LIVE_SIMRATE=0.02` 天/秒为默认。
- 默认形态未验证 LIVE_ENGINE 环境变量会被忽略（空实现下 main.cpp 的 liveEngine
  分支不编译，天然无效果——35/36 号证据的 LIVESKY 计数为 0 即佐证）。
- 引擎不关（StelMainView/StelApp 不销毁），退出走 `_exit` 纪律。
