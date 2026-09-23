// SkyViewport 实现。线程约束见头文件注释。
#include "ui/quick/SkyViewport.hpp"

#include "render/legacy/FrameMailbox.hpp"

#include <QElapsedTimer>
#include <QImage>
#include <QPointer>
#include <QSGImageNode>
#include <QQuickWindow>
#include <QSGTexture>
#include <cstdio>

namespace stelapp {

SkyViewport::SkyViewport(QQuickItem *parent)
    : QQuickItem(parent)
{
    // 有 QSG 内容：不设此标志时 updatePaintNode 永远不会被调用
    setFlag(QQuickItem::ItemHasContents, true);
}

SkyViewport::~SkyViewport()
{
    // 清空邮箱回调，避免生产者线程继续往已销毁对象上投递唤醒
    if (m_mailbox)
        m_mailbox->setFrameAvailableCallback(nullptr);
    // 采样定时器随对象销毁（父子关系归本项，无需手动删）
    // 纹理只在场景图线程使用；项析构时窗口即将销毁，直接释放安全
    delete m_texture;
    delete m_dummyTexture;
}

void SkyViewport::setFrameMailbox(FrameMailbox *mailbox)
{
    if (m_mailbox == mailbox)
        return;

    if (m_mailbox)
        m_mailbox->setFrameAvailableCallback(nullptr);

    m_mailbox = mailbox;

    if (m_mailbox) {
        // 帧到达 → 唤醒本项重绘。生产者可能在别的线程，所以用队列调用，
        // 由 QPointer 守卫避免对象已销毁时打到野指针上。
        // 注意：只在这里 update() 是不够的——邮箱不会自己触发场景图。
        QPointer<SkyViewport> guard(this);
        mailbox->setFrameAvailableCallback([guard]() {
            if (!guard)
                return;
            QMetaObject::invokeMethod(guard.data(), [guard]() {
                if (guard)
                    guard->update();
            }, Qt::QueuedConnection);
        });

        // 显示帧率采样（GUI 线程每秒一次），供 degraded 判定与诊断属性
        if (!m_fpsTimer) {
            m_fpsTimer = new QTimer(this);
            m_fpsTimer->setInterval(1000);
            connect(m_fpsTimer, &QTimer::timeout, this, &SkyViewport::sampleDisplayFps);
        }
        m_fpsTimer->start();
    }

    // 解绑（mailbox == nullptr）时保留已上传的最后一帧纹理：画面不闪，
    // 纹理归场景图线程所有，由本项析构或下一次上传时替换释放。
    update();
}

void SkyViewport::applyBackendResult(const QString &runtimeApiName, bool isVulkan)
{
    m_backend = runtimeApiName;
    emit backendChanged(m_backend);

    m_ready = isVulkan;
    if (!isVulkan) {
        m_errorMessage = QStringLiteral("请求 Vulkan，实际 %1。禁止静默回退。").arg(runtimeApiName);
        emit backendFailed(m_errorMessage);
    }
    emit readyChanged(m_ready);
}

void SkyViewport::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() == oldGeometry.size())
        return;

    // 尺寸变化 → 视口世代递增；携带旧世代的输入将被拒绝（Q-007）
    ++m_viewportGeneration;
    emit viewportGenerationChanged(m_viewportGeneration);
    update();
}

QSGNode *SkyViewport::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *)
{
    // THREAD: scenegraph。这里只允许读邮箱快照 + 建纹理，禁止触碰引擎模块。
    auto *node = static_cast<QSGImageNode *>(oldNode);
    if (!node) {
        node = window()->createImageNode();
        node->setOwnsTexture(false);  // 纹理由本项显式管理，生命周期更可控
        // 物理尺寸与逻辑尺寸 1:1 时，线性过滤不会引入额外模糊；缩放时才需要插值
        node->setFiltering(QSGTexture::Linear);
    }

    // 开发期跟踪（STELQUICK_A2_TRACE=1）：场景图线程内部状态，stdout 打印在 GUI 侧看不到
    static int traceCalls = 0;
    const bool trace = qEnvironmentVariableIsSet("STELQUICK_A2_TRACE") && traceCalls < 12;

    // 节点刚被重建（缩放/抓帧/隐藏显示都会触发）时，旧节点连同纹理一起没了。
    // 若帧号已上传过（frameNumber == m_uploadedFrameNumber），下面的大分支不会进，
    // 新节点会一直没纹理 → 批渲染器每帧报警告且画面缺一块。必须在这里补挂。
    if (!oldNode && m_texture) {
        node->setTexture(m_texture);
        node->setTextureCoordinatesTransform(QSGImageNode::NoTransform);
    } else if (!oldNode && !m_texture) {
        // 空节点兜底：1x1 占位。空纹理材质不仅每帧告警，basic 渲染循环下还会
        // 触发持续重绘饿死事件循环（2026-09-20 实测）。
        if (!m_dummyTexture) {
            QImage dummy(1, 1, QImage::Format_RGBA8888);
            dummy.fill(Qt::black);
            m_dummyTexture = window()->createTextureFromImage(dummy);
        }
        if (m_dummyTexture)
            node->setTexture(m_dummyTexture);
    }

    if (m_mailbox) {
        FrameLease lease = m_mailbox->takeLatestFrame();
        const LegacyFrame &frame = lease.frame();
        if (trace) {
            ++traceCalls;
            std::fprintf(stderr,
                         "SGTRACE[%d]: lease.valid=%d frameNumber=%llu uploaded=%llu "
                         "tex=%s rect=%.0fx%.0f win=%p\n",
                         traceCalls, lease.valid() ? 1 : 0,
                         (unsigned long long)frame.frameNumber,
                         (unsigned long long)m_uploadedFrameNumber,
                         m_texture ? "有" : "无",
                         boundingRect().width(), boundingRect().height(),
                         (void *)window());
            std::fflush(stderr);
        }
        if (lease.valid() && frame.frameNumber != m_uploadedFrameNumber) {
            // 上传统计起点：像素拷贝 + createTextureFromImage 全算"上传耗时"
            // （RHI 惰性提交在渲染期发生，此处量的是场景图线程的同步成本）
            QElapsedTimer uploadClock;
            uploadClock.start();
            // 包装槽位内存为 QImage（不拷贝）。格式契约见 LegacyFrame 注释：
            // RGBA8、原点左上、行优先、非预乘 alpha。
            const QImage sourceImage(frame.pixels,
                                     frame.physicalSize.width(),
                                     frame.physicalSize.height(),
                                     qsizetype(frame.rowStride),
                                     QImage::Format_RGBA8888);
            // 深拷贝：createTextureFromImage 的 RHI 路径惰性上传（纹理提交发生在
            // 本帧渲染时），必须保证像素内存不属于邮箱槽位这种会被复用的外部缓冲。
            // 泄漏面：纹理被释放时拷贝随之释放（QImage 引用计数）。
            const QImage image = sourceImage.copy();
            // 源数据自检：像素全零说明投递侧没写进去（与"没上传"是两码事）
            if (trace)
                std::fprintf(stderr, "SGTRACE: 源图 %dx%d stride=%d 中心像素=%08x\n",
                             image.width(), image.height(), int(frame.rowStride),
                             image.pixel(frame.physicalSize.width() / 2,
                                         frame.physicalSize.height() / 2));
            // 变体开关：STELQUICK_SG_OPAQUE=1 时去掉 alpha 标志（走 opaque 材质）。
            // 2026-09-20 Vulkan 黑屏排查用；默认保留 alpha。
            const auto texFlags = qEnvironmentVariableIsSet("STELQUICK_SG_OPAQUE")
                                      ? QQuickWindow::CreateTextureOptions()
                                      : QQuickWindow::CreateTextureOptions(
                                            QQuickWindow::TextureHasAlphaChannel);
            QSGTexture *newTexture = window()->createTextureFromImage(image, texFlags);
            if (trace) {
                std::fprintf(stderr, "SGTRACE: createTextureFromImage → %p",
                             (void *)newTexture);
                if (newTexture) {
                    const QSize ts = newTexture->textureSize();
                    std::fprintf(stderr, " 纹理尺寸=%dx%d", ts.width(), ts.height());
                }
                std::fprintf(stderr, "\n");
                std::fflush(stderr);
            }
            if (newTexture) {

                if (m_texture)
                    delete m_texture;   // 场景图线程内直接释放；RHI 会安排资源回收
                m_texture = newTexture;
                node->setTexture(m_texture);
                node->setTextureCoordinatesTransform(QSGImageNode::NoTransform);
                m_uploadedFrameNumber = frame.frameNumber;
                m_displayedFrameNumber.store(frame.frameNumber);
                // 上传统计（原子量，GUI 线程随时可读）
                {
                    const quint64 us = quint64(uploadClock.nsecsElapsed() / 1000);
                    m_uploadCount.fetch_add(1);
                    m_uploadSumUs.fetch_add(us);
                    quint64 prevMax = m_uploadMaxUs.load();
                    while (us > prevMax && !m_uploadMaxUs.compare_exchange_weak(prevMax, us)) {}
                }
                // GUI 侧的诊断属性：异步通知，不阻塞渲染线程
                const quint64 shown = frame.frameNumber;
                QMetaObject::invokeMethod(this, [this, shown]() {
                    emit displayedFrameNumberChanged(shown);
                }, Qt::QueuedConnection);
            }
        }
    }

    // 逻辑矩形：纹理（物理像素）映射到此矩形，DPR>1 时即为 1:1 设备像素
    node->setRect(boundingRect());
    return node;
}

// ── 消费侧计量（GUI 线程）──────────────────────────────────────────────────
void SkyViewport::sampleDisplayFps()
{
    // THREAD: GUI（QTimer）。每秒采样 displayedFrameNumber 增量 = 显示帧率。
    const quint64 now = m_displayedFrameNumber.load();
    const quint64 delta = (now > m_lastSampledFrameNumber) ? now - m_lastSampledFrameNumber : 0;
    m_lastSampledFrameNumber = now;
    m_displayedFps = double(delta);
    emit statsChanged();

    // P-BRG-04 降级判定：阈值>0 且确有帧在显示但速率低于阈值 → 降级预览。
    // fps==0（完全无帧）不算降级，算"无信号"，避免未启动生产者时误报。
    const bool degraded = (m_degradeThreshold > 0.0) && m_displayedFps > 0.0
                          && m_displayedFps < m_degradeThreshold;
    if (degraded != m_degraded) {
        m_degraded = degraded;
        emit degradedChanged(m_degraded);
    }
}

void SkyViewport::setDegradeThreshold(qreal fps)
{
    if (qFuzzyCompare(m_degradeThreshold, fps))
        return;
    m_degradeThreshold = fps;
    emit degradeThresholdChanged(m_degradeThreshold);
    // 立即复评一次，不等下个采样周期
    sampleDisplayFps();
}

qreal SkyViewport::uploadMeanMs() const
{
    const quint64 count = m_uploadCount.load();
    return count ? qreal(m_uploadSumUs.load()) / qreal(count) / 1000.0 : 0.0;
}

qreal SkyViewport::uploadMaxMs() const
{
    return qreal(m_uploadMaxUs.load()) / 1000.0;
}

} // namespace stelapp
