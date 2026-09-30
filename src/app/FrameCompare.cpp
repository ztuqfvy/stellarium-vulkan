// FrameCompare — 非 inline 部分实现（T37 提炼；动机见头注）。
#include "app/FrameCompare.hpp"

#include "render/legacy/FrameMailbox.hpp"

#include <QQuickItem>
#include <QQuickWindow>

namespace stelapp {
namespace framecmp {

FrameSample grabUpstream(FrameMailbox *mb)
{
    FrameSample s;
    if (!mb)
        return s;
    FrameLease lease = mb->takeLatestFrame();
    if (!lease.valid())
        return s;
    const LegacyFrame &fr = lease.frame();
    if (!fr.pixels || fr.physicalSize.isEmpty())
        return s;
    const QImage view(fr.pixels, fr.physicalSize.width(), fr.physicalSize.height(),
                      fr.rowStride, QImage::Format_RGBA8888);
    s.image = view.copy();   // 租约归还后 pixels 或被复用 ⇒ 必须独立副本
    if (s.image.isNull())
        return s;
    s.valid = true;
    s.frameNumber = fr.frameNumber;
    s.size = fr.physicalSize;
    summarize(s);
    return s;
}

FrameSample grabDownstream(QQuickWindow *w)
{
    FrameSample s;
    if (!w)
        return s;
    const QImage img = w->grabWindow();
    if (img.isNull())
        return s;
    s.image = img.convertToFormat(QImage::Format_RGBA8888);
    if (s.image.isNull())
        return s;
    s.valid = true;
    s.size = s.image.size();
    summarize(s);
    return s;
}

QRect viewportRectPx(QQuickWindow *w)
{
    if (!w)
        return QRect();
    auto *vp = w->findChild<QQuickItem *>(QStringLiteral("skyViewport"));
    if (!vp || !vp->isVisible() || vp->width() < 1 || vp->height() < 1)
        return QRect();
    const QRectF sceneRect(vp->mapToScene(QPointF(0.0, 0.0)), vp->size());
    const qreal dpr = w->effectiveDevicePixelRatio();
    return QRect(qRound(sceneRect.x() * dpr), qRound(sceneRect.y() * dpr),
                 qRound(sceneRect.width() * dpr), qRound(sceneRect.height() * dpr));
}

QString frameText(const FrameSample &s)
{
    if (!s.valid)
        return QStringLiteral("无效（取帧失败）");
    return QStringLiteral("有效 帧号=%1 尺寸=%2x%3 非黑=%4 哈希=%5")
        .arg(s.frameNumber ? QString::number(s.frameNumber)
                           : QStringLiteral("—（下游无帧号概念）"))
        .arg(s.size.width())
        .arg(s.size.height())
        .arg(s.nonBlack, 0, 'f', 4)
        .arg(s.hash, 0, 16);
}

} // namespace framecmp
} // namespace stelapp
