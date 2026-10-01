// LifeCycleCheck 实现。判据表与设计说明见同名头文件（同一套口径）。
//
// ⚠️ 本文件的核心纪律：**`Step::waitBefore` 的语义是"跑本步之前等"**。
//    第一版把它写成 `delayAfter`（跑完本步之后等，陷阱 41），结果
//    `Step{0, doResize}` → `Step{260, 采样}` 里的采样**在 setGeometry 之后 0ms 就执行**，
//    那 260ms 等在了采样之后 ⇒ LF-03 全程读到 `世代 X→X`、`帧号 +0`、`投递 +0`，
//    看似"产品缺陷"，实为仪器把"0ms 的读数"当成了"260ms 后的读数"。
//    改成 waitBefore 后，"动作 → 等 S → 采样"在代码里就是三步相邻有序，
//    这类错误在语法上不再可能犯（新陷阱，见 TRAPS.md / T44 文档 §3）。
#include "ui/LifeCycleCheck.hpp"

#include "render/legacy/FrameMailbox.hpp"
#include "ui/IFrameProducer.hpp"
#include "ui/quick/SkyViewport.hpp"

#include <QCoreApplication>
#include <QPair>
#include <QRect>
#include <QSet>
#include <QSize>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTimer>
#include <QVector>
#include <QWindow>

#include <algorithm>
#include <cstdio>
#include <memory>

#ifdef Q_OS_MACOS
#include <mach/mach.h>
#include <mach/task_info.h>
#endif

namespace stelapp {

namespace {

using CheckResult = LifeCycleCheck::Result;

// ── 每组的轮数（测试文档口径：×100）────────────────────────────────────────
constexpr int kRounds = 100;

// ── 内存预算（MiB：×100 轮生命周期事件的 phys_footprint 首末增量上限）─────
// ⚠️ 本值是**实测后写死**的（陷阱 87）：2026-10-01 四轮（正题 + 三条负控）实测
//   LF-02e ∈ [−1, +7]，LF-03d ∈ [−1, +15]（峰值来自正题：4 档尺寸的一次性分配，
//   不是逐轮泄漏 —— 4 档尺寸各分配一次，量有界）。取 2× 余量 = 32。
//   改它必须同时改 T44 文档的读数表。
constexpr qint64 kMemBudgetMiB = 32;

// ── 「最新帧年龄」预算（ms）────────────────────────────────────────────────
// LF-02a 的"队列有界"必须是**真命题**：`completeSlots ≤ kFrameSlotCount` 是定长数组的
// 构造性质（> 3 不可能），单靠它是**平凡真**（陷阱 67）。真正的有界性来自 U-FRM-01
// 「忙时丢帧、不积压」⇒ 判"最新完整帧的年龄不无界增长"。生产者 ~55fps ⇒ 稳态 ~18ms，
// 预算 300ms 给足余量（实测值见 T44 文档读数表）。
constexpr qint64 kAgeBudgetMs = 300;

//! `sizeGeneration` 的**实测恒值**：T44-A Q4 实测替身生产者出图尺寸固定 ⇒ 它不随
//! 窗口尺寸走（真源是**帧**的尺寸世代，不是窗口的）。写进 detail 是为了让人一眼看到
//! "这个量我们看过、它为什么不动"，而不是悄悄略过（陷阱 67：缺前提不是平凡真）。
constexpr quint32 kObservedSizeGen = 2;

// ── 布场收敛时间（ms）──────────────────────────────────────────────────────
// ⚠️ 这三个值是**实测后写死**的。宁可给足：动作只发出"请求"，平台把它变成状态变更要
// 走一轮事件循环；采样必须晚于**状态变更**而不是早于它。
//   · 最小化动画期间若立刻发恢复请求，两个请求会互相竞速/合并 ⇒ 窗口隔轮才真正切换
//     （T44-A Q6a 的 `[0,88,0,89,…]` 就是这个，**不是"采样相位"**）。
//   · 恢复后帧流重启：T44-A Q5 实测首帧延迟 ≤36ms，400ms 给足。
constexpr int kMinimizeSettleMs = 500;
constexpr int kRestoreSettleMs = 400;
constexpr int kResizeSettleMs = 350;

// ── 内存读数（与 T9 / SkyLongRun 同口径：phys_footprint。RSS 会被页面回收
//    掩盖增长，泄漏哨兵必须用 footprint）────────────────────────────────────
qint64 footprintBytes()
{
#ifdef Q_OS_MACOS
    task_vm_info_data_t info;
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO,
                  reinterpret_cast<task_info_t>(&info), &count) == KERN_SUCCESS)
        return qint64(info.phys_footprint);
#endif
    return -1;
}

qint64 toMiB(qint64 bytes) { return bytes < 0 ? -1 : bytes / (1024 * 1024); }

// ── 小工具（各 check 自带一份，不跨 TU 共享匿名命名空间）──────────────────

QString geomStr(const QRect &g)
{
    return QStringLiteral("%1x%2@%3,%4").arg(g.width()).arg(g.height()).arg(g.x()).arg(g.y());
}

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

//! "窗口 + 视口 + 邮箱"三元组快照，全部走公开面。
struct Snap
{
    bool visible = false;
    bool exposed = false;
    int windowState = 0;
    QRect geometry;
    quint64 displayed = 0;        //!< SkyViewport::displayedFrameNumber
    quint32 generation = 0;       //!< SkyViewport::viewportGeneration
    quint64 uploads = 0;
    bool ready = false;
    quint64 published = 0;
    quint64 dropped = 0;
    int completeSlots = 0;
    quint32 sizeGeneration = 0;
    qint64 ageMs = -1;
    quint64 leased = 0;
    int readersHeld = 0;
    qint64 bytesPerFrame = 0;
};

//! 一轮循环的累积统计（三组各一份）
struct LoopStats
{
    QVector<quint64> deltas;      //!< 每轮帧号增量
    int zeroRounds = 0;
    int curZeroRun = 0;
    int maxConsecZero = 0;
    int units = 0;                //!< 有效轮数
    int genIncrements = 0;        //!< 世代递增次数（LF-03/04）
    bool genMonoOk = true;        //!< 世代单调不减
    bool slotsOk = true;          //!< completeSlots ≤ kFrameSlotCount
    bool ageOk = true;            //!< 最新帧年龄 ≤ kAgeBudgetMs（不积压）
    bool readyOk = true;          //!< 视口 ready 恒真
    bool geomOk = true;           //!< 恢复后几何回归基线
    bool minStateOk = true;       //!< 最小化阶段窗口态真的变成"最小化"（LF-02f）
    int maxSlots = 0;
    qint64 maxAgeMs = -1;
    quint64 dMin = ~quint64(0), dMax = 0;   //!< 增量极值（诊断用）
    qint64 fp0 = -1, fp1 = -1;    //!< 组的首尾 phys_footprint

    void fold(quint64 d)
    {
        deltas << d;
        ++units;
        dMin = qMin(dMin, d);
        dMax = qMax(dMax, d);
        if (d == 0) { ++curZeroRun; ++zeroRounds; }
        else          curZeroRun = 0;
        maxConsecZero = qMax(maxConsecZero, curZeroRun);
    }

    QString deltaSummary() const
    {
        if (deltas.isEmpty())
            return QStringLiteral("—");
        QVector<quint64> v = deltas;
        std::sort(v.begin(), v.end());
        const quint64 med = v.at(v.size() / 2);
        return QStringLiteral("增量 min=%1 中位=%2 max=%3").arg(dMin).arg(med).arg(dMax);
    }
};

struct Ctx;
struct Step
{
    //! ⚠️ **跑本步的 body 之前**先等这么多毫秒。语义与陷阱 41 的 `delayAfter`
    //!（跑完本步之后等）**相反**，故改名以杜绝混用。见文件头注。
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
    CheckResult result;

    QRect baseGeometry;
    Snap loopPrev;                //!< 本轮起点快照（每轮刷新）
    bool abort = false;
    bool framesFlow = false;      //!< 布场自证：基线期帧号确实在增长
    QString abortWhy;

    // 诊断基准（**每组开始时 rebase**，否则首个窗口会把"开机以来的累计值"当成
    //  窗口增量打印出来 —— 陷阱 56/66：可比的量必须是同一个窗口上的）
    quint64 prodRendPrev = 0;
    quint64 leasedPrev = 0;
    quint64 hReqPrev = 0, hPubPrev = 0, hDropPrev = 0;

    // ── 负控开关（只影响**被测行为本身**，不伪造观测面 —— 与 T42 的 PATHS_OFF 同类）──
    //    · NOSIZE  ：不做尺寸切换 ⇒ 预期恰红 {LF-03a, LF-04a}（世代不递增，帧流照旧）。
    //    · NOSETTLE：把三个收敛期全置 0 ⇒ **复现"waitBefore 写成 delayAfter"那版仪器
    //                的读数**，预期恰红 {LF-02b, LF-02f?, LF-03a, LF-03c, LF-04a, LF-04c}
    //                （实跑写死）。这条是**仪器灵敏度的回归哨兵**：它必须会红，
    //                否则说明判据对"根本没等到效果发生"无感（陷阱 20）。
    //    · HIDE    ：用 A1 的老代理 `hide()/show()` 代替真最小化/恢复 ⇒
    //                窗口态根本不进"最小化"⇒ 恰好钉死 LF-02f（证明 hide 代理不够）。
    bool noSize = false;
    bool useHide = false;
    bool noSettle = false;

    LoopStats s02, s03, s04;

    // ── 台账（陷阱 85：id 必须逐字回写，否则"全 PASS 却 VERDICT=FAIL"）──
    QVector<QPair<QString, bool>> items;
    QSet<QString> marked;

    void mark(const QString &id, bool ok, const QString &detail)
    {
        items << qMakePair(id, ok);
        marked.insert(id);
        result.details << QStringLiteral("[%1] %2 — %3")
                              .arg(ok ? QStringLiteral("PASS") : QStringLiteral("FAIL"),
                                   id, detail);
    }

    void note(const QString &s) { result.details << s; }

    void observe(LoopStats &s, const Snap &now, bool checkGeom)
    {
        s.maxSlots = qMax(s.maxSlots, now.completeSlots);
        if (now.completeSlots > FrameMailbox::kFrameSlotCount)
            s.slotsOk = false;
        if (now.ageMs > kAgeBudgetMs)
            s.ageOk = false;
        s.maxAgeMs = qMax(s.maxAgeMs, now.ageMs);
        if (!now.ready)
            s.readyOk = false;
        if (checkGeom && now.geometry != baseGeometry)
            s.geomOk = false;
    }

    static bool memOk(const LoopStats &s)
    {
        if (s.fp0 < 0 || s.fp1 < 0)
            return true;   // 读数不可用（非 macOS）⇒ 不判红，只记（诚实性）
        return (s.fp1 - s.fp0) <= kMemBudgetMiB * 1024 * 1024;
    }

    //! 收敛期读数（负控 NOSETTLE 会把它们全变 0）。
    int settleMin() const { return noSettle ? 0 : kMinimizeSettleMs; }
    int settleRestore() const { return noSettle ? 0 : kRestoreSettleMs; }
    int settleResize() const { return noSettle ? 0 : kResizeSettleMs; }

    void finish();
    std::function<void(const CheckResult &)> result_cb;
};

//! 三个"被测行为"的执行入口 —— 负控开关就在这里分流（**关掉被测行为**，
//! 而不是伪造观测面：判据的红必须来自被测物，不来自仪器 —— 陷阱 3）。
void doMinimize(Ctx *c)
{
    if (c->useHide)
        c->window->hide();            // A1 的老代理（只有"窗口不可见"一个语义）
    else
        c->window->showMinimized();
}

void doRestore(Ctx *c)
{
    if (c->useHide)
        c->window->show();
    else
        c->window->showNormal();
}

void doResize(Ctx *c, int k)
{
    if (c->noSize)
        return;
    const QSize s = c->baseGeometry.size();
    c->window->setGeometry(c->baseGeometry.x(), c->baseGeometry.y(),
                           s.width() + k * 40, s.height() + k * 30);
}

void Ctx::finish()
{
    // ── 前提不齐：不给判据，也不给 FAIL（陷阱 3）──────────────────────────
    if (!framesFlow || (s02.units == 0 && s03.units == 0 && s04.units == 0))
    {
        result.ran = false;
        result.unavailable = true;
        result.passed = 0;
        result.total = 0;
        result.summary = QStringLiteral("生命周期自检不可用：%1")
                             .arg(abortWhy.isEmpty()
                                      ? QStringLiteral("未产出任何读数（前提不齐）")
                                      : abortWhy);
        if (result_cb)
            result_cb(result);
        return;
    }

    // ── 判据（LF-02 / LF-03 / LF-04）──────────────────────────────────────
    if (s02.units > 0)
    {
        mark(QStringLiteral("LF-02a"), s02.slotsOk && s02.ageOk,
             QStringLiteral("最小化/恢复 ×%1：帧队列有界且不积压（完整槽峰值 %2 ≤ %3；"
                            "最新帧年龄峰值 %4ms ≤ %5ms —— 单靠槽位数是平凡真）｜%6")
                 .arg(s02.units).arg(s02.maxSlots).arg(FrameMailbox::kFrameSlotCount)
                 .arg(s02.maxAgeMs).arg(kAgeBudgetMs).arg(s02.deltaSummary()));
        mark(QStringLiteral("LF-02b"), s02.maxConsecZero <= 1,
             QStringLiteral("最小化/恢复 ×%1：帧号每轮推进（最长连续零推进 %2，"
                            "零推进轮 %3/%1；收敛期 %4/%5ms）")
                 .arg(s02.units).arg(s02.maxConsecZero).arg(s02.zeroRounds)
                 .arg(settleMin()).arg(settleRestore()));
        mark(QStringLiteral("LF-02c"), s02.readyOk,
             QStringLiteral("最小化/恢复 ×%1：视口全程 ready（最小化期间对象不被拆）")
                 .arg(s02.units));
        mark(QStringLiteral("LF-02d"), s02.geomOk,
             QStringLiteral("最小化/恢复 ×%1：每轮恢复后几何回归基线 %2")
                 .arg(s02.units).arg(geomStr(baseGeometry)));
        mark(QStringLiteral("LF-02e"), memOk(s02),
             QStringLiteral("最小化/恢复 ×%1：phys_footprint %2 → %3 MiB（增 %4，预算 %5）")
                 .arg(s02.units).arg(toMiB(s02.fp0)).arg(toMiB(s02.fp1))
                 .arg(toMiB(s02.fp1 - s02.fp0)).arg(kMemBudgetMiB));
        mark(QStringLiteral("LF-02f"), s02.minStateOk,
             QStringLiteral("最小化/恢复 ×%1：**窗口态真的进过「最小化」**（不是 hide() 代理）"
                            "—— 最小化阶段窗口态 = 最小化，恢复后 = 正常")
                 .arg(s02.units));
    }

    if (s03.units > 0)
    {
        // 世代递增：每次尺寸切换都应 +1。留一格（1% 容差）是给单次事件循环打嗝，
        // 与 LF-02b 的"≤1"同族。
        // ⚠️ 旧注释说"首次可能与 Qt 初始 resize 合并" —— 那是**归因假象**：探针首测
        //    Δ=[0,1,1,1] 的真因是它轮询 0ms 就成功、本轮的 geometryChange 还没处理，
        //    bump 落进了下一个测量窗（探针已加"动作前重照 prev"护栏，复测 Δ=[1,1,1,1]，
        //    首帧延迟 30ms）。判据侧无此问题：350ms 收敛期覆盖了 bump。
        const int need = qMax(1, s03.units - 1);
        mark(QStringLiteral("LF-03a"), s03.genIncrements >= need,
             QStringLiteral("尺寸切换 ×%1：世代递增 %2 次（要求 ≥ %3 = 留 1% 容差）"
                            "｜sizeGen 恒 %4（真源是**帧**尺寸世代，"
                            "替身生产者出图尺寸固定 ⇒ 不动是预期）")
                 .arg(s03.units).arg(s03.genIncrements).arg(need).arg(kObservedSizeGen));
        mark(QStringLiteral("LF-03b"), s03.genMonoOk,
             QStringLiteral("尺寸切换 ×%1：世代单调不减（无回退）").arg(s03.units));
        mark(QStringLiteral("LF-03c"), s03.maxConsecZero <= 1,
             QStringLiteral("尺寸切换 ×%1：切换后帧号仍推进（最长连续零推进 %2，"
                            "零推进轮 %3/%1；收敛期 %4ms）｜%5")
                 .arg(s03.units).arg(s03.maxConsecZero).arg(s03.zeroRounds)
                 .arg(settleResize()).arg(s03.deltaSummary()));
        mark(QStringLiteral("LF-03d"), memOk(s03),
             QStringLiteral("尺寸切换 ×%1：phys_footprint %2 → %3 MiB（增 %4，预算 %5）")
                 .arg(s03.units).arg(toMiB(s03.fp0)).arg(toMiB(s03.fp1))
                 .arg(toMiB(s03.fp1 - s03.fp0)).arg(kMemBudgetMiB));
    }

    if (s04.units > 0)
    {
        mark(QStringLiteral("LF-04a"), s04.genIncrements > 0,
             QStringLiteral("组合序列 ×%1：尺寸切换支世代仍递增（%2 次）")
                 .arg(s04.units).arg(s04.genIncrements));
        mark(QStringLiteral("LF-04b"), s04.slotsOk && s04.ageOk && s04.readyOk,
             QStringLiteral("组合序列 ×%1：队列仍有界（完整槽峰值 %2，最新帧年龄峰值 %3ms）"
                            "+ 视口仍 ready")
                 .arg(s04.units).arg(s04.maxSlots).arg(s04.maxAgeMs));
        mark(QStringLiteral("LF-04c"), s04.maxConsecZero <= 1,
             QStringLiteral("组合序列 ×%1：帧号仍推进（最长连续零推进 %2，零推进轮 %3/%1）｜%4")
                 .arg(s04.units).arg(s04.maxConsecZero).arg(s04.zeroRounds)
                 .arg(s04.deltaSummary()));
    }

    // ── 自证：台账 id 是否全部被记账（陷阱 85 的再犯防火墙）──────────────
    const QStringList expected = {
        QStringLiteral("LF-02a"), QStringLiteral("LF-02b"), QStringLiteral("LF-02c"),
        QStringLiteral("LF-02d"), QStringLiteral("LF-02e"), QStringLiteral("LF-02f"),
        QStringLiteral("LF-03a"), QStringLiteral("LF-03b"), QStringLiteral("LF-03c"),
        QStringLiteral("LF-03d"),
        QStringLiteral("LF-04a"), QStringLiteral("LF-04b"), QStringLiteral("LF-04c"),
    };
    QStringList missing;
    for (const QString &id : expected)
        if (!marked.contains(id))
            missing << id;
    if (!missing.isEmpty())
    {
        result.details << QStringLiteral("[FAIL] LEDGER — 以下判据 id 未记账（陷阱 85）：%1")
                              .arg(missing.join(QStringLiteral(", ")));
        items << qMakePair(QStringLiteral("LEDGER"), false);
    }

    int passed = 0;
    for (const auto &it : items)
        if (it.second)
            ++passed;
    result.passed = passed;
    result.total = int(items.size());
    result.pass = (result.total > 0 && passed == result.total);
    // ⚠️ 汇总行必须含"判据 N/M"字样：回归脚本按这个形状取读数
    //（陷阱 85 同族 —— 台账口径与外部读取口径要逐字对齐）。
    result.summary = QStringLiteral("生命周期回归自检：判据 %1/%2 通过"
                                    "（LF-02 ×%3 / LF-03 ×%4 / LF-04 ×%5）")
                         .arg(passed).arg(result.total)
                         .arg(s02.units).arg(s03.units).arg(s04.units);
    if (result_cb)
        result_cb(result);
}

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
        s.leased = ms.leased;
        s.readersHeld = ms.readersHeld;
        s.bytesPerFrame = qint64(ms.bytesPerFrame);
    }
    return s;
}

//! host 侧四计数（生产者线程真值）。恒等 0 表示替身未装配或非替身实现。
void hostCounters(Ctx *c, quint64 &req, quint64 &pub, quint64 &drop, quint64 &addr)
{
    req = pub = drop = addr = 0;
    if (!c->producer)
        return;
    const ProducerCounters cc = c->producer->counters();
    req = cc.hostRequested;
    pub = cc.hostPublished;
    drop = cc.hostDroppedByMailbox;
    addr = cc.hostMailboxAddr;
}

//! 把诊断基准推到"此刻"（**每组开头调一次**）。
//! 否则首个诊断窗口打印的是"开机以来的累计值"，与后续的窗口增量不同量纲
//! （首版实测：`leased +1298` / `hostReq +5519` —— 那是累计值，不是 25 轮窗口）。
void rebaseCounters(Ctx *c)
{
    const Snap now = snapOf(c);
    c->prodRendPrev = c->producer ? c->producer->counters().rendered : 0;
    c->leasedPrev = now.leased;
    quint64 addr = 0;
    hostCounters(c, c->hReqPrev, c->hPubPrev, c->hDropPrev, addr);
}

} // namespace

void LifeCycleCheck::run(QCoreApplication *app,
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
    // 负控开关（见 Ctx 里的说明）：关掉**被测行为**，不伪造观测面。
    ctx->noSize = qEnvironmentVariableIsSet("STELQUICK_LIFECYCLE_NOSIZE");
    ctx->useHide = qEnvironmentVariableIsSet("STELQUICK_LIFECYCLE_HIDE");
    ctx->noSettle = qEnvironmentVariableIsSet("STELQUICK_LIFECYCLE_NOSETTLE");
    ctx->result.ran = true;
    ctx->result_cb = onDone;
    ctx->vp = window ? window->findChild<SkyViewport *>(QStringLiteral("skyViewport")) : nullptr;

    const auto unavailable = [ctx](const QString &why) {
        ctx->result.ran = false;
        ctx->result.unavailable = true;
        ctx->result.total = 0;
        ctx->result.summary = QStringLiteral("生命周期自检不可用：%1").arg(why);
        if (ctx->result_cb)
            ctx->result_cb(ctx->result);
    };
    if (!window)
        return unavailable(QStringLiteral("无窗口"));
    if (!ctx->vp)
        return unavailable(QStringLiteral("未按 objectName 找到 skyViewport"));
    if (!mailbox)
        return unavailable(QStringLiteral("无帧邮箱"));
    if (!producer)
        return unavailable(QStringLiteral("替身生产者未装配（帧流是本组全部「推进」判据的前提）"));

    auto steps = std::make_shared<QVector<Step>>();
    auto tick = std::make_shared<std::function<void(int)>>();
    auto runStep = [steps, ctx, tick](int i) {
        if (ctx->abort || i >= steps->size())
        {
            ctx->finish();
            return;
        }
        const int wait = steps->at(i).waitBefore;
        if (wait > 0)
        {
            QTimer::singleShot(wait, ctx->app, [steps, ctx, tick, i]() {
                if (ctx->abort || i >= steps->size())
                {
                    ctx->finish();
                    return;
                }
                steps->at(i).body(ctx.get());
                (*tick)(i + 1);
            });
        }
        else
        {
            steps->at(i).body(ctx.get());
            (*tick)(i + 1);
        }
    };
    *tick = [runStep](int i) { runStep(i); };
    QTimer::singleShot(delayMs, app, [runStep]() { runStep(0); });

    // ── S1 布场 + **帧流自证**（陷阱 3：仪器没接在实况上 ⇒ 报 UNAVAILABLE）──
    steps->append(Step{300, [](Ctx *c) {
        c->baseGeometry = c->window->geometry();
        c->loopPrev = snapOf(c);
        c->note(QStringLiteral("布场：几何 %1｜视口 ready=%2 generation=%3 displayed=%4｜"
                               "邮箱 投递=%5 完整槽=%6 sizeGen=%7 帧龄=%8ms｜footprint=%9 MiB")
                    .arg(geomStr(c->baseGeometry)).arg(c->loopPrev.ready)
                    .arg(c->loopPrev.generation).arg(c->loopPrev.displayed)
                    .arg(c->loopPrev.published).arg(c->loopPrev.completeSlots)
                    .arg(c->loopPrev.sizeGeneration).arg(c->loopPrev.ageMs)
                    .arg(toMiB(footprintBytes())));
    }});
    steps->append(Step{300, [](Ctx *c) {
        const Snap now = snapOf(c);
        c->framesFlow = now.displayed > c->loopPrev.displayed;
        if (!c->framesFlow)
        {
            c->abortWhy = QStringLiteral("布场 300ms 内 displayedFrameNumber 未增长"
                                         "（%1 → %2）⇒ 帧流不在，本组判据全部无意义")
                              .arg(c->loopPrev.displayed).arg(now.displayed);
            c->note(QStringLiteral("⚠️ 布场自证失败：%1").arg(c->abortWhy));
            c->abort = true;
        }
        else
        {
            c->note(QStringLiteral("布场自证：帧流在（displayed +%1）")
                        .arg(now.displayed - c->loopPrev.displayed));
        }
        c->loopPrev = now;
    }});

    // ── S2 LF-02：最小化/恢复 ×100 ────────────────────────────────────────
    // 每轮：记 prev → 最小化 → **等 500ms 让最小化真的生效** → 记最小化态
    //       → 恢复 → 等 400ms → 采样。
    // ⚠️ 收敛期**不可省**：动作只发"请求"，平台把它变成状态变更要走一轮事件循环。
    //    不等就采样 ⇒ 读到的是"0ms 后"的世界（首版即栽在这里）。
    steps->append(Step{100, [](Ctx *c) {
        c->s02.fp0 = footprintBytes();
        rebaseCounters(c);
    }});
    for (int r = 0; r < kRounds; ++r)
    {
        steps->append(Step{0, [r](Ctx *c) {
            c->loopPrev = snapOf(c);
            doMinimize(c);
        }});
        steps->append(Step{ctx->settleMin(), [r](Ctx *c) {
            // 最小化**稳态**：窗口态必须真的进"最小化"（LF-02f）。
            // hide() 代理在这里就露馅了：窗口态仍是"正常"。
            const Snap m = snapOf(c);
            if (m.windowState != int(Qt::WindowMinimized))
                c->s02.minStateOk = false;
            if (r % 25 == 24)
                c->note(QStringLiteral("LF-02 轮 %1 最小化稳态：visible=%2 exposed=%3 state=%4"
                                       "｜几何 %5")
                            .arg(r + 1).arg(m.visible).arg(m.exposed)
                            .arg(stateName(m.windowState)).arg(geomStr(m.geometry)));
        }});
        steps->append(Step{0, [](Ctx *c) { doRestore(c); }});
        steps->append(Step{ctx->settleRestore(), [r](Ctx *c) {
            const Snap now = snapOf(c);
            if (now.windowState != int(Qt::WindowNoState))
                c->s02.minStateOk = false;
            c->s02.fold(now.displayed - c->loopPrev.displayed);
            c->observe(c->s02, now, /*checkGeom=*/true);
            // 中途诊断（每 25 轮）：帧流衰减 / 窗口态错乱必须看得见时间点。
            // 注意两组窗口口径不同，分别标注（陷阱 65：别把不同量纲写在一条里）。
            if (r % 25 == 24)
            {
                quint64 hReq = 0, hPub = 0, hDrop = 0, hAddr = 0;
                hostCounters(c, hReq, hPub, hDrop, hAddr);
                const quint64 rend = c->producer ? c->producer->counters().rendered : 0;
                c->note(QStringLiteral("LF-02 轮 %1：%2｜**本轮** displayed +%3 投递 +%4 丢弃 +%5"
                                       "｜帧龄 %6ms 槽 %7｜state=%8 exposed=%9"
                                       "｜**25 轮** rendered +%10 hostPub +%11 hostDrop +%12")
                            .arg(r + 1).arg(geomStr(now.geometry))
                            .arg(now.displayed - c->loopPrev.displayed)
                            .arg(now.published - c->loopPrev.published)
                            .arg(now.dropped - c->loopPrev.dropped)
                            .arg(now.ageMs).arg(now.completeSlots)
                            .arg(stateName(now.windowState)).arg(now.exposed)
                            .arg(rend - c->prodRendPrev)
                            .arg(hPub - c->hPubPrev).arg(hDrop - c->hDropPrev));
                c->prodRendPrev = rend;
                c->hPubPrev = hPub; c->hDropPrev = hDrop; c->hReqPrev = hReq;
            }
            c->loopPrev = now;
        }});
    }
    steps->append(Step{0, [](Ctx *c) { c->s02.fp1 = footprintBytes(); }});

    // ── S3 LF-03：尺寸切换 ×100（4 档尺寸循环；世代递增 + 帧仍流 + 内存）──
    steps->append(Step{100, [](Ctx *c) {
        c->s03.fp0 = footprintBytes();
        rebaseCounters(c);
    }});
    for (int r = 0; r < kRounds; ++r)
    {
        const int k = (r % 4) + 1;
        steps->append(Step{0, [k](Ctx *c) {
            c->loopPrev = snapOf(c);
            doResize(c, k);
        }});
        steps->append(Step{ctx->settleResize(), [r](Ctx *c) {
            const Snap now = snapOf(c);
            const qint32 dGen = qint32(now.generation) - qint32(c->loopPrev.generation);
            if (dGen >= 1)
                ++c->s03.genIncrements;
            if (dGen < 0)
                c->s03.genMonoOk = false;
            c->s03.fold(now.displayed - c->loopPrev.displayed);
            c->observe(c->s03, now, /*checkGeom=*/false);
            if (r % 25 == 24)
            {
                quint64 hReq = 0, hPub = 0, hDrop = 0, hAddr = 0;
                hostCounters(c, hReq, hPub, hDrop, hAddr);
                const quint64 rend = c->producer ? c->producer->counters().rendered : 0;
                // 对象身份对账：host attach 的邮箱必须**就是**本 Check 读的那个
                //（不等 ⇒ 两边不是同一个邮箱，一切"计数矛盾"都解释通了 —— 陷阱 43）
                if (hAddr != 0 && hAddr != reinterpret_cast<quint64>(c->mailbox))
                    c->note(QStringLiteral("⚠️ 对象身份分裂：Check 读 %1，host attach 的是 %2")
                                .arg(reinterpret_cast<quint64>(c->mailbox), 0, 16)
                                .arg(hAddr, 0, 16));
                if (hAddr == 0)
                    c->note(QStringLiteral("⚠️ host 未报告邮箱地址（hostMailboxAddr=0）"
                                           "⇒ 身份对账**未生效**，不得当作已完成核对"));
                c->note(QStringLiteral("LF-03 轮 %1：世代 %2→%3（本轮 dGen=%4）｜**本轮** displayed +%5 "
                                       "投递 +%6 丢弃 +%7 leased +%8｜sizeGen %9 槽 %10 年龄 %11ms "
                                       "bytes/frame %12｜**25 轮** rendered +%13 hostReq +%14 "
                                       "hostPub +%15 hostDrop +%16｜exposed=%17")
                            .arg(r + 1).arg(c->loopPrev.generation).arg(now.generation).arg(dGen)
                            .arg(now.displayed - c->loopPrev.displayed)
                            .arg(now.published - c->loopPrev.published)
                            .arg(now.dropped - c->loopPrev.dropped)
                            .arg(now.leased - c->leasedPrev)
                            .arg(now.sizeGeneration).arg(now.completeSlots).arg(now.ageMs)
                            .arg(now.bytesPerFrame)
                            .arg(rend - c->prodRendPrev)
                            .arg(hReq - c->hReqPrev).arg(hPub - c->hPubPrev).arg(hDrop - c->hDropPrev)
                            .arg(now.exposed));
                c->prodRendPrev = rend;
                c->leasedPrev = now.leased;
                c->hReqPrev = hReq; c->hPubPrev = hPub; c->hDropPrev = hDrop;
            }
            c->loopPrev = now;
        }});
    }
    steps->append(Step{0, [](Ctx *c) {
        c->s03.fp1 = footprintBytes();
        c->window->setGeometry(c->baseGeometry);   // 复原
    }});

    // ── S4 LF-04：组合序列 ×100（偶数轮最小化/恢复、奇数轮尺寸切换）───────
    steps->append(Step{300, [](Ctx *c) {
        c->loopPrev = snapOf(c);
        c->s04.fp0 = footprintBytes();
        rebaseCounters(c);
        c->note(QStringLiteral("组合序列起始：几何已复原 %1").arg(geomStr(c->baseGeometry)));
    }});
    for (int r = 0; r < kRounds; ++r)
    {
        if (r % 2 == 0)
        {
            steps->append(Step{0, [](Ctx *c) {
                c->loopPrev = snapOf(c);
                doMinimize(c);
            }});
            steps->append(Step{ctx->settleMin(), [](Ctx *c) { doRestore(c); }});
            steps->append(Step{ctx->settleRestore(), [r](Ctx *c) {
                const Snap now = snapOf(c);
                c->s04.fold(now.displayed - c->loopPrev.displayed);
                c->observe(c->s04, now, /*checkGeom=*/false);
                // 偶数轮才走最小化支 ⇒ 用 18（19 永不命中偶数 —— 首版的采样死角）
                if (r % 20 == 18)
                {
                    const quint64 rend = c->producer ? c->producer->counters().rendered : 0;
                    c->note(QStringLiteral("LF-04 轮 %1（最小化支）：**本轮** 帧号 +%2 投递 +%3"
                                           "｜帧龄 %4ms 槽 %5｜state=%6 exposed=%7"
                                           "｜**20 轮** rendered +%8")
                                .arg(r + 1).arg(now.displayed - c->loopPrev.displayed)
                                .arg(now.published - c->loopPrev.published)
                                .arg(now.ageMs).arg(now.completeSlots)
                                .arg(stateName(now.windowState)).arg(now.exposed)
                                .arg(rend - c->prodRendPrev));
                    c->prodRendPrev = rend;
                }
                c->loopPrev = now;
            }});
        }
        else
        {
            const int k = ((r / 2) % 4) + 1;
            steps->append(Step{0, [k](Ctx *c) {
                c->loopPrev = snapOf(c);
                doResize(c, k);
            }});
            steps->append(Step{ctx->settleResize(), [r](Ctx *c) {
                const Snap now = snapOf(c);
                if (now.generation > c->loopPrev.generation)
                    ++c->s04.genIncrements;
                c->s04.fold(now.displayed - c->loopPrev.displayed);
                c->observe(c->s04, now, /*checkGeom=*/false);
                if (r % 20 == 19)
                {
                    const quint64 rend = c->producer ? c->producer->counters().rendered : 0;
                    c->note(QStringLiteral("LF-04 轮 %1（尺寸支）：世代 %2→%3｜**本轮** 帧号 +%4 "
                                           "投递 +%5｜帧龄 %6ms 槽 %7｜exposed=%8"
                                           "｜**20 轮** rendered +%9")
                                .arg(r + 1).arg(c->loopPrev.generation).arg(now.generation)
                                .arg(now.displayed - c->loopPrev.displayed)
                                .arg(now.published - c->loopPrev.published)
                                .arg(now.ageMs).arg(now.completeSlots).arg(now.exposed)
                                .arg(rend - c->prodRendPrev));
                    c->prodRendPrev = rend;
                }
                c->loopPrev = now;
            }});
        }
    }
    steps->append(Step{0, [](Ctx *c) { c->s04.fp1 = footprintBytes(); }});

    // ── S5 收尾：还原到**布场态**（陷阱 90：无条件恢复会自己修好被负控打断的路径）──
    steps->append(Step{0, [](Ctx *c) {
        if (c->window->windowState() != Qt::WindowNoState)
            c->window->showNormal();
        if (c->window->geometry() != c->baseGeometry)
            c->window->setGeometry(c->baseGeometry);
        c->note(QStringLiteral("收尾：窗口已还原到布场态（几何 %1 / state=%2）")
                    .arg(geomStr(c->baseGeometry))
                    .arg(stateName(int(c->window->windowStates()))));
    }});

    std::fprintf(stderr,
                 "LIFECHECK: 生命周期回归自检开跑（×%d ×3 组，delayMs=%d，收敛期 "
                 "%d/%d/%dms%s%s）\n",
                 kRounds, delayMs, ctx->settleMin(), ctx->settleRestore(), ctx->settleResize(),
                 ctx->noSettle ? " ⚠️负控 NOSETTLE" : "",
                 ctx->useHide ? " ⚠️负控 HIDE" : "");
    std::fflush(stderr);
}

} // namespace stelapp
