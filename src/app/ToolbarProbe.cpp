/*
 * ToolbarProbe — 实现（T34-A 工具栏数据面探针）。设计与四个问题见头注。
 *
 * 驱动方式沿用 LocationProbe 的线性步骤表：每步 = (跑完后等多少 ms, 步骤体)。
 * ⚠️ delayAfter 语义 = "跑完**本步**之后的等待"（T33 陷阱 41）：写 0 时下一步与
 * 本步只隔一个事件循环。触发 StelAction 后要等一拍再回读。
 */
#include "app/ToolbarProbe.hpp"

#include "app/ActionRouter.hpp"
#include "app/AppFacade.hpp"

#include <QCoreApplication>
#include <QSet>
#include <QTimer>
#include <QVector>

#if defined(STELQUICK_HAS_ENGINE)
#include "ConstellationMgr.hpp"
#include "GridLinesMgr.hpp"
#include "LandscapeMgr.hpp"
#include "NebulaMgr.hpp"
#include "SolarSystem.hpp"
#include "StelActionMgr.hpp"
#include "StelApp.hpp"
#include "StelModuleMgr.hpp"
#endif

namespace stelapp {

namespace {

using ProbeResult = ToolbarProbe::Result;

#if defined(STELQUICK_HAS_ENGINE)

//! T34 工具栏候选开关（照桌面 Stellarium 底栏/View 对话框选）。
//! engineRead = **模块 getter 回读路径**（不经 StelAction/StelProperty —— 陷阱 43：
//! 对照量与被测量不能同源；action 的 checked 就是它连的那个属性，复述它是自洽）。
struct ToggleSpec
{
    const char *actionId;
    const char *label;                 //!< QML 按钮文案（中文）
    std::function<bool()> engineRead;  //!< 模块 getter（独立回读路径）
};

QVector<ToggleSpec> toolbarSpecs()
{
    return {
        {"actionShow_Constellation_Lines", "星座连线",
         [] { return GETSTELMODULE(ConstellationMgr)->getFlagLines(); }},
        {"actionShow_Constellation_Labels", "星座名称",
         [] { return GETSTELMODULE(ConstellationMgr)->getFlagLabels(); }},
        {"actionShow_Constellation_Art", "星座插图",
         [] { return GETSTELMODULE(ConstellationMgr)->getFlagArt(); }},
        {"actionShow_Equatorial_Grid", "赤道坐标网格",
         [] { return GETSTELMODULE(GridLinesMgr)->getFlagEquatorGrid(); }},
        {"actionShow_Azimuthal_Grid", "地平坐标网格",
         [] { return GETSTELMODULE(GridLinesMgr)->getFlagAzimuthalGrid(); }},
        {"actionShow_Ground", "地面",
         [] { return GETSTELMODULE(LandscapeMgr)->getFlagLandscape(); }},
        {"actionShow_Cardinal_Points", "方位角标记",
         [] { return GETSTELMODULE(LandscapeMgr)->getFlagCardinalPoints(); }},
        {"actionShow_Atmosphere", "大气",
         [] { return GETSTELMODULE(LandscapeMgr)->getFlagAtmosphere(); }},
        {"actionShow_Nebulas", "深空天体",
         [] { return GETSTELMODULE(NebulaMgr)->getFlagHints(); }},
        {"actionShow_Planets_Labels", "行星标签",
         [] { return GETSTELMODULE(SolarSystem)->getFlagLabels(); }},
        {"actionShow_Planets_Orbits", "行星轨道",
         [] { return GETSTELMODULE(SolarSystem)->getFlagOrbits(); }},
        {"actionShow_Night_Mode", "夜间模式",
         [] { return StelApp::getInstance().getVisionModeNight(); }},
    };
}

struct Ctx
{
    QCoreApplication *app = nullptr;
    AppFacade *facade = nullptr;
    ActionRouter *router = nullptr;
    std::function<void(const ProbeResult &)> onDone;
    ProbeResult result;
    int nextDelay = 0;

    QStringList lines;

    //! actionToggled 观测：探针起跑即连，Q3 期间统计发射。
    QMetaObject::Connection toggledConn;
    QSet<QString> toggledIds;
    int toggledCount = 0;

    void note(const QString &line) { lines.append(line); }

    void finish()
    {
        if (toggledConn)
            QObject::disconnect(toggledConn);
        result.ran = true;
        result.summary = QStringLiteral("工具栏数据面探针：只报读数、不下结论（断言见 T34-C 的 ToolbarCheck）");
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

void ToolbarProbe::run(QCoreApplication *app,
                       AppFacade *facade,
                       ActionRouter *router,
                       const std::function<void(const Result &)> &onDone,
                       int delayMs)
{
#if !defined(STELQUICK_HAS_ENGINE)
    Q_UNUSED(facade)
    Q_UNUSED(router)
    Q_UNUSED(delayMs)
    Result r;
    r.unavailable = true;
    r.summary = QStringLiteral("工具栏探针需要合流形态构建（STELQUICK_HAS_ENGINE 未定义）");
    QTimer::singleShot(0, app, [onDone, r]() { onDone(r); });
#else
    auto *ctx = new Ctx;
    ctx->app = app;
    ctx->facade = facade;
    ctx->router = router;
    ctx->onDone = onDone;

    // Q4 的观测从现在开始挂：动作注册在模块 init 里，此刻引擎已 boot ⇒ 已注册。
    // （3 参连接：Ctx 不是 QObject，不能当 context 对象；finish() 里会断开。）
    ctx->toggledConn = QObject::connect(
        StelApp::getInstance().getStelActionManager(), &StelActionMgr::actionToggled,
        [ctx](const QString &id, bool value) {
            ctx->toggledCount++;
            ctx->toggledIds.insert(id);
            Q_UNUSED(value)
        });

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
        // 延时在**步骤体跑完之后**才读：步骤体改写 nextDelay 就是为了改这一次的等待。
        QTimer::singleShot(ctx->nextDelay, ctx->app, [tick]() { (*tick)(); });
    };

    // ── 步骤 1：Q1 注册表规模 / Q2 候选清单逐项读数 ───────────────────────────
    steps->append({0, [](Ctx *c) {
        if (!StelApp::isInitialized())
        {
            c->note(QStringLiteral("引擎不可用（StelApp 未初始化）⇒ 探针无读数"));
            return;
        }
        StelActionMgr *mgr = StelApp::getInstance().getStelActionManager();
        const QList<StelAction *> all = mgr->getActionList();
        const QStringList groups = mgr->getGroupList();
        c->note(QStringLiteral("Q1 引擎动作注册表：共 %1 个动作 / %2 个分组")
                    .arg(all.size()).arg(groups.size()));

        const QVector<ToggleSpec> specs = toolbarSpecs();
        c->note(QStringLiteral("Q2 候选清单共 %1 项，逐项：").arg(specs.size()));
        for (const ToggleSpec &spec : specs)
        {
            const QString id = QString::fromLatin1(spec.actionId);
            StelAction *action = mgr->findAction(id);
            if (!action)
            {
                c->note(QStringLiteral("Q2 %1 [%2] ⇒ **未注册**（模块没 init？）")
                            .arg(id, QString::fromUtf8(spec.label)));
                continue;
            }
            // 模块 getter 的读数放在 Q3 的"写前/写后"里出（这里只看动作本体）。
            c->note(QStringLiteral("Q2 %1 [%2] ⇒ checkable=%3 checked=%4 text=\"%5\" key=\"%6\"")
                        .arg(id, QString::fromUtf8(spec.label))
                        .arg(action->isCheckable() ? "true" : "**false**")
                        .arg(action->isChecked() ? "true" : "false")
                        .arg(action->getText())
                        .arg(action->getShortcut().toString()));
        }

        // 透传路径本身：ActionRouter::trigger 对引擎动作 ID 应当可用（isAvailable）。
        c->note(QStringLiteral("Q2b ActionRouter.isAvailable（透传路径）逐项："));
        for (const ToggleSpec &spec : specs)
        {
            const QString id = QString::fromLatin1(spec.actionId);
            c->note(QStringLiteral("Q2b %1 ⇒ %2")
                        .arg(id, c->router && c->router->isAvailable(id)
                                     ? QStringLiteral("可用")
                                     : QStringLiteral("**不可用**")));
        }
    }});

    // ── 步骤 2：Q3 透传翻转（前 4 个代表性开关：写一次）────────────────────────
    // 判别性前提：写前值各不相同才翻得出方向 —— 不强求，只要"写后 ≠ 写前"。
    steps->append({300, [](Ctx *c) {
        if (!c->router)
            return;
        const QVector<ToggleSpec> specs = toolbarSpecs();
        for (int i = 0; i < qMin(4, specs.size()); ++i)
        {
            const QString id = QString::fromLatin1(specs.at(i).actionId);
            const bool before = specs.at(i).engineRead();
            const bool executed = c->router->trigger(id);
            c->note(QStringLiteral("Q3 trigger(%1) ⇒ executed=%2 写前 getter=%3（写后下一步读）")
                        .arg(id, executed ? "true" : "**false**",
                             before ? "true" : "false"));
        }
    }});

    // ── 步骤 3：Q3b 回读 + Q4 信号统计 ────────────────────────────────────────
    steps->append({0, [](Ctx *c) {
        const QVector<ToggleSpec> specs = toolbarSpecs();
        for (int i = 0; i < qMin(4, specs.size()); ++i)
        {
            const QString id = QString::fromLatin1(specs.at(i).actionId);
            const bool after = specs.at(i).engineRead();
            c->note(QStringLiteral("Q3b %1 写后 getter=%2（与上一步写前对照，应翻转）")
                        .arg(id, after ? "true" : "false"));
        }
        c->note(QStringLiteral("Q4 actionToggled 观测：共发射 %1 次，涉及 %2 个 id：%3")
                    .arg(c->toggledCount)
                    .arg(c->toggledIds.size())
                    .arg(QStringList(c->toggledIds.constBegin(), c->toggledIds.constEnd())
                             .join(QStringLiteral(", "))));
        if (c->facade)
            c->note(QStringLiteral("Q4b AppFacade.displayTogglesRevision = %1（若 >0 ⇒ "
                                   "Facade 订阅已生效）")
                        .arg(c->facade->displayTogglesRevision()));
    }});

    (*tick)();
#endif
}

} // namespace stelapp
