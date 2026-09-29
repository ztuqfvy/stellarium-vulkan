/*
 * AppFacade — 实现（T15 最小切片）。
 *
 * 引擎依赖全部用 STELQUICK_HAS_ENGINE 守卫：独立工程形态（无引擎）编译为
 * no-op 版本，QML 命令栏可点但无引擎效果（currentFov=-1 可辨）。
 * 线程纪律与幂等语义见 AppFacade.hpp 头注。
 */
#include "app/AppFacade.hpp"

#include <QDebug>

#include <algorithm>
#include <cmath>

#if defined(STELQUICK_HAS_ENGINE)
#include "StelApp.hpp"
#include "StelCore.hpp"
#include "StelLocationMgr.hpp"   // T22：getAllTimezoneNames()（引擎真正接受的时区名单）
#include "StelMovementMgr.hpp"
#include "StelObject.hpp"
#include "StelObjectMgr.hpp"
#include "StelProjector.hpp"   // T27：skyEnginePos 读投影视口尺寸（比例映射基准）
#include "StelUtils.hpp"

#include <QDate>
#include <QStringList>
#include <QTimeZone>   // T22 时区选择器
#include <QWheelEvent> // T25 滚轮保真转发（wheelZoom 合成事件）
#include <QMouseEvent> // T27 鼠标保真转发（skyMouse* 合成事件）
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
    // 🔴 T22：读数必须来自**引擎**，不能用本地缓存。
    //
    // 缓存的问题（原实现 `return m_timeRate;`）：它只在 `setTimeRate()` 里更新，
    // 而引擎的速率动作走 `StelCore::increaseTimeSpeed()` 等，**不经过本类**
    // ⇒ 用户按 `L` 加速，引擎变了、UI 不动。这是"仪表没接在实况上"。
    //
    // ISimPacing::simRate() 的实现就是 `core->getTimeRate()`（LiveSkyRuntime.cpp:302），
    // 所以走它就是走引擎的 timeSpeed（T16 定案的"速率留在引擎"）。
    // 无 ISimPacing（无引擎形态）才回退缓存值。
    if (m_sim)
        return m_sim->simRate();
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

// ══════════════════════════════════════════════════════════════════════════
// T22 时间页收尾：MJD / 显示历法 / 时区 / 速率
//
// 与 T19 同一纪律：**只转发与照抄，不新发明天文/日历算法**。
// 每一条都在注释里给出旧界面的出处行号，便于回头核对。
// ══════════════════════════════════════════════════════════════════════════

double AppFacade::modifiedJulianDay() const
{
    // 从**真源**导出。禁用 `core->getMJDay()` 的理由见头注：
    // 它读 `JD.first`（上一帧快照），会把 T19 已经修掉的坑从 JD 挪到 MJD 上。
    return julianDay() - 2400000.5;
}

bool AppFacade::setModifiedJulianDay(double mjd)
{
    // 绝对写法。MJD 与 JD 只差常数 2400000.5（StelCore.cpp:1275/1280），
    // 所以**复用** setJulianDay 的范围校验与 token —— 写入路径仍然只有一条。
    // （旧界面 DateTimeDialog::mjdChanged 是增量写法 `applyJD(jd + delta)`，
    //   与绝对写法等价；这里取绝对写法是为了让判据能断言"位移精确等于 Δ"。）
    return setJulianDay(mjd + 2400000.5);
}

QString AppFacade::dateCalendarToken() const
{
#if defined(STELQUICK_HAS_ENGINE)
    if (!StelApp::isInitialized())
        return QStringLiteral("unknown");
    // 照抄 DateTimeDialog.cpp:251（`if (jd < 2299161) → 儒略历`），
    // 该字面量即 StelUtils.cpp:848 的 JD_GREG_CAL —— 引擎的日期换算内部就用它。
    // 判据用真源 julianDay()，不用 getJD()（快照），否则换历那一刻会晚一帧显示。
    return julianDay() < 2299161.0 ? QStringLiteral("julian")
                                   : QStringLiteral("gregorian");
#else
    return QStringLiteral("unknown");
#endif
}

QString AppFacade::dateCalendarText() const
{
    const QString t = dateCalendarToken();
    if (t == QStringLiteral("julian"))
        return QStringLiteral("儒略历");
    if (t == QStringLiteral("gregorian"))
        return QStringLiteral("格里高利历");
    return QStringLiteral("—");
}

QString AppFacade::timeZoneId() const
{
#if defined(STELQUICK_HAS_ENGINE)
    if (!StelApp::isInitialized())
        return QString();
    StelCore *core = StelApp::getInstance().getCore();
    return core ? core->getCurrentTimeZone() : QString();
#else
    return QString();
#endif
}

bool AppFacade::useCustomTimeZone() const
{
#if defined(STELQUICK_HAS_ENGINE)
    if (!StelApp::isInitialized())
        return false;
    StelCore *core = StelApp::getInstance().getCore();
    return core ? core->getUseCustomTimeZone() : false;
#else
    return false;
#endif
}

bool AppFacade::useDST() const
{
#if defined(STELQUICK_HAS_ENGINE)
    if (!StelApp::isInitialized())
        return false;
    StelCore *core = StelApp::getInstance().getCore();
    return core ? core->getUseDST() : false;
#else
    return false;
#endif
}

QStringList AppFacade::availableTimeZoneIds() const
{
#if defined(STELQUICK_HAS_ENGINE)
    // ⚠️ 这里刻意**不是**简单照抄 `LocationDialog::populateTimeZonesList`
    // （它用 `QTimeZone::availableTimeZoneIds()` 一把梭）。原因是引擎侧有两个
    // 静默失败在等着：
    //   ① `StelCore::setCurrentTimeZone()` 只接受
    //      `StelLocationMgr::getAllTimezoneNames()` 里的名字（StelCore.cpp:1717），
    //      不在名单里的**只打一条 qWarning 就不设置**（StelCore.cpp:1724）——
    //      "选了没用"。
    //   ② 名字若不能被 `QTimeZone` 解析，`getUTCOffset` 会**悄悄落回系统本地时区**
    //      （StelCore.cpp:1637 的 `!tzValid` 分支），连警告都不一定显眼 ——
    //      "选了个偏 8 小时的时区，结果一直是本机时区"。
    // 所以取**交集**：引擎接受的 ∩ Qt 能算的。两个坑都堵死。
    if (m_tzCache.isEmpty())
    {
        const QStringList engineNames =
            StelApp::isInitialized()
                ? StelApp::getInstance().getLocationMgr().getAllTimezoneNames()
                : QStringList();
        for (const QString &tz : engineNames)
        {
            if (QTimeZone(tz.toUtf8()).isValid())
                m_tzCache.append(tz);
        }
        // 无引擎/名单为空时退回 Qt 全集（至少控件可用；写入侧仍有回读验证兜底）。
        if (m_tzCache.isEmpty())
        {
            const QList<QByteArray> ids = QTimeZone::availableTimeZoneIds();
            for (const QByteArray &b : ids)
                m_tzCache.append(QString::fromLatin1(b));
        }
        std::sort(m_tzCache.begin(), m_tzCache.end());
    }
    return m_tzCache;
#else
    return QStringList();
#endif
}

bool AppFacade::setTimeZoneId(const QString &tz)
{
#if defined(STELQUICK_HAS_ENGINE)
    if (!StelApp::isInitialized())
        return false;
    StelCore *core = StelApp::getInstance().getCore();
    if (!core)
        return false;
    // 第一道：Qt 能不能解析（不能的话 getUTCOffset 会落回系统时区）。
    if (!QTimeZone(tz.toUtf8()).isValid())
        return false;
    const QString before = core->getCurrentTimeZone();
    core->setCurrentTimeZone(tz);
    // 第二道（关键）：**回读验证**。引擎对不在 `getAllTimezoneNames()` 里的名字
    // 是"打印警告 + 什么都不做"（StelCore.cpp:1717-1725），而它的名单来自地点库
    // 且经过 sanitize，与 Qt 的 id 集未必逐字一致。只看 QTimeZone::isValid() 就
    // 返回 true，等于向 UI 谎报"已切换"——比不切换更坏。
    if (core->getCurrentTimeZone() != tz)
    {
        Q_UNUSED(before);
        return false;
    }
    ++m_timeZoneWriteCount;
    return true;
#else
    Q_UNUSED(tz);
    return false;
#endif
}

void AppFacade::setUseCustomTimeZone(bool on)
{
#if defined(STELQUICK_HAS_ENGINE)
    if (!StelApp::isInitialized())
        return;
    if (StelCore *core = StelApp::getInstance().getCore())
        core->setUseCustomTimeZone(on);
#else
    Q_UNUSED(on);
#endif
}

void AppFacade::setUseDST(bool on)
{
#if defined(STELQUICK_HAS_ENGINE)
    if (!StelApp::isInitialized())
        return;
    if (StelCore *core = StelApp::getInstance().getCore())
        core->setUseDST(on);
#else
    Q_UNUSED(on);
#endif
}

QString AppFacade::timeRateText() const
{
#if defined(STELQUICK_HAS_ENGINE)
    // 换算与单位跳档**照抄** StelGuiItems.cpp:885-907，一档不改：
    //   factor = |rate| / JD_SECOND           → 速率倍数（秒/秒）
    //   初始单位 min/s，value = factor/60；≥60 → hr/s；再 ≥24 → d/s；再 ≥365.25 → yr/s
    //   倍数 ≤60 时旧界面只显示 `x<倍数>`，>60 才带括号里的换算值。
    const double factor = std::abs(timeRate()) / StelCore::JD_SECOND;
    double value = factor / 60.0;
    QString unit = QStringLiteral("分/秒");
    if (value >= 60.0)   { value /= 60.0;     unit = QStringLiteral("时/秒"); }
    if (value >= 24.0)   { value /= 24.0;     unit = QStringLiteral("天/秒"); }
    if (value >= 365.25) { value /= 365.25;   unit = QStringLiteral("年/秒"); }

    if (factor <= 60.0)
        return QStringLiteral("x%1").arg(QString::number(factor, 'f', 1));
    return QStringLiteral("x%1（%2 %3）")
        .arg(QString::number(factor, 'f', 0),
             QString::number(value, 'f', 2), unit);
#else
    return QStringLiteral("—");
#endif
}

QString AppFacade::timeDirection() const
{
    const double r = timeRate();
    if (r == 0.0)
        return QStringLiteral("stopped");
    return r < 0.0 ? QStringLiteral("backward") : QStringLiteral("forward");
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

void AppFacade::wheelZoom(int dx, int dy, int modifiers)
{
#if defined(STELQUICK_HAS_ENGINE)
    // T25：保真转发（语义见头注）。旧宿主（StelMainView::wheelEvent）就是这么干的：
    // 合成 QWheelEvent 直接交 StelApp::handleWheel，缩放/Ctrl+滚轮改时间等全部
    // modifiers 语义都由引擎各模块自己判定——本方法一行语义都不复刻。
    if (!StelApp::isInitialized())
        return;
    // 位置：视口中心。实测 StelMovementMgr::handleMouseWheel 只消费 angleDelta 与
    // modifiers、不消费坐标（缩放无平移耦合），中心点是安全的占位；保真转发
    // 保留的是 angleDelta/modifiers/buttons 语义。
    const QPointF center(640.0, 360.0);
    QWheelEvent wheel(center, center, QPoint(0, 0), QPoint(dx, dy),
                      Qt::NoButton, Qt::KeyboardModifiers(modifiers),
                      Qt::ScrollUpdate, false);
    StelApp::getInstance().handleWheel(&wheel);
#endif
}

// ── T27：天空页鼠标保真转发 ─────────────────────────────────────────────────

namespace {
//! 三个 skyMouse* 共用的坐标空间适配：QML 逻辑坐标 → 合成事件坐标。
//! 引擎投影视口 (wp,hp) 与 QML 窗口 (wq,hq) 在合流形态**不同构**（引擎渲染固定
//! renderSize 帧再缩放显示），按比例映射 + y 翻转（引擎 y 向上）；结果再除以
//! dppp——handleClick/handleMove 内部会乘回。等尺寸时退化为旧宿主 convertMouseEvent
//! 的 `h-1-y`。任一基准无效时返回 QNaN（调用方跳过）。
inline QPointF skyEnginePos(double x, double y, double wq, double hq)
{
    if (wq < 1.0 || hq < 1.0 || !StelApp::isInitialized())
        return QPointF(qQNaN(), qQNaN());
    StelCore *core = StelApp::getInstance().getCore();
    const StelProjectorP prj = core ? core->getProjection(StelCore::FrameJ2000) : nullptr;
    if (!prj || prj->getViewportWidth() < 1 || prj->getViewportHeight() < 1)
        return QPointF(qQNaN(), qQNaN());
    const double dppp = StelApp::getInstance().getDevicePixelsPerPixel();
    if (dppp <= 0.0)
        return QPointF(qQNaN(), qQNaN());
    return QPointF(x / wq * prj->getViewportWidth() / dppp,
                   (1.0 - y / hq) * prj->getViewportHeight() / dppp);
}
}  // namespace

void AppFacade::skyMousePress(double x, double y, double viewportWidth,
                              double viewportHeight, int button, int modifiers)
{
#if defined(STELQUICK_HAS_ENGINE)
    if (!StelApp::isInitialized())
        return;
    const QPointF pos = skyEnginePos(x, y, viewportWidth, viewportHeight);
    if (qIsNaN(pos.x()))
        return;
    const auto btn = static_cast<Qt::MouseButton>(button);
    QMouseEvent press(QEvent::MouseButtonPress, pos, pos, pos,
                      btn, btn, Qt::KeyboardModifiers(modifiers));
    StelApp::getInstance().handleClick(&press);
#else
    Q_UNUSED(x) Q_UNUSED(y) Q_UNUSED(viewportWidth) Q_UNUSED(viewportHeight)
    Q_UNUSED(button) Q_UNUSED(modifiers)
#endif
}

void AppFacade::skyMouseRelease(double x, double y, double viewportWidth,
                                double viewportHeight, int button, int modifiers)
{
#if defined(STELQUICK_HAS_ENGINE)
    if (!StelApp::isInitialized())
        return;
    const QPointF pos = skyEnginePos(x, y, viewportWidth, viewportHeight);
    if (qIsNaN(pos.x()))
        return;
    // buttons 在释放时刻为 NoButton；button 是**刚释放的那个键**——引擎用它
    // 区分左键（选中对象）与右键（反选）。
    QMouseEvent release(QEvent::MouseButtonRelease, pos, pos, pos,
                        static_cast<Qt::MouseButton>(button), Qt::NoButton,
                        Qt::KeyboardModifiers(modifiers));
    StelApp::getInstance().handleClick(&release);
#else
    Q_UNUSED(x) Q_UNUSED(y) Q_UNUSED(viewportWidth) Q_UNUSED(viewportHeight)
    Q_UNUSED(button) Q_UNUSED(modifiers)
#endif
}

void AppFacade::skyMouseMove(double x, double y, double viewportWidth,
                             double viewportHeight, int buttons)
{
#if defined(STELQUICK_HAS_ENGINE)
    if (!StelApp::isInitialized())
        return;
    const QPointF pos = skyEnginePos(x, y, viewportWidth, viewportHeight);
    if (qIsNaN(pos.x()))
        return;
    StelApp::getInstance().handleMove(pos.x(), pos.y(), Qt::MouseButtons(buttons));
#else
    Q_UNUSED(x) Q_UNUSED(y) Q_UNUSED(viewportWidth) Q_UNUSED(viewportHeight)
    Q_UNUSED(buttons)
#endif
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
    // T23：unSelect 引起的跟踪状态变化在**引擎侧**（selectedObjectChange 槽 →
    // setFlagTracking(false)）。不转发的话 QML 绑定收不到 trackingChanged，
    // 状态文案会残留"正在跟踪：×"（UI-08 在 T23 新相位里实测抓到）。
    ensureTrackingForwarding();
    m_info.clearSelection();
}

void AppFacade::ensureTrackingForwarding()
{
#if defined(STELQUICK_HAS_ENGINE)
    if (m_trackingForwarded || !StelApp::isInitialized())
        return;
    StelCore *core = StelApp::getInstance().getCore();
    StelMovementMgr *mv = core ? core->getMovementMgr() : nullptr;
    if (!mv)
        return;
    QObject::connect(mv, &StelMovementMgr::flagTrackingChanged,
                     this, [this]() { emit trackingChanged(); });
    m_trackingForwarded = true;
#endif
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
    // T23 之前这里读**合取真值**（引擎标志 ∧ 确有选中）：引擎 unSelect() 先清
    // 选中再发信号，selectedObjectChange 槽里的 setFlagTracking(false) 永不执行
    // ⇒ 单读引擎标志会在取消选中后谎报"跟踪中"。T23 已在引擎侧根治
    // （StelMovementMgr::selectedObjectChange 现在处理 RemoveFromSelection），
    // 且引擎 setFlagTracking(true) 本就要求有选中 ⇒ flagTracking==true 蕴含
    // 有选中，合取的第二因子成了死代码。直接读引擎标志 —— 引擎不再撒谎，
    // UI 也不必再打补丁。LOC-08b/LOC-09 的加严判据守着这条不变量。
    return core->getMovementMgr()->getFlagTracking();
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
    ensureTrackingForwarding();   // T23：引擎侧状态变化也要能推到 QML
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
    ensureTrackingForwarding();   // T23：同 locateSelected
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
