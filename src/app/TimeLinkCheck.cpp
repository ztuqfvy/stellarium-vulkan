/*
 * TimeLinkCheck — 实现（T35-C）。判据清单 / 与 T27 结论的关系 / 负控口径见头注。
 *
 * 驱动方式沿用 ToolbarCheck 的线性步骤表；`delayAfter` 语义 = "跑完本步之后的等待"
 *（T33 陷阱 41）——**本套里这个语义就是窗口长度本身**，写成 0 会把窗口压成一个
 * 事件循环（T27 的③正是"窗口没被测量"）。
 *
 * ── 两个采样口径（都承重）────────────────────────────────────────────────────
 *   · **同一时刻量齐**：ΔJD / 墙钟 / rate / scale / 恒星时 / AltAz 取样在同一个
 *     步骤体里连续完成 —— 帧泵是 GUI 线程的 QTimer，步骤体执行期间**不可能重入**，
 *     所以这些读数是同一时刻的快照（不需要锁）。
 *   · **墙钟用自己的计时器**：`QElapsedTimer` 是本文件自己的实例，与帧泵内部那只
 *     （`LiveSkyRuntime::m_simClock`）不同对象、不同代码路径 ⇒ 不是"自洽假绿"。
 *
 * ⚠️ 读数一律用 **Qt 的 `%n` 占位符**（不是 printf 的 `%.4f`）：`.arg()` 找不到
 * `%n` 会**原样返回**，日志里留下一串 `%.4f` —— "仪器会撒谎"的另一副面孔：
 * 不报错，只是把没替换的东西打出来。同款血泪见 `TimeLinkProbe.cpp` 头注。
 */
#include "app/TimeLinkCheck.hpp"

#include "app/AppFacade.hpp"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTimer>
#include <QVector>
#include <algorithm>
#include <cmath>

#if defined(STELQUICK_HAS_ENGINE)
#include "StelApp.hpp"
#include "StelCore.hpp"
#endif

namespace stelapp {

namespace {

using CheckResult = TimeLinkCheck::Result;

#if defined(STELQUICK_HAS_ENGINE)

//! 当地恒星日转过的角度：360.9856° 每平太阳日（TL-07 的恒等式系数）。
constexpr double kSiderealDegPerDay = 360.9856091;
//! TL-07 用的固定 J2000 方向：赤经 0h、赤纬 0（离天极 90°，方位漂移最灵敏）。
const Vec3d kFixedJ2000(1.0, 0.0, 0.0);

//! TL-01 的相对容差。理由：单次采样点的误差 ≤ 一帧（帧泵 ~20-50ms）；
//! rate=0.2 天/秒、窗口 3.0s 时，一帧误差占 2×0.05×0.2/(3.0×0.2) ≈ 3.3%；
//! 取 15% 留足余量，同时仍远小于负控的 50%。
constexpr double kLinkTolRel = 0.15;

//! 带符号的定点格式化（Qt 的 .arg 不打印 '+'，角差要看正负）。
QString signF(double v, int prec)
{
    return QStringLiteral("%1%2")
        .arg(v < 0.0 ? QStringLiteral("-") : QStringLiteral("+"))
        .arg(std::fabs(v), 0, 'f', prec);
}

struct Ctx
{
    QCoreApplication *app = nullptr;
    AppFacade *facade = nullptr;
    std::function<void(const CheckResult &)> onDone;
    CheckResult result;
    int nextDelay = 0;
    bool ok = true;
    bool stop = false;

    QElapsedTimer clock;

    // ── 窗口就绪门（T35 定稿轮新增；血泪见头注/产品文档 §8.6）──────────────
    //! 本套的"窗口型"判据全都用 `QTimer::singleShot(delayAfter)` 划窗。事件循环一旦
    //! 被饿住（批次连跑 11 套件 / Spotlight / 上一个进程的 Metal 拆卸），**定时器与帧泵
    //! 同源停摆** ⇒ 窗口被拖长、ΔJD 又少涨 ⇒ 两个量被**同一个**抖动污染 ⇒
    //! 该窗口的"链路自洽"判断**没有分辨力**（定稿轮实测：档A 名义 1.5s 被拖到 2.19s，
    //! 裸比值偏 13%，把 TL-02 打红 1/5 跑）。
    //! 处置沿 T22/T27/T28/T33「有界就绪门」先例：**仪器没接上 ⇒ 重测，不判红**。
    //! ⚠️ 这是**单边**门（`singleShot` 只会晚不会早 ⇒ W < 名义不可能），且**真缺陷不改
    //! 墙钟**（链路半速不改 W）⇒ 门**不会掩护真缺陷**。
    static constexpr double kWinSlack = 1.25;   //!< W 超名义 25% 即认为本窗被污染
    static constexpr int kWinMaxRetry = 3;      //!< 单窗重测预算（有界）
    int *stepIdx = nullptr;    //!< 驱动器的步号；就绪门用它"停在本步重测"
    double nominalW = 0.0;     //!< 本窗名义长度（秒）；0 = 未设 ⇒ 门关闭
    int winRetry = 0;

    // ── 窗口的 arm 值 ────────────────────────────────────────────────────
    double jd0 = 0.0, t0 = 0.0, lst0 = 0.0, rate0 = 0.0, scale0 = 1.0;
    Vec3d altAz0;

    // ── TL-01 ───────────────────────────────────────────────────────────
    double tl01Pred = 0.0;
    bool tl01RateStable = false;

    // ── TL-02（rate 0.2 / 0.8 两个等长窗口）─────────────────────────────
    double tl02Wa = 0.0, tl02Da = 0.0, tl02Wb = 0.0, tl02Db = 0.0;

    // ── TL-03（rate 0.4 下短/长窗口）────────────────────────────────────
    double tl03Ws = 0.0, tl03Ds = 0.0, tl03Wl = 0.0, tl03Dl = 0.0;

    // ── TL-04（冻结 / 恢复）──────────────────────────────────────────────
    double tl04FreezeDjd = 0.0;

    // ── TL-05（阶梯）────────────────────────────────────────────────────
    double tl05D1 = 0.0, tl05R1 = 0.0;
    double tl05D2 = 0.0, tl05R2 = 0.0;
    double tl05Ladder3 = 0.0;

    // ── 收尾还原（血泪第 9 条）────────────────────────────────────────────
    double rateSave = 1.0, scaleSave = 1.0;

    StelCore *core() const { return StelApp::getInstance().getCore(); }

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

    //! 同一时刻的完整快照（帧泵是 GUI 线程 QTimer ⇒ 本函数执行期间不可能重入）。
    //! `nominalMs > 0` 时同时设定本窗名义长度（窗口就绪门的分母）；不传则**保留**原值
    //!（就绪门自己重起窗时用这个形式，绝不能把 nominalW 清零）。
    void arm(double nominalMs = 0.0)
    {
        if (nominalMs > 0.0)
            nominalW = nominalMs / 1000.0;
        jd0 = core()->getSimClockJD();
        t0 = clock.nsecsElapsed() / 1e9;
        lst0 = core()->getLocalSiderealTime();
        altAz0 = core()->j2000ToAltAz(kFixedJ2000, StelCore::RefractionOff);
        rate0 = core()->getTimeRate();
        scale0 = core()->getSimClockScale();
    }

    //! 窗口读数（= 与 arm 的差）。
    struct Reading
    {
        double w = 0.0;      //!< 实测墙钟窗口（秒）
        double dJd = 0.0;    //!< ΔJD（天）
        double rate = 0.0;   //!< 窗口末端同刻读到的 rate
        double scale = 1.0;
        double pred = 0.0;   //!< W × rate0 × scale0（rate 用**窗口起点**的读数）
        double ratio = 0.0;  //!< dJd / pred
        double dLstDeg = 0.0;
        double altAzAngleDeg = 0.0;
        bool rateStable = false;
        //! 窗口就绪门判定"本窗被环境污染、读数不可用" ⇒ 调用方必须**立刻 return**。
        bool suspect = false;
    };

    Reading take()
    {
        Reading r;
        const double jd1 = core()->getSimClockJD();
        const double t1 = clock.nsecsElapsed() / 1e9;
        const double lst1 = core()->getLocalSiderealTime();
        const Vec3d altAz1 = core()->j2000ToAltAz(kFixedJ2000, StelCore::RefractionOff);
        r.rate = core()->getTimeRate();
        r.w = t1 - t0;
        r.dJd = jd1 - jd0;
        r.scale = scale0;
        r.pred = r.w * rate0 * scale0;
        r.ratio = (std::fabs(r.pred) > 1e-12) ? r.dJd / r.pred : 0.0;
        double dLst = lst1 - lst0;
        while (dLst > M_PI) dLst -= 2.0 * M_PI;
        while (dLst < -M_PI) dLst += 2.0 * M_PI;
        r.dLstDeg = dLst * 180.0 / M_PI;
        r.altAzAngleDeg = (altAz0.norm() > 1e-9 && altAz1.norm() > 1e-9)
                              ? altAz0.angle(altAz1) * 180.0 / M_PI : 0.0;
        // 窗口内 rate 必须没被改动（T27 陷阱②：rate 是移动靶）。
        r.rateStable = (std::fabs(r.rate - rate0) <= std::fabs(rate0) * 1e-9 + 1e-15);

        // ── 🔴 窗口就绪门 ────────────────────────────────────────────────────
        // 窗口超名义 25% ⇒ 事件循环被饿住（帧泵同源停摆）⇒ **本窗不可判**：
        // 重起窗、把本步**原样再跑一遍**（有界 kWinMaxRetry 次）。
        if (stepIdx && nominalW > 0.0 && r.w > nominalW * kWinSlack)
        {
            if (winRetry >= kWinMaxRetry)
            {
                note(QStringLiteral("窗口就绪门：W=%1s 连续超名义 %2s 的 25%，"
                                    "重测预算（%3 次）已尽 ⇒ **按现状判定**"
                                    "（该读数可能受环境污染，留意）")
                         .arg(r.w, 0, 'f', 4).arg(nominalW, 0, 'f', 3).arg(kWinMaxRetry));
                winRetry = 0;
                return r;
            }
            ++winRetry;
            note(QStringLiteral("窗口就绪门：W=%1s 超名义 %2s 的 25% ⇒ 事件循环被饿住"
                                "（定时器与帧泵同源停摆）⇒ **本窗不可判**，"
                                "重起窗重测（第 %3/%4 次）")
                     .arg(r.w, 0, 'f', 4).arg(nominalW, 0, 'f', 3)
                     .arg(winRetry).arg(kWinMaxRetry));
            arm();                                   // 重起窗（保留 nominalW / winRetry）
            nextDelay = static_cast<int>(nominalW * 1000.0 + 0.5);
            --(*stepIdx);                            // 停在本步：下一轮再 take
            r.suspect = true;
            return r;
        }
        winRetry = 0;                                // 本窗判定成立 ⇒ 预算复位
        return r;
    }

    void finish()
    {
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

//! 窗口读数的一行统一格式（TL-01/02/03/05 共用）。
QString fmtWindow(const QString &tag, const Ctx::Reading &r)
{
    const double implied = (r.rate * r.scale != 0.0) ? r.dJd / (r.rate * r.scale) : 0.0;
    return QStringLiteral("%1 W=%2s ΔJD=%3 天 rate=%4 scale=%5 ⇒ 应走 %6 天 比值=%7 反推窗口=%8s")
        .arg(tag)
        .arg(r.w, 0, 'f', 4)
        .arg(r.dJd, 0, 'f', 6)
        .arg(r.rate, 0, 'g', 9)
        .arg(r.scale, 0, 'g', 3)
        .arg(r.pred, 0, 'f', 6)
        .arg(r.ratio, 0, 'f', 4)
        .arg(implied, 0, 'f', 4);
}

#endif  // STELQUICK_HAS_ENGINE

} // namespace

void TimeLinkCheck::run(QCoreApplication *app,
                        AppFacade *facade,
                        const std::function<void(const Result &)>& onDone,
                        int delayMs)
{
#if !defined(STELQUICK_HAS_ENGINE)
    Q_UNUSED(facade)
    Q_UNUSED(delayMs)
    Result r;
    r.unavailable = true;
    r.summary = QStringLiteral("时间链路自检需要合流形态构建（STELQUICK_HAS_ENGINE 未定义）");
    QTimer::singleShot(0, app, [onDone, r]() { onDone(r); });
#else
    auto *ctx = new Ctx;
    ctx->app = app;
    ctx->facade = facade;
    ctx->onDone = onDone;
    ctx->clock.start();

    auto steps = std::make_shared<QVector<Step>>();
    auto idx = std::make_shared<int>(0);
    // 窗口就绪门要能"停在本步重测"：把步号交给 Ctx（见 Ctx::take 内的 `--(*stepIdx)`）。
    ctx->stepIdx = idx.get();

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

    // ── 步骤 1：前提门 + TL-06 读面同源 ───────────────────────────────────
    steps->append({delayMs, [](Ctx *c) {
        if (!StelApp::isInitialized() || !c->core())
        {
            c->result.unavailable = true;
            c->note(QStringLiteral("前提：引擎未初始化 ⇒ UNAVAILABLE"));
            c->stop = true;
            return;
        }
        StelCore *core = c->core();
        c->rateSave = core->getTimeRate();
        c->scaleSave = core->getSimClockScale();
        if (!core->isSimClockHostDriven())
        {
            c->result.unavailable = true;
            c->note(QStringLiteral("前提：仿真时钟未接管（模式=%1）⇒ UNAVAILABLE"
                                   "（本套判据只在 HostDriven 形态下有意义）")
                        .arg(core->getSimClockModeName()));
            c->stop = true;
            return;
        }

        const QString mode = core->getSimClockModeName();
        const bool hostDriven = core->isSimClockHostDriven();
        const double rate = core->getTimeRate();
        const double jdClock = core->getSimClockJD();
        const double snapshotDiff = std::fabs(core->getJD() - jdClock);
        const double facadeDiff = std::fabs((c->facade ? c->facade->julianDay() : 0.0) - jdClock);

        // 一帧的推进量上限：容差按它给（帧泵名义 50fps ⇒ 20ms；留 10 倍余量）。
        const double snapshotTol = std::max(1e-6, std::fabs(rate) * 0.2);
        c->mark(hostDriven && mode == QLatin1String("HostDriven")
                    && snapshotDiff <= snapshotTol && facadeDiff <= 1e-9,
                QStringLiteral("TL-06 读面同源：模式=%1 HostDriven=%2 |getJD−simClockJD|=%3"
                               "（≤%4）|facade.julianDay−simClockJD|=%5（≤1e-9）")
                    .arg(mode)
                    .arg(hostDriven ? QStringLiteral("true") : QStringLiteral("false"))
                    .arg(snapshotDiff, 0, 'e', 3)
                    .arg(snapshotTol, 0, 'e', 3)
                    .arg(facadeDiff, 0, 'e', 3));
        c->note(QStringLiteral("TL-06 语义：getJD() 是**上一帧快照**（T16），允许落后一帧；"
                               "facade.julianDay() 必须与真源逐位同源（防「第二个时间源」）"));
    }});

    // ── 步骤 2..4：TL-01 链路自洽 ─────────────────────────────────────────
    steps->append({400, [](Ctx *c) {
        if (c->stop || !StelApp::isInitialized() || !c->core())
            return;
        // 慢档：让"窗口内一帧误差"占比足够小（rate 越小相对误差越大，不能太小）。
        c->core()->setTimeRate(0.2);
        c->core()->setSimClockScale(1.0);
    }});
    steps->append({3000, [](Ctx *c) {
        if (c->stop || !StelApp::isInitialized() || !c->core())
            return;
        c->arm(3000.0);   // 名义窗 3000 ms（窗口就绪门的分母）
    }});
    steps->append({0, [](Ctx *c) {
        if (c->stop || !StelApp::isInitialized() || !c->core())
            return;
        const Ctx::Reading r = c->take();
        if (r.suspect) return;   // 窗口被调度拖长 ⇒ 本窗不可判（见 take 内的就绪门）
        c->tl01Pred = r.pred;
        c->tl01RateStable = r.rateStable;
        const double relErr =
            (std::fabs(r.pred) > 1e-12) ? std::fabs(r.dJd - r.pred) / std::fabs(r.pred) : 1.0;
        c->mark(relErr <= kLinkTolRel && r.rateStable,
                QStringLiteral("TL-01 链路自洽：%1 ⇒ 相对偏差=%2%（≤%3%）")
                    .arg(fmtWindow(QStringLiteral("窗口"), r))
                    .arg(relErr * 100.0, 0, 'f', 2)
                    .arg(kLinkTolRel * 100.0, 0, 'f', 0));
        c->note(QStringLiteral("TL-01 ⇒ **这就是 T27 那条线索的反命题**：同一时刻量齐三个量时，"
                               "「ΔJD == W × rate × scale」成立 ⇒ 不存在「推进量与 rate 脱钩」。"));
        c->note(QStringLiteral("TL-01 ⇒ T27 那个数字的对账：把「0.61 天」除以**任何**"
                               "一个真实的 rate 读数，得到的都是**某个真实窗口长度**"
                               "（÷0.1 = 6.1s、÷0.2 = 3.06s、÷1 = 0.61s）——"
                               "而「1.5 天/秒」这个数只能由「0.61 天 ÷ 名义 0.4s」得到："
                               "它既不是任何时刻的 rate 读数，也不是任何速率的物理量，"
                               "是「用错窗口 + 读错单位」的商。"));
    }});

    // ── 步骤 5..9：TL-02 rate 多档线性（成对，等长窗口）──────────────────
    steps->append({300, [](Ctx *c) {
        if (c->stop || !StelApp::isInitialized() || !c->core())
            return;
        c->core()->setTimeRate(0.2);
    }});
    steps->append({1500, [](Ctx *c) {
        if (c->stop || !StelApp::isInitialized() || !c->core())
            return;
        if (std::fabs(c->core()->getTimeRate() - 0.2) > 1e-12)
            c->core()->setTimeRate(0.2);
        c->arm(1500.0);   // 名义窗 1500 ms（窗口就绪门的分母）
    }});
    steps->append({0, [](Ctx *c) {
        if (c->stop || !StelApp::isInitialized() || !c->core())
            return;
        const Ctx::Reading r = c->take();
        if (r.suspect) return;   // 窗口被调度拖长 ⇒ 本窗不可判（见 take 内的就绪门）
        c->tl02Wa = r.w;
        c->tl02Da = r.dJd;
    }});
    steps->append({300, [](Ctx *c) {
        if (c->stop || !StelApp::isInitialized() || !c->core())
            return;
        c->core()->setTimeRate(0.8);
    }});
    steps->append({1500, [](Ctx *c) {
        if (c->stop || !StelApp::isInitialized() || !c->core())
            return;
        if (std::fabs(c->core()->getTimeRate() - 0.8) > 1e-12)
            c->core()->setTimeRate(0.8);
        c->arm(1500.0);   // 名义窗 1500 ms（窗口就绪门的分母）
    }});
    steps->append({0, [](Ctx *c) {
        if (c->stop || !StelApp::isInitialized() || !c->core())
            return;
        const Ctx::Reading r = c->take();
        if (r.suspect) return;   // 窗口被调度拖长 ⇒ 本窗不可判（见 take 内的就绪门）
        c->tl02Wb = r.w;
        c->tl02Db = r.dJd;
        // ⚠️ 断言用**速率归一化**的比值：(ΔJD/W)/rate 之比 —— 对"两窗长度不等"在解析上免疫。
        //    初版断言的是 ΔJD 之比≈4 **再加**一条"两窗长度差 ≤20%"的护栏；
        //    定稿轮实测该护栏会被**调度抖动**打红（档A 名义 1.5s 被拖到 **2.19s**，
        //    长度差 46% ⇒ TL-02 假红 1/5 跑）。抖动期间事件循环被饿住、帧泵同源停摆，
        //    ΔJD 也不会涨 ⇒ 比值必然偏。**归一化之后这个耦合就断了**。
        //    长度差**降为读数**（不再断言）：它已由归一化在解析上消掉，
        //    留着当断言只会把"调度抖动"误记成"产品回归"。
        const double ratio = (std::fabs(c->tl02Da) > 1e-12) ? c->tl02Db / c->tl02Da : 0.0;
        const double wDrift =
            (std::max(c->tl02Wa, c->tl02Wb) > 1e-9)
                ? std::fabs(c->tl02Wb - c->tl02Wa) / std::max(c->tl02Wa, c->tl02Wb) : 1.0;
        const double perSecA = (c->tl02Wa > 1e-9) ? c->tl02Da / c->tl02Wa : 0.0;   // 天/秒（档A）
        const double perSecB = (c->tl02Wb > 1e-9) ? c->tl02Db / c->tl02Wb : 0.0;   // 天/秒（档B）
        const double normRatio = (std::fabs(perSecA) > 1e-12) ? perSecB / perSecA : 0.0;
        c->note(QStringLiteral("TL-02 档A（rate=0.2）：W=%1s ΔJD=%2；档B（rate=0.8）："
                               "W=%3s ΔJD=%4；两窗口长度差=%5%"
                               "（**只作读数**：断言已按速率归一化）")
                    .arg(c->tl02Wa, 0, 'f', 4).arg(c->tl02Da, 0, 'f', 6)
                    .arg(c->tl02Wb, 0, 'f', 4).arg(c->tl02Db, 0, 'f', 6)
                    .arg(wDrift * 100.0, 0, 'f', 1));
        c->mark(std::fabs(normRatio / 4.0 - 1.0) <= 0.30,
                QStringLiteral("TL-02 rate 多档线性：**速率归一化**后之比=%1"
                               "（=(ΔJD_B/W_B)/(ΔJD_A/W_A)，期望 4.0，±30%；"
                               "裸 ΔJD 之比=%2、窗口长度差=%3%）⇒ rate **真的进了链路**")
                    .arg(normRatio, 0, 'f', 4)
                    .arg(ratio, 0, 'f', 4)
                    .arg(wDrift * 100.0, 0, 'f', 1));
    }});

    // ── 步骤 10..14：TL-03 窗口线性（反驳「固定步长」假设）────────────────
    steps->append({300, [](Ctx *c) {
        if (c->stop || !StelApp::isInitialized() || !c->core())
            return;
        c->core()->setTimeRate(0.4);
    }});
    steps->append({800, [](Ctx *c) {
        if (c->stop || !StelApp::isInitialized() || !c->core())
            return;
        if (std::fabs(c->core()->getTimeRate() - 0.4) > 1e-12)
            c->core()->setTimeRate(0.4);
        c->arm(800.0);   // 名义窗 800 ms（窗口就绪门的分母）
    }});
    steps->append({0, [](Ctx *c) {
        if (c->stop || !StelApp::isInitialized() || !c->core())
            return;
        const Ctx::Reading r = c->take();
        if (r.suspect) return;   // 窗口被调度拖长 ⇒ 本窗不可判（见 take 内的就绪门）
        c->tl03Ws = r.w;
        c->tl03Ds = r.dJd;
    }});
    steps->append({2400, [](Ctx *c) {
        if (c->stop || !StelApp::isInitialized() || !c->core())
            return;
        if (std::fabs(c->core()->getTimeRate() - 0.4) > 1e-12)
            c->core()->setTimeRate(0.4);
        c->arm(2400.0);   // 名义窗 2400 ms（窗口就绪门的分母）
    }});
    steps->append({0, [](Ctx *c) {
        if (c->stop || !StelApp::isInitialized() || !c->core())
            return;
        const Ctx::Reading r = c->take();
        if (r.suspect) return;   // 窗口被调度拖长 ⇒ 本窗不可判（见 take 内的就绪门）
        c->tl03Wl = r.w;
        c->tl03Dl = r.dJd;
        const double ratio = (std::fabs(c->tl03Ds) > 1e-12) ? c->tl03Dl / c->tl03Ds : 0.0;
        const double expRatio = (c->tl03Ws > 1e-9) ? c->tl03Wl / c->tl03Ws : 0.0;
        c->note(QStringLiteral("TL-03 短窗 W=%1s ΔJD=%2；长窗 W=%3s ΔJD=%4；"
                               "ΔJD 之比=%5 vs W 之比=%6（固定步长假设下 ΔJD 之比≈1）")
                    .arg(c->tl03Ws, 0, 'f', 4).arg(c->tl03Ds, 0, 'f', 6)
                    .arg(c->tl03Wl, 0, 'f', 4).arg(c->tl03Dl, 0, 'f', 6)
                    .arg(ratio, 0, 'f', 4).arg(expRatio, 0, 'f', 4));
        // 判别性：固定步长假设 ⇒ 比值趋近 1；本判据要求比值跟随窗口之比（±35%）。
        c->mark(std::fabs(ratio / expRatio - 1.0) <= 0.35 && expRatio > 2.0,
                QStringLiteral("TL-03 窗口线性：ΔJD 之比/W 之比=%1（±35%）⇒ 推进量与"
                               "**真实墙钟**成正比，不是「按帧数固定步长」")
                    .arg(expRatio > 1e-12 ? ratio / expRatio : 0.0, 0, 'f', 4));
    }});

    // ── 步骤 15..19：TL-04 冻结 / 恢复不补 ────────────────────────────────
    // ⚠️ 这里**显式**把 rate 钉成 0.2：否则 TL-04 会继承 TL-03 遗留的 0.4，
    //    使负控下的比值随"上一条判据留下的状态"变化（隐式依赖 ⇒ 负控红项集合
    //    会莫名其妙地漂）。显式之后，A（链路半速）与 B（链路 rate 钉死 0.1）
    //    在该窗口的比值都恰好是 0.5 ⇒ 都落在带宽 [0.4,1.6] 内 ⇒ 由负控 C 独家
    //    负责咬 TL-04。
    steps->append({300, [](Ctx *c) {
        if (c->stop || !StelApp::isInitialized() || !c->core())
            return;
        c->core()->setTimeRate(0.2);
        c->core()->setSimClockScale(0.0);
    }});
    steps->append({1200, [](Ctx *c) {
        if (c->stop || !StelApp::isInitialized() || !c->core())
            return;
        if (std::fabs(c->core()->getSimClockScale()) > 1e-12)
            c->core()->setSimClockScale(0.0);
        c->arm(1200.0);   // 名义窗 1200 ms（窗口就绪门的分母）
    }});
    steps->append({0, [](Ctx *c) {
        if (c->stop || !StelApp::isInitialized() || !c->core())
            return;
        const Ctx::Reading r = c->take();
        if (r.suspect) return;   // 窗口被调度拖长 ⇒ 本窗不可判（见 take 内的就绪门）
        c->tl04FreezeDjd = r.dJd;
        c->note(QStringLiteral("TL-04 冻结窗口 W=%1s scale=0 ⇒ ΔJD=%2 天")
                    .arg(r.w, 0, 'f', 4)
                    .arg(r.dJd, 0, 'f', 12));
    }});
    steps->append({300, [](Ctx *c) {
        if (c->stop || !StelApp::isInitialized() || !c->core())
            return;
        c->core()->setSimClockScale(1.0);
    }});
    steps->append({1500, [](Ctx *c) {
        if (c->stop || !StelApp::isInitialized() || !c->core())
            return;
        if (std::fabs(c->core()->getSimClockScale() - 1.0) > 1e-12)
            c->core()->setSimClockScale(1.0);
        c->arm(1500.0);   // 名义窗 1500 ms（窗口就绪门的分母）
    }});
    steps->append({0, [](Ctx *c) {
        if (c->stop || !StelApp::isInitialized() || !c->core())
            return;
        const Ctx::Reading r = c->take();
        if (r.suspect) return;   // 窗口被调度拖长 ⇒ 本窗不可判（见 take 内的就绪门）
        const double ratio = (std::fabs(r.pred) > 1e-12) ? r.dJd / r.pred : 0.0;
        // 判别性：冻结期 ≈1.5s ⇒ 若「补回来」，比值会 ≈2（(1.5+1.5)/1.5），越过上界。
        // ⚠️ 下界**刻意远离 1**（0.4，不是 0.5）：低于 1 的比值是 TL-01/02/03 的职责
        //（链路整体缩放），TL-04 只负责"不许补 + 冻结必须真零"。0.5 这个下界恰好是
        // 负控 A/B 的半速签名（dt×0.5 / rate 钉死 0.1）⇒ 边界重合会让 TL-04 的红/绿
        // 随计时噪声漂移（实测 0.4990 ↔ 0.5002），并且造成判据之间职责重叠。
        c->mark(std::fabs(c->tl04FreezeDjd) <= 1e-9 && ratio >= 0.4 && ratio <= 1.6,
                QStringLiteral("TL-04 冻结/恢复不补：冻结窗 ΔJD=%1（≤1e-9）∧ 恢复窗 "
                               "W=%2s ΔJD=%3 比值=%4（∈[0.4,1.6]；补冻结期会 →≈2，"
                               "链路死掉会 →≈0）")
                    .arg(c->tl04FreezeDjd, 0, 'f', 12)
                    .arg(r.w, 0, 'f', 4)
                    .arg(r.dJd, 0, 'f', 6)
                    .arg(ratio, 0, 'f', 4));
    }});

    // ── 步骤 20..26：TL-05 速率阶梯 + 阶梯后链路跟变 ──────────────────────
    steps->append({300, [](Ctx *c) {
        if (c->stop || !StelApp::isInitialized() || !c->core())
            return;
        c->core()->setTimeRate(0.1);
    }});
    steps->append({1000, [](Ctx *c) {
        if (c->stop || !StelApp::isInitialized() || !c->core())
            return;
        if (std::fabs(c->core()->getTimeRate() - 0.1) > 1e-12)
            c->core()->setTimeRate(0.1);
        c->arm(1000.0);   // 名义窗 1000 ms（窗口就绪门的分母）
    }});
    steps->append({0, [](Ctx *c) {
        if (c->stop || !StelApp::isInitialized() || !c->core())
            return;
        const Ctx::Reading r = c->take();
        if (r.suspect) return;   // 窗口被调度拖长 ⇒ 本窗不可判（见 take 内的就绪门）
        c->tl05D1 = r.dJd;
        c->tl05R1 = r.rate;
    }});
    steps->append({300, [](Ctx *c) {
        if (c->stop || !StelApp::isInitialized() || !c->core())
            return;
        c->core()->increaseTimeSpeed();
        c->note(QStringLiteral("TL-05 increaseTimeSpeed（= INTERACTCHECK IT-05 注入的 L 键那条路）："
                               "%1 ⇒ %2")
                    .arg(c->tl05R1, 0, 'g', 9)
                    .arg(c->core()->getTimeRate(), 0, 'g', 9));
    }});
    steps->append({1000, [](Ctx *c) {
        if (c->stop || !StelApp::isInitialized() || !c->core())
            return;
        c->arm(1000.0);   // 名义窗 1000 ms（窗口就绪门的分母）
    }});
    steps->append({0, [](Ctx *c) {
        if (c->stop || !StelApp::isInitialized() || !c->core())
            return;
        const Ctx::Reading r = c->take();
        if (r.suspect) return;   // 窗口被调度拖长 ⇒ 本窗不可判（见 take 内的就绪门）
        c->tl05D2 = r.dJd;
        c->tl05R2 = r.rate;
        c->core()->increaseTimeSpeed();
        c->tl05Ladder3 = c->core()->getTimeRate();

        const double ratio = (std::fabs(c->tl05D1) > 1e-12) ? c->tl05D2 / c->tl05D1 : 0.0;
        const double rateRatio = (std::fabs(c->tl05R1) > 1e-12) ? c->tl05R2 / c->tl05R1 : 0.0;
        const bool ladderOk = std::fabs(c->tl05R1 - 0.1) <= 1e-12
                              && std::fabs(c->tl05R2 - 1.0) <= 1e-9
                              && std::fabs(c->tl05Ladder3 - 10.0) <= 1e-6;
        c->note(QStringLiteral("TL-05 阶梯台账：%1 → %2 → %3（×10 阶梯，低档吸附 JD_SECOND）；"
                               "同长窗口 ΔJD：%4 → %5")
                    .arg(c->tl05R1, 0, 'g', 9).arg(c->tl05R2, 0, 'g', 9)
                    .arg(c->tl05Ladder3, 0, 'g', 9)
                    .arg(c->tl05D1, 0, 'f', 6).arg(c->tl05D2, 0, 'f', 6));
        c->note(QStringLiteral("TL-05 ⇒ 这条解释了 T27 的「engineRate 读数=10」：INTERACTCHECK 的 "
                               "IT-05 注入 L 键把 rate 从 0.1 提到 1（第 2 次到 10），"
                               "所以 DIAG9 打出来的读数与相位设计速率本就不是一个东西。"));
        c->mark(ladderOk && std::fabs(ratio / rateRatio - 1.0) <= 0.40,
                QStringLiteral("TL-05 速率阶梯 + 链路跟变：阶梯 {0.1,1,10} 成立 ∧ "
                               "ΔJD 之比/rate 之比=%1（±40%）")
                    .arg(rateRatio > 1e-12 ? ratio / rateRatio : 0.0, 0, 'f', 4));
    }});

    // ── 步骤 27..29：TL-07 恒星时腿（绝对恒等式 + 下游重算）────────────────
    // 窗口取 4s（ΔJD≈0.2 天 ⇒ ΔLST≈72°）：恒星时走 `getJD()`（上一帧快照）
    // 这条路，与真源有一帧之差的抖动；窗口越长，这一帧的相对影响越小。
    steps->append({400, [](Ctx *c) {
        if (c->stop || !StelApp::isInitialized() || !c->core())
            return;
        c->core()->setTimeRate(0.05);
    }});
    steps->append({4000, [](Ctx *c) {
        if (c->stop || !StelApp::isInitialized() || !c->core())
            return;
        if (std::fabs(c->core()->getTimeRate() - 0.05) > 1e-12)
            c->core()->setTimeRate(0.05);
        c->arm(4000.0);   // 名义窗 4000 ms（窗口就绪门的分母）
    }});
    steps->append({0, [](Ctx *c) {
        if (c->stop || !StelApp::isInitialized() || !c->core())
            return;
        const Ctx::Reading r = c->take();
        if (r.suspect) return;   // 窗口被调度拖长 ⇒ 本窗不可判（见 take 内的就绪门）
        const double resid = std::fabs(r.dLstDeg - r.dJd * kSiderealDegPerDay);
        // 容差 = max(1.0°, 期望角的 0.1%)。两项各有分工：
        //   · 1.0° 的固定项覆盖「上一帧快照」抖动（`getLocalSiderealTime` 走
        //     `getJD()`，与真源差 ≤ 一帧 ⇒ ≤ rate×20ms 的转角，实测 <0.4°）；
        //   · 0.1% 的比例项把**系统性比例错误**钉死 —— 恒星日/太阳日搞混是
        //     0.27%，超过 0.1% ⇒ 必红（这是绝对腿的判别力所在）。
        const double expectDeg = r.dJd * kSiderealDegPerDay;
        const double tolDeg = std::max(1.0, std::fabs(expectDeg) * 0.001);
        c->mark(resid <= tolDeg && r.altAzAngleDeg > 10.0,
                QStringLiteral("TL-07 恒星时腿：W=%1s ΔJD=%2 天 ⇒ ΔLST=%3° 期望 ΔJD×360.9856=%4°"
                               "（残差=%5° ≤%6°）∧ 固定 J2000 方向经引擎 j2000ToAltAz 落点转过 "
                               "%7°（>10°，证明下游真的重算了）")
                    .arg(r.w, 0, 'f', 4).arg(r.dJd, 0, 'f', 6)
                    .arg(signF(r.dLstDeg, 4))
                    .arg(signF(expectDeg, 4))
                    .arg(signF(resid, 4))
                    .arg(tolDeg, 0, 'f', 2)
                    .arg(r.altAzAngleDeg, 0, 'f', 3));
        c->note(QStringLiteral("TL-07 语义：这是**绝对腿**——恒星时是 JD 的天文函数（走 "
                               "Planet::getSiderealTime 这条路，不是复述 simClock），"
                               "360.9856°/天 把「天」这个单位钉死在画面上（T30 铁律）。"));
    }});

    // ── 步骤 30：收尾还原 ────────────────────────────────────────────────
    steps->append({0, [](Ctx *c) {
        if (c->stop || !StelApp::isInitialized() || !c->core())
            return;
        c->core()->setSimClockScale(c->scaleSave);
        c->core()->setTimeRate(c->rateSave);
        c->note(QStringLiteral("收尾：已还原 rate=%1 scale=%2 ⇒ 回读 rate=%3 scale=%4")
                    .arg(c->rateSave, 0, 'g', 9)
                    .arg(c->scaleSave, 0, 'g', 3)
                    .arg(c->core()->getTimeRate(), 0, 'g', 9)
                    .arg(c->core()->getSimClockScale(), 0, 'g', 3));
    }});

    (*tick)();
#endif
}

} // namespace stelapp
