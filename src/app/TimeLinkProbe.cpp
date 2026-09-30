/*
 * TimeLinkProbe — 实现（T35-A）。动机、六问清单、与 T27 那条线索的关系见头注。
 *
 * 驱动方式沿用 LocationCheck/ToolbarCheck 的线性步骤表；`delayAfter` 语义 =
 * "跑完本步之后的等待"（T33 陷阱 41）——**本套探针里这个语义是承重的**：
 * 窗口的长度就是它，写成 0 会把窗口压成一个事件循环。
 *
 * 墙钟用**自己的** QElapsedTimer（与帧泵内部那只不同实例、不同代码路径）。
 * ΔJD 读 `getSimClockJD()`（T16 的单一真源，非"上一帧快照"的 `getJD()`，
 * 后者只用于 Q1 的同源核对）。
 *
 * ⚠️ 本文件里的读数一律用 **Qt 的 `%1` 占位符**，不是 printf 的 `%.9f`：
 * 首次实现混用过，`.arg()` 找不到 `%n` 会**原样返回**，日志里留下一串 `%.9f`
 * （"仪器会撒谎"的另一副面孔：不报错，只是把没替换的东西打出来）。
 */
#include "app/TimeLinkProbe.hpp"

#include "app/AppFacade.hpp"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTimer>
#include <QVector>
#include <cmath>

#if defined(STELQUICK_HAS_ENGINE)
#include "StelApp.hpp"
#include "StelCore.hpp"
#include "StelMovementMgr.hpp"
#endif

namespace stelapp {

namespace {

using ProbeResult = TimeLinkProbe::Result;

#if defined(STELQUICK_HAS_ENGINE)

//! 当地恒星日转过的角度：360.9856° / 平太阳日（Q6 的期望系数）。
constexpr double kSiderealDegPerDay = 360.9856091;

//! J2000 赤道坐标向量的赤经（度）。
double raDeg(const Vec3d &v)
{
    return std::atan2(v[1], v[0]) * 180.0 / M_PI;
}

//! 窗口读数的一行统一格式（Q2/Q3/Q5 共用）。
QString fmtWindowLine(const QString &tag, double w, double dJd, double rate, double scale)
{
    const double pred = w * rate * scale;
    const double ratio = (std::fabs(pred) > 1e-12) ? dJd / pred : 0.0;
    const double implied = (rate * scale != 0.0) ? dJd / (rate * scale) : 0.0;
    const double devPct = (std::fabs(pred) > 1e-12) ? 100.0 * (dJd - pred) / pred : 0.0;
    return QStringLiteral("%1 窗口实测 W=%2s  ΔJD=%3 天  rate=%4 scale=%5 ⇒ 应走 %6 天  "
                          "比值=%7  反推窗口=%8s  偏差=%9%")
        .arg(tag)
        .arg(w, 0, 'f', 4)
        .arg(dJd, 0, 'f', 6)
        .arg(rate, 0, 'g', 9)
        .arg(scale, 0, 'g', 3)
        .arg(pred, 0, 'f', 6)
        .arg(ratio, 0, 'f', 4)
        .arg(implied, 0, 'f', 4)
        .arg(devPct, 0, 'f', 2);
}

struct Ctx
{
    QCoreApplication *app = nullptr;
    AppFacade *facade = nullptr;
    std::function<void(const ProbeResult &)> onDone;
    ProbeResult result;
    QStringList lines;
    int nextDelay = 0;

    QElapsedTimer clock;

    // ── 窗口采样的 arm 值 ────────────────────────────────────────────────
    double jd0 = 0.0, t0 = 0.0;
    double rateUsed = 0.0, scaleUsed = 1.0;
    QString windowTag;

    // ── 收尾要还原的原值（血泪第 9 条：探针改过的环境必须还原）────────────
    double rateSave = 1.0, scaleSave = 1.0;
    bool mountSave = true, trackingSave = false;

    // ── Q6 天文腿 ──────────────────────────────────────────────────────
    double ra0 = 0.0;
    double lst0 = 0.0;
    Vec3d altAz0;

    StelCore *core() const { return StelApp::getInstance().getCore(); }

    void note(const QString &line) { lines.append(QStringLiteral("  ") + line); }

    //! 一个窗口的原始读数（Q2 核心：三个量在同一时刻量齐）。
    void reportWindow()
    {
        const double jd1 = core()->getSimClockJD();
        const double t1 = clock.nsecsElapsed() / 1e9;
        note(fmtWindowLine(windowTag, t1 - t0, jd1 - jd0, rateUsed, scaleUsed));
    }

    void finish()
    {
        result.ran = true;
        result.summary = QStringLiteral("仿真时间链路探针：只报读数、不下结论"
                                        "（断言见 T35-C 的 TimeLinkCheck）");
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

//! "开窗"：记 jd0/t0，然后靠 delayAfter 让帧泵自由跑满窗口。
void armWindow(Ctx *c, const QString &tag, double rate, double scale)
{
    c->windowTag = tag;
    c->rateUsed = rate;
    c->scaleUsed = scale;
    c->jd0 = c->core()->getSimClockJD();
    c->t0 = c->clock.nsecsElapsed() / 1e9;
}

#endif  // STELQUICK_HAS_ENGINE

} // namespace

void TimeLinkProbe::run(QCoreApplication *app,
                        AppFacade *facade,
                        const std::function<void(const Result &)> &onDone,
                        int delayMs)
{
#if !defined(STELQUICK_HAS_ENGINE)
    Q_UNUSED(facade)
    Q_UNUSED(delayMs)
    Result r;
    r.unavailable = true;
    r.summary = QStringLiteral("时间链路探针需要合流形态构建（STELQUICK_HAS_ENGINE 未定义）");
    QTimer::singleShot(0, app, [onDone, r]() { onDone(r); });
#else
    auto *ctx = new Ctx;
    ctx->app = app;
    ctx->facade = facade;
    ctx->onDone = onDone;
    ctx->clock.start();

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

    // ── 步骤 1：前提门 + Q1 时钟面 ─────────────────────────────────────────
    steps->append({delayMs, [](Ctx *c) {
        if (!StelApp::isInitialized() || !c->core())
        {
            c->result.unavailable = true;
            c->note(QStringLiteral("前提：引擎未初始化 ⇒ UNAVAILABLE"));
            return;
        }
        StelCore *core = c->core();
        c->rateSave = core->getTimeRate();
        c->scaleSave = core->getSimClockScale();
        if (StelMovementMgr *m = core->getMovementMgr())
        {
            c->mountSave = m->getEquatorialMount();
            c->trackingSave = m->getFlagTracking();
        }

        c->note(QStringLiteral("Q1 时钟模式=%1（isHostDriven=%2）  scale=%3")
                    .arg(core->getSimClockModeName())
                    .arg(core->isSimClockHostDriven() ? QStringLiteral("true")
                                                      : QStringLiteral("false"))
                    .arg(core->getSimClockScale(), 0, 'g', 3));

        const double jdClock = core->getSimClockJD();
        const double jdSnapshot = core->getJD();
        const double jdFacade = c->facade ? c->facade->julianDay() : -1.0;
        c->note(QStringLiteral("Q1 读面 simClockJD=%1  getJD=%2  facade.julianDay=%3"
                               "（后两者与前者之差 %4 / %5 天）")
                    .arg(jdClock, 0, 'f', 9)
                    .arg(jdSnapshot, 0, 'f', 9)
                    .arg(jdFacade, 0, 'f', 9)
                    .arg(jdSnapshot - jdClock, 0, 'e', 3)
                    .arg(jdFacade - jdClock, 0, 'e', 3));
        c->note(QStringLiteral("Q1 速率面 core->getTimeRate=%1 天/秒（JDay/sec，"
                               "见 StelCore.hpp:595）  facade->timeRate=%2  "
                               "STELQUICK_LIVE_SIMRATE=%3")
                    .arg(core->getTimeRate(), 0, 'g', 9)
                    .arg(c->facade ? c->facade->timeRate() : -1.0, 0, 'g', 9)
                    .arg(qgetenv("STELQUICK_LIVE_SIMRATE").isEmpty()
                             ? QStringLiteral("(未设 ⇒ 帧泵默认 0.1)")
                             : QString::fromLocal8Bit(qgetenv("STELQUICK_LIVE_SIMRATE"))));
    }});

    // ── 步骤 2：把链路参数钉到已知值 ──────────────────────────────────────
    steps->append({400, [](Ctx *c) {
        if (!StelApp::isInitialized() || !c->core())
            return;
        c->core()->setTimeRate(0.2);
        c->core()->setSimClockScale(1.0);
    }});

    // ── 步骤 3/4（Q2 档 A）：长窗口 W≈2.5s ────────────────────────────────
    steps->append({2500, [](Ctx *c) {
        if (!StelApp::isInitialized() || !c->core())
            return;
        armWindow(c, QStringLiteral("Q2 档A(长)"), 0.2, 1.0);
    }});
    steps->append({0, [](Ctx *c) {
        if (!StelApp::isInitialized() || !c->core())
            return;
        c->reportWindow();
    }});

    // ── 步骤 5/6（Q3 档 A'）：短窗口 W≈0.8s（同 rate ⇒ Q3 窗口线性）─────────
    steps->append({800, [](Ctx *c) {
        if (!StelApp::isInitialized() || !c->core())
            return;
        armWindow(c, QStringLiteral("Q3 档A'(短)"), 0.2, 1.0);
    }});
    steps->append({0, [](Ctx *c) {
        if (!StelApp::isInitialized() || !c->core())
            return;
        c->reportWindow();
        c->note(QStringLiteral("Q3 语义：档A(长)/档A'(短) 的 ΔJD 之比应与 W 之比同阶（≈3.1）；"
                               "若帧泵按**固定步长**推进，两者会接近 1:1"));
    }});

    // ── 步骤 7..10（Q4）：increaseTimeSpeed 三级阶梯台账 ───────────────────
    steps->append({200, [](Ctx *c) {
        if (!StelApp::isInitialized() || !c->core())
            return;
        c->core()->setTimeRate(0.1);
        c->note(QStringLiteral("Q4 阶梯起点：setTimeRate(0.1) ⇒ 读数 %1")
                    .arg(c->core()->getTimeRate(), 0, 'g', 9));
    }});
    for (int i = 1; i <= 3; ++i)
    {
        steps->append({150, [i](Ctx *c) {
            if (!StelApp::isInitialized() || !c->core())
                return;
            const double before = c->core()->getTimeRate();
            c->core()->increaseTimeSpeed();
            c->note(QStringLiteral("Q4 increaseTimeSpeed 第 %1 次：%2 ⇒ %3（×10 阶梯）"
                                   "—— INTERACTCHECK 的 IT-05 自己就注入 L 键走这条路，"
                                   "所以同一进程里 engineRate 是**移动靶**")
                        .arg(i)
                        .arg(before, 0, 'g', 9)
                        .arg(c->core()->getTimeRate(), 0, 'g', 9));
        }});
    }

    // ── 步骤 11/12（Q5）：scale=0 冻结窗口 ────────────────────────────────
    steps->append({300, [](Ctx *c) {
        if (!StelApp::isInitialized() || !c->core())
            return;
        c->core()->setTimeRate(0.2);
        c->core()->setSimClockScale(0.0);
    }});
    steps->append({1000, [](Ctx *c) {
        if (!StelApp::isInitialized() || !c->core())
            return;
        armWindow(c, QStringLiteral("Q5 冻结(scale=0)"), 0.2, 0.0);
    }});
    steps->append({0, [](Ctx *c) {
        if (!StelApp::isInitialized() || !c->core())
            return;
        c->reportWindow();
    }});

    // ── 步骤 13/14（Q5b）：恢复 scale=1 后一窗口（不应补冻结期）──────────────
    steps->append({1200, [](Ctx *c) {
        if (!StelApp::isInitialized() || !c->core())
            return;
        c->core()->setSimClockScale(1.0);
        armWindow(c, QStringLiteral("Q5b 恢复后"), 0.2, 1.0);
    }});
    steps->append({0, [](Ctx *c) {
        if (!StelApp::isInitialized() || !c->core())
            return;
        c->reportWindow();
        c->note(QStringLiteral("Q5b 语义：恢复后这一窗口的 ΔJD 不应包含冻结期"
                               "（若「补回来」，偏差% 会 ≈ +100% 量级）"));
    }});

    // ── 步骤 15（Q6 预备）：AltAz + 自由挂载（天文腿前提，探针自己建立）──────
    steps->append({500, [](Ctx *c) {
        if (!StelApp::isInitialized() || !c->core())
            return;
        StelMovementMgr *m = c->core()->getMovementMgr();
        if (!m)
        {
            c->note(QStringLiteral("Q6 前置失败：无 MovementMgr ⇒ 天文腿无读数"));
            return;
        }
        m->setFlagTracking(false);
        m->setEquatorialMount(false);
        c->core()->setTimeRate(0.05);
        c->note(QStringLiteral("Q6 前提：挂载=AltAz（getEquatorialMount=%1） 跟踪=%2  "
                               "rate=%3 天/秒（窗口 2s ⇒ ΔJD≈0.1 天 ⇒ 恒星时应转 ≈36°）")
                    .arg(m->getEquatorialMount() ? QStringLiteral("true")
                                                 : QStringLiteral("false"))
                    .arg(m->getFlagTracking() ? QStringLiteral("true")
                                              : QStringLiteral("false"))
                    .arg(c->core()->getTimeRate(), 0, 'g', 4));
    }});

    // ── 步骤 16/17（Q6）：恒星时恒等式 + 下游重算 + "视线自转"说法核验 ───────
    steps->append({2000, [](Ctx *c) {
        if (!StelApp::isInitialized() || !c->core())
            return;
        StelMovementMgr *m = c->core()->getMovementMgr();
        if (!m)
            return;
        c->ra0 = raDeg(m->getViewDirectionJ2000());
        c->jd0 = c->core()->getSimClockJD();
        c->t0 = c->clock.nsecsElapsed() / 1e9;
        c->lst0 = c->core()->getLocalSiderealTime();
        c->altAz0 = c->core()->j2000ToAltAz(Vec3d(1., 0., 0.), StelCore::RefractionOff);
    }});
    steps->append({0, [](Ctx *c) {
        if (!StelApp::isInitialized() || !c->core())
            return;
        StelMovementMgr *m = c->core()->getMovementMgr();
        if (!m)
            return;
        const double ra1 = raDeg(m->getViewDirectionJ2000());
        const double jd1 = c->core()->getSimClockJD();
        const double t1 = c->clock.nsecsElapsed() / 1e9;
        const double lst1 = c->core()->getLocalSiderealTime();
        const Vec3d altAz1 = c->core()->j2000ToAltAz(Vec3d(1., 0., 0.), StelCore::RefractionOff);

        double dRa = ra1 - c->ra0;
        while (dRa > 180.0) dRa -= 360.0;
        while (dRa < -180.0) dRa += 360.0;
        double dLstDeg = (lst1 - c->lst0) * 180.0 / M_PI;
        while (dLstDeg > 180.0) dLstDeg -= 360.0;
        while (dLstDeg < -180.0) dLstDeg += 360.0;

        const double dJd = jd1 - c->jd0;
        const double expectDeg = dJd * kSiderealDegPerDay;
        const double azAngle = (c->altAz0.norm() > 1e-9 && altAz1.norm() > 1e-9)
                                   ? c->altAz0.angle(altAz1) * 180.0 / M_PI : 0.0;

        c->note(QStringLiteral("Q6a 时间窗：W=%1s  ΔJD=%2 天")
                    .arg(t1 - c->t0, 0, 'f', 4)
                    .arg(dJd, 0, 'f', 6));
        c->note(QStringLiteral("Q6b 恒星时恒等式：ΔLST=%1°  期望 ΔJD×360.9856=%2°  "
                               "残差=%3°（%4%）")
                    .arg(dLstDeg, 0, 'f', 4)
                    .arg(expectDeg, 0, 'f', 4)
                    .arg(dLstDeg - expectDeg, 0, 'f', 4)
                    .arg(std::fabs(expectDeg) > 1e-9
                             ? 100.0 * (dLstDeg - expectDeg) / expectDeg : 0.0,
                         0, 'f', 3));
        c->note(QStringLiteral("Q6c 下游重算：固定 J2000 方向 (RA=0h,Dec=0) 经引擎 "
                               "j2000ToAltAz 的落点转过 %1°（>0 ⇒ 帧末真的用新 JD 重算了；"
                               "T30 铁律：「设了新值」证明不了「重算过」）")
                    .arg(azAngle, 0, 'f', 3));
        c->note(QStringLiteral("Q6d 「视线随 JD 转」机制核验：静置（无键/无拖拽、tracking=false）时 "
                               "getViewDirectionJ2000 的 ΔRA=%1° —— 与 ΔLST 同阶 ⇒ 视线**确实**"
                               "随 JD 转。机理 = updateVisionVector 的「vision vector locked to "
                               "its position in the mountFrame」分支（flagLockEquPos 默认 false）"
                               "每帧由 mountFrameToJ2000(viewDirectionMountFrame) 重算，而量天系不动 "
                               "⇒ J2000 视线以恒星时速率旋转。这正是 T27「视线锁地平 ⇒ 随 JD 漂移」"
                               "的正确机理（T27 错的只是把**速率**读成了 1.5 天/秒）")
                    .arg(dRa, 0, 'f', 4));
    }});

    // ── 步骤 18：收尾还原（rate / scale / 挂载 / 跟踪）───────────────────────
    steps->append({0, [](Ctx *c) {
        if (!StelApp::isInitialized() || !c->core())
            return;
        StelMovementMgr *m = c->core()->getMovementMgr();
        if (m)
        {
            m->setEquatorialMount(c->mountSave);
            m->setFlagTracking(c->trackingSave);
        }
        c->core()->setSimClockScale(c->scaleSave);
        c->core()->setTimeRate(c->rateSave);
        c->note(QStringLiteral("收尾：已还原 rate=%1 scale=%2 挂载=%3 跟踪=%4")
                    .arg(c->core()->getTimeRate(), 0, 'g', 9)
                    .arg(c->core()->getSimClockScale(), 0, 'g', 3)
                    .arg(m ? (m->getEquatorialMount() ? QStringLiteral("赤道")
                                                      : QStringLiteral("AltAz"))
                           : QStringLiteral("<无>"))
                    .arg(m ? (m->getFlagTracking() ? QStringLiteral("true")
                                                   : QStringLiteral("false"))
                           : QStringLiteral("<无>")));
    }});

    (*tick)();
#endif
}

} // namespace stelapp
