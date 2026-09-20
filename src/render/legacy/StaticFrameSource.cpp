// StaticFrameSource 实现。定位见头文件注释（A2 开发期替代物）。
#include "render/legacy/StaticFrameSource.hpp"

#include "render/legacy/FrameMailbox.hpp"
#include "render/legacy/TestPattern.hpp"

#include <QImage>
#include <QtMath>

namespace stelapp {

QSize StaticFrameSource::physicalSizeFor(const QSize &logicalSize, qreal devicePixelRatio)
{
    const qreal dpr = devicePixelRatio > 0.0 ? devicePixelRatio : 1.0;
    return QSize(qMax(1, qRound(logicalSize.width() * dpr)),
                 qMax(1, qRound(logicalSize.height() * dpr)));
}

bool StaticFrameSource::publishTestPattern(FrameMailbox &mailbox,
                                          const QSize &logicalSize,
                                          qreal devicePixelRatio,
                                          quint64 frameNumber,
                                          quint64 stateNumber,
                                          QString *errorOut)
{
    const QSize physical = physicalSizeFor(logicalSize, devicePixelRatio);
    if (physical.width() < TestPattern::kMarkerInset * 2 + TestPattern::kMarkerSize
        || physical.height() < TestPattern::kMarkerInset * 2 + TestPattern::kMarkerSize) {
        if (errorOut)
            *errorOut = QStringLiteral("视口物理尺寸过小，无法放置角标：%1x%2")
                            .arg(physical.width()).arg(physical.height());
        return false;
    }

    const QImage image = TestPattern::render(physical);
    if (image.format() != QImage::Format_RGBA8888) {
        // 格式契约（LegacyFrame 注释）：RGBA8、非预乘 alpha
        if (errorOut)
            *errorOut = QStringLiteral("测试图案格式非 RGBA8888");
        return false;
    }

    // 开发期对照实验：把同一张图案存成 PNG，供 QML Image 元素加载（隔离 QSGImageNode 路径）
    const QString patternDump = qEnvironmentVariable("STELQUICK_PATTERN_DUMP");
    if (!patternDump.isEmpty()) {
        if (!image.save(patternDump) && errorOut)
            *errorOut = QStringLiteral("图案存盘失败：%1").arg(patternDump);
    }

    LegacyFrame frame;
    frame.frameNumber = frameNumber;
    frame.stateNumber = stateNumber;
    frame.sizeGeneration = mailbox.stats().sizeGeneration; // 与邮箱当前世代对齐
    frame.logicalSize = logicalSize;
    frame.physicalSize = physical;
    frame.rowStride = quint32(image.bytesPerLine());       // RGBA8888 下 = width * 4
    frame.pixels = nullptr;                               // 由邮箱填充

    if (!mailbox.publishFrame(frame, image.constBits(), image.sizeInBytes())) {
        if (errorOut)
            *errorOut = QStringLiteral("邮箱拒收（无可用槽位或世代不符）");
        return false;
    }
    return true;
}

} // namespace stelapp
