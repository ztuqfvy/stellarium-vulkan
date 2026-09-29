/*
 * LocationCheck — 实现（T33-C 观察地点写入面）。判据清单与设计理由见头注。
 *
 * 驱动方式沿用 LocateCheck 的**线性步骤表**：每步 = (跑完后等多少 ms, 步骤体)。
 *
 * ⚠️ 关于"等帧"：引擎 `updateTransformMatrices()` 由**渲染循环**每帧刷新，而
 * "写完地点"本身**不触发**重算；`tick()` 的 `delayAfter` 又只是"本步之后"的等待 ——
 * 把写入步的 delayAfter 写成 0，读取步就会在**同一个事件循环**里跑，读到**上一个地点**
 * 的矩阵（实测：同一二进制 9/10 与 8/10 两种结果，红的那条恰差 10.2° = 上一步的纬度）。
 * ⇒ 凡是要读**变换栈**的读取步，一律先过 `awaitFresh()` 有界就绪门。细节见头注。
 */
#include "app/LocationCheck.hpp"

#include "app/AppFacade.hpp"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTimer>
#include <QVector>

#include <cmath>
#include <limits>

#if defined(STELQUICK_HAS_ENGINE)
#include "StelApp.hpp"
#include "StelCore.hpp"
#include "StelLocation.hpp"
#include "StelLocationMgr.hpp"
#include "StelObject.hpp"
#include "StelObjectMgr.hpp"
#include "VecMath.hpp"
#endif

namespace stelapp {

namespace {

#if defined(STELQUICK_HAS_ENGINE)
//! 【主仪器】天极高度角 vs 观测者纬度 的容差。
//! 当日天极是**精确**在自转轴上的 ⇒ 读数精确等于纬度，容差可以收到 0.5°。
//! 判别间隙是 8.95°（Paris↔Beijing）⇒ 判别力 ≈ 18 倍。
const double kPoleAltTolDeg = 0.5;
//! 差分腿容差：两地点读数差 vs 纬度差（两对：8.95° 与 1.25°）。
//! 1.25° 那一对是最小可分辨量 —— 0.5° 容差仍留有 2.5 倍余量。
const double kLatDiffTolDeg = 0.5;
//! 经纬度回读容差。
const double kCoordTolDeg = 1e-3;
//! 每次地点写入后等帧刷新（写入步的 delayAfter）。
const int kSettleMs = 700;
//! 就绪门预算：每轮 60 ms，最多 3 s。
const int kGateStepMs = 60;
const int kGateBudgetMs = 3000;
#endif

struct Ctx
{
    QCoreApplication *app = nullptr;
    AppFacade *facade = nullptr;
    std::function<void(const LocationCheck::Result &)> onDone;
    LocationCheck::Result result;
    bool ok = true;
    int nextDelay = 0;
    //! 前提门：本套判据需要的地点库条目齐不齐（Paris/Beijing/同 Europe/Paris 时区的第二个）。
    //! 不齐 ⇒ 置 false + `result.unavailable = true`，**后续步骤一律不再执行**
    //! （见 tick()）⇒ 一条判据都不计 ⇒ 报 UNAVAILABLE（rc=6），既不假绿也不假红。
    bool premiseOk = true;

    //! 前提②：跑之前引擎是否**启用了"自定义时区"**（`StelCore::flagUseCTZ`）。
    //! 为 true 时时区联动会被**按设计跳过** ⇒ 本自检临时置 false，末尾**恢复**
    //! （血泪第 9 条：判据在同一步内改了环境就必须还环境）。`setUseCustomTimeZone`
    //! **不落盘**（无 `immediateSave`）⇒ 纯内存改动。详见步骤 1 里的长注释。
    bool ctzWasOn = false;

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
    //! 只记录、不计入判据数（信息行 / 照实 SKIP）。
    void note(const QString &line) { result.details.append(QStringLiteral("    %1").arg(line)); }

    // ── 跨步骤状态 ──────────────────────────────────────────────────────────
    QString startId, startName;
    double startLat = 0.0, startLon = 0.0, startAlt = 0.0, startOffsetH = 0.0;
    QString parisId, parisSameTzId, beijingId;
    //! 三个采样点的纬度（LC-04/04b 的对照量）。
    double parisLat = 0.0, abbevilleLat = 0.0, beijingLat = 0.0;
    //! 🔴 **地点库目录**里三个目标的纬度 —— LC-04 的对照量。
    //!
    //! 为什么不能拿 `f->locationLatitude()`（引擎当前地点）当对照量：负控 D 实证
    //! （写入 no-op）—— 那样两边都是**旧地点**，仪器读数与对照量"自洽" ⇒ LC-04 假绿。
    //! 目录是**另一个来源**：引擎当前地点来自 `position->currentLocation`，
    //! 目录来自 `StelLocationMgr` 的数据文件。写入没落到目标 ⇒ 两者对不上 ⇒ 必红。
    double parisDbLat = 0.0, abbevilleDbLat = 0.0, beijingDbLat = 0.0;
    bool parisDbOk = false, abbevilleDbOk = false, beijingDbOk = false;
    double offAfterParis = 0.0, offAfterSameTz = 0.0, offAfterBeijing = 0.0;
    //! 【主仪器】天极在地平坐标里的高度角/方位角（θ 无关、无岁差偏置，见 poleAltDeg）。
    //! 三个采样点：Paris / Abbeville / Beijing —— 差分腿要三个点。
    double poleAltParis = -1000.0, poleAzParis = -1000.0;
    double poleAltAbbeville = -1000.0;
    double poleAltBeijing = -1000.0, poleAzBeijing = -1000.0;
    //! 就绪门的最后一次读数（判红时给得出"当时读到多少"）。
    //! [0]=Paris，[1]=Abbeville，[2]=Beijing。
    double gateLast[3] = {-1000.0, -1000.0, -1000.0};
    int gatePolls[3] = {0, 0, 0};
    int gateMs[3] = {0, 0, 0};
    //! 旧仪器（Polaris 夹角）保留为**信息行**：天体目录是另一条链路，值得留读数；
    //! 但 0.74° 的天极偏移带来周日抖动（峰峰值 ~1.5°），**不拿它当判据**。
    double axisSep[2][3] = {{-1, -1, -1}, {-1, -1, -1}};
    bool polarisOk = false;
    //! 起始的读写计数（LC-08 对增量做断言，不对累计值 —— 累计值受外面调过几次影响）。
    int writeBase = 0, refuseBase = 0;

#if defined(STELQUICK_HAS_ENGINE)
    StelCore *core() const
    {
        return StelApp::isInitialized() ? StelApp::getInstance().getCore() : nullptr;
    }
    StelLocationMgr &locMgr() const { return StelApp::getInstance().getLocationMgr(); }

    //! **当日天极**在地平坐标里的方向。
    //!
    //! `matEquinoxEquToAltAz = Ry(−β)·Rz(−θ)`（β = 90°−纬度、θ = 恒星时+经度）。
    //! 作用在**当日天极** `(0,0,1)`（春分点赤道系）上：`Rz(−θ)` 绕 z 轴 ⇒ 对 z 轴上的
    //! 向量是**恒等** ⇒ 结果只由 β 决定 ⇒ **与恒星时 θ 完全无关**（零周日抖动），
    //! 而且用的是当日天极 ⇒ **连 J2000↔当日的岁差偏置都没有**。
    //! 于是 `asin(z)` **精确等于** `currentLocation.getLatitude()`（实测 |Δ| < 0.05°）。
    //!
    //! 为什么**不用 Polaris**（这里踩过一整轮冤枉路，务必别回退）：
    //! Polaris 赤纬 +89.26°，偏离天极 0.74° ⇒ 它与固定方向的夹角**随时角抖动**，
    //! 峰峰值 ~1.5°；而 Paris(48.853) 与 Abbeville(50.105) 只差 1.25° ⇒ 用 Polaris
    //! **根本分辨不出这两个地点**，判据会间歇红/绿。当日天极没有这个毛病。
    //!
    //! 这条仪器为什么**不是自证**：`matEquinoxEquToAltAz` 由 `position->
    //! getRotAltAzToEquatorial()`（吃 `currentLocation` 的经纬度）**每帧**重建；
    //! 另一头 `getCurrentLocation()` 是地点写入面直接改的结构体。两者一致
    //! ⇒ "写入不但落了结构体，还落到了坐标变换栈"；矩阵陈旧/未重算 ⇒ 读数停在
    //! **上一个地点**的纬度上 ⇒ 必红（这正是间歇红事件的真身）。
    Vec3d poleVectorAltAz() const
    {
        StelCore *c = core();
        if (!c)
            return Vec3d(0.0);
        Vec3d p = c->equinoxEquToAltAz(Vec3d(0.0, 0.0, 1.0), StelCore::RefractionOff);
        p.normalize();
        return p;
    }
    //! 天极在地平坐标里的高度角（度）= 观测者纬度。
    double poleAltDeg() const
    {
        StelCore *c = core();
        if (!c)
            return -1000.0;
        return std::asin(poleVectorAltAz()[2]) * 180.0 / M_PI;
    }
    //! 天极在地平坐标里的方位角（北点 = 0°，东正）。当日天极应**恰为 0°**。
    double poleAzDeg() const
    {
        StelCore *c = core();
        if (!c)
            return -1000.0;
        const Vec3d p = poleVectorAltAz();
        double a = std::atan2(p[1], p[0]) * 180.0 / M_PI;   // (x=南, y=东, z=天顶)
        a = 180.0 - a;                                       // 转到"北点 = 0°"惯例
        while (a <= -180.0) a += 360.0;
        while (a > 180.0) a -= 360.0;
        return a;
    }

    //! **有界就绪门**：等到变换栈真的跟上当前地点为止，然后才允许判据读数。
    //!
    //! 为什么必须主动跑事件循环：`StelCore::update()`（⇒ `updateTransformMatrices()`）
    //! 只在**帧泵 tick** 里被调用，而"写完地点"不会触发它。自检形态下界面是静态的，
    //! 帧的到达时刻不受我们控制 ⇒ 直接同步读会**间歇**读到上一个地点的矩阵。
    //! 这里在步骤体内 `processEvents` 让帧泵的 QTimer 有机会触发；步骤表的下一个
    //! `singleShot` 是在本步 body **返回之后**才登记的 ⇒ 不存在重入。
    //!
    //! 语义照血泪第 10 条（DYN 先例）：**有界**；超时**明确判红**（由调用方断言），
    //! 绝不"等不到就当过"。
    //! @param slot 0=Paris / 1=Abbeville / 2=Beijing（只用于记录）
    bool awaitFresh(int slot, double wantLat, const QString &label)
    {
        StelCore *c = core();
        if (!c)
            return false;
        // ── 负控开关（与 `..._FORCE_FOCUSGATE_FAIL` 同款先例）────────────────
        // `STELQUICK_LOC_GATE_OFF=1` ⇒ 关掉就绪门，**配合** `..._LOC_NODELAY=1`
        // （写入步不给延迟）可复现本轮修掉的那个间歇红：写入与回读只隔一个事件循环，
        // 谁先醒是竞态 ⇒ 变换栈还停在**上一个地点** ⇒ LC-04 家族红。
        // 这是**证明门是承重件**用的，不是产品路径。
        if (qEnvironmentVariableIsSet("STELQUICK_LOC_GATE_OFF"))
        {
            gatePolls[slot] = -1;
            note(QStringLiteral("就绪门[%1]：**已由 STELQUICK_LOC_GATE_OFF 关闭**（负控）")
                     .arg(label));
            return true;
        }
        QElapsedTimer t;
        t.start();
        int polls = 0;
        double last = poleAltDeg();
        while (qAbs(last - wantLat) > kPoleAltTolDeg && t.elapsed() < kGateBudgetMs)
        {
            QCoreApplication::processEvents(QEventLoop::AllEvents, kGateStepMs);
            ++polls;
            last = poleAltDeg();
        }
        gateLast[slot] = last;
        gatePolls[slot] = polls;
        gateMs[slot] = int(t.elapsed());
        note(QStringLiteral("就绪门[%1]：等 %2 ms / 探测 %3 次（上限 %4 ms）⇒ 天极高度角 %5° "
                            "（目标纬度 %6°，容差 %7°）")
                 .arg(label)
                 .arg(gateMs[slot])
                 .arg(polls)
                 .arg(kGateBudgetMs)
                 .arg(QString::number(last, 'f', 3),
                      QString::number(wantLat, 'f', 3),
                      QString::number(kPoleAltTolDeg, 'f', 2)));
        return qAbs(last - wantLat) <= kPoleAltTolDeg;
    }

    //! 某个 AltAz 坐标轴在 J2000 里表示的方向，与 Polaris 的 J2000 方向的夹角。
    //! **仅用于信息行**（旧仪器）：Polaris 偏离天极 0.74° ⇒ 夹角随时角抖动 ±0.74°。
    double axisSepDeg(const Vec3d &targetJ2000, int axis) const
    {
        StelCore *c = core();
        if (!c)
            return -1.0;
        Vec3d a(0.0);
        a[axis] = 1.0;
        const Vec3d inJ2000 = c->altAzToJ2000(a, StelCore::RefractionOff);
        return inJ2000.angle(targetJ2000) * 180.0 / M_PI;
    }

    //! 引擎**自己**报的观测者纬度（绕过 AppFacade，作为独立复核腿）。
    //! facade 的 `locationLatitude()` 也是读 `core->getCurrentLocation()`，但对不上时
    //! 要先排除"是不是 facade 那层在骗人"。
    double engLat() const
    {
        StelCore *c = core();
        return c ? double(c->getCurrentLocation().getLatitude()) : -1000.0;
    }
    double engLon() const
    {
        StelCore *c = core();
        return c ? double(c->getCurrentLocation().getLongitude()) : -1000.0;
    }

    //! Polaris 的 J2000 方向。拿不到 → okOut=false（照 T18 先例：照实 SKIP，不洗 PASS）。
    Vec3d polarisJ2000(bool *okOut) const
    {
        StelCore *c = core();
        if (!c)
        {
            *okOut = false;
            return Vec3d(0.0);
        }
        const StelObjectP p =
            StelApp::getInstance().getStelObjectMgr().searchByName(QStringLiteral("Polaris"));
        if (!p)
        {
            *okOut = false;
            return Vec3d(0.0);
        }
        *okOut = true;
        return p->getJ2000EquatorialPos(c);
    }

    void finish()
    {
        result.ran = true;
        result.pass = ok;
        result.summary = ok
            ? QStringLiteral("T33 观察地点写入面自检全过（范围闸/读面/写入生效/时区联动/"
                             "判别腿/天极高度=纬度 绝对腿+三点差分/非法值无副作用/not-found/往返/计数）")
            : QStringLiteral("T33 观察地点写入面自检存在失败项（见明细）");
        onDone(result);
    }
#endif
};

//! 驱动步骤：跑完本步后等 delayAfter 毫秒进下一步。
struct Step
{
    int delayAfter = 0;
    std::function<void(Ctx *)> body;
};

#if defined(STELQUICK_HAS_ENGINE)
//! 经 AppFacade 的**公共查库 API**取一个"精确名字 + 指定区域"地点的完整 ID。
//!
//! ⚠️ 为什么必须带 regionHint：`findLocations` 的结果是 `QMap::values()`，
//! **按 key 字典序**，完全匹配不优先（T33-A 探针已记）。实测 `findLocations("Paris")`
//! 的第一条精确匹配是 **"Paris, Northern America"（美国德州巴黎，UTC−5）**，
//! 不是欧洲巴黎（"Paris, Western Europe"，UTC+2）—— 首跑 LC-03b 就是被这个坑红的
//! （判据没错，是选目标选错了）。这里用区域名过滤把目标钉死。
bool pickExactId(AppFacade *f, const QString &name, const QString &regionHint, QString *idOut)
{
    const QStringList hits = f->findLocations(name, 200);
    QString fallback;
    for (const QString &h : hits)
    {
        const QString head = h.section(QLatin1Char(','), 0, 0).trimmed();
        if (head.compare(name, Qt::CaseInsensitive) != 0)
            continue;
        if (!regionHint.isEmpty() && h.contains(regionHint, Qt::CaseInsensitive))
        {
            *idOut = h;
            return true;
        }
        if (fallback.isEmpty())
            fallback = h;   // 区域提示没命中时至少给一条（并在日志里照实说明）
    }
    if (!fallback.isEmpty())
    {
        *idOut = fallback;
        return true;
    }
    return false;
}
#endif

} // namespace

void LocationCheck::run(QCoreApplication *app,
                        AppFacade *facade,
                        const std::function<void(const Result &)> &onDone,
                        int delayMs)
{
#if !defined(STELQUICK_HAS_ENGINE)
    Q_UNUSED(delayMs)
    Result r;
    r.unavailable = true;
    r.summary = QStringLiteral("地点自检需要合流形态构建（STELQUICK_HAS_ENGINE 未定义）");
    QTimer::singleShot(0, app, [onDone, r]() { onDone(r); });
#else
    auto *ctx = new Ctx;
    ctx->app = app;
    ctx->facade = facade;
    ctx->onDone = onDone;

    auto steps = std::make_shared<QVector<Step>>();
    auto idx = std::make_shared<int>(0);

    //! 写入步的延迟。**不给**延迟（=0）时，回读步与写入步只隔一个事件循环 ⇒ 竞态。
    //! 负控开关 `STELQUICK_LOC_NODELAY=1` 用来**复现**那个间歇红（见 awaitFresh 注释）。
    const int writeDelayMs =
        qEnvironmentVariableIsSet("STELQUICK_LOC_NODELAY") ? 0 : kSettleMs;

    auto tick = std::make_shared<std::function<void()>>();
    *tick = [ctx, steps, idx, tick]() {
        if (*idx >= steps->size())
        {
            ctx->finish();
            return;
        }
        const Step s = steps->at((*idx)++);
        ctx->nextDelay = s.delayAfter;
        // 前提不成立（地点库条目缺失）⇒ 后续步骤全跳过 ⇒ 判据数为 0 ⇒ 报 UNAVAILABLE。
        if (ctx->premiseOk)
            s.body(ctx);
        QTimer::singleShot(ctx->nextDelay, ctx->app, [tick]() { (*tick)(); });
    };

    // ── 步骤 1：LC-01 纯谓词 / LC-02 读面 / 挑目标 / 写 Paris ─────────────────
    steps->append({delayMs, [](Ctx *c) {
        StelCore *core = c->core();
        if (!core)
        {
            c->note(QStringLiteral("引擎不可用（core == nullptr）"));
            return;
        }
        AppFacade *f = c->facade;
        if (!f)
        {
            c->note(QStringLiteral("facade == nullptr"));
            return;
        }

        // ── LC-01 坐标范围**纯谓词**（恒可跑，不依赖引擎）──────────────────
        // 边界必须**含**（±90/±180/-1000/100000 是合法的），越界必须**拒**。
        // NaN 也要拒：QML 里空输入经 `Number()` 就是 NaN，是真实用户路径。
        const auto nan = std::numeric_limits<double>::quiet_NaN();
        const auto inf = std::numeric_limits<double>::infinity();
        const bool latOk = AppFacade::isAcceptableLatitude(-90.0)
                           && AppFacade::isAcceptableLatitude(90.0)
                           && AppFacade::isAcceptableLatitude(0.0)
                           && AppFacade::isAcceptableLatitude(89.9999)
                           && !AppFacade::isAcceptableLatitude(-90.0001)
                           && !AppFacade::isAcceptableLatitude(91.0)
                           && !AppFacade::isAcceptableLatitude(nan)
                           && !AppFacade::isAcceptableLatitude(inf);
        const bool lonOk = AppFacade::isAcceptableLongitude(-180.0)
                           && AppFacade::isAcceptableLongitude(180.0)
                           && AppFacade::isAcceptableLongitude(0.0)
                           && !AppFacade::isAcceptableLongitude(180.5)
                           && !AppFacade::isAcceptableLongitude(-180.5)
                           && !AppFacade::isAcceptableLongitude(nan);
        const bool altOk = AppFacade::isAcceptableAltitude(-1000.0)
                           && AppFacade::isAcceptableAltitude(100000.0)
                           && AppFacade::isAcceptableAltitude(0.0)
                           && !AppFacade::isAcceptableAltitude(-1000.5)
                           && !AppFacade::isAcceptableAltitude(100001.0)
                           && !AppFacade::isAcceptableAltitude(nan);
        c->mark(latOk && lonOk && altOk,
                QStringLiteral("LC-01 坐标范围纯谓词（含边界 / 拒越界 / 拒 NaN / 拒 ±inf）："
                               "lat=%1 lon=%2 alt=%3")
                    .arg(latOk ? QStringLiteral("过") : QStringLiteral("**败**"),
                         lonOk ? QStringLiteral("过") : QStringLiteral("败"),
                         altOk ? QStringLiteral("过") : QStringLiteral("败")));

        // ── 前提②：引擎**未启用"自定义时区"** ────────────────────────────────
        // 🔴 实测（T33-D，2026-09-29，Windows）：这条判据原先**隐含依赖用户配置**。
        //    `StelCore.cpp:240-243`：config 的 `localization/time_zone` 非空
        //    ⇒ `setUseCustomTimeZone(true)`；而 `setObserver` 里的时区联动
        //    （`StelCore.cpp:1543-1547`）条件含 `!getUseCustomTimeZone()` ⇒ **按设计跳过**。
        //    于是 Windows 那台（config.ini 里 `time_zone = Asia/Shanghai`）出现：
        //    LC-03a **恒红**（`ΔUTCOffset = 0.00 h`），而 LC-03b（"时区**不变**"）
        //    **假绿** —— 它不是测出来的，是前提失效送的（血泪第 12 条的同族）。
        //    ⇒ 判据必须**自己建立前提**，不能靠"这台机器的配置刚好合适"；末尾**恢复**。
        c->ctzWasOn = f->useCustomTimeZone();
        if (c->ctzWasOn)
        {
            f->setUseCustomTimeZone(false);
            c->note(QStringLiteral("前提②：引擎 `flagUseCTZ` 原为 **true**（本机 config 设了 "
                                   "`localization/time_zone`）⇒ 时区联动会被按设计跳过。"
                                   "本自检临时置 **false**，末尾恢复 ⇒ 判据不依赖用户配置"));
        }
        else
        {
            c->note(QStringLiteral("前提②：引擎 `flagUseCTZ` = false ⇒ 时区联动路径开放"));
        }

        // ── LC-02 读面非空 + 范围 ──────────────────────────────────────────
        c->startName = f->locationName();
        c->startId = f->locationId();
        c->startLat = f->locationLatitude();
        c->startLon = f->locationLongitude();
        c->startAlt = f->locationAltitudeMeters();
        c->startOffsetH = core->getUTCOffset(core->getJD());
        c->writeBase = int(f->locationWriteCount());
        c->refuseBase = int(f->locationRefusedCount());
        const bool readOk = !c->startName.isEmpty() && !c->startId.isEmpty()
                            && !f->locationPlanet().isEmpty()
                            && AppFacade::isAcceptableLatitude(c->startLat)
                            && AppFacade::isAcceptableLongitude(c->startLon);
        c->mark(readOk,
                QStringLiteral("LC-02 读面非空且坐标在范围内：name=\"%1\" id=\"%2\" planet=\"%3\" "
                               "lat=%4 lon=%5 alt=%6 时区=%7")
                    .arg(c->startName, c->startId, f->locationPlanet())
                    .arg(QString::number(c->startLat, 'f', 4),
                         QString::number(c->startLon, 'f', 4),
                         QString::number(c->startAlt, 'f', 1),
                         f->locationTimeZone()));
        c->note(QStringLiteral("起始 UTCOffset = %1 h").arg(c->startOffsetH, 0, 'f', 3));

        // ── 挑目标：Paris（**跨时区** —— 这条是 LC-03a 的前提腿）────────────
        // ⚠️ 不能用 Beijing：起始地点（引擎默认配的绵阳）与 Beijing 同属
        //    Asia/Shanghai ⇒ 时区联动那条腿会**假绿**（T33-A 探针踩过）。
        if (!pickExactId(f, QStringLiteral("Paris"), QStringLiteral("Western Europe"), &c->parisId))
        {
            c->note(QStringLiteral("库里取不到 Paris ⇒ 无法构造跨时区场景"));
            c->premiseOk = false;
            c->result.unavailable = true;
            return;
        }
        if (!pickExactId(f, QStringLiteral("Beijing"), QStringLiteral("Eastern Asia"), &c->beijingId))
        {
            c->note(QStringLiteral("库里取不到 Beijing ⇒ LC-04 只有一点，差分腿不成立"));
            c->premiseOk = false;
            c->result.unavailable = true;
            return;
        }

        // ── 目录侧纬度（LC-04 的对照量，与"引擎当前地点"**不同来源**）──────────
        {
            const StelLocation lp = c->locMgr().locationForString(c->parisId);
            c->parisDbOk = lp.isValid();
            c->parisDbLat = c->parisDbOk ? double(lp.getLatitude(true)) : 0.0;
            const StelLocation lb = c->locMgr().locationForString(c->beijingId);
            c->beijingDbOk = lb.isValid();
            c->beijingDbLat = c->beijingDbOk ? double(lb.getLatitude(true)) : 0.0;
        }

        // 同 Paris 时区（Europe/Paris）的另一个地点 —— LC-03b 的判别腿要用。
        // 直接扫库：`fingerprints` 只匹配 name/region，匹配不了时区。
        const QString parisTz = f->locationTimeZone();   // 起始地点的时区，先占位
        Q_UNUSED(parisTz)
        {
            const LocationList all = c->locMgr().getAll();
            for (const StelLocation &l : all)
            {
                if (l.ianaTimeZone == QLatin1String("Europe/Paris")
                    && l.name.compare(QStringLiteral("Paris"), Qt::CaseInsensitive) != 0
                    && !l.getID().isEmpty())
                {
                    c->parisSameTzId = l.getID();
                    c->abbevilleDbLat = double(l.getLatitude(true));   // 目录侧纬度
                    c->abbevilleDbOk = true;
                    break;
                }
            }
        }
        c->note(QStringLiteral("目标：Paris=\"%1\" / Beijing=\"%2\" / 同 Europe/Paris 时区的另一地点=\"%3\"")
                    .arg(c->parisId, c->beijingId,
                         c->parisSameTzId.isEmpty() ? QStringLiteral("<没找到>") : c->parisSameTzId));

        // ── 前提门：三个目标的**目录纬度**都得拿得到 ────────────────────────────
        // 拿不到就没法做 LC-04/04b 的对照（对照量刻意取目录而非回读，见 Ctx 里的注释）。
        // 此时**照实报 UNAVAILABLE**，不假绿也不假红（tick() 会跳过后续所有步骤）。
        if (!c->parisDbOk || !c->beijingDbOk || !c->abbevilleDbOk)
        {
            c->note(QStringLiteral("目录里取不到全部三个目标的纬度"
                                   "(paris=%1 beijing=%2 abbeville=%3) ⇒ UNAVAILABLE")
                        .arg(c->parisDbOk).arg(c->beijingDbOk).arg(c->abbevilleDbOk));
            c->premiseOk = false;
            c->result.unavailable = true;
            return;
        }
        c->note(QStringLiteral("目录纬度：Paris %1° / Abbeville %2° / Beijing %3°")
                    .arg(QString::number(c->parisDbLat, 'f', 4),
                         QString::number(c->abbevilleDbLat, 'f', 4),
                         QString::number(c->beijingDbLat, 'f', 4)));

        // ── 写 Paris ───────────────────────────────────────────────────────
        const bool wrote = f->setLocationById(c->parisId);
        c->note(QStringLiteral("写入 Paris ⇒ %1 token=%2")
                    .arg(wrote ? QStringLiteral("成功") : QStringLiteral("失败"), f->lastLocationRefusal()));
        c->parisLat = f->locationLatitude();

        // ⚠️ 这里**不需要**冻结仿真时间（T27 血泪第 15 条的手法①）：主仪器用的是
        // **当日天极**，它严格在自转轴上 ⇒ `Rz(−θ)` 对它是恒等 ⇒ 读数与恒星时无关。
        // 只有旧仪器（Polaris）才怕自转，而它已被降级成信息行。
    }});

    // ── 步骤 2：就绪门 + LC-03a 写入生效（成对）+ Paris 侧仪器读数 ─────────────
    steps->append({0, [](Ctx *c) {
        AppFacade *f = c->facade;
        StelCore *core = c->core();
        if (!f || !core)
            return;
        // ⚠️ 门必须在**本相位开头**：放在读数之后会让重试把判据重跑一遍、计数虚高
        //    （T30 的 DYN 先例）。
        const bool fresh = c->awaitFresh(0, c->parisLat, QStringLiteral("Paris"));
        if (!fresh)
            c->note(QStringLiteral("**就绪门超时**：写完 Paris 后 %1 ms 内变换栈仍没跟上"
                                   "（极点高度角停在 %2°，目标 %3°）")
                        .arg(c->gateMs[0])
                        .arg(QString::number(c->gateLast[0], 'f', 3),
                             QString::number(c->parisLat, 'f', 3)));

        const double lat = f->locationLatitude();
        const double off = core->getUTCOffset(core->getJD());
        c->offAfterParis = off;
        const double dOff = off - c->startOffsetH;
        // (a) 地点 API 侧：确实变成了 Paris
        const bool moved = QString::compare(f->locationId(), c->parisId, Qt::CaseSensitive) == 0
                           || lat > 40.0;   // Paris 纬度 48.85（起始是 31.47 绵阳）
        // (b) 引擎侧独立量：UTCOffset 从 Asia/Shanghai(+8) 变到 Europe/Paris(+2)
        const bool tzFollowed = qAbs(dOff) > 1.0;
        c->mark(moved && tzFollowed,
                QStringLiteral("LC-03a 写入跨时区地点生效（成对）：回读 id=\"%1\" lat=%2 "
                               "∧ UTCOffset %3 → %4（Δ=%5 h）")
                    .arg(f->locationId())
                    .arg(QString::number(lat, 'f', 4))
                    .arg(QString::number(c->startOffsetH, 'f', 2),
                         QString::number(off, 'f', 2),
                         QString::number(dOff, 'f', 2)));

        // 【主仪器】当日天极在地平坐标里的高度角 —— 不依赖 Polaris，恒可采。
        c->poleAltParis = c->poleAltDeg();
        c->poleAzParis = c->poleAzDeg();
        c->note(QStringLiteral("【主仪器·Paris】天极高度角 = %1°（目标纬度 %2°，Δ=%3°）方位 %4°│"
                               "facade 纬度 %5° / 引擎纬度 %6° / 引擎经度 %7°")
                    .arg(QString::number(c->poleAltParis, 'f', 3),
                         QString::number(c->parisLat, 'f', 3),
                         QString::number(qAbs(c->poleAltParis - c->parisLat), 'f', 3),
                         QString::number(c->poleAzParis, 'f', 2),
                         QString::number(lat, 'f', 3),
                         QString::number(c->engLat(), 'f', 3),
                         QString::number(c->engLon(), 'f', 3)));

        // 旧仪器（Polaris 夹角）**只作信息行**：0.74° 的天极偏移带来周日抖动，
        // 拿它当判据会间歇红/绿（本轮踩过）。留读数是因为它走"天体目录"这条侧链路。
        bool pok = false;
        const Vec3d pv = c->polarisJ2000(&pok);
        c->polarisOk = pok;
        if (pok)
        {
            for (int ax = 0; ax < 3; ++ax)
                c->axisSep[0][ax] = c->axisSepDeg(pv, ax);
            c->note(QStringLiteral("【参考·Paris】与 Polaris 的夹角：轴0=%1° 轴1=%2° 轴2=%3°"
                                   "（轴2 是天顶轴，应 ≈ 90−纬度 = %4°；抖动 ±0.74°）")
                        .arg(QString::number(c->axisSep[0][0], 'f', 2),
                             QString::number(c->axisSep[0][1], 'f', 2),
                             QString::number(c->axisSep[0][2], 'f', 2),
                             QString::number(90.0 - lat, 'f', 2)));
        }
        else
        {
            c->note(QStringLiteral("参考行不可用（检索不到 Polaris），**不影响任何判据**"));
        }
    }});

    // ── 步骤 3：写"同 Paris 时区"的另一个地点（LC-03b 的布场 + LC-04b 的第三点）──
    // ⚠️ 写入步给足 delayAfter：即便下一相位开头还有就绪门，也别让"竞态"有机会出现
    //    （门是兜底，不是借口 —— 见头注里那条 delayAfter 的教训）。
    steps->append({writeDelayMs, [](Ctx *c) {
        AppFacade *f = c->facade;
        if (!f)
            return;
        if (c->parisSameTzId.isEmpty())
        {
            c->note(QStringLiteral("没找到同 Europe/Paris 时区的第二地点 ⇒ LC-03b 将照实 SKIP"));
            return;
        }
        const bool wrote = f->setLocationById(c->parisSameTzId);
        c->note(QStringLiteral("写入同巴黎时区地点 \"%1\" ⇒ %2 token=%3")
                    .arg(c->parisSameTzId,
                         wrote ? QStringLiteral("成功") : QStringLiteral("失败"),
                         f->lastLocationRefusal()));
        c->abbevilleLat = f->locationLatitude();
    }});

    // ── 步骤 4：就绪门 + LC-03b 判别腿（经纬变 ∧ 时区**不变**）+ 第三点读数 ─────
    steps->append({0, [](Ctx *c) {
        AppFacade *f = c->facade;
        StelCore *core = c->core();
        if (!f || !core)
            return;
        if (c->parisSameTzId.isEmpty())
        {
            c->note(QStringLiteral("LC-03b SKIP（没有同巴黎时区的第二地点可用）"));
            return;
        }
        c->awaitFresh(1, c->abbevilleLat, QStringLiteral("Abbeville"));
        const double lat = f->locationLatitude();
        const double off = core->getUTCOffset(core->getJD());
        c->offAfterSameTz = off;
        // 经纬确实变了（与 Paris 不同），但时区偏移**没变** —— 这才读得出
        // "UTCOffset 是跟着地点走的"，而不是"随便写什么都变"。
        const bool moved = qAbs(lat - c->parisLat) > 0.05;
        const bool tzStable = qAbs(off - c->offAfterParis) < 0.01;
        c->mark(moved && tzStable,
                QStringLiteral("LC-03b 判别腿（同时区换地点）：纬度 %1° → %2°（确实变了）"
                               "∧ UTCOffset 保持 %3 h（未变）")
                    .arg(QString::number(c->parisLat, 'f', 3),
                         QString::number(lat, 'f', 3),
                         QString::number(off, 'f', 2)));
        // LC-04b 要的三点里的一点：与 Paris **只差 1.25° 纬度**、时区相同。
        // 这一对是主仪器分辨力的下限检查（Polaris 仪器在这一对上会失明）。
        c->poleAltAbbeville = c->poleAltDeg();
        c->note(QStringLiteral("【主仪器·Abbeville】天极高度角 = %1°（目标纬度 %2°，Δ=%3°）"
                               "│与 Paris 同 Europe/Paris 时区，纬度只差 %4°")
                    .arg(QString::number(c->poleAltAbbeville, 'f', 3),
                         QString::number(c->abbevilleLat, 'f', 3),
                         QString::number(qAbs(c->poleAltAbbeville - c->abbevilleLat), 'f', 3),
                         QString::number(qAbs(c->abbevilleLat - c->parisLat), 'f', 3)));
    }});

    // ── 步骤 5：写 Beijing（LC-04 的第三个纬度点）─────────────────────────────
    steps->append({writeDelayMs, [](Ctx *c) {
        AppFacade *f = c->facade;
        if (!f || c->beijingId.isEmpty())
        {
            c->note(QStringLiteral("Beijing ID 不可用 ⇒ LC-04 的第三点缺失"));
            return;
        }
        const bool wrote = f->setLocationById(c->beijingId);
        c->note(QStringLiteral("写入 Beijing ⇒ %1 token=%2")
                    .arg(wrote ? QStringLiteral("成功") : QStringLiteral("失败"),
                         f->lastLocationRefusal()));
        c->beijingLat = f->locationLatitude();
        if (StelCore *core = c->core())
            c->offAfterBeijing = core->getUTCOffset(core->getJD());
    }});

    // ── 步骤 6：就绪门 + LC-04（绝对腿）+ LC-04b（三点判别腿）──────────────────
    steps->append({0, [](Ctx *c) {
        AppFacade *f = c->facade;
        StelCore *core = c->core();
        if (!f || !core)
            return;
        const double lat = f->locationLatitude();
        // 主仪器不依赖 Polaris ⇒ 这里**没有** SKIP 分支，恒可判。
        const bool fresh = c->awaitFresh(2, lat, QStringLiteral("Beijing"));
        c->poleAltBeijing = c->poleAltDeg();
        c->poleAzBeijing = c->poleAzDeg();
        c->note(QStringLiteral("【主仪器·Beijing】天极高度角 = %1°（目标纬度 %2°，Δ=%3°）方位 %4°│"
                               "facade 纬度 %5° / 引擎纬度 %6°")
                    .arg(QString::number(c->poleAltBeijing, 'f', 3),
                         QString::number(lat, 'f', 3),
                         QString::number(qAbs(c->poleAltBeijing - lat), 'f', 3),
                         QString::number(c->poleAzBeijing, 'f', 2),
                         QString::number(lat, 'f', 3),
                         QString::number(c->engLat(), 'f', 3)));

        // 参考行：Polaris（走天体目录侧链路，抖动 ±0.74° ⇒ 只作信息）。
        bool pok = false;
        const Vec3d pv = c->polarisJ2000(&pok);
        if (pok)
        {
            for (int ax = 0; ax < 3; ++ax)
                c->axisSep[1][ax] = c->axisSepDeg(pv, ax);
            c->note(QStringLiteral("【参考·Beijing】与 Polaris 的夹角：轴0=%1° 轴1=%2° 轴2=%3°"
                                   "（轴2 应 ≈ 90−纬度 = %4°）")
                        .arg(QString::number(c->axisSep[1][0], 'f', 2),
                             QString::number(c->axisSep[1][1], 'f', 2),
                             QString::number(c->axisSep[1][2], 'f', 2),
                             QString::number(90.0 - lat, 'f', 2)));
        }

        // ── LC-04 绝对腿：三个地点都满足 |天极高度角 − 目录纬度| < 0.5° ─────────
        // 恒等式：北天极高度 = 观测者纬度。仪器 = 当日天极（见 poleAltDeg 注释）。
        // 🔴 对照量刻意取**地点库目录**里的纬度，不取 `f->locationLatitude()`：
        //    后者在"写入没落地"时会跟仪器读数一起停在旧地点 → 两边自洽 → 假绿
        //    （负控 D 实证）。目录是**另一条来源**（`StelLocationMgr` 数据文件 vs
        //    `position->currentLocation`）⇒ 写入没落到目标就必红。
        // 判别间隙 8.95°、容差 0.5° ⇒ 判别力 ≈ 18 倍。
        // 前提门已保证三个目录纬度都可读（拿不到会走 UNAVAILABLE、根本到不了这里）。
        const double eParis = qAbs(c->poleAltParis - c->parisDbLat);
        const double eAbbe = qAbs(c->poleAltAbbeville - c->abbevilleDbLat);
        const double eBj = qAbs(c->poleAltBeijing - c->beijingDbLat);
        c->mark(fresh && eParis < kPoleAltTolDeg && eAbbe < kPoleAltTolDeg
                    && eBj < kPoleAltTolDeg,
                QStringLiteral("LC-04 可验算物理量（天极高度 = 观测者纬度）："
                               "观测点 |Δ| vs **目录纬度**——Paris %1° / Abbeville %2° / "
                               "Beijing %3°（容差 %4°）")
                    .arg(QString::number(eParis, 'f', 3),
                         QString::number(eAbbe, 'f', 3),
                         QString::number(eBj, 'f', 3),
                         QString::number(kPoleAltTolDeg, 'f', 2)));
        c->note(QStringLiteral("LC-04 对照读数：Paris 仪器 %1° vs 目录 %2°（引擎回读 %3°）│"
                               "Abbeville 仪器 %4° vs 目录 %5°（回读 %6°）│"
                               "Beijing 仪器 %7° vs 目录 %8°（回读 %9°）")
                    .arg(QString::number(c->poleAltParis, 'f', 3),
                         QString::number(c->parisDbLat, 'f', 3),
                         QString::number(c->parisLat, 'f', 3),
                         QString::number(c->poleAltAbbeville, 'f', 3),
                         QString::number(c->abbevilleDbLat, 'f', 3),
                         QString::number(c->abbevilleLat, 'f', 3),
                         QString::number(c->poleAltBeijing, 'f', 3),
                         QString::number(c->beijingDbLat, 'f', 3),
                         QString::number(lat, 'f', 3)));

        // ── LC-04b 判别腿：三点两两的读数差 ≈ **目录**纬度差 ───────────────────
        // 与 LC-04 相互独立：只看**仪器自己**在三个地点之间的差分（消掉任何常量偏置），
        // 不依赖仪器的绝对刻度。对照量同样是**目录**（不是回读）—— 理由见 LC-04。
        // 两对刻意选在**尺度两端**：Paris↔Beijing 8.95°（大）、Paris↔Abbeville 1.25°（小）。
        // 小的那一对是"分辨力下限"：Polaris 仪器在这里会失明（抖动 ±0.74° > 1.25°/2）。
        const double dPB = qAbs(c->poleAltBeijing - c->poleAltParis);
        const double wPB = qAbs(c->beijingDbLat - c->parisDbLat);
        const double dPA = qAbs(c->poleAltAbbeville - c->poleAltParis);
        const double wPA = qAbs(c->abbevilleDbLat - c->parisDbLat);
        c->mark(qAbs(dPB - wPB) < kLatDiffTolDeg && qAbs(dPA - wPA) < kLatDiffTolDeg,
                QStringLiteral("LC-04b 判别腿（三点差分）：读数差 Paris↔Beijing %1° vs 纬度差 %2°"
                               "（|Δ|=%3°）；Paris↔Abbeville %4° vs %5°（|Δ|=%6°）；容差 %7°")
                    .arg(QString::number(dPB, 'f', 3), QString::number(wPB, 'f', 3),
                         QString::number(qAbs(dPB - wPB), 'f', 3),
                         QString::number(dPA, 'f', 3), QString::number(wPA, 'f', 3),
                         QString::number(qAbs(dPA - wPA), 'f', 3),
                         QString::number(kLatDiffTolDeg, 'f', 2)));
    }});

    // ── 步骤 7：LC-05 非法值**无副作用** ────────────────────────────────────
    // 注：本任务**不冻结仿真时间**（主仪器与恒星时无关，见步骤 1 末尾的说明），
    // 所以这里也没有"恢复时钟"的收尾动作 —— 判据自己不改环境，就不需要还环境。
    steps->append({0, [](Ctx *c) {
        AppFacade *f = c->facade;
        if (!f)
            return;

        const QString idBefore = f->locationId();
        const bool b1 = f->setLocationByCoordinates(91.0, 0.0, 0.0);
        const QString t1 = f->lastLocationRefusal();
        const bool b2 = f->setLocationByCoordinates(0.0, 200.0, 0.0);
        const QString t2 = f->lastLocationRefusal();
        const bool b3 = f->setLocationByCoordinates(0.0, 0.0, 1.0e6);
        const QString t3 = f->lastLocationRefusal();
        const QString idAfter = f->locationId();
        const bool refused = !b1 && !b2 && !b3;
        const bool tokens = t1 == QLatin1String("invalid-latitude")
                            && t2 == QLatin1String("invalid-longitude")
                            && t3 == QLatin1String("invalid-altitude");
        const bool noSideEffect = idBefore == idAfter;
        c->mark(refused && tokens && noSideEffect,
                QStringLiteral("LC-05 非法值被拒 ∧ token 正确 ∧ **无副作用**："
                               "lat=91→%1(%2) lon=200→%3(%4) alt=1e6→%5(%6) id \"%7\"→\"%8\"")
                    .arg(b1 ? QStringLiteral("接受") : QStringLiteral("拒"), t1)
                    .arg(b2 ? QStringLiteral("接受") : QStringLiteral("拒"), t2)
                    .arg(b3 ? QStringLiteral("接受") : QStringLiteral("拒"), t3)
                    .arg(idBefore, idAfter));
    }});

    // ── 步骤 8：LC-06 not-found（不存在的 ID）───────────────────────────────
    steps->append({0, [](Ctx *c) {
        AppFacade *f = c->facade;
        if (!f)
            return;
        const QString idBefore = f->locationId();
        const bool b = f->setLocationById(QStringLiteral("这个地点不存在, Nowhere"));
        const QString t = f->lastLocationRefusal();
        const bool noSideEffect = idBefore == f->locationId();
        c->mark(!b && t == QLatin1String("not-found") && noSideEffect,
                QStringLiteral("LC-06 不存在的 ID 被拒（token=%1）∧ 无副作用（id 仍 \"%2\"）")
                    .arg(t, f->locationId()));
    }});

    // ── 步骤 9：LC-07 布场 —— 写回起始地点 ──────────────────────────────────
    steps->append({writeDelayMs, [](Ctx *c) {
        AppFacade *f = c->facade;
        if (!f)
            return;
        // 优先用 ID（能一并恢复名字与时区）；取不到就用坐标写（只恢复经纬度）。
        bool ok = false;
        if (!c->startId.isEmpty())
            ok = f->setLocationById(c->startId);
        if (!ok)
        {
            c->note(QStringLiteral("起始地点的 ID \"%1\" 写不回去（token=%2）⇒ 退回按坐标写")
                        .arg(c->startId, f->lastLocationRefusal()));
            ok = f->setLocationByCoordinates(c->startLat, c->startLon, c->startAlt,
                                             c->startName);
        }
        c->note(QStringLiteral("写回起始地点 ⇒ %1 token=%2")
                    .arg(ok ? QStringLiteral("成功") : QStringLiteral("失败"),
                         f->lastLocationRefusal()));
    }});

    // ── 步骤 10：LC-07 断言 + LC-08 计数 ────────────────────────────────────
    steps->append({kSettleMs, [](Ctx *c) {
        AppFacade *f = c->facade;
        if (!f)
            return;
        const double dLat = qAbs(f->locationLatitude() - c->startLat);
        const double dLon = qAbs(f->locationLongitude() - c->startLon);
        c->mark(dLat < kCoordTolDeg && dLon < kCoordTolDeg,
                QStringLiteral("LC-07 往返：写回起始地点后 lat 差 %1° lon 差 %2°（容差 %3°）")
                    .arg(QString::number(dLat, 'f', 5),
                         QString::number(dLon, 'f', 5),
                         QString::number(kCoordTolDeg, 'f', 5)));

        // ── LC-08 计数：只对**增量**断言 ────────────────────────────────────
        // 累计值会被外面的调用影响，所以记了 baseline。
        // 预期：成功写入 = Paris + 同巴黎时区 + Beijing + 写回起始 = 4 次
        //      （若 parisSameTzId 为空则 3 次）；拒绝 = 非法值 3 + not-found 1 = 4 次。
        const int w = int(f->locationWriteCount()) - c->writeBase;
        const int r = int(f->locationRefusedCount()) - c->refuseBase;
        const int wantW = c->parisSameTzId.isEmpty() ? 3 : 4;
        c->mark(w == wantW && r == 4,
                QStringLiteral("LC-08 计数增量：写入 %1 次（期望 %2）/ 拒绝 %3 次（期望 4）")
                    .arg(w).arg(wantW).arg(r));
    }});

    // ── 步骤 11：恢复前提②（判据自己改的环境，自己还）──────────────────────
    // 血泪第 9 条：判据在同一步内改了环境就必须恢复，否则**后续**判据在错误语境里测出假红。
    // 这里改的是 `flagUseCTZ`（纯内存、不落盘）⇒ 恢复后与用户 config 一致。
    steps->append({0, [](Ctx *c) {
        AppFacade *f = c->facade;
        if (!f || !c->ctzWasOn)
            return;
        f->setUseCustomTimeZone(true);
        c->note(QStringLiteral("已恢复前提②：`flagUseCTZ` 还原为 **true**（与用户 config 一致）"));
    }});

    (*tick)();
#endif
}

} // namespace stelapp
