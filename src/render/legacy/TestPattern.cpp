// TestPattern 实现。几何布局由 layoutFor() 统一给出，render() 与 probes() 共用同一份布局，
// 避免"画的地方"和"检查的地方"漂移。
#include "render/legacy/TestPattern.hpp"

#include <QPainter>
#include <QRect>

namespace stelapp {

namespace {

// 布局：图案元素的位置全部在这里算一次
struct Layout
{
    QSize size;
    QRect marker[4];       // 0=TL 1=TR 2=BL 3=BR
    int barX0 = 0;
    int barY0 = 0;
    int barW = 0;
    int barH = 0;
    int checkX0 = 0;
    int checkY0 = 0;
    int checkCols = 4;
    int checkRows = 3;
    QRect alphaBlock;
    QPoint center;
    int crossArm = 26;
    int crossThickness = 6;
};

Layout layoutFor(const QSize &size)
{
    Layout l;
    l.size = size;
    const int W = size.width();
    const int H = size.height();
    const int m = TestPattern::kMarkerSize;
    const int in = TestPattern::kMarkerInset;

    l.marker[0] = QRect(in, in, m, m);
    l.marker[1] = QRect(W - in - m, in, m, m);
    l.marker[2] = QRect(in, H - in - m, m, m);
    l.marker[3] = QRect(W - in - m, H - in - m, m, m);

    l.barW = qMax(12, int(W * 0.15));
    l.barH = TestPattern::kBarHeight;
    l.barX0 = (W - l.barW * 4) / 2;
    l.barY0 = int(H * 0.34);

    l.checkX0 = int(W * 0.12);
    l.checkY0 = int(H * 0.62);

    l.alphaBlock = QRect(int(W * 0.72), int(H * 0.66),
                         TestPattern::kAlphaBlock, TestPattern::kAlphaBlock);

    l.center = QPoint(W / 2, H / 2);
    return l;
}

QColor checkerColor(int row, int col)
{
    return ((row + col) % 2 == 0) ? QColor(0x20, 0x20, 0x20) : QColor(0xe0, 0xe0, 0xe0);
}

} // namespace

QColor TestPattern::backgroundColor()
{
    return QColor(0x30, 0x30, 0x30, 255);
}

QColor TestPattern::markerColor(int corner)
{
    switch (corner) {
    case 0: return QColor(255, 0, 0, 255);     // TL 红
    case 1: return QColor(0, 255, 0, 255);     // TR 绿
    case 2: return QColor(0, 0, 255, 255);     // BL 蓝
    case 3: return QColor(255, 255, 0, 255);   // BR 黄
    default: return QColor(0, 0, 0, 255);
    }
}

QString TestPattern::markerName(int corner)
{
    switch (corner) {
    case 0: return QStringLiteral("左上角标(红)");
    case 1: return QStringLiteral("右上角标(绿)");
    case 2: return QStringLiteral("左下角标(蓝)");
    case 3: return QStringLiteral("右下角标(黄)");
    default: return QStringLiteral("角标?");
    }
}

QColor TestPattern::barColor(int index)
{
    switch (index) {
    case 0: return QColor(255, 0, 0, 255);     // R
    case 1: return QColor(0, 255, 0, 255);     // G
    case 2: return QColor(0, 0, 255, 255);     // B
    case 3: return QColor(255, 255, 255, 255); // 白
    default: return QColor(0, 0, 0, 255);
    }
}

QImage TestPattern::render(const QSize &physicalSize)
{
    const Layout l = layoutFor(physicalSize);
    QImage image(physicalSize, QImage::Format_RGBA8888);
    image.fill(backgroundColor());

    QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing, false);   // 探针依赖硬边界，禁抗锯齿
    p.setPen(Qt::NoPen);

    for (int i = 0; i < 4; ++i)
        p.fillRect(l.marker[i], markerColor(i));

    // 色条之间留 4px 间隙，避免取色落到边界上
    for (int i = 0; i < 4; ++i)
        p.fillRect(QRect(l.barX0 + i * l.barW, l.barY0, l.barW - 4, l.barH), barColor(i));

    for (int row = 0; row < l.checkRows; ++row)
        for (int col = 0; col < l.checkCols; ++col)
            p.fillRect(QRect(l.checkX0 + col * kCellSize, l.checkY0 + row * kCellSize,
                             kCellSize, kCellSize),
                       checkerColor(row, col));

    // 中心十字
    p.fillRect(QRect(l.center.x() - l.crossArm, l.center.y() - l.crossThickness / 2,
                     l.crossArm * 2, l.crossThickness), QColor(255, 255, 255, 255));
    p.fillRect(QRect(l.center.x() - l.crossThickness / 2, l.center.y() - l.crossArm,
                     l.crossThickness, l.crossArm * 2), QColor(255, 255, 255, 255));

    // 半透明块：必须 Source 直写。用默认 SourceOver 会被合成掉 alpha，
    // 那样"透明度是否正确"这条就永远测不出来了。
    p.setCompositionMode(QPainter::CompositionMode_Source);
    p.fillRect(l.alphaBlock, QColor(255, 255, 255, 128));
    p.setCompositionMode(QPainter::CompositionMode_SourceOver);

    return image;
}

QVector<TestPattern::Probe> TestPattern::probes(const QSize &physicalSize)
{
    const Layout l = layoutFor(physicalSize);
    QVector<Probe> out;

    for (int i = 0; i < 4; ++i)
        out.append({ l.marker[i].center(), markerColor(i), markerName(i), true });

    const char *barNames[4] = { "色条 R", "色条 G", "色条 B", "色条 白" };
    for (int i = 0; i < 4; ++i) {
        out.append({ QPoint(l.barX0 + i * l.barW + (l.barW - 4) / 2, l.barY0 + l.barH / 2),
                     barColor(i), QString::fromUtf8(barNames[i]), true });
    }

    out.append({ QPoint(l.checkX0 + kCellSize / 2, l.checkY0 + kCellSize / 2),
                 checkerColor(0, 0), QStringLiteral("棋盘格 奇"), true });
    out.append({ QPoint(l.checkX0 + kCellSize + kCellSize / 2, l.checkY0 + kCellSize / 2),
                 checkerColor(0, 1), QStringLiteral("棋盘格 偶"), true });

    out.append({ l.center, QColor(255, 255, 255, 255), QStringLiteral("中心十字"), true });

    // 半透明块：期望值是"与视口背后的页面底色合成"的结果。
    // 合成发生在 sRGB 空间还是线性空间会影响数值，因此这里不做精确比对，
    // 只做区间判定：既不等于不透明白(255)、也不等于完全没画(背景 48)。
    out.append({ l.alphaBlock.center(), QColor(150, 150, 150, 255),
                 QStringLiteral("半透明块(α=128)"), false });

    return out;
}

bool TestPattern::selfCheck()
{
    // 独立于 render 的硬编码守卫：探针依赖这四色，改动必须显式过这里
    const QColor expect[4] = { QColor(255, 0, 0), QColor(0, 255, 0),
                               QColor(0, 0, 255), QColor(255, 255, 0) };
    for (int i = 0; i < 4; ++i) {
        if (markerColor(i) != expect[i])
            return false;
    }
    return backgroundColor() == QColor(0x30, 0x30, 0x30) && barColor(3) == QColor(255, 255, 255);
}

} // namespace stelapp
