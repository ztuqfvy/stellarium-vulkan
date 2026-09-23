// SkyLongRun 实现（P-BRG-01 消费侧全量计量长跑）。设计与判据见头文件。
#include "ui/SkyLongRun.hpp"

#include "render/legacy/FrameMailbox.hpp"
#include "ui/LiveFrameSource.hpp"
#include "ui/quick/SkyViewport.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QProcess>
#include <QQuickWindow>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

#ifdef Q_OS_MACOS
#include <mach/mach.h>
#endif

namespace stelapp {

namespace {

// ── 内存读数（与 T9 同口径：用 phys_footprint，RSS 会被页面回收掩盖增长）───
qint64 residentBytes()
{
#ifdef Q_OS_MACOS
    task_vm_info_data_t info;
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO,
                  reinterpret_cast<task_info_t>(&info), &count) == KERN_SUCCESS)
        return qint64(info.resident_size);
#endif
    return -1;
}

qint64 footprintBytes()
{
#ifdef Q_OS_MACOS
    task_vm_info_data_t info;
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO,
                  reinterpret_cast<task_info_t>(&info), &count) == KERN_SUCCESS)
        return qint64(info.phys_footprint);
#endif
    return -1;
}

// ── 环境前置（与 T9-C00 同判据、同实现口径）──────────────────────────────
struct PowerState
{
    bool valid = false;
    bool lowPowerMode = false;
    bool onBattery = false;
    QString source;   //!< 「Now drawing from 'AC Power'」原文，直接进证据流
};

PowerState queryPowerState()
{
    PowerState s;
    s.lowPowerMode = false;

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
    bt.start(QStringLiteral("/usr/bin/pmset"),
             {QStringLiteral("-g"), QStringLiteral("batt")});
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
        else
        {
            s.valid = false;   // 来源不可辨 → 不冒进判定
        }
    }
    else
    {
        s.valid = false;
    }
    return s;
}

inline bool envCompliant(const PowerState &s)
{
    return s.valid && !s.lowPowerMode && !s.onBattery;
}

struct Percentiles
{
    double mean = 0.0;
    double p50 = 0.0;
    double p95 = 0.0;
    double p99 = 0.0;
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
    auto at = [&v, n](double q) { return v[std::min<size_t>(size_t(double(n) * q), n - 1)]; };
    p.p50 = at(0.50);
    p.p95 = at(0.95);
    p.p99 = at(0.99);
    p.max = v.back();
    return p;
}

//! 线性回归斜率（y 单位 MiB，t 单位秒）→ 换算成 MiB/min。
double slopePerMinute(const std::vector<double> &tSec, const std::vector<double> &y)
{
    const size_t n = std::min(tSec.size(), y.size());
    if (n < 3)
        return 0.0;
    double mt = 0.0, my = 0.0;
    for (size_t i = 0; i < n; ++i)
    {
        mt += tSec[i];
        my += y[i];
    }
    mt /= double(n);
    my /= double(n);
    double num = 0.0, den = 0.0;
    for (size_t i = 0; i < n; ++i)
    {
        num += (tSec[i] - mt) * (y[i] - my);
        den += (tSec[i] - mt) * (tSec[i] - mt);
    }
    if (den <= 0.0)
        return 0.0;
    return (num / den) * 60.0;
}

//! 逐秒采样行（CSV 一行 = 一秒；全量保留，结束时统一过滤统计）。
struct SecondsRow
{
    double tSec = 0.0;
    bool measure = false;
    quint64 displayed = 0;
    quint64 published = 0;
    quint64 dropped = 0;
    qint64 mailboxAgeMs = -1;
    quint64 uploadCount = 0;
    quint64 uploadSumUs = 0;
    qreal uploadMaxMs = 0.0;
    qint64 rssKb = -1;
    qint64 footprintKb = -1;
    bool exposed = false;
    bool degraded = false;
    double producerFps = 0.0;
    quint64 producerRendered = 0;
    quint64 producerFailed = 0;
};

//! 逐帧上屏记录（frameSwapped）。
struct SwapRecord
{
    double tSec = 0.0;
    bool measure = false;
    quint64 frameNumber = 0;
    double intervalMs = 0.0;
};

constexpr double kTargetFpsFloor = 40.0;   //!< 稳态吞吐下限（与 §6.1.1 同量级）
// 上屏间隔门槛（消费侧口径）：vsync 60Hz 下间隔自然量化在 16.7 / 33.4ms 两档，
// 生产者不足 60fps 时混排是正常的"漏一帧"，不等于延迟超标。故只管异常长停顿。
constexpr double kIntervalP99Ms = 50.0;    //!< 上屏间隔 p99 上限
constexpr double kIntervalMaxMs = 100.0;   //!< 上屏间隔单次最坏值上限
constexpr double kUploadMeanMs = 5.0;      //!< 单次上传均值上限
constexpr double kUploadMaxMs = 100.0;     //!< 单次上传最坏值上限
constexpr double kFrameAgeMs = 500.0;      //!< 邮箱帧龄上限
constexpr double kMemSlopeMiBPerMin = 1.0; //!< footprint 斜率上限
constexpr double kDegradedShareMax = 0.01; //!< 降级误报占比上限

} // namespace

SkyLongRunOptions SkyLongRun::optionsFromEnv()
{
    SkyLongRunOptions o;
    const int warm = qEnvironmentVariableIntValue("STELQUICK_LONGRUN_WARMUP_SECONDS");
    if (warm > 0)
        o.warmupSeconds = warm;
    const int secs = qEnvironmentVariableIntValue("STELQUICK_LONGRUN_SECONDS");
    if (secs > 0)
        o.measureSeconds = secs;
    const QByteArray fpsEnv = qgetenv("STELQUICK_LONGRUN_FPS");
    if (!fpsEnv.isEmpty())
        o.producerFps = fpsEnv.toDouble();
    const QByteArray sizeEnv = qgetenv("STELQUICK_LONGRUN_SIZE");
    if (!sizeEnv.isEmpty())
    {
        const QList<QByteArray> wh = sizeEnv.split('x');
        if (wh.size() == 2)
            o.physicalSize = QSize(wh.at(0).toInt(), wh.at(1).toInt());
    }
    o.csvPath = qEnvironmentVariable("STELQUICK_LONGRUN_CSV",
                                     QStringLiteral("/tmp/stelsky-longrun.csv"));
    o.framesCsvPath = qEnvironmentVariable("STELQUICK_LONGRUN_FRAMES_CSV",
                                           o.csvPath + QStringLiteral(".frames.csv"));
    o.allowThrottled = qEnvironmentVariableIsSet("STELQUICK_LONGRUN_ALLOW_THROTTLED")
                       && qEnvironmentVariableIntValue("STELQUICK_LONGRUN_ALLOW_THROTTLED") != 0;
    return o;
}

void SkyLongRun::runStartupSequence(
    QGuiApplication *app, QQuickWindow *window, SkyViewport *viewport, FrameMailbox *mailbox,
    const SkyLongRunOptions &options, const std::function<void(const SkyLongRunResult &)> &done)
{
    auto result = std::make_shared<SkyLongRunResult>();

    // ── SL-C00 环境前置（硬门；不合规直接拒绝，不产生测量数据）────────────────
    const PowerState psStart = queryPowerState();
    if (!options.allowThrottled && !envCompliant(psStart))
    {
        result->ran = false;
        result->envBlocked = true;
        result->pass = false;
        result->summary = QStringLiteral(
                              "环境不合规：%1；低电量模式=%2——按冻结协议拒绝测量"
                              "（先接入电源并关闭低电量模式；仅调试可设 "
                              "STELQUICK_LONGRUN_ALLOW_THROTTLED=1）")
                              .arg(psStart.source.isEmpty() ? QStringLiteral("电源状态未知")
                                                            : psStart.source,
                                   psStart.lowPowerMode ? QStringLiteral("开")
                                                        : QStringLiteral("关"));
        std::fprintf(stderr, "STELLRUN: %s\n", qPrintable(result->summary));
        std::fflush(stderr);
        done(*result);
        return;
    }
    std::fprintf(stderr, "STELLRUN: 环境前置通过（%s，低电量模式关）\n",
                 qPrintable(psStart.source));
    std::fflush(stderr);

    // 降级阈值（P-BRG-04）：正常 60fps 下不应触发，SL-C10 检查 30 分钟内是否误报。
    viewport->setDegradeThreshold(15.0);

    // ── CSV 准备 ─────────────────────────────────────────────────────────────
    // 注意：QFile 必须堆分配并由 lambda 共享持有。本函数设置完定时器就返回，
    // 若用栈上对象，定时器回调拿到的就是悬垂指针（2026-09-23 首跑即 SIGSEGV）。
    QDir().mkpath(QFileInfo(options.csvPath).absolutePath());
    auto csv = std::make_shared<QFile>(options.csvPath);
    const bool csvOpen =
        csv->open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text);
    if (csvOpen)
        csv->write("t_s,phase,displayed_frames,published,dropped,mailbox_age_ms,"
                   "upload_count,upload_sum_us,upload_max_ms,rss_kb,footprint_kb,"
                   "exposed,degraded,producer_fps,producer_rendered,producer_failed\n");

    auto framesCsv = std::make_shared<QFile>(options.framesCsvPath);
    const bool framesOpen =
        framesCsv->open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text);
    if (framesOpen)
        framesCsv->write("t_s,phase,frame_number,interval_ms\n");

    // ── 生产者 ───────────────────────────────────────────────────────────────
    LiveFrameSourceConfig cfg;
    cfg.physicalSize = options.physicalSize;
    cfg.devicePixelRatio = options.devicePixelRatio;
    cfg.fps = options.producerFps;
    cfg.simRate = 1.0;

    auto liveSource = std::make_shared<LiveFrameSource>();
    QString error;
    if (!liveSource->start(mailbox, cfg, &error))
    {
        result->pass = false;
        result->summary = QStringLiteral("生产者装配失败：%1").arg(error);
        std::fprintf(stderr, "STELLRUN: %s\n", qPrintable(result->summary));
        std::fflush(stderr);
        done(*result);
        return;
    }

    auto rows = std::make_shared<std::vector<SecondsRow>>();
    auto swaps = std::make_shared<std::vector<SwapRecord>>();
    auto swapsWritten = std::make_shared<size_t>(0);
    auto clock = std::make_shared<QElapsedTimer>();
    auto lastSwapNs = std::make_shared<qint64>(-1);
    auto envViolations = std::make_shared<int>(0);

    clock->start();

    // ── 逐帧上屏记录（frameSwapped 在 GUI 线程送达）──────────────────────────
    // 只做内存累积：落盘在每秒采样里批量做（避免每次 swap 都碰 IO，且崩溃时
    // 已落盘部分足以定位）。上屏间隔分布是 SL-C03 的原始证据。
    QObject::connect(window, &QQuickWindow::frameSwapped, window,
                     [swaps, clock, lastSwapNs, viewport, warmup = options.warmupSeconds]() {
                         const qint64 nowNs = clock->nsecsElapsed();
                         double intervalMs = 0.0;
                         if (*lastSwapNs >= 0)
                             intervalMs = double(nowNs - *lastSwapNs) / 1e6;
                         *lastSwapNs = nowNs;

                         SwapRecord r;
                         r.tSec = double(nowNs) / 1e9;
                         r.measure = r.tSec >= double(warmup);
                         r.frameNumber = viewport->displayedFrameNumber();
                         r.intervalMs = intervalMs;
                         swaps->push_back(r);
                     });

    // ── 每秒采样（写 CSV + 电源漂移复核）────────────────────────────────────
    auto nextPowerCheckAt = std::make_shared<int>(0);
    auto sampler = new QTimer(app);
    sampler->setInterval(1000);
    QObject::connect(
        sampler, &QTimer::timeout, app,
        [rows, swaps, swapsWritten, mailbox, viewport, liveSource, clock, window,
         warmup = options.warmupSeconds, nextPowerCheckAt, envViolations, csv, csvOpen,
         framesCsv, framesOpen]() {
            const double t = double(clock->elapsed()) / 1000.0;
            const bool isMeasure = t >= double(warmup);

            // 帧流 CSV 增量落盘（在采样时刻统一写，避免每次 swap 都碰 IO）
            if (framesOpen)
            {
                for (size_t i = *swapsWritten; i < swaps->size(); ++i)
                {
                    const SwapRecord &r = (*swaps)[i];
                    framesCsv->write(qPrintable(QStringLiteral("%1,%2,%3,%4\n")
                                                       .arg(r.tSec, 0, 'f', 3)
                                                       .arg(r.measure ? "measure" : "warmup")
                                                       .arg(r.frameNumber)
                                                       .arg(r.intervalMs, 0, 'f', 3)));
                }
                *swapsWritten = swaps->size();
                if (int(t) % 60 == 0)
                    framesCsv->flush();
            }

            // 环境漂移监测（测量期间每 60s 一次）
            if (int(t) >= *nextPowerCheckAt)
            {
                const PowerState psNow = queryPowerState();
                if (psNow.valid && (psNow.lowPowerMode || psNow.onBattery))
                {
                    ++(*envViolations);
                    if (*envViolations <= 5)
                        std::fprintf(stderr, "STELLRUN: 测量期间环境违规：%s 低电量模式=%d\n",
                                     qPrintable(psNow.source), psNow.lowPowerMode ? 1 : 0);
                    else if (*envViolations == 6)
                        std::fprintf(stderr, "STELLRUN: （后续同类告警已省略）\n");
                    std::fflush(stderr);
                }
                *nextPowerCheckAt = int(t) + 60;
            }

            const FrameMailbox::Stats ms = mailbox->stats();
            const LiveFrameSource::RuntimeStats ps = liveSource->runtimeStats();

            SecondsRow row;
            row.tSec = t;
            row.measure = isMeasure;
            row.displayed = viewport->displayedFrameNumber();
            row.published = ms.published;
            row.dropped = ms.dropped;
            row.mailboxAgeMs = ms.latestFrameAgeMs;
            row.uploadCount = viewport->uploadCount();
            row.uploadSumUs = viewport->uploadSumUs();
            row.uploadMaxMs = viewport->uploadMaxMs();
            row.rssKb = residentBytes() / 1024;
            row.footprintKb = footprintBytes() / 1024;
            row.exposed = window->isExposed();
            row.degraded = viewport->degraded();
            row.producerFps = ps.producerFps;
            row.producerRendered = ps.rendered;
            row.producerFailed = ps.failed;
            rows->push_back(row);

            if (csvOpen)
            {
                csv->write(qPrintable(
                    QStringLiteral("%1,%2,%3,%4,%5,%6,%7,%8,%9,%10,%11,%12,%13,%14,%15,%16\n")
                        .arg(t, 0, 'f', 3)
                        .arg(isMeasure ? "measure" : "warmup")
                        .arg(row.displayed)
                        .arg(row.published)
                        .arg(row.dropped)
                        .arg(row.mailboxAgeMs)
                        .arg(row.uploadCount)
                        .arg(row.uploadSumUs)
                        .arg(double(row.uploadMaxMs), 0, 'f', 3)
                        .arg(row.rssKb)
                        .arg(row.footprintKb)
                        .arg(row.exposed ? 1 : 0)
                        .arg(row.degraded ? 1 : 0)
                        .arg(row.producerFps, 0, 'f', 2)
                        .arg(row.producerRendered)
                        .arg(row.producerFailed)));
                if (int(t) % 60 == 0)
                    csv->flush();
            }
        });
    sampler->start();

    std::fprintf(stderr, "STELLRUN: 预热 %d 秒开始（生产者名义 %.1f fps，%dx%d）\n",
                 options.warmupSeconds, options.producerFps, options.physicalSize.width(),
                 options.physicalSize.height());
    std::fflush(stderr);

    QTimer::singleShot(options.warmupSeconds * 1000, app,
                       [measure = options.measureSeconds]() {
        std::fprintf(stderr, "STELLRUN: 测量 %d 秒开始（预热段数据保留在 CSV，统计不含）\n",
                     measure);
        std::fflush(stderr);
    });

    // ── 收尾：汇总判据 ───────────────────────────────────────────────────────
    QTimer::singleShot((options.warmupSeconds + options.measureSeconds) * 1000, app,
                       [app, sampler, window, viewport, mailbox, liveSource, rows, swaps,
                        swapsWritten, clock, envViolations, options,
                        allowThrottled = options.allowThrottled, result, done, csv, csvOpen,
                        framesCsv, framesOpen]() {
                           sampler->stop();

                           // 帧流 CSV 尾部剩余 + 关盘
                           if (framesOpen)
                           {
                               for (size_t i = *swapsWritten; i < swaps->size(); ++i)
                               {
                                   const SwapRecord &r = (*swaps)[i];
                                   framesCsv->write(
                                       qPrintable(QStringLiteral("%1,%2,%3,%4\n")
                                                      .arg(r.tSec, 0, 'f', 3)
                                                      .arg(r.measure ? "measure" : "warmup")
                                                      .arg(r.frameNumber)
                                                      .arg(r.intervalMs, 0, 'f', 3)));
                               }
                               *swapsWritten = swaps->size();
                               framesCsv->close();
                           }
                           if (csvOpen)
                           {
                               csv->flush();
                               csv->close();
                           }

                           const FrameMailbox::Stats ms = mailbox->stats();
                           const LiveFrameSource::RuntimeStats ps = liveSource->runtimeStats();

                           // 生产者停机（先于判据打印；停机耗时不影响已采集数据）
                           liveSource->stop();

                           auto add = [result](const QString &id, bool pass,
                                               const QString &detail) {
                               result->details << QStringLiteral("%1 %2 %3")
                                                      .arg(id, pass ? "PASS" : "FAIL", detail);
                               ++result->checkCount;
                               if (!pass)
                                   ++result->failCount;
                           };

                           // 测量段基线（取测量段第一行；若无则取最后一行）
                           SecondsRow measFirst, measLast;
                           bool haveMeas = false;
                           for (const SecondsRow &r : *rows)
                           {
                               if (!r.measure)
                                   continue;
                               if (!haveMeas)
                               {
                                   measFirst = r;
                                   haveMeas = true;
                               }
                               measLast = r;
                           }
                           // 稳态窗口：测量段的后 2/3（对齐 T9「最后 1200s」口径）
                           const double steadyStart =
                               double(options.warmupSeconds) + double(options.measureSeconds) / 3.0;

                           std::vector<double> steadyT, steadyFp, steadyPropFps;
                           quint64 windowDisplayedFirst = 0, windowDisplayedLast = 0;
                           bool windowFirst = false;
                           double windowTFirst = 0.0, windowTLast = 0.0;
                           qint64 measureMaxAge = 0;
                           double measureMaxUploadMs = 0.0;
                           int measureRows = 0, measureExposedRows = 0, measureDegradedRows = 0;
                           for (const SecondsRow &r : *rows)
                           {
                               if (!r.measure)
                                   continue;
                               ++measureRows;
                               if (r.exposed)
                                   ++measureExposedRows;
                               if (r.degraded)
                                   ++measureDegradedRows;
                               measureMaxAge = std::max(measureMaxAge, r.mailboxAgeMs);
                               measureMaxUploadMs =
                                   std::max(measureMaxUploadMs, double(r.uploadMaxMs));
                               if (r.tSec >= steadyStart)
                               {
                                   steadyT.push_back(r.tSec);
                                   steadyFp.push_back(double(r.footprintKb) / 1024.0);
                                   steadyPropFps.push_back(r.producerFps);
                                   if (!windowFirst)
                                   {
                                       windowFirst = true;
                                       windowDisplayedFirst = r.displayed;
                                       windowTFirst = r.tSec;
                                   }
                                   windowDisplayedLast = r.displayed;
                                   windowTLast = r.tSec;
                               }
                           }

                           // 稳态显示吞吐
                           double steadyDisplayFps = 0.0;
                           if (windowFirst && windowTLast > windowTFirst
                               && windowDisplayedLast >= windowDisplayedFirst)
                               steadyDisplayFps = double(windowDisplayedLast
                                                         - windowDisplayedFirst)
                                                  / (windowTLast - windowTFirst);

                           // 稳态上屏间隔分布
                           std::vector<double> steadyIntervals;
                           for (const SwapRecord &r : *swaps)
                               if (r.measure && r.tSec >= steadyStart && r.intervalMs > 0.0)
                                   steadyIntervals.push_back(r.intervalMs);
                           const Percentiles si = summarize(steadyIntervals);

                           // 稳态生产者速率
                           double steadyProducerFps = 0.0;
                           if (!steadyPropFps.empty())
                           {
                               double s = 0.0;
                               for (double x : steadyPropFps)
                                   s += x;
                               steadyProducerFps = s / double(steadyPropFps.size());
                           }

                           // 测量段增量（生产者/邮箱/上传）
                           const quint64 prodFailDelta =
                               haveMeas ? (ps.failed - measFirst.producerFailed) : 0;
                           const quint64 renderedDelta =
                               haveMeas ? (ps.rendered - measFirst.producerRendered) : 0;
                           const quint64 dropDelta =
                               haveMeas ? (ms.dropped - measFirst.dropped) : 0;
                           const quint64 pubDelta =
                               haveMeas ? (ms.published - measFirst.published) : 0;
                           const quint64 uploadCountDelta =
                               haveMeas ? (viewport->uploadCount() - measFirst.uploadCount) : 0;
                           const quint64 uploadSumDelta =
                               haveMeas ? (viewport->uploadSumUs() - measFirst.uploadSumUs) : 0;
                           const double uploadMeanDeltaMs =
                               uploadCountDelta ? double(uploadSumDelta) / double(uploadCountDelta)
                                                      / 1000.0
                                                : 0.0;

                           const double memSlope = slopePerMinute(steadyT, steadyFp);
                           const double memGrowthMiB =
                               steadyFp.empty() ? 0.0 : steadyFp.back() - steadyFp.front();

                           // ── SL-C01 生产者健康 ────────────────────────────────
                           const bool c01 = prodFailDelta == 0
                                            && steadyProducerFps >= kTargetFpsFloor;
                           add("SL-C01", c01,
                               QStringLiteral("生产者零失败=%1（测量段渲染 %2 帧 / 失败 %3）；"
                                              "稳态实测 %4 fps（下限 %5）")
                                   .arg(prodFailDelta == 0 ? QStringLiteral("是")
                                                           : QStringLiteral("否"))
                                   .arg(renderedDelta)
                                   .arg(prodFailDelta)
                                   .arg(steadyProducerFps, 0, 'f', 2)
                                   .arg(kTargetFpsFloor, 0, 'f', 0));

                           // ── SL-C02 显示吞吐 ─────────────────────────────────
                           const bool c02 = steadyDisplayFps >= kTargetFpsFloor;
                           add("SL-C02", c02,
                               QStringLiteral("稳态显示帧率 %1 fps（下限 %2）；"
                                              "显示帧号 %3 → %4（窗口 %5s）")
                                   .arg(steadyDisplayFps, 0, 'f', 2)
                                   .arg(kTargetFpsFloor, 0, 'f', 0)
                                   .arg(windowDisplayedFirst)
                                   .arg(windowDisplayedLast)
                                   .arg(windowTLast - windowTFirst, 0, 'f', 0));

                           // ── SL-C03 上屏节奏（无异常长停顿）──────────────────
                           const bool c03 = !steadyIntervals.empty()
                                            && si.p99 <= kIntervalP99Ms
                                            && si.max <= kIntervalMaxMs;
                           add("SL-C03", c03,
                               QStringLiteral("稳态上屏间隔（%1 帧）：mean %2 / p50 %3 / "
                                              "p95 %4 / p99 %5 / max %6 ms"
                                              "（门槛 p99≤%7、max≤%8；vsync 下 16.7/33.4ms "
                                              "混排属正常漏一帧，判据只管异常长停顿）")
                                   .arg(quint64(steadyIntervals.size()))
                                   .arg(si.mean, 0, 'f', 2)
                                   .arg(si.p50, 0, 'f', 2)
                                   .arg(si.p95, 0, 'f', 2)
                                   .arg(si.p99, 0, 'f', 2)
                                   .arg(si.max, 0, 'f', 2)
                                   .arg(kIntervalP99Ms, 0, 'f', 0)
                                   .arg(kIntervalMaxMs, 0, 'f', 0));

                           // ── SL-C04 邮箱完整 ─────────────────────────────────
                           const bool c04 = dropDelta == 0;
                           add("SL-C04", c04,
                               QStringLiteral("测量段邮箱：投递 %1 / 丢弃 %2（须 0）")
                                   .arg(pubDelta)
                                   .arg(dropDelta));

                           // ── SL-C05 上传健康 ─────────────────────────────────
                           const bool c05 = uploadCountDelta > 0
                                            && uploadMeanDeltaMs < kUploadMeanMs
                                            && double(measureMaxUploadMs) < kUploadMaxMs;
                           add("SL-C05", c05,
                               QStringLiteral("测量段上传 %1 次，增量均值 %2ms（<%3），"
                                              "全程最坏 %4ms（<%5）")
                                   .arg(uploadCountDelta)
                                   .arg(uploadMeanDeltaMs, 0, 'f', 3)
                                   .arg(kUploadMeanMs, 0, 'f', 1)
                                   .arg(measureMaxUploadMs, 0, 'f', 2)
                                   .arg(kUploadMaxMs, 0, 'f', 0));

                           // ── SL-C06 帧龄有界 ─────────────────────────────────
                           const bool c06 = measureMaxAge > 0
                                            && double(measureMaxAge) < kFrameAgeMs;
                           add("SL-C06", c06,
                               QStringLiteral("测量段邮箱帧龄最大 %1ms（<%2）")
                                   .arg(measureMaxAge)
                                   .arg(kFrameAgeMs, 0, 'f', 0));

                           // ── SL-C07 内存稳定 ─────────────────────────────────
                           const bool c07 = !steadyFp.empty()
                                            && std::fabs(memSlope) <= kMemSlopeMiBPerMin;
                           add("SL-C07", c07,
                               QStringLiteral("稳态 phys_footprint 斜率 %1 MiB/min"
                                              "（|斜率|≤%2）；窗口内 %3 → %4 MiB（增 %5）")
                                   .arg(memSlope, 0, 'f', 3)
                                   .arg(kMemSlopeMiBPerMin, 0, 'f', 1)
                                   .arg(steadyFp.empty() ? 0.0 : steadyFp.front(), 0, 'f', 1)
                                   .arg(steadyFp.empty() ? 0.0 : steadyFp.back(), 0, 'f', 1)
                                   .arg(memGrowthMiB, 0, 'f', 1));

                           // ── SL-C08 环境未漂移 ───────────────────────────────
                           const bool c08 = *envViolations == 0 || allowThrottled;
                           add("SL-C08", c08,
                               QStringLiteral("测量期间环境漂移 %1 次违规（每 60s 复核；"
                                              "0 次才说明数据在合规环境取得）%2")
                                   .arg(*envViolations)
                                   .arg(allowThrottled ? QStringLiteral("；已允许降频，降级为警告")
                                                       : QString()));

                           // ── SL-C09 窗口暴露 ─────────────────────────────────
                           const bool c09 = measureRows > 0
                                            && measureExposedRows == measureRows;
                           add("SL-C09", c09,
                               QStringLiteral("测量段窗口暴露 %1/%2 个采样点"
                                              "（被遮挡/最小化时场景图降频，显示数据不可信）")
                                   .arg(measureExposedRows)
                                   .arg(measureRows));

                           // ── SL-C10 降级未误报 ───────────────────────────────
                           const double degradedShare =
                               measureRows ? double(measureDegradedRows) / double(measureRows)
                                           : 0.0;
                           const bool c10 = measureRows > 0
                                            && degradedShare <= kDegradedShareMax;
                           add("SL-C10", c10,
                               QStringLiteral("测量段降级误报 %1/%2 个采样点（%3%，上限 %4%）")
                                   .arg(measureDegradedRows)
                                   .arg(measureRows)
                                   .arg(degradedShare * 100.0, 0, 'f', 3)
                                   .arg(kDegradedShareMax * 100.0, 0, 'f', 0));

                           result->ran = true;
                           // SL-C09 失败 = 测量中窗口被遮挡/最小化（或屏保接管）：
                           // 显示/节奏/生产统计全部被污染，此时跑出来的 FAIL 没有诊断价值，
                           // 必须显式判 INVALID（数据不可信），与"管道真坏了"（FAIL）区分开。
                           // 实测案例：屏保 idleTime=600s 在测量中段接管 591s，exposed 段
                           // 本身 53.71 fps / 内存斜率 0.010 MiB/min，管道完全健康。
                           result->dataInvalid = !c09;
                           result->pass = (result->failCount == 0);
                           result->summary =
                               QStringLiteral("消费侧长跑：预热 %1s + 测量 %2s；"
                                              "稳态显示 %3 fps / 生产 %4 fps；"
                                              "上屏间隔 p95 %5ms / p99 %6ms；"
                                              "丢弃 %7；内存斜率 %8 MiB/min%9")
                                   .arg(options.warmupSeconds)
                                   .arg(options.measureSeconds)
                                   .arg(steadyDisplayFps, 0, 'f', 2)
                                   .arg(steadyProducerFps, 0, 'f', 2)
                                   .arg(si.p95, 0, 'f', 2)
                                   .arg(si.p99, 0, 'f', 2)
                                   .arg(dropDelta)
                                   .arg(memSlope, 0, 'f', 3)
                                   .arg(result->dataInvalid
                                            ? QStringLiteral("；VERDICT=INVALID（测量中窗口暴露不全，"
                                                             "统计被遮挡污染，须排除遮挡源后重跑）")
                                            : QString());
                           if (csvOpen)
                               result->details << QStringLiteral("SL-INFO 逐秒 CSV：%1（%2 行）")
                                                      .arg(options.csvPath)
                                                      .arg(rows->size());
                           if (framesOpen)
                               result->details << QStringLiteral("SL-INFO 逐帧上屏 CSV：%1（%2 行）")
                                                      .arg(options.framesCsvPath)
                                                      .arg(swaps->size());
                           Q_UNUSED(app)
                           Q_UNUSED(window)
                           done(*result);
                       });
}

} // namespace stelapp
