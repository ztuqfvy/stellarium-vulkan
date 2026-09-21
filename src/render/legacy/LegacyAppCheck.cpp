// LegacyAppCheck 实现。判据设计与引导方式见头文件注释。

#include "render/legacy/LegacyAppCheck.hpp"

#include "StelMainView.hpp"
#include "core/StelApp.hpp"
#include "core/StelCore.hpp"
#include "core/StelSkyDrawer.hpp"
#include "render/legacy/FrameMailbox.hpp"
#include "render/legacy/LegacySkyHost.hpp"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QImage>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QSet>
#include <QThread>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace stelapp {

namespace {

// 固定纪元（J2000）：帧内容 = f(kJdEpoch + simSeconds)，映射完全确定。
constexpr double kJdEpoch = 2451545.0;
// 每测量帧的仿真推进（秒）。600s = 2.5°星空旋转 ≈ 9px @1280 宽——
// 足以保证逐帧指纹互异（数值依据写在这里，防止后人"随手改小"让 C05 变假红）。
constexpr double kSimStep = 600.0;
constexpr int kWarmupFrames = 30;

// FNV-1a 64：整帧字节指纹（与 T6 自检同一套，保证两份证据可比）。
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
    bool valid = false;
};

FrameProbe probeFrame(const LegacyFrame &f)
{
    FrameProbe p;
    if (!f.pixels || f.physicalSize.width() < 8 || f.physicalSize.height() < 8 || f.rowStride == 0)
        return p;
    const int w = f.physicalSize.width();
    const int h = f.physicalSize.height();
    const int stride = int(f.rowStride);
    const quint8 *base = f.pixels;

    p.hash = fnv1a64(base, qsizetype(stride) * h);
    double lumSum = 0.0;
    int n = 0;
    QSet<quint32> colors;
    for (int row = 0; row < h; ++row)
    {
        for (int col = 0; col < w; ++col)
        {
            const quint8 *px = base + qsizetype(row) * stride + col * 4;
            lumSum += (0.2126 * px[0] + 0.7152 * px[1] + 0.0722 * px[2]) / 255.0;
            if (colors.size() < 1024 && (n % 13 == 0))
                colors.insert((quint32(px[0]) << 16) | (quint32(px[1]) << 8) | quint32(px[2]));
            ++n;
        }
    }
    p.meanLuminance = lumSum / qMax(1, n);
    p.distinctColors = int(colors.size());
    p.valid = true;
    return p;
}

struct PixelDiff
{
    qint64 differing = 0;  //!< 有差异的像素数
    int maxDelta = 0;      //!< 最大单通道差
    double meanDelta = 0.0;//!< 差异像素的平均通道差
};

PixelDiff diffFrames(const QByteArray &a, const QByteArray &b, qsizetype stride, int height)
{
    Q_UNUSED(stride)
    Q_UNUSED(height)
    PixelDiff d;
    const qsizetype n = qMin(a.size(), b.size());
    const quint8 *pa = reinterpret_cast<const quint8 *>(a.constData());
    const quint8 *pb = reinterpret_cast<const quint8 *>(b.constData());
    const qsizetype pixels = n / 4;
    qint64 channelSum = 0;
    qint64 channelCount = 0;
    for (qsizetype px = 0; px < pixels; ++px)
    {
        const quint8 *x = pa + px * 4;
        const quint8 *y = pb + px * 4;
        int dm = 0;
        for (int c = 0; c < 4; ++c)
        {
            const int delta = qAbs(int(x[c]) - int(y[c]));
            if (delta > 0)
                channelSum += delta;
            channelCount++;
            dm = qMax(dm, delta);
        }
        if (dm > 0)
            ++d.differing;
        if (dm > d.maxDelta)
            d.maxDelta = dm;
    }
    d.meanDelta = channelCount > 0 ? double(channelSum) / double(channelCount) : 0.0;
    return d;
}

} // namespace

LegacyAppCheckResult LegacyAppCheck::run(QSettings *confSettings)
{
    Q_UNUSED(confSettings) // 引擎配置已在 main.cpp 完成；本自检不改配置（确定性开关只动运行时对象）

    LegacyAppCheckResult result;
    QStringList &details = result.details;
    QStringList &frameLines = result.frames;

    const auto addCheck = [&details, &result](const char *id, bool ok, const QString &note) {
        ++result.checkCount;
        if (!ok)
            ++result.failCount;
        details << QStringLiteral("%1 %2 %3")
                       .arg(QString::fromLatin1(id),
                            ok ? QStringLiteral("PASS") : QStringLiteral("FAIL"), note);
    };

    // ── 环境变量与参数 ────────────────────────────────────────────────────────
    int frameCount = qEnvironmentVariableIntValue("STELA3_FRAMES");
    if (frameCount <= 0)
        frameCount = 8;
    const QString dumpPath = qEnvironmentVariable("STELA3_DUMP");

    QSize fboSize(1280, 720);
    const QByteArray forcedSize = qgetenv("STELA3_SIZE");
    if (!forcedSize.isEmpty())
    {
        const QList<QByteArray> parts = forcedSize.split('x');
        if (parts.size() == 2)
            fboSize = QSize(parts.value(0).toInt(), parts.value(1).toInt());
        // 0x0 是有意为之的负控：让 LegacySkyHost 装配必败（A3-C03 红、退出码 8）。
    }

    // ── A3-C01 引擎无头初始化（A3 第一未知数）─────────────────────────────────
    StelMainView *mainWin = new StelMainView(confSettings);
    mainWin->setAttribute(Qt::WA_DontShowOnScreen, true);
    mainWin->resize(fboSize.width() > 0 ? fboSize.width() : 1280,
                    fboSize.height() > 0 ? fboSize.height() : 720);
    mainWin->show();

    QElapsedTimer bootClock;
    bootClock.start();
    for (int i = 0; i < 600 && !StelApp::isInitialized(); ++i)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        if (!StelApp::isInitialized())
            QThread::msleep(10);
    }

    if (!StelApp::isInitialized())
    {
        result.setupError =
            QStringLiteral("WA_DontShowOnScreen 引导失败：%1ms 内 initializeGL 未触发，"
                           "StelApp 未初始化。此路不通，需换引导方式（如显式调用 "
                           "glWidget 初始化），这正是 A3 要暴露的未知数。")
                .arg(bootClock.elapsed());
        result.summary = result.setupError;
        addCheck("A3-C01", false, result.setupError);
        delete mainWin;
        return result;
    }
    addCheck("A3-C01", true,
             QStringLiteral("引擎无头初始化成功：StelApp::init 由 initializeGL 触发，"
                            "耗时 %1ms，全程 WA_DontShowOnScreen（窗口不上屏）、未进入 exec。")
                 .arg(bootClock.elapsed()));

    // ── A3-C02 GL 形态记录（不采信请求值，只记拿到值）──────────────────────────
    {
        const StelMainView::GLInfo &gi = StelMainView::getInstance().getGLInformation();
        const bool ok = (gi.mainContext != nullptr) && gi.majorVersion >= 3;
        addCheck("A3-C02", ok,
                 QStringLiteral("引擎主上下文：version=%1.%2 core=%3 highGraphics=%4 renderer=\"%5\"")
                     .arg(gi.majorVersion)
                     .arg(gi.mainContext ? gi.mainContext->format().minorVersion() : 0)
                     .arg(gi.isCoreProfile ? 1 : 0)
                     .arg(gi.isHighGraphicsMode ? 1 : 0)
                     .arg(gi.renderer));
    }

    // ── 确定性开关（只动运行时对象，不写用户 config.ini）──────────────────────
    StelApp &app = StelApp::getInstance();
    app.getCore()->setTimeRate(0.0);
    app.getCore()->getSkyDrawer()->setFlagTwinkle(false);
    // 人眼自适应（viewing/use_luminance_adaptation 默认开）：update(dt) 会按 dt
    // 累积调整全图亮度——首跑实测它让"同 JD 重渲染"产生 99.6% 像素、maxΔ=25 的
    // 全局偏移（update(600) vs update(0)）。它本质是历史状态累积器，必须关。
    app.getCore()->getSkyDrawer()->setFlagLuminanceAdaptation(false);
    app.setDevicePixelsPerPixel(1.0);

    // ── A3-C03 借用引擎上下文装配帧桥（负控注入点）────────────────────────────
    mainWin->glContextMakeCurrent();
    FrameMailbox mailbox;
    LegacySkyHost host;
    LegacySkyHostConfig cfg;
    cfg.contextMode = GlContextMode::kBorrowed; // 引擎资源在 glWidget 上下文里，我们借用它
    cfg.physicalSize = fboSize;
    cfg.maxDimension = 4096;
    cfg.devicePixelRatio = 1.0;

    QString error;
    if (!host.initialize(cfg, &error))
    {
        result.setupError = QStringLiteral("LegacySkyHost::initialize（借用模式）失败：%1").arg(error);
        result.summary = result.setupError;
        addCheck("A3-C03", false, result.setupError);
        mainWin->deinit();
        delete mainWin;
        return result;
    }
    addCheck("A3-C03", true,
             QStringLiteral("帧桥装配成功（借用引擎上下文）：离屏 FBO %1x%2，读回行步长 %3")
                 .arg(fboSize.width()).arg(fboSize.height()).arg(host.readbackInfo().rowStride));

    // ── 渲染回调：A3 的核心替换点（T6 是合成场景，这里是真实引擎）─────────────
    // 注意顺序：先注入 JD，再 update（引擎时钟已停，不会自己推 JD），最后 draw。
    // draw() 直接画进当前 FBO（LegacySkyHost 已绑好离屏目标）——与 doScreenshot()
    // 已验证的"画进自选 FBO"路径同源。
    host.setRenderCallback([](double dt, double simSeconds, const QSize &) {
        StelApp &a = StelApp::getInstance();
        a.getCore()->setJD(kJdEpoch + simSeconds);
        a.update(dt);
        a.draw();
    });
    host.attachMailbox(&mailbox);

    // ── 预热：等异步资源加载完成 + 引擎稳定 ───────────────────────────────────
    for (int i = 0; i < kWarmupFrames; ++i)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QString e;
        if (!host.renderOneFrame(i * kSimStep, &e))
            qWarning() << "STELA3: 预热帧失败" << i << e;
        mailbox.takeLatestFrame(); // 丢弃
    }
    // 统计快照：判据只看**测量段**的增量（否则 30 个预热帧会把 C04 打成假红）。
    const LegacySkyHost::Stats warmupStats = host.stats();

    // ── 测量：显式驱动 N 帧 ───────────────────────────────────────────────────
    struct ProbeMeta
    {
        FrameProbe probe;
        quint64 frameNumber = 0;
        quint64 stateNumber = 0;
        bool present = false;
    };
    std::vector<ProbeMeta> metas;
    metas.reserve(size_t(frameCount));
    // 最后一帧的像素副本：供 C06 逐像素 diff（邮箱槽位会被复用，必须自己拷）。
    QByteArray lastPixels;
    QSize lastSize;
    quint32 lastStride = 0;
    for (int i = 0; i < frameCount; ++i)
    {
        const double sim = i * kSimStep;
        QString frameError;
        // 双绘法：第一画吸收"适配亮度单帧滞后"（眼适配亮度 = 上一帧天空亮度的
        // 函数，见 StelSkyDrawer::preDraw —— 引擎固有行为，不受任何开关控制），
        // 第二画才是纯 f(JD) 的稳态帧。判据只吃第二画。
        if (!host.renderOneFrame(sim, &frameError))
        {
            frameLines << QStringLiteral("frame=%1 sim=%2 渲染失败：%3")
                              .arg(i + 1).arg(sim, 0, 'f', 1).arg(frameError);
            metas.push_back(ProbeMeta{});
            continue;
        }
        mailbox.takeLatestFrame(); // 丢弃滞后帧
        if (!host.renderOneFrame(sim, &frameError))
        {
            frameLines << QStringLiteral("frame=%1 sim=%2 二次渲染失败：%3")
                              .arg(i + 1).arg(sim, 0, 'f', 1).arg(frameError);
            metas.push_back(ProbeMeta{});
            continue;
        }
        FrameLease lease = mailbox.takeLatestFrame();
        if (!lease.valid())
        {
            frameLines << QStringLiteral("frame=%1 sim=%2 邮箱取帧失败")
                              .arg(i + 1).arg(sim, 0, 'f', 1);
            metas.push_back(ProbeMeta{});
            continue;
        }
        const LegacyFrame &f = lease.frame();
        ProbeMeta m;
        m.probe = probeFrame(f);
        m.frameNumber = f.frameNumber;
        m.stateNumber = f.stateNumber;
        m.present = true;

        if (i == frameCount - 1 && f.pixels)
        {
            lastSize = f.physicalSize;
            lastStride = f.rowStride;
            lastPixels = QByteArray(reinterpret_cast<const char *>(f.pixels),
                                    int(qsizetype(f.rowStride) * f.physicalSize.height()));
        }

        if (i == 0 && !dumpPath.isEmpty() && f.pixels)
        {
            const QImage img(f.pixels, f.physicalSize.width(), f.physicalSize.height(),
                             int(f.rowStride), QImage::Format_RGBA8888);
            const bool saved = img.copy().save(dumpPath);
            frameLines << QStringLiteral("dump=%1 %2（%3x%4）")
                              .arg(dumpPath, saved ? QStringLiteral("已保存") : QStringLiteral("保存失败"))
                              .arg(f.physicalSize.width()).arg(f.physicalSize.height());
        }

        frameLines << QStringLiteral("frame=%1 sim=%2 jd=%3 num=%4 hash=0x%5 mean=%6 colors=%7 "
                                     "render=%8ms readback=%9ms")
                          .arg(i + 1)
                          .arg(sim, 0, 'f', 1)
                          .arg(kJdEpoch + sim, 0, 'f', 5)
                          .arg(f.frameNumber)
                          .arg(m.probe.hash, 16, 16, QLatin1Char('0'))
                          .arg(m.probe.meanLuminance, 0, 'f', 4)
                          .arg(m.probe.distinctColors)
                          .arg(host.stats().lastRenderMs)
                          .arg(host.stats().lastReadbackMs);
        metas.push_back(m);
    }

    // ── A3-C04 请求 N 帧得 N 帧（只统计测量段增量）────────────────────────────
    {
        const LegacySkyHost::Stats hs = host.stats();
        const FrameMailbox::Stats ms = mailbox.stats();
        const quint64 reqDelta = hs.requested - warmupStats.requested;
        const quint64 pubDelta = hs.published - warmupStats.published;
        const quint64 failDelta = hs.failed - warmupStats.failed;
        const quint64 dropDelta = hs.droppedByMailbox - warmupStats.droppedByMailbox;
        // 双绘法：测量段每帧请求/投递 2 次（首画吸收适配滞后），有效帧 = 一半
        addCheck("A3-C04",
                 reqDelta == quint64(2 * frameCount) && pubDelta == quint64(2 * frameCount)
                     && failDelta == 0 && dropDelta == 0,
                 QStringLiteral("测量段请求 %1 帧（双绘）→ 投递 %2，有效帧 %3（丢失 %4，失败 %5）；"
                                "预热 %6 帧不计入。mailbox published=%7 dropped=%8")
                     .arg(reqDelta).arg(pubDelta).arg(frameCount).arg(dropDelta)
                     .arg(failDelta).arg(kWarmupFrames).arg(ms.published).arg(ms.dropped));
    }

    // ── A3-C05 内容随仿真时间变化 ─────────────────────────────────────────────
    {
        QSet<quint64> hashes;
        bool allValid = true;
        for (const ProbeMeta &m : metas)
        {
            if (!m.present || !m.probe.valid) { allValid = false; continue; }
            hashes.insert(m.probe.hash);
        }
        addCheck("A3-C05", allValid && hashes.size() == frameCount,
                 QStringLiteral("%1 帧得到 %2 个互不相同的指纹——真实天空内容随 JD 推进变化，"
                                "不是静态残留（时间流速=0，JD 由本自检注入）")
                     .arg(frameCount).arg(hashes.size()));
    }

    // ── A3-C06 同一状态紧邻重渲染指纹一致（确定性，带诊断）─────────────────────
    // 必须**紧邻**重渲染（同 JD、dt=0、中间无其他帧）：跨多帧"时间倒回"再比，
    // 引擎历史状态会污染比对。本判据回答：同一状态是否出同一像素？
    // 连画两次重渲染并给出逐像素 diff——若 R2==R1≠A，说明 draw 自身带一次性
    // 状态突变（首画后稳定）；若 R1≠R2，说明逐帧随机源仍在（需继续排查）。
    {
        const double lastSim = double(frameCount - 1) * kSimStep;
        QString e;
        bool ok = true;

        auto renderAndCopy = [&](QByteArray &pixels, quint64 &hash) -> bool {
            // 双绘协议与测量段一致：第一画吸收滞后，取第二画
            if (!host.renderOneFrame(lastSim, &e))
                return false;
            mailbox.takeLatestFrame();
            if (!host.renderOneFrame(lastSim, &e))
                return false;
            FrameLease lease = mailbox.takeLatestFrame();
            if (!lease.valid())
                return false;
            const LegacyFrame &f = lease.frame();
            hash = probeFrame(f).hash;
            pixels = QByteArray(reinterpret_cast<const char *>(f.pixels),
                                int(qsizetype(f.rowStride) * f.physicalSize.height()));
            return true;
        };

        quint64 r1Hash = 0, r2Hash = 0;
        QByteArray r1Pixels, r2Pixels;
        ok = renderAndCopy(r1Pixels, r1Hash);
        if (ok)
            ok = renderAndCopy(r2Pixels, r2Hash);

        const quint64 lastHash = metas.empty() ? 0 : metas.back().probe.hash;
        QString diag;
        if (ok)
        {
            const PixelDiff d1 = diffFrames(lastPixels, r1Pixels, lastStride, lastSize.height());
            const PixelDiff d2 = diffFrames(r1Pixels, r2Pixels, lastStride, lastSize.height());
            diag = QStringLiteral("diff(A,R1)=%1px maxΔ=%2 meanΔ=%3；diff(R1,R2)=%4px maxΔ=%5")
                       .arg(d1.differing).arg(d1.maxDelta).arg(d1.meanDelta, 0, 'f', 3)
                       .arg(d2.differing).arg(d2.maxDelta);
            if (!dumpPath.isEmpty() && d1.differing > 0)
            {
                QImage imgA(reinterpret_cast<const quint8 *>(lastPixels.constData()),
                            lastSize.width(), lastSize.height(), int(lastStride),
                            QImage::Format_RGBA8888);
                QImage imgR1(reinterpret_cast<const quint8 *>(r1Pixels.constData()),
                             lastSize.width(), lastSize.height(), int(lastStride),
                             QImage::Format_RGBA8888);
                imgA.copy().save(dumpPath + QStringLiteral(".A.png"));
                imgR1.copy().save(dumpPath + QStringLiteral(".R1.png"));
                diag += QStringLiteral("；对照图已存 %1.A.png / %1.R1.png").arg(dumpPath);
            }
        }
        addCheck("A3-C06", ok && r1Hash != 0 && r1Hash == lastHash && r2Hash == r1Hash,
                 QStringLiteral("同 JD（sim=%1）紧邻重渲染：A=0x%2 R1=0x%3 R2=0x%4——"
                                "%5%6")
                     .arg(lastSim, 0, 'f', 1)
                     .arg(lastHash, 16, 16, QLatin1Char('0'))
                     .arg(r1Hash, 16, 16, QLatin1Char('0'))
                     .arg(r2Hash, 16, 16, QLatin1Char('0'))
                     .arg(ok ? QStringLiteral("引擎对同一状态出同一像素（确定性成立）")
                             : QStringLiteral("渲染失败：") + e)
                     .arg(diag));
    }

    // ── A3-C07 帧内容非平凡 ───────────────────────────────────────────────────
    {
        bool nonTrivial = true;
        double minMean = 1e9;
        int minColors = 1 << 30;
        for (const ProbeMeta &m : metas)
        {
            if (!m.present || !m.probe.valid) { nonTrivial = false; continue; }
            minMean = qMin(minMean, m.probe.meanLuminance);
            minColors = qMin(minColors, m.probe.distinctColors);
        }
        addCheck("A3-C07", nonTrivial && minMean > 0.0 && minColors >= 16,
                 QStringLiteral("帧内容非平凡：最少颜色数=%1（≥16），最小平均亮度=%2（>0）——"
                                "不是黑屏也不是纯色")
                     .arg(minColors).arg(minMean, 0, 'f', 4));
    }

    // ── A3-C08 统计可得 + 无 GL 错误 ──────────────────────────────────────────
    {
        const LegacySkyHost::Stats hs = host.stats();
        mainWin->glContextMakeCurrent();
        const GLenum glErr = QOpenGLContext::currentContext()
                                 ? QOpenGLContext::currentContext()->functions()->glGetError()
                                 : GL_INVALID_OPERATION;
        mainWin->glContextDoneCurrent();
        const bool statsOk = hs.lastRenderMs >= 0 && hs.lastReadbackMs >= 0 && hs.lastTotalMs >= 0;
        addCheck("A3-C08", statsOk && glErr == GL_NO_ERROR,
                 QStringLiteral("耗时统计可用（render=%1ms readback=%2ms，T9 长跑基础量）；"
                                "GL 错误栈=0x%3（NO_ERROR 才合格）")
                     .arg(hs.lastRenderMs).arg(hs.lastReadbackMs).arg(glErr, 0, 16));
    }

    // ── 收尾：顺序敏感——先还帧桥，再引擎 deinit，最后删窗口 ───────────────────
    host.attachMailbox(nullptr);
    host.shutdown();
    mainWin->deinit();
    delete mainWin;

    result.ran = true;
    result.pass = (result.failCount == 0);
    result.summary = QStringLiteral("A3 旧宿主进程内集成自检：%1/%2 项通过")
                         .arg(result.checkCount - result.failCount)
                         .arg(result.checkCount);
    std::sort(details.begin(), details.end());
    return result;
}

} // namespace stelapp
