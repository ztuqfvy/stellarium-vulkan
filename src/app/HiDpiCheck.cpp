/*
 * HiDpiCheck — 实现（T39-C 高 DPI + 渲染诊断自检）。判据清单与设计依据见头注。
 *
 * 驱动方式沿用 DisplayCheck / NightModeCheck 的线性步骤表：每步 = (跑完后等多少 ms, 步骤体)。
 * ⚠️ delayAfter 语义 = "跑完**本步**之后的等待"（T33 陷阱 41 / T37 陷阱 69 / T38 首轮）：
 * 凡"写后要读"的配对，读一律放下一步；**写步挂延迟**（同事件循环里读 = 读到写前的值）。
 */
#include "app/HiDpiCheck.hpp"

#include "app/AppFacade.hpp"
#include "app/FrameCompare.hpp"
#include "render/legacy/FrameMailbox.hpp"

#include <QCoreApplication>
#include <QMouseEvent>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTimer>
#include <QVector>

#if defined(STELQUICK_HAS_ENGINE)
#include "StelApp.hpp"
#endif

namespace stelapp {

namespace {

using CheckResult = HiDpiCheck::Result;
using framecmp::DiffStats;
using framecmp::FrameSample;

#if defined(STELQUICK_HAS_ENGINE) && defined(STELQUICK_WIDGETS_HOST)

// 噪声容差（**幅度口径** —— T38 实测：冻结下亚 LSB 抖动的面积会随场景状态变、
// 幅度不会）。⚠️ 门限必须**同布场实测**（HP-00）：T39 探针里本布场噪声是
// 逐位相同，T38 布场是 0.079%/Δ1-2 —— 不能跨布场搬数字。
constexpr int kNoiseMaxDelta = 2;

// 效应门（帧级判据）：差异像素占比至少 1%（探针实测字号效应 4.054%，噪声 0 ——
// 1% 的门在两者之间，留了 4 倍余量）。
constexpr double kEffectMinRatio = 0.01;

bool belowNoise(const DiffStats &d)
{
    if (!d.comparable)
        return false;
    if (d.differing == 0)
        return true;
    return d.maxDelta <= kNoiseMaxDelta;
}

double ratioOf(const DiffStats &d)
{
    return (d.comparable && d.pixels > 0) ? double(d.differing) / double(d.pixels) : 0.0;
}

//! 视觉树里 objectName 相符的全部项（陷阱 45：Repeater delegate 的 QObject 父链
//! 是空的 ⇒ 一律走视觉树 childItems 递归；与 DisplayCheck 同款，提取待做）。
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

//! 向窗口投递一次真实鼠标点击（ToolbarCheck/AC-12 同款：最外层注入，
//! 不经控件的 click() 捷径 —— invokeMethod 走不到 moved 信号，真实事件才走得到）。
bool clickAt(QQuickWindow *window, const QPointF &scenePos)
{
    const QPoint global = window->mapToGlobal(scenePos.toPoint());
    QMouseEvent press(QEvent::MouseButtonPress, scenePos, scenePos,
                      QPointF(global), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QMouseEvent release(QEvent::MouseButtonRelease, scenePos, scenePos,
                        QPointF(global), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(window, &press);
    QCoreApplication::sendEvent(window, &release);
    return press.isAccepted() || release.isAccepted();
}

//! 沿视觉父链向上找 item 所属的 Flickable（ScrollView 的内部实现）。
//! 🔴 不能从 root 向下 DFS：StackLayout 的**所有页面**都在视觉树里，第一页的
//! Flickable 会先被摸到 —— T39 首轮实测滚了个别人的滚不到的条（滑块 sceneY 纹丝不动）。
QQuickItem *findAncestorFlickable(QQuickItem *item)
{
    for (QQuickItem *p = item->parentItem(); p; p = p->parentItem())
        if (QString::fromLatin1(p->metaObject()->className()).contains(QLatin1String("Flickable")))
            return p;
    return nullptr;
}

struct Ctx
{
    QCoreApplication *app = nullptr;
    AppFacade *facade = nullptr;
    QQuickWindow *window = nullptr;
    FrameMailbox *mailbox = nullptr;
    std::function<void(const CheckResult &)> onDone;
    CheckResult result;
    QStringList lines;

    //! 判据台账（mark/finish）。⚠️ 约定：note 的**第一个词**必须是判据 id
    //! （"HP-01 引擎面往返：..."）—— id 从 note 里解析，调用处不用重复传。
    struct Item
    {
        bool done = false;
        bool pass = false;
        bool na = false;      //!< 测不出（方法无效/异常帧）⇒ INCONCLUSIVE，不算 FAIL
    };
    QVector<QPair<QString, Item>> items;   // 有序（汇总按清单顺序）
    int inconclusiveCount = 0;

    Item *findItem(const QString &id)
    {
        for (auto &kv : items)
            if (kv.first == id)
                return &kv.second;
        return nullptr;
    }

    void mark(bool pass, const QString &note)
    {
        const QString id = note.section(QLatin1Char(' '), 0, 0);
        if (Item *it = findItem(id))
        {
            it->done = true;
            it->pass = pass;
        }
        lines.append(QStringLiteral("[%1] %2")
                         .arg(pass ? QStringLiteral("PASS") : QStringLiteral("FAIL"), note));
    }

    //! 该判据"测不出"（方法无效/异常帧）⇒ 记 INCONCLUSIVE，不算 FAIL。
    void markNa(const QString &note)
    {
        const QString id = note.section(QLatin1Char(' '), 0, 0);
        if (Item *it = findItem(id))
        {
            it->done = true;
            it->na = true;
        }
        ++inconclusiveCount;
        lines.append(QStringLiteral("[NA ] %1（⇒ 整套 INCONCLUSIVE）").arg(note));
    }

    void finish()
    {
        int passed = 0;
        int judged = 0;
        for (const auto &kv : items)
        {
            if (!kv.second.done || kv.second.na)
                continue;
            ++judged;
            if (kv.second.pass)
                ++passed;
        }
        result.passed = passed;
        result.total = judged;
        result.inconclusive = inconclusiveCount > 0;
        result.pass = !result.inconclusive && judged == items.size() && passed == judged;
        result.summary = result.inconclusive
                             ? QStringLiteral("高 DPI 自检：INCONCLUSIVE（%1 条测不出，%2/%3 过）")
                                   .arg(inconclusiveCount)
                                   .arg(passed)
                                   .arg(judged)
                             : QStringLiteral("高 DPI 自检：判据 %1/%2").arg(passed).arg(judged);
        result.details = lines;
        onDone(result);
    }

    //! 初值台账（收尾还原）。
    int screenFont = 0;
    int guiFont = 0;
    double buttonScale = 0.0;
    bool wasPaused = false;
    bool pausedByUs = false;
    // ── 布场昼夜确定性（T42 收口轮实测的血泪，与 DisplayCheck 同款）──────────
    // 开机 JD = 墙钟 ⇒ 白天跑批时天空是**纯大气渐变、一个标签都没有** ⇒
    // HP-09 的"字号帧效应"（探针夜间标定 4.054%）实测只剩 0.001 ⇒ 假红。
    // 治法：布场把 JD 钉在**固定夜时刻**（星空+标签可见 ⇒ 与标定环境一致），
    // restoreAll 还原。
    double jdRef = 0.0;
    bool jdChangedByUs = false;

    //! 帧级判据的参考帧（HP-00 噪声底帧② = HP-09 的基准）。
    FrameSample refFrame;

    //! 还原全部高 DPI 参数 + 仿真状态。
    void restoreAll()
    {
#if defined(STELQUICK_HAS_ENGINE)
        if (StelApp::isInitialized())
        {
            StelApp::getInstance().setScreenFontSize(screenFont);
            StelApp::getInstance().setGuiFontSize(guiFont);
            StelApp::getInstance().setScreenButtonScale(buttonScale);
        }
#endif
        if (pausedByUs && facade)
            facade->setSimulationPaused(false);
        if (jdChangedByUs && facade)
            facade->setJulianDay(jdRef);
    }

    void note(const QString &line) { lines.append(line); }
};

struct Step
{
    int delayAfter = 0;
    std::function<void(Ctx *)> body;
};

//! 布尔转"开/关"。
QString onOff(bool b) { return b ? QStringLiteral("开") : QStringLiteral("关"); }

#endif  // STELQUICK_HAS_ENGINE && STELQUICK_WIDGETS_HOST

} // namespace

void HiDpiCheck::run(QCoreApplication *app,
                     AppFacade *facade,
                     QQuickWindow *window,
                     FrameMailbox *mailbox,
                     const std::function<void(const Result &)> &onDone,
                     int delayMs)
{
#if !defined(STELQUICK_HAS_ENGINE) || !defined(STELQUICK_WIDGETS_HOST)
    Q_UNUSED(app)
    Q_UNUSED(facade)
    Q_UNUSED(window)
    Q_UNUSED(mailbox)
    Q_UNUSED(delayMs)
    Result r;
    r.unavailable = true;
    r.summary = QStringLiteral("高 DPI 自检需要合流形态构建");
    QTimer::singleShot(0, app, [onDone, r]() { onDone(r); });
#else
    auto *ctx = new Ctx;
    ctx->app = app;
    ctx->facade = facade;
    ctx->window = window;
    ctx->mailbox = mailbox;
    ctx->onDone = onDone;
    for (const QString &id : {QStringLiteral("HP-00"), QStringLiteral("HP-01"),
                              QStringLiteral("HP-02"), QStringLiteral("HP-03"),
                              QStringLiteral("HP-04"), QStringLiteral("HP-05"),
                              QStringLiteral("HP-06"), QStringLiteral("HP-07"),
                              QStringLiteral("HP-08"), QStringLiteral("HP-09"),
                              QStringLiteral("HP-10"), QStringLiteral("HP-11")})
        ctx->items.append({id, {}});

    QQuickItem *root = window ? window->contentItem() : nullptr;

    auto steps = std::make_shared<QVector<Step>>();
    auto idx = std::make_shared<int>(0);
    auto tick = std::make_shared<std::function<void()>>();

    // ── S1：布场 + 初值台账 ────────────────────────────────────────────────
    // ⚠️ delayAfter 6500：跳 JD 后瞳孔适应（StelSkyDrawer::reportLuminanceInFov 的
    // log 平滑律，transitionSpeed=0.2 ⇒ 每秒只剩 35% 差距）需要数秒收敛，
    // 否则 HP-00/DP-00 噪声底门吃到的还是 ramp 中段的帧（T42 收口轮实测）。
    steps->append({6500, [](Ctx *c) {
        c->screenFont = c->facade->screenFontSize();
        c->guiFont = c->facade->guiFontSize();
        c->buttonScale = c->facade->screenButtonScale();
        c->wasPaused = c->facade->simulationPaused();
        if (!c->wasPaused)
        {
            c->facade->setSimulationPaused(true);
            c->pausedByUs = true;
        }
        // 固定夜 JD（见 Ctx 注释）：先记原值再写（陷阱 9：判据改的环境同批还原）。
        c->jdRef = c->facade->julianDay();
        c->facade->setJulianDay(2461000.4167);   // ≈2025-11-16 22:00 UT（夜）
        c->jdChangedByUs = true;
        c->note(QStringLiteral("初值：screenFontSize=%1 guiFontSize=%2 screenButtonScale=%3 "
                               "｜dpp=%4 screenScale=%5 guiScale=%6")
                    .arg(c->screenFont)
                    .arg(c->guiFont)
                    .arg(c->buttonScale)
                    .arg(c->facade->devicePixelsPerPixel())
                    .arg(c->facade->screenScale())
                    .arg(c->facade->guiScale()));
        c->note(QStringLiteral("布场：仿真冻结 %1｜JD 钉夜（%2 → 2461000.4167）"
                               "（帧静定 —— 像素比较前提；大气**不动**：探针已证"
                               "夜间布场下天空文本可见且效应 4.054%）")
                    .arg(c->pausedByUs ? QStringLiteral("已由判据置停")
                                       : (c->wasPaused ? QStringLiteral("本来就在停")
                                                       : QStringLiteral("未置停")))
                    .arg(c->jdRef, 0, 'f', 4));
    }});

    // ── S2：噪声底帧①（HP-09 的基准就在下一步取）───────────────────────────
    steps->append({500, [](Ctx *c) {
        c->refFrame = framecmp::grabUpstream(c->mailbox);
        c->note(QStringLiteral("噪声底帧①：%1").arg(framecmp::frameText(c->refFrame)));
    }});

    // ── S3：噪声底帧② + HP-00 判 ──────────────────────────────────────────
    steps->append({500, [](Ctx *c) {
        const FrameSample b = framecmp::grabUpstream(c->mailbox);
        const DiffStats d = framecmp::diffOf(c->refFrame, b);
        c->note(QStringLiteral("噪声底帧②：%1").arg(framecmp::frameText(b)));
        if (belowNoise(d))
        {
            c->mark(true, QStringLiteral("HP-00 噪声底门：冻结后同状态两帧**低于噪声容差**"
                                         "（实测 %1，Δ≤%2）—— 后续像素判据口径有效")
                               .arg(framecmp::diffText(d))
                               .arg(kNoiseMaxDelta));
        }
        else
        {
            c->mark(false, QStringLiteral("HP-00 噪声底门：同状态两帧差异 %1 超出容差"
                                          "（Δ≤%2）⇒ 帧没静定 ⇒ 后续像素判据不可信")
                               .arg(framecmp::diffText(d))
                               .arg(kNoiseMaxDelta));
            c->markNa(QStringLiteral("HP-09 噪声底门没过，帧级判据不做"));
        }
        // 参考帧固定用②（最新）。
        c->refFrame = b;
    }});

    // ── S3b：噪声底**判别对照**（W-T41 跨平台复验 2026-10-01）────────────────
    // Windows 上 HP-00 红（差异 0.178% / Δ32，包围盒 (5,0)-(1271,449) = 上半屏），
    // mac 实测为 0；两轮逐位一致 ⇒ 不是随机抖动，是系统性的"帧间在变"。
    // 这条判别对照把两种解释分开：
    //   收敛不足（等更久就一样）  vs  渲染本身非确定 / 有持续变化源（永远不一样）
    // 只在 HP-00 红时才有信息量；绿时它只是再证一次噪声为 0，措辞保持中性。
    steps->append({2500, [](Ctx *c) {
        const FrameSample c3 = framecmp::grabUpstream(c->mailbox);
        const DiffStats d3 = framecmp::diffOf(c->refFrame, c3);   // refFrame 此刻 = 帧②
        c->note(QStringLiteral("HP-00 判别对照：帧② → +2500ms 帧③ 差异 %1 ⇒ %2")
                    .arg(framecmp::diffText(d3))
                    .arg(belowNoise(d3)
                             ? QStringLiteral("已静定（原红若发生属收敛不足）")
                             : QStringLiteral("仍在变 ⇒ 渲染非确定或有持续变化源")));
    }});

    // ── S4：HP-01 写（façade 写 33）────────────────────────────────────────
    steps->append({300, [](Ctx *c) {
        c->facade->setScreenFontSize(33);
        c->note(QStringLiteral("HP-01 已 façade 写 screenFontSize=33（回读放下一步）"));
    }});

    // ── S5：HP-01 判（façade 读回 + 引擎 getter 独立回读）+ HP-02 写 99 ──────
    steps->append({300, [](Ctx *c) {
        const int f = c->facade->screenFontSize();
        int e = -1;
#if defined(STELQUICK_HAS_ENGINE)
        if (StelApp::isInitialized())
            e = StelApp::getInstance().getScreenFontSize();   // 独立回读（陷阱 43）
#endif
        const bool ok = (f == 33 && e == 33);
        c->mark(ok, QStringLiteral("HP-01 引擎面往返：façade 写 33 ⇒ façade 读回 %1 ∧ "
                                   "引擎 getter 独立回读 %2")
                        .arg(f)
                        .arg(e));
        c->facade->setScreenFontSize(99);   // HP-02 的"远"值
        c->note(QStringLiteral("HP-02 已写 99（超上限，回读放下一步）"));
    }});

    // ── S6：HP-02 判（99 → 50）+ 写 0 ─────────────────────────────────────
    steps->append({300, [](Ctx *c) {
        const int f = c->facade->screenFontSize();
        int e = -1;
#if defined(STELQUICK_HAS_ENGINE)
        if (StelApp::isInitialized())
            e = StelApp::getInstance().getScreenFontSize();
#endif
        const bool ok = (f == 50 && e == 50)
                        && c->facade->lastDisplayRefusal() == QStringLiteral("out-of-range");
        c->mark(ok, QStringLiteral("HP-02 范围闸（上沿）：写 99 ⇒ façade=%1 ∧ 引擎=%2 ∧ "
                                   "refusal=%3（原版 SpinBox 上限 50）")
                        .arg(f)
                        .arg(e)
                        .arg(c->facade->lastDisplayRefusal()));
        c->facade->setScreenFontSize(0);
        c->note(QStringLiteral("HP-02 已写 0（超下限，回读放下一步）"));
    }});

    // ── S7：HP-02 判（0 → 5）+ HP-03 写 99 ────────────────────────────────
    steps->append({300, [](Ctx *c) {
        const int f = c->facade->screenFontSize();
        int e = -1;
#if defined(STELQUICK_HAS_ENGINE)
        if (StelApp::isInitialized())
            e = StelApp::getInstance().getScreenFontSize();
#endif
        const bool ok = (f == 5 && e == 5);
        c->mark(ok, QStringLiteral("HP-02 范围闸（下沿）：写 0 ⇒ façade=%1 ∧ 引擎=%2"
                                   "（原版 SpinBox 下限 5）")
                        .arg(f)
                        .arg(e));
        c->facade->setGuiFontSize(99);
        c->note(QStringLiteral("HP-03 已写 guiFontSize=99（回读放下一步）"));
    }});

    // ── S8：HP-03 判（99 → 50）+ 写 0 ─────────────────────────────────────
    steps->append({300, [](Ctx *c) {
        const int f = c->facade->guiFontSize();
        int e = -1;
#if defined(STELQUICK_HAS_ENGINE)
        if (StelApp::isInitialized())
            e = StelApp::getInstance().getGuiFontSize();
#endif
        const bool ok = (f == 50 && e == 50);
        c->mark(ok, QStringLiteral("HP-03 范围闸（上沿）：写 99 ⇒ façade=%1 ∧ 引擎=%2"
                                   "（原版 SpinBox 上限 50）")
                        .arg(f)
                        .arg(e));
        c->facade->setGuiFontSize(0);
        c->note(QStringLiteral("HP-03 已写 0（回读放下一步）"));
    }});

    // ── S9：HP-03 判（0 → 7）+ HP-04 写 999 ───────────────────────────────
    steps->append({300, [](Ctx *c) {
        const int f = c->facade->guiFontSize();
        int e = -1;
#if defined(STELQUICK_HAS_ENGINE)
        if (StelApp::isInitialized())
            e = StelApp::getInstance().getGuiFontSize();
#endif
        const bool ok = (f == 7 && e == 7);
        c->mark(ok, QStringLiteral("HP-03 范围闸（下沿）：写 0 ⇒ façade=%1 ∧ 引擎=%2"
                                   "（原版 SpinBox 下限 7）")
                        .arg(f)
                        .arg(e));
        c->facade->setScreenButtonScale(999.0);
        c->note(QStringLiteral("HP-04 已写 screenButtonScale=999（回读放下一步）"));
    }});

    // ── S10：HP-04 判（999 → 200）+ 写 0 ──────────────────────────────────
    steps->append({300, [](Ctx *c) {
        const double f = c->facade->screenButtonScale();
        double e = -1.0;
#if defined(STELQUICK_HAS_ENGINE)
        if (StelApp::isInitialized())
            e = StelApp::getInstance().getScreenButtonScale();
#endif
        const bool ok = qFuzzyCompare(f, 200.0) && qFuzzyCompare(e, 200.0);
        c->mark(ok, QStringLiteral("HP-04 范围闸（上沿）：写 999 ⇒ façade=%1 ∧ 引擎=%2"
                                   "（⚠️ [50,200] 是产品自定 —— 原版只有配置键）")
                        .arg(f)
                        .arg(e));
        c->facade->setScreenButtonScale(0.0);
        c->note(QStringLiteral("HP-04 已写 0（回读放下一步）"));
    }});

    // ── S11：HP-04 判（0 → 50）+ 还原三属性 + HP-05 算术验算 ───────────────
    steps->append({300, [](Ctx *c) {
        const double f = c->facade->screenButtonScale();
        double e = -1.0;
#if defined(STELQUICK_HAS_ENGINE)
        if (StelApp::isInitialized())
            e = StelApp::getInstance().getScreenButtonScale();
#endif
        const bool ok = qFuzzyCompare(f, 50.0) && qFuzzyCompare(e, 50.0);
        c->mark(ok, QStringLiteral("HP-04 范围闸（下沿）：写 0 ⇒ façade=%1 ∧ 引擎=%2")
                        .arg(f)
                        .arg(e));

        // 还原三属性（后续 UI/帧级判据要从初值出发）。
        c->facade->setScreenFontSize(c->screenFont);
        c->facade->setGuiFontSize(c->guiFont);
        c->facade->setScreenButtonScale(c->buttonScale);

        // HP-05：派生量算术验算（陷阱 43：用 dpp×ratio 独立算，不与引擎读数同源）。
        const double dpp = c->facade->devicePixelsPerPixel();
        const double sExpect = dpp * (c->facade->screenFontSize() / 13.0);
        const double gExpect = dpp * (c->facade->guiFontSize() / 13.0);
        const double sGot = c->facade->screenScale();
        const double gGot = c->facade->guiScale();
        const bool ok5 = qAbs(sExpect - sGot) < 1e-6 && qAbs(gExpect - gGot) < 1e-6;
        c->mark(ok5, QStringLiteral("HP-05 派生量算术验算：screenScale 引擎 %1 vs 独立算 "
                                    "dpp(%2)×字号/13 = %3｜guiScale 引擎 %4 vs %5"
                                    "（差均 <1e-6）")
                         .arg(sGot)
                         .arg(dpp)
                         .arg(sExpect)
                         .arg(gGot)
                         .arg(gExpect));
    }});

    // ── S12：HP-06 UI 控件存在 + 滑块参数 ─────────────────────────────────
    steps->append({300, [root](Ctx *c) {
        const auto slider = collectByObjectName(root, QStringLiteral("displayScreenFontSlider"));
        const auto value = collectByObjectName(root, QStringLiteral("displayScreenFontValue"));
        const auto status = collectByObjectName(root, QStringLiteral("displayScaleStatusLabel"));
        const bool present = !slider.isEmpty() && !value.isEmpty() && !status.isEmpty();
        if (!present)
        {
            c->mark(false, QStringLiteral("HP-06 控件齐备：slider=%1 value=%2 status=%3 —— "
                                          "有缺失")
                               .arg(slider.size())
                               .arg(value.size())
                               .arg(status.size()));
            c->markNa(QStringLiteral("HP-07 滑块不存在"));
            c->markNa(QStringLiteral("HP-08 滑块不存在"));
            return;
        }
        QQuickItem *s = slider.first();
        const double from = s->property("from").toDouble();
        const double to = s->property("to").toDouble();
        const double val = s->property("value").toDouble();
        const bool paramsOk = qFuzzyCompare(from, c->facade->screenFontSizeMin())
                              && qFuzzyCompare(to, c->facade->screenFontSizeMax())
                              && qFuzzyCompare(val, double(c->facade->screenFontSize()));
        c->mark(paramsOk,
                QStringLiteral("HP-06 控件齐备：滑块 from=%1/to=%2/value=%3 vs façade "
                               "范围 [%4,%5]/当前 %6 ∧ 三个 objectName 全部找到")
                    .arg(from)
                    .arg(to)
                    .arg(val)
                    .arg(c->facade->screenFontSizeMin())
                    .arg(c->facade->screenFontSizeMax())
                    .arg(c->facade->screenFontSize()));
    }});

    // ── S13：HP-07 引擎侧写 30（UI 跟随放下一步）───────────────────────────
    steps->append({400, [](Ctx *c) {
#if defined(STELQUICK_HAS_ENGINE)
        if (StelApp::isInitialized())
            StelApp::getInstance().setScreenFontSize(30);   // **引擎侧**写（绕过 façade）
#endif
        c->note(QStringLiteral("HP-07 已引擎侧写 screenFontSize=30（读 UI 放下一步）"));
    }});

    // ── S14：HP-07 判（UI 跟随 30）+ 切到显示页（HP-08 真实点击的前提）──────
    steps->append({500, [root](Ctx *c) {
        const auto slider = collectByObjectName(root, QStringLiteral("displayScreenFontSlider"));
        if (slider.isEmpty())
        {
            c->markNa(QStringLiteral("HP-07 滑块不存在"));
            c->markNa(QStringLiteral("HP-08 滑块不存在"));
            return;
        }
        const double uiVal = slider.first()->property("value").toDouble();
        const bool ok7 = qFuzzyCompare(uiVal, 30.0);
        c->mark(ok7, QStringLiteral("HP-07 绑定腿：引擎侧写 30 ⇒ UI 滑块 value=%1 "
                                    "%2（守 T15：绑定必须真的读属性）")
                         .arg(uiVal)
                         .arg(ok7 ? QStringLiteral("跟随") : QStringLiteral("不跟随")));

        // HP-08：真实点击的前提 = 显示页**可见**（StackLayout 里的隐藏页不收事件，
        // invokeMethod("increase") 又不发 moved 信号 —— 首轮实测两者都测不出东西）。
        // 切页走真实点击 navDisplayButton（ToolbarCheck 的 clickAt 先例，AC-12 同款）。
        const auto navBtn = collectByObjectName(root, QStringLiteral("navDisplayButton"));
        if (navBtn.isEmpty())
        {
            c->markNa(QStringLiteral("HP-08 切页按钮不存在（navDisplayButton）"));
            return;
        }
        QQuickItem *btn = navBtn.first();
        clickAt(c->window, btn->mapToScene(QPointF(btn->width() / 2, btn->height() / 2)));
        c->note(QStringLiteral("HP-08 已真实点击 navDisplayButton（等切页 → 下一步点滑块）"));
    }});

    // ── S15：HP-08 滑块进视口（DisplayPage 内容长 ⇒ 滑块组在页面底部、常态在
    // ScrollView 视口外 —— isVisible() 只看标志位不看视口裁剪，T39 首轮实测
    // "点了个寂寞"。滚进视口是真实点击的前提）────────────────────────────────
    steps->append({400, [root](Ctx *c) {
        const auto slider = collectByObjectName(root, QStringLiteral("displayScreenFontSlider"));
        if (slider.isEmpty())
        {
            c->markNa(QStringLiteral("HP-08 滑块不存在"));
            return;
        }
        QQuickItem *s = slider.first();
        if (!s->isVisible())
        {
            c->markNa(QStringLiteral("HP-08 滑块不可见（切页未生效）"));
            return;
        }
        QPointF p = s->mapToScene(QPointF(s->width() * 0.7, s->height() / 2));
        if (p.y() >= c->window->height() - 8)
        {
            QQuickItem *flick = findAncestorFlickable(s);
            if (!flick)
            {
                c->markNa(QStringLiteral("HP-08 滑块在视口外（sceneY=%1，窗口高=%2）"
                                         "且其祖先链上找不到 Flickable 可滚")
                              .arg(p.y())
                              .arg(c->window->height()));
                return;
            }
            const double curY = flick->property("contentY").toDouble();
            const double shift = p.y() - c->window->height() * 0.5;
            flick->setProperty("contentY", curY + shift);
            c->note(QStringLiteral("HP-08 滑块在视口外（sceneY=%1，窗口高 %2）⇒ 已滚所属 "
                                   "Flickable contentY %3→%4")
                        .arg(p.y())
                        .arg(c->window->height())
                        .arg(curY)
                        .arg(curY + shift));
        }
        c->note(QStringLiteral("HP-08 滑块已进视口（sceneY=%1）").arg(p.y()));
    }});

    // ── S16：HP-08 真实点击滑块（点轨道 = setValue + moved —— 真用户通路）────
    steps->append({500, [root](Ctx *c) {
        const auto slider = collectByObjectName(root, QStringLiteral("displayScreenFontSlider"));
        if (slider.isEmpty())
        {
            c->markNa(QStringLiteral("HP-08 滑块不存在"));
            return;
        }
        QQuickItem *s = slider.first();
        const QPointF p = s->mapToScene(QPointF(s->width() * 0.7, s->height() / 2));
        const bool sent = clickAt(c->window, p);
        const double uiVal = s->property("value").toDouble();
        c->note(QStringLiteral("HP-08 已真实点击滑块 70% 位置（scene=%1,%2）—— clickAt 返回 %3｜"
                               "点击后 UI value=%4（点前 30；value 变了说明 press 到了滑块）")
                    .arg(p.x())
                    .arg(p.y())
                    .arg(sent ? QStringLiteral("true") : QStringLiteral("false"))
                    .arg(uiVal));
    }});

    // ── S16：HP-08 前半判 + 引擎侧写 25 ───────────────────────────────────
    steps->append({400, [](Ctx *c) {
        const int now = c->facade->screenFontSize();
        // 点击前 façade=30（S13 引擎侧写的）。点 70% 位置 ⇒ 值应变 ≈36。
        if (now == 30)
        {
            c->markNa(QStringLiteral("HP-08 交互模拟无效（点击后 façade 仍为 %1）—— "
                                     "真实点击未触发 onMoved，这条判据测不出东西，不硬判")
                          .arg(now));
        }
        else
        {
            c->note(QStringLiteral("HP-08 前半：真实点击 ⇒ façade=%1（点前 30）⇒ onMoved 通路活")
                        .arg(now));
        }
#if defined(STELQUICK_HAS_ENGINE)
        if (StelApp::isInitialized())
            StelApp::getInstance().setScreenFontSize(25);   // 引擎侧再改一次
#endif
        c->note(QStringLiteral("HP-08 后半：引擎侧已写 25（读 UI 放下一步）"));
    }});

    // ── S17：HP-08 后半判（交互后 UI 仍要跟随）+ 切回天空页 + 还原 ──────────
    steps->append({500, [root](Ctx *c) {
        const auto slider = collectByObjectName(root, QStringLiteral("displayScreenFontSlider"));
        if (!slider.isEmpty())
        {
            const double uiVal = slider.first()->property("value").toDouble();
            const bool ok = qFuzzyCompare(uiVal, 25.0);
            c->mark(ok, QStringLiteral("HP-08 交互后绑定仍活：真实点击交互后，引擎侧写 25 "
                                       "⇒ UI 滑块 value=%1 %2 —— Qt 6 Slider 若交互打破"
                                       "绑定，只有这条才测得出（T38 DP-07 没交互过）")
                             .arg(uiVal)
                             .arg(ok ? QStringLiteral("跟随")
                                     : QStringLiteral("不跟随（绑定已被交互杀死）")));
        }
        else
        {
            c->markNa(QStringLiteral("HP-08 滑块不存在"));
        }

        c->facade->setScreenFontSize(c->screenFont);   // 还原字号
        // 切回天空页（上游帧与 QML 页面无关，但保持"进场什么态、退场什么态"）。
        const auto navSky = collectByObjectName(root, QStringLiteral("navSkyButton"));
        if (!navSky.isEmpty())
        {
            QQuickItem *btn = navSky.first();
            clickAt(c->window, btn->mapToScene(QPointF(btn->width() / 2, btn->height() / 2)));
        }
        c->note(QStringLiteral("screenFontSize 已还原为 %1；已切回天空页").arg(c->screenFont));
    }});

    // ── S17：HP-09 帧效应写（40）───────────────────────────────────────────
    steps->append({700, [](Ctx *c) {
        c->facade->setScreenFontSize(40);
        c->note(QStringLiteral("HP-09 已写 screenFontSize=40（抓帧放下一步 —— 等待挂在写步）"));
    }});

    // ── S18：HP-09 帧判 + 还原 ────────────────────────────────────────────
    steps->append({700, [](Ctx *c) {
        const FrameSample after = framecmp::grabUpstream(c->mailbox);
        const DiffStats d = framecmp::diffOf(c->refFrame, after);
        c->note(QStringLiteral("HP-09 字号 40 帧：%1").arg(framecmp::frameText(after)));
        c->note(QStringLiteral("vs 参考帧：%1").arg(framecmp::diffText(d)));
        const bool ok = after.valid && d.comparable && ratioOf(d) >= kEffectMinRatio;
        if (after.valid)
            c->mark(ok, QStringLiteral("HP-09 字号帧效应（成对）：字号 %1→40 ⇒ 上游帧差异占比 "
                                       "%2 ≥ 门 %3 —— 探针实测本效应 4.054%、噪声 0")
                             .arg(c->screenFont)
                             .arg(ratioOf(d), 0, 'f', 4)
                             .arg(kEffectMinRatio));
        else
            c->markNa(QStringLiteral("HP-09 帧无效"));
        c->facade->setScreenFontSize(c->screenFont);   // 还原（判别对照放下一步）
    }});

    // ── S19：HP-09 判别对照（还原 ⇒ 帧回噪声级）────────────────────────────
    steps->append({700, [](Ctx *c) {
        const FrameSample back = framecmp::grabUpstream(c->mailbox);
        const DiffStats d = framecmp::diffOf(c->refFrame, back);
        c->note(QStringLiteral("HP-09 还原后帧：%1").arg(framecmp::frameText(back)));
        c->note(QStringLiteral("vs 参考帧（判别对照）：%1").arg(framecmp::diffText(d)));
        // 判别对照：还原后回噪声级 ⇒ 效应确实由字号引起（可逆）。不红：只是 note。
        c->note(belowNoise(d)
                    ? QStringLiteral("  ⇒ 还原后回噪声级 ⇒ 效应可逆，判别对照成立")
                    : QStringLiteral("  🔴 还原后未回噪声级 ⇒ 效应另有来源，HP-09 的读数存疑"));
    }});

    // ── S20：HP-10 诊断数据面健康（bytesPerFrame 独立验算）─────────────────
    steps->append({300, [](Ctx *c) {
        const FrameMailbox::Stats st = c->mailbox->stats();
        const FrameSample probe = framecmp::grabUpstream(c->mailbox);
        const qint64 expect = probe.valid
                                  ? qint64(probe.image.width()) * probe.image.height() * 4
                                  : -1;
        const bool ok = st.published > 0 && st.dropped <= st.published
                        && st.completeSlots >= 0 && st.completeSlots <= FrameMailbox::kFrameSlotCount
                        && st.readersHeld >= 0
                        && (!probe.valid || st.bytesPerFrame == expect);
        c->mark(ok, QStringLiteral("HP-10 诊断数据面健康：published=%1 dropped=%2 "
                                   "completeSlots=%3/%4 readersHeld=%5 bytesPerFrame=%6 "
                                   "vs 独立验算 宽%7×高%8×4=%9")
                         .arg(st.published)
                         .arg(st.dropped)
                         .arg(st.completeSlots)
                         .arg(FrameMailbox::kFrameSlotCount)
                         .arg(st.readersHeld)
                         .arg(st.bytesPerFrame)
                         .arg(probe.valid ? probe.image.width() : -1)
                         .arg(probe.valid ? probe.image.height() : -1)
                         .arg(expect));
    }});

    // ── S21：HP-11 复原核对 + 收尾 ─────────────────────────────────────────
    steps->append({0, [](Ctx *c) {
        c->restoreAll();
        const int f = c->facade->screenFontSize();
        const int g = c->facade->guiFontSize();
        const double b = c->facade->screenButtonScale();
        const bool ok = f == c->screenFont && g == c->guiFont
                        && qFuzzyCompare(b, c->buttonScale);
        c->mark(ok, QStringLiteral("HP-11 复原：screenFontSize=%1（初值 %2）guiFontSize=%3（%4）"
                                   "screenButtonScale=%5（%6）｜仿真已恢复 %7")
                         .arg(f)
                         .arg(c->screenFont)
                         .arg(g)
                         .arg(c->guiFont)
                         .arg(b)
                         .arg(c->buttonScale)
                         .arg(onOff(!c->facade->simulationPaused())));
        c->finish();
    }});

    // 步骤调度器（照抄 DisplayCheck）。
    *tick = [ctx, steps, idx, tick]() {
        if (*idx >= steps->size())
        {
            ctx->finish();
            return;
        }
        Step &s = (*steps)[*idx];
        ++(*idx);
        s.body(ctx);
        QTimer::singleShot(s.delayAfter, ctx->app, *tick);
    };
    QTimer::singleShot(delayMs, app, *tick);
#endif
}

} // namespace stelapp
