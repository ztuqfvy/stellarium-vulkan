/*
 * DisplayProbe — 实现（T38-A 显示参数命令面探针）。设计与五个问题见头注。
 *
 * 驱动方式沿用 ToolbarProbe / LocationProbe 的线性步骤表：每步 = (跑完后等多少 ms, 步骤体)。
 * ⚠️ delayAfter 语义 = "跑完**本步**之后的等待"（T33 陷阱 41 / T37 陷阱 69）：
 * 写 0 时下一步与本步只隔一个事件循环；**凡"写后要读"的配对，读一律放下一步**
 * （同事件循环里读 = 读到写前的值，是 T37 那个假绿的同族病根）。
 */
#include "app/DisplayProbe.hpp"

#include "app/AppFacade.hpp"

#include <QCoreApplication>
#include <QTimer>
#include <QVector>

#if defined(STELQUICK_HAS_ENGINE)
#include "StelActionMgr.hpp"
#include "StelApp.hpp"
#include "StelCore.hpp"
#include "StelMovementMgr.hpp"
#include "StelSkyDrawer.hpp"
#endif

namespace stelapp {

namespace {

using ProbeResult = DisplayProbe::Result;

#if defined(STELQUICK_HAS_ENGINE)

//! Q1 的被测项：StelSkyDrawer 的数值属性（全部带 NOTIFY ⇒ 可给 QML 建依赖）。
struct NumProp
{
    const char *name;
    std::function<double()> get;
    std::function<void(double)> set;
    double probeValue;      //!< 刻意取"远"值 —— setter 若夹取，回读会露馅
};

QVector<NumProp> numProps()
{
    return {
        {"relativeStarScale", [] { return StelApp::getInstance().getCore()->getSkyDrawer()->getRelativeStarScale(); },
         [](double v) { StelApp::getInstance().getCore()->getSkyDrawer()->setRelativeStarScale(v); }, 4.0},
        {"absoluteStarScale", [] { return StelApp::getInstance().getCore()->getSkyDrawer()->getAbsoluteStarScale(); },
         [](double v) { StelApp::getInstance().getCore()->getSkyDrawer()->setAbsoluteStarScale(v); }, 3.0},
        {"lightPollutionLuminance",
         [] { return StelApp::getInstance().getCore()->getSkyDrawer()->getLightPollutionLuminance(); },
         [](double v) { StelApp::getInstance().getCore()->getSkyDrawer()->setLightPollutionLuminance(v); }, 5.0},
        {"customStarMagLimit",
         [] { return StelApp::getInstance().getCore()->getSkyDrawer()->getCustomStarMagnitudeLimit(); },
         [](double v) { StelApp::getInstance().getCore()->getSkyDrawer()->setCustomStarMagnitudeLimit(v); }, 12.5},
    };
}

struct Ctx
{
    QCoreApplication *app = nullptr;
    AppFacade *facade = nullptr;
    std::function<void(const ProbeResult &)> onDone;
    ProbeResult result;
    int nextDelay = 0;

    QStringList lines;

    //! NOTIFY 计数（Q5）：决定 QML 能不能对"引擎自己改的值"建依赖。
    int relStarNotify = 0;
    int absStarNotify = 0;
    int lightPollutionNotify = 0;
    int magLimitNotify = 0;
    int fovNotify = 0;
    int projKeyNotify = 0;
    QVector<QMetaObject::Connection> conns;

    //! 初值台账（收尾还原）。
    struct Initial
    {
        double relStarScale = 1.0;
        double absStarScale = 1.0;
        double lightPollution = 1.0;
        double customMagLimit = 6.0;
        bool flagMagLimit = false;
        double fov = 60.0;
        QString projectionKey;
    } initial;

    //! 静置期 NOTIFY 基线（Q6：决定滑块用"绑定"还是"轮询"）。
    int idleBaseRel = 0;
    int idleBaseFov = 0;
    int idleBaseProj = 0;

    void note(const QString &line) { lines.append(line); }

    void finish()
    {
        for (const QMetaObject::Connection &c : conns)
            QObject::disconnect(c);
        result.ran = true;
        result.summary =
            QStringLiteral("显示参数命令面探针：只报读数、不下结论（断言见 T38-C 的 DisplayCheck）");
        result.details = lines;
        onDone(result);
    }
};

//! 驱动步骤：跑完本步后等 delayAfter 毫秒进下一步。
struct Step
{
    int delayAfter = 0;
    std::function<void(Ctx *)> body;
};

#endif  // STELQUICK_HAS_ENGINE

} // namespace

void DisplayProbe::run(QCoreApplication *app,
                       AppFacade *facade,
                       const std::function<void(const Result &)> &onDone,
                       int delayMs)
{
#if !defined(STELQUICK_HAS_ENGINE)
    Q_UNUSED(app)
    Q_UNUSED(facade)
    Q_UNUSED(delayMs)
    Result r;
    r.unavailable = true;
    r.summary = QStringLiteral("显示参数探针需要合流形态构建（STELQUICK_HAS_ENGINE 未定义）");
    QTimer::singleShot(0, app, [onDone, r]() { onDone(r); });
#else
    auto *ctx = new Ctx;
    ctx->app = app;
    ctx->facade = facade;
    ctx->onDone = onDone;

    // Q5 观测：从起跑就挂（3 参连接：Ctx 不是 QObject；finish() 里统一断开）。
    {
        StelSkyDrawer *sd = StelApp::getInstance().getCore()->getSkyDrawer();
        StelMovementMgr *mm = StelApp::getInstance().getCore()->getMovementMgr();
        StelCore *core = StelApp::getInstance().getCore();
        ctx->conns << QObject::connect(sd, &StelSkyDrawer::relativeStarScaleChanged,
                                       [ctx](double) { ctx->relStarNotify++; });
        ctx->conns << QObject::connect(sd, &StelSkyDrawer::absoluteStarScaleChanged,
                                       [ctx](double) { ctx->absStarNotify++; });
        ctx->conns << QObject::connect(sd, &StelSkyDrawer::lightPollutionLuminanceChanged,
                                       [ctx](double) { ctx->lightPollutionNotify++; });
        ctx->conns << QObject::connect(sd, &StelSkyDrawer::customStarMagLimitChanged,
                                       [ctx](double) { ctx->magLimitNotify++; });
        ctx->conns << QObject::connect(mm, &StelMovementMgr::currentFovChanged,
                                       [ctx](double) { ctx->fovNotify++; });
        ctx->conns << QObject::connect(core, &StelCore::currentProjectionTypeKeyChanged,
                                       [ctx](const QString &) { ctx->projKeyNotify++; });
    }

    auto steps = std::make_shared<QVector<Step>>();
    auto idx = std::make_shared<int>(0);

    auto tick = std::make_shared<std::function<void()>>();
    *tick = [ctx, steps, idx, tick]() {
        if (*idx >= steps->size())
        {
            ctx->finish();
            return;
        }
        const Step s = steps->at((*idx)++);
        ctx->nextDelay = s.delayAfter;
        s.body(ctx);
        QTimer::singleShot(ctx->nextDelay, ctx->app, [tick]() { (*tick)(); });
    };

    // ── 步骤 1：Q1/Q3/Q4 初值台账（写前必须先把初值留痕，否则收尾还原不了）──────
    steps->append({0, [](Ctx *c) {
        StelSkyDrawer *sd = StelApp::getInstance().getCore()->getSkyDrawer();
        StelMovementMgr *mm = StelApp::getInstance().getCore()->getMovementMgr();
        StelCore *core = StelApp::getInstance().getCore();

        c->initial.relStarScale = sd->getRelativeStarScale();
        c->initial.absStarScale = sd->getAbsoluteStarScale();
        c->initial.lightPollution = sd->getLightPollutionLuminance();
        c->initial.customMagLimit = sd->getCustomStarMagnitudeLimit();
        c->initial.flagMagLimit = sd->getFlagStarMagnitudeLimit();
        c->initial.fov = mm->getCurrentFov();
        c->initial.projectionKey = core->getCurrentProjectionTypeKey();

        c->note(QStringLiteral("Q1 初值（收尾会还原）：relativeStarScale=%1 absoluteStarScale=%2 "
                               "lightPollutionLuminance=%3 customStarMagLimit=%4")
                    .arg(c->initial.relStarScale)
                    .arg(c->initial.absStarScale)
                    .arg(c->initial.lightPollution)
                    .arg(c->initial.customMagLimit));
        c->note(QStringLiteral("Q1b flagStarMagnitudeLimit（星等限制开关）=%1；"
                               "limitMagnitude（**引擎按环境算出的**有效星等限，非用户值）=%2")
                    .arg(c->initial.flagMagLimit ? "true" : "false")
                    .arg(sd->getLimitMagnitude()));
        c->note(QStringLiteral("Q3 视场初值：currentFov=%1 minFov=%2 maxFov=%3 userMaxFov=%4 "
                               "aimFov=%5")
                    .arg(c->initial.fov)
                    .arg(mm->getMinFov())
                    .arg(mm->getMaxFov())
                    .arg(mm->getUserMaxFov())
                    .arg(mm->getAimFov()));
        c->note(QStringLiteral("Q4 投影初值：key=%1（中文名 %2）")
                    .arg(c->initial.projectionKey, core->getCurrentProjectionNameI18n()));
        c->note(QStringLiteral("Q4b 投影 key 全清单（getAllProjectionTypeKeys）共 %1 个：%2")
                    .arg(core->getAllProjectionTypeKeys().size())
                    .arg(core->getAllProjectionTypeKeys().join(QStringLiteral(", "))));
    }});

    // ── 步骤 2：Q1 写（每项写一个"远"值；读放下一步）─────────────────────────
    steps->append({300, [](Ctx *c) {
        for (const NumProp &p : numProps())
        {
            p.set(p.probeValue);
            c->note(QStringLiteral("Q1 写入 %1 := %2（读放下一步）")
                        .arg(QString::fromLatin1(p.name))
                        .arg(p.probeValue));
        }
        StelApp::getInstance().getCore()->getSkyDrawer()->setFlagStarMagnitudeLimit(true);
        c->note(QStringLiteral("Q1 写入 flagStarMagnitudeLimit := true（读放下一步）"));
    }});

    // ── 步骤 3：Q1 回读（夹取在这里暴露）────────────────────────────────────
    steps->append({200, [](Ctx *c) {
        int clampCount = 0;
        for (const NumProp &p : numProps())
        {
            const double after = p.get();
            const bool exact = qFuzzyCompare(after + 1.0, p.probeValue + 1.0);
            if (!exact)
                clampCount++;
            c->note(QStringLiteral("Q1 回读 %1 = %2（写入 %3）⇒ %4")
                        .arg(QString::fromLatin1(p.name))
                        .arg(after)
                        .arg(p.probeValue)
                        .arg(exact ? QStringLiteral("与写入一致（**setter 不夹取**）")
                                   : QStringLiteral("**被改写/夹取**")));
        }
        const bool flagAfter = StelApp::getInstance().getCore()->getSkyDrawer()->getFlagStarMagnitudeLimit();
        c->note(QStringLiteral("Q1 回读 flagStarMagnitudeLimit = %1")
                    .arg(flagAfter ? "true" : "false"));
        c->note(QStringLiteral("Q1c 小结：%1/4 项被引擎改写（不夹取 ⇒ 范围闸必须由产品侧承担）")
                    .arg(clampCount));
    }});

    // ── 步骤 3b：Q1d 星等限制的"真值在哪"（产品与判据都别读错量）─────────────
    // 🔴 实测：flag=true + custom=12.5 时 `getLimitMagnitude()` **仍是按环境算出的
    // 有效值**（白天 ≈ -4.44），**不反映用户设定**。用户设定的真值在
    // `getCustomStarMagnitudeLimit()` + `getFlagStarMagnitudeLimit()`；真实生效点是
    // `ZoneArray.cpp:449` 的星表人工截断。判据拿 getLimitMagnitude() 当"星等生效"
    // 的读数 ⇒ 结论必错（T38 要躲的"读错了量的名字"型假绿）。
    steps->append({400, [](Ctx *c) {
        StelSkyDrawer *sd = StelApp::getInstance().getCore()->getSkyDrawer();
        c->note(QStringLiteral("Q1d 语义验证：flag=true + custom=12.5 已写（读两个量放"
                               "下一步）"));
        sd->setFlagStarMagnitudeLimit(true);
        sd->setCustomStarMagnitudeLimit(12.5);
    }});

    steps->append({200, [](Ctx *c) {
        StelSkyDrawer *sd = StelApp::getInstance().getCore()->getSkyDrawer();
        c->note(QStringLiteral("Q1d flag=%1 customStarMagLimit=%2（**用户设定真值**）"
                               " vs getLimitMagnitude()=%3（**引擎按环境算的有效值**）"
                               " ⇒ 两者不等属正常；判据只认前一对，且视觉验证须先关大气让"
                               "星星可见")
                    .arg(sd->getFlagStarMagnitudeLimit() ? "true" : "false")
                    .arg(sd->getCustomStarMagnitudeLimit())
                    .arg(sd->getLimitMagnitude()));
    }});

    // ── 步骤 4：Q2 星等步进动作（写）────────────────────────────────────────
    steps->append({300, [](Ctx *c) {
        StelActionMgr *am = StelApp::getInstance().getStelActionManager();
        const double before = StelApp::getInstance().getCore()->getSkyDrawer()->getCustomStarMagnitudeLimit();
        c->note(QStringLiteral("Q2 步进前 customStarMagLimit = %1").arg(before));
        for (const char *id : {"actionShow_Stars_MagnitudeLimitIncrease",
                               "actionShow_Stars_MagnitudeLimitReduce"})
        {
            StelAction *a = am->findAction(QString::fromLatin1(id));
            c->note(QStringLiteral("Q2 %1 ⇒ %2")
                        .arg(QString::fromLatin1(id),
                             a ? QStringLiteral("已注册（text=\"%1\"）").arg(a->getText())
                               : QStringLiteral("**未注册**")));
        }
        // 全量开关（把 +0.1 的语义验成"真的动了 getter"）。
        StelAction *inc = am->findAction(QStringLiteral("actionShow_Stars_MagnitudeLimitIncrease"));
        if (inc)
            inc->trigger();
        c->note(QStringLiteral("Q2 已 trigger(Increase)（回读放下一步）"));
    }});

    // ── 步骤 5：Q2 回读 ─────────────────────────────────────────────────────
    steps->append({150, [](Ctx *c) {
        c->note(QStringLiteral("Q2 步进后 customStarMagLimit = %1（读走**模块 getter**，"
                               "不复述 StelAction）")
                    .arg(StelApp::getInstance().getCore()->getSkyDrawer()->getCustomStarMagnitudeLimit()));
    }});

    // ── 步骤 6：Q3 视场（写 60 度，读下一步）─────────────────────────────────
    steps->append({300, [](Ctx *c) {
        StelMovementMgr *mm = StelApp::getInstance().getCore()->getMovementMgr();
        mm->setFov(60.0);
        c->note(QStringLiteral("Q3 setFov(60)（读放下一步）"));
    }});

    // ── 步骤 7：Q3 回读 + 夹取边界（负数 / 超上限）────────────────────────────
    steps->append({200, [](Ctx *c) {
        StelMovementMgr *mm = StelApp::getInstance().getCore()->getMovementMgr();
        c->note(QStringLiteral("Q3 setFov(60) 后：currentFov=%1 aimFov=%2（min=%3 max=%4）")
                    .arg(mm->getCurrentFov())
                    .arg(mm->getAimFov())
                    .arg(mm->getMinFov())
                    .arg(mm->getMaxFov()));
        mm->setFov(-5.0);
        mm->setFov(1000.0);
        c->note(QStringLiteral("Q3 setFov(-5) 与 setFov(1000) 已发（夹取落点以下一步读数与 "
                               "min/max 比对）"));
    }});

    // ── 步骤 8：Q3 夹取读数 + Q4 投影逐 key 往返（写）────────────────────────
    steps->append({400, [](Ctx *c) {
        StelMovementMgr *mm = StelApp::getInstance().getCore()->getMovementMgr();
        c->note(QStringLiteral("Q3 夹取落点：currentFov=%1（对比 min=%2 max=%3 ⇒ 后发的 1000 "
                               "被夹到上限）")
                    .arg(mm->getCurrentFov())
                    .arg(mm->getMinFov())
                    .arg(mm->getMaxFov()));

        // Q4：逐 key 设一遍（读放下一步），顺带量 maxFov 随投影变化（updateMaximumFov）。
        StelCore *core = StelApp::getInstance().getCore();
        const QStringList keys = core->getAllProjectionTypeKeys();
        core->setCurrentProjectionTypeKey(QStringLiteral("ProjectionFisheye"));
        c->note(QStringLiteral("Q4 已设 ProjectionFisheye（读放下一步；清单 %1 项）")
                    .arg(keys.size()));
    }});

    // ── 步骤 9：Q4 投影逐 key 往返 ──────────────────────────────────────────
    steps->append({400, [](Ctx *c) {
        StelCore *core = StelApp::getInstance().getCore();
        const QStringList keys = core->getAllProjectionTypeKeys();
        const QString back = core->getCurrentProjectionTypeKey();
        c->note(QStringLiteral("Q4 设 Fisheye 后回读 key=%1（应一致）maxFov=%2（对照步骤 1 的初值，"
                               "可看出 updateMaximumFov 是否随投影变）")
                    .arg(back)
                    .arg(StelApp::getInstance().getCore()->getMovementMgr()->getMaxFov()));

        int mismatch = 0;
        for (const QString &k : keys)
        {
            core->setCurrentProjectionTypeKey(k);
            const QString got = core->getCurrentProjectionTypeKey();
            const bool ok = (got == k);
            if (!ok)
                mismatch++;
            c->note(QStringLiteral("Q4 key=%1 ⇒ 回读 %2 %3 中文名=\"%4\" maxFov=%5")
                        .arg(k, got, ok ? QStringLiteral("[一致]") : QStringLiteral("**[不一致]**"),
                             core->getCurrentProjectionNameI18n())
                        .arg(StelApp::getInstance().getCore()->getMovementMgr()->getMaxFov()));
        }
        c->note(QStringLiteral("Q4 小结：%1/%2 个 key 往返一致")
                    .arg(keys.size() - mismatch)
                    .arg(keys.size()));
    }});

    // ── 步骤 10：Q4b 非法 key 的落点（审计说会静默变 Stereographic）──────────
    steps->append({300, [](Ctx *c) {
        StelCore *core = StelApp::getInstance().getCore();
        core->setCurrentProjectionTypeKey(QStringLiteral("Nonsense_Key_T38"));
        c->note(QStringLiteral("Q4b 已设非法 key \"Nonsense_Key_T38\"（回读放下一步）"));
    }});

    // ── 步骤 11：Q4b 回读 + Q5 计数 + 收尾还原 ──────────────────────────────
    steps->append({300, [](Ctx *c) {
        StelCore *core = StelApp::getInstance().getCore();
        const QString got = core->getCurrentProjectionTypeKey();
        c->note(QStringLiteral("Q4b 非法 key 落点：%1 %2")
                    .arg(got,
                         got == QStringLiteral("ProjectionStereographic")
                             ? QStringLiteral("= Stereographic ⇒ **引擎静默兜底**（产品侧必须自己闸门）")
                             : QStringLiteral("（**不是** Stereographic，与审计推断不同，需复查）")));

        // Q5：NOTIFY 计数。
        c->note(QStringLiteral("Q5 NOTIFY 计数（决定 QML 能否建依赖）："
                               "relativeStarScale=%1 absoluteStarScale=%2 lightPollution=%3 "
                               "customStarMagLimit=%4 currentFov=%5 projectionKey=%6")
                    .arg(c->relStarNotify)
                    .arg(c->absStarNotify)
                    .arg(c->lightPollutionNotify)
                    .arg(c->magLimitNotify)
                    .arg(c->fovNotify)
                    .arg(c->projKeyNotify));

        // 收尾还原（写成独立步骤，便于"还原失败"时一眼看出）。
        StelSkyDrawer *sd = StelApp::getInstance().getCore()->getSkyDrawer();
        sd->setRelativeStarScale(c->initial.relStarScale);
        sd->setAbsoluteStarScale(c->initial.absStarScale);
        sd->setLightPollutionLuminance(c->initial.lightPollution);
        sd->setCustomStarMagnitudeLimit(c->initial.customMagLimit);
        sd->setFlagStarMagnitudeLimit(c->initial.flagMagLimit);
        StelApp::getInstance().getCore()->getMovementMgr()->setFov(c->initial.fov);
        core->setCurrentProjectionTypeKey(c->initial.projectionKey);
        c->note(QStringLiteral("收尾：已还原初值（回读放下一步核对）"));
    }});

    // ── 步骤 12：还原核对 + Q6 静置基线 ─────────────────────────────────────
    steps->append({1500, [](Ctx *c) {
        StelSkyDrawer *sd = StelApp::getInstance().getCore()->getSkyDrawer();
        StelMovementMgr *mm = StelApp::getInstance().getCore()->getMovementMgr();
        StelCore *core = StelApp::getInstance().getCore();
        c->note(QStringLiteral("收尾核对：relativeStarScale=%1（初值 %2）absoluteStarScale=%3"
                               "（初值 %4）lightPollution=%5（初值 %6）customStarMagLimit=%7"
                               "（初值 %8）flagMagLimit=%9（初值 %10）fov=%11（初值 %12）"
                               "projectionKey=%13（初值 %14）")
                    .arg(sd->getRelativeStarScale())
                    .arg(c->initial.relStarScale)
                    .arg(sd->getAbsoluteStarScale())
                    .arg(c->initial.absStarScale)
                    .arg(sd->getLightPollutionLuminance())
                    .arg(c->initial.lightPollution)
                    .arg(sd->getCustomStarMagnitudeLimit())
                    .arg(c->initial.customMagLimit)
                    .arg(sd->getFlagStarMagnitudeLimit() ? "true" : "false")
                    .arg(c->initial.flagMagLimit ? "true" : "false")
                    .arg(mm->getCurrentFov())
                    .arg(c->initial.fov)
                    .arg(core->getCurrentProjectionTypeKey(), c->initial.projectionKey));
        // Q6 基线：从这里开始"什么都不做"。
        c->idleBaseRel = c->relStarNotify;
        c->idleBaseFov = c->fovNotify;
        c->idleBaseProj = c->projKeyNotify;
        c->note(QStringLiteral("Q6 静置基线已记（rel=%1 fov=%2 proj=%3），下一步静置 1.5s 后"
                               "报增量")
                    .arg(c->idleBaseRel)
                    .arg(c->idleBaseFov)
                    .arg(c->idleBaseProj));
    }});

    // ── 步骤 13：Q6 静置期 NOTIFY 频率（**决定滑块能不能建绑定**）──────────
    // T15 铁律：每帧都变的连续量不许给 QML 建依赖（重算风暴）。`currentFov` 若在
    // 静置时仍持续 emit，滑块就必须**轮询**而不是绑定。
    steps->append({0, [](Ctx *c) {
        c->note(QStringLiteral("Q6 静置 1.5s 内 NOTIFY 增量：relativeStarScale=%1 "
                               "currentFov=%2 projectionKey=%3")
                    .arg(c->relStarNotify - c->idleBaseRel)
                    .arg(c->fovNotify - c->idleBaseFov)
                    .arg(c->projKeyNotify - c->idleBaseProj));
    }});

    (*tick)();
#endif
}

} // namespace stelapp
