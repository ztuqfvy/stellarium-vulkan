/*
 * LegacySkyHost — 旧 OpenGL 天空宿主的**显式帧驱动**（A2 主体，T6/T7/T8）。
 *
 * 职责：把"旧宿主在 paint 事件里被动绘制"改成"由外部时钟显式驱动、渲染到离屏目标、
 *   读回 RGBA8 内存帧、投递到 FrameMailbox"。
 *
 * 本类回答的**唯一问题**（计划一 A2 硬约束）：
 *   隐藏旧窗口不保证继续绘制。必须实现显式帧驱动，证明离屏目标和上下文有效；
 *   不能只调用 hide() 后等 paint 事件。
 *   → 因此本类的每一条路径都**不引用任何窗口可见性、不依赖任何 paint 事件**。
 *     验收判据：请求 N 帧必须得到 N 帧，且帧内容随**仿真时间**变化（证明不是静态残留）。
 *
 * 分层（关键：这是"产帧侧"，与 QML/Vulkan 消费侧解耦）：
 *
 *     外部仿真时钟 ──renderOneFrame(simSeconds)──►  LegacySkyHost
 *                                                    │ makeCurrent
 *                                                    │ 绑定离屏 FBO（RGBA8+D24S8）
 *                                                    │ 调用 RenderCallback ← 旧宿主在此画天空
 *                                                    │ glReadPixels
 *                                                    │ 行序翻转（GL 左下 → 契约左上）
 *                                                    └──publishFrame──► FrameMailbox ──► 消费侧
 *
 *   整条路径不经过 QML / QSG / Vulkan 纹理上传，因此**不被** Qt6.11.2 Vulkan RHI ×
 *   MoltenVK 的静态纹理黑屏缺陷阻塞（该缺陷的定性见 docs/BUILD_RECORD.zh_CN.md）。
 *
 * 禁止：
 *   1. 依赖 hide() / paint 事件 / 窗口可见性来触发绘制。
 *   2. 逐帧 PNG/JPEG 编码，或写图片文件后回读。
 *   3. 在 QML 渲染线程调用本类（GL 上下文线程亲和性）。
 *   4. 让渲染线程访问可变的 Stellarium 模块（渲染回调只可在 GL 线程内触碰引擎）。
 *   5. 超出 GL 实现上限的尺寸静默降级——必须显式失败（见 setTargetSize）。
 */
#pragma once

#include <QSize>
#include <QString>
#include <QElapsedTimer>
#include <functional>
#include <memory>

namespace stelapp {

class FrameMailbox;

// ── GL 上下文来源 ─────────────────────────────────────────────────────────
enum class GlContextMode
{
    //! 自建 QOpenGLContext + QOffscreenSurface：**完全没有窗口**。
    //! 用途：T6 证据（证明离屏驱动成立）、后续辅助进程方案。
    //! 限制：引擎的 GL 资源必须在本上下文内创建（StelOpenGL 会校验上下文一致性）。
    kOwn,
    //! 借用外部已存在的 GL 上下文（A3 起：旧宿主 qopenglwidget 的 context）。
    //! 用途：进程内集成——引擎资源在旧上下文里创建，我们只借用它渲染到自己的 FBO。
    kBorrowed
};

// 读回像素的原点约定（写进帧元数据，消费侧据此决定是否翻转纹理坐标）。
enum class ReadbackOrigin
{
    kTopLeft,     //!< 帧数据首行对应图像顶部（本工程契约要求的值）
    kBottomLeft   //!< GL 原生约定（glReadPixels 输出）
};

// ── 配置 ─────────────────────────────────────────────────────────────────
struct LegacySkyHostConfig
{
    //! 离屏目标像素尺寸（= 逻辑尺寸 × DPR，换算见 StaticFrameSource::physicalSizeFor）。
    QSize physicalSize{1280, 720};
    //! 像素比，仅用于把物理尺寸反算成帧元数据里的 logicalSize。
    //! 留 1.0 表示"逻辑=物理"（离屏渲染无屏幕缩放的常见情形）。
    //! 界面若在 HiDPI 屏上显示该帧，应传屏幕 DPR，让消费侧知道该按多大显示。
    qreal devicePixelRatio = 1.0;
    //! 尺寸上限。超过 GL 实现上限或本值时 setTargetSize 显式失败，不静默截断。
    //! 取 4096 的理由：读回带宽与队列内存受控（4096×4096 RGBA8 = 64MB/帧 × 3 槽位）。
    int maxDimension = 4096;
    GlContextMode contextMode = GlContextMode::kOwn;
    //! 期望的 GL 版本与 profile，须与旧宿主一致（StelMainView::getDesiredGLFormat）。
    //! 默认 3.3 Core —— Stellarium 桌面版默认请求的就是这个。
    int glMajor = 3;
    int glMinor = 3;
    bool coreProfile = true;
};

//! 初始化后实测到的 GL 事实（不采信"请求值"，只记"拿到值"）。
struct LegacyGlInfo
{
    QString version;        //!< GL_VERSION 字符串
    QString renderer;       //!< GL_RENDERER 字符串
    QString vendor;         //!< GL_VENDOR 字符串
    QString shaderLanguage; //!< GL_SHADING_LANGUAGE_VERSION
    int majorVersion = 0;
    int minorVersion = 0;
    bool coreProfile = false;
    int maxRenderbufferSize = 0;
    int maxTextureSize = 0;
    bool ownContext = false;   //!< true = 自建离屏上下文（无窗口）
    bool offscreenSurface = false;
};

//! 一帧读回后的元数据（**必须**随帧一起交出去，否则消费侧只能猜方向/步长）。
struct LegacyReadbackInfo
{
    quint32 rowStride = 0;                                  //!< 实际行步长（字节）
    ReadbackOrigin origin = ReadbackOrigin::kTopLeft;       //!< 数据首行的图像位置
    bool verticallyFlippedDuringReadback = false;           //!< 是否在读回时做了行序翻转
    QSize physicalSize;
};

class LegacySkyHost
{
public:
    //! 渲染回调契约（**在 GL 上下文已 current、离屏 FBO 已绑定、viewport 已设为
    //! physicalSize 的前提下被调用**）：实现须把整帧画到当前 target 上。
    //!
    //! 参数语义：
    //!   deltaSeconds — 距上一帧的**仿真**时间增量（首帧为 0）。直接对应
    //!                  @see StelApp::update(double deltaTime)，旧宿主接入时原样转发。
    //!   simSeconds   — 当前**仿真**时间（非墙钟）。旧宿主接入时由 StelCore 的 JD 换算。
    //!                  帧内容必须随它变化，否则 T6 判据"证明不是静态残留"不成立。
    //!   physicalSize — 离屏目标像素尺寸。
    //!
    //! 禁止：回调内创建/销毁窗口、调用 QCoreApplication::processEvents、切换 FBO。
    using RenderCallback =
        std::function<void(double deltaSeconds, double simSeconds, const QSize &physicalSize)>;

    struct Stats
    {
        quint64 requested = 0;         //!< renderOneFrame 被调用次数
        quint64 published = 0;         //!< 成功读回并投递到邮箱的次数
        quint64 droppedByMailbox = 0;  //!< 邮箱拒收（无可用槽位/世代不符/序号不新）
        quint64 failed = 0;            //!< 渲染或读回失败次数（必须为 0 才算通过）
        quint32 sizeGeneration = 0;
        double lastDeltaSeconds = 0.0;
        double lastSimSeconds = 0.0;
        qint64 lastRenderMs = -1;      //!< 上一帧 callback 耗时（不含读回）
        qint64 lastReadbackMs = -1;    //!< 上一帧读回耗时
        qint64 lastTotalMs = -1;
        qsizetype bytesPerFrame = 0;
    };

    LegacySkyHost();
    ~LegacySkyHost();
    LegacySkyHost(const LegacySkyHost &) = delete;
    LegacySkyHost &operator=(const LegacySkyHost &) = delete;

    // ── 装配（THREAD: 即将成为 GL 上下文所属线程的那个线程，且此后不再变更）──────
    // 建立 GL 上下文与离屏目标。失败时把**可操作**的原因写入 errorOut（不静默降级）。
    // kOwn 模式：创建 QOpenGLContext + QOffscreenSurface，全程无窗口、无 paint 事件。
    bool initialize(const LegacySkyHostConfig &config, QString *errorOut);
    void shutdown();

    // THREAD: gl-ctx. 变更离屏目标尺寸。超上限/超 GL 能力时返回 false，
    // 成功时递增尺寸世代（旧世代帧会被邮箱拒收，消费侧需重建纹理）。
    bool setTargetSize(const QSize &physicalSize, QString *errorOut);

    void setRenderCallback(RenderCallback callback);
    // 生命周期：邮箱必须比本对象活得久（本对象析构前请先 attachMailbox(nullptr)）。
    void attachMailbox(FrameMailbox *mailbox);

    //! THREAD: gl-ctx。在"上下文已 current"的前提下执行 fn。
    //! 用途：装配/释放**客户端** GL 资源（渲染回调里要用到的着色器、VAO、纹理…），
    //! 使其生命周期与上下文正确对齐——否则会在错误的上下文里创建或删除对象，
    //! 表现为"资源莫名其妙失效"或退出期崩溃。
    //! 返回 false 表示取不到上下文（此时 fn 未被调用）。
    bool withContextCurrent(const std::function<void()> &fn);

    // ── 显式帧驱动（THREAD: gl-ctx）──────────────────────────────────────────
    // 请求一帧：makeCurrent → 绑定离屏目标 → 回调渲染 → 读回 RGBA8 → 翻转行序 → 投递。
    // **不等待、不依赖任何事件**：返回 true 即表示这一帧已经进邮箱。
    bool renderOneFrame(double simSeconds, QString *errorOut);

    //! 实测到的 GL 事实（定义在 .cpp，因为实现细节封装在 Private 里）。
    const LegacyGlInfo &glInfo() const;
    const LegacyReadbackInfo &readbackInfo() const;
    Stats stats() const;
    bool isInitialized() const;

    struct Private;

private:
    std::unique_ptr<Private> d;
};

} // namespace stelapp
