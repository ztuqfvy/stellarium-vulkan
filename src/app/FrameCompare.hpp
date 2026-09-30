/*
 * FrameCompare — 帧采样与逐像素比较的共用工具（T37 提炼）。
 *
 * 为什么提炼成头文件：NightModeProbe（T37-A 探针）与 NightModeCheck（T37-C 判据）
 * 必须用**同一把尺子** —— 探针与判据各持一套实现的话，口径会漂移
 * （"探针说 0 差异、判据说 3 像素差异"这种鸡毛蒜皮会浪费一轮排查）。
 * 对照量同源是纪律（陷阱：对照量必须换来源；这里反过来的教训是
 * "主量与探针的口径必须一致，否则探针的结论无法迁移到判据"）。
 *
 * 两条独立读回路径（陷阱 43）：
 *   上游 grabUpstream   = FrameMailbox 原始 RGBA（我们真正交给 Qt Quick 的东西）
 *   下游 grabDownstream = QQuickWindow::grabWindow()（用户眼睛看到的东西）
 *
 * 坐标约定：图像均为 Format_RGBA8888、原点左上、无预乘。
 * viewportRectPx 返回**抓帧坐标系**（物理像素）里的矩形 —— 换算链照抄
 * A2FrameCheck（mapToScene → ×DPR）。
 */
#pragma once

#include <QImage>
#include <QRect>
#include <QSize>
#include <QString>
#include <QVector>
#include <QtGlobal>

class QQuickWindow;

namespace stelapp {

class FrameMailbox;

namespace framecmp {

//! 一帧的独立副本 + 结构性摘要（租约立刻归还，比较在副本上做）。
struct FrameSample
{
    bool valid = false;
    quint64 frameNumber = 0;   //!< 帧泵单调递增（上游有、下游恒 0）
    QSize size;
    QImage image;              //!< 归一化到 Format_RGBA8888
    quint64 hash = 0;          //!< FNV-1a 逐字节
    double nonBlack = 0.0;     //!< 非黑像素比例（区分"真画了"与"全黑/不完整帧"）
};

//! 差异统计。differing/maxDelta/meanDelta 都只看 RGB（不看 alpha）。
struct DiffStats
{
    bool comparable = false;
    qint64 pixels = 0;
    qint64 differing = 0;
    int maxDelta = 0;
    double meanDelta = 0.0;   //!< 平均"通道最大差"
    bool sameHash = false;
    //! 差异像素的空间分布：bbox + 高度 8 等分计数 —— 区分"天空内容变了"
    //! 与"QML UI 变了"（T37-A 实测：0.2% 的全图差异可以全部落在工具栏一条带里）。
    int minX = -1, minY = -1, maxX = -1, maxY = -1;
    QVector<qint64> bands;
};

inline void summarize(FrameSample &s)
{
    if (s.image.isNull())
        return;
    quint64 h = 1469598103934665603ull;
    const quint64 prime = 1099511628211ull;
    qint64 nonBlack = 0;
    const int w = s.image.width();
    const int ht = s.image.height();
    for (int y = 0; y < ht; ++y)
    {
        const uchar *row = s.image.constScanLine(y);
        for (int x = 0; x < w; ++x)
        {
            const uchar *p = row + x * 4;
            h ^= p[0]; h *= prime;
            h ^= p[1]; h *= prime;
            h ^= p[2]; h *= prime;
            h ^= p[3]; h *= prime;
            if (p[0] || p[1] || p[2])
                ++nonBlack;
        }
    }
    s.hash = h;
    const qint64 total = qint64(w) * qint64(ht);
    s.nonBlack = total ? double(nonBlack) / double(total) : 0.0;
}

//! roi 为空矩形 = 全图；否则只统计 roi 内（把"天空视口"从"QML 工具栏"里切出来）。
inline DiffStats diffOf(const FrameSample &a, const FrameSample &b,
                        const QRect &roi = QRect())
{
    DiffStats d;
    if (!a.valid || !b.valid || a.image.isNull() || b.image.isNull()
        || a.image.size() != b.image.size())
        return d;
    d.comparable = true;
    d.sameHash = (a.hash == b.hash);
    const int w = a.image.width();
    const int ht = a.image.height();

    int x0 = 0, x1 = w - 1, y0 = 0, y1 = ht - 1;
    if (roi.isValid())
    {
        x0 = qMax(0, roi.left());
        x1 = qMin(w - 1, roi.right());
        y0 = qMax(0, roi.top());
        y1 = qMin(ht - 1, roi.bottom());
    }
    if (x1 < x0 || y1 < y0)
        return d;

    d.bands = QVector<qint64>(8, 0);
    quint64 sum = 0;
    for (int y = y0; y <= y1; ++y)
    {
        const uchar *ra = a.image.constScanLine(y);
        const uchar *rb = b.image.constScanLine(y);
        qint64 rowDiff = 0;
        for (int x = x0; x <= x1; ++x)
        {
            const uchar *pa = ra + x * 4;
            const uchar *pb = rb + x * 4;
            const int m = qMax(qMax(qAbs(int(pa[0]) - int(pb[0])),
                                    qAbs(int(pa[1]) - int(pb[1]))),
                               qAbs(int(pa[2]) - int(pb[2])));
            if (m > 0)
            {
                ++d.differing;
                ++rowDiff;
                if (d.minX < 0 || x < d.minX)
                    d.minX = x;
                if (x > d.maxX)
                    d.maxX = x;
                if (d.minY < 0)
                    d.minY = y;
                d.maxY = y;
            }
            if (m > d.maxDelta)
                d.maxDelta = m;
            sum += quint64(m);
        }
        if (rowDiff > 0)
        {
            const int band = qBound(0, ((y - y0) * 8) / qMax(1, y1 - y0 + 1), 7);
            d.bands[band] += rowDiff;
        }
    }
    d.pixels = qint64(x1 - x0 + 1) * qint64(y1 - y0 + 1);
    d.meanDelta = d.pixels ? double(sum) / double(d.pixels) : 0.0;
    return d;
}

inline QString diffText(const DiffStats &d)
{
    if (!d.comparable)
        return QStringLiteral("不可比（帧缺失或尺寸不一致）");
    if (d.differing == 0)
        return QStringLiteral("差异像素=0（逐位相同，哈希%1）")
            .arg(d.sameHash ? QStringLiteral("相同") : QStringLiteral("不同"));
    const double pct = d.pixels ? 100.0 * double(d.differing) / double(d.pixels) : 0.0;
    QString bands;
    for (int i = 0; i < d.bands.size(); ++i)
        bands += QStringLiteral("%1%2").arg(i ? QStringLiteral("/") : QString())
                     .arg(d.bands.at(i));
    return QStringLiteral("差异像素=%1/%2（%3%） 最大通道差=%4 平均通道差=%5 哈希%6 "
                          "包围盒=(%7,%8)-(%9,%10) 高度8带=%11")
        .arg(d.differing)
        .arg(d.pixels)
        .arg(pct, 0, 'f', 3)
        .arg(d.maxDelta)
        .arg(d.meanDelta, 0, 'f', 4)
        .arg(d.sameHash ? QStringLiteral("相同") : QStringLiteral("不同"))
        .arg(d.minX).arg(d.minY).arg(d.maxX).arg(d.maxY)
        .arg(bands);
}

//! 上游：FrameMailbox 里最新一帧。
FrameSample grabUpstream(FrameMailbox *mb);

//! 下游：窗口合成结果。@p w 为空或抓帧失败返回无效样本。
FrameSample grabDownstream(QQuickWindow *w);

//! 天空视口在**抓帧坐标系**里的矩形（物理像素）；空 = 找不到/不可见。
QRect viewportRectPx(QQuickWindow *w);

//! 一帧的可读摘要。
QString frameText(const FrameSample &s);

} // namespace framecmp
} // namespace stelapp
