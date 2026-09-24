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
#include "StelUtils.hpp"

#include <QDate>
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
    // T19：读**仿真时钟**，不读 `getJD()`。
    // `getJD()`（JD.first）只在 `StelCore::updateTime()` 里从 simClock 同步一次，
    // 而 HostDriven 下的推进是在渲染回调里 `core->advanceSimClock(dt)` —— 两者
    // 之间隔着一个 update 调用。读 getJD() 等于读"上一帧的快照"，读 simClock
    // 才是 T16 确立的单一真源（且不依赖 update 是否被调过）。
    return StelApp::getInstance().getCore()->getSimClockJD();
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

// ══════════════════════════════════════════════════════════════════════════
// T19「改时间」环
//
// 这一节的代码全部是**转发**，没有一行日历数学：
//   本地日历 → JD  : StelUtils::getJDFromDate
//   JD → 日历      : StelUtils::getDateTimeFromJulianDay   （返回的是 **UT**）
//   本地 ↔ UT      : StelCore::getUTCOffset(jd)（单位**小时**）
// 这不是洁癖。A-alpha 的原文要求是"区分 UTC、时区、显示历法；**不重新发明天文
// 时间算法**"。自己写一份儒略日公式出来，就会与引擎在 1582 年儒略/格里历切换、
// 闰年规则、ΔT 上各自漂移——而且漂得悄无声息（往返测试才会暴露）。
//
// 唯一的"自己的"逻辑是**范围校验**，且理由很具体：引擎的 getJDFromDate 在
// 1582-10-15 之前的分支**不做校验**（`if (test.isValid() && y>1582)` 不成立
// 就直接走 Numerical Recipes 的公式），13 月 32 日会得到一个"合法"的垃圾 JD。
// 所以这里用 `QDate::isValid` 提前挡——**与引擎自己用的判定同一个**（Qt 的日历）。
// ══════════════════════════════════════════════════════════════════════════

void AppFacade::setTimeRefusal(const char *reason)
{
    const QString next = (reason && *reason) ? QString::fromLatin1(reason)
                                             : QStringLiteral("ok");
    if (next != QStringLiteral("ok"))
        ++m_timeRefusedCount;             // 计数照旧：不因"值没变"而漏计一次拒绝
    if (next == m_timeRefusal)
        return;                           // 只在**确有变化**时通知（与 T18 的 setRefusal 同纪律）
    m_timeRefusal = next;
    emit lastTimeRefusalChanged();
}

QString AppFacade::formatJd(double jd, bool local) const
{
#if defined(STELQUICK_HAS_ENGINE)
    if (!StelApp::isInitialized())
        return QString();
    StelCore *core = StelApp::getInstance().getCore();
    if (!core)
        return QString();
    int y = 0, mo = 0, d = 0, h = 0, mi = 0, s = 0;
    if (local)
    {
        // 与旧 DateTimeDialog::setDateTime 同一口径：**读**本地日历要 **+** 偏移。
        jd += core->getUTCOffset(jd) / 24.0;
    }
    StelUtils::getDateTimeFromJulianDay(jd, &y, &mo, &d, &h, &mi, &s);
    // 引擎对 BC 年份的内部约定是 `year <= 0 ? year-1 : year`（见 getUTCOffset），
    // 显示时还原成人类写法（0 年不显示，标成 -1 BC 反而更绕）；这里只回显原始值。
    return QStringLiteral("%1-%2-%3 %4:%5:%6")
        .arg(y, 4, 10, QLatin1Char('0'))
        .arg(mo, 2, 10, QLatin1Char('0'))
        .arg(d, 2, 10, QLatin1Char('0'))
        .arg(h, 2, 10, QLatin1Char('0'))
        .arg(mi, 2, 10, QLatin1Char('0'))
        .arg(s, 2, 10, QLatin1Char('0'));
#else
    Q_UNUSED(jd);
    Q_UNUSED(local);
    return QString();
#endif
}

double AppFacade::utcOffsetHours() const
{
#if defined(STELQUICK_HAS_ENGINE)
    if (!StelApp::isInitialized())
        return 0.0;
    StelCore *core = StelApp::getInstance().getCore();
    if (!core)
        return 0.0;
    return core->getUTCOffset(core->getSimClockJD());
#else
    return 0.0;
#endif
}

QString AppFacade::localDateTimeText() const
{
    return formatJd(julianDay(), true);
}

QString AppFacade::utcDateTimeText() const
{
    return formatJd(julianDay(), false);
}

int AppFacade::localDateTimeField(int which) const
{
#if defined(STELQUICK_HAS_ENGINE)
    if (!StelApp::isInitialized())
        return 0;
    StelCore *core = StelApp::getInstance().getCore();
    if (!core)
        return 0;
    const double jd = core->getSimClockJD();
    int y = 0, mo = 0, d = 0, h = 0, mi = 0, s = 0;
    StelUtils::getDateTimeFromJulianDay(jd + core->getUTCOffset(jd) / 24.0,
                                       &y, &mo, &d, &h, &mi, &s);
    switch (which)
    {
    case 0: return y;
    case 1: return mo;
    case 2: return d;
    case 3: return h;
    case 4: return mi;
    case 5: return s;
    default: return 0;
    }
#else
    Q_UNUSED(which);
    return 0;
#endif
}

bool AppFacade::setJulianDay(double jd)
{
#if defined(STELQUICK_HAS_ENGINE)
    if (!StelApp::isInitialized())
    {
        setTimeRefusal("engine-unavailable");
        return false;
    }
    StelCore *core = StelApp::getInstance().getCore();
    if (!core)
    {
        setTimeRefusal("engine-unavailable");
        return false;
    }
    // 引擎在 StelCore::updateTime 里把 JD 钳到 [-71328212.5, 74769924.499988]
    // （防天文算法在极端值上炸）。越界时**提前拒绝**，好过"写进去、下一帧被
    // 悄悄改掉"——后者会让 UI 显示一个引擎并不认可的值，且没人知道为什么。
    if (!(jd >= -71328212.500012 && jd <= 74769924.499988))
    {
        setTimeRefusal("out-of-range");
        return false;
    }
    core->setJD(jd);          // T16 的唯一跳转入口 → simClock.jumpTo(jd)
    setTimeRefusal("ok");
    ++m_timeWriteCount;
    return true;
#else
    Q_UNUSED(jd);
    setTimeRefusal("engine-unavailable");
    return false;
#endif
}

bool AppFacade::setLocalDateTime(int y, int m, int d, int h, int min, int s)
{
#if defined(STELQUICK_HAS_ENGINE)
    if (!StelApp::isInitialized())
    {
        setTimeRefusal("engine-unavailable");
        return false;
    }
    StelCore *core = StelApp::getInstance().getCore();
    if (!core)
    {
        setTimeRefusal("engine-unavailable");
        return false;
    }
    // ① 范围校验。QDate 的 y<=0 口径与引擎 getJDFromDate 内部一致（`y <= 0 ? y-1 : y`，
    //    因为 QDate 没有 0 年）。时刻字段单独挡。
    const QDate probe(y <= 0 ? y - 1 : y, m, d);
    if (!probe.isValid() || h < 0 || h > 23 || min < 0 || min > 59 || s < 0 || s > 60)
    {
        // 注意：拒绝**不能有副作用** —— 时钟一个字节都不动（TimeCheck TC-05 钉这条）。
        setTimeRefusal("invalid-date");
        return false;
    }
    double cjd = 0.0;
    if (!StelUtils::getJDFromDate(&cjd, y, m, d, h, min, static_cast<float>(s)))
    {
        setTimeRefusal("invalid-date");
        return false;
    }
    // ② 本地 → UT。**与旧 DateTimeDialog::newJd() 逐位一致**：
    //        cjd -= core->getUTCOffset(cjd) / 24.0;
    //    偏移取在**校正前**的 cjd 上（不是校正后的）—— 这是旧口径，照抄不"改进"。
    //    两处差 1 小时以内的时区偏移只在 DST 边界日不同，此处不为它引入新的不兼容。
    cjd -= core->getUTCOffset(cjd) / 24.0;
    return setJulianDay(cjd);
#else
    Q_UNUSED(y); Q_UNUSED(m); Q_UNUSED(d);
    Q_UNUSED(h); Q_UNUSED(min); Q_UNUSED(s);
    setTimeRefusal("engine-unavailable");
    return false;
#endif
}

void AppFacade::setTimeNow()
{
#if defined(STELQUICK_HAS_ENGINE)
    if (!StelApp::isInitialized())
    {
        setTimeRefusal("engine-unavailable");
        return;
    }
    StelCore *core = StelApp::getInstance().getCore();
    if (!core)
    {
        setTimeRefusal("engine-unavailable");
        return;
    }
    // 引擎既有实现（`setJD(getJDFromSystem())`），不重造。"现在"是绝对时刻，
    // 因此**不套**时区偏移 —— 套了就变成"把本地钟面当 UT"，差一个时区。
    core->setTimeNow();
    setTimeRefusal("ok");
    ++m_timeWriteCount;
#else
    setTimeRefusal("engine-unavailable");
#endif
}

QString AppFacade::timeRefusalText() const
{
    if (m_timeRefusal == QStringLiteral("ok"))
        return QString();
    if (m_timeRefusal == QStringLiteral("engine-unavailable"))
        return QStringLiteral("引擎未就绪，时间无法写入。");
    if (m_timeRefusal == QStringLiteral("invalid-date"))
        return QStringLiteral("日期/时间字段超出范围（如 13 月、32 日、25 时）——已忽略。");
    if (m_timeRefusal == QStringLiteral("out-of-range"))
        return QStringLiteral("该时刻超出引擎可表示范围——已忽略。");
    return QStringLiteral("时间写入失败（%1）。").arg(m_timeRefusal);
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
