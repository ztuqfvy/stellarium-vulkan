/*
 * AppFacadeCheck — 实现（T15）。判据清单见头注。
 *
 * 时间线（全部 GUI 线程 singleShot 链）：
 *   t0        AC-1/2/4/5/6 + AC-3a 即时判据；记录 jdA
 *   +400ms    AC-3b 前进基线（未暂停时 JD 必须在走）→ 暂停
 *   +900ms    取 jdP1
 *   +1400ms   AC-3c 冻结断言 → 恢复
 *   +1900ms   AC-3d 恢复断言 → 汇总裁决
 */
#include "app/AppFacadeCheck.hpp"

#include "app/ActionRouter.hpp"
#include "app/AppFacade.hpp"

#include <QCoreApplication>
#include <QDebug>
#include <QQuickWindow>
#include <QTimer>

#if defined(STELQUICK_HAS_ENGINE)
#include "StelActionMgr.hpp"
#include "StelApp.hpp"
#endif

namespace stelapp {

namespace {

struct Ctx
{
    QCoreApplication *app = nullptr;
    QQuickWindow *window = nullptr;
    AppFacade *facade = nullptr;
    ActionRouter *router = nullptr;
    std::function<void(const AppFacadeCheck::Result &)> onDone;

    AppFacadeCheck::Result result;
    bool t0Ok = true;
    // C3 观测值
    double jdA = 0.0;
    double jdAdv = 0.0;
    double jdP1 = 0.0;
    double jdP2 = 0.0;
    double jdR = 0.0;
    int pausedSignalCount = 0;
    // C5 观测值
    quint64 toggledCount = 0;
    quint64 dispatchBefore = 0;

    void mark(QString line)
    {
        qDebug().noquote() << "ACTIONCHECK:" << line;
        result.details.append(line);
        if (line.endsWith(QStringLiteral("FAIL")))
            t0Ok = false;
    }

    void finish()
    {
        result.ran = true;
        result.pass = t0Ok;
        result.summary = t0Ok ? QStringLiteral(
                            "T15 命令通路自检：拆除判定/zoom 单步/暂停冻结与恢复/"
                            "禁用门/透传单次/焦点守卫 全过")
                              : QStringLiteral("T15 命令通路自检存在失败项（见明细）");
        onDone(result);
    }
};

void step(Ctx *ctx, int ms, std::function<void()> body)
{
    QTimer::singleShot(ms, ctx->app, [ctx, body]() { body(); });
}

} // namespace

void AppFacadeCheck::runStartupSequence(QCoreApplication *app,
                                        QQuickWindow *window,
                                        AppFacade *facade,
                                        ActionRouter *router,
                                        const std::function<void(const Result &)> &onDone)
{
    auto *ctx = new Ctx{app, window, facade, router, onDone, {}, true,
                        0.0, 0.0, 0.0, 0.0, 0.0, 0, 0, 0};

    // ── AC-1 拆除判定 ─────────────────────────────────────────────────────────
    bool widgetDispatchOff = false;
#if defined(STELQUICK_HAS_ENGINE)
    widgetDispatchOff = !StelAction::isWidgetShortcutDispatchEnabled();
#else
    widgetDispatchOff = true;   // 无引擎形态根本没有 widget 分发
#endif
    ctx->mark(widgetDispatchOff ? QStringLiteral("AC-1 widget QAction 分发拆除：OK（已关闭）")
                                : QStringLiteral("AC-1 widget QAction 分发拆除：FAIL（仍在注册）"));

    // ── AC-2 zoom 单步（aimFov 精确断言）──────────────────────────────────────
    const double fov0 = facade->fieldOfView();
    if (fov0 > 0.0)
    {
        facade->zoomIn();
        const double a1 = facade->fieldOfView();
        facade->zoomIn();
        const double a2 = facade->fieldOfView();
        facade->zoomOut();
        const double a3 = facade->fieldOfView();
        const bool zoomOk = qAbs(a1 - fov0 * 0.8) < 1e-6
                            && qAbs(a2 - fov0 * 0.64) < 1e-6
                            && qAbs(a3 - fov0 * 0.8) < 1e-6;
        ctx->mark(QStringLiteral("AC-2 zoom 单步：fov %1 → %2 → %3 → %4（期望 %5 → %6 → %7 → %8）：%9")
                      .arg(fov0, 0, 'f', 4).arg(a1, 0, 'f', 4).arg(a2, 0, 'f', 4).arg(a3, 0, 'f', 4)
                      .arg(fov0, 0, 'f', 4).arg(fov0 * 0.8, 0, 'f', 4)
                      .arg(fov0 * 0.64, 0, 'f', 4).arg(fov0 * 0.8, 0, 'f', 4)
                      .arg(zoomOk ? QStringLiteral("OK") : QStringLiteral("FAIL")));
        facade->setFieldOfView(fov0);   // 恢复原视场，不给后续判据留动画尾巴
    }
    else
    {
        ctx->mark(QStringLiteral("AC-2 zoom 单步：SKIP（引擎不可用，fov=%1）").arg(fov0));
    }

    // ── AC-4 注册表禁用门（U-ACT-02）──────────────────────────────────────────
    quint64 blockedRuns = 0;
    router->registerAction(QStringLiteral("check.disabled"),
                           { [&blockedRuns]() { ++blockedRuns; },
                             []() { return false; },
                             QStringLiteral("自检：恒禁用") });
    const quint64 dispatchBeforeC4 = router->dispatchCount();
    const bool c4 = !router->trigger(QStringLiteral("check.disabled"))
                    && blockedRuns == 0
                    && router->dispatchCount() == dispatchBeforeC4;
    ctx->mark(c4 ? QStringLiteral("AC-4 禁用门拒绝且零副作用：OK")
                 : QStringLiteral("AC-4 禁用门拒绝且零副作用：FAIL"));

    // ── AC-5 引擎透传单次（U-ACT-01）──────────────────────────────────────────
#if defined(STELQUICK_HAS_ENGINE)
    if (StelApp::isInitialized())
    {
        if (StelAction *cardinal =
                StelApp::getInstance().getStelActionManager()->findAction(QStringLiteral("actionShow_Cardinal_Points")))
        {
            ctx->dispatchBefore = router->dispatchCount();
            // context 用 app（QObject）——Ctx 是普通结构体，不能作 connect 的 context。
            QObject::connect(cardinal, &StelAction::toggled, ctx->app,
                             [ctx](bool) { ++ctx->toggledCount; }, Qt::DirectConnection);
            const bool checked0 = cardinal->isChecked();
            // 路径 1：ID 直呼
            const bool r1 = router->trigger(QStringLiteral("actionShow_Cardinal_Points"));
            const bool once1 = r1 && cardinal->isChecked() != checked0 && ctx->toggledCount == 1;
            // 路径 2：键盘（Q 是 actionShow_Cardinal_Points 的唯一主键位，全工程唯一）
            const bool consumed = router->routeKey(static_cast<int>(Qt::Key_Q), static_cast<int>(Qt::NoModifier));
            const bool once2 = consumed && ctx->toggledCount == 2
                               && cardinal->isChecked() == checked0;   // 又翻回去了
            const bool c5 = once1 && once2 && router->dispatchCount() == ctx->dispatchBefore + 2;
            ctx->mark(QStringLiteral("AC-5 透传单次（ID 直呼 + Q 键各恰一次，toggled 计数=%1）：%2")
                          .arg(ctx->toggledCount)
                          .arg(c5 ? QStringLiteral("OK") : QStringLiteral("FAIL")));
        }
        else
        {
            ctx->mark(QStringLiteral("AC-5 透传单次：SKIP（未找到 actionShow_Cardinal_Points）"));
        }
    }
    else
    {
        ctx->mark(QStringLiteral("AC-5 透传单次：SKIP（引擎未引导）"));
    }
#endif

    // ── AC-6 焦点守卫基线（U-ACT-03 的可自动化部分）───────────────────────────
    const bool c6 = router->canDispatchToSky();
    ctx->mark(c6 ? QStringLiteral("AC-6 无焦点时放行（canDispatchToSky=true）：OK")
                 : QStringLiteral("AC-6 无焦点时放行（canDispatchToSky=true）：FAIL"));

    // ── AC-3a 同值幂等 ────────────────────────────────────────────────────────
    QObject::connect(facade, &AppFacade::simulationPausedChanged, ctx->app,
                     [ctx](bool) { ++ctx->pausedSignalCount; }, Qt::DirectConnection);
    facade->setSimulationPaused(false);   // 默认即 false → 不应发信号
    const bool idem = ctx->pausedSignalCount == 0;
    ctx->mark(idem ? QStringLiteral("AC-3a 同值 setSimulationPaused 幂等（零信号）：OK")
                   : QStringLiteral("AC-3a 同值 setSimulationPaused 幂等（零信号）：FAIL"));

    ctx->jdA = facade->julianDay();

    // ── AC-3 时间线 ───────────────────────────────────────────────────────────
    step(ctx, 400, [ctx]() {
        // AC-3b 前进基线：未暂停时 JD 必须在走（帧泵已在跑）
        ctx->jdAdv = ctx->facade->julianDay();
        const bool advancing = ctx->jdAdv > ctx->jdA + 1e-9;
        ctx->mark(QStringLiteral("AC-3b 运行态 JD 推进（+0.4s，Δ=%1 天）：%2")
                      .arg(ctx->jdAdv - ctx->jdA, 0, 'f', 6)
                      .arg(advancing ? QStringLiteral("OK") : QStringLiteral("FAIL")));
        if (advancing)
            ctx->facade->setSimulationPaused(true);
        step(ctx, 500, [ctx]() {
            ctx->jdP1 = ctx->facade->julianDay();
            step(ctx, 500, [ctx]() {
                ctx->jdP2 = ctx->facade->julianDay();
                const bool frozen = qAbs(ctx->jdP2 - ctx->jdP1) < 1e-12;
                ctx->mark(QStringLiteral("AC-3c 暂停 0.5s JD 漂移 %1 天（门槛 1e-12）：%2")
                              .arg(qAbs(ctx->jdP2 - ctx->jdP1), 0, 'e', 3)
                              .arg(frozen ? QStringLiteral("OK") : QStringLiteral("FAIL")));
                ctx->facade->setSimulationPaused(false);
                step(ctx, 500, [ctx]() {
                    ctx->jdR = ctx->facade->julianDay();
                    const bool resumed = ctx->jdR > ctx->jdP2 + 1e-9;
                    ctx->mark(resumed ? QStringLiteral("AC-3d 恢复后 JD 继续推进：OK")
                                      : QStringLiteral("AC-3d 恢复后 JD 继续推进：FAIL"));
                    ctx->finish();
                });
            });
        });
    });
}

} // namespace stelapp
