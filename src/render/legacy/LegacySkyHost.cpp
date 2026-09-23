/*
 * LegacySkyHost 实现（A2 主体 T6/T7）。
 *
 * 设计要点与"为什么这么做"都写在注释里——本文件里每一个看起来多余的步骤，
 * 都对应一类"能显示出一张看起来没问题的图"的静默错误。
 */

#include "LegacySkyHost.hpp"

#include "FrameMailbox.hpp"

#include <QByteArray>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QOffscreenSurface>
#include <QSurfaceFormat>
#include <QThread>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace stelapp {

// FBO 颜色附件内部格式：
//   GL_RGBA8 —— **不用** GL_SRGB8_ALPHA8。
// 理由：帧契约声明 sRGB 色彩空间，即"像素值就是 sRGB 编码值"。若用 GL_SRGB8_ALPHA8，
// 驱动会在写入时做线性化、读出时再编码，中间任何一步的取整都会让逐像素比对产生
// ±1~2 的抖动，而 A2/A3 一路都要靠逐像素比对做判据。用 GL_RGBA8 保持直通。
static constexpr GLenum kColorInternalFormat = GL_RGBA8;
static constexpr GLenum kColorFormat = GL_RGBA;
static constexpr GLenum kColorType = GL_UNSIGNED_BYTE;
static constexpr GLenum kDepthInternalFormat = GL_DEPTH24_STENCIL8;
static constexpr GLenum kDepthAttachment = GL_DEPTH_STENCIL_ATTACHMENT;

struct LegacySkyHost::Private
{
    LegacySkyHostConfig config;
    LegacyGlInfo glInfo;
    LegacyReadbackInfo readback;
    LegacySkyHost::Stats stats;
    LegacySkyHost::RenderCallback callback;
    FrameMailbox *mailbox = nullptr;
    bool initialized = false;

    // kOwn 模式自有对象
    QOpenGLContext *context = nullptr;        // 借用模式下指向外部对象，不拥有
    QOffscreenSurface *offscreen = nullptr;   // 仅 kOwn 模式存在
    bool ownsContext = false;

    QOpenGLFunctions *gl = nullptr;           // 由 context 提供，生命周期同 context

    // 离屏目标
    GLuint fbo = 0;
    GLuint colorTexture = 0;
    GLuint depthStencilRb = 0;
    quint32 sizeGeneration = 1;

    // 读回缓冲（GL 原生行序 = 自下而上）
    QByteArray staging;
    // 投递缓冲（契约要求行序 = 自上而下）
    QByteArray publishBuffer;

    QElapsedTimer wallClock;

    void destroyTargets(QOpenGLFunctions *f)
    {
        if (!f)
            return;
        if (colorTexture) { f->glDeleteTextures(1, &colorTexture); colorTexture = 0; }
        if (depthStencilRb) { f->glDeleteRenderbuffers(1, &depthStencilRb); depthStencilRb = 0; }
        if (fbo) { f->glDeleteFramebuffers(1, &fbo); fbo = 0; }
    }
};

LegacySkyHost::LegacySkyHost() : d(new Private) {}

LegacySkyHost::~LegacySkyHost()
{
    shutdown();
}

namespace {

QString glString(QOpenGLFunctions *f, GLenum name)
{
    const GLubyte *s = f->glGetString(name);
    return s ? QString::fromLatin1(reinterpret_cast<const char *>(s)) : QString();
}

} // namespace

bool LegacySkyHost::initialize(const LegacySkyHostConfig &config, QString *errorOut)
{
    const auto fail = [errorOut](const QString &msg) {
        if (errorOut)
            *errorOut = msg;
        return false;
    };

    if (d->initialized)
        return fail(QStringLiteral("LegacySkyHost 已初始化，重复调用 initialize()。"));

    d->config = config;

    // ── 尺寸合法性：在这里就拦掉，不要等到渲染时黑屏才发现 ──────────────────
    if (config.physicalSize.width() < 1 || config.physicalSize.height() < 1)
        return fail(QStringLiteral("配置的物理尺寸非法：%1x%2")
                        .arg(config.physicalSize.width())
                        .arg(config.physicalSize.height()));
    if (config.physicalSize.width() > config.maxDimension
        || config.physicalSize.height() > config.maxDimension)
        return fail(QStringLiteral("配置的物理尺寸 %1x%2 超过 maxDimension=%3。"
                                   "请显式调小目标尺寸或调高 maxDimension（并评估读回带宽）。")
                        .arg(config.physicalSize.width())
                        .arg(config.physicalSize.height())
                        .arg(config.maxDimension));

    // ── 上下文 ────────────────────────────────────────────────────────────
    if (config.contextMode == GlContextMode::kOwn)
    {
        // 格式与旧宿主对齐（@see StelMainView::getDesiredGLFormat）：
        // 3.3 Core + RGBA8 + D24S8。故意不请求 multisample：
        // 逐像素校验不需要抗锯齿，开了反而让比对结果不可复现。
        QSurfaceFormat fmt;
        fmt.setRenderableType(QSurfaceFormat::OpenGL);
        fmt.setVersion(config.glMajor, config.glMinor);
        fmt.setProfile(config.coreProfile ? QSurfaceFormat::CoreProfile
                                         : QSurfaceFormat::CompatibilityProfile);
        if (!config.coreProfile)
            fmt.setOption(QSurfaceFormat::DeprecatedFunctions);
        fmt.setRedBufferSize(8);
        fmt.setGreenBufferSize(8);
        fmt.setBlueBufferSize(8);
        fmt.setAlphaBufferSize(8);
        fmt.setDepthBufferSize(24);
        fmt.setStencilBufferSize(8);
        fmt.setSwapInterval(0); // 离屏无交换；显式关掉避免继承到奇怪默认

        d->offscreen = new QOffscreenSurface();
        d->offscreen->setFormat(fmt);
        d->offscreen->create();
        if (!d->offscreen->isValid())
            return fail(QStringLiteral("QOffscreenSurface::create() 失败——离屏驱动的前提不成立。"
                                       "macOS 上请确认请求的 GL 版本/ profile 受支持（%1.%2）。")
                            .arg(config.glMajor)
                            .arg(config.glMinor));

        d->context = new QOpenGLContext();
        d->context->setFormat(fmt);
        if (!d->context->create())
            return fail(QStringLiteral("QOpenGLContext::create() 失败（格式：OpenGL %1.%2 %3）。")
                            .arg(config.glMajor)
                            .arg(config.glMinor)
                            .arg(config.coreProfile ? QStringLiteral("Core") : QStringLiteral("Compat")));
        d->ownsContext = true;

        if (!d->context->makeCurrent(d->offscreen))
            return fail(QStringLiteral("QOpenGLContext::makeCurrent(offscreen) 失败。"));
    }
    else
    {
        // 借用模式：外部必须已经建好上下文，且与本线程亲和（GL 上下文不可跨线程使用）。
        QOpenGLContext *external = QOpenGLContext::currentContext();
        if (!external)
            return fail(QStringLiteral("借用模式要求调用时已有 current 的 GL 上下文"
                                       "（本实现不猜测外部对象，直接取 currentContext）。"));
        d->context = external;
        d->ownsContext = false;
    }

    d->gl = d->context->functions();
    if (!d->gl)
        return fail(QStringLiteral("无法从 GL 上下文获取函数表（QOpenGLContext::functions() 返回空）。"));
    d->gl->initializeOpenGLFunctions();

    // ── 记录"拿到的事实"，而不是"请求的值" ────────────────────────────────
    {
        const QSurfaceFormat fmt = d->context->format();
        d->glInfo.version = glString(d->gl, GL_VERSION);
        d->glInfo.renderer = glString(d->gl, GL_RENDERER);
        d->glInfo.vendor = glString(d->gl, GL_VENDOR);
        d->glInfo.shaderLanguage = glString(d->gl, GL_SHADING_LANGUAGE_VERSION);
        d->glInfo.majorVersion = fmt.majorVersion();
        d->glInfo.minorVersion = fmt.minorVersion();
        d->glInfo.coreProfile = (fmt.profile() == QSurfaceFormat::CoreProfile);
        d->glInfo.ownContext = d->ownsContext;
        d->glInfo.offscreenSurface = (d->offscreen != nullptr);
        GLint v = 0;
        d->gl->glGetIntegerv(GL_MAX_RENDERBUFFER_SIZE, &v);
        d->glInfo.maxRenderbufferSize = int(v);
        v = 0;
        d->gl->glGetIntegerv(GL_MAX_TEXTURE_SIZE, &v);
        d->glInfo.maxTextureSize = int(v);
    }

    d->initialized = true;

    // ── 离屏目标 ──────────────────────────────────────────────────────────
    if (!setTargetSize(config.physicalSize, errorOut))
    {
        d->initialized = false;
        return false;
    }

    d->sizeGeneration = d->stats.sizeGeneration;

    if (d->ownsContext)
        d->context->doneCurrent();

    d->wallClock.start();
    return true;
}

bool LegacySkyHost::setTargetSize(const QSize &physicalSize, QString *errorOut)
{
    const auto fail = [errorOut](const QString &msg) {
        if (errorOut)
            *errorOut = msg;
        return false;
    };

    if (!d->initialized)
        return fail(QStringLiteral("setTargetSize 在 initialize() 之前调用。"));
    if (physicalSize.width() < 1 || physicalSize.height() < 1)
        return fail(QStringLiteral("目标尺寸非法：%1x%2")
                        .arg(physicalSize.width())
                        .arg(physicalSize.height()));
    if (physicalSize.width() > d->config.maxDimension
        || physicalSize.height() > d->config.maxDimension)
        return fail(QStringLiteral("目标尺寸 %1x%2 超过 maxDimension=%3（显式失败，不静默截断）。")
                        .arg(physicalSize.width())
                        .arg(physicalSize.height())
                        .arg(d->config.maxDimension));

    // GL 能力上限：同样显式失败。这里刻意不 clamp —— clamp 会让"以为渲染了 720p、
    // 实际渲染了 4096 上限裁掉的画面"这种问题变成静默数据错误。
    const int cap = std::min(d->glInfo.maxRenderbufferSize, d->glInfo.maxTextureSize);
    if (cap > 0
        && (physicalSize.width() > cap || physicalSize.height() > cap))
        return fail(QStringLiteral("目标尺寸 %1x%2 超过本机 GL 上限 %3。")
                        .arg(physicalSize.width())
                        .arg(physicalSize.height())
                        .arg(cap));

    const bool sizeChanged = (physicalSize != d->readback.physicalSize);

    // 尺寸变了才重建附件；且必须在上下文 current 的前提下做
    if (sizeChanged || d->fbo == 0)
    {
        // 外部管理上下文（借用模式）：不自行 makeCurrent，只校验（见配置字段注释）
        if (d->config.contextManagedExternally)
        {
            if (QOpenGLContext::currentContext() != d->context)
                return fail(QStringLiteral("setTargetSize: 声明了外部管理上下文，但调用时"
                                           "引擎上下文并非 current。"));
        }
        else if (!d->context->makeCurrent(d->offscreen
                                              ? static_cast<QSurface *>(d->offscreen)
                                              : d->context->surface()))
            return fail(QStringLiteral("setTargetSize: makeCurrent 失败。"));

        QOpenGLFunctions *f = d->gl;
        d->destroyTargets(f);

        f->glGenTextures(1, &d->colorTexture);
        f->glBindTexture(GL_TEXTURE_2D, d->colorTexture);
        f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        // 显式上传一张未初始化纹理：不分配的话 FBO 可能"完整"但内容随机，
        // 而随机内容会伪装成"渲染有问题"。
        QByteArray zeros(physicalSize.width() * physicalSize.height() * 4, '\0');
        f->glTexImage2D(GL_TEXTURE_2D, 0, GLint(kColorInternalFormat),
                        physicalSize.width(), physicalSize.height(), 0,
                        kColorFormat, kColorType, zeros.constData());
        f->glBindTexture(GL_TEXTURE_2D, 0);

        f->glGenRenderbuffers(1, &d->depthStencilRb);
        f->glBindRenderbuffer(GL_RENDERBUFFER, d->depthStencilRb);
        f->glRenderbufferStorage(GL_RENDERBUFFER, kDepthInternalFormat,
                                 physicalSize.width(), physicalSize.height());
        f->glBindRenderbuffer(GL_RENDERBUFFER, 0);

        f->glGenFramebuffers(1, &d->fbo);
        f->glBindFramebuffer(GL_FRAMEBUFFER, d->fbo);
        f->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                  GL_TEXTURE_2D, d->colorTexture, 0);
        f->glFramebufferRenderbuffer(GL_FRAMEBUFFER, kDepthAttachment,
                                     GL_RENDERBUFFER, d->depthStencilRb);

        // 完整性必须显式检查。不检查的话，不完整的 FBO 会让所有绘制静默丢弃，
        // 表现为"全黑"，与"渲染逻辑写错"无法区分——这是最常见的归因陷阱。
        const GLenum status = f->glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE)
        {
            f->glBindFramebuffer(GL_FRAMEBUFFER, 0);
            if (d->ownsContext)
                d->context->doneCurrent();
            return fail(QStringLiteral("离屏 FBO 不完整：%1x%2，GL_FRAMEBUFFER_STATUS=0x%3")
                            .arg(physicalSize.width())
                            .arg(physicalSize.height())
                            .arg(status, 0, 16));
        }
        f->glBindFramebuffer(GL_FRAMEBUFFER, 0);
        if (d->ownsContext)
            d->context->doneCurrent();

        // 读回缓冲：
        //   行步长 = 宽度 × 4 字节。glReadPixels 的 GL_PACK_ALIGNMENT 默认是 4，
        //   而 RGBA8 下 width×4 本身已是 4 的倍数，所以此处二者相等；
        //   但我们仍显式设 PACK_ALIGNMENT=1，并把**实际使用的**步长写进元数据——
        //   将来若换成 RGB8（无 alpha），默认对齐会把每行补到 4 的倍数，
        //   那时"紧凑排布"的假设会静默错位。
        d->readback.physicalSize = physicalSize;
        d->readback.rowStride = quint32(physicalSize.width() * 4);
        d->readback.origin = ReadbackOrigin::kTopLeft; // 投递前会做行序翻转
        d->readback.verticallyFlippedDuringReadback = true;
        d->staging.resize(qsizetype(d->readback.rowStride) * physicalSize.height());
        d->publishBuffer.resize(d->staging.size());
        d->stats.bytesPerFrame = d->publishBuffer.size();

        // 尺寸世代：宽高变化 → 旧世代帧一律拒收（消费侧据此重建纹理）
        d->sizeGeneration = d->mailbox ? d->mailbox->bumpSizeGeneration() : (d->sizeGeneration + 1);
        d->stats.sizeGeneration = d->sizeGeneration;
    }

    return true;
}

void LegacySkyHost::setRenderCallback(RenderCallback callback)
{
    d->callback = std::move(callback);
}

void LegacySkyHost::attachMailbox(FrameMailbox *mailbox)
{
    d->mailbox = mailbox;
    if (mailbox)
    {
        // 若已有离屏目标，把当前尺寸的世代同步给邮箱（避免"第一批帧因世代不符被丢"）
        d->sizeGeneration = mailbox->bumpSizeGeneration();
        d->stats.sizeGeneration = d->sizeGeneration;
    }
}

bool LegacySkyHost::withContextCurrent(const std::function<void()> &fn)
{
    if (!d->initialized || !d->context || !fn)
        return false;
    // 外部管理上下文（借用模式）：调用方负责 makeCurrent，本类只用不切（见配置字段注释）。
    if (d->config.contextManagedExternally)
    {
        if (QOpenGLContext::currentContext() != d->context)
            return false;
        fn();
        return true;
    }
    QSurface *surface = d->offscreen ? static_cast<QSurface *>(d->offscreen)
                                     : d->context->surface();
    if (!surface)
        return false;
    if (!d->context->makeCurrent(surface))
        return false;
    fn();
    if (d->ownsContext)
        d->context->doneCurrent();
    return true;
}

bool LegacySkyHost::renderOneFrame(double simSeconds, QString *errorOut)
{
    const auto fail = [errorOut](const QString &msg) {
        if (errorOut)
            *errorOut = msg;
        return false;
    };

    if (!d->initialized)
        return fail(QStringLiteral("renderOneFrame 在 initialize() 之前调用。"));
    if (!d->mailbox)
        return fail(QStringLiteral("renderOneFrame: 未 attachMailbox，帧无处投递（不静默丢弃）。"));
    if (!d->callback)
        return fail(QStringLiteral("renderOneFrame: 未 setRenderCallback，无渲染内容。"));
    if (d->fbo == 0)
        return fail(QStringLiteral("renderOneFrame: 离屏目标未建立。"));

    ++d->stats.requested;

    QSurface *surface = d->offscreen ? static_cast<QSurface *>(d->offscreen)
                                     : d->context->surface();
    // 外部管理上下文（借用模式）：调用方已 current，这里只校验，不自行 makeCurrent。
    // 理由见 LegacySkyHostConfig::contextManagedExternally 注释。
    if (d->config.contextManagedExternally)
    {
        if (QOpenGLContext::currentContext() != d->context)
            return fail(QStringLiteral("renderOneFrame: 声明了外部管理上下文，但调用时"
                                       "引擎上下文并非 current（调用方须先 "
                                       "StelMainView::glContextMakeCurrent）。"));
    }
    else
    {
        if (!surface)
            return fail(QStringLiteral("renderOneFrame: 无可用绘制表面。"));
        if (!d->context->makeCurrent(surface))
            return fail(QStringLiteral("renderOneFrame: makeCurrent 失败。"));
    }

    QOpenGLFunctions *f = d->gl;
    const QSize size = d->readback.physicalSize;

    // 仿真时间增量：**不是墙钟差**。旧宿主把它直接喂给 StelApp::update(dt)。
    double dt = 0.0;
    if (d->stats.requested > 1)
        dt = simSeconds - d->stats.lastSimSeconds;
    else
        d->stats.lastSimSeconds = simSeconds;
    if (dt < 0.0)
        dt = 0.0; // 时间倒退（用户把时间拨回去）时不给负 dt，避免上层模块出现负积分
    d->stats.lastDeltaSeconds = dt;

    // 记录并恢复宿主原本的 FBO 绑定：借用模式下（A3 与旧宿主同进程）
    // 不恢复会把旧宿主的 QOpenGLWidget 默认 FBO 顶掉，产生难查的错帧。
    GLint previousFbo = 0;
    f->glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFbo);

    f->glBindFramebuffer(GL_FRAMEBUFFER, d->fbo);
    f->glViewport(0, 0, size.width(), size.height());

    // 干净起点：清掉颜色与深度/模板。清色用不透明黑——
    // 透明黑会让"没画出东西"看起来像"画出了透明的东西"，二者在本阶段必须可区分。
    f->glDisable(GL_SCISSOR_TEST);
    f->glClearColor(0.f, 0.f, 0.f, 1.f);
    f->glClearDepthf(1.f);
    f->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

    // ── 绘制（唯一由外部注入的部分）─────────────────────────────────────────
    const qint64 tRender = d->wallClock.elapsed();
    d->callback(dt, simSeconds, size);
    // 读回前必须让 GPU 真的画完：glReadPixels 本身会隐式同步，但显式 glFinish
    // 能让"渲染耗时/读回耗时"这两个统计量各自可解释，而不是把等待都记到读回头上。
    f->glFinish();
    const qint64 tAfterRender = d->wallClock.elapsed();

    // ── 读回（T7）──────────────────────────────────────────────────────────
    f->glPixelStorei(GL_PACK_ALIGNMENT, 1);
    f->glReadPixels(0, 0, size.width(), size.height(),
                    kColorFormat, kColorType, d->staging.data());
    const qint64 tAfterRead = d->wallClock.elapsed();
    f->glBindFramebuffer(GL_FRAMEBUFFER, GLuint(previousFbo));
    if (d->ownsContext)
        d->context->doneCurrent();

    // ── 行序翻转：GL 原点在左下，帧契约要求左上 ────────────────────────────
    // 必须在这里真的翻，而不是只改元数据里的方向标记——只标不翻，
    // 消费侧要么显示上下颠倒，要么对不上探针坐标。
    {
        const int stride = int(d->readback.rowStride);
        const int h = size.height();
        const char *src = d->staging.constData();
        char *dst = d->publishBuffer.data();
        for (int row = 0; row < h; ++row)
            std::memcpy(dst + qsizetype(row) * stride,
                        src + qsizetype(h - 1 - row) * stride,
                        size_t(stride));
    }

    LegacyFrame frame;
    frame.frameNumber = d->stats.requested; // 请求序号即帧序号：请求 N 帧得 N 帧可对账
    // 状态编号：由**仿真时间**量化为毫秒。用它而不是墙钟，是为了让"帧内容随仿真时间变化"
    // 这条判据在元数据层也可核对——消费侧可以据此判断"内容变了"是仿真推进还是随机抖动。
    frame.stateNumber = quint64(std::llround(simSeconds * 1000.0));
    frame.sizeGeneration = d->sizeGeneration;
    // 逻辑尺寸 = 物理尺寸 / DPR。DPR 未标注时（1.0）两者相等。
    {
        const qreal dpr = d->config.devicePixelRatio > 0.0 ? d->config.devicePixelRatio : 1.0;
        frame.logicalSize =
            QSize(qMax(1, int(std::lround(size.width() / dpr))),
                  qMax(1, int(std::lround(size.height() / dpr))));
    }
    frame.physicalSize = size;
    frame.rowStride = d->readback.rowStride;
    frame.pixels = nullptr; // publishFrame 会拷进槽位缓冲并把 pixels 指向槽位，此处无须预置

    const bool ok = d->mailbox->publishFrame(
        frame, reinterpret_cast<const quint8 *>(d->publishBuffer.constData()),
        d->publishBuffer.size());

    d->stats.lastSimSeconds = simSeconds;
    d->stats.lastRenderMs = tAfterRender - tRender;
    d->stats.lastReadbackMs = tAfterRead - tAfterRender;
    d->stats.lastTotalMs = tAfterRead - tRender;
    if (ok)
        ++d->stats.published;
    else
        ++d->stats.droppedByMailbox;

    // 投递被拒不算渲染失败（邮箱忙时丢旧帧是设计内行为），但要在统计里看得见。
    return true;
}

void LegacySkyHost::shutdown()
{
    if (!d)
        return;
    if (d->initialized && d->context)
    {
        QSurface *surface = d->offscreen ? static_cast<QSurface *>(d->offscreen)
                                         : d->context->surface();
        if (surface && d->context->makeCurrent(surface))
        {
            d->destroyTargets(d->gl);
            d->context->doneCurrent();
        }
    }
    d->staging.clear();
    d->publishBuffer.clear();
    if (d->ownsContext)
    {
        delete d->context;
        d->context = nullptr;
        delete d->offscreen;
        d->offscreen = nullptr;
    }
    d->gl = nullptr;
    d->initialized = false;
    d->mailbox = nullptr;
}

LegacySkyHost::Stats LegacySkyHost::stats() const { return d->stats; }

const LegacyGlInfo &LegacySkyHost::glInfo() const { return d->glInfo; }

const LegacyReadbackInfo &LegacySkyHost::readbackInfo() const { return d->readback; }

bool LegacySkyHost::isInitialized() const { return d->initialized; }

} // namespace stelapp
