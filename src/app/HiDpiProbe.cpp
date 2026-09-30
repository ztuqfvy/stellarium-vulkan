/*
 * HiDpiProbe — 实现（T39-A 高 DPI + 渲染诊断数据面探针）。设计与八个问题见头注。
 *
 * 驱动方式沿用 DisplayProbe / LocationProbe / ToolbarProbe 的线性步骤表：
 * 每步 = (跑完后等多少 ms, 步骤体)。
 * ⚠️ delayAfter 语义 = "跑完**本步**之后的等待"（T33 陷阱 41 / T37 陷阱 69 / T38 首轮）：
 * 写 0 时下一步与本步只隔一个事件循环；**凡"写后要读"的配对，读一律放下一步**。
 */
#include "app/HiDpiProbe.hpp"

#include "app/AppFacade.hpp"
#include "app/FrameCompare.hpp"
#include "render/legacy/FrameMailbox.hpp"

#include <QCoreApplication>
#include <QFont>
#include <QGuiApplication>
#include <QQuickItem>
#include <QQuickWindow>
#include <QScreen>
#include <QSettings>
#include <QTimer>
#include <QVector>

#include <algorithm>

#if defined(STELQUICK_HAS_ENGINE)
#include "StelApp.hpp"
#endif

namespace stelapp {

namespace {

using ProbeResult = HiDpiProbe::Result;

#if defined(STELQUICK_HAS_ENGINE) && defined(STELQUICK_WIDGETS_HOST)

//! 视觉树里一项的字体实况（Q4：改 guiFontSize 前后逐项配对）。
//! ⚠️ `pixelSize` 与 `pointSize` 在 QFont 里**互斥**（设其一另一个变 -1）——
//! 只读 pixelSize 的话，用 pointSize 描述字号的项**测不出任何变化**，
//! 会得出"不跟随"的**假结论**（首轮探针就踩了这个：见 Q4 的仪器正控）。
struct FontSlot
{
    QString cls;      //!< QML 类型名（配对用；两次扫描树不变 ⇒ 顺序一致）
    int pixelSize = 0;
    int pointSize = 0;
    QString family;
    QString qn;       //!< objectName（有则记，便于人读）
    bool differsFrom(const FontSlot &o) const
    {
        return pixelSize != o.pixelSize || pointSize != o.pointSize || family != o.family;
    }
    QString desc() const
    {
        return QStringLiteral("px=%1/pt=%2/%3").arg(pixelSize).arg(pointSize).arg(family);
    }
};

//! 递归收集视觉树里所有带 `font` 属性的项（陷阱 45：Repeater delegate 的 QObject
//! 父链是空的 ⇒ 只能走视觉树）。
void collectFonts(QQuickItem *item, QVector<FontSlot> &out)
{
    if (!item)
        return;
    const QVariant f = item->property("font");
    if (f.isValid() && f.canConvert<QFont>())
    {
        const QFont font = f.value<QFont>();
        FontSlot s;
        s.cls = QString::fromLatin1(item->metaObject()->className());
        s.pixelSize = font.pixelSize();
        s.pointSize = font.pointSize();
        s.family = font.family();
        const QVariant qn = item->property("objectName");
        s.qn = qn.toString();
        out.append(s);
    }
    const QList<QQuickItem *> kids = item->childItems();
    for (QQuickItem *k : kids)
        collectFonts(k, out);
}

//! 两次扫描的配对对比：返回 (跟随数, 未跟随数, 明细行)。
QString fontDiffText(const QVector<FontSlot> &before, const QVector<FontSlot> &after,
                     int &followed, int &fixed)
{
    followed = 0;
    fixed = 0;
    QStringList changes;
    const int n = qMin(before.size(), after.size());
    for (int i = 0; i < n; ++i)
    {
        if (!before[i].differsFrom(after[i]))
            ++fixed;
        else
        {
            ++followed;
            if (changes.size() < 6)
                changes << QStringLiteral("%1(%2) %3→%4")
                               .arg(after[i].cls,
                                    after[i].qn.isEmpty() ? QStringLiteral("-") : after[i].qn)
                               .arg(before[i].desc())
                               .arg(after[i].desc());
        }
    }
    return QStringLiteral("跟随 %1 / 不动 %2（共配对 %3）%4")
        .arg(followed)
        .arg(fixed)
        .arg(n)
        .arg(changes.isEmpty() ? QString()
                               : QStringLiteral("；样本：") + changes.join(QStringLiteral("、")));
}

//! 视觉树里字号描述的分布摘要。三类分开数（这决定"跟随"的语义）：
//!   · 显式 pixelSize（>0）—— QFont 里 pixelSize 生效
//!   · 显式 pointSize（>0）且 pixelSize<0 —— 用点值描述，改像素字号**原则上不该跟随**
//!   · 两者都 <0 —— 无字号（继承/默认）
QString fontDistText(const QVector<FontSlot> &v)
{
    if (v.isEmpty())
        return QStringLiteral("（无带 font 的项）");
    int px = 0, pt = 0, none = 0;
    QVector<int> pxSizes;
    QVector<int> ptSizes;
    for (const FontSlot &s : v)
    {
        if (s.pixelSize > 0) { ++px; pxSizes.append(s.pixelSize); }
        else if (s.pointSize > 0) { ++pt; ptSizes.append(s.pointSize); }
        else ++none;
    }
    auto range = [](QVector<int> &a) {
        if (a.isEmpty())
            return QStringLiteral("∅");
        std::sort(a.begin(), a.end());
        return QStringLiteral("%1~%2").arg(a.first()).arg(a.last());
    };
    return QStringLiteral("n=%1｜显式 pixelSize %2 项(%3)｜显式 pointSize %4 项(%5)｜无字号 %6 项")
        .arg(v.size())
        .arg(px)
        .arg(range(pxSizes))
        .arg(pt)
        .arg(range(ptSizes))
        .arg(none);
}

//! 找视觉树里第一个"显式设了像素字号"的项（仪器正控用）。
QQuickItem *firstPixelFontItem(QQuickItem *item)
{
    if (!item)
        return nullptr;
    const QVariant f = item->property("font");
    if (f.isValid() && f.canConvert<QFont>() && f.value<QFont>().pixelSize() > 0)
        return item;
    const QList<QQuickItem *> kids = item->childItems();
    for (QQuickItem *k : kids)
        if (QQuickItem *hit = firstPixelFontItem(k))
            return hit;
    return nullptr;
}

struct Ctx
{
    QCoreApplication *app = nullptr;
    AppFacade *facade = nullptr;
    QQuickWindow *window = nullptr;
    FrameMailbox *mailbox = nullptr;
    std::function<void(const ProbeResult &)> onDone;
    ProbeResult result;
    QStringList lines;

    //! 初值台账（收尾还原）。
    int screenFont = 0;
    int guiFont = 0;
    double buttonScale = 0.0;
    QFont origFont;

    //! 布场还原（Q5 的像素比较要求帧静定）。
    bool wasPaused = false;
    bool pausedByUs = false;

    //! Q4 的前置扫描结果。
    QVector<FontSlot> fontsBefore;
    QVector<FontSlot> fontsAfter;

    //! Q5 的参考帧与噪声底。
    framecmp::FrameSample noiseA;
    framecmp::FrameSample refFrame;

    //! Q6 静置期的 NOTIFY 计数。
    int screenFontNotify = 0;
    int guiFontNotify = 0;
    int buttonScaleNotify = 0;
    int idleBaseScreen = 0;
    int idleBaseGui = 0;
    int idleBaseButton = 0;
    QVector<QMetaObject::Connection> conns;

    void note(const QString &line) { lines.append(line); }

    void finish()
    {
        for (const QMetaObject::Connection &c : conns)
            QObject::disconnect(c);
        result.ran = true;
        result.summary = QStringLiteral(
            "高 DPI + 渲染诊断数据面探针：只报读数、不下结论（断言见 T39-C 的 HiDpiCheck）");
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

#endif  // STELQUICK_HAS_ENGINE && STELQUICK_WIDGETS_HOST

} // namespace

void HiDpiProbe::run(QCoreApplication *app,
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
    r.summary = QStringLiteral("高 DPI 探针需要合流形态构建");
    QTimer::singleShot(0, app, [onDone, r]() { onDone(r); });
#else
    auto *ctx = new Ctx;
    ctx->app = app;
    ctx->facade = facade;
    ctx->window = window;
    ctx->mailbox = mailbox;
    ctx->onDone = onDone;

    StelApp &sa = StelApp::getInstance();
    QQuickItem *root = window ? window->contentItem() : nullptr;

    auto steps = std::make_shared<QVector<Step>>();
    auto idx = std::make_shared<int>(0);
    auto tick = std::make_shared<std::function<void()>>();

    // ── S1：初值台账 + Q1(前半) + Q2 算术验算 + Q8 config 来源 ─────────────
    steps->append({250, [&sa](Ctx *c) {
        // Q6 观测从起跑就挂（3 参连接：Ctx 不是 QObject；finish() 里统一断开）。
        c->conns << QObject::connect(&sa, &StelApp::screenFontSizeChanged, &sa,
                                      [c](int) { ++c->screenFontNotify; });
        c->conns << QObject::connect(&sa, &StelApp::guiFontSizeChanged, &sa,
                                      [c](int) { ++c->guiFontNotify; });
        c->conns << QObject::connect(&sa, &StelApp::screenButtonScaleChanged, &sa,
                                      [c](double) { ++c->buttonScaleNotify; });

        c->screenFont = sa.getScreenFontSize();
        c->guiFont = sa.getGuiFontSize();
        c->buttonScale = sa.getScreenButtonScale();
        c->origFont = QGuiApplication::font();

        c->note(QStringLiteral("── Q1/Q2 初值台账（高 DPI 三属性 + 派生量）──"));
        c->note(QStringLiteral("screenFontSize     = %1（int，天空文本字号）").arg(c->screenFont));
        c->note(QStringLiteral("guiFontSize        = %1（int，GUI 面板字号）").arg(c->guiFont));
        c->note(QStringLiteral("screenButtonScale  = %1（double，按钮尺寸百分比）").arg(c->buttonScale));

        const double dpp = sa.getDevicePixelsPerPixel();
        const double sRatio = sa.screenFontSizeRatio();
        const double gRatio = sa.guiFontSizeRatio();
        const double sScale = sa.getScreenScale();
        const double gScale = sa.getGuiScale();
        c->note(QStringLiteral("devicePixelsPerPixel = %1").arg(dpp));
        c->note(QStringLiteral("screenFontSizeRatio  = %1｜guiFontSizeRatio = %2（分母=默认字号 %3）")
                    .arg(sRatio)
                    .arg(gRatio)
                    .arg(StelApp::getDefaultGuiFontSize()));
        c->note(QStringLiteral("getScreenScale() = %1｜getGuiScale() = %2（引擎读数）")
                    .arg(sScale)
                    .arg(gScale));
        // 陷阱 43：不复述自己的读法 —— 用 dpp×ratio 独立算一遍对比。
        c->note(QStringLiteral("  独立验算 dpp×screenRatio = %1（与引擎读数差 %2）｜"
                               "dpp×guiRatio = %3（差 %4）")
                    .arg(dpp * sRatio, 0, 'f', 6)
                    .arg(qAbs(dpp * sRatio - sScale), 0, 'f', 6)
                    .arg(dpp * gRatio, 0, 'f', 6)
                    .arg(qAbs(dpp * gRatio - gScale), 0, 'f', 6));
        c->note(QStringLiteral("窗口 devicePixelRatio = %1（合流宿主是 WA_DontShowOnScreen）")
                    .arg(c->window ? c->window->devicePixelRatio() : -1.0));
        if (c->window && c->window->screen())
            c->note(QStringLiteral("主屏 DPR = %1｜逻辑尺寸 %2×%3")
                        .arg(c->window->screen()->devicePixelRatio())
                        .arg(c->window->width())
                        .arg(c->window->height()));

        // Q8：初值的来源（陷阱 44 同族 —— 判据若假设"初值=默认 13"，只在用户 config
        // 页面上成立/不成立，跨平台复验才逼得出）。
        if (QSettings *st = sa.getSettings())
        {
            c->note(QStringLiteral("── Q8 config 来源（陷阱 44 同族）──"));
            c->note(QStringLiteral("settings file    = %1").arg(st->fileName()));
            c->note(QStringLiteral("gui/screen_font_size   = %1（默认 13）")
                        .arg(st->value(QStringLiteral("gui/screen_font_size"), QStringLiteral("<未落盘>")).toString()));
            c->note(QStringLiteral("gui/gui_font_size      = %1（默认 13）")
                        .arg(st->value(QStringLiteral("gui/gui_font_size"), QStringLiteral("<未落盘>")).toString()));
            c->note(QStringLiteral("gui/screen_button_scale= %1（默认 100）")
                        .arg(st->value(QStringLiteral("gui/screen_button_scale"), QStringLiteral("<未落盘>")).toString()));
        }

        // 冻结仿真（帧静定 —— Q5 的像素比较必须有判别力）。
        // 🔴 首轮探针漏了这一步：仿真以 0.1 天/秒在跑，参考帧 180 → 测试帧 237 之间
        //    天空自己就变了 ⇒ Q5 读数"差异 99.99%"完全是**时间流逝**造成的，
        //    与 screenFontSize 无关（零判别力的经典形态）。
        if (c->facade)
        {
            c->wasPaused = c->facade->simulationPaused();
            if (!c->wasPaused)
            {
                c->facade->setSimulationPaused(true);
                c->pausedByUs = true;
            }
        }
        c->note(QStringLiteral("布场：仿真冻结 %1（Q5 的像素比较前提）")
                    .arg(c->pausedByUs ? QStringLiteral("已由探针置停")
                                       : (c->wasPaused ? QStringLiteral("本来就在停") 
                                                       : QStringLiteral("未置停"))));
    }});

    // ── S2：Q1 写三个"远"值（99 / 99 / 999）—— setter 若夹取，回读会露馅 ─────
    steps->append({300, [&sa](Ctx *c) {
        c->note(QStringLiteral("── Q1 写读往返 + 夹取（写入值刻意取「远」值：99 / 99 / 999）──"));
        sa.setScreenFontSize(99);
        sa.setGuiFontSize(99);
        sa.setScreenButtonScale(999.0);
        c->note(QStringLiteral("已写 screenFontSize=99 / guiFontSize=99 / screenButtonScale=999"
                               "（回读放下一步）"));
    }});

    // ── S3：Q1 回读（是否夹取）+ Q3 绕过 setter 直接改全局字体 ─────────────
    steps->append({300, [&sa](Ctx *c) {
        const int rs = sa.getScreenFontSize();
        const int rg = sa.getGuiFontSize();
        const double rb = sa.getScreenButtonScale();
        c->note(QStringLiteral("回读 screenFontSize = %1 ⇒ %2")
                    .arg(rs)
                    .arg(rs == 99 ? QStringLiteral("**原样**（setter 不夹取）")
                                  : QStringLiteral("被夹到 %1").arg(rs)));
        c->note(QStringLiteral("回读 guiFontSize    = %1 ⇒ %2")
                    .arg(rg)
                    .arg(rg == 99 ? QStringLiteral("**原样**（setter 不夹取）")
                                  : QStringLiteral("被夹到 %1").arg(rg)));
        c->note(QStringLiteral("回读 screenButtonScale = %1 ⇒ %2")
                    .arg(rb)
                    .arg(qFuzzyCompare(rb, 999.0) ? QStringLiteral("**原样**（setter 不夹取）")
                                                  : QStringLiteral("被夹到 %1").arg(rb)));

        // 先把三属性还原（后面 Q3/Q4 要在干净初值上做）。
        sa.setScreenFontSize(c->screenFont);
        sa.setScreenButtonScale(c->buttonScale);
        c->note(QStringLiteral("三属性已还原（guiFontSize 留在 99，下一步 Q3 用它做对照）"));

        // Q3：绕过 setter，直接改 QGuiApplication 字体。
        c->note(QStringLiteral("── Q3 getGuiFontSize() 的来源（绕过 setter 直改全局字体）──"));
        c->note(QStringLiteral("改前：getGuiFontSize()=%1｜QGuiApplication::font().pixelSize()=%2")
                    .arg(sa.getGuiFontSize())
                    .arg(QGuiApplication::font().pixelSize()));
        QFont f = QGuiApplication::font();
        f.setPixelSize(21);
        QGuiApplication::setFont(f);
        c->note(QStringLiteral("已绕过 setter 直改全局字体 pixelSize=21（回读放下一步）"));
    }});

    // ── S4：Q3 判读 + Q5 前的 QML 字体分布（前扫）+ Q4 前扫 ────────────────
    steps->append({300, [root, &sa](Ctx *c) {
        const int g = sa.getGuiFontSize();
        c->note(QStringLiteral("改后：getGuiFontSize()=%1｜QGuiApplication::font().pixelSize()=%2 ⇒ %3")
                    .arg(g)
                    .arg(QGuiApplication::font().pixelSize())
                    .arg(g == 21 ? QStringLiteral("🔴 **它读的就是全局字体**（不是内部缓存）⇒ "
                                                  "任何绕过 setter 的改字都会污染「用户设定值」")
                                 : QStringLiteral("未被污染（读的是内部缓存）")));

        // 还原全局字体（Q3 的观测已拿到），并建立 Q4 的**基准扫描**。
        QGuiApplication::setFont(c->origFont);
        c->note(QStringLiteral("全局字体已还原为 pixelSize=%1").arg(QGuiApplication::font().pixelSize()));
        collectFonts(root, c->fontsBefore);
        c->note(QStringLiteral("Q4 基准扫描（前）：%1").arg(fontDistText(c->fontsBefore)));

        // ── Q4 仪器正控（"方法测不出东西"的反面证据）───────────────────────
        // 这一段**不是**测产品，是测**尺子**：若连"人为改一处字号"都查不出来，
        // 下面那句"325 项一个都不跟随"就毫无意义（T38 首轮 DP-10 的同族教训）。
        {
            // 正控一：读取链路 —— 栈上造一个项，分别用 pixelSize / pointSize 描述。
            QQuickItem stackItem;
            QFont fpx;
            fpx.setPixelSize(33);
            stackItem.setProperty("font", QVariant::fromValue(fpx));
            QVector<FontSlot> t1;
            collectFonts(&stackItem, t1);
            bool sawPx = false;
            for (const FontSlot &s : t1)
                if (s.pixelSize == 33)
                    sawPx = true;
            QFont fpt;
            fpt.setPointSize(37);
            stackItem.setProperty("font", QVariant::fromValue(fpt));
            QVector<FontSlot> t2;
            collectFonts(&stackItem, t2);
            bool sawPt = false;
            for (const FontSlot &s : t2)
                if (s.pointSize == 37)
                    sawPt = true;
            c->note(QStringLiteral("Q4 仪器正控①（读取链路）：显式 pixelSize=33 %1｜显式 pointSize=37 %2")
                        .arg(sawPx ? QStringLiteral("能读到") : QStringLiteral("🔴读不到"))
                        .arg(sawPt ? QStringLiteral("能读到") : QStringLiteral("🔴读不到")));

            // 正控二：**真实树**里人为改一项的字号，看配对对比能否查出恰好 1 项变化。
            QQuickItem *victim = firstPixelFontItem(root);
            if (victim)
            {
                const QFont orig = victim->property("font").value<QFont>();
                QVector<FontSlot> before;
                collectFonts(root, before);
                QFont mutated = orig;
                mutated.setPixelSize(41);
                victim->setProperty("font", QVariant::fromValue(mutated));
                QVector<FontSlot> after;
                collectFonts(root, after);
                int fu = 0;
                int fx = 0;
                const QString d = fontDiffText(before, after, fu, fx);
                c->note(QStringLiteral("Q4 仪器正控②（真实树人为扰动 %1）：%2")
                            .arg(QString::fromLatin1(victim->metaObject()->className()))
                            .arg(d));
                c->note(QStringLiteral("  ⇒ %1")
                            .arg(fu == 1 ? QStringLiteral("恰好 1 项被查出 ⇒ 配对对比有效")
                                         : QStringLiteral("查出 %1 项（预期 1）⇒ 配对口径需复核").arg(fu)));
                victim->setProperty("font", QVariant::fromValue(orig));
            }
            else
            {
                c->note(QStringLiteral("Q4 仪器正控②：树里找不到显式像素字号的项 ⇒ 正控②无法执行"));
            }
            // 正控③：把 app font 改成**点值**描述 —— 基准扫描里 257 项是 pointSize 描述的，
            // 只改 pixelSize 的话它们**本就不该跟随**（QFont 里两者互斥）⇒ 必须反过来验一次，
            // 否则"一个都不跟随"的结论**不完备**（T38 DP-10 的同族：自造伪命题）。
            {
                QFont pf = c->origFont;
                pf.setPointSize(25);
                QGuiApplication::setFont(pf);
                QVector<FontSlot> afterPt;
                collectFonts(root, afterPt);
                int fu = 0;
                int fx = 0;
                const QString d = fontDiffText(c->fontsBefore, afterPt, fu, fx);
                c->note(QStringLiteral("Q4 正控③（app font 改 pointSize=25）：%1").arg(d));
                QGuiApplication::setFont(c->origFont);
                c->note(QStringLiteral("  ⇒ %1")
                            .arg(fu == 0
                                     ? QStringLiteral("点值路径同样**一项都不跟随** ⇒ 结论完备")
                                     : QStringLiteral("点值路径有 %1 项跟随 ⇒ 结论要按字号的"
                                                      "描述方式分开说")
                                           .arg(fu)));
            }
        }

        // Q4 正题：用**正经 setter** 改 guiFontSize。
        c->note(QStringLiteral("── Q4 setGuiFontSize() 的波及面（正经 setter）──"));
        c->note(QStringLiteral("改前：guiFontSize=%1｜app font pixelSize=%2")
                    .arg(sa.getGuiFontSize())
                    .arg(QGuiApplication::font().pixelSize()));
        sa.setGuiFontSize(25);
        c->note(QStringLiteral("已 setGuiFontSize(25)（后扫放下一步）"));
    }});

    // ── S5：Q4 后扫（逐项配对）+ 还原 guiFontSize ──────────────────────────
    steps->append({300, [root, &sa](Ctx *c) {
        c->note(QStringLiteral("改后：guiFontSize=%1｜app font pixelSize=%2")
                    .arg(sa.getGuiFontSize())
                    .arg(QGuiApplication::font().pixelSize()));
        collectFonts(root, c->fontsAfter);
        int followed = 0;
        int fixed = 0;
        const QString d = fontDiffText(c->fontsBefore, c->fontsAfter, followed, fixed);
        c->note(QStringLiteral("Q4 配对对比：%1").arg(d));
        c->note(QStringLiteral("  ⇒ %1")
                    .arg(followed == 0
                             ? QStringLiteral("🔴 **QML 侧一个都不跟随**：引擎 guiFontSize 只作用于"
                                              "老 QWidget 对话框；QML UI 自有字号 ⇒ 高 DPI 的 QML "
                                              "缩放必须由**产品侧**自己做系数")
                             : QStringLiteral("有 %1 项跟随（说明部分 QML 项未显式设字号）")
                                   .arg(followed)));

        sa.setGuiFontSize(c->guiFont);
        c->note(QStringLiteral("guiFontSize 已还原为 %1（app font pixelSize=%2）")
                    .arg(sa.getGuiFontSize())
                    .arg(QGuiApplication::font().pixelSize()));
    }});

    // ── S6a：Q5 噪声底（冻结下同状态连抓两帧 —— **没有参照物就不知道 2% 算大算小**）──
    steps->append({450, [](Ctx *c) {
        c->note(QStringLiteral("── Q5 screenFontSize 的帧级效应 ──"));
        c->noiseA = framecmp::grabUpstream(c->mailbox);
        c->note(QStringLiteral("噪声底帧①（冻结、未动任何参数）：%1")
                    .arg(framecmp::frameText(c->noiseA)));
    }});

    // ── S6b：噪声底读数 + 参考帧（此刻三属性均已回初值）─────────────────────
    steps->append({450, [](Ctx *c) {
        const framecmp::FrameSample b = framecmp::grabUpstream(c->mailbox);
        c->note(QStringLiteral("噪声底帧②：%1").arg(framecmp::frameText(b)));
        c->note(QStringLiteral("噪声底（①②同状态）：%1")
                    .arg(framecmp::diffText(framecmp::diffOf(c->noiseA, b))));
        c->refFrame = b;
        c->note(QStringLiteral("参考帧（初值，字号 %1）：%2")
                    .arg(c->screenFont)
                    .arg(framecmp::frameText(c->refFrame)));
    }});

    // ── S7：Q5 写 screenFontSize=40（步进动作面之外的大幅值）───────────────
    steps->append({700, [&sa](Ctx *c) {
        sa.setScreenFontSize(40);
        c->note(QStringLiteral("已 setScreenFontSize(40)（抓帧放下一步 —— T38 血泪：等待必须挂在"
                               "**写入步**，否则读到上一帧缓冲）"));
    }});

    // ── S8：Q5 抓帧 + 比 + 还原 ────────────────────────────────────────────
    steps->append({300, [&sa](Ctx *c) {
        const framecmp::FrameSample after = framecmp::grabUpstream(c->mailbox);
        const framecmp::DiffStats d = framecmp::diffOf(c->refFrame, after);
        c->note(QStringLiteral("字号 40 帧：%1").arg(framecmp::frameText(after)));
        c->note(QStringLiteral("vs 参考帧：%1").arg(framecmp::diffText(d)));
        c->note(QStringLiteral("  ⇒ 天空文本字号 %1→40 的上游帧差异见上（量级由 T39-C 标定门槛）")
                    .arg(c->screenFont));
        sa.setScreenFontSize(c->screenFont);
        c->note(QStringLiteral("screenFontSize 已还原为 %1").arg(sa.getScreenFontSize()));
    }});

    // ── S8b：Q5 判别对照（还原字号 ⇒ 帧应回噪声级；否则"效应"另有来源）──────
    steps->append({700, [](Ctx *c) {
        const framecmp::FrameSample back = framecmp::grabUpstream(c->mailbox);
        c->note(QStringLiteral("还原字号后的帧：%1").arg(framecmp::frameText(back)));
        c->note(QStringLiteral("vs 参考帧（判别对照 —— 还原应回到噪声级）：%1")
                    .arg(framecmp::diffText(framecmp::diffOf(c->refFrame, back))));
    }});

    // ── S9：Q7 渲染诊断数据面（FrameMailbox::Stats）─────────────────────────
    steps->append({250, [](Ctx *c) {
        c->note(QStringLiteral("── Q7 渲染诊断数据面（FrameMailbox::Stats，任意线程可读）──"));
        const FrameMailbox::Stats st = c->mailbox->stats();
        c->note(QStringLiteral("latestCompletedFrameNumber = %1")
                    .arg(c->mailbox->latestCompletedFrameNumber()));
        c->note(QStringLiteral("published=%1｜dropped=%2｜leased=%3")
                    .arg(st.published)
                    .arg(st.dropped)
                    .arg(st.leased));
        c->note(QStringLiteral("sizeGeneration=%1｜completeSlots=%2 / 槽位 %3｜readersHeld=%4")
                    .arg(st.sizeGeneration)
                    .arg(st.completeSlots)
                    .arg(FrameMailbox::kFrameSlotCount)
                    .arg(st.readersHeld));
        c->note(QStringLiteral("latestFrameAgeMs=%1（-1 = 无完整帧）｜bytesPerFrame=%2")
                    .arg(st.latestFrameAgeMs)
                    .arg(st.bytesPerFrame));
    }});

    // ── S10：Q6 静置期基线 ─────────────────────────────────────────────────
    steps->append({1500, [](Ctx *c) {
        c->idleBaseScreen = c->screenFontNotify;
        c->idleBaseGui = c->guiFontNotify;
        c->idleBaseButton = c->buttonScaleNotify;
        c->note(QStringLiteral("── Q6 NOTIFY 静置基线（此刻起静置 1.5s）──"));
        c->note(QStringLiteral("基线计数：screen=%1｜gui=%2｜button=%3")
                    .arg(c->idleBaseScreen)
                    .arg(c->idleBaseGui)
                    .arg(c->idleBaseButton));
    }});

    // ── S11：Q6 静置增量 + 收尾核对 ────────────────────────────────────────
    steps->append({0, [&sa](Ctx *c) {
        c->note(QStringLiteral("静置 1.5s 增量：screen=%1｜gui=%2｜button=%3"
                               "（决定 QML 能否安全绑定）")
                    .arg(c->screenFontNotify - c->idleBaseScreen)
                    .arg(c->guiFontNotify - c->idleBaseGui)
                    .arg(c->buttonScaleNotify - c->idleBaseButton));

        c->note(QStringLiteral("── 收尾还原核对 ──"));
        if (c->pausedByUs && c->facade)
        {
            c->facade->setSimulationPaused(false);
            c->note(QStringLiteral("仿真已恢复运行（探针置停的）"));
        }
        c->note(QStringLiteral("screenFontSize=%1（初值 %2）｜guiFontSize=%3（初值 %4）"
                               "｜screenButtonScale=%5（初值 %6）")
                    .arg(sa.getScreenFontSize())
                    .arg(c->screenFont)
                    .arg(sa.getGuiFontSize())
                    .arg(c->guiFont)
                    .arg(sa.getScreenButtonScale())
                    .arg(c->buttonScale));
        c->finish();
    }});

    // 步骤调度器（照抄 DisplayCheck）：每步跑完等 delayAfter 再进下一步。
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
