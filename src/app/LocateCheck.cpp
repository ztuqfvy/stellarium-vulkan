/*
 * LocateCheck — 实现（T18 定位/跟踪）。判据清单与设计理由见头注。
 *
 * 驱动方式：**线性步骤表**。每步 = (跑完后等多少 ms, 步骤体)；步骤体可改写
 * `c->nextDelay` 来改变这一次的等待（"重采样"就是这么实现的）。
 *
 * 刻意**不**做成"步骤体自己再挂一个定时器"：那会与驱动器的延时推进并存，
 * 变成"两条路都在往前推"，同一处理器被跑两遍（本文件初稿踩过这个坑）。
 * 需要"等一会儿再采样"的地方一律拆成两步。
 *
 * 时间线（全部 GUI 线程；总时长约 12s）：
 *   1  ∪3500   LOC-01 纯谓词；LOC-02 无选中不谎报；挑 fixture；LOC-03a locateSelected(true)
 *   2  ∪0/1500 LOC-03b 采样锁定夹角；不达标则改 nextDelay=1500 触发重采样
 *   3  ∪0      （仅重采样路径）再采样一次并定判
 *   4  ∪1200   LOC-04a 记 a0 并跳 6h
 *   5  ∪0      LOC-04b (a) 目标 AltAz 确实动了 ∧ (b) 夹角仍 ≈0
 *   6  ∪2500   LOC-05a locateSelected(false)（只归中不跟踪）
 *   7  ∪1200   LOC-05b-prep 记 off0 并跳 6h
 *   8  ∪0      LOC-05c (a2) 目标确实动了 ∧ (c) 夹角 > 5°（放开后不再跟随）
 *   9  ∪0      LOC-06 关跟踪
 *   10 ∪2500   LOC-07a locateSelected(true)
 *   11 ∪0      LOC-07b 幂等重选同一 stableId，跟踪必须不断
 *   12 ∪0      LOC-08 家园行星端到端 + 收尾清选中
 */
#include "app/LocateCheck.hpp"

#include "app/AppFacade.hpp"
#include "app/ObjectInfoModel.hpp"
#include "app/SearchResultsModel.hpp"

#include <QCoreApplication>
#include <QTimer>
#include <QVector>

#if defined(STELQUICK_HAS_ENGINE)
#include "StelApp.hpp"
#include "StelCore.hpp"
#include "StelMovementMgr.hpp"
#include "StelObject.hpp"
#include "StelObjectMgr.hpp"
#include "VecMath.hpp"
#endif

namespace stelapp {

namespace {

//! 判据门槛（改这里必须同步改头注与文档）。
#if defined(STELQUICK_HAS_ENGINE)
const double kLockToleranceDeg = 0.5;   //!< 跟踪锁定：夹角必须小于这个值
const double kMotionFloorDeg   = 5.0;   //!< "世界确实动了"：AltAz 变化必须大于这个值
const double kTimeJumpDays     = 0.25;  //!< 每次跳 6 小时（地球自转 90°）
#endif

//! fixture 候选，按优先级。月球排第一：A4 固定流程 I-REP-02 就是"搜月球→定位"。
#if defined(STELQUICK_HAS_ENGINE)
const char *const kFixtureCandidates[] = {"Moon", "Jupiter", "Sirius", "Vega", "Polaris"};
#endif

struct Ctx
{
    QCoreApplication *app = nullptr;
    AppFacade *facade = nullptr;
    std::function<void(const LocateCheck::Result &)> onDone;

    LocateCheck::Result result;
    bool ok = true;
    int nextDelay = 0;         //!< 本步结束后等多久进下一步（步骤体可改写）

    QString fixtureName;       //!< 选中天体的英文名（身份比对用：显示名随语言变）
    QString fixtureSid;        //!< 它的 stableId

    double ang0 = -1.0;        //!< 定位后夹角（度）
    double ang1 = -1.0;        //!< 跟踪中、跳 6h 后夹角
    double off0 = -1.0;        //!< 不跟踪、归中后夹角
    double off1 = -1.0;        //!< 不跟踪、跳 6h 后夹角
    double moved1 = -1.0;      //!< 跟踪那段里目标 AltAz 变化量（度）
    double moved2 = -1.0;      //!< 不跟踪那段里目标 AltAz 变化量（度）
    double offsetDeg = 0.0;    //!< 引擎视口中心偏移等效角度（默认 0）
    bool needResample = false; //!< 锁定夹角未达标 → 走重采样路径
    bool haveFixture = false;
#if defined(STELQUICK_HAS_ENGINE)
    Vec3d prevAltAz = Vec3d(0.);     //!< 跨步骤传递的"跳变前"目标地平向量
    Vec3d settleAltAz = Vec3d(0.);   //!< "归中不跟踪"那段等待**开始时**的目标地平向量
#endif

    // 输出纪律（与 T17 的 SearchModelCheck 一致）：**只往 result.details 累积，
    // 本文件不直接打印**。报告由 main.cpp 的单点回调统一打印一次。
    // 反例与代价：T15 的 AppFacadeCheck 是"qDebug 实时行 + main.cpp 报告"两份，
    // 验证脚本把 stdout/stderr 合并重定向（> file 2>&1）后每条判据**出现两遍**，
    // 证据文件里 AC-* 全是成对的（T17 期即已如此）。这里不再复制那个模式。
    void mark(const QString &line)
    {
        result.details.append(line);
        ++result.total;
        if (line.endsWith(QStringLiteral("FAIL")))
            ok = false;
        else if (line.endsWith(QStringLiteral("OK")))
            ++result.passed;
    }

    //! 只记录、不计入判据数（信息行 / 照实 SKIP）。避免把 SKIP 与"未判"混进
    //! 通过率，也避免有人看到 100% 就以为"全都验过了"。
    void note(const QString &line)
    {
        result.details.append(line);
    }

#if defined(STELQUICK_HAS_ENGINE)
    StelCore *core() const
    {
        return StelApp::isInitialized() ? StelApp::getInstance().getCore() : nullptr;
    }
    StelMovementMgr *mv() const
    {
        StelCore *c = core();
        return c ? c->getMovementMgr() : nullptr;
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
    //! 目标方向与视向的夹角（度）。两者都在 J2000 赤道系，**不**复刻 mountMode
    //! 换算 —— 那串换算是被**测**的对象，不该混进判据的输入。
    double sepDeg() const
    {
        StelCore *c = core();
        StelMovementMgr *m = mv();
        const StelObjectP obj = selected();
        if (!c || !m || !obj)
            return -1.0;
        return obj->getJ2000EquatorialPos(c).angle(m->getViewDirectionJ2000()) * 180.0 / M_PI;
    }
    //! 目标当前的地平坐标单位向量（用来量"世界动了多少"）。
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
    //! 引擎的视口中心偏移等效角度（度）。跟踪分支会给目标纬度加这么多。
    double viewportOffsetDeg() const
    {
        StelCore *c = core();
        StelMovementMgr *m = mv();
        if (!c || !m)
            return 0.0;
        return c->getCurrentStelProjectorParams().viewportCenterOffset[1] * m->getCurrentFov();
    }
#endif

    void finish()
    {
        result.ran = true;
        result.pass = ok;
        result.summary = ok
            ? QStringLiteral("T18 定位/跟踪自检全过（守卫/无选中不谎报/定位落地/"
                             "跟踪跟随/放开不跟随/关跟踪/幂等重选/家园行星）")
            : QStringLiteral("T18 定位/跟踪自检存在失败项（见明细）");
        onDone(result);
    }
};

//! 驱动步骤：跑完本步后等 delayAfter 毫秒进下一步。步骤体可改写 ctx->nextDelay
//! 来改变这一次的等待（"重采样"就是这么实现的）。
struct Step
{
    int delayAfter = 0;
    std::function<void(Ctx *)> body;
};

#if defined(STELQUICK_HAS_ENGINE)

//! 经 **AppFacade 自己的公共 API** 挑一个可用 fixture（不用私有引擎调用）。
//! 必须按下标逐个比对名字：引擎聚合结果的排序口径是**名称字典序**、不是相关度
//! （T17 实测），所以不能假定第 0 行就是我们要的那个。
bool pickFixture(AppFacade *facade, QString *sidOut, QString *nameOut)
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
            *nameOut = en.isEmpty() ? dn : en;
            return true;
        }
    }
    return false;
}

#endif  // STELQUICK_HAS_ENGINE

//! 判定"锁定夹角达标"的期望值：引擎跟踪分支会加上视口中心偏移，
//! 故期望夹角 = |偏移|（默认配置下就是 0）。
#if defined(STELQUICK_HAS_ENGINE)
bool lockOk(double ang, double offsetDeg)
{
    return qAbs(ang - qAbs(offsetDeg)) < kLockToleranceDeg;
}
#endif

} // namespace

void LocateCheck::run(QCoreApplication *app,
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
        // 延时在**步骤体跑完之后**才读：步骤体改写 nextDelay 就是为了改这一次的等待。
        QTimer::singleShot(ctx->nextDelay, ctx->app, [tick]() { (*tick)(); });
    };

    // ── 步骤 1：LOC-01 / LOC-02 / fixture / 发起定位 ─────────────────────────
    steps->append({delayMs, [](Ctx *c) {
        // LOC-01 家园行星守卫：纯谓词。真去检索"当前观察地点的行星"不一定拿得到
        // 对象（首页地点是 Earth，能否检索取决于配置），但守卫逻辑本身必须
        // **恒可验**，不能因环境漏测 —— 这是把它抽成纯谓词的全部理由。
        const bool a = AppFacade::isHomePlanet(QStringLiteral("Earth"), QStringLiteral("Earth")) == true;
        const bool b = AppFacade::isHomePlanet(QStringLiteral("Moon"), QStringLiteral("Earth")) == false;
        const bool d = AppFacade::isHomePlanet(QString(), QStringLiteral("Earth")) == false;
        const bool e = AppFacade::isHomePlanet(QStringLiteral("Earth"), QString()) == false;
        const bool f = AppFacade::isHomePlanet(QStringLiteral("earth"), QStringLiteral("Earth")) == false;
        // 最后一条是**故意**的：引擎用 == 严格比对（区分大小写），本层不引入
        // 比引擎更宽松的等价关系 —— 否则"我们放行、引擎拒绝"会成为新的不一致源。
        const bool pass = a && b && d && e && f;
        c->mark(QStringLiteral("LOC-01 家园行星守卫谓词（同名拒绝/异名放行/空串不误拒/"
                               "大小写严格）：%1")
                    .arg(pass ? QStringLiteral("OK") : QStringLiteral("FAIL")));

        // LOC-02 无选中时不谎报跟踪。引擎的 setFlagTracking(true) 在无选中时会
        // emit flagTrackingChanged(true) 而内部仍是 false（StelMovementMgr.cpp:1386-1404）
        // —— 任何缓存标志的 UI 都会在这里谎报；本层读合取真值免疫。
        c->facade->clearSelection();
        const bool setOnNoSel = c->facade->setTracking(true);
        const bool c2 = !setOnNoSel
                        && c->facade->lastLocateRefusal() == QStringLiteral("no-selection")
                        && !c->facade->isTracking();
        c->mark(QStringLiteral("LOC-02 无选中时 setTracking(true) 拒绝（返回 %1，理由 %2，"
                               "isTracking=%3）：%4")
                    .arg(setOnNoSel ? QStringLiteral("true") : QStringLiteral("false"),
                         c->facade->lastLocateRefusal(),
                         c->facade->isTracking() ? QStringLiteral("true") : QStringLiteral("false"),
                         c2 ? QStringLiteral("OK") : QStringLiteral("FAIL")));

#if defined(STELQUICK_HAS_ENGINE)
        c->haveFixture = pickFixture(c->facade, &c->fixtureSid, &c->fixtureName);
        if (!c->haveFixture)
        {
            // LOC-01/02 的结论已照实打印，绝不伪装成全绿（"不许静默通过"纪律）。
            c->result.unavailable = true;
            c->result.ran = false;
            c->result.summary = QStringLiteral(
                "T18 定位/跟踪自检：环境里找不到可用 fixture（跳过 LOC-03..08）");
            c->onDone(c->result);
            return;
        }
        c->mark(QStringLiteral("LOC-fixture 选中 %1（stableId=%2）：OK")
                    .arg(c->fixtureName, c->fixtureSid));

        c->offsetDeg = c->viewportOffsetDeg();
        c->note(QStringLiteral("LOC-info 基准 JD=%1，视口中心偏移等效 %2°（跟踪期望夹角=|偏移|）")
                    .arg(c->facade->julianDay(), 0, 'f', 5)
                    .arg(c->offsetDeg, 0, 'f', 4));

        const bool located = c->facade->locateSelected(true);
        c->mark(QStringLiteral("LOC-03a locateSelected(true) 返回 %1，理由 %2：%3")
                    .arg(located ? QStringLiteral("true") : QStringLiteral("false"),
                         c->facade->lastLocateRefusal(),
                         located ? QStringLiteral("OK") : QStringLiteral("FAIL")));
#endif
    }});

#if defined(STELQUICK_HAS_ENGINE)
    // ── 步骤 2：LOC-03b 采样锁定夹角（自动移动 1.5s 已由步骤 1 的延时覆盖）──
    steps->append({0, [](Ctx *c) {
        if (!c->haveFixture)
            return;
        c->ang0 = c->sepDeg();
        if (lockOk(c->ang0, c->offsetDeg))
        {
            c->mark(QStringLiteral("LOC-03b 自动移动结束后锁定夹角 %1°（期望 |%2|±%3）：OK")
                        .arg(c->ang0, 0, 'f', 4)
                        .arg(c->offsetDeg, 0, 'f', 4)
                        .arg(kLockToleranceDeg, 0, 'f', 2));
            return;
        }
        // 没达标先别判 FAIL：自动移动可能因帧泵抖动还没跑完。
        // 再等一轮重采样——这是**防假红**，不是放宽门槛（下一轮若仍不达标就判 FAIL）。
        c->needResample = true;
        c->nextDelay = 1500;
        c->note(QStringLiteral("LOC-03b 首次采样夹角 %1° 未达标 → 重采样一次（+1.5s）")
                    .arg(c->ang0, 0, 'f', 4));
    }});

    // ── 步骤 3：（仅重采样路径）再采样并定判 ────────────────────────────────
    steps->append({0, [](Ctx *c) {
        if (!c->haveFixture || !c->needResample)
            return;   // 一步到位的路径：这里什么都不做
        c->ang0 = c->sepDeg();
        c->mark(QStringLiteral("LOC-03b 重采样后锁定夹角 %1°（期望 |%2|±%3，重采样 1 次）：%4")
                    .arg(c->ang0, 0, 'f', 4)
                    .arg(c->offsetDeg, 0, 'f', 4)
                    .arg(kLockToleranceDeg, 0, 'f', 2)
                    .arg(lockOk(c->ang0, c->offsetDeg) ? QStringLiteral("OK")
                                                       : QStringLiteral("FAIL")));
    }});

    // ── 步骤 4：LOC-04a 记基线并跳 6 小时 ──────────────────────────────────
    steps->append({1200, [](Ctx *c) {
        if (!c->haveFixture)
            return;
        // 顺序要紧：**先**记下跳变前的目标地平方向，**再**跳时间。
        // 反过来的话 a0 就是跳变后的值，moved1 会恒为 0，
        // "信号存在"那条判据就变成了永远 FAIL 的假红（或永远 OK 的假绿）。
        c->prevAltAz = c->objAltAz();
        StelCore *core = c->core();
        if (core)
            core->setJD(c->facade->julianDay() + kTimeJumpDays);
    }});

    // ── 步骤 5：LOC-04b 「世界动了」∧「视向跟着」双断言 ────────────────────
    steps->append({0, [](Ctx *c) {
        if (!c->haveFixture)
            return;
        const Vec3d a1 = c->objAltAz();
        c->moved1 = c->prevAltAz.angle(a1) * 180.0 / M_PI;
        c->ang1 = c->sepDeg();
        const bool moved = c->moved1 > kMotionFloorDeg;
        const bool followed = lockOk(c->ang1, c->offsetDeg);
        c->mark(QStringLiteral("LOC-04 (a) 跳 %1 天后目标 AltAz 变化 %2°（门槛 >%3，"
                               "证明世界确实动了）：%4")
                    .arg(kTimeJumpDays, 0, 'f', 2)
                    .arg(c->moved1, 0, 'f', 2)
                    .arg(kMotionFloorDeg, 0, 'f', 1)
                    .arg(moved ? QStringLiteral("OK") : QStringLiteral("FAIL")));
        c->mark(QStringLiteral("LOC-04 (b) 跟踪中目标到视向夹角 %1°（期望 |%2|±%3）：%4")
                    .arg(c->ang1, 0, 'f', 4)
                    .arg(c->offsetDeg, 0, 'f', 4)
                    .arg(kLockToleranceDeg, 0, 'f', 2)
                    .arg(followed ? QStringLiteral("OK") : QStringLiteral("FAIL")));
    }});

    // ── 步骤 6：LOC-05a 改成"只定位不跟踪" ─────────────────────────────────
    steps->append({2500, [](Ctx *c) {
        if (!c->haveFixture)
            return;
        const bool l = c->facade->locateSelected(false);
        const bool notTracking = !c->facade->isTracking();
        c->mark(QStringLiteral("LOC-05a locateSelected(false) 只归中不跟踪（返回 %1，"
                               "isTracking=%2）：%3")
                    .arg(l ? QStringLiteral("true") : QStringLiteral("false"),
                         c->facade->isTracking() ? QStringLiteral("true") : QStringLiteral("false"),
                         (l && notTracking) ? QStringLiteral("OK") : QStringLiteral("FAIL")));
        // 记下这段"等待归中"开始时刻的目标地平方向：下面要用它量出
        // "等待期间天空自己转了多少"。见 LOC-05 (c) 的注释。
        c->settleAltAz = c->objAltAz();
    }});

    // ── 步骤 7：LOC-05b-prep 记 off0 并跳 6 小时 ────────────────────────────
    steps->append({1200, [](Ctx *c) {
        if (!c->haveFixture)
            return;
        c->off0 = c->sepDeg();
        // 等待期间天空的自转量（本自检的仿真速率 = 0.1 天/秒 ⇒ 墙钟 1 秒 = 天空 36°）。
        // 这不是缺陷，是测量环境：**只归中不跟踪**的视向在自动移动（1.5s）结束后
        // 就固定在地平系的某个方向上，天空继续转，目标自然越走越远。
        // 把它显式打出来，免得读者把"归中后 33°"误读成"归中没成功"。
        const double settleMove = c->settleAltAz.angle(c->objAltAz()) * 180.0 / M_PI;
        c->note(QStringLiteral("LOC-05b-note 归中等待的 2.5s 内天空自转 %1°"
                               "（自动移动 1.5s 追完之后视向即固定，余量即 off0 的来源）")
                    .arg(settleMove, 0, 'f', 2));
        c->prevAltAz = c->objAltAz();
        StelCore *core = c->core();
        if (core)
            core->setJD(c->facade->julianDay() + kTimeJumpDays);
    }});

    // ── 步骤 8：LOC-05c 「世界动了」∧「放开后不再跟随」 ───────────────────
    steps->append({0, [](Ctx *c) {
        if (!c->haveFixture)
            return;
        const Vec3d a1 = c->objAltAz();
        c->moved2 = c->prevAltAz.angle(a1) * 180.0 / M_PI;
        c->off1 = c->sepDeg();
        const bool moved = c->moved2 > kMotionFloorDeg;
        const bool notFollowed = c->off1 > kMotionFloorDeg;
        c->mark(QStringLiteral("LOC-05 (a2) 不跟踪时同样的跳变下目标 AltAz 变化 %1°"
                               "（门槛 >%2，证明对照组的信号同样存在）：%3")
                    .arg(c->moved2, 0, 'f', 2)
                    .arg(kMotionFloorDeg, 0, 'f', 1)
                    .arg(moved ? QStringLiteral("OK") : QStringLiteral("FAIL")));
        c->mark(QStringLiteral("LOC-05 (c) 不跟踪时夹角 %1° → 跳变后 %2°（门槛 >%3，"
                               "与 LOC-04(b) 的 %4° 形成判别性对照）：%5")
                    .arg(c->off0, 0, 'f', 4)
                    .arg(c->off1, 0, 'f', 2)
                    .arg(kMotionFloorDeg, 0, 'f', 1)
                    .arg(c->ang1, 0, 'f', 4)
                    .arg(notFollowed ? QStringLiteral("OK") : QStringLiteral("FAIL")));
    }});

    // ── 步骤 9：LOC-06 关跟踪 ───────────────────────────────────────────────
    steps->append({0, [](Ctx *c) {
        if (!c->haveFixture)
            return;
        const bool off = c->facade->setTracking(false);
        const bool cleaned = !c->facade->isTracking() && c->facade->trackedName().isEmpty();
        c->mark(QStringLiteral("LOC-06 setTracking(false)（返回 %1，isTracking=%2，"
                               "trackedName 空=%3）：%4")
                    .arg(off ? QStringLiteral("true") : QStringLiteral("false"),
                         c->facade->isTracking() ? QStringLiteral("true") : QStringLiteral("false"),
                         c->facade->trackedName().isEmpty() ? QStringLiteral("true")
                                                            : QStringLiteral("false"),
                         (off && cleaned) ? QStringLiteral("OK") : QStringLiteral("FAIL")));
    }});

    // ── 步骤 10：LOC-07a 重新定位（进入跟踪）────────────────────────────────
    steps->append({2500, [](Ctx *c) {
        if (!c->haveFixture)
            return;
        c->facade->locateSelected(true);
    }});

    // ── 步骤 11：LOC-07b 幂等重选不得打断跟踪 ──────────────────────────────
    steps->append({0, [](Ctx *c) {
        if (!c->haveFixture)
            return;
        const bool wasTracking = c->facade->isTracking();
        const QString trackedBefore = c->facade->trackedName();
        // 重选**同一个**对象：引擎的 selectedObjectChange（StelMovementMgr.cpp:757-766）
        // 会无条件 setFlagTracking(false)。T18 在 ObjectInfoModel::selectByStableId
        // 加了幂等闸（同一对象不发 setSelectedObject），这条就是那道闸的回归判据。
        const bool reselect = c->facade->selectByStableId(c->fixtureSid);
        const bool stillTracking = c->facade->isTracking();
        c->mark(QStringLiteral("LOC-07 幂等重选不打断跟踪（跟踪中「%1」→ 重选同一 stableId "
                               "返回 %2 → 跟踪 %3）：%4")
                    .arg(trackedBefore,
                         reselect ? QStringLiteral("true") : QStringLiteral("false"),
                         stillTracking ? QStringLiteral("true") : QStringLiteral("false"),
                         (wasTracking && reselect && stillTracking)
                             ? QStringLiteral("OK") : QStringLiteral("FAIL")));
    }});

    // ── 步骤 12：LOC-08 家园行星端到端 + 收尾 ──────────────────────────────
    steps->append({0, [](Ctx *c) {
        if (!c->haveFixture)
            return;
        StelCore *core = c->core();
        StelObjectMgr *mgr = c->objMgr();
        if (!core || !mgr)
        {
            c->note(QStringLiteral("LOC-08 家园行星端到端：SKIP（引擎不可用）"));
            return;
        }
        const QString home = core->getCurrentLocation().planetName;
        // 用公共 findAndSelect 而不是自己去 SolarSystem 里翻：这正是引擎自己在
        // setObserver 里做的事（StelCore.cpp:1584），不必另立一套。
        const bool found = mgr->findAndSelect(home);
        const StelObjectP obj = c->selected();
        const QString selName = obj ? obj->getEnglishName() : QString();
        if (!found || selName != home)
        {
            // 照实 SKIP：LOC-01 已证明守卫谓词本身正确，这里只是补一条端到端。
            // 首页地点是 Earth，而 Earth 能否被检索取决于配置，不是缺陷。
            c->note(QStringLiteral("LOC-08 家园行星端到端：SKIP（当前观察行星 \"%1\" "
                                   "不可检索/选不上，选到的是 \"%2\"）")
                        .arg(home, selName));
        }
        else
        {
            const bool located = c->facade->locateSelected(true);
            const bool refused = !located
                                 && c->facade->lastLocateRefusal() == QStringLiteral("home-planet")
                                 && !c->facade->isTracking();
            c->mark(QStringLiteral("LOC-08 家园行星端到端（选中 \"%1\" 后 locateSelected 返回 %2，"
                                   "理由 %3，isTracking=%4）：%5")
                        .arg(home,
                             located ? QStringLiteral("true") : QStringLiteral("false"),
                             c->facade->lastLocateRefusal(),
                             c->facade->isTracking() ? QStringLiteral("true")
                                                     : QStringLiteral("false"),
                             refused ? QStringLiteral("OK") : QStringLiteral("FAIL")));
        }

        // 收尾：清选中后必须**同时**看到跟踪归零。
        //
        // 这条判据测的是 `isTracking()` 的"合取真值"下界，而不是一句废话：
        // 引擎的 `flagTracking` 在 unSelect() 之后**不会被清**——原因是
        // StelMovementMgr::selectedObjectChange()（StelMovementMgr.cpp:757-766）
        // 整段包在 `if (objectMgr->getWasSelected())` 里，而 StelObjectMgr::unSelect()
        // 是**先** clear 掉 lastSelectedObjects **再** emit（StelObjectMgr.cpp:537-544）
        // ⇒ 槽里看到的就是"没有选中"，那条 `setFlagTracking(false)` 永不执行。
        // 所以这里必须：先真的进入跟踪（否则判据空洞），清选中，再读**原始标志**
        // 与**合取值**各一次——把两个值的分歧摆在证据里。
        c->facade->selectByStableId(c->fixtureSid);   // LOC-08 可能把选中换成了家园行星
        const bool entered = c->facade->locateSelected(true);
        const bool trackBefore = c->facade->isTracking();
        const bool rawBefore = c->mv() ? c->mv()->getFlagTracking() : false;
        c->facade->clearSelection();
        const bool trackAfter = c->facade->isTracking();
        const bool rawAfter = c->mv() ? c->mv()->getFlagTracking() : false;
        c->mark(QStringLiteral("LOC-08b 清选中后 isTracking 归零（进入跟踪 %1；"
                               "清前 合取=%2/引擎原始=%3 → 清后 合取=%4/引擎原始=%5）：%6")
                    .arg(entered ? QStringLiteral("true") : QStringLiteral("false"),
                         trackBefore ? QStringLiteral("true") : QStringLiteral("false"),
                         rawBefore ? QStringLiteral("true") : QStringLiteral("false"),
                         trackAfter ? QStringLiteral("true") : QStringLiteral("false"),
                         rawAfter ? QStringLiteral("true") : QStringLiteral("false"),
                         (entered && trackBefore && !trackAfter)
                             ? QStringLiteral("OK") : QStringLiteral("FAIL")));
    }});
#endif  // STELQUICK_HAS_ENGINE

    QTimer::singleShot(0, app, [tick]() { (*tick)(); });
}

} // namespace stelapp
