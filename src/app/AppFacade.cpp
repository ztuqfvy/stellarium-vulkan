/*
 * AppFacade — 实现（T15 最小切片）。
 *
 * 引擎依赖全部用 STELQUICK_HAS_ENGINE 守卫：独立工程形态（无引擎）编译为
 * no-op 版本，QML 命令栏可点但无引擎效果（currentFov=-1 可辨）。
 * 线程纪律与幂等语义见 AppFacade.hpp 头注。
 */
#include "app/AppFacade.hpp"

#include <QDebug>

#if defined(STELQUICK_HAS_ENGINE)
#include "StelApp.hpp"
#include "StelCore.hpp"
#include "StelMovementMgr.hpp"
#include "StelObject.hpp"
#include "StelObjectMgr.hpp"
#endif

namespace stelapp {

AppFacade::AppFacade(QObject *parent)
    : QObject(parent)
{
}

void AppFacade::attachSimControl(ISimPacing *sim)
{
    m_sim = sim;
}

bool AppFacade::movementReady() const
{
#if defined(STELQUICK_HAS_ENGINE)
    return StelApp::isInitialized();
#else
    return false;
#endif
}

double AppFacade::julianDay() const
{
#if defined(STELQUICK_HAS_ENGINE)
    if (!StelApp::isInitialized())
        return 0.0;
    return StelApp::getInstance().getCore()->getJD();
#else
    return 0.0;
#endif
}

double AppFacade::timeRate() const
{
    return m_timeRate;
}

void AppFacade::setTimeRate(double ratePerJulianDaySecond)
{
    if (ratePerJulianDaySecond == m_timeRate)
        return;   // 幂等闸
    m_timeRate = ratePerJulianDaySecond;
    if (m_sim)
        m_sim->setSimRate(ratePerJulianDaySecond);
    emit timeRateChanged(m_timeRate);
}

double AppFacade::fieldOfView() const
{
#if defined(STELQUICK_HAS_ENGINE)
    if (!StelApp::isInitialized())
        return -1.0;
    // 未在动画中时 getAimFov() 返回当前视场；动画中返回目标值——
    // 对"命令发出去了什么"的语义更稳，且 zoomIn/zoomOut 连续点击时
    // 不会读到动画中间值导致步进倍率漂移。
    return StelApp::getInstance().getCore()->getMovementMgr()->getAimFov();
#else
    return -1.0;
#endif
}

void AppFacade::setFieldOfView(double degrees)
{
#if defined(STELQUICK_HAS_ENGINE)
    if (!movementReady())
        return;
    StelApp::getInstance().getCore()->getMovementMgr()->zoomTo(degrees, 0.4f);
    emit fieldOfViewChanged(fieldOfView());
#endif
}

bool AppFacade::simulationPaused() const
{
    return m_simulationPaused;
}

void AppFacade::setSimulationPaused(bool paused)
{
    if (paused == m_simulationPaused)
        return;   // 幂等闸：防双触发（U-ACT-01 的第一道防线）
    m_simulationPaused = paused;
    if (m_sim)
        m_sim->setSimScale(paused ? 0.0 : 1.0);
    qDebug() << "AppFacade: simulationPaused =" << paused
             << (m_sim ? "" : "（无 ISimPacing——无引擎形态，命令未落地）");
    emit simulationPausedChanged(m_simulationPaused);
}

void AppFacade::zoomIn()
{
    const double fov = fieldOfView();
    if (fov <= 0.0)
        return;   // 引擎不可用或视场异常，安全 no-op
    setFieldOfView(fov * 0.8);
}

void AppFacade::zoomOut()
{
    const double fov = fieldOfView();
    if (fov <= 0.0)
        return;
    setFieldOfView(fov * 1.25);
}

// ── T17：搜索 / 选择 ─────────────────────────────────────────────────────────

int AppFacade::searchObjects(const QString &query, int maxItems)
{
    // 模型自己做引擎取数（含无引擎形态的安全降级），本类只转发并回一个"有几行"。
    m_search.search(query, maxItems);
    return m_search.count();
}

bool AppFacade::selectSearchResult(int row)
{
    const QString sid = m_search.stableIdAt(row);
    if (sid.isEmpty()) {
        // 行越界（QML 列表与模型短暂不同步时会走到这里）：归零而非沿用陈旧值。
        m_info.clear();
        return false;
    }
    return m_info.selectByStableId(sid);
}

bool AppFacade::selectByStableId(const QString &stableId)
{
    return m_info.selectByStableId(stableId);
}

void AppFacade::clearSelection()
{
    m_info.clearSelection();
}

// ── T18：定位 / 跟踪 ─────────────────────────────────────────────────────────

void AppFacade::setRefusal(const char *reason)
{
    const QString next = QString::fromLatin1((reason && *reason) ? reason : "ok");
    if (next == m_refusal)
        return;                       // 只在**确有变化**时通知（QML 绑定不要每帧刷新）
    m_refusal = next;
    emit lastLocateRefusalChanged();
}

QString AppFacade::locateRefusalText() const
{
    // 文案与判定分离：判定逻辑永远看 token（见头注），文案只服务 UI。
    if (m_refusal == QStringLiteral("no-selection"))
        return tr("未选中天体，无法定位");
    if (m_refusal == QStringLiteral("home-planet"))
        return tr("不能定位到当前观察地点所在的行星");
    if (m_refusal == QStringLiteral("engine-unavailable"))
        return tr("引擎未就绪，定位命令未生效");
    return QString();
}

bool AppFacade::isHomePlanet(const QString &objectEnglishName, const QString &locationPlanetName)
{
    // 引擎侧的原始判据（`SearchDialog.cpp:1468-1484`，多处 GUI 同一条）：
    //     if (newSelected[0]->getEnglishName() != core->getCurrentLocation().planetName)
    //         { mvmgr->moveToObject(...); mvmgr->setFlagTracking(true); }
    //     else GETSTELMODULE(StelObjectMgr)->unSelect();     // 不能指向"脚下"
    // 为什么用英文名比对而不是 ID：地点里的 planetName 就是行星的英文名
    // （StelLocation::planetName），这是引擎自己选定的比对口径，不要另立一套。
    if (objectEnglishName.isEmpty() || locationPlanetName.isEmpty())
        return false;   // 取不到名字时判"不是"，宁可漏判也不误拒
    return objectEnglishName == locationPlanetName;
}

bool AppFacade::isTracking() const
{
#if defined(STELQUICK_HAS_ENGINE)
    if (!StelApp::isInitialized())
        return false;
    StelCore *core = StelApp::getInstance().getCore();
    if (!core || !core->getMovementMgr())
        return false;
    // 合取真值，两个都与不得：
    //   ① 引擎标志 —— 但它在 unSelect() 之后**不会被清**。原因在引擎自己身上：
    //      StelMovementMgr::selectedObjectChange()（StelMovementMgr.cpp:757-766）
    //      整段包在 `if (objectMgr->getWasSelected())` 里，而 StelObjectMgr::unSelect()
    //      是**先** clear 掉 lastSelectedObjects **再** emit
    //      → 槽里看到的就是"没有选中"，于是那条 `setFlagTracking(false)` 永不执行。
    //   ② 确有选中 —— 因为 updateVisionVector() 的跟踪分支同样要求 getWasSelected()，
    //      没有选中时锁定根本不发生。
    // 只读其中一个都会谎报：读①会在取消选中后显示"跟踪中"，读②会把
    // "用户想要的跟踪"当成"已经跟踪"。UI 与自检都用这个合取值。
    return core->getMovementMgr()->getFlagTracking()
           && StelApp::getInstance().getStelObjectMgr().getWasSelected();
#else
    return false;
#endif
}

QString AppFacade::trackedName() const
{
#if defined(STELQUICK_HAS_ENGINE)
    if (!isTracking())
        return QString();
    const QList<StelObjectP> &sel = StelApp::getInstance().getStelObjectMgr().getSelectedObject();
    if (sel.isEmpty() || !sel.first())
        return QString();
    const QString n = sel.first()->getNameI18n();
    return n.isEmpty() ? sel.first()->getEnglishName() : n;
#else
    return QString();
#endif
}

bool AppFacade::locateSelected(bool track)
{
#if defined(STELQUICK_HAS_ENGINE)
    if (!StelApp::isInitialized()) {
        setRefusal("engine-unavailable");
        ++m_locateRefusedCount;
        return false;
    }
    StelCore *core = StelApp::getInstance().getCore();
    StelObjectMgr &objMgr = StelApp::getInstance().getStelObjectMgr();
    StelMovementMgr *mv = core ? core->getMovementMgr() : nullptr;
    if (!mv) {
        setRefusal("engine-unavailable");
        ++m_locateRefusedCount;
        return false;
    }

    const QList<StelObjectP> &sel = objMgr.getSelectedObject();
    if (sel.isEmpty() || !sel.first()) {
        // 没有选中就谈不上定位。**不要**在这里退化成"清跟踪"——调用方要区分
        // "关掉跟踪"和"想定位但没得定位"，前者走 setTracking(false)。
        setRefusal("no-selection");
        ++m_locateRefusedCount;
        return false;
    }
    const StelObjectP &obj = sel.first();

    if (isHomePlanet(obj->getEnglishName(),
                     core->getCurrentLocation().planetName)) {
        setRefusal("home-planet");
        ++m_locateRefusedCount;
        return false;
    }

    if (track) {
        // 引擎自带"移动 + 锁定"：setFlagTracking(true) 内部就会调
        // moveToObject(getAutoMoveDuration())（StelMovementMgr.cpp:1386-1404）。
        // 不自己再调一次 moveToObject——那会重启一次 1.5s 自动移动。
        mv->setFlagTracking(true);
    } else {
        // 只移动：先解锁定，避免 1.5s 自动移动与"每帧被跟踪分支覆盖"打架
        // （updateVisionVector 的 flagAutoMove 分支与 tracking 分支互斥——
        //  跟踪开着时自动移动结束后会被立刻拉回目标，但移动过程会被抢）。
        mv->setFlagTracking(false);
        mv->moveToObject(obj, mv->getAutoMoveDuration());
    }

    setRefusal("ok");
    ++m_locateCount;
    emit trackingChanged();
    return true;
#else
    Q_UNUSED(track);
    setRefusal("engine-unavailable");
    ++m_locateRefusedCount;
    return false;
#endif
}

bool AppFacade::setTracking(bool on)
{
    if (on)
        return locateSelected(true);   // 开启跟踪 = 定位 + 锁定（守卫完全一致）

#if defined(STELQUICK_HAS_ENGINE)
    if (!StelApp::isInitialized()) {
        setRefusal("engine-unavailable");
        return false;
    }
    StelCore *core = StelApp::getInstance().getCore();
    StelMovementMgr *mv = core ? core->getMovementMgr() : nullptr;
    if (!mv) {
        setRefusal("engine-unavailable");
        return false;
    }
    mv->setFlagTracking(false);
    setRefusal("ok");
    emit trackingChanged();
    return true;
#else
    setRefusal("engine-unavailable");
    return false;
#endif
}

} // namespace stelapp
