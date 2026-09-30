// NightModeProbe 实现（T37-A）。动机、审计链、问题清单见头注。
//
// T37 提炼：帧采样/比较的 helper 已上移到 app/FrameCompare.hpp —— 探针与判据
// （NightModeCheck）必须用**同一把尺子**，否则口径漂移（探针 0 差异、判据 3 像素
// 这类鸡毛蒜皮会浪费一轮排查）。
//
// 驱动方式沿用 TimeLinkProbe/ToolbarCheck 的线性步骤表；`delayAfter` 语义 =
// "**跑完本步之后**等"（T33 陷阱 41）——本套探针里这个语义是承重的：冻结之后
// 让帧泵自由出帧的那几百毫秒，就是"效果有没有机会生效"的窗口。
//
// ⚠️ 本文件里的读数一律用 **Qt 的 `%1` 占位符**，不是 printf 的 `%.3f`
// （TimeLinkProbe 头注记过：忘了替换时 `.arg()` 会原样返回，日志里留一串 `%.3f`，
// 这是"仪器会撒谎"的另一副面孔 —— 不报错，只是把没替换的东西打出来）。
#include "app/NightModeProbe.hpp"

#include "app/ActionRouter.hpp"
#include "app/AppFacade.hpp"
#include "app/FrameCompare.hpp"
#include "render/legacy/FrameMailbox.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QImage>
#include <QQuickWindow>
#include <QTimer>
#include <QVector>
#include <memory>

#if defined(STELQUICK_HAS_ENGINE) && defined(STELQUICK_WIDGETS_HOST)
#include "StelApp.hpp"
#include "StelMainView.hpp"
#endif

namespace stelapp {

namespace {

#if defined(STELQUICK_HAS_ENGINE) && defined(STELQUICK_WIDGETS_HOST)

using namespace stelapp::framecmp;
using ProbeResult = NightModeProbe::Result;

//! 探针要报的两个开关（夜视 = 被测对象；星座线 = 判别对照量）。
constexpr const char *kNightAction = "actionShow_Night_Mode";
// 判别对照动作可覆盖（T37-X2 判别实验用，与 NightModeCheck 同名变量同语义）。
constexpr const char *kControlActionDefault = "actionShow_Constellation_Lines";

//! 把一对帧 + 差异高亮图存盘（人眼核对用）。
//! `STELQUICK_PROBE_DUMP_DIR` 未设则跳过；差异像素染洋红、非差异压暗成 1/3，
//! 视口矩形画青框 —— 一眼看出"差异在视口内还是视口外（QML UI）"。
void dumpPair(const QString &dir, const QString &tag,
              const FrameSample &a, const FrameSample &b, const QRect &markViewport)
{
    if (dir.isEmpty())
        return;
    QDir().mkpath(dir);
    if (a.valid)
        a.image.save(QStringLiteral("%1/%2-a.png").arg(dir, tag));
    if (b.valid)
        b.image.save(QStringLiteral("%1/%2-b.png").arg(dir, tag));
    if (!a.valid || !b.valid || a.image.size() != b.image.size() || a.image.isNull())
        return;

    QImage vis = b.image.copy();
    const int w = vis.width();
    const int ht = vis.height();
    for (int y = 0; y < ht; ++y)
    {
        uchar *row = vis.scanLine(y);
        const uchar *ra = a.image.constScanLine(y);
        const uchar *rb = b.image.constScanLine(y);
        for (int x = 0; x < w; ++x)
        {
            const uchar *pa = ra + x * 4;
            const uchar *pb = rb + x * 4;
            if (pa[0] != pb[0] || pa[1] != pb[1] || pa[2] != pb[2])
            {
                row[x * 4 + 0] = 255;   // 洋红 = 差异
                row[x * 4 + 1] = 0;
                row[x * 4 + 2] = 255;
                row[x * 4 + 3] = 255;
            }
            else
            {
                row[x * 4 + 0] = uchar(pb[0] / 3);
                row[x * 4 + 1] = uchar(pb[1] / 3);
                row[x * 4 + 2] = uchar(pb[2] / 3);
                row[x * 4 + 3] = 255;
            }
        }
    }
    if (markViewport.isValid())
    {
        const int ys[2] = {markViewport.top(), markViewport.bottom()};
        const int xs[2] = {markViewport.left(), markViewport.right()};
        for (int k = 0; k < 2; ++k)
        {
            if (ys[k] < 0 || ys[k] >= ht)
                continue;
            uchar *row = vis.scanLine(ys[k]);
            for (int x = qMax(0, markViewport.left());
                 x <= qMin(w - 1, markViewport.right()); ++x)
            {
                row[x * 4 + 0] = 0; row[x * 4 + 1] = 255; row[x * 4 + 2] = 255;
            }
        }
        for (int y = qMax(0, markViewport.top());
             y <= qMin(ht - 1, markViewport.bottom()); ++y)
        {
            uchar *row = vis.scanLine(y);
            for (int k = 0; k < 2; ++k)
            {
                if (xs[k] < 0 || xs[k] >= w)
                    continue;
                row[xs[k] * 4 + 0] = 0; row[xs[k] * 4 + 1] = 255; row[xs[k] * 4 + 2] = 255;
            }
        }
    }
    vis.save(QStringLiteral("%1/%2-diff.png").arg(dir, tag));
}

//! 下游差异的"双面"读数：全图 + 视口内（两者之差即 QML UI 的贡献）。
QString downstreamDualText(const FrameSample &a, const FrameSample &b, const QRect &roi)
{
    QString out = QStringLiteral("全图 %1").arg(diffText(diffOf(a, b)));
    if (roi.isValid())
    {
        const DiffStats inVp = diffOf(a, b, roi);
        out += QStringLiteral("  ⟂ 其中视口内（%1,%2)-(%3,%4) %5")
                   .arg(roi.left()).arg(roi.top()).arg(roi.right()).arg(roi.bottom())
                   .arg(diffText(inVp));
    }
    else
    {
        out += QStringLiteral("  ⟂ 视口矩形不可用（未按 objectName 找到 skyViewport）");
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
    std::function<void(const ProbeResult &)> onDone;
    ProbeResult result;
    QStringList lines;

    QRect viewportRoi;             //!< 天空视口在抓帧坐标系里的矩形（下游用）
    QString controlAction = QString::fromLatin1(kControlActionDefault);

    // ── 采样缓存 ────────────────────────────────────────────────────────
    FrameSample refUpA, refUpB;     // Q3 噪声底（上游，同一状态）
    FrameSample refDnA, refDnB;     // Q3 噪声底（下游，同一状态）
    FrameSample nightOnUp, nightOnDn;   // Q4/Q5：夜视 ON
    FrameSample ctlOnUp, ctlOnDn;       // Q6：星座线 ON（判别对照）

    // ── 收尾要还原的原值（血泪：探针改过的环境必须还原）────────────────
    bool nightWasOn = false;
    bool ctlWasOn = false;
    bool pausedByUs = false;

    void note(const QString &line) { lines.append(QStringLiteral("  ") + line); }

    void finish()
    {
        result.ran = true;
        result.summary = QStringLiteral("夜视链路数据面探针：只报读数、不下结论"
                                        "（断言见 T37-C 的 NightModeCheck）");
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

//! 宿主 `nightMode` 动态属性的当前值（-1 = 属性不存在）。
int hostNightProperty()
{
    const QVariant v = StelMainView::getInstance().property("nightMode");
    return v.isValid() ? (v.toBool() ? 1 : 0) : -1;
}

QString hostLine()
{
    StelMainView &mv = StelMainView::getInstance();
    return QStringLiteral("宿主 StelMainView：isVisible=%1 WA_DontShowOnScreen=%2 "
                          "isHighGraphicsMode=%3 property(\"nightMode\")=%4")
        .arg(mv.isVisible() ? 1 : 0)
        .arg(mv.testAttribute(Qt::WA_DontShowOnScreen) ? 1 : 0)
        .arg(mv.getGLInformation().isHighGraphicsMode ? 1 : 0)
        .arg(hostNightProperty());
}

#endif  // STELQUICK_HAS_ENGINE && STELQUICK_WIDGETS_HOST

} // namespace

void NightModeProbe::run(QCoreApplication *app,
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
    r.summary = QStringLiteral("夜视探针需要合流形态构建"
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
        if (*idx >= steps->size())
        {
            ctx->finish();
            return;
        }
        const Step s = steps->at((*idx)++);
        s.body(ctx);
        QTimer::singleShot(s.delayAfter, ctx->app, [tick]() { (*tick)(); });
    };

    // ── S1：前提门 + Q1/Q2 + 冻结 + 把判别对照量置为"关" ─────────────────
    steps->append({delayMs, [](Ctx *c) {
        if (!StelApp::isInitialized())
        {
            c->result.unavailable = true;
            c->note(QStringLiteral("前提：引擎未初始化 ⇒ UNAVAILABLE"));
            return;
        }
        StelApp &sa = StelApp::getInstance();
        c->nightWasOn = sa.getVisionModeNight();
        if (c->facade)
            c->ctlWasOn = c->facade->actionChecked(c->controlAction);

        c->note(QStringLiteral("Q1 引擎夜视初值：getVisionModeNight=%1（actionChecked(%2)=%3）")
                    .arg(c->nightWasOn ? 1 : 0)
                    .arg(QString::fromLatin1(kNightAction))
                    .arg(c->facade
                             ? (c->facade->actionChecked(QString::fromLatin1(kNightAction)) ? 1 : 0)
                             : -1));
        c->note(QStringLiteral("Q2 %1").arg(hostLine()));
        c->note(QStringLiteral("Q1/Q2 判别对照量 %1 初值=%2")
                    .arg(c->controlAction)
                    .arg(c->ctlWasOn ? 1 : 0));

        // 冻结仿真：帧泵照常出帧，只有 JD 推进被冻结 ⇒ 期望帧内容静定。
        if (c->facade)
        {
            c->facade->setSimulationPaused(true);
            c->pausedByUs = true;
        }
        // 判别对照量先置"关"（若本来就是关则不动），保证参考帧语义单一。
        if (c->facade && c->router && c->ctlWasOn)
            c->router->trigger(c->controlAction);
        c->note(QStringLiteral("已冻结仿真（setSimulationPaused(true)）+ 判别对照量置关"
                               " ⇒ 期望帧内容静定（噪声底见 Q3）"));
    }});

    // ── S2：参考帧 A（up + dn）+ 解析天空视口矩形 ────────────────────────
    steps->append({900, [](Ctx *c) {
        if (!StelApp::isInitialized())
            return;
        c->viewportRoi = viewportRectPx(c->window);
        c->note(QStringLiteral("Q3 天空视口矩形（抓帧坐标系，物理像素）：%1")
                    .arg(c->viewportRoi.isValid()
                             ? QStringLiteral("(%1,%2)-(%3,%4) %5x%6")
                                   .arg(c->viewportRoi.left()).arg(c->viewportRoi.top())
                                   .arg(c->viewportRoi.right()).arg(c->viewportRoi.bottom())
                                   .arg(c->viewportRoi.width()).arg(c->viewportRoi.height())
                             : QStringLiteral("不可用")));
        c->refUpA = grabUpstream(c->mailbox);
        c->refDnA = grabDownstream(c->window);
        c->note(QStringLiteral("Q3 参考帧A（上游）：%1").arg(frameText(c->refUpA)));
        c->note(QStringLiteral("Q3 参考帧A（下游）：%1").arg(frameText(c->refDnA)));
    }});

    // ── S3：参考帧 B（同状态）⇒ 报噪声底 ────────────────────────────────
    steps->append({0, [](Ctx *c) {
        if (!StelApp::isInitialized())
            return;
        c->refUpB = grabUpstream(c->mailbox);
        c->refDnB = grabDownstream(c->window);
        c->note(QStringLiteral("Q3 参考帧B（上游）：%1").arg(frameText(c->refUpB)));
        c->note(QStringLiteral("Q3 参考帧B（下游）：%1").arg(frameText(c->refDnB)));
        c->note(QStringLiteral("Q3 **噪声底**（同状态 A↔B，上游）：%1")
                    .arg(diffText(diffOf(c->refUpA, c->refUpB))));
        c->note(QStringLiteral("Q3 **噪声底**（同状态 A↔B，下游）：%1")
                    .arg(downstreamDualText(c->refDnA, c->refDnB, c->viewportRoi)));
        const QString dumpDir = qEnvironmentVariable("STELQUICK_PROBE_DUMP_DIR");
        dumpPair(dumpDir, QStringLiteral("night-baseline"), c->refDnA, c->refDnB,
                 c->viewportRoi);
    }});

    // ── S4：翻转夜视 ⇒ 读引擎 + 宿主链（写入步；等待挂这里 —— 陷阱 5/69）──
    steps->append({900, [](Ctx *c) {
        if (!StelApp::isInitialized())
            return;
        const bool executed = c->router
                                  ? c->router->trigger(QString::fromLatin1(kNightAction))
                                  : false;
        const bool engineNow = StelApp::getInstance().getVisionModeNight();
        c->note(QStringLiteral("Q1/Q4 翻转：trigger(%1) ⇒ executed=%2 引擎 getVisionModeNight "
                               "%3→%4")
                    .arg(QString::fromLatin1(kNightAction))
                    .arg(executed ? 1 : 0)
                    .arg(c->nightWasOn ? 1 : 0)
                    .arg(engineNow ? 1 : 0));
        c->note(QStringLiteral("Q2 翻转后 %1").arg(hostLine()));
    }});

    // ── S5：取夜视 ON 帧 ⇒ 报 Q4/Q5（读步；S4 已等 900ms，这里立即抓）────
    steps->append({0, [](Ctx *c) {
        if (!StelApp::isInitialized())
            return;
        c->nightOnUp = grabUpstream(c->mailbox);
        c->nightOnDn = grabDownstream(c->window);
        c->note(QStringLiteral("Q4 夜视ON（上游）：%1").arg(frameText(c->nightOnUp)));
        c->note(QStringLiteral("Q4 夜视 OFF↔ON（上游，vs 参考帧B）：%1")
                    .arg(diffText(diffOf(c->refUpB, c->nightOnUp))));
        c->note(QStringLiteral("Q5 夜视ON（下游）：%1").arg(frameText(c->nightOnDn)));
        c->note(QStringLiteral("Q5 夜视 OFF↔ON（下游，vs 参考帧B）：%1")
                    .arg(downstreamDualText(c->refDnB, c->nightOnDn, c->viewportRoi)));
        const QString dumpDir = qEnvironmentVariable("STELQUICK_PROBE_DUMP_DIR");
        dumpPair(dumpDir, QStringLiteral("night-downstream"), c->refDnB, c->nightOnDn,
                 c->viewportRoi);
        dumpPair(dumpDir, QStringLiteral("night-upstream"), c->refUpB, c->nightOnUp,
                 QRect());
    }});

    // ── S6：还原夜视 ⇒ 开判别对照量（写入步；等待挂这里）──────────────────
    steps->append({900, [](Ctx *c) {
        if (!StelApp::isInitialized())
            return;
        if (c->router)
            c->router->trigger(QString::fromLatin1(kNightAction));   // 还原
        const bool engineAfterRestore = StelApp::getInstance().getVisionModeNight();
        if (c->router)
            c->router->trigger(c->controlAction); // 判别对照：开
        c->note(QStringLiteral("Q1 还原夜视：getVisionModeNight=%1（应为初值 %2）")
                    .arg(engineAfterRestore ? 1 : 0)
                    .arg(c->nightWasOn ? 1 : 0));
        c->note(QStringLiteral("Q6 打开判别对照量 %1（原值 %2）")
                    .arg(c->controlAction)
                    .arg(c->ctlWasOn ? 1 : 0));
    }});

    // ── S7：取对照 ON 帧 ⇒ 报 Q6（承重，读步）───────────────────────────
    steps->append({0, [](Ctx *c) {
        if (!StelApp::isInitialized())
            return;
        c->ctlOnUp = grabUpstream(c->mailbox);
        c->ctlOnDn = grabDownstream(c->window);
        c->note(QStringLiteral("Q6 对照ON（上游）：%1").arg(frameText(c->ctlOnUp)));
        c->note(QStringLiteral("Q6 **判别对照** OFF↔ON（上游，vs 参考帧B）：%1")
                    .arg(diffText(diffOf(c->refUpB, c->ctlOnUp))));
        c->note(QStringLiteral("Q6 对照ON（下游）：%1").arg(frameText(c->ctlOnDn)));
        c->note(QStringLiteral("Q6 **判别对照** OFF↔ON（下游，vs 参考帧B）：%1")
                    .arg(downstreamDualText(c->refDnB, c->ctlOnDn, c->viewportRoi)));
        const QString dumpDir = qEnvironmentVariable("STELQUICK_PROBE_DUMP_DIR");
        dumpPair(dumpDir, QStringLiteral("control-downstream"), c->refDnB, c->ctlOnDn,
                 c->viewportRoi);
        dumpPair(dumpDir, QStringLiteral("control-upstream"), c->refUpB, c->ctlOnUp,
                 QRect());
    }});

    // ── S8：还原全部 + 收尾 ────────────────────────────────────────────
    steps->append({0, [](Ctx *c) {
        if (!StelApp::isInitialized())
            return;
        if (c->router && !c->ctlWasOn)
            c->router->trigger(c->controlAction);   // 判别对照量还原
        if (c->facade && c->pausedByUs)
            c->facade->setSimulationPaused(false);
        c->note(QStringLiteral("Q6 语义：Q4/Q5 若与 Q3 噪声底同量级（尤其同为 0），"
                               "说明**夜视在合流形态的读回帧上不可见**；Q6 必须显著大于"
                               "噪声底 —— 否则是「比较方法测不出东西」的假绿。"));
        c->note(QStringLiteral("收尾：判别对照量还原=%1 仿真暂停还原=%2")
                    .arg(!c->ctlWasOn ? 1 : 0)
                    .arg(c->pausedByUs ? 1 : 0));
    }});

    QTimer::singleShot(0, app, [tick]() { (*tick)(); });
#endif
}

} // namespace stelapp
