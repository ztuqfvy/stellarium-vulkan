// A2FrameCheck 实现。坐标系换算与容差说明见头文件。
#include "ui/A2FrameCheck.hpp"

#include "render/legacy/FrameMailbox.hpp"
#include "render/legacy/StaticFrameSource.hpp"
#include "render/legacy/TestPattern.hpp"
#include "ui/quick/SkyViewport.hpp"

#include <QGuiApplication>
#include <QImage>
#include <QPointF>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTimer>
#include <QtMath>
#include <cstdio>
#include <memory>

namespace stelapp {

namespace {

int channelDelta(const QColor &a, const QColor &b)
{
    return qMax(qMax(qAbs(a.red() - b.red()), qAbs(a.green() - b.green())),
                qAbs(a.blue() - b.blue()));
}

QString colorText(const QColor &c)
{
    return QStringLiteral("(%1,%2,%3)").arg(c.red()).arg(c.green()).arg(c.blue());
}

} // namespace

A2CheckResult A2FrameCheck::run(QQuickWindow *window, QQuickItem *viewport, const QSize &physicalSize)
{
    A2CheckResult result;

    if (!window || !viewport) {
        result.summary = QStringLiteral("校验无法进行：窗口或视口为空");
        return result;
    }

    const QImage grabbed = window->grabWindow();
    if (grabbed.isNull()) {
        result.summary = QStringLiteral("校验手段不可用：grabWindow() 返回空图");
        return result;
    }
    result.checkRan = true;

    // 诊断（A2 调试期保留）：区域统计 + 存盘。
    // 目的：区分"抓帧机制失效"与"视口没画东西"——前者整窗（含页头浅灰 #f5f6f8）全黑，
    // 后者只有视口区域黑而页头正常。不给全黑图一个笼统结论。
    {
        const int headerY = 20;   // 页头行内（逻辑 10px 处），物理点
        const QColor header = grabbed.pixelColor(grabbed.width() / 2, headerY);
        const QColor center = grabbed.pixelColor(grabbed.width() / 2, grabbed.height() / 2);
        result.details.append(QStringLiteral("诊断：页头像素%1（期望约(245,246,248)） 窗口中心像素%2")
                                  .arg(colorText(header), colorText(center)));
        const QString dumpPath = qEnvironmentVariable("STELQUICK_A2_DUMP");
        if (!dumpPath.isEmpty()) {
            if (grabbed.save(dumpPath))
                result.details.append(QStringLiteral("诊断：抓帧已存盘 %1").arg(dumpPath));
            else
                result.details.append(QStringLiteral("诊断：抓帧存盘失败 %1").arg(dumpPath));
        }
    }

    const qreal dpr = window->effectiveDevicePixelRatio();
    const QPointF sceneOrigin = viewport->mapToScene(QPointF(0.0, 0.0));

    result.details.append(QStringLiteral("抓帧尺寸=%1x%2 DPR=%3 视口场景原点=(%4,%5) 视口逻辑尺寸=%6x%7 纹理物理尺寸=%8x%9")
                              .arg(grabbed.width()).arg(grabbed.height())
                              .arg(dpr)
                              .arg(sceneOrigin.x()).arg(sceneOrigin.y())
                              .arg(viewport->width()).arg(viewport->height())
                              .arg(physicalSize.width()).arg(physicalSize.height()));

    const QVector<TestPattern::Probe> probes = TestPattern::probes(physicalSize);
    result.probeCount = probes.size();

    for (const TestPattern::Probe &probe : probes) {
        // 物理(纹理)点 → 逻辑点 → 场景点 → 抓帧设备点
        const QPointF localLogical(probe.pos.x() / dpr, probe.pos.y() / dpr);
        const QPointF scenePoint = sceneOrigin + localLogical;
        const int gx = qRound(scenePoint.x() * dpr);
        const int gy = qRound(scenePoint.y() * dpr);

        if (gx < 0 || gy < 0 || gx >= grabbed.width() || gy >= grabbed.height()) {
            ++result.failCount;
            result.details.append(QStringLiteral("  [FAIL] %1 抓帧坐标 (%2,%3) 越界（视口未铺满或映射错误）")
                                      .arg(probe.name).arg(gx).arg(gy));
            continue;
        }

        const QColor actual = grabbed.pixelColor(gx, gy);
        const int delta = channelDelta(actual, probe.expect);
        const int tolerance = probe.exact ? kColorTolerance : kAlphaTolerance;
        const bool ok = delta <= tolerance;

        if (!ok)
            ++result.failCount;

        result.details.append(QStringLiteral("  [%1] %2 纹理点(%3,%4) 抓帧点(%5,%6) 期望%7 实际%8 偏差%9")
                                  .arg(ok ? QStringLiteral("PASS") : QStringLiteral("FAIL"))
                                  .arg(probe.name)
                                  .arg(probe.pos.x()).arg(probe.pos.y())
                                  .arg(gx).arg(gy)
                                  .arg(colorText(probe.expect))
                                  .arg(colorText(actual))
                                  .arg(delta));
    }

    result.pass = (result.failCount == 0) && result.checkRan;
    result.summary = QStringLiteral("探针 %1 项，失败 %2 项")
                         .arg(result.probeCount).arg(result.failCount);
    return result;
}

// ── 启动期完整流程 ───────────────────────────────────────────────────────
void A2FrameCheck::runStartupSequence(QGuiApplication *app,
                                      QQuickWindow *window,
                                      QQuickItem *viewport,
                                      FrameMailbox *mailbox,
                                      const std::function<void(const A2CheckResult &)> &done)
{
    auto finish = std::make_shared<std::function<void(const A2CheckResult &)>>(done);

    if (!app || !window || !viewport || !mailbox) {
        A2CheckResult r;
        r.summary = QStringLiteral("A2 校验前置缺失：窗口/视口/邮箱为空");
        (*finish)(r);
        return;
    }

    struct State
    {
        QSize physical;
        quint64 frameNumber = 0;
        int waitedMs = 0;
        bool uploaded = false;
        std::shared_ptr<QTimer> poll;
    };
    auto state = std::make_shared<State>();

    // 第一步：等布局稳定（首帧渲染 + StackLayout 定尺寸），再投递
    QTimer::singleShot(400, app, [=]() {
        const QSize logical(qRound(viewport->width()), qRound(viewport->height()));
        const qreal dpr = window->effectiveDevicePixelRatio();
        state->physical = StaticFrameSource::physicalSizeFor(logical, dpr);
        state->frameNumber = 1;

        QString error;
        const bool published = StaticFrameSource::publishTestPattern(
            *mailbox, logical, dpr, state->frameNumber, 1, &error);
        std::printf("A2CHECK: 投递测试图案 逻辑=%dx%d DPR=%.2f 物理=%dx%d → %s%s%s\n",
                    logical.width(), logical.height(), dpr,
                    state->physical.width(), state->physical.height(),
                    published ? "成功" : "失败",
                    error.isEmpty() ? "" : " 原因：", error.toUtf8().constData());
        std::fflush(stdout);

        if (!published) {
            A2CheckResult r;
            r.summary = QStringLiteral("测试图案投递失败：%1").arg(error);
            (*finish)(r);
            return;
        }

        // 第二步：轮询等场景图确认已上传（不阻塞事件循环）
        auto poll = std::make_shared<QTimer>();
        poll->setInterval(50);
        auto *viewportRaw = viewport;
        QObject::connect(poll.get(), &QTimer::timeout, app, [=]() {
            state->waitedMs += 50;
            auto *sky = qobject_cast<SkyViewport *>(viewportRaw);
            const quint64 shown = sky ? sky->displayedFrameNumber() : state->frameNumber;
            if (qEnvironmentVariableIsSet("STELQUICK_A2_TRACE"))
                std::fprintf(stderr, "POLLTRACE: %dms cast=%s shown=%llu 目标=%llu\n",
                             state->waitedMs, sky ? "OK" : "失败",
                             (unsigned long long)shown,
                             (unsigned long long)state->frameNumber);
            if (shown >= state->frameNumber) {
                state->uploaded = true;
                poll->stop();
                // 已上传不等于已上屏：多等一会儿，确保抓帧抓到新内容，
                // 且（调试模式下）QML Image 有时间完成 file:// 异步加载
                QTimer::singleShot(400, app, [=]() {
                    const A2CheckResult r = A2FrameCheck::run(window, viewportRaw, state->physical);
                    (*finish)(r);
                });
                return;
            }
            if (state->waitedMs >= 4000) {
                poll->stop();
                A2CheckResult r;
                r.summary = QStringLiteral("超时：场景图未确认上传（等待 %1ms）").arg(state->waitedMs);
                (*finish)(r);
            }
        });
        state->poll = poll;
        poll->start();
    });
}

} // namespace stelapp
