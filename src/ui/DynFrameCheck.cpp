// DynFrameCheck 实现。判据定义见头文件。
#include "ui/DynFrameCheck.hpp"

#include "render/legacy/FrameMailbox.hpp"
#include "ui/IFrameProducer.hpp"
#include "ui/quick/SkyViewport.hpp"

#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QQuickWindow>
#include <QTimer>
#include <QVector>
#include <algorithm>
#include <cstdio>

namespace stelapp {

namespace {

struct Sample
{
    double tSec = 0.0;
    quint64 displayed = 0;
    quint64 published = 0;
    quint64 dropped = 0;
    qint64 mailboxAgeMs = -1;
    quint64 producerRendered = 0;
    quint64 producerFailed = 0;
    double producerFps = 0.0;
    bool degraded = false;
    double displayedFps = 0.0;
};

} // namespace

DynFrameCheck::Options DynFrameCheck::optionsFromEnv()
{
    Options o;
    const int secs = qEnvironmentVariableIntValue("STELQUICK_DYN_SECONDS");
    if (secs > 0)
        o.seconds = secs;
    const QByteArray fpsEnv = qgetenv("STELQUICK_LIVE_FPS");
    if (!fpsEnv.isEmpty())
        o.producerFps = fpsEnv.toDouble();
    o.degradeProbe = !qEnvironmentVariableIsSet("STELQUICK_DYN_DEGRADE_PROBE")
                     || qEnvironmentVariableIntValue("STELQUICK_DYN_DEGRADE_PROBE") != 0;
    const QByteArray degradeEnv = qgetenv("STELQUICK_DEGRADE_FPS");
    if (!degradeEnv.isEmpty())
        o.degradeFps = degradeEnv.toDouble();
    return o;
}

void DynFrameCheck::runStartupSequence(QGuiApplication *app,
                                       QQuickWindow *window,
                                       SkyViewport *viewport,
                                       FrameMailbox *mailbox,
                                       IFrameProducer *producer,
                                       const Options &options,
                                       const std::function<void(const DynCheckResult &)> &done)
{
    auto result = std::make_shared<DynCheckResult>();
    if (!producer)
    {
        // 生产者装配由调用方负责（T12：两种生产者装配方式差异巨大）。
        // 走到这里说明调用方没装配成功——判据不成立，必须按 UNAVAILABLE 处理。
        result->ran = false;
        result->pass = false;
        result->summary = QStringLiteral("生产者未装配");
        done(*result);
        return;
    }

    // 降级判定阈值交给视口（P-BRG-04 的 UI 侧判定）
    viewport->setDegradeThreshold(options.degradeFps);

    auto samples = std::make_shared<QVector<Sample>>();
    auto clock = std::make_shared<QElapsedTimer>();
    auto degradedDuringLow = std::make_shared<bool>(false);
    auto degradedAfterRestore = std::make_shared<bool>(false);
    auto degradeLowActive = std::make_shared<bool>(false);
    // 降级探针时间窗（秒）：在 1/3 处降到 5fps，2/3 处恢复
    const double lowStart = options.seconds / 3.0;
    const double lowEnd = options.seconds * 2.0 / 3.0;

    result->ran = true;
    clock->start();

    // 逐 250ms 采样 CSV（STELQUICK_DYN_CSV=<path>）：诊断与留证用。
    // 为什么必须有：真实引擎生产者的产出速率**不是常数**——引擎首帧要编译着色器、
    // 上传纹理、加载星表，前几秒速率显著低于稳态（2026-09-23 T12 实测：8 秒窗口
    // 平均 31.5 fps vs 自报稳态 53.6 fps）。没有曲线就只能看到"总量不达标"的
    // 假红，无法判断是"生产者病态"还是"窗口被 warmup 主导"。
    // 注意用 shared_ptr<QFile>：定时器 lambda 按值捕获，栈上对象会在
    // runStartupSequence 返回后悬垂（2026-09-23 SkyLongRun 首跑 SIGSEGV 的同源坑）。
    auto csvPath = qEnvironmentVariable("STELQUICK_DYN_CSV");
    auto csv = std::make_shared<QFile>(csvPath);
    const bool csvOk = !csvPath.isEmpty() && csv->open(QIODevice::WriteOnly | QIODevice::Truncate);
    if (csvOk)
        csv->write("tSec,displayed,published,dropped,mailboxAgeMs,producerRendered,"
                   "producerFailed,producerFps,degraded,displayedFps\n");

    // 采样器：250ms 一次（首样 0ms 时 displayed=0，无意义，跳过）
    auto sampler = new QTimer(app);
    sampler->setInterval(250);
    QObject::connect(sampler, &QTimer::timeout, app, [window, viewport, mailbox,
                                                        producer, samples, clock, csv, csvOk,
                                                        degradedDuringLow, degradedAfterRestore,
                                                        degradeLowActive, lowStart, lowEnd,
                                                        nominalFps = options.producerFps]() {
        const double t = clock->elapsed() / 1000.0;

        // 降速探针调度（调 producer->setFps：LiveFrameSource 是跨线程原子量，
        // LiveSkyRuntime 是 GUI 线程内改 QTimer 间隔；本回调在 GUI 线程，两者皆合法）
        if (*degradeLowActive)
        {
            if (t >= lowEnd)
            {
                producer->setFps(nominalFps);   // 恢复名义速率
                *degradeLowActive = false;
            }
        }
        else if (t >= lowStart && t < lowEnd)
        {
            producer->setFps(5.0);
            *degradeLowActive = true;
        }

        const FrameMailbox::Stats ms = mailbox->stats();
        const ProducerCounters ps = producer->counters();
        Sample s;
        s.tSec = t;
        s.displayed = viewport->displayedFrameNumber();
        s.published = ms.published;
        s.dropped = ms.dropped;
        s.mailboxAgeMs = ms.latestFrameAgeMs;
        s.producerRendered = ps.rendered;
        s.producerFailed = ps.failed;
        s.producerFps = ps.fps;
        s.degraded = viewport->degraded();
        s.displayedFps = viewport->displayedFps();
        if (t >= lowStart + 1.0 && t < lowEnd && s.degraded)
            *degradedDuringLow = true;
        if (t >= lowEnd + 1.0 && !s.degraded && !samples->isEmpty())
            *degradedAfterRestore = true;
        samples->append(s);

        if (csvOk)
        {
            csv->write(QStringLiteral("%1,%2,%3,%4,%5,%6,%7,%8,%9,%10\n")
                           .arg(t, 0, 'f', 3)
                           .arg(s.displayed)
                           .arg(s.published)
                           .arg(s.dropped)
                           .arg(s.mailboxAgeMs)
                           .arg(s.producerRendered)
                           .arg(s.producerFailed)
                           .arg(s.producerFps, 0, 'f', 2)
                           .arg(s.degraded ? 1 : 0)
                           .arg(s.displayedFps, 0, 'f', 2)
                           .toUtf8());
            csv->flush();
        }
    });
    sampler->start();

    // 结束：先抓第一张，间隔 ~700ms 抓第二张（同一次事件循环内连抓两张必然相同——
// 2026-09-23 首跑踩到），再汇总判据。
    QTimer::singleShot(options.seconds * 1000, app, [app, sampler, window, viewport,
                                                      mailbox, producer, samples, csv, csvOk,
                                                      options, degradedDuringLow,
                                                      degradedAfterRestore, lowEnd, result, done]() {
        sampler->stop();
        auto grab1 = std::make_shared<QImage>(window->grabWindow());
        QTimer::singleShot(700, app, [window, viewport, mailbox, producer, samples,
                                      csv, csvOk,
                                      options, grab1, degradedDuringLow,
                                      degradedAfterRestore, lowEnd, result, done]() {
        const QImage grab2 = window->grabWindow();

        const FrameMailbox::Stats ms = mailbox->stats();
        const ProducerCounters ps = producer->counters();
        const double nominalFps = options.producerFps;

        // 生产者停机（先于判据打印，避免退出期泄漏；停机耗时计入总时长无妨）
        producer->stop();

        // 只 close，**不要** deleteLater：对象归 shared_ptr 管，deleteLater 会造成
        // "Qt 删一次 + shared_ptr 析构再删一次"的双重释放（2026-09-23 实测 SIGABRT(134)，
        // 判据全绿却拿到非零退出码——症状极具误导性）。
        if (csvOk)
            csv->close();

        // ── 稳态窗口（判据口径的核心，2026-09-23 T12 重定）─────────────────────
        // 为什么不再用"全程平均对名义速率"：两类与生产者健康**无关**的因素会污染平均——
        //   ① warmup：真实引擎首帧要编译着色器、上传纹理、加载星表，前几秒产出速率
        //      显著低于稳态。T12 实测：8 秒窗口平均 31.5 fps，而 1s 滑窗稳态 53.6 fps，
        //      差 40% → 用平均判据会**稳定假红**（与 T9 一致性首跑被降频同一性质）。
        //   ② 降速探针：中段人为压到 5 fps，是自检自己施加的，不该反过来惩罚生产者。
        // 判"生产者是否健康"要看**稳定状态下的能力**，故取尾窗稳态速率：
        //   起点 = max(降速段结束, 总时长 − max(3s, 时长×35%))
        // 降速探针关闭时起点无下限（取最后 3s 或 35%）。
        const double stableFrom = options.degradeProbe ? lowEnd : 0.0;
        const double tailStart = qMax(stableFrom,
                                      options.seconds - qMax(3.0, options.seconds * 0.35));
        const Sample *tailFirst = nullptr;
        for (const Sample &s : *samples)
            if (s.tSec >= tailStart && (!tailFirst || s.tSec < tailFirst->tSec))
                tailFirst = &s;
        const Sample *tailLast = samples->isEmpty() ? nullptr : &samples->last();
        double steadyProduceFps = 0.0;
        double steadyDisplayFps = 0.0;
        if (tailFirst && tailLast && tailLast->tSec > tailFirst->tSec)
        {
            const double dt = tailLast->tSec - tailFirst->tSec;
            steadyProduceFps = double(tailLast->producerRendered - tailFirst->producerRendered) / dt;
            steadyDisplayFps = double(tailLast->displayed - tailFirst->displayed) / dt;
        }
        const double produceFloor = 0.6 * nominalFps;
        const double displayFloor = 0.4 * nominalFps;

        auto add = [result](const QString &id, bool pass, const QString &detail) {
            result->details << QStringLiteral("%1 %2 %3").arg(id, pass ? "PASS" : "FAIL", detail);
            ++result->checkCount;
            if (!pass)
                ++result->failCount;
        };
        auto skip = [result](const QString &id, const QString &why) {
            result->details << QStringLiteral("%1 SKIP %2").arg(id, why);
            ++result->checkCount;   // SKIP 计数但不计失败
        };

        // D1-C01 生产者健康：零失败 + 稳态产出速率达标（下限 0.6×名义）
        const bool c01 = ps.failed == 0 && steadyProduceFps >= produceFloor;
        add("D1-C01", c01,
            QStringLiteral("生产者 failed=%1，尾窗[%2s起]稳态 %3 fps（下限 %4）"
                           "；全程累计 %5 帧（含 warmup）")
                .arg(ps.failed)
                .arg(tailStart, 0, 'f', 1)
                .arg(steadyProduceFps, 0, 'f', 1)
                .arg(produceFloor, 0, 'f', 1)
                .arg(ps.rendered));

        // D1-C02 显示推进：稳态显示速率达标（显示被 vsync 封顶，下限取 0.4×名义）
        const quint64 displayedDelta = samples->isEmpty() ? 0 : samples->last().displayed;
        const bool c02 = steadyDisplayFps >= displayFloor;
        add("D1-C02", c02,
            QStringLiteral("显示 尾窗稳态 %1 fps（下限 %2）；全程推进 %3 帧")
                .arg(steadyDisplayFps, 0, 'f', 1)
                .arg(displayFloor, 0, 'f', 1)
                .arg(displayedDelta));

        // D1-C03 邮箱有界
        const bool c03 = ms.published > 0
                         && double(ms.dropped) <= 0.05 * double(ms.published);
        add("D1-C03", c03,
            QStringLiteral("邮箱 published=%1 dropped=%2（≤5%）")
                .arg(ms.published).arg(ms.dropped));

        // D1-C04 帧龄有界（降速段帧龄天然变大：5fps → 年龄可达 ~200ms；阈值取 500ms）
        qint64 maxAge = 0;
        for (const Sample &s : *samples)
            maxAge = qMax(maxAge, s.mailboxAgeMs);
        const bool c04 = maxAge > 0 && maxAge < 500;
        add("D1-C04", c04,
            QStringLiteral("邮箱帧龄最大 %1ms（<500ms；降速段含 ~200ms 属预期）").arg(maxAge));

        // D1-C05 上传健康
        const quint64 uploadCount = viewport->uploadCount();
        const qreal uploadMax = viewport->uploadMaxMs();
        const qreal uploadMean = viewport->uploadMeanMs();
        const bool c05 = uploadCount > 0 && uploadMax < 100.0;
        add("D1-C05", c05,
            QStringLiteral("上传 count=%1 mean=%2ms max=%3ms（<100ms）")
                .arg(uploadCount)
                .arg(double(uploadMean), 0, 'f', 2)
                .arg(double(uploadMax), 0, 'f', 2));

        // D1-C06 内容动态（Vulkan 黑屏为 §6.2 已知缺陷 → SKIP，不判失败）
        const QString backend = viewport->backend();
        if (backend == QStringLiteral("Vulkan"))
        {
            skip("D1-C06",
                 QStringLiteral("Vulkan 纹理路径为 §6.2 已知外部缺陷（黑屏），逐像素动态判据在此后端不可用"));
        }
        else
        {
            const bool c06 = !grab1->isNull() && !grab2.isNull() && *grab1 != grab2;
            add("D1-C06", c06,
                QStringLiteral("相隔 700ms 两次抓帧%1（%2x%3）")
                    .arg(c06 ? "不同（内容动态）" : "相同或为空（内容静态/黑屏）")
                    .arg(grab1->width()).arg(grab1->height()));
        }

        // D1-C07 降级切换（P-BRG-04）
        if (options.degradeProbe)
        {
            const bool c07 = *degradedDuringLow && *degradedAfterRestore;
            add("D1-C07", c07,
                QStringLiteral("降速窗内 degraded=%1（须 true）/ 恢复后 degraded=%2（须 false）")
                    .arg(*degradedDuringLow ? "true" : "false",
                         *degradedAfterRestore ? "false" : "true"));
        }
        else
        {
            skip("D1-C07", "探针被 STELQUICK_DYN_DEGRADE_PROBE=0 关闭");
        }

        result->pass = result->failCount == 0;
        result->summary = QStringLiteral("动态帧通路自检：%1 判据，%2 失败")
                              .arg(result->checkCount).arg(result->failCount);
        done(*result);
        });
    });
}

} // namespace stelapp
