// DynFrameCheck 实现。判据定义见头文件。
#include "ui/DynFrameCheck.hpp"

#include "render/legacy/FrameMailbox.hpp"
#include "ui/LiveFrameSource.hpp"
#include "ui/quick/SkyViewport.hpp"

#include <QElapsedTimer>
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
                                       const Options &options,
                                       const std::function<void(const DynCheckResult &)> &done)
{
    // 降级判定阈值交给视口（P-BRG-04 的 UI 侧判定）
    viewport->setDegradeThreshold(options.degradeFps);

    // 生产者配置
    LiveFrameSourceConfig cfg;
    cfg.physicalSize = options.physicalSize;
    cfg.devicePixelRatio = options.devicePixelRatio;
    cfg.fps = options.producerFps;
    cfg.simRate = 1.0;

    auto result = std::make_shared<DynCheckResult>();
    auto liveSource = std::make_shared<LiveFrameSource>();
    auto samples = std::make_shared<QVector<Sample>>();
    auto clock = std::make_shared<QElapsedTimer>();
    auto degradedDuringLow = std::make_shared<bool>(false);
    auto degradedAfterRestore = std::make_shared<bool>(false);
    auto degradeLowActive = std::make_shared<bool>(false);
    // 降级探针时间窗（秒）：在 1/3 处降到 5fps，2/3 处恢复
    const double lowStart = options.seconds / 3.0;
    const double lowEnd = options.seconds * 2.0 / 3.0;

    QString error;
    if (!liveSource->start(mailbox, cfg, &error))
    {
        result->ran = false;
        result->pass = false;
        result->summary = QStringLiteral("装配失败：%1").arg(error);
        done(*result);
        return;
    }
    result->ran = true;
    clock->start();

    // 采样器：250ms 一次（首样 0ms 时 displayed=0，无意义，跳过）
    auto sampler = new QTimer(app);
    sampler->setInterval(250);
    QObject::connect(sampler, &QTimer::timeout, app, [window, viewport, mailbox,
                                                        liveSource, samples, clock,
                                                        degradedDuringLow, degradedAfterRestore,
                                                        degradeLowActive, lowStart, lowEnd,
                                                        nominalFps = options.producerFps]() {
        const double t = clock->elapsed() / 1000.0;

        // 降速探针调度（跨线程调速，原子量）
        if (*degradeLowActive)
        {
            if (t >= lowEnd)
            {
                liveSource->setFps(nominalFps);   // 恢复名义速率
                *degradeLowActive = false;
            }
        }
        else if (t >= lowStart && t < lowEnd)
        {
            liveSource->setFps(5.0);
            *degradeLowActive = true;
        }

        const FrameMailbox::Stats ms = mailbox->stats();
        const LiveFrameSource::RuntimeStats ps = liveSource->runtimeStats();
        Sample s;
        s.tSec = t;
        s.displayed = viewport->displayedFrameNumber();
        s.published = ms.published;
        s.dropped = ms.dropped;
        s.mailboxAgeMs = ms.latestFrameAgeMs;
        s.producerRendered = ps.rendered;
        s.producerFailed = ps.failed;
        s.producerFps = ps.producerFps;
        s.degraded = viewport->degraded();
        s.displayedFps = viewport->displayedFps();
        if (t >= lowStart + 1.0 && t < lowEnd && s.degraded)
            *degradedDuringLow = true;
        if (t >= lowEnd + 1.0 && !s.degraded && !samples->isEmpty())
            *degradedAfterRestore = true;
        samples->append(s);
    });
    sampler->start();

    // 结束：先抓第一张，间隔 ~700ms 抓第二张（同一次事件循环内连抓两张必然相同——
// 2026-09-23 首跑踩到），再汇总判据。
    QTimer::singleShot(options.seconds * 1000, app, [app, sampler, window, viewport,
                                                      mailbox, liveSource, samples,
                                                      options, degradedDuringLow,
                                                      degradedAfterRestore, result, done]() {
        sampler->stop();
        auto grab1 = std::make_shared<QImage>(window->grabWindow());
        QTimer::singleShot(700, app, [window, viewport, mailbox, liveSource, samples,
                                      options, grab1, degradedDuringLow,
                                      degradedAfterRestore, result, done]() {
        const QImage grab2 = window->grabWindow();

        const FrameMailbox::Stats ms = mailbox->stats();
        const LiveFrameSource::RuntimeStats ps = liveSource->runtimeStats();
        const double nominalFps = options.producerFps;

        // 生产者停机（先于判据打印，避免退出期泄漏；停机耗时计入总时长无妨）
        liveSource->stop();

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

        // D1-C01 生产者健康（下限取 0.6×名义——降速探针段只产 ~5fps，属预期）
        const bool c01 = ps.failed == 0 && ps.rendered >= quint64(0.6 * nominalFps * options.seconds);
        add("D1-C01", c01,
            QStringLiteral("生产者 rendered=%1 failed=%2（期望 ≥%3，速率实测 %4 fps）")
                .arg(ps.rendered).arg(ps.failed)
                .arg(quint64(0.6 * nominalFps * options.seconds))
                .arg(ps.producerFps, 0, 'f', 1));

        // D1-C02 显示推进（降速段会拉低增量，按时间占比放宽到 0.4×）
        const quint64 displayedDelta = samples->isEmpty() ? 0
            : samples->last().displayed;
        const quint64 displayFloor = quint64(0.4 * nominalFps * options.seconds);
        const bool c02 = displayedDelta >= displayFloor;
        add("D1-C02", c02,
            QStringLiteral("显示帧号推进 %1（下限 %2，vsync 封顶属正常）")
                .arg(displayedDelta).arg(displayFloor));

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
