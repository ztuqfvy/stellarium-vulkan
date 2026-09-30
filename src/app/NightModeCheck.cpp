// NightModeCheck 实现（T37-C）。判据清单、探针依据、负控见头注。
//
// 驱动沿用步骤表体例；delayAfter = "跑完本步之后等"（陷阱 41）——
// 翻转开关后的 900ms 是"效果有机会生效"的窗口，写 0 即竞态。
#include "app/NightModeCheck.hpp"

#include "app/ActionRouter.hpp"
#include "app/AppFacade.hpp"
#include "app/FrameCompare.hpp"
#include "render/legacy/FrameMailbox.hpp"

#include <QCoreApplication>
#include <QQuickWindow>
#include <QThread>
#include <QTimer>
#include <QVector>
#include <memory>

#if defined(STELQUICK_HAS_ENGINE) && defined(STELQUICK_WIDGETS_HOST)
#include "StelApp.hpp"
#endif

namespace stelapp {

namespace {

#if defined(STELQUICK_HAS_ENGINE) && defined(STELQUICK_WIDGETS_HOST)

using namespace stelapp::framecmp;
using CheckResult = NightModeCheck::Result;

constexpr const char *kNightAction = "actionShow_Night_Mode";
// 判别对照动作（默认星座线）。NC-04 的本质只需要"一个能真实改变上游像素的
// 引擎状态"，星座线只是随手挑中的一个。T37 实测星座线开关后间歇出现"缺天空
// 背景层"的撕裂帧（T37-X2），故做成可覆盖 —— 用它排查"撕裂是否该动作专属"。
constexpr const char *kControlActionDefault = "actionShow_Constellation_Lines";

// NC-03② 的门槛（T37-B 实测标定：实现前 0/0.0000，实现后 100%/163.6）。
// 90% = 视口内几乎全部像素都要被滤镜改写（滤镜对所有非黑像素都动手）。
// ⚠️ 平均通道差门**不能再用 50**：大气关掉后背景是暗星空，滤镜只显著改写
// 星点/线条（占比小），全帧均值会掉到个位数 —— 均值门改 1.0（只排除"零变化"）。
// **伪证守门由 NC-03① 承担**：大气关 ⇒ 引擎夜视反应（大气退场）成 no-op ⇒
// 上游不变（NC-03①）成立时，下游的任何变化都只能来自 Qt Quick 滤镜，
// "黑帧伪装成效果"的通道被堵死（旧口径的教训见 TRAPS 69）。
constexpr double kEffectDiffRatio = 0.90;
constexpr double kEffectMinMean = 1.0;

// 噪声容差（T37-C 实测标定，2026-09-30 16:45 run1 冻结静置窗口 A）：
//   上游 264/921600（0.029%）最大通道差 1；下游 476/2457600（0.019%）通道差 1。
// ⇒ 冻结后引擎渲染存在**亚 LSB 非确定性**（264 像素整帧散布，疑 GPU 混合/抖动），
//   "逐位相同"的口径不成立，判据必须用"低于噪声容差"。
// 量级分离（三个数量级，门槛不担心洗掉真变化）：
//   噪声 0.029%/Δ1 ｜ 判别对照（星座线开关）0.9%/Δ9 ｜ 夜视效果 100%/Δ~180
constexpr double kNoiseRatio = 1e-3;   // 0.1%
constexpr int kNoiseMaxDelta = 2;

//! "低于噪声容差"= 没有真实状态变化（零差异直接算；非零时要求占比与通道差都在噪声带内）。
bool belowNoise(const DiffStats &d)
{
    if (!d.comparable)
        return false;
    if (d.differing == 0)
        return true;
    const double ratio = double(d.differing) / double(qMax<qint64>(d.pixels, 1));
    return ratio < kNoiseRatio && d.maxDelta <= kNoiseMaxDelta;
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

    QRect viewportRoi;
    QString controlAction = QString::fromLatin1(kControlActionDefault);

    // T37-X2 静态性观测（**只报读数，不做判据**）：冻结后引擎帧到底静不静、
    // 状态翻转会不会污染帧流。两个窗口各采 6 帧（间隔 300ms）：
    //   窗口A = 冻结后、翻转前（回答"冻结本身静不静"）
    //   窗口B = 夜视翻转后（回答"翻转是否把帧流打黑/打花"）
    // JD 同步打印 —— JD 动了 ⇒ 冻结失效；JD 不动而帧动了 ⇒ 渲染非确定性。
    int staticN = 0;
    FrameSample staticFirst;

    FrameSample refUpA, refUpB, refDnA, refDnB;
    FrameSample nightOnUp, nightOnDn, nightOffUp, nightOffDn;
    FrameSample ctlOnUp;

    bool nightWasOn = false;
    bool ctlWasOn = false;
    bool pausedByUs = false;
    // 环境前提：**大气关闭**（T37-X2 定性后新增）。引擎对夜视的既有反应 =
    // 大气层退场（AtmosphereLightweight/Preetham/ShowMySky 的 `if (night) return;`，
    // 原版语义，旧宿主靠 GL effect 滤红、合流形态靠 Qt Quick 滤镜）。大气开着
    // 测 ⇒ 夜视翻转必然改变上游帧（大气消失），NC-03①"引擎渲染路径未被触碰"
    // 的前提就塌了。大气关掉后该反应变 no-op，NC-03① 才承重。收尾还原。
    bool atmWasOn = false;
    bool atmChanged = false;

    void note(const QString &line) { lines.append(QStringLiteral("  ") + line); }

    void mark(bool ok, const QString &line)
    {
        ++total;
        if (ok)
            ++passed;
        lines.append(QStringLiteral("  [%1] %2")
                         .arg(ok ? QStringLiteral("PASS") : QStringLiteral("FAIL"))
                         .arg(line));
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
            result.summary = QStringLiteral("夜视自检：环境前提不齐 ⇒ UNAVAILABLE"
                                            "（%1/%2 条通过；环境门不记 FAIL）").arg(passed).arg(total);
        else if (inconclusive)
            result.summary = QStringLiteral("夜视自检：遇到竞态型读数 ⇒ INCONCLUSIVE"
                                            "（%1/%2 条通过）").arg(passed).arg(total);
        else
            result.summary = QStringLiteral("夜视自检：判据 %1/%2").arg(passed).arg(total);
        result.details = lines;
        onDone(result);
    }
};

struct Step
{
    int delayAfter = 0;
    std::function<void(Ctx *)> body;
};

// T37-X2 静态性观测（**只报读数，不做判据**）：tag 在编译期都是字面量；
// reset=true 的那一步重新定基（首采不作比较）。必须是自由函数 —— steps 由
// QTimer 异步执行，栈上 lambda 按引用捕获会悬垂。
void staticSample(Ctx *c, const char *tag, bool reset)
{
    const FrameSample s = grabUpstream(c->mailbox);
    const double jd = c->facade ? c->facade->julianDay() : 0.0;
    if (reset || !c->staticFirst.valid)
    {
        c->staticFirst = s;
        c->note(QStringLiteral("T37-X2 %1 #0：帧号=%2 JD=%3 非黑=%4 哈希=%5（首采定基）")
                    .arg(tag)
                    .arg(s.frameNumber)
                    .arg(jd, 0, 'f', 6)
                    .arg(s.nonBlack, 0, 'f', 4)
                    .arg(s.hash, 0, 16));
    }
    else
    {
        const DiffStats d = diffOf(c->staticFirst, s);
        c->note(QStringLiteral("T37-X2 %1 #%2：帧号=%3 JD=%4 非黑=%5 vs首采 [%6]")
                    .arg(tag)
                    .arg(c->staticN)
                    .arg(s.frameNumber)
                    .arg(jd, 0, 'f', 6)
                    .arg(s.nonBlack, 0, 'f', 4)
                    .arg(diffText(d)));
    }
    ++c->staticN;
}

#endif  // STELQUICK_HAS_ENGINE && STELQUICK_WIDGETS_HOST

} // namespace

void NightModeCheck::run(QCoreApplication *app,
                         AppFacade *facade,
                         ActionRouter *router,
                         QQuickWindow *window,
                         FrameMailbox *mailbox,
                         const std::function<void(const Result &)> &onDone,
                         int delayMs)
{
#if !defined(STELQUICK_HAS_ENGINE) || !defined(STELQUICK_WIDGETS_HOST)
    Q_UNUSED(facade)
    Q_UNUSED(router)
    Q_UNUSED(window)
    Q_UNUSED(mailbox)
    Q_UNUSED(delayMs)
    Result r;
    r.unavailable = true;
    r.summary = QStringLiteral("夜视自检需要合流形态构建"
                               "（STELQUICK_HAS_ENGINE + STELQUICK_WIDGETS_HOST 未同时定义）");
    QTimer::singleShot(0, app, [onDone, r]() { onDone(r); });
#else
    auto *ctx = new Ctx;
    ctx->app = app;
    ctx->facade = facade;
    ctx->router = router;
    ctx->window = window;
    ctx->mailbox = mailbox;
    ctx->onDone = onDone;
    ctx->controlAction = qEnvironmentVariable("STELQUICK_NIGHT_CONTROL_ACTION",
                                              QString::fromLatin1(kControlActionDefault));

    auto steps = std::make_shared<QVector<Step>>();
    auto idx = std::make_shared<int>(0);

    auto tick = std::make_shared<std::function<void()>>();
    *tick = [ctx, steps, idx, tick]() {
        if (*idx >= steps->size() || ctx->unavailable)
        {
            ctx->finish();
            return;
        }
        const Step s = steps->at((*idx)++);
        s.body(ctx);
        QTimer::singleShot(s.delayAfter, ctx->app, [tick]() { (*tick)(); });
    };

    // ── S1：前提门 + 初值 + 冻结 + 对照量置关 ─────────────────────────────
    steps->append({delayMs, [](Ctx *c) {
        if (!StelApp::isInitialized())
        {
            c->unavailable = true;
            c->note(QStringLiteral("前提：引擎未初始化 ⇒ UNAVAILABLE"));
            return;
        }
        StelApp &sa = StelApp::getInstance();
        c->nightWasOn = sa.getVisionModeNight();
        if (c->facade)
            c->ctlWasOn = c->facade->actionChecked(c->controlAction);
        if (c->facade)
        {
            c->atmWasOn = c->facade->actionChecked(QStringLiteral("actionShow_Atmosphere"));
            c->facade->setSimulationPaused(true);
            c->pausedByUs = true;
        }
        if (c->router && c->ctlWasOn)
            c->router->trigger(c->controlAction);
        if (c->router && c->atmWasOn)
        {
            c->router->trigger(QStringLiteral("actionShow_Atmosphere"));
            c->atmChanged = true;
        }
        c->note(QStringLiteral("初值：夜视=%1 对照量(%2)=%3 大气=%4；已冻结仿真 + "
                               "对照量置关 + **大气置关**（隔离引擎夜视反应里的"
                               "『大气层退场』项，NC-03① 的前提）")
                    .arg(c->nightWasOn ? 1 : 0)
                    .arg(c->controlAction)
                    .arg(c->ctlWasOn ? 1 : 0)
                    .arg(c->atmWasOn ? 1 : 0));
    }});

    // ── S1b：静态性观测窗口 A（冻结静置 6 采样）───────────────────────────
    for (int i = 0; i < 6; ++i)
        steps->append({300, [i](Ctx *c) {
            staticSample(c, "冻结静置", i == 0);
        }});

    // ── S2：参考帧 A + 视口矩形 ─────────────────────────────────────────
    steps->append({900, [](Ctx *c) {
        c->viewportRoi = viewportRectPx(c->window);
        c->refUpA = grabUpstream(c->mailbox);
        c->refDnA = grabDownstream(c->window);
        c->note(QStringLiteral("参考帧A 上游：%1").arg(frameText(c->refUpA)));
        c->note(QStringLiteral("参考帧A 下游：%1  视口矩形=%2")
                    .arg(frameText(c->refDnA))
                    .arg(c->viewportRoi.isValid()
                             ? QStringLiteral("(%1,%2)-(%3,%4)")
                                   .arg(c->viewportRoi.left()).arg(c->viewportRoi.top())
                                   .arg(c->viewportRoi.right()).arg(c->viewportRoi.bottom())
                             : QStringLiteral("不可用")));
    }});

    // ── S3：参考帧 B ⇒ NC-02 噪声底门 ───────────────────────────────────
    steps->append({0, [](Ctx *c) {
        c->refUpB = grabUpstream(c->mailbox);
        c->refDnB = grabDownstream(c->window);
        const DiffStats up = diffOf(c->refUpA, c->refUpB);
        const DiffStats dn = diffOf(c->refDnA, c->refDnB);
        c->note(QStringLiteral("NC-02 读数：上游 [%1]；下游 [%2]")
                    .arg(diffText(up), diffText(dn)));
        // 上游（邮箱帧的 CPU 拷贝）与下游（GPU 读回）都以"低于噪声容差"为口径
        //（逐位相同在冻结下也不成立 —— 引擎渲染有亚 LSB 抖动，见 kNoiseRatio 注释）。
        const bool noiseFree = belowNoise(up) && belowNoise(dn);
        if (!noiseFree)
        {
            // 环境门：冻结失效/渲染抖动 ⇒ 后面所有像素判据的口径都不可信。
            c->unavailable = true;
            c->note(QStringLiteral("NC-02 噪声底非零 ⇒ 判据口径不可信 ⇒ 整套 UNAVAILABLE"
                                   "（环境门第三态，不记 FAIL）"));
        }
        else
        {
            c->mark(true, QStringLiteral("NC-02 噪声底低于容差（占比<0.1% ∧ 通道差≤2；"
                                         "实测上游 0.029%/Δ1、下游 0.019%/Δ1 —— 引擎渲染"
                                         "在冻结下有亚 LSB 抖动）—— 后续像素判据的口径有效"));
        }
    }});

    // ── S4：翻转夜视 ⇒ NC-01 前半 ───────────────────────────────────────
    steps->append({0, [](Ctx *c) {
        const bool executed = c->router
                                  ? c->router->trigger(QString::fromLatin1(kNightAction))
                                  : false;
        const bool engineNow = StelApp::getInstance().getVisionModeNight();
        c->mark(executed && engineNow != c->nightWasOn,
                QStringLiteral("NC-01(开) trigger(%1) ⇒ executed=%2 引擎 getter %3→%4"
                               "（getter 独立回读，非 actionChecked 复述）")
                    .arg(QString::fromLatin1(kNightAction))
                    .arg(executed ? 1 : 0)
                    .arg(c->nightWasOn ? 1 : 0)
                    .arg(engineNow ? 1 : 0));
    }});

    // ── S4b：静态性观测窗口 B（夜视翻转后 6 采样）─────────────────────────
    // **不重置基** —— 直接与窗口 A 的首采比：翻转后第一帧就动 ⇒ 翻转污染帧流；
    // 动了之后回不来/持续黑 ⇒ 与 NC-03① 的假绿同源（黑帧伪装成效果）。
    for (int i = 0; i < 6; ++i)
        steps->append({300, [](Ctx *c) {
            staticSample(c, "夜视翻转后", false);
        }});

    // ── S5：ON 帧 ⇒ NC-03 效果面 ────────────────────────────────────────
    steps->append({900, [](Ctx *c) {
        c->nightOnUp = grabUpstream(c->mailbox);
        c->nightOnDn = grabDownstream(c->window);
        const DiffStats up = diffOf(c->refUpB, c->nightOnUp);
        const DiffStats dnIn = diffOf(c->refDnB, c->nightOnDn, c->viewportRoi);
        c->note(QStringLiteral("NC-03 读数：上游 [%1]；下游视口内 [%2]")
                    .arg(diffText(up), diffText(dnIn)));
        // ① 上游必须低于噪声容差（"UI 侧实现"的结构定性；引擎侧动了这里红）
        c->mark(belowNoise(up),
                QStringLiteral("NC-03① 上游引擎帧低于噪声容差（夜视是 Qt Quick 侧实现，"
                               "引擎渲染路径未被触碰）"));
        // ② 下游视口内必须显著变红
        const double ratio = (dnIn.comparable && dnIn.pixels)
                                 ? double(dnIn.differing) / double(dnIn.pixels) : 0.0;
        const bool effect = dnIn.comparable && ratio >= kEffectDiffRatio
                            && dnIn.meanDelta >= kEffectMinMean;
        c->mark(effect,
                QStringLiteral("NC-03② 下游视口内差异像素比例=%1（门 %2）平均通道差=%3"
                               "（门 %4）—— 效果真的生效了（伪证守门 = NC-03①）")
                    .arg(ratio, 0, 'f', 4)
                    .arg(kEffectDiffRatio, 0, 'f', 2)
                    .arg(dnIn.meanDelta, 0, 'f', 2)
                    .arg(kEffectMinMean, 0, 'f', 1));
    }});

    // ── S6：**暂不关夜视**，置开判别对照（写入步）────────────────────────
    // 【设计定论】夜视保持开启时先测对照量（NC-04 上游判据不受 layer 影响），
    // layer 拆除留到 NC-05（还原判据本来就是测"拆掉之后的世界"）。
    // ⚠️ delayAfter=900 挂在**本步**（写入步）—— S8 的抓帧必须等到新帧真正
    // 产出；挂在读步上就是陷阱 5/69（读到旧帧 ⇒ "逐位相同"假绿，T37 实测
    // 同一步骤结构下时灵时不灵）。
    steps->append({900, [](Ctx *c) {
        if (c->router)
            c->router->trigger(c->controlAction);  // 对照量：开
        c->note(QStringLiteral("判别对照量 %1 已置开（原值 %2）；**夜视仍保持开启**")
                    .arg(c->controlAction)
                    .arg(c->ctlWasOn ? 1 : 0));
    }});

    // ── S8：对照帧 ⇒ NC-04（承重，读步）─────────────────────────────────
    steps->append({0, [](Ctx *c) {
        c->ctlOnUp = grabUpstream(c->mailbox);
        const DiffStats up = diffOf(c->refUpB, c->ctlOnUp);
        c->note(QStringLiteral("NC-04 读数：对照 OFF↔ON 上游 [%1]").arg(diffText(up)));
        // 帧泵停更门（T37 实测：关夜视后偶发上游帧不再推进）：帧号没涨 ⇒
        // "差异=0"读的是同一帧，不是"星座线没画" ⇒ 不能硬判 FAIL。
        if (c->ctlOnUp.valid && c->refUpB.valid
            && c->ctlOnUp.frameNumber <= c->refUpB.frameNumber)
        {
            c->inconclusive = true;
            c->mark(false, QStringLiteral("NC-04 上游帧号未推进（%1 → %2）⇒ 帧泵停更，"
                                           "读数无意义 ⇒ INCONCLUSIVE（T37-X2 另案定性）")
                               .arg(c->refUpB.frameNumber)
                               .arg(c->ctlOnUp.frameNumber));
            return;
        }
        // 判别对照的"真变化"门槛 = 超出噪声容差（占比 ≥ 0.1%）；
        // 撕裂帧（T37-X2 黑天空签名）的守门不变：占比 > 0.5 ⇒ 存帧 + INCONCLUSIVE。
        const double ratio = (up.comparable && up.pixels)
                                 ? double(up.differing) / double(up.pixels) : 0.0;
        if (up.comparable && ratio > 0.5)
        {
            c->ctlOnUp.image.save(QStringLiteral("/tmp/nightcheck-nc04-torn.png"));
            c->inconclusive = true;
            c->mark(false, QStringLiteral("NC-04 差异占比 %1 > 0.5 ⇒ 读到的是全帧级异常帧"
                                           "（黑天空撕裂签名，已存 /tmp/nightcheck-nc04-torn.png，"
                                           "T37-X2 另案定性）⇒ INCONCLUSIVE")
                               .arg(ratio, 0, 'f', 4));
            return;
        }
        c->mark(up.comparable && ratio >= kNoiseRatio,
                QStringLiteral("NC-04 判别对照：同一方法对 %1 开关测出**超出噪声容差**的差异"
                               "（差异像素=%2，占比 %3 ≥ 门 %4）—— 证明比较方法活着，"
                               "NC-03① 不是假绿")
                    .arg(c->controlAction)
                    .arg(up.differing)
                    .arg(ratio, 0, 'f', 4)
                    .arg(kNoiseRatio, 0, 'f', 4));
    }});

    // ── S9（写入步）：还原对照量 + 关夜视 ─────────────────────────────────
    // 对照量必须在 NC-05 之前还原 —— 否则复原帧里带着星座线，NC-05 拿 refDnB
    // 一比必然红（T37 实测 1.557% 恰好是星座线的量级）。等待挂在本步之后
    // （陷阱 5），读步（S10）落地时效果已生效。
    steps->append({900, [](Ctx *c) {
        if (c->router && !c->ctlWasOn)
            c->router->trigger(c->controlAction);                    // 还原对照量
        if (c->router)
            c->router->trigger(QString::fromLatin1(kNightAction));   // 还原夜视
        c->note(QStringLiteral("写入步：对照量还原=%1 夜视还原（getter 现值 %2）")
                    .arg(!c->ctlWasOn ? 1 : 0)
                    .arg(StelApp::getInstance().getVisionModeNight() ? 1 : 0));
    }});

    // ── S10（读步）：复原帧 ⇒ NC-05 ─────────────────────────────────────
    // 有界重试：layer 翻转后 grabWindow 偶发拿到"天空全黑"的固定帧（T37-X2），
    // 3 次重试仍异常才 INCONCLUSIVE —— 竞态型读数不硬判。
    steps->append({0, [](Ctx *c) {
        // 有界重试只针对**异常帧**（黑帧/取帧失败）：星线 fader 在仿真冻结期间
        // 停在中间态（T37-X4），复原帧带 ~1.5%/Δ≤23 的星线余辉且 3s 不衰减 ——
        // 这不是竞态，重试等不掉，判据改为有界容忍（见下方 mark）。
        DiffStats dnIn;
        bool sane = false;
        for (int attempt = 0; attempt < 3; ++attempt)
        {
            if (attempt > 0)
            {
                c->note(QStringLiteral("NC-05 第 %1 次抓帧疑似异常帧 ⇒ 等 600ms 重试")
                            .arg(attempt));
                QThread::msleep(600);
            }
            c->nightOffUp = grabUpstream(c->mailbox);
            c->nightOffDn = grabDownstream(c->window);
            dnIn = diffOf(c->refDnB, c->nightOffDn, c->viewportRoi);
            sane = c->nightOffDn.valid
                   && c->nightOffDn.nonBlack >= c->refDnB.nonBlack * 0.90;
            if (sane)
                break;
        }
        c->note(QStringLiteral("NC-05 读数：复原帧上游 [%1]").arg(frameText(c->nightOffUp)));
        // 定位读数：上游到底还原没有 —— vs 参考帧B（干净基线）与 vs 对照ON帧。
        //   vs refUpB 干净 ⇒ 引擎侧还原成功，脏在显示层；vs ctlOnUp 相同 ⇒ 对照量没关掉。
        if (c->nightOffUp.valid && c->refUpB.valid)
            c->note(QStringLiteral("NC-05 定位：复原帧上游 vs 参考帧B [%1]")
                        .arg(diffText(diffOf(c->refUpB, c->nightOffUp))));
        if (c->nightOffUp.valid && c->ctlOnUp.valid)
            c->note(QStringLiteral("NC-05 定位：复原帧上游 vs 对照ON帧 [%1]")
                        .arg(diffText(diffOf(c->ctlOnUp, c->nightOffUp))));
        c->note(QStringLiteral("NC-05 定位：对照量现值=%1（期望 %2） 夜视现值=%3（期望 %4）")
                    .arg(c->facade && c->facade->actionChecked(c->controlAction) ? 1 : 0)
                    .arg(c->ctlWasOn ? 1 : 0)
                    .arg(StelApp::getInstance().getVisionModeNight() ? 1 : 0)
                    .arg(c->nightWasOn ? 1 : 0));
        c->note(QStringLiteral("NC-05 读数：复原帧下游 [%1]").arg(frameText(c->nightOffDn)));
        c->note(QStringLiteral("NC-05 读数：复原帧 vs 参考帧B（视口内）[%1]")
                    .arg(diffText(dnIn)));
        if (!sane)
        {
            // 定性用：同时存上/下游异常帧，区分"显示层黑"vs"生产者黑"（T37-X2）。
            if (c->nightOffUp.valid)
                c->nightOffUp.image.save(QStringLiteral("/tmp/nightcheck-nc05-anom-up.png"));
            if (c->nightOffDn.valid)
                c->nightOffDn.image.save(QStringLiteral("/tmp/nightcheck-nc05-anom-dn.png"));
            c->inconclusive = true;
            c->mark(false, QStringLiteral("NC-05 复原帧 5 次抓取均未达判据"
                                          "（非黑 %1 vs 参考 %2；差异 [%3]）⇒ INCONCLUSIVE"
                                          "—— 持续异常，需按 T37-X2 定性")
                             .arg(c->nightOffDn.nonBlack, 0, 'f', 4)
                             .arg(c->refDnB.nonBlack, 0, 'f', 4)
                             .arg(diffText(dnIn)));
            return;
        }
        const bool engineNow = StelApp::getInstance().getVisionModeNight();
        const double ratio5 = (dnIn.comparable && dnIn.pixels)
                                  ? double(dnIn.differing) / double(dnIn.pixels) : 0.0;
        // "无永久损伤"判据：① 引擎 getter 回初值；② 复原帧与参考帧B 的差异**不含
        // 全帧级红移签名**——夜视滤镜若没关，视口内会是 ~100% 像素/均值 ~150 的
        // 红移（黑帧伪装的教训见 TRAPS 69，这里以 ratio<0.5 ∧ mean<20 双门槛卡住）。
        // 已知有界容忍：对照量星线 fader 在仿真冻结期间停在中间态（T37-X4 另案），
        // 复原帧带 ~1.5%/Δ≤23 的余辉 —— 不影响"夜视可逆"的结论。
        c->mark(engineNow == c->nightWasOn
                && dnIn.comparable && ratio5 < 0.5 && dnIn.meanDelta < 20.0,
                QStringLiteral("NC-05 复原：引擎 getter 回初值 ∧ 视口内无全帧级红移"
                               "（占比 %1 < 0.5，均值 %2 < 20）—— 效果可逆，"
                               "一次夜视不是永久损伤（星线 fader 余辉有界容忍，T37-X4）")
                    .arg(ratio5, 0, 'f', 4)
                    .arg(dnIn.meanDelta, 0, 'f', 2));
    }});

    // ── S11：还原 + 收尾 ───────────────────────────────────────────────
    steps->append({0, [](Ctx *c) {
        if (c->router && c->atmChanged)
            c->router->trigger(QStringLiteral("actionShow_Atmosphere"));  // 大气还原
        if (c->facade && c->pausedByUs)
            c->facade->setSimulationPaused(false);
        c->note(QStringLiteral("收尾：对照量还原（见 S9） 大气还原=%1 仿真暂停还原=%2")
                    .arg(c->atmChanged ? 1 : 0)
                    .arg(c->pausedByUs ? 1 : 0));
    }});

    QTimer::singleShot(0, app, [tick]() { (*tick)(); });
#endif
}

} // namespace stelapp
