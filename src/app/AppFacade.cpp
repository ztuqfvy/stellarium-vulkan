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

} // namespace stelapp
