/*
 * AppFacadeCheck — 实现（T15 命令通路 + T16 单一仿真时钟）。判据清单见头注。
 *
 * 时间线（全部 GUI 线程 singleShot 链）：
 *   t0        ST 时钟纯逻辑自检 + AC-1/2/4/5/6/7 + AC-3a；设基线速率 R；记 jdA
 *   +400ms    AC-3b 前进基线（未暂停时 JD 必须在走）→ 暂停
 *   +900ms    取 jdP1
 *   +1400ms   AC-3c 冻结断言 → AC-8 把墙钟锚点拉远 1 小时
 *   +1800ms   AC-8 断言（墙钟路径未参与）→ AC-9a 外部跳转 +1000 天
 *   +2300ms   AC-9b 跳转值未被拽回 → 恢复推进，记窗口 A 起点
 *   +3100ms   AC-9c 从新锚点起算 + AC-10 窗口 A 量级 → 速率切 2R，记窗口 B 起点
 *   +3900ms   AC-10 比值断言 → AC-11 直接 core->setTimeRate(0)
 *   +4400ms   AC-11 冻结断言 → 恢复速率 → 汇总裁决
 */
#include "app/AppFacadeCheck.hpp"

#include "app/ActionRouter.hpp"
#include "app/AppFacade.hpp"

#include <QCoreApplication>
#include <QDebug>
#include <QKeyEvent>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTimer>

#if defined(STELQUICK_HAS_ENGINE)
#include "StelActionMgr.hpp"
#include "StelApp.hpp"
#include "StelMainView.hpp"
#include "core/StelClockController.hpp"
#include "core/StelCore.hpp"
#include <QDateTime>
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
    // AC-3 观测值
    double jdA = 0.0;
    double jdAdv = 0.0;
    double jdP1 = 0.0;
    double jdP2 = 0.0;
    int pausedSignalCount = 0;
    // T16 观测值
    double rateA = 0.0;        // 基线速率 R
    double jdWallProbe = 0.0;  // AC-8：墙钟锚点被拉远后的观测值
    double jdT = 0.0;          // AC-9a：跳转目标
    double jdHold = 0.0;       // AC-9b：跳转后静置观测值
    double jdResumeA = 0.0;    // AC-9c/AC-10：窗口 A 起点
    double jdWindowStart = 0.0; // 窗口计时起点（读值即被覆盖为推进量）
    double jdResumeB = 0.0;    // AC-10：窗口 B 起点
    double deltaA = 0.0;       // AC-10：窗口 A 推进量
    double jdZero = 0.0;       // AC-11：速率归零瞬间
    // AC-5 观测值
    quint64 toggledCount = 0;
    quint64 dispatchBefore = 0;

    void mark(QString line)
    {
        qDebug().noquote() << "ACTIONCHECK:" << line;
        result.details.append(line);
        // 约定：判据行以判定词收尾（OK / FAIL / SKIP 句尾）。
        if (line.endsWith(QStringLiteral("FAIL")))
            t0Ok = false;
    }

    //! 取引擎 StelCore（无引擎 / 未引导时返回 nullptr）。
    void *coreRaw() const
    {
#if defined(STELQUICK_HAS_ENGINE)
        if (!StelApp::isInitialized())
            return nullptr;
        return StelApp::getInstance().getCore();
#else
        return nullptr;
#endif
    }

    void finish()
    {
        result.ran = true;
        result.pass = t0Ok;
        result.summary = t0Ok ? QStringLiteral(
                            "T15/T16 自检：命令通路（拆除判定/zoom/暂停/禁用门/透传/焦点）"
                            "与单一仿真时钟（驱动源单点/墙钟旁路/跳转重锚/速率贯通）全过")
                              : QStringLiteral("T15/T16 自检存在失败项（见明细）");
        onDone(result);
    }
};

void step(Ctx *ctx, int ms, std::function<void()> body)
{
    // 注意：捕获列表里不放 ctx——lambda 体不用它，放了会触发
    // -Wunused-lambda-capture（context 已由 singleShot 的第二个参数给定）。
    QTimer::singleShot(ms, ctx->app, [body]() { body(); });
}

} // namespace

void AppFacadeCheck::runStartupSequence(QCoreApplication *app,
                                        QQuickWindow *window,
                                        AppFacade *facade,
                                        ActionRouter *router,
                                        const std::function<void(const Result &)> &onDone)
{
    auto *ctx = new Ctx;
    ctx->app = app;
    ctx->window = window;
    ctx->facade = facade;
    ctx->router = router;
    ctx->onDone = onDone;

    // ── ST：时钟控制器纯逻辑自检（无 GL / 无引擎，12 项）──────────────────────
    // 与下面 AC-* 的分工：ST-* 证明"时钟语义本身正确"，AC-* 证明"语义在真实引擎
    // 与帧泵下确实生效"。两层都过才算 T16 成立。
    {
        QStringList clockLog;
        const bool clockOk = StelClockController::selfTest(&clockLog);
        for (const QString &line : clockLog)
            ctx->mark(QStringLiteral("CLOCK/%1").arg(line));
        if (!clockOk)
            ctx->t0Ok = false;
    }

    // ── AC-1 拆除判定 ─────────────────────────────────────────────────────────
    bool widgetDispatchOff = false;
#if defined(STELQUICK_HAS_ENGINE)
    widgetDispatchOff = !StelAction::isWidgetShortcutDispatchEnabled();
#else
    widgetDispatchOff = true;   // 无引擎形态根本没有 widget 分发
#endif
    ctx->mark(widgetDispatchOff ? QStringLiteral("AC-1 widget QAction 分发拆除：OK（已关闭）")
                                : QStringLiteral("AC-1 widget QAction 分发拆除：FAIL（仍在注册）"));

    // ── AC-7 唯一 update 驱动源（T16）─────────────────────────────────────────
    // 这是 T14 §1.1 结论"单源单链"在合流形态下的可观测判据。注意它**不是**在修
    // 一个正在发生的 bug：30 分钟长跑日志里 paintGL/drawEnded 各 0 次，旧路径从未
    // 执行。本判据把"恰好不发生"固化为"机制上不允许"（守卫见 StelMainView::drawEnded）。
#if defined(STELQUICK_HAS_ENGINE)
    {
        bool hostDriven = false;
        bool legacyTimerActive = true;
        QString modeName = QStringLiteral("(引擎未引导)");
        if (ctx->coreRaw())
        {
            StelCore *core = static_cast<StelCore *>(ctx->coreRaw());
            hostDriven = core->isSimClockHostDriven();
            modeName = core->getSimClockModeName();
            legacyTimerActive = StelMainView::getInstance().isLegacyFrameTimerActive();
        }
        const bool c7 = hostDriven && !legacyTimerActive;
        ctx->mark(QStringLiteral("AC-7 唯一 update 驱动源：simClock=%1，旧宿主 fpsTimer=%2：%3")
                      .arg(modeName,
                           legacyTimerActive ? QStringLiteral("仍在跑")
                                             : QStringLiteral("未激活"),
                           c7 ? QStringLiteral("OK") : QStringLiteral("FAIL")));
    }
#else
    ctx->mark(QStringLiteral("AC-7 唯一 update 驱动源：SKIP（无引擎形态）"));
#endif

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

    // ── AC-12 QML 键盘挂载点端到端（T17 引入）─────────────────────────────────
    //
    // 为什么需要这条：AC-5 证明的是 **C++ 侧** routeKey 逻辑正确（它直接调函数），
    // 却证明不了"QML 那把键盘真的接到了 routeKey 上"。事实是 T15 把 Keys.onPressed
    // 挂在 ApplicationWindow 上——`Keys` 是 Item 的附加属性，ApplicationWindow 继承自
    // Window 而非 Item，挂载**从未生效**（运行时告警 "Could not attach Keys property
    // ... is not an Item"，自 T15 起每份证据里都在，却没人把它与判据联系起来）。
    // ⇒ 典型"仪器没接在实况上"：判据全绿而实况是坏的。
    //
    // 本条就地补上：
    //   ① 挂载点必须存在，且确实是 Item（Keys 可附加的前提）；
    //   ② 若场景图有 activeFocusItem（窗口已激活），把按键**真的从窗口投递进去**，
    //      断言引擎动作恰好触发一次——这才是端到端；
    //   ③ 窗口未激活时 SKIP（自动化运行下常见）：这是环境条件，不是缺陷，
    //      照实写明"挂载点已就位但端到端未验"，不伪装成 OK。
    {
        QQuickItem *sink = window ? window->findChild<QQuickItem *>(QStringLiteral("skyKeySink"))
                                  : nullptr;
        StelAction *cardinal =
            StelApp::isInitialized()
                ? StelApp::getInstance().getStelActionManager()->findAction(
                      QStringLiteral("actionShow_Cardinal_Points"))
                : nullptr;
        QQuickItem *focusItem = window ? window->activeFocusItem() : nullptr;

        if (!sink) {
            ctx->mark(QStringLiteral("AC-12 QML 键盘挂载点：FAIL（未找到 skyKeySink Item）"));
        } else if (!cardinal) {
            ctx->mark(QStringLiteral("AC-12 QML 键盘挂载点：SKIP（未找到 actionShow_Cardinal_Points）"));
        } else if (!focusItem) {
            ctx->mark(QStringLiteral("AC-12 QML 键盘挂载点：SKIP（挂载点 skyKeySink 就位=OK，"
                                     "但场景图无 activeFocusItem（窗口未激活），端到端未验）"));
        } else {
            const quint64 before = router->dispatchCount();
            const bool checked0 = cardinal->isChecked();
            QKeyEvent press(QEvent::KeyPress, static_cast<int>(Qt::Key_Q), Qt::NoModifier);
            QCoreApplication::sendEvent(window, &press);
            const bool e2e = (router->dispatchCount() == before + 1)
                             && (cardinal->isChecked() != checked0);
            ctx->mark(QStringLiteral("AC-12 QML 键盘挂载点端到端（Q 键经窗口投递恰触发一次）：%1")
                          .arg(e2e ? QStringLiteral("OK") : QStringLiteral("FAIL")));
        }
    }

    // ── AC-3a 同值幂等 ────────────────────────────────────────────────────────
    QObject::connect(facade, &AppFacade::simulationPausedChanged, ctx->app,
                     [ctx](bool) { ++ctx->pausedSignalCount; }, Qt::DirectConnection);
    facade->setSimulationPaused(false);   // 默认即 false → 不应发信号
    const bool idem = ctx->pausedSignalCount == 0;
    ctx->mark(idem ? QStringLiteral("AC-3a 同值 setSimulationPaused 幂等（零信号）：OK")
                   : QStringLiteral("AC-3a 同值 setSimulationPaused 幂等（零信号）：FAIL"));

    // T16：设基线速率 R。此后 AC-9/AC-10 的推进量都以它为参照。
    ctx->rateA = 0.1;
    facade->setTimeRate(ctx->rateA);

    ctx->jdA = facade->julianDay();

    // ── AC-3 时间线 ───────────────────────────────────────────────────────────
    step(ctx, 400, [ctx]() {
        // AC-3b 前进基线：未暂停时 JD 必须在走（帧泵已在跑）
        ctx->jdAdv = ctx->facade->julianDay();
        const bool advancing = ctx->jdAdv > ctx->jdA + 1e-9;
        ctx->mark(QStringLiteral("AC-3b 运行态 JD 推进（+0.4s @rate=%1，Δ=%2 天）：%3")
                      .arg(ctx->rateA, 0, 'f', 3)
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

                // ── AC-8 墙钟路径未参与推进（T16）────────────────────────────
                // 暂停态下把**墙钟锚点**人为拉远 1 小时。
                //   · EngineWallClock 路径仍在跑的话：JD = 锚点 + 3600s × rate
                //     = 锚点 + 360 天（rate=0.1）——肉眼可见的巨跳；
                //   · HostDriven：updateTime 根本不读锚点，JD 严格不变。
                // 这是"墙钟路径确实被旁路"的决定性判据，且是严格相等断言，
                // 不依赖任何时间窗/抖动容忍。锚点污染由下面的 setJD 自我修复
                // （setJD → resetSync 会把 milliSecondsOfLastJDUpdate 拉回当前）。
#if defined(STELQUICK_HAS_ENGINE)
                if (ctx->coreRaw())
                {
                    static_cast<StelCore *>(ctx->coreRaw())
                        ->setMilliSecondsOfLastJDUpdate(QDateTime::currentMSecsSinceEpoch() - 3600 * 1000);
                }
#endif
                step(ctx, 400, [ctx]() {
                    ctx->jdWallProbe = ctx->facade->julianDay();
                    const double wallDrift = ctx->jdWallProbe - ctx->jdP2;
                    const bool wallBypassed = qAbs(wallDrift) < 1e-12;
                    ctx->mark(QStringLiteral("AC-8 墙钟锚点拉远 1h 后 JD 仍严格不变（Δ=%1 天，"
                                             "证明 updateTime 不读墙钟）：%2")
                                  .arg(wallDrift, 0, 'e', 3)
                                  .arg(wallBypassed ? QStringLiteral("OK") : QStringLiteral("FAIL")));

                    // ── AC-9a 外部跳转重锚（T16 的头号目标）──────────────────
                    // 模拟插件/脚本：**直接** core->setJD（不经 AppFacade，不经帧泵）。
                    // T15 的宿主闭式公式会在下一帧把跳转值覆盖回 m_jdAccum——
                    // "跳一下就被拽回来"。T16 里宿主推的是同一个真源，跳转必须站住。
                    ctx->jdT = ctx->jdWallProbe + 1000.0;   // 1000 天之后
#if defined(STELQUICK_HAS_ENGINE)
                    if (ctx->coreRaw())
                        static_cast<StelCore *>(ctx->coreRaw())->setJD(ctx->jdT);
#endif
                    step(ctx, 500, [ctx]() {
                        ctx->jdHold = ctx->facade->julianDay();
                        const bool held = qAbs(ctx->jdHold - ctx->jdT) < 1e-12;
                        ctx->mark(QStringLiteral("AC-9b 暂停态外部跳转静置 0.5s 仍严格成立"
                                                 "（目标 %1，观测 %2）：%3")
                                      .arg(ctx->jdT, 0, 'f', 3)
                                      .arg(ctx->jdHold, 0, 'f', 3)
                                      .arg(held ? QStringLiteral("OK") : QStringLiteral("FAIL")));

                        // ── AC-9c 恢复后从**新锚点**起算 ─────────────────────
                        ctx->facade->setSimulationPaused(false);
                        ctx->jdResumeA = ctx->facade->julianDay();
                        ctx->jdWindowStart = ctx->jdResumeA;
                        step(ctx, 800, [ctx]() {
                            ctx->deltaA = ctx->facade->julianDay() - ctx->jdWindowStart;
                            // 从新锚点起、量级合理（R=0.1 × 0.8s ≈ 0.08 天；
                            // 容差 ±30%：dt 用真实墙钟差，总推进量对 tick 抖动不敏感）
                            const bool fromNewAnchor = ctx->deltaA > 0.056 && ctx->deltaA < 0.104;
                            ctx->mark(QStringLiteral("AC-9c 恢复 0.8s 推进 Δ=%1 天"
                                                     "（期望 %2±30%，起点=跳转值 %3）：%4")
                                          .arg(ctx->deltaA, 0, 'f', 6)
                                          .arg(ctx->rateA * 0.8, 0, 'f', 6)
                                          .arg(ctx->jdResumeA, 0, 'f', 3)
                                          .arg(fromNewAnchor ? QStringLiteral("OK")
                                                             : QStringLiteral("FAIL")));

                            // ── AC-10 速率线性贯通：窗口 B 用 2R ─────────────
                            ctx->facade->setTimeRate(ctx->rateA * 2.0);
                            ctx->jdWindowStart = ctx->facade->julianDay();
                            step(ctx, 800, [ctx]() {
                                const double deltaB = ctx->facade->julianDay() - ctx->jdWindowStart;
                                const double ratio = ctx->deltaA > 1e-9 ? deltaB / ctx->deltaA : -1.0;
                                const bool linear = ratio > 1.5 && ratio < 2.6;
                                ctx->mark(QStringLiteral("AC-10 速率贯通：R=%1 窗口 Δ=%2，2R=%3 窗口 Δ=%4，"
                                                         "比值 %5（期望 ≈2）：%6")
                                              .arg(ctx->rateA, 0, 'f', 3)
                                              .arg(ctx->deltaA, 0, 'f', 6)
                                              .arg(ctx->rateA * 2.0, 0, 'f', 3)
                                              .arg(deltaB, 0, 'f', 6)
                                              .arg(ratio, 0, 'f', 3)
                                              .arg(linear ? QStringLiteral("OK")
                                                          : QStringLiteral("FAIL")));

                                // ── AC-11 速率 0 真冻结（插件零改造的判据）────
                                // **直接调引擎** setTimeRate(0)——等价于插件/快捷动作
                                // actionSet_Time_Rate_Zero 的行为。T16 之前宿主用自己
                                // 的速率字段推进，插件这样调是**停不住**的；T16 之后速率
                                // 就是推进真源，必须严格冻结。
#if defined(STELQUICK_HAS_ENGINE)
                                if (ctx->coreRaw())
                                    static_cast<StelCore *>(ctx->coreRaw())->setTimeRate(0.0);
#endif
                                ctx->jdZero = ctx->facade->julianDay();
                                step(ctx, 500, [ctx]() {
                                    const double drift = ctx->facade->julianDay() - ctx->jdZero;
                                    const bool zeroFreeze = qAbs(drift) < 1e-12;
                                    ctx->mark(QStringLiteral("AC-11 插件式 core->setTimeRate(0) 后 "
                                                             "0.5s 漂移 %1 天（门槛 1e-12）：%2")
                                                  .arg(drift, 0, 'e', 3)
                                                  .arg(zeroFreeze ? QStringLiteral("OK")
                                                                  : QStringLiteral("FAIL")));
                                    // 收尾：恢复基线速率，避免给后续复用同一进程的判据留状态
                                    ctx->facade->setTimeRate(ctx->rateA);
                                    ctx->finish();
                                });
                            });
                        });
                    });
                });
            });
        });
    });
}

} // namespace stelapp
