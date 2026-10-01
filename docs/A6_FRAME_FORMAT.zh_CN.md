# A6 帧格式文档（交接计划二的硬性契约第 3 条）

> **出处**：开发计划一 §7「交给计划二的硬性契约」第 3 条 ——
> 「帧格式文档：**逻辑/物理尺寸、原点/方向、颜色空间、透明度、帧/状态/尺寸世代标识**」。
> **性质**：冻结项。计划二（原生 Vulkan 天空）接手后**不得**改这些语义；
> 若确需变更，必须同步改本文件与所有消费侧，并重新跑 A2 逐像素与 DYN 判据。
>
> **本文不是新写的主张**，而是把散在源码注释与构建记录里的事实集中成一份可交付物。
> 每一条都给了**权威源**（源码行）与**实测证据**（判据/证据文件），可逐条复核。

---

## 1. 帧是什么

旧天空渲染器每产出一帧，就把「**元数据 + 像素字节**」投进 `FrameMailbox`
（`src/render/legacy/FrameMailbox.hpp` / `.cpp`）。消费侧（`SkyViewport`，
Qt Quick 场景图线程）取最新完整帧、上传纹理、上屏。

- 生产者线程：`gl-ctx`（旧 GL 上下文所属线程）
- 消费者线程：`scenegraph`（Qt Quick 渲染线程）
- 中间只传**不可变快照**，两侧不共享可变状态（编码规范 §5 硬性禁区）。

权威源：`src/render/legacy/FrameMailbox.hpp` 头注与 `struct LegacyFrame`。

---

## 2. 尺寸：逻辑 / 物理 / DPR

| 字段 | 含义 | 权威源 |
|---|---|---|
| `physicalSize` | **离屏 FBO 的像素尺寸**（= 实际读回的像素矩阵尺寸） | `LegacySkyHost.cpp` `frame.physicalSize = size;` |
| `logicalSize` | `physicalSize / devicePixelRatio`，四舍五入且下限 1 | `LegacySkyHost.cpp`（`frame.logicalSize = QSize(lround(w/dpr), lround(h/dpr))`） |
| `rowStride` | **一行的字节数**，独立字段而不是"由宽度推得" | `LegacySkyHost.cpp` `frame.rowStride = width*4` |

约定与理由：

1. **`rowStride` 必须作为事实交出去，不能靠宽度推。** RGBA8 下 `width×4` 天然 4 字节对齐，
   但一旦换 RGB8，默认的 `GL_PACK_ALIGNMENT=4` 会静默错位。
   本项目**显式设 `glPixelStorei(GL_PACK_ALIGNMENT, 1)` 并把实际使用的步长写进元数据**
   （`LegacySkyHost.cpp`，构建记录 2026-09-20 条目）。
2. 默认验收尺寸 **1280×720**，`devicePixelRatio` 未标注时按 **1.0** 处理
   （此时逻辑尺寸 == 物理尺寸）。
3. 尺寸变化**不是**改同一个帧的字段，而是**递增尺寸世代**（见 §6）。

---

## 3. 内存布局：行优先、原点左上、**行序真的翻了**

| 属性 | 值 |
|---|---|
| 每像素字节 | **4**（R,G,B,A 顺序） |
| 行序 | **行优先**（每行连续，行间按 `rowStride` 步进） |
| 原点 | **左上** |
| 方向 | 第 0 行 = 图像**顶**行；第 0 列 = 图像**左**列 |

⚠️ **这里有一个必须说清楚的坑：OpenGL 的原点在左下，帧契约要求左上。**
`glReadPixels(0,0,w,h)` 交出来的是**自下而上**的像素行。本项目在
`LegacySkyHost` 里**真的把行序翻过来**（`memcpy` 逐行倒序重排），而不是只改元数据里的
方向标记：

> `// ── 行序翻转：GL 原点在左下，帧契约要求左上 ──`
> `// 必须在这里真的翻，而不是只改元数据里的方向标记——只标不翻，`
> `// 消费侧要么显示上下颠倒，要么对不上探针坐标。`

对应地，消费侧**不做**任何额外的垂直镜像（`node->setTextureCoordinatesTransform(
QSGImageNode::NoTransform)`，`src/ui/quick/SkyViewport.cpp`）——
两侧的口径必须一致，否则画面上下颠倒。

**方向的可复现验证**：替身场景 `LegacyTestScene` 固定画两条定位色带
（**顶 24 行 = 绿**、**左 24 列 = 红**，`src/render/legacy/LegacyTestScene.cpp`），
A2 逐像素判据据此**独立**验证"行翻转"与"水平朝向"两件事 —— 不依赖场景内容本身。

---

## 4. 颜色：sRGB 直通、非预乘 alpha

| 属性 | 值 | 权威源 |
|---|---|---|
| 读回格式 | `GL_RGBA` + `GL_UNSIGNED_BYTE` | `LegacySkyHost.cpp:31-32` |
| FBO 内部格式 | **`GL_RGBA8`**（**不是** `GL_SRGB8_ALPHA8`） | `LegacySkyHost.cpp:30` |
| 色彩空间 | **sRGB 编码值直通**（像素值本身就是 sRGB 编码，不做线性化往返） | `LegacySkyHost.cpp:26-29` |
| alpha | **非预乘** | `FrameMailbox.hpp` 格式契约注释 |
| 深度附件 | `D24S8` | `LegacySkyHost.cpp:123` |

**为什么是 `GL_RGBA8` 而不是 `GL_SRGB8_ALPHA8`**（原文理由）：

> 帧契约声明 sRGB 色彩空间，即"像素值就是 sRGB 编码值"。若用 `GL_SRGB8_ALPHA8`，
> 读写会做线性化往返，给逐像素比对引入 ±1~2 的抖动，而 A2/A3 一路都要靠逐像素
> 比对做判据。用 `GL_RGBA8` 保持直通。

**多采样**：**故意不请求 multisample**（`LegacySkyHost.cpp:123`）——
逐像素判据要的是确定性，不是抗锯齿观感。

---

## 5. 三个标识：帧号 / 状态号 / 尺寸世代

| 标识 | 字段 | 语义 | 权威源 |
|---|---|---|---|
| **帧号** | `frameNumber`（`quint64`） | 生产者**单调递增**。"请求 N 帧得 N 帧可对账" ⇒ **请求序号即帧序号** | `LegacySkyHost.cpp` `frame.frameNumber = d->stats.requested;` |
| **状态号** | `stateNumber`（`quint64`） | **仿真时间**量化为**毫秒**（`llround(simSeconds*1000)`）。**不是墙钟** | `LegacySkyHost.cpp` |
| **尺寸世代** | `sizeGeneration`（`quint32`） | 宽高变化时由 `bumpSizeGeneration()` 递增；**旧世代的帧一律拒收** | `FrameMailbox.hpp` |

★ **`stateNumber` 用仿真时间而不是墙钟，是有判据意义的**：消费侧可以据此判断
"画面内容变了"到底是**仿真在推进**还是**随机抖动** —— 这是 T35（仿真时间链路）
与 DYN 动态帧判据的基础。

★ **尺寸世代的作用是"防止跨尺寸帧混用"**：尺寸一变，旧世代帧不再进入消费路径，
避免"用 1280×720 的像素去填 1920×1080 的纹理"这种静默错位。

---

## 6. 邮箱语义：3 槽位、只留最新、忙时丢旧

| 性质 | 值 | 说明 |
|---|---|---|
| 槽位数 | `FrameMailbox::kFrameSlotCount = 3` | 够"生产者写一槽、消费者读一槽、余一槽周转" |
| 保留策略 | **只保留最新完整帧** | 忙时丢**旧**帧，不积压 |
| 丢弃是否算错 | **不算** | "投递被拒"是设计内行为，计入 `dropped` 供观测，**不计入渲染失败** |
| 租约 | `FrameLease`（RAII） | 消费者持有期间该槽位不被复用；析构即归还 |

**为什么必须有界**：无界队列在"消费者慢一帧"时会退化成内存黑洞；
`P-BRG-01` 明确要求"**队列长度**"进统计，`CFG/DYN` 判据里也有"邮箱有界 / 丢弃 ≤5%"。
邮箱还有 `Stats` 供诊断读取：`published / dropped / leased / sizeGeneration /
completeSlots（队列长度）/ readersHeld / latestFrameAgeMs / bytesPerFrame`。

---

## 7. 消费侧怎么用它（`SkyViewport`）

1. 场景图线程 `takeLatestFrame()` 拿租约（可能无效 ⇒ 用 1×1 占位纹理兜底）。
2. 帧号未变则**不重传纹理**（`m_uploadedFrameNumber` 去重）。
3. 节点被重建（缩放/隐藏显示/抓帧都会）时，**必须把已有纹理补挂回新节点** ——
   否则新节点永远没纹理，"每帧告警 + 画面缺一块"。
4. 坐标变换用 `NoTransform`（因为 §3 已经翻过行序）。

> 这三条都属于**消费侧的契约义务**，计划二换成原生 Vulkan 天空后，
> 若仍沿用 `SkyViewport` 作为页面协议入口，则这些义务不变。

---

## 8. 计划二不得改的东西（冻结清单）

1. 元数据字段名与语义：`frameNumber` / `stateNumber` / `sizeGeneration` /
   `logicalSize` / `physicalSize` / `rowStride`。
2. 像素契约：**RGBA8 字节序 R,G,B,A**、**行优先**、**原点左上（行序已翻）**、
   **非预乘 alpha**、**sRGB 编码直通**。
3. 有界语义：槽位数与"只留最新完整帧、忙时丢旧帧"的策略。
4. `stateNumber` 用**仿真时间**量化这一条（换成墙钟会让"内容随仿真变化"的判据失效）。

---

## 9. 证据与复核入口

| 事实 | 证据 |
|---|---|
| 行序真的翻转（不是只改标记） | 源码 `LegacySkyHost.cpp`「行序翻转」段 + `LegacyTestScene.cpp` 的两条定位色带 |
| 逐像素方向/通道顺序/透明度正确 | `STELQUICK_A2_CHECK=1` ⇒ `A2CHECK` 12/12（macOS 走 Metal，见测试文档 §6.2 已知外部缺陷说明） |
| 尺寸世代拒收旧帧 | `FrameMailbox` 单元行为 + `SkyViewport::viewportGeneration`（尺寸变化时递增） |
| `rowStride` 实测值 | 长跑日志 `T9-C02`「离屏 FBO 1280x720，行步长 5120，每帧 3600 KB」 |
| RGBA8 而非 SRGB8 的理由 | `LegacySkyHost.cpp:26-30` + 构建记录 2026-09-20 |
