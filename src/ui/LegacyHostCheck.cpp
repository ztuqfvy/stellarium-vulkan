// LegacyHostCheck 实现。判据设计见头文件注释。

#include "ui/LegacyHostCheck.hpp"

#include "render/legacy/FrameMailbox.hpp"
#include "render/legacy/LegacySkyHost.hpp"
#include "render/legacy/LegacyTestScene.hpp"

#include <QGuiApplication>
#include <QImage>
#include <QSet>
#include <QWindow>

#include <algorithm>
#include <cmath>
#include <vector>

namespace stelapp {

namespace {

// FNV-1a 64：对整帧字节做内容指纹。用它而不是"抽样几个像素"，
// 是因为抽样会漏掉"局部在变、其余是静态残留"这种最能骗过人的情形。
quint64 fnv1a64(const quint8 *data, qsizetype size)
{
    quint64 h = 1469598103934665603ULL;
    for (qsizetype i = 0; i < size; ++i)
    {
        h ^= data[i];
        h *= 1099511628211ULL;
    }
    return h;
}

struct FrameProbe
{
    quint64 hash = 0;
    double meanLuminance = 0.0;
    int distinctColors = 0;
    // 太阳质心（已排除四周 32px 方向标记带区域）。-1 表示本帧没找到太阳。
    double sunX = -1.0;
    double sunY = -1.0;
    // 方向标记带的判据量
    double topBandGreen = 0.0;    // 首 24 行的 G 均值
    double bottomBandGreen = 0.0; // 末 24 行的 G 均值
    double leftBandRed = 0.0;     // 首 24 列的 R 均值
    double rightBandRed = 0.0;    // 末 24 列的 R 均值
    bool valid = false;
};

// 从邮箱投递后的帧（**最终交付数据**，不是渲染中间态）计算各判据量。
FrameProbe probeFrame(const LegacyFrame &f)
{
    FrameProbe p;
    if (!f.pixels || f.physicalSize.width() < 96 || f.physicalSize.height() < 96 || f.rowStride == 0)
        return p;

    const int w = f.physicalSize.width();
    const int h = f.physicalSize.height();
    const int stride = int(f.rowStride);
    const quint8 *base = f.pixels;

    p.hash = fnv1a64(base, qsizetype(stride) * h);

    double lumSum = 0.0;
    QSet<quint32> colors;
    const int margin = 32; // 排除方向标记带区域

    double topG = 0.0, botG = 0.0;
    int topN = 0, botN = 0;
    for (int row = 0; row < 24; ++row)
    {
        for (int col = 0; col < w; ++col)
        {
            const quint8 *px = base + qsizetype(row) * stride + col * 4;
            topG += px[1];
            px = base + qsizetype(h - 1 - row) * stride + col * 4;
            botG += px[1];
            ++topN;
            ++botN;
        }
    }
    p.topBandGreen = topG / qMax(1, topN);
    p.bottomBandGreen = botG / qMax(1, botN);

    double leftR = 0.0, rightR = 0.0;
    int leftN = 0, rightN = 0;
    for (int col = 0; col < 24; ++col)
    {
        for (int row = 0; row < h; ++row)
        {
            const quint8 *px = base + qsizetype(row) * stride + col * 4;
            leftR += px[0];
            px = base + qsizetype(row) * stride + (w - 1 - col) * 4;
            rightR += px[0];
            ++leftN;
            ++rightN;
        }
    }
    p.leftBandRed = leftR / qMax(1, leftN);
    p.rightBandRed = rightR / qMax(1, rightN);

    // 第一遍：区域内的均值与最大亮度、颜色多样性
    double maxLum = 0.0;
    int sampleCount = 0;
    for (int row = margin; row < h - margin; ++row)
    {
        for (int col = margin; col < w - margin; ++col)
        {
            const quint8 *px = base + qsizetype(row) * stride + col * 4;
            const double lum = (0.2126 * px[0] + 0.7152 * px[1] + 0.0722 * px[2]) / 255.0;
            lumSum += lum;
            maxLum = qMax(maxLum, lum);
            if (colors.size() < 512 && (sampleCount % 7 == 0))
                colors.insert((quint32(px[0]) << 16) | (quint32(px[1]) << 8) | quint32(px[2]));
            ++sampleCount;
        }
    }
    p.meanLuminance = lumSum / qMax(1, sampleCount);
    p.distinctColors = colors.size();

    // 第二遍：太阳质心。阈值取区域内最大亮度的 90%——本场景里只有太阳圆盘
    // （白昼饱和为纯白、夜晚为暖白）能达到该亮度；星点被刻意压到 0.6 以免干扰。
    const double threshold = maxLum * 0.9;
    if (maxLum > 0.2)
    {
        double sx = 0.0, sy = 0.0;
        qint64 n = 0;
        for (int row = margin; row < h - margin; ++row)
        {
            for (int col = margin; col < w - margin; ++col)
            {
                const quint8 *px = base + qsizetype(row) * stride + col * 4;
                const double lum = (0.2126 * px[0] + 0.7152 * px[1] + 0.0722 * px[2]) / 255.0;
                if (lum >= threshold)
                {
                    sx += col;
                    sy += row;
                    ++n;
                }
            }
        }
        if (n > 0)
        {
            p.sunX = sx / double(n);
            p.sunY = sy / double(n);
        }
    }

    p.valid = true;
    return p;
}

} // namespace

LegacyHostCheckResult LegacyHostCheck::run(const QSize &physicalSize, int frameCount,
                                           double simStartSeconds, double simStepSeconds)
{
    LegacyHostCheckResult result;
    QStringList &details = result.details;
    QStringList &frameLines = result.frames;

    // 可选：首帧存盘路径（辅助留档，不参与判定）
    const QString dumpPath = qEnvironmentVariable("STELQUICK_LEGACY_HOST_DUMP");

    const auto addCheck = [&details, &result](const char *id, bool ok, const QString &note) {
        ++result.checkCount;
        if (!ok)
            ++result.failCount;
        details << QStringLiteral("%1 %2 %3")
                       .arg(QString::fromLatin1(id),
                            ok ? QStringLiteral("PASS") : QStringLiteral("FAIL"), note);
    };

    // ── T6-C01 无窗口环境（在建立连接/渲染之前先取证）────────────────────────
    const int topLevels = QGuiApplication::topLevelWindows().size();
    const int allWindows = QGuiApplication::allWindows().size();
    addCheck("T6-C01", topLevels == 0 && allWindows == 0,
             QStringLiteral("无窗口环境：topLevelWindows=%1 allWindows=%2 "
                            "（自检在窗口创建前、且不进入事件循环，故不存在"
                            "\"paint 事件顺手帮我们画了一帧\"这一可能）")
                 .arg(topLevels)
                 .arg(allWindows));

    if (!qobject_cast<QGuiApplication *>(QCoreApplication::instance()))
    {
        result.summary = QStringLiteral("需要 QGuiApplication");
        result.setupError = result.summary;
        return result;
    }

    FrameMailbox mailbox;
    LegacySkyHost host;
    LegacyTestScene *scene = nullptr;

    LegacySkyHostConfig config;
    config.physicalSize = physicalSize;
    config.maxDimension = 4096;
    config.contextMode = GlContextMode::kOwn; // 自建离屏上下文：完全无窗口
    config.glMajor = 3;
    config.glMinor = 3;
    config.coreProfile = true;
    config.devicePixelRatio = kDevicePixelRatio;

    // 负控入口：允许外部强塞配置，让装配**必然失败**。
    // 动机：四审（Windows）在 runner 上发现"负控能红、被测对象不能红"的不对称——
    // 一个永远不会红的检查器，它的 PASS 不构成证据。本自检必须自己证明能红。
    //
    // 用法：STELQUICK_LEGACY_HOST_SIZE=0x0 → 期望 VERDICT=FAIL、进程退出码 8。
    //
    // 注意：**不能**用"强塞不存在的 GL 版本"当负控——实测（macOS/Apple M3）
    // 请求 GL 9.9 会被静默降级成 4.1 Core，装配照样成功。这意味着两件事：
    //   ① macOS 上"GL 版本请求"不能作为失败注入点；
    //   ② 请求的 GL 版本**不保证拿到**，所以 T6-C02 只记录实测值、不比对请求值。
    const QByteArray forcedSize = qgetenv("STELQUICK_LEGACY_HOST_SIZE");
    if (!forcedSize.isEmpty())
    {
        const QList<QByteArray> parts = forcedSize.split('x');
        if (parts.size() == 2)
            config.physicalSize = QSize(parts.value(0).toInt(), parts.value(1).toInt());
    }

    QString error;
    if (!host.initialize(config, &error))
    {
        result.setupError = QStringLiteral("LegacySkyHost::initialize 失败：%1").arg(error);
        result.summary = result.setupError;
        addCheck("T6-C02", false, result.setupError);
        return result;
    }
    Q_UNUSED(error)

    const LegacyGlInfo &gl = host.glInfo();
    addCheck("T6-C02",
             gl.ownContext && gl.offscreenSurface && gl.majorVersion >= 3,
             QStringLiteral("GL 上下文形态：own=%1 offscreen=%2 version=%3.%4 core=%5 "
                            "renderer=\"%6\"")
                 .arg(gl.ownContext ? 1 : 0)
                 .arg(gl.offscreenSurface ? 1 : 0)
                 .arg(gl.majorVersion)
                 .arg(gl.minorVersion)
                 .arg(gl.coreProfile ? 1 : 0)
                 .arg(gl.renderer));

    // 离屏目标尺寸上限：显式失败而非静默截断（负向检查）
    {
        QString over;
        const bool overRejected = !host.setTargetSize(QSize(config.maxDimension + 1, 64), &over);
        addCheck("T6-C03", overRejected,
                 QStringLiteral("超上限尺寸被显式拒绝（不静默截断）：%1")
                     .arg(overRejected ? over : QStringLiteral("未拒绝——这是缺陷")));
    }

    // 装配渲染回调：旧宿主在此画天空。本测试用 LegacyTestScene（开发期替代物）。
    // 注：T6-C04 在两条互斥路径上出现——装配失败（此处提前返回）或装配结果（循环后）。
    host.withContextCurrent([&scene]() { scene = new LegacyTestScene(); });
    if (!scene)
    {
        result.setupError = QStringLiteral("无法在自建上下文中装配测试场景（withContextCurrent 失败）。");
        result.summary = result.setupError;
        addCheck("T6-C04", false, result.setupError);
        host.shutdown();
        return result;
    }

    host.setRenderCallback([scene](double dt, double simSeconds, const QSize &size) {
        Q_UNUSED(dt)
        scene->render(simSeconds, size);
    });
    host.attachMailbox(&mailbox);

    // ── 显式驱动 N 帧 ──────────────────────────────────────────────────────
    struct FrameMeta
    {
        quint64 frameNumber = 0;
        quint64 stateNumber = 0;
        quint32 rowStride = 0;
        quint32 sizeGeneration = 0;
        QSize physicalSize;
        QSize logicalSize;
        bool present = false;
    };
    std::vector<FrameProbe> probes;
    std::vector<FrameMeta> metas;
    probes.reserve(size_t(frameCount));
    metas.reserve(size_t(frameCount));
    for (int i = 0; i < frameCount; ++i)
    {
        const double sim = simStartSeconds + i * simStepSeconds;
        QString frameError;
        const bool ok = host.renderOneFrame(sim, &frameError);
        if (!ok)
        {
            frameLines << QStringLiteral("frame=%1 sim=%2 渲染失败：%3")
                              .arg(i + 1).arg(sim, 0, 'f', 3).arg(frameError);
            probes.push_back(FrameProbe{});
            metas.push_back(FrameMeta{});
            continue;
        }
        // 从**邮箱**取帧：验证的是最终交付数据，顺带覆盖 FrameMailbox 投递路径。
        FrameLease lease = mailbox.takeLatestFrame();
        if (!lease.valid())
        {
            frameLines << QStringLiteral("frame=%1 sim=%2 邮箱取帧失败")
                              .arg(i + 1).arg(sim, 0, 'f', 3);
            probes.push_back(FrameProbe{});
            metas.push_back(FrameMeta{});
            continue;
        }
        const LegacyFrame &f = lease.frame();
        const FrameProbe p = probeFrame(f);
        probes.push_back(p);

        // 可选：把首帧存成 PNG，供人眼确认"确实是一张合理的图"（不是黑屏、
        // 上下没颠倒、顶部有绿带左侧有红带）。程序化判据是主证据，这个只是辅助留档。
        if (i == 0 && !dumpPath.isEmpty() && f.pixels)
        {
            const QImage img(f.pixels, f.physicalSize.width(), f.physicalSize.height(),
                             int(f.rowStride), QImage::Format_RGBA8888);
            const bool saved = img.copy().save(dumpPath);
            frameLines << QStringLiteral("dump=%1 %2（%3x%4, rowStride=%5）")
                              .arg(dumpPath, saved ? QStringLiteral("已保存") : QStringLiteral("保存失败"))
                              .arg(f.physicalSize.width()).arg(f.physicalSize.height())
                              .arg(f.rowStride);
        }

        FrameMeta meta;
        meta.frameNumber = f.frameNumber;
        meta.stateNumber = f.stateNumber;
        meta.rowStride = f.rowStride;
        meta.sizeGeneration = f.sizeGeneration;
        meta.physicalSize = f.physicalSize;
        meta.logicalSize = f.logicalSize;
        meta.present = true;
        metas.push_back(meta);

        frameLines << QStringLiteral("frame=%1 sim=%2 num=%3 hash=0x%4 mean=%5 sunX=%6 "
                                     "physical=%7x%8 logical=%9x%10 stride=%11 gen=%12 "
                                     "render=%13ms readback=%14ms")
                          .arg(i + 1)
                          .arg(sim, 0, 'f', 3)
                          .arg(f.frameNumber)
                          .arg(p.hash, 16, 16, QLatin1Char('0'))
                          .arg(p.meanLuminance, 0, 'f', 4)
                          .arg(p.sunX, 0, 'f', 1)
                          .arg(f.physicalSize.width()).arg(f.physicalSize.height())
                          .arg(f.logicalSize.width()).arg(f.logicalSize.height())
                          .arg(f.rowStride)
                          .arg(f.sizeGeneration)
                          .arg(host.stats().lastRenderMs)
                          .arg(host.stats().lastReadbackMs);
    }

    // ── 一致性：请求 N 帧必须得到 N 帧 ──────────────────────────────────────
    const LegacySkyHost::Stats hs = host.stats();
    const FrameMailbox::Stats ms = mailbox.stats();
    addCheck("T6-C05",
             hs.requested == quint64(frameCount) && hs.published == quint64(frameCount)
                 && hs.failed == 0 && hs.droppedByMailbox == 0,
             QStringLiteral("请求 %1 帧 → 投递 %2 帧（丢失 %3，失败 %4）；邮箱侧 published=%5 dropped=%6")
                 .arg(hs.requested).arg(hs.published).arg(hs.droppedByMailbox)
                 .arg(hs.failed).arg(ms.published).arg(ms.dropped));

    // ── 队列有界（不能随时间增长）──────────────────────────────────────────
    addCheck("T6-C06",
             ms.completeSlots <= FrameMailbox::kFrameSlotCount,
             QStringLiteral("邮箱队列有界：completeSlots=%1 ≤ %2，readersHeld=%3")
                 .arg(ms.completeSlots).arg(FrameMailbox::kFrameSlotCount).arg(ms.readersHeld));

    // ── 帧元数据：序号递增、步长、尺寸、DPR 换算、世代恒定 ────────────────────
    {
        const QSize expectLogical(int(std::lround(physicalSize.width() / kDevicePixelRatio)),
                                  int(std::lround(physicalSize.height() / kDevicePixelRatio)));
        const qsizetype expectStride = qsizetype(physicalSize.width()) * 4;
        bool metaOk = true;
        QString why;
        quint64 prevNumber = 0;
        quint32 firstGen = 0;
        for (size_t i = 0; i < metas.size(); ++i)
        {
            const FrameMeta &m = metas[i];
            if (!m.present) { metaOk = false; why = QStringLiteral("第 %1 帧元数据缺失").arg(i + 1); break; }
            if (m.frameNumber <= prevNumber)
            { metaOk = false; why = QStringLiteral("帧序号未严格递增（第 %1 帧 num=%2）").arg(i + 1).arg(m.frameNumber); break; }
            prevNumber = m.frameNumber;
            if (qsizetype(m.rowStride) != expectStride)
            { metaOk = false; why = QStringLiteral("行步长 %1 ≠ 期望 %2").arg(m.rowStride).arg(expectStride); break; }
            if (m.physicalSize != physicalSize)
            { metaOk = false; why = QStringLiteral("物理尺寸 %1x%2 ≠ 期望 %3x%4")
                                        .arg(m.physicalSize.width()).arg(m.physicalSize.height())
                                        .arg(physicalSize.width()).arg(physicalSize.height()); break; }
            if (m.logicalSize != expectLogical)
            { metaOk = false; why = QStringLiteral("逻辑尺寸 %1x%2 ≠ physical/dpr=%3x%4")
                                        .arg(m.logicalSize.width()).arg(m.logicalSize.height())
                                        .arg(expectLogical.width()).arg(expectLogical.height()); break; }
            if (i == 0)
                firstGen = m.sizeGeneration;
            else if (m.sizeGeneration != firstGen)
            { metaOk = false; why = QStringLiteral("尺寸未变但世代从 %1 变为 %2").arg(firstGen).arg(m.sizeGeneration); break; }
            // stateNumber 必须与仿真时间同步（毫秒量化），否则"内容随仿真时间变化"缺元数据支撑
            const quint64 expectState = quint64(std::llround((simStartSeconds + double(i) * simStepSeconds) * 1000.0));
            if (m.stateNumber != expectState)
            { metaOk = false; why = QStringLiteral("状态编号 %1 ≠ 仿真时间量化值 %2").arg(m.stateNumber).arg(expectState); break; }
        }
        const QString note = metaOk
            ? QStringLiteral("帧序号严格递增、行步长=%1、物理=%2x%3、逻辑=%4x%5（=physical/dpr，dpr=%6）、"
                             "世代恒为 %7、状态编号=仿真时间×1000")
                  .arg(expectStride).arg(physicalSize.width()).arg(physicalSize.height())
                  .arg(expectLogical.width()).arg(expectLogical.height())
                  .arg(kDevicePixelRatio).arg(firstGen)
            : QStringLiteral("元数据不符：%1").arg(why);
        addCheck("T6-C07", metaOk, note);
    }

    // ── 行序翻转与水平方向（对最终交付数据取证）─────────────────────────────
    {
        bool orientOk = !probes.empty();
        double minTopGap = 1e9, minLeftGap = 1e9;
        for (const FrameProbe &p : probes)
        {
            if (!p.valid) { orientOk = false; continue; }
            const double topGap = p.topBandGreen - p.bottomBandGreen;   // 顶部绿带
            const double leftGap = p.leftBandRed - p.rightBandRed;      // 左侧红带
            minTopGap = qMin(minTopGap, topGap);
            minLeftGap = qMin(minLeftGap, leftGap);
            // 带宽 24 行/列，占 540 行 / 960 列的 4.4% / 2.5%；
            // 带内为纯色（G=255 或 R=255），带外是天空/星/日，均值上必有数十级差距。
            if (topGap < 30.0 || leftGap < 20.0)
                orientOk = false;
        }
        addCheck("T6-C08", orientOk,
                 QStringLiteral("行序翻转与水平方向：顶部绿带优势最小 %1（>30 合格），"
                                "左侧红带优势最小 %2（>20 合格）——若忘记翻转，顶部带会出现在图像底部")
                     .arg(minTopGap, 0, 'f', 1)
                     .arg(minLeftGap, 0, 'f', 1));
    }

    // ── 内容非平凡 ─────────────────────────────────────────────────────────
    {
        bool nonTrivial = true;
        double minMean = 1e9;
        int minColors = 1 << 30;
        for (const FrameProbe &p : probes)
        {
            if (!p.valid) { nonTrivial = false; continue; }
            minMean = qMin(minMean, p.meanLuminance);
            minColors = qMin(minColors, p.distinctColors);
            if (p.meanLuminance <= 0.0 || p.distinctColors < 16)
                nonTrivial = false;
        }
        addCheck("T6-C09", nonTrivial,
                 QStringLiteral("帧内容非平凡：最少不同颜色数=%1（≥16），最小平均亮度=%2（>0）")
                     .arg(minColors).arg(minMean, 0, 'f', 4));
    }

    // ── 帧内容随**仿真**时间变化（T6 核心判据）──────────────────────────────
    {
        QSet<quint64> hashes;
        for (const FrameProbe &p : probes)
            if (p.valid)
                hashes.insert(p.hash);
        addCheck("T6-C10", hashes.size() == frameCount,
                 QStringLiteral("%1 帧得到 %2 个互不相同的像素指纹——内容确实在变，"
                                "不是静态残留或缓存复读")
                     .arg(frameCount).arg(hashes.size()));
    }

    // ── 可复现性：同一仿真时间重渲染必得同一像素 ────────────────────────────
    {
        const double sim = simStartSeconds;
        QString e;
        bool ok = host.renderOneFrame(sim, &e);
        quint64 repeatHash = 0;
        if (ok)
        {
            FrameLease lease = mailbox.takeLatestFrame();
            if (lease.valid())
                repeatHash = probeFrame(lease.frame()).hash;
            else
                ok = false;
        }
        const quint64 firstHash = probes.empty() ? 0 : probes.front().hash;
        addCheck("T6-C11", ok && repeatHash != 0 && repeatHash == firstHash,
                 QStringLiteral("同一仿真时间 sim=%1 重渲染 → 指纹 %2（首帧 %3）——"
                                "确定性成立，说明内容由仿真时间决定，而非随机或墙钟")
                     .arg(sim, 0, 'f', 3)
                     .arg(repeatHash, 16, 16, QLatin1Char('0'))
                     .arg(firstHash, 16, 16, QLatin1Char('0')));
    }

    // ── 太阳水平位置随仿真时间单调推进 ──────────────────────────────────────
    {
        bool monotonic = true;
        bool haveSun = true;
        double prevX = -1e9;
        QString trace;
        for (const FrameProbe &p : probes)
        {
            if (!p.valid || p.sunX < 0.0) { haveSun = false; continue; }
            if (p.sunX <= prevX)
                monotonic = false;
            prevX = p.sunX;
            trace += QStringLiteral("%1 ").arg(p.sunX, 0, 'f', 1);
        }
        addCheck("T6-C12", haveSun && monotonic,
                 QStringLiteral("太阳质心水平位置逐帧单调推进：%1（有向运动，非噪声抖动）")
                     .arg(trace.trimmed()));
    }

    // ── 耗时统计可得 ───────────────────────────────────────────────────────
    addCheck("T6-C13", hs.lastRenderMs >= 0 && hs.lastReadbackMs >= 0 && hs.lastTotalMs >= 0,
             QStringLiteral("耗时统计可用：lastRender=%1ms lastReadback=%2ms total=%3ms "
                            "bytesPerFrame=%4（T9 长跑统计的基础量）")
                 .arg(hs.lastRenderMs).arg(hs.lastReadbackMs).arg(hs.lastTotalMs)
                 .arg(hs.bytesPerFrame));

    // ── 场景 GL 资源装配结果（着色器编译 / VAO / VBO）────────────────────────
    // 着色器是首帧惰性编译的，所以这一项只能放在循环之后判。
    addCheck("T6-C04", scene->ok(),
             scene->ok()
                 ? QStringLiteral("场景 GL 资源装配成功（着色器编译+链接、VAO/VBO 创建）")
                 : QStringLiteral("场景 GL 资源装配失败：%1").arg(scene->lastError()));

    // ── 收尾：在上下文 current 的前提下释放客户端 GL 资源 ────────────────────
    host.withContextCurrent([&scene]() {
        if (scene)
        {
            scene->releaseResources();
            delete scene;
            scene = nullptr;
        }
    });
    if (scene) { delete scene; scene = nullptr; }
    host.attachMailbox(nullptr);
    host.shutdown();

    // ── 再确认一次"无窗口"未被破坏 ──────────────────────────────────────────
    addCheck("T6-C14",
             QGuiApplication::topLevelWindows().isEmpty() && QGuiApplication::allWindows().isEmpty(),
             QStringLiteral("全程未创建任何窗口：topLevelWindows=%1 allWindows=%2")
                 .arg(QGuiApplication::topLevelWindows().size())
                 .arg(QGuiApplication::allWindows().size()));

    result.ran = true;
    result.pass = (result.failCount == 0);
    result.summary = QStringLiteral("T6 旧宿主显式帧驱动自检：%1/%2 项通过")
                         .arg(result.checkCount - result.failCount)
                         .arg(result.checkCount);
    // 按编号排序：C04/C14 是在测量阶段之后补上的，不排序的话输出顺序会让人误以为漏项。
    std::sort(details.begin(), details.end());
    return result;
}

} // namespace stelapp
