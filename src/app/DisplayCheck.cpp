// DisplayCheck 实现（T38-C）。判据清单、探针依据、负控见头注。
//
// 驱动沿用步骤表体例；delayAfter = "跑完**本步**之后等"（陷阱 41/69）。
// 数据面（属性读写/元对象/视觉树）**同步**，一步内做完；**帧级**判据必须
// "写一步、抓帧下一步"——写 0 即竞态（T37 那个假绿的同族病根）。
#include "app/DisplayCheck.hpp"

#include "app/ActionRouter.hpp"
#include "app/AppFacade.hpp"
#include "app/FrameCompare.hpp"
#include "render/legacy/FrameMailbox.hpp"

#include <QCoreApplication>
#include <QMetaMethod>
#include <QMetaProperty>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSet>
#include <QTimer>
#include <QVector>
#include <cmath>
#include <memory>

#if defined(STELQUICK_HAS_ENGINE) && defined(STELQUICK_WIDGETS_HOST)
#include "StelApp.hpp"
#include "StelCore.hpp"
#include "StelMovementMgr.hpp"
#include "StelSkyDrawer.hpp"
#endif

namespace stelapp {

namespace {

#if defined(STELQUICK_HAS_ENGINE) && defined(STELQUICK_WIDGETS_HOST)

using namespace stelapp::framecmp;
using CheckResult = DisplayCheck::Result;

//! 判别对照动作（默认大气）。DP-08 需要一个"已知能显著改帧"的引擎状态来证明
//! **比较方法活着**（否则"视场/投影没差异"可能是"方法测不出东西"的假绿）。
constexpr const char *kControlActionDefault = "actionShow_Atmosphere";

// 噪声容差（照抄 T37-C 标定：冻结下引擎渲染有亚 LSB 抖动）。
//
// 🔴 T38-C 第二轮实测修正了口径：**抖动面积会随场景状态变，幅度不会。**
//   DP-00（刚冻结、静止天空）实测 0.079%/Δ1；而"对照量还原核对"（大气开关来回后的
//   同一状态）实测 **0.557%/Δ2** —— 面积涨了 7 倍，幅度只从 1 到 2。
//   ⇒ 「低于噪声容差」**只能拿幅度当判据**（`maxDelta ≤ kNoiseMaxDelta`）；把面积
//   （differing/pixels）当必要条件会造出假红。面积只用来给 DP-00 当"渲染有没有真的
//   在动"的粗门（kNoiseGateRatio）。
constexpr int kNoiseMaxDelta = 2;
//! DP-00 专用：同状态两帧的差异**面积**粗门（2%）。它不是"噪声容差"，是"帧有没有
//! 静下来"的存在性门 —— 引擎没冻住的话差异面积会接近全图。
constexpr double kNoiseGateRatio = 0.02;

//! 帧级"生效"门：差异占比要**超过**噪声带一个数量级（取 1%）。
//! 实测标定：噪声 0.03% ｜ 视场/投影/星等改动都是**结构性**变化（几十个百分点）。
constexpr double kEffectMinRatio = 0.01;

bool belowNoise(const DiffStats &d)
{
    if (!d.comparable)
        return false;
    if (d.differing == 0)
        return true;
    // 只看**幅度**：只要没有任何像素"真的变了"（Δ ≤ 亚 LSB 抖动上限），就算静定。
    return d.maxDelta <= kNoiseMaxDelta;
}

double ratioOf(const DiffStats &d)
{
    return (d.comparable && d.pixels > 0) ? double(d.differing) / double(d.pixels) : 0.0;
}

//! 亮像素计数（max(R,G,B) ≥ thr）。**为什么不能用"全图差异占比"判星等截断**：
//! 极限星等只改**星点像素**，占比天然小，而全图里还有地面/地平线/背景 —— 用"占比
//! ≥1%"当门会把一个真实的、方向明确的效应误判成 FAIL（或反过来靠别的东西凑够 1%
//! 变成假绿）。所以改用**方向量 + 成对**：LOW（截到 mag 0 ≈ 无星）与 HIGH（全开）
//! 两个状态的亮像素数之差 = 被截掉的那部分星，方向单一、不依赖初值。
qint64 countBright(const FrameSample &s, int thr)
{
    if (!s.valid || s.image.isNull())
        return -1;
    qint64 n = 0;
    const int w = s.image.width();
    const int ht = s.image.height();
    for (int y = 0; y < ht; ++y)
    {
        const uchar *row = s.image.constScanLine(y);
        for (int x = 0; x < w; ++x)
        {
            const uchar *p = row + x * 4;
            if (qMax(qMax(int(p[0]), int(p[1])), int(p[2])) >= thr)
                ++n;
        }
    }
    return n;
}

//! 亮像素门限。星点即使小也画得很亮（叠加到饱和），96 能把"星"与"背景/暗天光"分开。
constexpr int kBrightThreshold = 96;

//! DP-10 的 HIGH 端（"全开"）。取 15 而不是上界 21：默认星表在合流 bundle 里到 ~11 等，
//! 15 已经等价于"全开"（再往上不会多出星），留一点余量避免恰好卡在星表边界上。
constexpr double kMagLimitHigh = 15.0;

//! DP-10 方向量门：LOW↔HIGH 之间亮像素至少要多出这么多。
//! 标定（第二轮实测）：LOW=0（一颗星都不画）｜HIGH=2460 ⇒ 取 500 = 信号的 1/5，
//! 同时是"噪声绝不可能造出的量级"（DP-00 抖动只有 725 个像素差 Δ≤1，做不到 500 个
//! ≥96 的亮像素）。
constexpr qint64 kMagLimitMinDelta = 500;

//! DP-10 静置对照门：同一状态下连抓两帧的亮像素计数允许的最大波动。
constexpr qint64 kBrightNoiseMaxDelta = 100;

//! 视觉树递归收集 objectName（**必须递归 childItems** —— T34 陷阱 45：
//! Repeater delegate 的 QObject 父链是空的，`QObject::findChild` 永远扫不到）。
QVector<QQuickItem *> collectByObjectName(QQuickItem *root, const QString &name)
{
    QVector<QQuickItem *> out;
    QVector<QQuickItem *> stack;
    if (root)
        stack.append(root);
    while (!stack.isEmpty())
    {
        QQuickItem *it = stack.takeLast();
        if (!it)
            continue;
        if (it->objectName() == name)
            out.append(it);
        const QList<QQuickItem *> kids = it->childItems();
        for (QQuickItem *k : kids)
            stack.append(k);
    }
    return out;
}

//! 视觉树里 objectName 以 prefix 开头的全部项（给 12 个 proj_* 用）。
QVector<QQuickItem *> collectByPrefix(QQuickItem *root, const QString &prefix)
{
    QVector<QQuickItem *> out;
    QVector<QQuickItem *> stack;
    if (root)
        stack.append(root);
    while (!stack.isEmpty())
    {
        QQuickItem *it = stack.takeLast();
        if (!it)
            continue;
        if (it->objectName().startsWith(prefix))
            out.append(it);
        const QList<QQuickItem *> kids = it->childItems();
        for (QQuickItem *k : kids)
            stack.append(k);
    }
    return out;
}

struct Ctx
{
    QCoreApplication *app = nullptr;
    AppFacade *facade = nullptr;
    ActionRouter *router = nullptr;
    QQuickWindow *window = nullptr;
    FrameMailbox *mailbox = nullptr;
    std::function<void(const CheckResult &)> onDone;
    CheckResult result;
    QStringList lines;
    int passed = 0;
    int total = 0;
    bool unavailable = false;
    bool inconclusive = false;
    int nextDelay = 0;

    QString controlAction = QString::fromLatin1(kControlActionDefault);

    // 初值台账（收尾还原 + DP-11 比对）。
    double relScale = 1.0, absScale = 1.0, lp = 0.0, magLimit = 0.0, fov = 60.0;
    bool magLimitOn = false;
    QString projKey;
    QString uiProjKey;          //!< DP-06 用：UI 上的当前投影 key（取自引擎初值）
    bool atmWasOn = false;
    bool atmChanged = false;
    bool pausedByUs = false;
    // ── 布场昼夜确定性（T42 收口轮实测的血泪，TRAPS 80 的深层变体）──────────
    // 开机 JD = 墙钟 ⇒ 白天跑批时"冻结+大气关"的天上满是**太阳照亮的卫星**
    // （卫星位置用真实 UTC，冻结仿真它照样动）⇒ DP-00 噪声底 Δ220、INCONCLUSIVE。
    // 治法：布场把 JD 钉在**固定夜时刻**（UT 22:00 ⇒ 卫星不日照、天空只有静星），
    // 收尾还原 —— 让测量环境昼夜一致，不再随跑批时段漂移。
    double jdRef = 0.0;
    bool jdChangedByUs = false;
    int frameRefNonBlackOk = 0;

    FrameSample refA;
    FrameSample lastName;
    FrameSample lowFrame;       //!< DP-10 的 LOW 端帧（留作读数）
    qint64 lowBright = -1;      //!< DP-10 的 LOW 端亮像素数（-1 = 帧无效）

    void note(const QString &line) { lines.append(line); }

    void mark(bool ok, const QString &line)
    {
        ++total;
        if (ok)
            ++passed;
        lines.append(QStringLiteral("  [%1] %2")
                         .arg(ok ? QStringLiteral("PASS") : QStringLiteral("FAIL"))
                         .arg(line));
    }

    FrameSample grabUp() const { return grabUpstream(mailbox); }

    //! 抓一帧上游；若"非黑比例"远低于参考（撕裂/缺背景层的帧，T37-X2），返回无效。
    FrameSample grabUpStable() const
    {
        FrameSample s = grabUp();
        if (!s.valid)
            return s;
        if (refA.valid && s.nonBlack < refA.nonBlack * 0.5)
        {
            FrameSample bad;
            return bad;   // 异常帧 ⇒ 调用方记 INCONCLUSIVE，不记 FAIL
        }
        return s;
    }

    //! 把**全部**显示参数写回初值台账。
    //! 🔴 帧级判据的基准必须干净（T38-C 首轮实测的血泪）：数据面 DP-01..DP-03 把
    //!   `absoluteStarScale` / `lightPollutionLuminance` / `flag+customStarMagLimit`
    //!   都改过且没还原，若带着残留抓"参考帧"，DP-09/10/11 就全比在一个**被污染的
    //!   假基准**上 —— 参考帧 `nonBlack` 被光污染顶到 1.0000，DP-11 必红。
    void restoreAllDisplayParams()
    {
        if (!facade)
            return;
        facade->setStarRelativeScale(relScale);
        facade->setStarAbsoluteScale(absScale);
        facade->setLightPollutionLuminance(lp);
        facade->setStarMagnitudeLimit(magLimit);
        facade->setStarMagnitudeLimitEnabled(magLimitOn);
        facade->setProjectionTypeKey(projKey);
        facade->setFieldOfViewNow(fov);
    }

    void finish()
    {
        result.ran = true;
        result.pass = !unavailable && !inconclusive && total > 0 && passed == total;
        result.unavailable = unavailable;
        result.inconclusive = inconclusive;
        result.passed = passed;
        result.total = total;
        if (unavailable)
            result.summary = QStringLiteral("显示参数自检：环境不满足（UNAVAILABLE）");
        else
            result.summary = QStringLiteral("显示参数自检：判据 %1/%2").arg(passed).arg(total);
        result.details = lines;
        onDone(result);
    }
};

struct Step
{
    int delayAfter = 0;
    std::function<void(Ctx *)> body;
};

#endif  // STELQUICK_HAS_ENGINE && STELQUICK_WIDGETS_HOST

} // namespace

void DisplayCheck::run(QCoreApplication *app,
                       AppFacade *facade,
                       ActionRouter *router,
                       QQuickWindow *window,
                       FrameMailbox *mailbox,
                       const std::function<void(const Result &)> &onDone,
                       int delayMs)
{
#if !defined(STELQUICK_HAS_ENGINE) || !defined(STELQUICK_WIDGETS_HOST)
    Q_UNUSED(app)
    Q_UNUSED(facade)
    Q_UNUSED(router)
    Q_UNUSED(window)
    Q_UNUSED(mailbox)
    Q_UNUSED(delayMs)
    Result r;
    r.unavailable = true;
    r.summary = QStringLiteral("显示参数自检需要合流形态构建");
    QTimer::singleShot(0, app, [onDone, r]() { onDone(r); });
#else
    auto *ctx = new Ctx;
    ctx->app = app;
    ctx->facade = facade;
    ctx->router = router;
    ctx->window = window;
    ctx->mailbox = mailbox;
    ctx->onDone = onDone;
    ctx->controlAction = qEnvironmentVariable("STELQUICK_DISPLAY_CONTROL_ACTION",
                                              QString::fromLatin1(kControlActionDefault));

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

    // ── S1：布场 + 初值台账 ────────────────────────────────────────────────
    // 冻结仿真（帧静定）+ **大气置关**（让星星可见 —— 星等判据的前提，T37 血泪）。
    // ⚠️ delayAfter 6500：跳 JD 后瞳孔适应（StelSkyDrawer::reportLuminanceInFov 的
    // log 平滑律，transitionSpeed=0.2 ⇒ 每秒只剩 35% 差距）需要数秒收敛，
    // 否则 HP-00/DP-00 噪声底门吃到的还是 ramp 中段的帧（T42 收口轮实测）。
    steps->append({6500, [](Ctx *c) {
        StelSkyDrawer *sd = StelApp::getInstance().getCore()->getSkyDrawer();
        c->relScale = sd->getRelativeStarScale();
        c->absScale = sd->getAbsoluteStarScale();
        c->lp = sd->getLightPollutionLuminance();
        c->magLimit = sd->getCustomStarMagnitudeLimit();
        c->magLimitOn = sd->getFlagStarMagnitudeLimit();
        c->fov = StelApp::getInstance().getCore()->getMovementMgr()->getCurrentFov();
        c->projKey = StelApp::getInstance().getCore()->getCurrentProjectionTypeKey();
        c->uiProjKey = c->projKey;
        c->refA = FrameSample();
        c->lastName = FrameSample();

        c->note(QStringLiteral("初值：relativeStarScale=%1 absoluteStarScale=%2 "
                               "lightPollutionLuminance=%3 customStarMagLimit=%4 "
                               "flagStarMagnitudeLimit=%5 fov=%6 projection=%7")
                    .arg(c->relScale)
                    .arg(c->absScale)
                    .arg(c->lp)
                    .arg(c->magLimit)
                    .arg(c->magLimitOn ? "true" : "false")
                    .arg(c->fov)
                    .arg(c->projKey));

        if (c->facade)
        {
            c->facade->setSimulationPaused(true);
            c->pausedByUs = true;
            // 固定夜 JD（见 Ctx 注释）：先记原值再写（陷阱 9：判据改的环境同批还原）。
            c->jdRef = c->facade->julianDay();
            c->facade->setJulianDay(2461000.4167);   // ≈2025-11-16 22:00 UT（夜）
            c->jdChangedByUs = true;
        }
        // 大气置关（隔离前提）：STELQUICK_HAS_ENGINE 下 getFlagAtmosphere 走模块。
        c->atmWasOn = c->facade && c->facade->actionChecked(QStringLiteral("actionShow_Atmosphere"));
        if (c->atmWasOn && c->router)
        {
            c->router->trigger(QStringLiteral("actionShow_Atmosphere"));
            c->atmChanged = true;
        }
        c->note(QStringLiteral("布场：已冻结仿真 + 大气置关（原值 %1）—— 星等/亮度的视觉"
                               "效应只在星星可见时存在")
                    .arg(c->atmWasOn ? "开" : "关"));
    }});

    // ── S2：帧静定基线（参考帧A）+ 噪声底 ──────────────────────────────────
    steps->append({400, [](Ctx *c) {
        c->refA = c->grabUp();
        c->note(QStringLiteral("参考帧A（上游）：%1").arg(frameText(c->refA)));
    }});

    steps->append({300, [](Ctx *c) {
        const FrameSample again = c->grabUp();
        const DiffStats d = diffOf(c->refA, again);
        c->note(QStringLiteral("噪声底读数（同状态两帧）：%1").arg(diffText(d)));
        const bool stable = belowNoise(d) && ratioOf(d) < kNoiseGateRatio;
        if (!stable)
        {
            c->mark(false, QStringLiteral("DP-00 噪声底门：冻结后同状态两帧差异 %1 —— **未静定**"
                                          "（要求幅度 Δ≤%2 ∧ 差异面积<%3 —— 面积只当粗门，"
                                          "真正的噪声判据是幅度）⇒ 后续像素判据口径不可信 ⇒ "
                                          "整套 INCONCLUSIVE")
                               .arg(diffText(d))
                               .arg(kNoiseMaxDelta)
                               .arg(kNoiseGateRatio));
            c->inconclusive = true;
        }
        else
        {
            c->mark(true, QStringLiteral("DP-00 噪声底门：冻结后同状态两帧**静定**（实测 %1；"
                                         "噪声判据取 **幅度** Δ≤%2，不取面积 —— 实测抖动面积"
                                         "会随场景状态变）—— 后续像素判据的口径有效")
                              .arg(diffText(d))
                              .arg(kNoiseMaxDelta));
        }
    }});

    // ── S3：数据面 DP-01..DP-05（全同步，一步做完）─────────────────────────
    steps->append({300, [](Ctx *c) {
        StelSkyDrawer *sd = StelApp::getInstance().getCore()->getSkyDrawer();
        StelCore *core = StelApp::getInstance().getCore();

        // DP-01 状态面往返：façade 写 → **引擎 getter 独立回读**（陷阱 43）。
        c->facade->setStarRelativeScale(2.5);
        c->facade->setStarAbsoluteScale(2.0);
        c->facade->setLightPollutionLuminance(1.0e-3);
        c->facade->setStarMagnitudeLimit(8.0);
        c->facade->setStarMagnitudeLimitEnabled(true);
        const bool dp1 = qFuzzyCompare(sd->getRelativeStarScale() + 1.0, 2.5 + 1.0)
                         && qFuzzyCompare(sd->getAbsoluteStarScale() + 1.0, 2.0 + 1.0)
                         && qFuzzyCompare(sd->getLightPollutionLuminance() + 1e-9, 1.0e-3 + 1e-9)
                         && qFuzzyCompare(sd->getCustomStarMagnitudeLimit() + 1.0, 8.0 + 1.0)
                         && sd->getFlagStarMagnitudeLimit();
        c->mark(dp1, QStringLiteral("DP-01 状态面往返：5 项 façade 写 → **引擎 getter 独立回读**"
                                    "一致（rel=%1 abs=%2 lp=%3 mag=%4 flag=%5）")
                         .arg(sd->getRelativeStarScale())
                         .arg(sd->getAbsoluteStarScale())
                         .arg(sd->getLightPollutionLuminance())
                         .arg(sd->getCustomStarMagnitudeLimit())
                         .arg(sd->getFlagStarMagnitudeLimit() ? "true" : "false"));

        // DP-02 范围闸：写越界 ⇒ 引擎收到**上界** ∧ refusal=="out-of-range"。
        c->facade->setStarRelativeScale(99.0);
        const double clamped = sd->getRelativeStarScale();
        const QString r1 = c->facade->lastDisplayRefusal();
        c->facade->setStarRelativeScale(1.0);
        const QString r2 = c->facade->lastDisplayRefusal();
        c->mark(qFuzzyCompare(clamped + 1.0, c->facade->starRelativeScaleMax() + 1.0)
                    && r1 == QStringLiteral("out-of-range")
                    && r2 == QStringLiteral("ok"),
                QStringLiteral("DP-02 范围闸：写 99 ⇒ 引擎收到上界 %1（不是 99）∧ refusal=%2；"
                               "写回合法值 ⇒ refusal=%3 —— 引擎 setter 不夹取，闸门在 façade")
                    .arg(clamped)
                    .arg(r1, r2));

        // DP-03 投影 12 key 往返 + maxFov 是**活属性**（随投影变）。
        const QStringList keys = c->facade->projectionTypeKeys();
        int mismatch = 0;
        QSet<QString> maxFovs;
        QStringList detail;
        for (const QString &k : keys)
        {
            const bool ok = c->facade->setProjectionTypeKey(k);
            const QString got = core->getCurrentProjectionTypeKey();
            if (!ok || got != k)
                ++mismatch;
            const QString mf = QString::number(core->getMovementMgr()->getMaxFov(), 'f', 1);
            maxFovs.insert(mf);
            detail.append(QStringLiteral("%1=%2(maxFov %3)").arg(k, got, mf));
        }
        c->note(QStringLiteral("DP-03 逐 key 读数：%1").arg(detail.join(QStringLiteral("; "))));
        c->mark(mismatch == 0 && keys.size() == 12 && maxFovs.size() >= 3,
                QStringLiteral("DP-03 投影：%1/12 个 key 往返一致 ∧ maxFov 取到 %2 个不同值"
                               "（%3）⇒ 上限是**投影的函数**，滑块上限必须跟着它走")
                    .arg(keys.size() - mismatch)
                    .arg(maxFovs.size())
                    .arg(QStringList(maxFovs.constBegin(), maxFovs.constEnd())
                             .join(QStringLiteral("/"))));

        // DP-04 白名单闸：写乱码 ⇒ 拒绝 ∧ 引擎 key **不变**。
        const QString before = core->getCurrentProjectionTypeKey();
        const bool accepted = c->facade->setProjectionTypeKey(QStringLiteral("Nonsense_Key_T38"));
        const QString after = core->getCurrentProjectionTypeKey();
        c->mark(!accepted && after == before
                    && c->facade->lastDisplayRefusal() == QStringLiteral("unknown-projection"),
                QStringLiteral("DP-04 白名单闸：写乱码 ⇒ accepted=%1 ∧ 引擎 key %2→%3（%4）∧ "
                               "refusal=%5 —— 引擎自己会**静默兜底 Stereographic**，闸门挡住它")
                    .arg(accepted ? "true" : "false", before, after,
                         before == after ? QStringLiteral("未变") : QStringLiteral("**变了**"),
                         c->facade->lastDisplayRefusal()));

        // DP-05 拒绝理由必须是 **Q_PROPERTY**（T19 血泪：Q_INVOKABLE 在 QML 绑定里
        // 读到函数对象、与字符串比较恒 false）。
        const QMetaObject *mo = c->facade->metaObject();
        const int pi = mo->indexOfProperty("lastDisplayRefusal");
        const bool hasProp = pi >= 0;
        const bool hasNotify = hasProp && mo->property(pi).hasNotifySignal();
        c->mark(hasProp && hasNotify,
                QStringLiteral("DP-05 lastDisplayRefusal 是 Q_PROPERTY=%1 ∧ 有 NOTIFY=%2"
                               "（QML 绑定才比得出字符串）")
                    .arg(hasProp ? "true" : "false")
                    .arg(hasNotify ? "true" : "false"));

        // 把投影还原到初值，供后续帧级判据用（DP-03 把最后一个 key 留在末尾）。
        c->facade->setProjectionTypeKey(c->projKey);
    }});

    // ── S4：DP-06 UI 控件齐备（视觉树递归）──────────────────────────────────
    steps->append({200, [](Ctx *c) {
        QQuickItem *root = c->window ? c->window->contentItem() : nullptr;
        if (!root)
        {
            c->mark(false, QStringLiteral("DP-06 UI 控件：拿不到 contentItem ⇒ 无法判"));
            return;
        }
        const QStringList singles{"navDisplayButton", "displayRelScaleSlider",
                                  "displayAbsScaleSlider", "displayLightPollutionSlider",
                                  "displayMagLimitCheck", "displayMagLimitSlider",
                                  "displayFovSlider"};
        QStringList missing;
        for (const QString &n : singles)
            if (collectByObjectName(root, n).isEmpty())
                missing.append(n);
        const QVector<QQuickItem *> projButtons = collectByPrefix(root, QStringLiteral("proj_"));
        // 诊断：Repeater 本身在不在、它的 count 是多少 —— "按钮 0 个"到底是
        // "Repeater 没建项"还是"建了但 objectName 不对/不在视觉树里"，靠这一行分清。
        const QVector<QQuickItem *> reps =
            collectByObjectName(root, QStringLiteral("displayProjectionRepeater"));
        const QString repInfo =
            reps.isEmpty()
                ? QStringLiteral("displayProjectionRepeater **未找到**")
                : QStringLiteral("displayProjectionRepeater.count=%1")
                      .arg(reps.first()->property("count").toInt());
        c->note(QStringLiteral("DP-06 视觉树扫描：单件 %1/%2 找到；proj_* 共 %3 个；%4")
                    .arg(singles.size() - missing.size())
                    .arg(singles.size())
                    .arg(projButtons.size())
                    .arg(repInfo));
        if (!missing.isEmpty())
            c->note(QStringLiteral("DP-06 缺失：%1").arg(missing.join(QStringLiteral(", "))));
        c->mark(missing.isEmpty() && projButtons.size() == 12,
                QStringLiteral("DP-06 UI 控件齐备：7 个单件全找到 ∧ 12 个投影按钮全找到"
                               "（**走视觉树递归** —— Repeater delegate 的 QObject 父链是"
                               "空的，findChild 扫不到）"));
    }});

    // ── S5：DP-07 绑定腿（引擎改 ⇒ UI 跟）──────────────────────────────────
    steps->append({120, [](Ctx *c) {
        // **绕过 façade** 直接写引擎（模拟"引擎侧自己变了"：快捷键、动作、别的面板）。
        StelApp::getInstance().getCore()->getSkyDrawer()->setRelativeStarScale(3.25);
        c->note(QStringLiteral("DP-07 已由**引擎侧**把 relativeStarScale 写成 3.25"
                               "（不经 façade）⇒ 下一拍看 UI 滑块跟不跟"));
    }});

    steps->append({250, [](Ctx *c) {
        QQuickItem *root = c->window ? c->window->contentItem() : nullptr;
        const QVector<QQuickItem *> sliders =
            collectByObjectName(root, QStringLiteral("displayRelScaleSlider"));
        double uiValue = -1.0;
        if (!sliders.isEmpty())
            uiValue = sliders.first()->property("value").toDouble();
        const double engineValue =
            StelApp::getInstance().getCore()->getSkyDrawer()->getRelativeStarScale();
        c->mark(!sliders.isEmpty() && qAbs(uiValue - engineValue) < 1e-6,
                QStringLiteral("DP-07 绑定腿：引擎侧改 %1 ⇒ UI 滑块 value=%2（UI 必须跟引擎，"
                               "否则面板停在首帧）")
                    .arg(engineValue)
                    .arg(uiValue));
        // 🔴 **全量还原**（不只是 DP-07 改的那一项）——理由见 Ctx::restoreAllDisplayParams。
        c->restoreAllDisplayParams();
        c->note(QStringLiteral("DP-07 后已**全量还原**显示参数到初值台账（帧级判据的基准"
                               "必须干净）"));
    }});

    // ── S6：帧级判据用的新参考帧（此刻才算真的回到初值）────────────────────
    steps->append({450, [](Ctx *c) {
        c->refA = c->grabUp();
        c->lastName = c->refA;
        c->note(QStringLiteral("参考帧A'（**全量还原后**）：%1 亮像素(≥%2)=%3")
                    .arg(frameText(c->refA))
                    .arg(kBrightThreshold)
                    .arg(countBright(c->refA, kBrightThreshold)));
    }});

    // ── S7：DP-08 视场写（读/抓帧放下一步）──────────────────────────────────
    // ⚠️ 延迟必须挂在**写步**上：`delayAfter` 的语义是"跑完**本步**之后等"（陷阱 41/69）。
    //   首轮实测把它挂到了读步 ⇒ 写后 0ms 就抓帧 ⇒ 拿到的是**上一次的帧缓冲**，
    //   差异逐位相同（哈希都相同）⇒ DP-08 假红。写 0 即竞态，这条是原样复现。
    steps->append({400, [](Ctx *c) {
        c->facade->setFieldOfViewNow(30.0);
        c->note(QStringLiteral("DP-08 已 setFieldOfViewNow(30)（原 %1）").arg(c->fov));
    }});

    // ── S8：DP-08 判 + 判别对照（大气开）────────────────────────────────────
    steps->append({400, [](Ctx *c) {
        const FrameSample b = c->grabUpStable();
        const DiffStats d = diffOf(c->refA, b);
        c->note(QStringLiteral("DP-08 帧读数（视场 %1→30）：%2")
                    .arg(c->fov)
                    .arg(diffText(d)));
        c->mark(b.valid && d.comparable && ratioOf(d) >= kEffectMinRatio,
                QStringLiteral("DP-08 视场生效（成对）：改视场后上游帧差异占比 %1 ≥ 门 %2")
                    .arg(ratioOf(d), 0, 'f', 4)
                    .arg(kEffectMinRatio));
        if (!b.valid)
            c->inconclusive = true;

        // 判别对照：同一套取帧+比较方法，对**大气开关**必须测出显著差异。
        if (c->router)
            c->router->trigger(c->controlAction);
        c->lastName = b;
        c->note(QStringLiteral("判别对照：已 trigger(%1)（回读放下一步）").arg(c->controlAction));
    }});

    // ── S9：判别对照判（承重）───────────────────────────────────────────────
    steps->append({400, [](Ctx *c) {
        const FrameSample ctl = c->grabUpStable();
        const DiffStats d = diffOf(c->lastName, ctl);
        c->note(QStringLiteral("判别对照帧读数：%1").arg(diffText(d)));
        c->mark(ctl.valid && d.comparable && ratioOf(d) >= kEffectMinRatio,
                QStringLiteral("DP-08b 判别对照（承重）：同一方法对 %1 测出差异占比 %2 ≥ 门 %3"
                               " ⇒ 方法活着，DP-08 的差异不是「测不出东西」的假绿")
                    .arg(c->controlAction)
                    .arg(ratioOf(d), 0, 'f', 4)
                    .arg(kEffectMinRatio));
        if (!ctl.valid)
            c->inconclusive = true;
        // 大气还原（对照量的还原放在写步之后、下一步读）。
        if (c->router)
            c->router->trigger(c->controlAction);
    }});

    // ── S10：DP-09 投影写（先在下一步确认大气已回，再切投影）───────────────
    steps->append({400, [](Ctx *c) {
        const FrameSample back = c->grabUpStable();
        // **基线要用 lastName（触发对照**之前**的那一帧，视场同为 30、大气同为关）**，
        // 不能用参考帧A'（那是视场 60）—— 否则这个"核对"会把视场差异算进来，
        // 读出一个 99.9% 的假警报（首轮实测就是这么误导的）。
        const DiffStats d = diffOf(c->lastName, back);
        c->note(QStringLiteral("对照量还原核对（大气回原值，基线=触发前帧 fov30）：%1"
                               " 亮像素=%2 （**非判据**，只报读数）")
                    .arg(diffText(d))
                    .arg(countBright(back, kBrightThreshold)));

        // 视场还原（DP-08 改成了 30）+ 切投影。
        c->facade->setFieldOfViewNow(c->fov);
        c->facade->setProjectionTypeKey(QStringLiteral("ProjectionFisheye"));
        c->lastName = c->refA;
        c->note(QStringLiteral("DP-09 已还原视场并切 ProjectionFisheye（抓帧放下一步）"));
    }});

    // ── S11：DP-09 判（投影生效）───────────────────────────────────────────
    steps->append({400, [](Ctx *c) {
        const FrameSample f = c->grabUpStable();
        const DiffStats d = diffOf(c->refA, f);
        c->note(QStringLiteral("DP-09 帧读数（投影 %1→Fisheye）：%2")
                    .arg(c->projKey)
                    .arg(diffText(d)));
        const bool projOk = StelApp::getInstance().getCore()->getCurrentProjectionTypeKey()
                            == QStringLiteral("ProjectionFisheye");
        c->mark(projOk && f.valid && d.comparable && ratioOf(d) >= kEffectMinRatio,
                QStringLiteral("DP-09 投影生效（成对）：key 已是 Fisheye ∧ 上游帧差异占比 %1 "
                               "≥ 门 %2")
                    .arg(ratioOf(d), 0, 'f', 4)
                    .arg(kEffectMinRatio));
        if (!f.valid)
            c->inconclusive = true;
    }});

    // ── S12：DP-10 星等截断 —— 写 LOW（截到 mag 0 ⇒ 星表近乎清空）──────────
    // ⚠️ 延迟挂在**写步**（同 S7 的血泪）。
    steps->append({400, [](Ctx *c) {
        // 前提：大气已关（星星可见——星等判据的前提，也是 DP-11 复原的基准条件）。
        // 投影先还原到初值，避免把 DP-09 的鱼眼带进星等量。
        c->facade->setProjectionTypeKey(c->projKey);
        c->facade->setStarMagnitudeLimitEnabled(true);
        c->facade->setStarMagnitudeLimit(c->facade->starMagnitudeLimitMin());
        c->note(QStringLiteral("DP-10 LOW：已开手动极限星等并压到下界 %1（原设定 %2，"
                               "原开关 %3）")
                    .arg(c->facade->starMagnitudeLimitMin())
                    .arg(c->magLimit)
                    .arg(c->magLimitOn ? "true" : "false"));
    }});

    // ── S13：DP-10 取 LOW 帧（第一拍）─────────────────────────────────────
    steps->append({300, [](Ctx *c) {
        c->lowFrame = c->grabUpStable();
        c->lowBright = countBright(c->lowFrame, kBrightThreshold);
        c->note(QStringLiteral("DP-10 LOW 帧(1)：亮像素(≥%1)=%2")
                    .arg(kBrightThreshold)
                    .arg(c->lowBright));
    }});

    // ── S13b：DP-10a 静置对照（同状态第二拍）+ 写 HIGH ─────────────────────
    // 为什么要这一拍：亮像素计数是**新引入的量**，必须先证明它在"什么都不改"的时候
    // 是稳的，否则下一步 LOW↔HIGH 的跳变可能只是抖动（判别对照必须换来源、必须
    // 有自己的静置底 —— 陷阱：别把"没测出东西"当"东西不存在"，也别把抖动当信号）。
    steps->append({400, [](Ctx *c) {
        const FrameSample low2 = c->grabUpStable();
        const qint64 low2Bright = countBright(low2, kBrightThreshold);
        const qint64 drift = qAbs(low2Bright - c->lowBright);
        c->mark(c->lowBright >= 0 && low2Bright >= 0 && drift <= kBrightNoiseMaxDelta,
                QStringLiteral("DP-10a 静置对照（承重）：**状态不变**时连抓两帧，亮像素 "
                               "%1→%2（漂移 %3 ≤ 门 %4）⇒ 计数在噪声下稳定 ⇒ 下一步的跳变"
                               "不是抖动凑出来的")
                    .arg(c->lowBright)
                    .arg(low2Bright)
                    .arg(drift)
                    .arg(kBrightNoiseMaxDelta));
        if (c->lowBright < 0 || low2Bright < 0)
            c->inconclusive = true;
        c->facade->setStarMagnitudeLimit(kMagLimitHigh);
    }});

    // ── S14：DP-10b 判（LOW↔HIGH 方向量成对）+ 全量还原 ────────────────────
    steps->append({500, [](Ctx *c) {
        const FrameSample hi = c->grabUpStable();
        const qint64 hiBright = countBright(hi, kBrightThreshold);
        const qint64 delta = hiBright - c->lowBright;
        // ⚠️ 这里**故意不**再附加"vs 参考帧A' 差异占比 ≥1%"：第二轮实测 HIGH 帧与参考帧
        //   A' **逐位相同**（哈希相同）—— 初值（flag 关 ⇒ 走引擎自算的有效限）本来就是
        //   "全开"，拿它当对照等于在验证一个**没被声称过的命题**（"15 与初值不同"），
        //   必然红。真正被声称的是"截断跟着**设定值**走"，那由 LOW↔HIGH 给。
        const DiffStats dRef = diffOf(c->refA, hi);
        const DiffStats dLow = diffOf(c->refA, c->lowFrame);
        c->note(QStringLiteral("DP-10 HIGH 帧：亮像素=%1（LOW=%2，差=%3）；HIGH vs 参考帧A'："
                               "%4 ⇒ 初值本身即「全开」；LOW vs 参考帧A'：%5")
                    .arg(hiBright)
                    .arg(c->lowBright)
                    .arg(delta)
                    .arg(diffText(dRef))
                    .arg(diffText(dLow)));
        c->mark(c->lowBright >= 0 && hiBright >= 0 && delta >= kMagLimitMinDelta,
                QStringLiteral("DP-10b 星等截断生效（成对 · 方向量）：极限星等 %1↔%2 使亮像素"
                               "%3↔%4（差 %5 ≥ 门 %6）⇒ 星表截断确实跟着设定值走。用**方向量**"
                               "而不是全图占比：星点像素占比天然小、会被地面/背景稀释；"
                               "LOW↔HIGH 成对**不依赖初值长什么样**，方向单一")
                    .arg(c->facade->starMagnitudeLimitMin())
                    .arg(kMagLimitHigh)
                    .arg(c->lowBright)
                    .arg(hiBright)
                    .arg(delta)
                    .arg(kMagLimitMinDelta));
        if (c->lowBright < 0 || hiBright < 0)
            c->inconclusive = true;

        c->restoreAllDisplayParams();
        c->note(QStringLiteral("收尾：已全量还原（核对放下一步）"));
    }});

    // ── S14：DP-11 复原（getter 回初值 + 帧回参考）─────────────────────────
    steps->append({500, [](Ctx *c) {
        StelSkyDrawer *sd = StelApp::getInstance().getCore()->getSkyDrawer();
        StelCore *core = StelApp::getInstance().getCore();
        const bool stateOk =
            qFuzzyCompare(sd->getRelativeStarScale() + 1.0, c->relScale + 1.0)
            && qFuzzyCompare(sd->getAbsoluteStarScale() + 1.0, c->absScale + 1.0)
            && qFuzzyCompare(sd->getLightPollutionLuminance() + 1e-12, c->lp + 1e-12)
            && qFuzzyCompare(sd->getCustomStarMagnitudeLimit() + 1.0, c->magLimit + 1.0)
            && sd->getFlagStarMagnitudeLimit() == c->magLimitOn
            && core->getCurrentProjectionTypeKey() == c->projKey
            && qAbs(core->getMovementMgr()->getCurrentFov() - c->fov) < 0.5;
        const FrameSample g = c->grabUpStable();
        const DiffStats d = diffOf(c->refA, g);
        c->note(QStringLiteral("DP-11 复原帧读数：%1").arg(diffText(d)));
        c->mark(stateOk && (!g.valid || belowNoise(d)),
                QStringLiteral("DP-11 复原：引擎 getter 全回初值=%1 ∧ 复原帧 vs 参考帧A' "
                               "差异占比 %2（低于噪声容差=%3）")
                    .arg(stateOk ? "true" : "false")
                    .arg(ratioOf(d), 0, 'f', 4)
                    .arg(belowNoise(d) ? "true" : "false"));
        if (!g.valid)
            c->inconclusive = true;
    }});

    // ── S15：还原环境（大气/仿真/JD）────────────────────────────────────────
    steps->append({0, [](Ctx *c) {
        if (c->router && c->atmChanged)
            c->router->trigger(QStringLiteral("actionShow_Atmosphere"));
        if (c->facade && c->jdChangedByUs)
            c->facade->setJulianDay(c->jdRef);
        if (c->facade && c->pausedByUs)
            c->facade->setSimulationPaused(false);
        c->note(QStringLiteral("收尾：大气还原=%1 仿真暂停还原=%2 JD 还原=%3")
                    .arg(c->atmChanged ? 1 : 0)
                    .arg(c->pausedByUs ? 1 : 0)
                    .arg(c->jdChangedByUs ? 1 : 0));
    }});

    QTimer::singleShot(delayMs, app, [tick]() { (*tick)(); });
#endif
}

} // namespace stelapp
