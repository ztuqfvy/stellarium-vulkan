// LifeCycleProbe 实现。问题表与设计说明见 LifeCycleProbe.hpp（同一套口径）。
#include "ui/LifeCycleProbe.hpp"

#include "render/legacy/FrameMailbox.hpp"
#include "ui/IFrameProducer.hpp"
#include "ui/quick/SkyViewport.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QRect>
#include <QSize>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTimer>
#include <QVector>
#include <QWindow>

#include <algorithm>
#include <cstdio>
#include <memory>

namespace stelapp {

namespace {

using ProbeResult = LifeCycleProbe::Result;

// ── 收敛期（ms）────────────────────────────────────────────────────────────
// 动作只发"请求"，平台把它变成状态变更要走一轮事件循环。采样必须晚于**状态变更**。
// ⚠️ 首版没有这个概念（等待写在采样之后），读数的**时间窗**是错的（新陷阱 102）。
// 本组与 LifeCycleCheck 的收敛期保持一致，否则两边读数不可比。
constexpr int kProbeMinSettleMs = 500;
constexpr int kProbeRestoreSettleMs = 400;

//! 观察到的"窗口 + 视口 + 邮箱"三元组快照。全部走公开面。
struct Snap
{
    bool visible = false;
    bool exposed = false;
    int windowState = 0;          //!< 1=WindowMinimized 2=WindowMaximized 0=WindowNoState
    QRect geometry;
    quint64 displayed = 0;        //!< SkyViewport::displayedFrameNumber
    quint32 generation = 0;       //!< SkyViewport::viewportGeneration
    quint64 uploads = 0;          //!< SkyViewport::uploadCount
    bool ready = false;
    quint64 published = 0;
    quint64 dropped = 0;
    int completeSlots = 0;
    quint32 sizeGeneration = 0;
    qint64 ageMs = -1;
};

struct Ctx;
struct Step
{
    //! ⚠️ **跑本步的 body 之前**先等这么多毫秒。语义与陷阱 41 的 `delayAfter`
    //!（跑完本步之后等）**相反**，故改名以杜绝混用。
    //!
    //! 首版用的是 `delayAfter`，于是 `Step{0, showMinimized}` → `Step{1200, 采样}`
    //! 里的采样**在最小化之后 0ms 就执行**，那 1200ms 等在了采样之后 ⇒ 标着
    //! 「最小化 1.2s 期间」的 Q2 增量，量的其实是**最小化之前**的 900ms（新陷阱 102）。
    //! 这是**归因错误**：读数本身是真的，被测的时间窗是假的。
    int waitBefore = 0;
    std::function<void(Ctx *)> body;
};

struct Ctx
{
    QCoreApplication *app = nullptr;
    QQuickWindow *window = nullptr;
    SkyViewport *vp = nullptr;
    FrameMailbox *mailbox = nullptr;
    IFrameProducer *producer = nullptr;
    ProbeResult result;

    Snap prev;                    //!< 上一步的快照（做增量用）
    QRect baseGeometry;
    QElapsedTimer stamp;                  //!< 通用打点
    qint64 probeStartMs = -1;
    QVector<quint64> fastDeltas;          //!< Q6：每轮快循环的帧号增量
    QVector<int> fastMinStates;           //!< Q6：每轮最小化稳态的窗口态（是否真的最小化了）
    QVector<quint32> genAfterChange;      //!< Q4：尺寸变化后的世代序列

    // ── 重入式轮询（给"首帧延迟"用：step 只能跑一次，而首帧延迟必须细粒度采样）──
    // 机制：step 体内设 retryCurrent ⇒ 调度器**重入当前步**而不是前进。
    bool retryCurrent = false;
    int retryDelay = 0;
    //! 轮询会话状态
    quint64 pollBaseDisplayed = 0;        //!< 轮询起点的显示帧号
    qint64 restoreFirstFrameMs = -1;      //!< Q3：恢复后**真·首帧延迟**（30ms 粒度）
    qint64 restorePollTries = 0;
    QVector<qint64> sizeFirstFrameMs;     //!< Q5：每次尺寸切换后的真·首帧延迟

    void note(const QString &s) { result.details << s; }
    void finish()
    {
        result.summary = QStringLiteral("生命周期数据面探针：%1 条读数（只读，无 PASS 判定）")
                             .arg(result.details.size());
        if (result_cb)
            result_cb(result);
    }
    std::function<void(const ProbeResult &)> result_cb;
};

// ─────────────────────────────────────────────────────────────────────────────
// 小工具
// ─────────────────────────────────────────────────────────────────────────────

Snap snapOf(Ctx *c)
{
    Snap s;
    if (c->window)
    {
        s.visible = c->window->isVisible();
        s.exposed = c->window->isExposed();
        s.windowState = int(c->window->windowStates());
        s.geometry = c->window->geometry();
    }
    if (c->vp)
    {
        s.displayed = c->vp->displayedFrameNumber();
        s.generation = c->vp->viewportGeneration();
        s.uploads = c->vp->uploadCount();
        s.ready = c->vp->ready();
    }
    if (c->mailbox)
    {
        const FrameMailbox::Stats ms = c->mailbox->stats();
        s.published = ms.published;
        s.dropped = ms.dropped;
        s.completeSlots = ms.completeSlots;
        s.sizeGeneration = ms.sizeGeneration;
        s.ageMs = ms.latestFrameAgeMs;
    }
    return s;
}

QString geomStr(const QRect &g)
{
    return QStringLiteral("%1x%2@%3,%4").arg(g.width()).arg(g.height()).arg(g.x()).arg(g.y());
}

//! 状态名：1=最小化 2=最大化 0=正常（QWindow::WindowState 位掩码，这里取值本身）
QString stateName(int st)
{
    switch (st)
    {
    case 0:  return QStringLiteral("正常");
    case 1:  return QStringLiteral("最小化");
    case 2:  return QStringLiteral("最大化");
    case 4:  return QStringLiteral("全屏");
    default: return QStringLiteral("0x%1").arg(st, 0, 16);
    }
}

} // namespace

void LifeCycleProbe::run(QCoreApplication *app,
                         QQuickWindow *window,
                         FrameMailbox *mailbox,
                         IFrameProducer *producer,
                         const std::function<void(const Result &)> &onDone,
                         int delayMs)
{
    auto ctx = std::make_shared<Ctx>();
    ctx->app = app;
    ctx->window = window;
    ctx->mailbox = mailbox;
    ctx->producer = producer;
    ctx->result.ran = true;
    ctx->result_cb = onDone;
    ctx->vp = window ? window->findChild<SkyViewport *>(QStringLiteral("skyViewport")) : nullptr;

    if (!window || !ctx->vp)
    {
        ctx->result.ran = false;
        ctx->result.unavailable = true;
        ctx->result.summary = QStringLiteral("生命周期探针不可用：%1")
                                  .arg(!window ? QStringLiteral("无窗口")
                                               : QStringLiteral("未按 objectName 找到 skyViewport"));
        if (ctx->result_cb)
            ctx->result_cb(ctx->result);
        return;
    }

    auto steps = std::make_shared<QVector<Step>>();
    auto tick = std::make_shared<std::function<void(int)>>();
    // ⚠️ 重入语义：step 体内若设 `c->retryCurrent = true`，调度器**重入当前步**
    //    （延迟改用 `c->retryDelay`）而不是前进 —— 这是"首帧延迟"能被 30ms 粒度
    //    采样出来的唯一办法（Step 本身一次性，append 时还不知道要轮询几次）。
    //    这也是本轮修掉的一个**名不符实**：首版把"恢复后 1.2s 的总墙钟"叫成
    //    "首帧延迟"（陷阱 65），名字与所测条件不同义。
    // ⚠️ 等待发生在**下一步的 body 之前**（见 Step 注释）。
    auto runStep = [steps, ctx, tick](int i) {
        auto exec = [steps, ctx, tick, i]() {
            if (i >= steps->size())
            {
                ctx->finish();
                return;
            }
            ctx->retryCurrent = false;
            steps->at(i).body(ctx.get());
            if (ctx->retryCurrent)
            {
                QTimer::singleShot(ctx->retryDelay, ctx->app, [tick, i]() { (*tick)(i); });
                return;
            }
            const int next = i + 1;
            if (next < steps->size() && steps->at(next).waitBefore > 0)
                QTimer::singleShot(steps->at(next).waitBefore, ctx->app,
                                   [tick, next]() { (*tick)(next); });
            else
                (*tick)(next);
        };
        if (i >= steps->size())
        {
            ctx->finish();
            return;
        }
        const int wait = steps->at(i).waitBefore;
        if (wait > 0)
            QTimer::singleShot(wait, ctx->app, exec);
        else
            exec();
    };
    *tick = [runStep](int i) { runStep(i); };
    QTimer::singleShot(delayMs, app, [runStep]() { runStep(0); });

    // ── S1 布场：记基线几何 + 确认帧在流（否则后面全无意义）────────────────
    steps->append(Step{900, [](Ctx *c) {
        c->probeStartMs = qint64(QDateTime::currentMSecsSinceEpoch());
        c->stamp.start();
        c->baseGeometry = c->window->geometry();
        c->prev = snapOf(c);
        c->note(QStringLiteral("Q0 布场：几何 %1｜visible=%2 exposed=%3 state=%4｜"
                               "视口 ready=%5 generation=%6 uploads=%7 displayed=%8｜"
                               "邮箱 投递=%9 丢弃=%10 完整槽=%11 sizeGen=%12 帧龄=%13ms"
                               "｜生产者=%14")
                    .arg(geomStr(c->baseGeometry))
                    .arg(c->prev.visible).arg(c->prev.exposed).arg(stateName(c->prev.windowState))
                    .arg(c->prev.ready).arg(c->prev.generation).arg(c->prev.uploads)
                    .arg(c->prev.displayed)
                    .arg(c->prev.published).arg(c->prev.dropped).arg(c->prev.completeSlots)
                    .arg(c->prev.sizeGeneration).arg(c->prev.ageMs)
                    .arg(c->producer ? QStringLiteral("已装配") : QStringLiteral("未装配")));
    }});

    // ── S2 Q1：最小化语义（isVisible / isExposed / windowState）─────────────
    steps->append(Step{0, [](Ctx *c) {
        c->stamp.restart();
        c->window->showMinimized();
    }});
    steps->append(Step{1200, [](Ctx *c) {
        const Snap now = snapOf(c);
        c->note(QStringLiteral("Q1 最小化后：visible=%1（前 %2）exposed=%3（前 %4）"
                               "state=%5（前 %6）｜几何 %7（前 %8）")
                    .arg(now.visible).arg(c->prev.visible)
                    .arg(now.exposed).arg(c->prev.exposed)
                    .arg(stateName(now.windowState)).arg(stateName(c->prev.windowState))
                    .arg(geomStr(now.geometry)).arg(geomStr(c->prev.geometry)));
        c->note(QStringLiteral("Q2 最小化→采样**这 1200ms 窗口内**的增量：显示帧号 +%1｜投递 +%2｜"
                               "丢弃 +%3｜上传 +%4｜完整槽 %5→%6｜世代 %7｜帧龄 %8ms"
                               "（生产者 %9）")
                    .arg(now.displayed - c->prev.displayed)
                    .arg(now.published - c->prev.published)
                    .arg(now.dropped - c->prev.dropped)
                    .arg(now.uploads - c->prev.uploads)
                    .arg(c->prev.completeSlots).arg(now.completeSlots)
                    .arg(now.generation).arg(now.ageMs)
                    .arg(c->producer ? QStringLiteral("独立线程，窗口状态不影响它")
                                     : QStringLiteral("未装配")));
        c->note(QStringLiteral("Q7 观测面自证：窗口 hidden 期间 displayedFrameNumber 是否变化 —— "
                               "本条读数即为该问题的答案（+%1）；"
                               "后续每轮都复读，用于区分「窗口不推进」与「数据面停更」")
                    .arg(now.displayed - c->prev.displayed));
        c->note(QStringLiteral("Q7b 视口 ready=%1（最小化期间视口对象是否仍就绪）").arg(now.ready));
        c->prev = now;
    }});

    // ── S3 Q3：恢复语义（几何是否回归 + **真·首帧延迟**）────────────────────
    // 30ms 粒度轮询最多 50 次（1.5s）：displayedFrameNumber 一旦相比恢复瞬间推进，
    // 就把 elapsed() 记为首帧延迟。⚠️ 首版把"恢复后 1.2s 的总墙钟"当首帧延迟报
    // —— 名字与所测条件不同义（陷阱 65），本轮改正。
    steps->append(Step{0, [](Ctx *c) {
        c->window->showNormal();
        c->stamp.restart();
        c->pollBaseDisplayed = c->prev.displayed;

        c->restoreFirstFrameMs = -1;
        c->restorePollTries = 0;
    }});
    steps->append(Step{0, [](Ctx *c) {
        const Snap now = snapOf(c);
        const qint64 t = c->stamp.elapsed();
        if (c->restoreFirstFrameMs < 0 && now.displayed > c->pollBaseDisplayed)
            c->restoreFirstFrameMs = t;
        if (c->restoreFirstFrameMs < 0 && ++c->restorePollTries < 50)
        {
            c->retryCurrent = true;
            c->retryDelay = 30;
            return;
        }

        c->note(QStringLiteral("Q3 恢复：**首帧延迟 %1ms**（30ms 粒度轮询 %2 次；"
                               "-1 = 1.5s 内未推进）｜visible=%3（前 %4）exposed=%5（前 %6）"
                               "state=%7｜几何 %8（基线 %9，回归=%10）｜帧号 %11→%12")
                    .arg(c->restoreFirstFrameMs).arg(c->restorePollTries)
                    .arg(now.visible).arg(c->prev.visible)
                    .arg(now.exposed).arg(c->prev.exposed)
                    .arg(stateName(now.windowState))
                    .arg(geomStr(now.geometry)).arg(geomStr(c->baseGeometry))
                    .arg(now.geometry == c->baseGeometry ? QStringLiteral("是") : QStringLiteral("否"))
                    .arg(c->pollBaseDisplayed).arg(now.displayed));
        c->prev = now;
    }});
    steps->append(Step{1000, [](Ctx *c) {
        const Snap now = snapOf(c);
        c->note(QStringLiteral("Q3b 恢复后稳态（+1s）：投递 +%1 丢弃 +%2 完整槽 %3"
                               "（≤3 才有界）帧龄 %4ms｜上传 +%5｜世代 %6")
                    .arg(now.published - c->prev.published)
                    .arg(now.dropped - c->prev.dropped)
                    .arg(now.completeSlots).arg(now.ageMs)
                    .arg(now.uploads - c->prev.uploads).arg(now.generation));
        c->prev = now;
    }});

    // ── S4 Q4/Q5：尺寸切换 = 世代递增？同尺寸重设是否假递增？代价多少？─────
    // 逐次切换，每次单独报「世代 +1 与否 + 帧号是否仍推进 + 真·首帧延迟」。
    for (int k = 1; k <= 4; ++k)
    {
        const int dw = k * 40, dh = k * 30;
        steps->append(Step{300, [dw, dh](Ctx *c) {
            // ⚠️ 先重照 prev 再动窗口：上一轮 resize 的 geometryChange 可能在上一轮
            //   轮询 0ms 成功**之后**才处理 ⇒ 它的世代 bump 会落进本窗。
            //   不重照就把上一轮的账记到本轮头上（Q4b-1 首测 Δ=1 即此，2026-10-01）。
            c->prev = snapOf(c);
            const QSize s = c->baseGeometry.size();
            c->window->setGeometry(c->baseGeometry.x(), c->baseGeometry.y(),
                                   s.width() + dw, s.height() + dh);
            c->stamp.restart();
            c->pollBaseDisplayed = c->prev.displayed;
            c->restoreFirstFrameMs = -1;
            c->restorePollTries = 0;
        }});
        steps->append(Step{0, [dw, dh](Ctx *c) {
            const Snap now = snapOf(c);
            const qint64 t = c->stamp.elapsed();
            if (c->restoreFirstFrameMs < 0 && now.displayed > c->pollBaseDisplayed)
                c->restoreFirstFrameMs = t;
            if (c->restoreFirstFrameMs < 0 && ++c->restorePollTries < 50)
            {
                c->retryCurrent = true;
                c->retryDelay = 30;
                return;
            }
            const quint32 dGen = now.generation - c->prev.generation;
            c->genAfterChange << now.generation;
            c->sizeFirstFrameMs << c->restoreFirstFrameMs;
            c->note(QStringLiteral("Q4 尺寸 +%1x+%2 ⇒ %3｜世代 %4→%5（Δ=%6）｜sizeGen %7→%8｜"
                                   "帧号 +%9 上传 +%10｜**切换后首帧延迟 %11ms**")
                        .arg(dw).arg(dh).arg(geomStr(now.geometry))
                        .arg(c->prev.generation).arg(now.generation).arg(dGen)
                        .arg(c->prev.sizeGeneration).arg(now.sizeGeneration)
                        .arg(now.displayed - c->prev.displayed)
                        .arg(now.uploads - c->prev.uploads)
                        .arg(c->restoreFirstFrameMs));
            c->prev = now;
        }});
    }
    // 同尺寸重设 —— 假递增检查（判据若拿世代当"重建发生"的证据，必须先知道这条）
    // ⚠️ **两条都要测，否则是平凡真**（陷阱 67）：
    //   Q4b-1 `setGeometry(当前几何)`：可能被 Qt 短路 ⇒ Δ=0 只说明"这条路不触发"，
    //         不说明"resize 通路不假递增"；
    //   Q4b-2 `resize(当前尺寸)`：**绕过 setGeometry 的几何比较**，直接走 resize 通路
    //         —— 这条才是"窗口管理器重复发同尺寸 resize"的真实对应物。
    steps->append(Step{300, [](Ctx *c) {
        c->prev = snapOf(c);                 // 吸收上一动作的迟到 bump（同上）
        c->stamp.restart();
        c->window->setGeometry(c->window->geometry());   // 尺寸不变
    }});
    steps->append(Step{500, [](Ctx *c) {
        const Snap now = snapOf(c);
        c->note(QStringLiteral("Q4b-1 **同尺寸 setGeometry**：世代 %1→%2（Δ=%3）"
                               "｜帧号 +%4｜（Δ=0 也可能是被 Qt 短路，见 Q4b-2）")
                    .arg(c->prev.generation).arg(now.generation)
                    .arg(now.generation - c->prev.generation)
                    .arg(now.displayed - c->prev.displayed));
        c->prev = now;
    }});
    steps->append(Step{300, [](Ctx *c) {
        c->prev = snapOf(c);                 // 吸收上一动作的迟到 bump（同上）
        c->stamp.restart();
        c->window->resize(c->window->size());            // 同尺寸，走 resize 通路
    }});
    steps->append(Step{500, [](Ctx *c) {
        const Snap now = snapOf(c);
        c->note(QStringLiteral("Q4b-2 **同尺寸 resize**：世代 %1→%2（Δ=%3）｜sizeGen %4→%5"
                               "｜帧号 +%6｜**Δ=0 才能拿世代当「重建发生」的证据**")
                    .arg(c->prev.generation).arg(now.generation)
                    .arg(now.generation - c->prev.generation)
                    .arg(c->prev.sizeGeneration).arg(now.sizeGeneration)
                    .arg(now.displayed - c->prev.displayed));
        c->prev = now;
    }});
    // 复原几何
    steps->append(Step{300, [](Ctx *c) {
        c->prev = snapOf(c);                 // 吸收上一动作的迟到 bump（同上）
        c->window->setGeometry(c->baseGeometry);
    }});
    steps->append(Step{600, [](Ctx *c) {
        const Snap now = snapOf(c);
        c->note(QStringLiteral("Q4c 几何复原 ⇒ %1（基线 %2）｜世代 %3｜帧号 +%4")
                    .arg(geomStr(now.geometry)).arg(geomStr(c->baseGeometry))
                    .arg(now.generation).arg(now.displayed - c->prev.displayed));
        c->prev = now;
    }});

    // ── S5 Q6：×20 快速最小化/恢复（每轮是否都能重新推进）─────────────────
    // 每轮：最小化 → **等 500ms 让最小化真的生效**（并在此刻记稳态窗口态）→ 恢复
    //       → 等 400ms → 采样。轮预算 = 常量算出来，别手写（改常量不改文案 = 假记录）。
    for (int r = 0; r < 20; ++r)
    {
        steps->append(Step{0, [](Ctx *c) {
            c->prev = snapOf(c);
            c->window->showMinimized();
        }});
        steps->append(Step{kProbeMinSettleMs, [r](Ctx *c) {
            // 最小化**稳态**：窗口态必须真的进「最小化」。
            //（A1 用 hide() 代理，进不了这个状态 —— 这条读数是 LF-02f 的设计依据）
            const Snap m = snapOf(c);
            c->fastMinStates << m.windowState;
            if (r % 5 == 4)
                c->note(QStringLiteral("Q6c 轮 %1 最小化稳态：state=%2 visible=%3 exposed=%4")
                            .arg(r + 1).arg(stateName(m.windowState))
                            .arg(m.visible).arg(m.exposed));
        }});
        steps->append(Step{0, [](Ctx *c) { c->window->showNormal(); }});
        steps->append(Step{kProbeRestoreSettleMs, [r](Ctx *c) {
            const Snap now = snapOf(c);
            const quint64 d = now.displayed - c->prev.displayed;
            c->fastDeltas << d;
            c->prev = now;
        }});
    }
    steps->append(Step{300, [](Ctx *c) {
        quint64 lo = ~quint64(0), hi = 0, sum = 0;
        for (quint64 d : c->fastDeltas)
        {
            lo = qMin(lo, d);
            hi = qMax(hi, d);
            sum += d;
        }
        const int zeroRounds = int(std::count_if(c->fastDeltas.begin(), c->fastDeltas.end(),
                                                 [](quint64 d) { return d == 0; }));
        const int minReached = int(std::count_if(c->fastMinStates.begin(), c->fastMinStates.end(),
                                                 [](int st) { return st == int(Qt::WindowMinimized); }));
        c->note(QStringLiteral("Q6 ×20 快速最小化/恢复（每轮 %1ms = 最小化 %2 + 恢复后 %3）："
                               "帧号增量 min=%4 max=%5 均值=%6｜**零推进轮数=%7**"
                               "（>0 说明存在「恢复后再不推进」的轮）｜"
                               "**真的进到「最小化」态的轮数=%8/20**（<20 ⇒ 动作被竞速/吞掉）")
                    .arg(kProbeMinSettleMs + kProbeRestoreSettleMs)
                    .arg(kProbeMinSettleMs).arg(kProbeRestoreSettleMs)
                    .arg(lo).arg(hi)
                    .arg(c->fastDeltas.isEmpty()
                             ? QStringLiteral("—")
                             : QString::number(double(sum) / c->fastDeltas.size(), 'f', 1))
                    .arg(zeroRounds).arg(minReached));
        // 光有 min/max/均值不够 —— "从第几轮开始不推进"必须看得见（诊断用）
        {
            QStringList seq;
            for (quint64 d : c->fastDeltas)
                seq << QString::number(d);
            c->note(QStringLiteral("Q6a 每轮增量序列：[%1]").arg(seq.join(QStringLiteral(", "))));
        }
        const Snap now = snapOf(c);
        c->note(QStringLiteral("Q6b 循环末：几何 %1｜世代 %2｜完整槽 %3｜丢弃 %4｜帧龄 %5ms")
                    .arg(geomStr(now.geometry)).arg(now.generation)
                    .arg(now.completeSlots).arg(now.dropped).arg(now.ageMs));
        c->prev = now;
    }});

    // ── S6 Q8：进程级预算（启动 → 首帧墙钟）───────────────────────────────
    steps->append(Step{0, [](Ctx *c) {
        // 进程起点用 QCoreApplication 的启动时刻近似：探针只知道"现在"与"首帧"。
        // 首帧时刻无法回溯，故这里报的是**总墙钟**（由外部脚本配合 date 精确测）。
        const qint64 up = c->app ? qint64(QDateTime::currentMSecsSinceEpoch() - c->probeStartMs)
                                 : -1;
        c->note(QStringLiteral("Q8 探针自身耗时 %1ms（进程级「启动→退出」墙钟由外部脚本量，"
                               "×100 预算见 tools/a6-verify.sh）")
                    .arg(up));
        QStringList sz;
        for (qint64 v : c->sizeFirstFrameMs)
            sz << QString::number(v);
        c->note(QStringLiteral("Q9 世代序列（尺寸逐次变化）：[%1]｜恢复首帧 %2ms｜"
                               "尺寸切换首帧序列 [%3] ms（均 30ms 粒度轮询，-1=1.5s 内未推进）")
                    .arg([c] {
                        QStringList l;
                        for (quint32 g : c->genAfterChange) l << QString::number(g);
                        return l.join(QStringLiteral(", "));
                    }())
                    .arg(c->restoreFirstFrameMs)
                    .arg(sz.join(QStringLiteral(", "))));
    }});

    // 收尾：把窗口恢复到布场态（探针不得留下副作用 —— 陷阱 90）
    steps->append(Step{0, [](Ctx *c) {
        if (c->window->windowState() != Qt::WindowNoState)
            c->window->showNormal();
        if (c->window->geometry() != c->baseGeometry)
            c->window->setGeometry(c->baseGeometry);
        c->note(QStringLiteral("收尾：窗口已还原到布场态（几何 %1 / state=%2）")
                    .arg(geomStr(c->baseGeometry)).arg(stateName(int(c->window->windowStates()))));
    }});

    std::fprintf(stderr, "LIFEPROBE: 生命周期数据面探针开跑（delayMs=%d）\n", delayMs);
    std::fflush(stderr);
}

// ─────────────────────────────────────────────────────────────────────────────
// P-LIF-01 的单次腿：进程级「启动 → 首帧上屏」计时
// ─────────────────────────────────────────────────────────────────────────────
// ⚠️ 计时**必须用 `main()` 开头取的 processStartMs**。首版若用本函数进入时刻，
//    量到的是"引擎装配之后到首帧" —— 名字叫"启动到首帧"却测了别的东西（陷阱 65）。
// ⚠️ 超时按**真实失败**报（前提齐备而结果没出现 = 失败），不是 UNAVAILABLE。
void LifeCycleProbe::runStartupOnce(QCoreApplication *app,
                                    QQuickWindow *window,
                                    qint64 processStartMs,
                                    const std::function<void(const Result &)> &onDone,
                                    int timeoutMs)
{
    auto ctx = std::make_shared<Ctx>();
    ctx->app = app;
    ctx->window = window;
    ctx->result.ran = true;
    ctx->result_cb = onDone;
    ctx->vp = window ? window->findChild<SkyViewport *>(QStringLiteral("skyViewport")) : nullptr;

    if (!window || !ctx->vp)
    {
        ctx->result.ran = false;
        ctx->result.unavailable = true;
        ctx->result.summary = QStringLiteral("进程级首帧计时不可用：%1")
                                  .arg(!window ? QStringLiteral("无窗口")
                                               : QStringLiteral("未按 objectName 找到 skyViewport"));
        if (ctx->result_cb)
            ctx->result_cb(ctx->result);
        return;
    }

    auto tries = std::make_shared<qint64>(0);
    auto poll = std::make_shared<std::function<void()>>();
    *poll = [ctx, processStartMs, timeoutMs, tries, poll]() {
        const quint64 displayed = ctx->vp->displayedFrameNumber();
        const qint64 since = qint64(QDateTime::currentMSecsSinceEpoch()) - processStartMs;
        if (displayed > 0)
        {
            ctx->result.firstFrameReached = true;
            ctx->result.firstFrameMs = since;
            ctx->result.summary = QStringLiteral("进程级启动→首帧：%1 ms（轮询 %2 次，30ms 粒度）")
                                      .arg(since).arg(*tries);
            ctx->result.details << QStringLiteral("ONESHOT firstFrameMs=%1 tries=%2 displayed=%3")
                                       .arg(since).arg(*tries).arg(displayed);
            ctx->finish();
            return;
        }
        if (since >= timeoutMs)
        {
            ctx->result.firstFrameReached = false;
            ctx->result.firstFrameMs = -1;
            ctx->result.summary = QStringLiteral("进程级启动→首帧：%1 ms 内未上屏"
                                                 "（**超时 = 真实失败**，不是 UNAVAILABLE）")
                                      .arg(timeoutMs);
            ctx->result.details << QStringLiteral("ONESHOT TIMEOUT after %1ms tries=%2 displayed=%3")
                                       .arg(timeoutMs).arg(*tries).arg(displayed);
            ctx->finish();
            return;
        }
        ++*tries;
        QTimer::singleShot(30, ctx->app, [poll]() { (*poll)(); });
    };
    QTimer::singleShot(0, app, [poll]() { (*poll)(); });
}

} // namespace stelapp
