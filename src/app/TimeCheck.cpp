/*
 * TimeCheck — 实现（T19「改时间」环）。判据清单与设计理由见头注。
 *
 * 驱动方式：与 LocateCheck 相同的**线性步骤表**。每步 = (跑完后等多少 ms, 步骤体)；
 * 步骤体可改写 `c->nextDelay` 改变这一次的等待。**不**让步骤体自己再挂定时器
 * （那会变成两条路都在往前推，同一处理器被跑两遍 —— LocateCheck 初稿踩过）。
 *
 * 时间线（全部 GUI 线程；总时长约 8s）：
 *   1  ∪{delayMs} 挑 fixture；暂停时钟；TC-01/02/03 往返恒等；TC-04 互不相同
 *   2  ∪100   TC-05 与旧公式逐位一致；TC-06 UTC 往返；TC-07 偏移契约；
 *             TC-08 非法输入被拒且无副作用；TC-14 超范围被拒
 *   3  ∪1200  TC-09a 记 ref 与 jdRef，跳 +0.25 天（等 1200ms 让引擎重算）
 *   4  ∪0     TC-09b 目标 AltAz 变化 > 5°
 *   5  ∪1200  TC-10a 记此刻 AltAz，**写回同一个 JD**
 *   6  ∪0     TC-10b AltAz 变化 < 0.05°（判别性对照）
 *   7  ∪1200  TC-11a 跳回 jdRef（等 1200ms）
 *   8  ∪0     TC-11b AltAz 复原 < 0.05°（可逆性）
 *   9  ∪0     TC-12 跳 1900-01-01（差值必 > 100 天）→ setTimeNow → < 1e-3 天
 *   10 ∪600   恢复运行态，记 jdPre，启动秒表，跳 +0.5 天
 *   11 ∪0     TC-13 JD 停在"跳转目标 + 实测墙钟 × 速率"上（没被拽回）
 */
#include "app/TimeCheck.hpp"

#include "app/AppFacade.hpp"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTimer>
#include <QVector>

#include <cmath>

#if defined(STELQUICK_HAS_ENGINE)
#include "StelApp.hpp"
#include "StelCore.hpp"
#include "StelObject.hpp"
#include "StelObjectMgr.hpp"
#include "StelUtils.hpp"
#include "VecMath.hpp"
#endif

namespace stelapp {

namespace {

#if defined(STELQUICK_HAS_ENGINE)

//! 判据门槛（改这里必须同步改头注与文档）。
const double kFormulaTolDay = 1e-9;    //!< 与旧对话框公式的逐位比对容差（≈0.09 ms）
const double kUtcRoundTripDay = 1.5e-5; //!< JD 域往返容差。**不是随手取的**：
                                        //!< 本地字段只到整数秒，回写必然截断掉小数秒，
                                        //!< 故下界就是 1 秒 = 1.157e-5 天。取 1.5e-5（≈1.3 s）
                                        //!< 刚好覆盖截断、又足够窄到能抓住"差一个时区"（8 h = 0.33 天）。
const double kWorldMoveFloorDeg = 5.0;  //!< "世界确实动了"：AltAz 变化必须大于这个值
//! 改时间后"等引擎把新时刻算进去"的墙钟毫秒（判据必须等它）。
//! ⚠️ 驱动器的语义是"跑完本步之后等 delayAfter 毫秒"，所以这个等待**必须挂在
//! **写入步**上，不能挂在读取步上——挂错位置会让读取发生在写入后 0ms，
//! 读到的是**上一个时刻**的星空，于是"世界没动"变成假失败（T19 首跑即踩此坑：
//! 诊断耗时戳显示写入→读取实测间隔 0ms，而应有的等待跑到了下一步去）。
const int kWorldSettleMs = 1200;

const double kNoOpCeilDeg = 0.05;       //!< 判别性对照：写回同一 JD 时不许动
const double kTimeJumpDays = 0.25;      //!< 世界动一动用：跳 6 小时（自转 90°）
const double kNowTolDay = 1e-3;         //!< "现在"的容差（≈86 s，留给写入与读出的间隔）
const double kNowContrastDay = 100.0;   //!< "现在"的判别性对照：跳开的差值必须大于这个
const double kClawbackJumpDays = 0.5;   //!< T13 跳转量：足够大到"被拽回"一眼可辨

//! fixture 候选，按优先级。月球排第一：A4 固定流程 I-REP-02 就是"搜月球"。
const char *const kFixtureCandidates[] = {"Moon", "Jupiter", "Sirius", "Vega", "Polaris"};

#endif

struct Step
{
    int delayAfter;
    std::function<void(struct Ctx *)> body;
};

struct Ctx
{
    QCoreApplication *app = nullptr;
    AppFacade *facade = nullptr;
    std::function<void(const TimeCheck::Result &)> onDone;

    TimeCheck::Result result;
    bool ok = true;
    int nextDelay = 0;         //!< 本步结束后等多久进下一步（步骤体可改写）

    // 输出纪律：**只往 result.details 累积，本文件不直接打印**（与 T17/T18 一致）。
    // 理由见 LocateCheck 的同名注释：既 qDebug 又 append 会让每条判据在证据里出现两遍。
    void mark(const QString &line)
    {
        result.details.append(line);
        ++result.total;
        if (line.endsWith(QStringLiteral("FAIL")))
            ok = false;
        else if (line.endsWith(QStringLiteral("OK")))
            ++result.passed;
    }

    //! 只记录、不计入判据数（信息行 / 照实 SKIP）。
    void note(const QString &line) { result.details.append(line); }

#if defined(STELQUICK_HAS_ENGINE)
    StelCore *core() const
    {
        return StelApp::isInitialized() ? StelApp::getInstance().getCore() : nullptr;
    }
    StelObjectMgr *objMgr() const
    {
        return StelApp::isInitialized() ? &StelApp::getInstance().getStelObjectMgr() : nullptr;
    }
    const StelObjectP selected() const
    {
        StelObjectMgr *m = objMgr();
        if (!m)
            return StelObjectP();
        const QList<StelObjectP> &sel = m->getSelectedObject();
        return (sel.isEmpty() || !sel.first()) ? StelObjectP() : sel.first();
    }
    //! 目标当前的地平坐标单位向量（用来量"星空动了多少"）。
    Vec3d objAltAz() const
    {
        StelCore *c = core();
        const StelObjectP obj = selected();
        if (!c || !obj)
            return Vec3d(0.);
        Vec3d v = obj->getAltAzPosAuto(c);
        v.normalize();
        return v;
    }
#endif

    //! 读回**本地日历**的 6 个字段（0=年 … 5=秒）。这是"往对话框里回填"的那条路。
    QVector<int> localFields() const
    {
        QVector<int> f(6);
        for (int i = 0; i < 6; ++i)
            f[i] = facade->localDateTimeField(i);
        return f;
    }

    // ---- 跨步骤传递的状态 ----
    QString fixtureName;
    QString fixtureSid;
    bool haveFixture = false;

    QVector<int> rt[3];             //!< TC-01/02/03 的读回字段
    QVector<int> written[3];        //!< 对应写入的输入字段
    double jdRef = 0.0;             //!< 世界动一动用的参考时刻
#if defined(STELQUICK_HAS_ENGINE)
    Vec3d altAzRef = Vec3d(0.);
    Vec3d altAzNow = Vec3d(0.);
#endif
    double jdPreJump = 0.0;
    double jdJumpTarget = 0.0;
    QElapsedTimer stopwatch;

    void finish()
    {
        result.ran = true;
        result.pass = ok;
        result.summary = ok
            ? QStringLiteral("T19 改时间自检全过（往返恒等/公式一致/UTC 往返/非法拒绝/"
                             "星空随动/对照不动/可逆/现在/不被拽回）")
            : QStringLiteral("T19 改时间自检存在失败项（见明细）");
        onDone(result);
    }

    //! 输入字段 → 可读串（判据行的输出用）。
    static QString ftext(const QVector<int> &f)
    {
        return QStringLiteral("%1-%2-%3 %4:%5:%6")
            .arg(f[0], 4, 10, QLatin1Char('0'))
            .arg(f[1], 2, 10, QLatin1Char('0'))
            .arg(f[2], 2, 10, QLatin1Char('0'))
            .arg(f[3], 2, 10, QLatin1Char('0'))
            .arg(f[4], 2, 10, QLatin1Char('0'))
            .arg(f[5], 2, 10, QLatin1Char('0'));
    }
};

#if defined(STELQUICK_HAS_ENGINE)

//! 经 AppFacade 公共 API 挑一个可用 fixture（与 LocateCheck 同一候选表/同一口径）。
bool pickFixture(AppFacade *facade, QString *sidOut)
{
    for (const char *want : kFixtureCandidates)
    {
        const QString name = QString::fromLatin1(want);
        const int n = facade->searchObjects(name, 8);
        for (int i = 0; i < n; ++i)
        {
            if (!facade->selectSearchResult(i))
                continue;
            const QString en = facade->objectInfo()->englishName();
            const QString dn = facade->objectInfo()->displayName();
            if (en.compare(name, Qt::CaseInsensitive) != 0
                && dn.compare(name, Qt::CaseInsensitive) != 0)
                continue;
            const QString sid = facade->objectInfo()->stableId();
            if (sid.isEmpty())
                continue;
            *sidOut = sid;
            return true;
        }
    }
    return false;
}

//! 一组"往返恒等"判据：写入 @p in，读回 6 字段，逐字段必须相等。返回读回值。
QVector<int> roundTrip(Ctx *c, const QVector<int> &in)
{
    const bool wrote = c->facade->setLocalDateTime(in[0], in[1], in[2],
                                                   in[3], in[4], in[5]);
    Q_UNUSED(wrote);
    return c->localFields();
}

#endif

} // namespace

void TimeCheck::run(QCoreApplication *app,
                    AppFacade *facade,
                    const std::function<void(const Result &)> &onDone,
                    int delayMs)
{
    auto *ctx = new Ctx;
    ctx->app = app;
    ctx->facade = facade;
    ctx->onDone = onDone;

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

#if !defined(STELQUICK_HAS_ENGINE)
    // 无引擎形态：时间写入全部 no-op。照实报 UNAVAILABLE，不伪装成"验过了"。
    steps->append({0, [](Ctx *c) {
        c->result.unavailable = true;
        c->note(QStringLiteral("TIMECHECK: 本构建未链接引擎（STELQUICK_HAS_ENGINE 未定义），"
                               "时间写入全为 no-op —— 记 UNAVAILABLE"));
    }});
#else
    // ── 步骤 1：挑 fixture、暂停时钟、三组往返恒等 ─────────────────────────
    steps->append({delayMs, [](Ctx *c) {
        c->haveFixture = pickFixture(c->facade, &c->fixtureSid);
        if (c->haveFixture)
            c->note(QStringLiteral("TIMECHECK-note fixture = %1（stableId=%2）")
                        .arg(c->facade->objectInfo()->displayName(), c->fixtureSid));
        else
            c->note(QStringLiteral("TIMECHECK-note 未取到 fixture —— TC-09/10/11 照实 SKIP"));

        // 暂停时钟（simClock.scale=0：帧泵照跑、JD 冻结）。
        // 自检环境 simRate=0.1 天/秒 ⇒ 跨步骤的 300 ms 等待会带来 0.03 天（43 分钟）
        // 的自然漂移，足以把"往返恒等"这种子毫秒级判据整个淹掉。冻结后再比。
        c->facade->setSimulationPaused(true);
        c->note(QStringLiteral("TIMECHECK-note 时钟已暂停（scale=0），时间判据在冻结区执行；"
                               "simRate=%1 天/秒")
                    .arg(c->core()->getTimeRate(), 0, 'f', 4));

        // ── TC-01/02/03：往返恒等。三组覆盖"常规 / 跨年边界 / 闰日"。──────
        static const int kInputs[3][6] = {
            {2026,  9, 24, 14, 30,  0},   // 常规日期
            {2026, 12, 31, 23, 59, 59},   // 跨年边界（自造日历最先在这里翻车）
            {2024,  2, 29, 12,  0,  0},   // 闰日
        };
        static const char *const kNames[3] = {"常规日期", "跨年边界", "闰日"};  // UTF-8，取值处用 fromUtf8
        static const char *const kIds[3] = {"TC-01", "TC-02", "TC-03"};

        for (int k = 0; k < 3; ++k)
        {
            QVector<int> in(6);
            for (int i = 0; i < 6; ++i)
                in[i] = kInputs[k][i];
            c->written[k] = in;
            c->rt[k] = roundTrip(c, in);
            bool same = (c->rt[k].size() == 6);
            for (int i = 0; same && i < 6; ++i)
                same = (c->rt[k][i] == in[i]);
            c->mark(QStringLiteral("%1 往返恒等·%2：写 %3 → 读回 %4：%5")
                        // ⚠️ 必须 fromUtf8：这两个表是 UTF-8 的 `const char*`，
                        // 用 fromLatin1 会把「常规日期」显示成「å¸¸è§æ¥æ」（T19 首跑实测）。
                        .arg(QString::fromUtf8(kIds[k]), QString::fromUtf8(kNames[k]),
                             Ctx::ftext(in), Ctx::ftext(c->rt[k]),
                             same ? QStringLiteral("OK") : QStringLiteral("FAIL")));
        }

        // ── TC-04：判别性对照 —— 三组读回必须**彼此不同** ──────────────────
        // 否则"读回"可能是个常量（那三条往返判据就全成了假绿）。
        {
            bool distinct = true;
            for (int a = 0; a < 3 && distinct; ++a)
                for (int b = a + 1; b < 3 && distinct; ++b)
                    if (c->rt[a] == c->rt[b])
                        distinct = false;
            c->mark(QStringLiteral("TC-04 读回确实依赖输入（三组读回互不相同：%1 / %2 / %3）：%4")
                        .arg(Ctx::ftext(c->rt[0]), Ctx::ftext(c->rt[1]), Ctx::ftext(c->rt[2]),
                             distinct ? QStringLiteral("OK") : QStringLiteral("FAIL")));
        }
    }});

    // ── 步骤 2：公式一致 / UTC 往返 / 偏移契约 / 非法拒绝 / 超范围 ──────────
    steps->append({100, [](Ctx *c) {
        StelCore *core = c->core();

        // ── TC-05：与旧 DateTimeDialog::newJd() 的公式逐位比对 ─────────────
        // 这是"没有重新发明天文/日历算法"的**机械证据**：如果本层自己写了一版
        // 儒略日公式，它迟早会在这个容差下现形（容差 1e-9 天 ≈ 0.09 ms）。
        {
            c->facade->setLocalDateTime(2026, 9, 24, 14, 30, 0);
            const double got = c->facade->julianDay();
            double want = 0.0;
            StelUtils::getJDFromDate(&want, 2026, 9, 24, 14, 30, 0.f);
            want -= core->getUTCOffset(want) / 24.0;   // 本地 → UT，与旧对话框逐字一致
            const double diff = std::fabs(got - want);
            c->mark(QStringLiteral("TC-05 与旧对话框公式逐位一致（本层 %1 vs 旧公式 %2，"
                                   "差 %3 天；容差 %4）：%5")
                        .arg(got, 0, 'f', 9).arg(want, 0, 'f', 9)
                        .arg(diff, 0, 'e', 3).arg(kFormulaTolDay, 0, 'e', 1)
                        .arg(diff <= kFormulaTolDay ? QStringLiteral("OK")
                                                    : QStringLiteral("FAIL")));
        }

        // ── TC-06：UTC 往返（读出本地字段 → 原样写回 → JD 必须复原）───────
        // 这条与 TC-01..03 是**同一个病的两个方向**：
        //   TC-01..03 是"字段域"往返（写字段→读字段）；
        //   TC-06     是"JD 域"往返（读字段→写字段→比 JD）。
        // 若写/读的 `±offset` 方向有一个搞反，JD 会整体偏 2×offset（这台机 16 h = 0.67 天），
        // 远超容差。字段域往返抓不到方向错（因为两边同错、自己抵消），这条能。
        {
            const double jd0 = c->facade->julianDay();
            const QVector<int> f = c->localFields();
            c->facade->setLocalDateTime(f[0], f[1], f[2], f[3], f[4], f[5]);
            const double jd1 = c->facade->julianDay();
            const double d = std::fabs(jd1 - jd0);
            const double off = c->facade->utcOffsetHours();
            c->mark(QStringLiteral("TC-06 UTC 往返（偏移 %1 h）：读本地字段 %2 → 原样写回 → "
                                   "ΔJD = %3 天（容差 %4；整数秒截断下界 1.16e-5）：%5")
                        .arg(off, 0, 'f', 2).arg(Ctx::ftext(f))
                        .arg(d, 0, 'e', 3).arg(kUtcRoundTripDay, 0, 'e', 1)
                        .arg(d <= kUtcRoundTripDay ? QStringLiteral("OK")
                                                   : QStringLiteral("FAIL")));
            if (std::fabs(off) < 1e-9)
                c->note(QStringLiteral("TIMECHECK-note 当前时区偏移为 0 —— TC-06 此时**不敏感**"
                                       "（它靠非零偏移才有判别力），读数照记但请结合证据里的"
                                       "偏移值解读"));
        }

        // ── TC-07：偏移契约（本层读的偏移必须就是引擎给的）────────────────
        {
            const double jd = c->facade->julianDay();
            const double viaFacade = c->facade->utcOffsetHours();
            const double viaCore = core->getUTCOffset(jd);
            c->mark(QStringLiteral("TC-07 utcOffsetHours 与 core->getUTCOffset 一致"
                                   "（%1 vs %2 h，时区=%3）：%4")
                        .arg(viaFacade, 0, 'f', 6).arg(viaCore, 0, 'f', 6)
                        .arg(core->getCurrentTimeZone())
                        .arg(std::fabs(viaFacade - viaCore) < 1e-9 ? QStringLiteral("OK")
                                                                   : QStringLiteral("FAIL")));
        }

        // ── TC-08：非法输入被拒 —— 且**无副作用** ─────────────────────────
        // "拒绝"必须是可观测的，不只是"返回 false"：时钟一个字节都不许动。
        {
            static const int kBad[3][6] = {
                {2026, 13, 24, 12,  0, 0},   // 13 月
                {2026,  9, 32, 12,  0, 0},   // 32 日
                {2026,  9, 24, 25,  0, 0},   // 25 时
            };
            static const char *const kWhat[3] = {"13 月", "32 日", "25 时"};  // UTF-8，取值处用 fromUtf8
            const double jdBefore = c->facade->julianDay();
            QStringList saw;
            bool allRejected = true;
            for (int k = 0; k < 3; ++k)
            {
                QVector<int> in(6);
                for (int i = 0; i < 6; ++i)
                    in[i] = kBad[k][i];
                const bool landed = c->facade->setLocalDateTime(in[0], in[1], in[2],
                                                                in[3], in[4], in[5]);
                const QString tok = c->facade->lastTimeRefusal();
                if (landed || tok != QStringLiteral("invalid-date"))
                    allRejected = false;
                saw << QStringLiteral("%1→%2").arg(QString::fromUtf8(kWhat[k]), tok);
            }
            const double drift = std::fabs(c->facade->julianDay() - jdBefore);
            c->mark(QStringLiteral("TC-08 非法输入被拒且无副作用（%1；时钟 ΔJD=%2 天）：%3")
                        .arg(saw.join(QStringLiteral("、")))
                        .arg(drift, 0, 'e', 3)
                        .arg((allRejected && drift < 1e-9) ? QStringLiteral("OK")
                                                           : QStringLiteral("FAIL")));
        }

        // ── TC-14：超范围 JD 被拒（且无副作用）────────────────────────────
        {
            const double jdBefore = c->facade->julianDay();
            const bool landed = c->facade->setJulianDay(1.0e12);
            const QString tok = c->facade->lastTimeRefusal();
            const double drift = std::fabs(c->facade->julianDay() - jdBefore);
            c->mark(QStringLiteral("TC-14 超范围 JD 被拒（setJulianDay(1e12) → %1 / 理由 %2；"
                                   "时钟 ΔJD=%3 天）：%4")
                        .arg(landed ? QStringLiteral("true") : QStringLiteral("false"), tok)
                        .arg(drift, 0, 'e', 3)
                        .arg((!landed && tok == QStringLiteral("out-of-range") && drift < 1e-9)
                                 ? QStringLiteral("OK") : QStringLiteral("FAIL")));
        }
    }});

    // ── 步骤 3：TC-09a 记参考时刻并跳 +6h（等待挂在**本步**：跳完要留时间给引擎重算）
    steps->append({kWorldSettleMs, [](Ctx *c) {
        if (!c->haveFixture)
            return;
        c->jdRef = c->facade->julianDay();
#if defined(STELQUICK_HAS_ENGINE)
        c->altAzRef = c->objAltAz();
#endif
        c->facade->setJulianDay(c->jdRef + kTimeJumpDays);
    }});

    // ── 步骤 4：TC-09b 星空真的动了（本步只读，不等）─────────────────────
    steps->append({0, [](Ctx *c) {
        if (!c->haveFixture)
        {
            c->note(QStringLiteral("TC-09 SKIP（无 fixture，无法量目标 AltAz 变化）"));
            c->note(QStringLiteral("TC-10 SKIP（同上）"));
            c->note(QStringLiteral("TC-11 SKIP（同上）"));
            return;
        }
#if defined(STELQUICK_HAS_ENGINE)
        const Vec3d a1 = c->objAltAz();
        const double moved = c->altAzRef.angle(a1) * 180.0 / M_PI;
        c->mark(QStringLiteral("TC-09 世界确实动了（暂停态跳 %1 天后等 %2ms，"
                               "目标 AltAz 变化 %3°，门槛 >%4）：%5")
                    .arg(kTimeJumpDays, 0, 'f', 2).arg(kWorldSettleMs)
                    .arg(moved, 0, 'f', 4)
                    .arg(kWorldMoveFloorDeg, 0, 'f', 1)
                    .arg(moved > kWorldMoveFloorDeg ? QStringLiteral("OK")
                                                    : QStringLiteral("FAIL")));
#endif
    }});

    // ── 步骤 5：TC-10a 写回**同一个** JD（对照；等待同样挂在本写入步）──────
    steps->append({kWorldSettleMs, [](Ctx *c) {
        if (!c->haveFixture)
            return;
#if defined(STELQUICK_HAS_ENGINE)
        c->altAzNow = c->objAltAz();
#endif
        const double jd = c->facade->julianDay();
        c->facade->setJulianDay(jd);   // 同一个值
    }});

    // ── 步骤 6：TC-10b 对照：星空必须不动（本步只读，不等）───────────────
    steps->append({0, [](Ctx *c) {
        if (!c->haveFixture)
            return;
#if defined(STELQUICK_HAS_ENGINE)
        const double d = c->altAzNow.angle(c->objAltAz()) * 180.0 / M_PI;
        c->mark(QStringLiteral("TC-10 判别性对照：写回同一 JD 后等 %1ms，AltAz 变化 %2°"
                               "（上限 <%3）—— 证明 TC-09 的\"动了\"不是测量噪声：%4")
                    .arg(kWorldSettleMs).arg(d, 0, 'f', 4).arg(kNoOpCeilDeg, 0, 'f', 2)
                    .arg(d < kNoOpCeilDeg ? QStringLiteral("OK") : QStringLiteral("FAIL")));
#endif
    }});

    // ── 步骤 7：TC-11a 跳回参考时刻（等待挂在本写入步）────────────────────
    steps->append({kWorldSettleMs, [](Ctx *c) {
        if (!c->haveFixture)
            return;
        c->facade->setJulianDay(c->jdRef);
    }});

    // ── 步骤 8：TC-11b 可逆：星空必须复原（本步只读，不等）───────────────
    steps->append({0, [](Ctx *c) {
        if (!c->haveFixture)
            return;
#if defined(STELQUICK_HAS_ENGINE)
        const double d = c->altAzRef.angle(c->objAltAz()) * 180.0 / M_PI;
        c->mark(QStringLiteral("TC-11 可逆：跳回原时刻后等 %1ms，AltAz 复原（残差 %2°，"
                               "上限 <%3）—— 排除\"改时间把状态写坏了\"：%4")
                    .arg(kWorldSettleMs).arg(d, 0, 'f', 4).arg(kNoOpCeilDeg, 0, 'f', 2)
                    .arg(d < kNoOpCeilDeg ? QStringLiteral("OK") : QStringLiteral("FAIL")));
#endif
    }});

    // ── 步骤 9：TC-12 "现在"（含判别性对照）──────────────────────────────
    steps->append({0, [](Ctx *c) {
        StelCore *core = c->core();
        // 先跳到 1900-01-01 —— 让"现在"这条判据**有东西可判别**。
        double jd1900 = 0.0;
        StelUtils::getJDFromDate(&jd1900, 1900, 1, 1, 0, 0, 0.f);
        c->facade->setJulianDay(jd1900);
        const double before = std::fabs(c->facade->julianDay() - StelUtils::getJDFromSystem());
        c->facade->setTimeNow();
        const double after = std::fabs(c->facade->julianDay() - StelUtils::getJDFromSystem());
        const bool contrast = before > kNowContrastDay;
        c->mark(QStringLiteral("TC-12 \"现在\"：跳开后与系统时刻差 %1 天（>%2 的对照成立=%3），"
                               "setTimeNow 后差 %4 天（容差 %5）：%6")
                    .arg(before, 0, 'f', 2).arg(kNowContrastDay, 0, 'f', 0)
                    .arg(contrast ? QStringLiteral("true") : QStringLiteral("false"))
                    .arg(after, 0, 'e', 3).arg(kNowTolDay, 0, 'e', 1)
                    .arg((contrast && after < kNowTolDay) ? QStringLiteral("OK")
                                                          : QStringLiteral("FAIL")));
        Q_UNUSED(core);
    }});

    // ── 步骤 10：TC-13 前半 —— 恢复运行态，跳 +0.5 天，起表 ──────────────
    steps->append({600, [](Ctx *c) {
        c->facade->setSimulationPaused(false);   // 必须有推进源，这条才有意义
        c->jdPreJump = c->facade->julianDay();
        c->jdJumpTarget = c->jdPreJump + kClawbackJumpDays;
        c->stopwatch.start();
        c->facade->setJulianDay(c->jdJumpTarget);
    }});

    // ── 步骤 11：TC-13 判 —— JD 停在"目标 + 实测漂移"上 ─────────────────
    steps->append({0, [](Ctx *c) {
        const double elapsed = c->stopwatch.elapsed() / 1000.0;
        const double rate = c->core()->getTimeRate();       // 天/秒（HostDriven 口径）
        const double expect = c->jdJumpTarget + elapsed * rate;
        const double jdAfter = c->facade->julianDay();
        const double err = std::fabs(jdAfter - expect);
        // 容忍度：实测漂移的 20% + 0.02 天（定时器抖动 + 一帧的推进）。
        const double tol = 0.20 * std::fabs(elapsed * rate) + 0.02;
        // 关键的反向断言：**没被拽回**。若宿主把 JD 当闭式公式的产物（T15 的旧写法），
        // 这里会看到 jdAfter 弹回 jdPreJump 附近，于是"停在目标上"这一条必红。
        const bool notClawedBack = (jdAfter - c->jdPreJump) > 0.4 * kClawbackJumpDays;
        c->mark(QStringLiteral("TC-13 写入不被帧泵拽回（运行态跳 %1 天，实测 %2 s × %3 = "
                               "预期漂移 %4 天）：JD 差值 %5，与\"目标+漂移\"差 %6 天"
                               "（容差 %7）；未被拽回=%8：%9")
                    .arg(kClawbackJumpDays, 0, 'f', 2)
                    .arg(elapsed, 0, 'f', 3).arg(rate, 0, 'f', 4)
                    .arg(elapsed * rate, 0, 'e', 2)
                    .arg(jdAfter - c->jdPreJump, 0, 'f', 4)
                    .arg(err, 0, 'e', 2).arg(tol, 0, 'e', 2)
                    .arg(notClawedBack ? QStringLiteral("true") : QStringLiteral("false"))
                    .arg((err <= tol && notClawedBack) ? QStringLiteral("OK")
                                                       : QStringLiteral("FAIL")));
        // 收尾：把暂停恢复回去，免得后续（若有）误读"时钟还在跑"。
        c->facade->setSimulationPaused(true);
    }});
#endif  // STELQUICK_HAS_ENGINE

    QTimer::singleShot(0, app, [tick]() { (*tick)(); });
}

} // namespace stelapp
