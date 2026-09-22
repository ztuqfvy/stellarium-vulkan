// LegacyLongRun 实现（T9 长跑）。设计说明见头文件注释。

#include "render/legacy/LegacyLongRun.hpp"

#include "StelMainView.hpp"
#include "core/StelApp.hpp"
#include "core/StelCore.hpp"
#include "core/StelSkyDrawer.hpp"
#include "render/legacy/FrameMailbox.hpp"
#include "render/legacy/LegacySkyHost.hpp"

#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QCoreApplication>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QProcess>
#include <QThread>

#include <algorithm>
#include <cstdio>
#include <vector>

// macOS 常驻内存 / phys_footprint（内存泄漏哨兵的数据源）。
#include <mach/mach.h>

namespace stelapp {

namespace {

constexpr double kJdEpoch = 2451545.0; // J2000，与 A3 相同

qint64 residentBytes()
{
    task_vm_info_data_t info;
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO,
                  reinterpret_cast<task_info_t>(&info), &count) == KERN_SUCCESS)
        return qint64(info.resident_size);
    return -1;
}

qint64 footprintBytes()
{
    task_vm_info_data_t info;
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO,
                  reinterpret_cast<task_info_t>(&info), &count) == KERN_SUCCESS)
        return qint64(info.phys_footprint);
    return -1;
}

// T9-C00 环境前置：门槛冻结协议要求"接通电源 + 关闭低电量模式"。
// 2026-09-22 教训：电池供电 + 低电量模式下，同一二进制的稳态吞吐从 52.66 fps 掉到
// 7.32 fps（探针 3.75 fps），而 T9-C01~C07 全部 PASS（它们不含吞吐门槛判定）——
// 环境污染静默通过。本前置把它变成硬失败，杜绝同类误读。
struct PowerState
{
    bool valid = false;
    bool lowPowerMode = false;
    bool onBattery = false;
    QString source; //!< pmset -g batt 首行（人可读，直接进证据流）
};

PowerState queryPowerState()
{
    PowerState s;
    QProcess pl;
    pl.start(QStringLiteral("/usr/bin/pmset"), {QStringLiteral("-g")});
    if (pl.waitForFinished(5000))
    {
        const QString out = QString::fromLatin1(pl.readAllStandardOutput());
        for (const QString &line : out.split(QLatin1Char('\n')))
        {
            const QString t = line.trimmed();
            if (t.startsWith(QLatin1String("lowpowermode")))
            {
                s.valid = true;
                s.lowPowerMode =
                    (t.section(QLatin1Char(' '), -1).trimmed() == QLatin1String("1"));
            }
        }
    }
    QProcess bt;
    bt.start(QStringLiteral("/usr/bin/pmset"), {QStringLiteral("-g"), QStringLiteral("batt")});
    if (bt.waitForFinished(5000))
    {
        const QString out = QString::fromLatin1(bt.readAllStandardOutput());
        s.source = out.section(QLatin1Char('\n'), 0, 0).trimmed();
        if (s.source.contains(QLatin1String("Battery Power")))
        {
            s.valid = true;
            s.onBattery = true;
        }
        else if (s.source.contains(QLatin1String("AC Power")))
        {
            s.valid = true;
            s.onBattery = false;
        }
    }
    return s;
}

struct Percentiles
{
    double mean = 0.0;
    double p50 = 0.0;
    double p95 = 0.0;
    double max = 0.0;
};

Percentiles summarize(std::vector<double> v)
{
    Percentiles p;
    if (v.empty())
        return p;
    std::sort(v.begin(), v.end());
    const size_t n = v.size();
    double sum = 0.0;
    for (double x : v)
        sum += x;
    p.mean = sum / double(n);
    p.p50 = v[n * 50 / 100];
    p.p95 = v[qMin(n - 1, n * 95 / 100)];
    p.max = v.back();
    return p;
}

QString fmtPercentile(const char *label, const Percentiles &p)
{
    return QStringLiteral("%1 mean=%2 p50=%3 p95=%4 max=%5")
        .arg(QString::fromLatin1(label))
        .arg(p.mean, 0, 'f', 2).arg(p.p50, 0, 'f', 2)
        .arg(p.p95, 0, 'f', 2).arg(p.max, 0, 'f', 2);
}

} // namespace

LegacyAppCheckResult LegacyLongRun::run(QSettings *confSettings)
{
    LegacyAppCheckResult result;
    QStringList &details = result.details;

    const auto addCheck = [&details, &result](const char *id, bool ok, const QString &note) {
        ++result.checkCount;
        if (!ok)
            ++result.failCount;
        details << QStringLiteral("%1 %2 %3")
                       .arg(QString::fromLatin1(id),
                            ok ? QStringLiteral("PASS") : QStringLiteral("FAIL"), note);
    };

    // ── 参数 ─────────────────────────────────────────────────────────────────
    qint64 measureSeconds = qEnvironmentVariableIntValue("STELT9_SECONDS");
    if (measureSeconds <= 0)
        measureSeconds = 1800;
    qint64 warmupSeconds = qEnvironmentVariableIntValue("STELT9_WARMUP_SECONDS");
    if (warmupSeconds <= 0)
        warmupSeconds = 120;
    double jdRate = 1.0 / 86400.0; // JD 天 / 墙钟秒（默认真实时间流速）
    {
        bool ok = false;
        const double v = qEnvironmentVariable("STELT9_JD_RATE").toDouble(&ok);
        if (ok && v > 0.0)
            jdRate = v;
    }
    QString csvPath = qEnvironmentVariable("STELT9_CSV");
    if (csvPath.isEmpty())
        csvPath = QStringLiteral("/tmp/stelt9-frames.csv");

    QSize fboSize(1280, 720);
    const QByteArray forcedSize = qgetenv("STELT9_SIZE");
    if (!forcedSize.isEmpty())
    {
        const QList<QByteArray> parts = forcedSize.split('x');
        if (parts.size() == 2)
            fboSize = QSize(parts.value(0).toInt(), parts.value(1).toInt());
        // 0x0 负控：装配必败 → VERDICT=UNAVAILABLE、退出码 8。
    }

    // ── T9-C00 环境前置：被测环境必须与门槛冻结协议一致 ──────────────────────
    // 不合规 → 直接拒绝测量并退出码 9（不创建窗口、不初始化引擎）。
    // 理由见头文件"环境前置"段与 2026-09-22 教训。
    const bool allowThrottled = qEnvironmentVariableIsSet("STELT9_ALLOW_THROTTLED");
    {
        const PowerState ps = queryPowerState();
        const bool envOk = ps.valid && !ps.lowPowerMode && !ps.onBattery;
        addCheck("T9-C00", envOk || allowThrottled,
                 QStringLiteral("运行环境：电源=%1；低电量模式=%2；判定=%3。"
                                "冻结协议要求接通电源且关闭低电量模式"
                                "（被系统降频时的吞吐/延迟数据无意义）%4")
                     .arg(ps.source.isEmpty() ? QStringLiteral("未知") : ps.source,
                          ps.lowPowerMode ? QStringLiteral("开") : QStringLiteral("关"),
                          envOk ? QStringLiteral("合规") : QStringLiteral("不合规"),
                          allowThrottled ? QStringLiteral("；STELT9_ALLOW_THROTTLED 已设，降级为警告")
                                         : QString()));
        if (!envOk && !allowThrottled)
        {
            result.envBlocked = true;
            result.summary =
                QStringLiteral("环境不合规：%1；低电量模式=%2——按冻结协议拒绝测量"
                               "（先接入电源并关闭低电量模式；仅调试可设 STELT9_ALLOW_THROTTLED=1）")
                    .arg(ps.source.isEmpty() ? QStringLiteral("电源状态未知") : ps.source,
                         ps.lowPowerMode ? QStringLiteral("开") : QStringLiteral("关"));
            result.setupError = result.summary;
            result.pass = false;
            std::fprintf(stderr, "STELT9: %s\n", qPrintable(result.summary));
            std::fflush(stderr);
            return result; // ran=false + envBlocked=true → 退出码 9
        }
    }

    // ── 引导（与 A3 相同的 WA_DontShowOnScreen 方式）──────────────────────────
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
        result.setupError = QStringLiteral("WA_DontShowOnScreen 引导失败（%1ms）").arg(bootClock.elapsed());
        result.summary = result.setupError;
        addCheck("T9-C01", false, result.setupError);
        delete mainWin;
        return result;
    }
    addCheck("T9-C01", true,
             QStringLiteral("引擎无头初始化成功（%1ms）").arg(bootClock.elapsed()));

    // 生产契约：JD 由外部注入，引擎自走时钟停掉。星闪/人眼自适应保持默认开
    // ——T9 量的是真实成本，不做确定性（那是 A3 自检的职责）。
    StelApp &app = StelApp::getInstance();
    app.getCore()->setTimeRate(0.0);
    app.setDevicePixelsPerPixel(1.0);

    // ── 帧桥装配 ─────────────────────────────────────────────────────────────
    mainWin->glContextMakeCurrent();
    FrameMailbox mailbox;
    LegacySkyHost host;
    LegacySkyHostConfig cfg;
    cfg.contextMode = GlContextMode::kBorrowed;
    cfg.physicalSize = fboSize;
    cfg.maxDimension = 4096;
    cfg.devicePixelRatio = 1.0;

    QString error;
    if (!host.initialize(cfg, &error))
    {
        result.setupError = QStringLiteral("LegacySkyHost::initialize 失败：%1").arg(error);
        result.summary = result.setupError;
        addCheck("T9-C02", false, result.setupError);
        mainWin->deinit();
        delete mainWin;
        return result;
    }
    addCheck("T9-C02", true,
             QStringLiteral("帧桥装配成功（借用上下文，离屏 FBO %1x%2，行步长 %3，每帧 %4 KB）")
                 .arg(fboSize.width()).arg(fboSize.height())
                 .arg(host.readbackInfo().rowStride)
                 .arg(qint64(host.readbackInfo().rowStride) * fboSize.height() / 1024));

    // 回调：JD 随已流逝测量时间推进（回调拿到的 simSeconds 就是测量段秒数 × 速率）
    host.setRenderCallback([jdRate](double dt, double simSeconds, const QSize &) {
        Q_UNUSED(dt) // dt 由 renderOneFrame 的第一参传入引擎 update，见下
        StelApp &a = StelApp::getInstance();
        a.getCore()->setJD(kJdEpoch + simSeconds * jdRate);
        a.update(dt > 0.0 ? qMin(dt, 1.0) : 0.0);
        a.draw();
    });
    host.attachMailbox(&mailbox);

    QFile csvFile(csvPath);
    const bool csvOpen = csvFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text);
    if (csvOpen)
    {
        csvFile.write("t_s,phase,frame_number,render_ms,readback_ms,total_ms,"
                      "mailbox_complete,mailbox_age_ms,rss_kb,footprint_kb\n");
    }

    // ── 帧循环（预热段与测量段共用；phase 决定是否计入统计）────────────────────
    std::vector<double> renderMs, readbackMs, totalMs, ageMs;
    std::vector<double> rssSeries, footprintSeries; // 每 10s 采样点
    quint64 failedFrames = 0;
    const qint64 probeEvery = 500;       // 内容哨兵抽查间隔（帧）
    qint64 probeFailures = 0;

    QElapsedTimer wallClock;
    wallClock.start();
    double lastFrameWall = 0.0;
    qint64 nextMemSampleAt = 10;  // 秒
    qint64 nextProgressAt = 60;   // 秒
    qint64 nextEventPumpAt = 0;   // 秒（每秒泵一次事件）
    qint64 nextPowerSampleAt = 60; // 秒（测量期间每 60 秒复核电源状态）
    qint64 envViolations = 0;      // 测量期间环境违规次数（T9-C08）

    auto runPhase = [&](const char *phase, qint64 durationSeconds) {
        QElapsedTimer phaseClock;
        phaseClock.start();
        while (phaseClock.elapsed() < durationSeconds * 1000)
        {
            const double now = double(wallClock.elapsed()) / 1000.0;
            const double dt = qMax(0.0, now - lastFrameWall);
            lastFrameWall = now;

            if (wallClock.elapsed() >= nextEventPumpAt * 1000)
            {
                // 泵事件：保持异步加载/定时器活着（生产路径由主循环做这件事）
                QCoreApplication::processEvents(QEventLoop::AllEvents, 0);
                ++nextEventPumpAt;
            }

            if (wallClock.elapsed() >= nextPowerSampleAt * 1000)
            {
                // 环境漂移监测：中途拔电 / 系统自动切低电量模式会让本次数据失效
                const PowerState psNow = queryPowerState();
                if (psNow.valid && (psNow.lowPowerMode || psNow.onBattery))
                {
                    ++envViolations;
                    if (envViolations <= 5)
                        qWarning() << "STELT9: 测量期间环境违规" << psNow.source
                                   << "lowPowerMode=" << psNow.lowPowerMode;
                }
                nextPowerSampleAt += 60;
            }

            QString frameError;
            const double simSeconds = double(phaseClock.elapsed()) / 1000.0;
            if (!host.renderOneFrame(simSeconds, &frameError))
            {
                ++failedFrames;
                if (failedFrames <= 10 || failedFrames % 500 == 0)
                    qWarning() << "STELT9: 渲染失败" << failedFrames << frameError;
                continue;
            }
            const FrameMailbox::Stats before = mailbox.stats();
            FrameLease lease = mailbox.takeLatestFrame();
            const LegacySkyHost::Stats hs = host.stats();

            if (qstrcmp(phase, "measure") == 0)
            {
                renderMs.push_back(hs.lastRenderMs);
                readbackMs.push_back(hs.lastReadbackMs);
                totalMs.push_back(hs.lastTotalMs);
                ageMs.push_back(before.latestFrameAgeMs >= 0 ? double(before.latestFrameAgeMs) : 0.0);
                if (csvOpen)
                {
                    csvFile.write(qPrintable(QStringLiteral(
                        "%1,%2,%3,%4,%5,%6,%7,%8,%9,%10\n")
                        .arg(now, 0, 'f', 3).arg(QString::fromLatin1(phase))
                        .arg(lease.valid() ? quint64(lease.frame().frameNumber) : quint64(0))
                        .arg(double(hs.lastRenderMs), 0, 'f', 2)
                        .arg(double(hs.lastReadbackMs), 0, 'f', 2)
                        .arg(double(hs.lastTotalMs), 0, 'f', 2)
                        .arg(before.completeSlots)
                        .arg(before.latestFrameAgeMs)
                        .arg(residentBytes() / 1024).arg(footprintBytes() / 1024)));
                }
                // 内容哨兵：每 probeEvery 帧抽查一帧内容非平凡（防静默黑屏）
                if (lease.valid() && (renderMs.size() % probeEvery) == 0)
                {
                    const LegacyFrame &f = lease.frame();
                    double lumSum = 0.0;
                    const int w = f.physicalSize.width();
                    const int h = f.physicalSize.height();
                    for (int row = 0; row < h; row += 7)
                    {
                        const quint8 *line = f.pixels + qsizetype(row) * f.rowStride;
                        for (int col = 0; col < w; col += 7)
                            lumSum += line[col * 4 + 1]; // G 通道
                    }
                    if (lumSum <= 0.0)
                        ++probeFailures;
                }
            }
        }
    };

    // ── 预热段 ───────────────────────────────────────────────────────────────
    std::fprintf(stderr, "STELT9: 预热 %lld 秒开始\n", warmupSeconds);
    std::fflush(stderr);
    runPhase("warmup", warmupSeconds);
    const LegacySkyHost::Stats warmupStats = host.stats();
    const FrameMailbox::Stats warmupMailbox = mailbox.stats();
    const qint64 rssWarmup = residentBytes();
    const qint64 footprintWarmup = footprintBytes();

    // ── 测量段 ───────────────────────────────────────────────────────────────
    std::fprintf(stderr, "STELT9: 测量 %lld 秒开始\n", measureSeconds);
    std::fflush(stderr);
    runPhase("measure", measureSeconds);
    if (csvOpen)
        csvFile.flush();

    const LegacySkyHost::Stats hs = host.stats();
    const FrameMailbox::Stats ms = mailbox.stats();
    const double measuredSeconds = double(measureSeconds);
    const double fps = double(renderMs.size()) / measuredSeconds;

    // ── T9-C03 管道完整性：请求=投递、零失败、零丢弃 ───────────────────────────
    {
        const quint64 reqDelta = hs.requested - warmupStats.requested;
        const quint64 pubDelta = hs.published - warmupStats.published;
        const quint64 failDelta = hs.failed - warmupStats.failed;
        const quint64 dropDelta = ms.dropped - warmupMailbox.dropped;
        addCheck("T9-C03",
                 reqDelta == pubDelta && failDelta == 0 && dropDelta == 0
                     && failedFrames == 0,
                 QStringLiteral("测量段请求 %1 = 投递 %2，渲染失败 %3（进程内计数 %4），"
                                "邮箱丢弃 %5 —— 管道完整（帧 %6，%7 fps）")
                     .arg(reqDelta).arg(pubDelta).arg(failedFrames).arg(failDelta)
                     .arg(dropDelta).arg(renderMs.size()).arg(fps, 0, 'f', 2));
    }

    // ── T9-C04 延迟分布（P-BRG-02/03 的基线数据）───────────────────────────────
    {
        const Percentiles pr = summarize(renderMs);
        const Percentiles pb = summarize(readbackMs);
        const Percentiles pt = summarize(totalMs);
        const Percentiles pa = summarize(ageMs);
        addCheck("T9-C04", !totalMs.empty(),
                 QStringLiteral("%1；%2；%3；%4。"
                                "帧龄说明：生产者即取即用模式下帧龄 ≡ totalMs，"
                                "ageMs 单列仅作邮箱簿记核对（恒≈0 属预期）")
                     .arg(fmtPercentile("renderMs:", pr),
                          fmtPercentile("readbackMs:", pb),
                          fmtPercentile("totalMs:", pt),
                          fmtPercentile("ageMs:", pa)));
    }

    // ── T9-C05 内存不随时间长增（基线记录；门槛在数据冻结后另定）────────────────
    {
        const qint64 rssEnd = residentBytes();
        const qint64 footprintEnd = footprintBytes();
        const double rssGrowthMiB = double(rssEnd - rssWarmup) / (1024.0 * 1024.0);
        const double fpGrowthMiB = double(footprintEnd - footprintWarmup) / (1024.0 * 1024.0);
        addCheck("T9-C05", rssEnd > 0 && footprintEnd > 0,
                 QStringLiteral("内存基线：预热后 RSS=%1 MiB → 结束 %2 MiB（增 %3 MiB）；"
                                "phys_footprint %4 → %5 MiB（增 %6 MiB）。"
                                "%7 字节/帧 × 3 槽位为常驻帧内存下限，增长超过数倍即泄漏")
                     .arg(rssWarmup / (1024 * 1024)).arg(rssEnd / (1024 * 1024))
                     .arg(rssGrowthMiB, 0, 'f', 1)
                     .arg(double(footprintWarmup) / (1024.0 * 1024.0), 0, 'f', 1)
                     .arg(double(footprintEnd) / (1024.0 * 1024.0), 0, 'f', 1)
                     .arg(fpGrowthMiB, 0, 'f', 1)
                     .arg(host.readbackInfo().rowStride * quint64(fboSize.height())));
    }

    // ── T9-C06 内容哨兵 ──────────────────────────────────────────────────────
    addCheck("T9-C06", probeFailures == 0,
             QStringLiteral("长跑内容抽查：%1 次抽查（每 %2 帧 1 次）全通过，无静默黑屏")
                 .arg(renderMs.size() / probeEvery).arg(probeEvery));

    // ── GL 错误栈 ────────────────────────────────────────────────────────────
    {
        mainWin->glContextMakeCurrent();
        const GLenum glErr = QOpenGLContext::currentContext()
                                 ? QOpenGLContext::currentContext()->functions()->glGetError()
                                 : GL_INVALID_OPERATION;
        mainWin->glContextDoneCurrent();
        addCheck("T9-C07", glErr == GL_NO_ERROR,
                 QStringLiteral("30 分钟后 GL 错误栈=0x%1（NO_ERROR 才合格）").arg(glErr, 0, 16));
    }

    // ── T9-C08 环境未漂移（测量期间始终接电且低电量模式关闭）──────────────────
    addCheck("T9-C08", envViolations == 0 || allowThrottled,
             QStringLiteral("测量期间环境漂移检查：%1 次违规（每 60 秒复核一次；"
                            "0 次才说明数据在合规环境下取得）%2")
                 .arg(envViolations)
                 .arg(allowThrottled ? QStringLiteral("；已设 STELT9_ALLOW_THROTTLED，降级为警告")
                                     : QString()));

    if (csvOpen)
    {
        csvFile.close();
        details << QStringLiteral("T9-INFO 逐帧 CSV：%1（%2 行）")
                       .arg(csvPath).arg(renderMs.size());
    }

    // ── 收尾：顺序与 A3 相同 ──────────────────────────────────────────────────
    host.attachMailbox(nullptr);
    host.shutdown();
    mainWin->deinit();
    delete mainWin;

    result.ran = true;
    result.pass = (result.failCount == 0);
    result.summary = QStringLiteral("T9 长跑基线：%1 秒测量 / %2 帧 / %3 fps / 失败 %4 / 丢弃 %5")
                         .arg(measuredSeconds, 0, 'f', 0)
                         .arg(renderMs.size()).arg(fps, 0, 'f', 2)
                         .arg(failedFrames).arg(ms.dropped - warmupMailbox.dropped);
    return result;
}

} // namespace stelapp
