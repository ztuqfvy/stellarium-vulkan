/*
 * ToolbarCheck — 实现（T34-C）。判据清单 / 与 T33 的差别 / 负控口径见头注。
 *
 * 驱动方式沿用 LocationCheck 的线性步骤表；`delayAfter` 语义 = "跑完本步之后
 * 的等待"（T33 陷阱 41）。本套判据读的是同步 bool，无变换栈 ⇒ 不需要就绪门；
 * 每步 300ms 只为让 actionToggled → revision 的 queued 信号先落地。
 */
#include "app/ToolbarCheck.hpp"

#include "app/ActionRouter.hpp"
#include "app/AppFacade.hpp"

#include <QCoreApplication>
#include <QMouseEvent>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSet>
#include <QTimer>
#include <QVector>
#include <algorithm>

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

using CheckResult = ToolbarCheck::Result;

#if defined(STELQUICK_HAS_ENGINE)

//! 与 Toolbar.qml 的 `toggles` 清单**同一份**（改一处必须改另一处 —— 两处读数串
//! 要同步改的教训见 TRAPS 41 的兄弟条）。TB-09 按这份清单逐个找 QML 按钮。
struct ToggleSpec
{
    const char *actionId;
    std::function<bool()> engineRead;  //!< 模块 getter（独立回读路径，陷阱 43）
    bool writeProbe = false;           //!< 是否入选写入腿（前 4 个）
};

QVector<ToggleSpec> toolbarSpecs()
{
    return {
        {"actionShow_Constellation_Lines",
         [] { return GETSTELMODULE(ConstellationMgr)->getFlagLines(); }, true},
        {"actionShow_Constellation_Labels",
         [] { return GETSTELMODULE(ConstellationMgr)->getFlagLabels(); }},
        {"actionShow_Constellation_Art",
         [] { return GETSTELMODULE(ConstellationMgr)->getFlagArt(); }},
        {"actionShow_Equatorial_Grid",
         [] { return GETSTELMODULE(GridLinesMgr)->getFlagEquatorGrid(); }, true},
        {"actionShow_Azimuthal_Grid",
         [] { return GETSTELMODULE(GridLinesMgr)->getFlagAzimuthalGrid(); }},
        {"actionShow_Ground",
         [] { return GETSTELMODULE(LandscapeMgr)->getFlagLandscape(); }, true},
        {"actionShow_Cardinal_Points",
         [] { return GETSTELMODULE(LandscapeMgr)->getFlagCardinalPoints(); }},
        {"actionShow_Atmosphere",
         [] { return GETSTELMODULE(LandscapeMgr)->getFlagAtmosphere(); }},
        {"actionShow_Nebulas",
         [] { return GETSTELMODULE(NebulaMgr)->getFlagHints(); }},
        {"actionShow_Planets_Labels",
         [] { return GETSTELMODULE(SolarSystem)->getFlagLabels(); }},
        {"actionShow_Planets_Orbits",
         [] { return GETSTELMODULE(SolarSystem)->getFlagOrbits(); }},
        {"actionShow_Night_Mode",
         [] { return StelApp::getInstance().getVisionModeNight(); }, true},
    };
}

struct Ctx
{
    QCoreApplication *app = nullptr;
    AppFacade *facade = nullptr;
    ActionRouter *router = nullptr;
    QQuickWindow *window = nullptr;
    std::function<void(const CheckResult &)> onDone;
    CheckResult result;
    int nextDelay = 0;
    bool ok = true;

    QMetaObject::Connection toggledConn;
    QSet<QString> toggledIds;   //!< 写入腿期间发射过 toggled 的 id
    int revisionBase = 0;
    //! 前提门失败 ⇒ 停止后续步骤（一条判据都不计，报 UNAVAILABLE）。
    bool stop = false;

    //! 写入腿 4 个目标的写前值（TB-02..04 翻转腿 / TB-05 复原腿的对照）。
    QVector<bool> beforeValues;
    //! TB-09 的逐按钮一致性（信息行用）。
    int uiConsistent = 0, uiMissing = 0;

    //! TB-12 的**双采样**（点击前/点击后各采一次按钮 engineOn 与引擎）。
    //! 🔴 为什么必须两次：绑定冻结时（REV_OFF/TOKEN_OFF 负控）engineOn 停在
    //! **delegate 创建时刻**的值（实测 = false，早于引擎配置装载）。单次点击
    //! 后引擎若恰好也是 false 就**撞车** ⇒ 负控白做（T34 实测：REV_OFF 下
    //! TB-12 一度假绿）。采两次（翻转 + 复原）时，一个冻结值不可能同时等于
    //! true 和 false 两个引擎态 ⇒ 必有一次不匹配 ⇒ 负控必红。
    bool tb12ClickMatched = false;
    bool tb12RestoreMatched = false;
    QString tb12ClickDetail;
    QString tb12RestoreDetail;

    void mark(bool cond, const QString &line)
    {
        ++result.total;
        if (cond)
        {
            ++result.passed;
            result.details.append(QStringLiteral("  ✓ %1").arg(line));
        }
        else
        {
            ok = false;
            result.details.append(QStringLiteral("  ✗ %1").arg(line));
        }
    }
    void note(const QString &line) { result.details.append(QStringLiteral("    %1").arg(line)); }

    int revision() const { return facade ? facade->displayTogglesRevision() : 0; }

    void finish()
    {
        if (toggledConn)
            QObject::disconnect(toggledConn);
        result.ran = true;
        result.pass = (result.passed == result.total) && result.total > 0;
        result.summary = QStringLiteral("判据 %1/%2  VERDICT=%3")
                             .arg(result.passed).arg(result.total)
                             .arg(result.pass ? "PASS" : "FAIL");
        onDone(result);
    }
};

//! 驱动步骤：跑完本步后等 delayAfter 毫秒进下一步。
struct Step
{
    int delayAfter = 0;
    std::function<void(Ctx *)> body;
};

//! 按钮中心在窗口坐标系里的位置（TB-10 的真实点击落点）。
QPointF itemCenterScene(QQuickItem *item)
{
    return item->mapToScene(QPointF(item->width() / 2.0, item->height() / 2.0));
}

//! 🔴 工具栏开关按钮**不能用 findChild 找**（T34 实测的仪器面陷阱）：
//! Repeater 的 delegate 只 `setParentItem(Flow)`，**QObject parent 是 null**
//! ⇒ `QObject::findChild` 沿 QObject 父链走，**永远扫不到它们**（导航按钮是
//! 静态声明的、QObject 父链正常，所以 findChild 对它们有效——同款 API 两种
//! 结果，极易误判成"按钮不存在"）。修法：先走 findChild（对静态按钮有效），
//! 未命中再定位 `toolbarToggleFlow`、**沿视觉树 childItems() 递归**找名字。
QQuickItem *findToggleItem(QQuickWindow *window, const QString &objectName)
{
    if (QQuickItem *direct = window->findChild<QQuickItem *>(objectName))
        return direct;
    QQuickItem *flow = window->findChild<QQuickItem *>(QStringLiteral("toolbarToggleFlow"));
    if (!flow)
        return nullptr;
    std::function<QQuickItem *(QQuickItem *)> walk =
        [&](QQuickItem *item) -> QQuickItem * {
        for (QQuickItem *child : item->childItems())
        {
            if (child->objectName() == objectName)
                return child;
            if (QQuickItem *hit = walk(child))
                return hit;
        }
        return nullptr;
    };
    return walk(flow);
}

//! 向窗口投递一次真实鼠标点击（AC-12 先例：最外层注入，不经控件的 click() 捷径）。
bool clickAt(QQuickWindow *window, const QPointF &scenePos)
{
    const QPoint global = window->mapToGlobal(scenePos.toPoint());
    QMouseEvent press(QEvent::MouseButtonPress, scenePos, scenePos,
                      QPointF(global), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QMouseEvent release(QEvent::MouseButtonRelease, scenePos, scenePos,
                        QPointF(global), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(window, &press);
    QCoreApplication::sendEvent(window, &release);
    return press.isAccepted() || release.isAccepted();
}

#endif  // STELQUICK_HAS_ENGINE

} // namespace

void ToolbarCheck::run(QCoreApplication *app,
                       AppFacade *facade,
                       ActionRouter *router,
                       QQuickWindow *window,
                       const std::function<void(const Result &)> &onDone,
                       int delayMs)
{
#if !defined(STELQUICK_HAS_ENGINE)
    Q_UNUSED(facade)
    Q_UNUSED(router)
    Q_UNUSED(window)
    Q_UNUSED(delayMs)
    Result r;
    r.unavailable = true;
    r.summary = QStringLiteral("工具栏自检需要合流形态构建（STELQUICK_HAS_ENGINE 未定义）");
    QTimer::singleShot(0, app, [onDone, r]() { onDone(r); });
#else
    auto *ctx = new Ctx;
    ctx->app = app;
    ctx->facade = facade;
    ctx->router = router;
    ctx->window = window;
    ctx->onDone = onDone;

    // 信号观测（TB-06）从现在开始挂：写入腿的每一次翻转都该汇到这里。
    ctx->toggledConn = QObject::connect(
        StelApp::getInstance().getStelActionManager(), &StelActionMgr::actionToggled,
        [ctx](const QString &id, bool) { ctx->toggledIds.insert(id); });

    auto steps = std::make_shared<QVector<Step>>();
    auto idx = std::make_shared<int>(0);

    auto tick = std::make_shared<std::function<void()>>();
    *tick = [ctx, steps, idx, tick]() {
        if (ctx->stop || *idx >= steps->size())
        {
            ctx->finish();
            return;
        }
        const Step s = steps->at((*idx)++);
        ctx->nextDelay = s.delayAfter;
        s.body(ctx);
        QTimer::singleShot(ctx->nextDelay, ctx->app, [tick]() { (*tick)(); });
    };

    // ── 步骤 1：前提门 + 写前快照（delayMs 后开跑，等引擎动作注册落定）──────────
    steps->append({delayMs, [](Ctx *c) {
        if (!StelApp::isInitialized())
        {
            c->result.unavailable = true;
            c->note(QStringLiteral("前提：引擎未初始化 ⇒ UNAVAILABLE"));
            c->stop = true;
            return;
        }
        StelActionMgr *mgr = StelApp::getInstance().getStelActionManager();
        const QVector<ToggleSpec> specs = toolbarSpecs();
        int present = 0, checkable = 0;
        for (const ToggleSpec &spec : specs)
        {
            if (const StelAction *a = mgr->findAction(QString::fromLatin1(spec.actionId)))
            {
                ++present;
                if (a->isCheckable())
                    ++checkable;
            }
        }
        c->note(QStringLiteral("前提：12 个候选动作 present=%1/12 checkable=%2/12")
                    .arg(present).arg(checkable));
        if (present != specs.size() || checkable != specs.size())
        {
            c->result.unavailable = true;
            c->note(QStringLiteral("前提不齐 ⇒ UNAVAILABLE（动作注册面缺失，判据无意义）"));
            c->stop = true;
            return;
        }
        c->revisionBase = c->revision();
        for (const ToggleSpec &spec : specs)
            if (spec.writeProbe)
                c->beforeValues.append(spec.engineRead());
        c->note(QStringLiteral("前提：revisionBase=%1 写前值 %2 个（%3/12 入写入腿）")
                    .arg(c->revisionBase)
                    .arg(c->beforeValues.size())
                    .arg(std::count_if(specs.cbegin(), specs.cend(),
                                       [](const ToggleSpec &s) { return s.writeProbe; })));
        {
            QStringList initVals;
            for (const ToggleSpec &spec : specs)
                if (spec.writeProbe)
                    initVals << QStringLiteral("%1=%2")
                                    .arg(QString::fromLatin1(spec.actionId)
                                             .section(QLatin1Char('_'), 2),
                                         spec.engineRead() ? "true" : "false");
            c->note(QStringLiteral("前提：boot 初值 %1").arg(initVals.join(QStringLiteral(", "))));
        }
    }});

    // ── 步骤 2：写入腿 —— 对 4 个代表开关各 trigger 一次 ─────────────────────
    steps->append({300, [](Ctx *c) {
        const QVector<ToggleSpec> specs = toolbarSpecs();
        for (const ToggleSpec &spec : specs)
        {
            if (!spec.writeProbe)
                continue;
            const QString id = QString::fromLatin1(spec.actionId);
            const bool executed = c->router->trigger(id);
            c->note(QStringLiteral("TB 写入：trigger(%1) ⇒ executed=%2")
                        .arg(id, executed ? "true" : "**false**"));
        }
    }});

    // ── 步骤 3：TB-01..05 读数 + 触发第二轮（复原）────────────────────────────
    steps->append({0, [](Ctx *c) {
        const QVector<ToggleSpec> specs = toolbarSpecs();
        int probeIdx = 0;
        int judgeNo = 0;
        for (const ToggleSpec &spec : specs)
        {
            if (!spec.writeProbe)
                continue;
            ++judgeNo;
            const QString id = QString::fromLatin1(spec.actionId);
            const bool now = spec.engineRead();
            const bool before = c->beforeValues.at(probeIdx);
            c->mark(now != before,
                    QStringLiteral("TB-0%1 写入生效：%2 getter %3 ⇒ %4（翻转）")
                        .arg(judgeNo).arg(id,
                                          before ? "true" : "false",
                                          now ? "true" : "false"));
            ++probeIdx;
        }

        // 第二轮：全部 trigger 回来（复原；TB-05 在下一步读）。
        for (const ToggleSpec &spec : specs)
        {
            if (!spec.writeProbe)
                continue;
            c->router->trigger(QString::fromLatin1(spec.actionId));
        }

        // TB-06 信号腿：4 个 id 各发射 ≥1 次（观测从 run() 起挂）。
        int fired = 0;
        QStringList missed;
        for (const ToggleSpec &spec : specs)
        {
            if (!spec.writeProbe)
                continue;
            const QString id = QString::fromLatin1(spec.actionId);
            if (c->toggledIds.contains(id))
                ++fired;
            else
                missed << id;
        }
        c->mark(fired == c->beforeValues.size(),
                QStringLiteral("TB-06 信号腿：actionToggled 发射 %1/%2 个 id%3")
                    .arg(fired).arg(c->beforeValues.size())
                    .arg(missed.isEmpty() ? QString()
                                          : QStringLiteral("（缺：%1）").arg(missed.join(","))));

        // TB-07 revision 腿：净增 ≥ 2×4（queued 信号 300ms 足够落地）。
        const int delta = c->revision() - c->revisionBase;
        c->mark(delta >= 2 * c->beforeValues.size(),
                QStringLiteral("TB-07 revision 腿：净增 %1 ≥ %2")
                    .arg(delta).arg(2 * c->beforeValues.size()));
    }});

    // ── 步骤 4：TB-05 往返复原 + TB-08/TB-11 负控 ────────────────────────────
    steps->append({300, [](Ctx *c) {
        const QVector<ToggleSpec> specs = toolbarSpecs();

        // TB-05 往返复原：4 个 getter 回到写前值。
        int restored = 0;
        int probeIdx = 0;
        for (const ToggleSpec &spec : specs)
        {
            if (!spec.writeProbe)
                continue;
            if (spec.engineRead() == c->beforeValues.at(probeIdx))
                ++restored;
            ++probeIdx;
        }
        c->mark(restored == c->beforeValues.size(),
                QStringLiteral("TB-05 往返复原：%1/%2 回到写前值")
                    .arg(restored).arg(c->beforeValues.size()));

        // TB-08 负控：不存在的 ID ⇒ 拒绝 ∧ revision 不动 ∧ toggled 不发射。
        {
            const QString ghost = QStringLiteral("actionShow_Nonexistent_T34");
            const int revBefore = c->revision();
            const int toggleCountBefore = c->toggledIds.size();
            const bool executed = c->router->trigger(ghost);
            const bool clean = !executed && c->revision() == revBefore
                               && c->toggledIds.size() == toggleCountBefore;
            c->mark(clean,
                    QStringLiteral("TB-08 负控：trigger(%1) ⇒ executed=%2 revision Δ=%3 toggled Δ=%4")
                        .arg(ghost,
                             executed ? "true(**应拒绝**)" : "false",
                             QString::number(c->revision() - revBefore),
                             QString::number(c->toggledIds.size() - toggleCountBefore)));
        }

        // TB-11 判别负控：注册表命令不碰显示开关 ⇒ revision 不动。
        // （没有这条，"revision 变了"读不出是"引擎翻转"在驱动还是"任何 trigger"。）
        {
            const int revBefore = c->revision();
            const bool executed = c->router->trigger(QStringLiteral("app.togglePause"));
            const int delta = c->revision() - revBefore;
            // 复原（连点两次 = 原状态）。
            c->router->trigger(QStringLiteral("app.togglePause"));
            c->mark(executed && delta == 0,
                    QStringLiteral("TB-11 判别负控：registry(app.togglePause) executed=%1 "
                                   "revision Δ=%2（必须为 0）")
                        .arg(executed ? "true" : "false").arg(delta));
        }
    }});

    // ── 步骤 5：TB-09 UI 腿（存在 ∧ 态一致）+ 真实点击第一轮 ──────────────────
    steps->append({300, [](Ctx *c) {
        const QVector<ToggleSpec> specs = toolbarSpecs();
        int found = 0, consistent = 0;
        QStringList problems;
        QQuickItem *clickBtn = nullptr;
        bool clickEngineBefore = false;
        for (const ToggleSpec &spec : specs)
        {
            const QString id = QString::fromLatin1(spec.actionId);
            QQuickItem *btn = findToggleItem(c->window,
                                             QStringLiteral("toolToggle_") + id);
            if (!btn)
            {
                problems << (id + QStringLiteral(":<按钮缺失>"));
                continue;
            }
            ++found;
            // uiLayoutReady 等价判断（照 main.cpp 的门：有正尺寸且可见）。
            if (!(btn->width() > 1.0 && btn->height() > 1.0 && btn->isVisible()))
            {
                problems << (id + QStringLiteral(":<未完成布局>"));
                continue;
            }
            const QVariant engineOnV = btn->property("engineOn");
            if (!engineOnV.isValid())
            {
                problems << (id + QStringLiteral(":<engineOn 属性缺失>"));
                continue;
            }
            const bool engineOn = engineOnV.toBool();
            const bool engineNow = spec.engineRead();
            if (engineOn != engineNow)
                problems << (id + QStringLiteral(":<engineOn=%1 引擎=%2>")
                                    .arg(engineOn ? "true" : "false",
                                         engineNow ? "true" : "false"));
            else
                ++consistent;
            // 点击腿选**初始为 false** 的开关（星座连线）：若绑定被冻结在
            // 创建时刻的值（pre-boot ⇒ false），点击翻成 true 后 stale=false
            // ≠ 引擎 true ⇒ 必红。选 Ground（初始 true）则 stale false 与点击后
            // 撞车 ⇒ 负控白做（T34 首轮实测，见交付文档）。
            if (id == QLatin1String("actionShow_Constellation_Lines"))
            {
                clickBtn = btn;
                clickEngineBefore = engineNow;
            }
        }
        c->uiConsistent = consistent;
        c->uiMissing = specs.size() - found;
        c->mark(found == specs.size() && consistent == specs.size(),
                QStringLiteral("TB-09 UI 腿：按钮 %1/12 找到、态一致 %2/12%3")
                    .arg(found).arg(consistent)
                    .arg(problems.isEmpty()
                             ? QString()
                             : QStringLiteral("（问题：%1）").arg(problems.join("; "))));

        // TB-10 第一轮：真实鼠标点击星座连线按钮（最外层注入，AC-12 先例）。
        if (clickBtn)
        {
            const int revBefore = c->revision();
            const bool clicked = clickAt(c->window, itemCenterScene(clickBtn));
            // 点击后立即读（不等下一步）：区分"点击没翻动引擎"与"翻了又被谁翻回"。
            const bool rightAfter = GETSTELMODULE(ConstellationMgr)->getFlagLines();
            // 落点诊断：childAt 告诉我们这个坐标处视觉树里到底是谁（仪器没接上
            // 的最快定位法——T31 先例"读数为负先给仪器加诊断"）。
            const QPointF center = itemCenterScene(clickBtn);
            // 🔴 布局自证（**不是** childAt）：childAt 在 ApplicationWindow 的
            // contentItem 上会**撒谎** —— 实测按钮明明在 (48,58)、点击也确实生效
            // （TB-10 绿、CLICK_OFF 下变红），childAt 仍一路返回根级
            // `ApplicationWindowContentControl`（返回的是 contentItem 自己，不是它
            // 的后代）⇒ 拿它当"点击有没有砸中按钮"的判据必误判（T34 实测）。
            // 改用**几何必要条件**：点击落点必须落在按钮**及其全部祖先**的矩形内
            // —— 事件投递的必要条件，且能抓住"Toolbar 高度 0"那类假绿
            //（h=0 ⇒ 祖先 contains(y) 假 ⇒ 这里必报不覆盖）。
            {
                bool covered = true;
                QStringList uncovered;
                for (QQuickItem *p = clickBtn; p; p = p->parentItem())
                {
                    const QPointF lp = p->mapFromScene(center);
                    if (!p->contains(lp))
                    {
                        covered = false;
                        uncovered << (p->objectName().isEmpty()
                                          ? QString::fromLatin1(p->metaObject()->className())
                                          : p->objectName());
                    }
                }
                c->note(QStringLiteral("TB-10 落点覆盖：covered=%1（未覆盖的祖先：%2）")
                            .arg(covered ? QStringLiteral("true") : QStringLiteral("**false**"),
                                 uncovered.isEmpty() ? QStringLiteral("<无>")
                                                     : uncovered.join(QStringLiteral(","))));
                c->note(QStringLiteral("TB-10 落点：center=(%1,%2) 按钮 scene(0,0)=(%3,%4) %5x%6")
                            .arg(QString::number(center.x(), 'f', 1),
                                 QString::number(center.y(), 'f', 1))
                            .arg(QString::number(clickBtn->mapToScene(QPointF(0, 0)).x(), 'f', 1),
                                 QString::number(clickBtn->mapToScene(QPointF(0, 0)).y(), 'f', 1))
                            .arg(clickBtn->width(), 0, 'f', 0)
                            .arg(clickBtn->height(), 0, 'f', 0));
            }
            // 父链轨迹：按钮自报家门（x/y/w/h 逐级向上）——区分"mapToScene 坐标
            // 错"与"按钮真在那、事件被别人吃掉"。
            {
                QStringList chain;
                for (QQuickItem *p = clickBtn; p; p = p->parentItem())
                    chain.prepend(QStringLiteral("%1(x=%2 y=%3 w=%4 h=%5)")
                                      .arg(p->objectName().isEmpty()
                                               ? QString::fromLatin1(p->metaObject()->className())
                                               : p->objectName())
                                      .arg(p->x(), 0, 'f', 0)
                                      .arg(p->y(), 0, 'f', 0)
                                      .arg(p->width(), 0, 'f', 0)
                                      .arg(p->height(), 0, 'f', 0));
                chain.append(QStringLiteral("scene(0,0)=(%1,%2) btn w=%3 h=%4 visible=%5")
                                 .arg(QString::number(clickBtn->mapToScene(QPointF(0, 0)).x(), 'f', 1),
                                      QString::number(clickBtn->mapToScene(QPointF(0, 0)).y(), 'f', 1))
                                 .arg(clickBtn->width(), 0, 'f', 0)
                                 .arg(clickBtn->height(), 0, 'f', 0)
                                 .arg(clickBtn->isVisible() ? "true" : "false"));
                c->note(QStringLiteral("TB-10 父链：%1").arg(chain.join(QStringLiteral(" ← "))));
            }
            c->note(QStringLiteral("TB-10 第一轮点击：accepted=%1 点击前引擎=%2 点击后立即=%3 "
                                   "revisionBase=%4")
                        .arg(clicked ? "true" : "false")
                        .arg(clickEngineBefore ? "true" : "false")
                        .arg(rightAfter ? "true" : "false")
                        .arg(revBefore));
        }
        else
        {
            c->note(QStringLiteral("TB-10 星座连线按钮缺失 ⇒ 本条将判红（仪器没接上，不洗）"));
        }
    }});

    // ── 步骤 6：TB-10 读数 + 第二轮点击（复原）────────────────────────────────
    steps->append({300, [](Ctx *c) {
        // TB-10 断言"点击 ⇒ 引擎状态翻转"。TB-05 已把 Ground 还原到写前值，
        // 所以这里的现值与写前值**不同**即为翻转成功（对照 = 写前快照，单一口径）。
        int linesIdx = -1, k = 0;
        const QVector<ToggleSpec> specs = toolbarSpecs();
        for (const ToggleSpec &spec : specs)
        {
            if (!spec.writeProbe)
                continue;
            if (QLatin1String(spec.actionId) == QLatin1String("actionShow_Constellation_Lines"))
                linesIdx = k;
            ++k;
        }
        const bool before = (linesIdx >= 0) ? c->beforeValues.at(linesIdx) : false;
        const bool now = GETSTELMODULE(ConstellationMgr)->getFlagLines();
        c->mark(now != before,
                QStringLiteral("TB-10 UI 点击腿：真实点击星座连线 ⇒ 引擎 %1 ⇒ %2（翻转）")
                    .arg(before ? "true" : "false", now ? "true" : "false"));

        // TB-12 采样①（点击后）：按钮 engineOn 必须跟随引擎。这是 QML 绑定铁律的
        // **直接判据**：`engineOn` 绑定里必须真的读过 `displayTogglesRevision` ——
        // 若有人改成只调 `actionChecked()`（不读 token），引擎翻转后这个属性
        // **永远停在首帧**。断言在步骤 7 合成（需两次采样，见 Ctx::tb12* 注释）。
        {
            QQuickItem *btn = findToggleItem(
                c->window, QStringLiteral("toolToggle_actionShow_Constellation_Lines"));
            const QVariant engineOnV = btn ? btn->property("engineOn") : QVariant();
            const bool engineOn = engineOnV.isValid() ? engineOnV.toBool() : !now;
            c->tb12ClickMatched = engineOnV.isValid() && (engineOn == now);
            c->tb12ClickDetail =
                QStringLiteral("点击后 engineOn=%1 vs 引擎=%2")
                    .arg(engineOnV.isValid() ? (engineOn ? "true" : "false")
                                             : QStringLiteral("<缺失>"),
                         now ? "true" : "false");
        }

        // 第二轮点击：复原（TB-12 采样②在下一步）。
        QQuickItem *btn = findToggleItem(
            c->window, QStringLiteral("toolToggle_actionShow_Constellation_Lines"));
        if (btn)
            clickAt(c->window, itemCenterScene(btn));
    }});

    // ── 步骤 7：复原读数 + TB-12 双采样合成 + 收尾 ───────────────────────────
    steps->append({0, [](Ctx *c) {
        const bool now = GETSTELMODULE(ConstellationMgr)->getFlagLines();
        int linesIdx = -1, k = 0;
        const QVector<ToggleSpec> specs = toolbarSpecs();
        for (const ToggleSpec &spec : specs)
        {
            if (!spec.writeProbe)
                continue;
            if (QLatin1String(spec.actionId) == QLatin1String("actionShow_Constellation_Lines"))
                linesIdx = k;
            ++k;
        }
        const bool before = (linesIdx >= 0) ? c->beforeValues.at(linesIdx) : false;
        c->note(QStringLiteral("收尾：星座连线 引擎=%1（写前=%2）⇒ %3")
                    .arg(now ? "true" : "false", before ? "true" : "false",
                         now == before ? QStringLiteral("已复原")
                                       : QStringLiteral("**未复原（污染后续读数）**")));

        // TB-12 采样②（复原后）+ 合成。两次采样都匹配才绿 —— 单次采样会被
        // "冻结值恰好撞车"骗过（REV_OFF 实测假绿），两次则不可能同时撞车。
        {
            QQuickItem *btn = findToggleItem(
                c->window, QStringLiteral("toolToggle_actionShow_Constellation_Lines"));
            const QVariant engineOnV = btn ? btn->property("engineOn") : QVariant();
            const bool engineOn = engineOnV.isValid() ? engineOnV.toBool() : !now;
            c->tb12RestoreMatched = engineOnV.isValid() && (engineOn == now);
            c->tb12RestoreDetail =
                QStringLiteral("复原后 engineOn=%1 vs 引擎=%2")
                    .arg(engineOnV.isValid() ? (engineOn ? "true" : "false")
                                             : QStringLiteral("<缺失>"),
                         now ? "true" : "false");
            const bool allOk = c->tb12ClickMatched && c->tb12RestoreMatched;
            c->mark(allOk,
                    QStringLiteral("TB-12 绑定重算腿：%1；%2%3")
                        .arg(c->tb12ClickDetail, c->tb12RestoreDetail,
                             allOk ? QString()
                                   : QStringLiteral("（**绑定没重算**——token 没读/订阅没连；"
                                                    "负控 REV_OFF/TOKEN_OFF 必红）")));
        }
    }});

    (*tick)();
#endif
}

} // namespace stelapp
