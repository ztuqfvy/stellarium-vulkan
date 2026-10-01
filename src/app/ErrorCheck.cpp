/*
 * ErrorCheck — 实现（T42-C）。判据表与设计原则见同名头文件。
 */
#include "app/ErrorCheck.hpp"

#if defined(STELQUICK_HAS_ENGINE)

#include "app/ActionRouter.hpp"
#include "app/ErrorModel.hpp"
#include "core/StelActionMgr.hpp"
#include "core/StelApp.hpp"
#include "core/StelFileMgr.hpp"
#include "StelLogger.hpp"

#include <QApplication>
#include <QClipboard>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QKeySequence>
#include <QMouseEvent>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSettings>
#include <QSet>
#include <QTimer>
#include <QVariantMap>
#include <QWidget>

#include <functional>
#include <memory>

namespace stelapp {

namespace {

using CheckResult = ErrorCheck::Result;

// ── 工具（与 HelpCheck 同款；各 check 自带一份，不跨 TU 共享匿名命名空间）─────

QMap<QString, QVariant> dumpSettings(QSettings *s)
{
    QMap<QString, QVariant> out;
    if (!s)
        return out;
    const QStringList keys = s->allKeys();
    for (const QString &k : keys)
        out.insert(k, s->value(k));
    return out;
}

QStringList diffKeys(const QMap<QString, QVariant> &a, const QMap<QString, QVariant> &b)
{
    QStringList out;
    for (auto it = a.cbegin(); it != a.cend(); ++it)
    {
        const auto jt = b.constFind(it.key());
        if (jt == b.cend() || jt.value() != it.value())
            out << it.key();
    }
    for (auto it = b.cbegin(); it != b.cend(); ++it)
        if (!a.contains(it.key()))
            out << (QStringLiteral("+") + it.key());
    out.sort();
    return out;
}

//! 递归**视觉树**收集 objectName 命中的项。
//! ⚠️ 陷阱 45：Repeater 生成的 delegate 的 QObject 父链是空的 ⇒ `findChild` 永远
//!    扫不到；静态兄弟项却能扫到 ⇒ 一律走 `childItems()` 递归。
void collectByObjectName(QQuickItem *root, const QString &name, QList<QQuickItem *> *out)
{
    if (!root)
        return;
    if (root->objectName() == name)
        out->append(root);
    const QList<QQuickItem *> kids = root->childItems();
    for (QQuickItem *k : kids)
        collectByObjectName(k, name, out);
}

QList<QQuickItem *> byName(QQuickWindow *w, const QString &name)
{
    QList<QQuickItem *> out;
    if (!w)
        return out;
    if (auto *content = w->contentItem())
        collectByObjectName(content, name, &out);
    return out;
}

QPointF itemCenterInWindow(QQuickItem *item)
{
    if (!item)
        return QPointF(-1, -1);
    return item->mapToScene(QPointF(item->width() / 2.0, item->height() / 2.0));
}

//! 落点是否被**自身及其全部祖先**的矩形覆盖（陷阱 47：`childAt` 会撒谎）。
bool pointCoveredByAncestors(QQuickItem *item, const QPointF &scenePos)
{
    if (!item || item->width() <= 0 || item->height() <= 0)
        return false;
    QPointF p = scenePos;
    for (QQuickItem *it = item; it; it = it->parentItem())
    {
        const QPointF lp = it->mapFromScene(p);
        const QRectF r(0, 0, it->width(), it->height());
        if (!r.contains(lp))
            return false;
        // 可见性/裁剪：QML 侧用 clip 的 Item 才裁剪，这里只判矩形与 visible
        if (!it->isVisible())
            return false;
    }
    return true;
}

bool clickButton(QQuickWindow *window, const QString &objectName, QString *why = nullptr)
{
    const QList<QQuickItem *> items = byName(window, objectName);
    if (items.isEmpty())
    {
        if (why)
            *why = QStringLiteral("未找到 %1").arg(objectName);
        return false;
    }
    QQuickItem *btn = items.first();
    const QPointF scenePos = itemCenterInWindow(btn);
    if (!pointCoveredByAncestors(btn, scenePos))
    {
        if (why)
            *why = QStringLiteral("%1 的中心 %2,%3 未被自身+祖先矩形覆盖（沉底/被裁剪）")
                       .arg(objectName)
                       .arg(scenePos.x())
                       .arg(scenePos.y());
        return false;
    }
    // ⚠️ 两个坐标参数都传 **scenePos**：`QQuickWindow::mousePressEvent` 用的是
    //    **窗口坐标**（在 QQuickWindow 里 scene == window 坐标系），而 QMouseEvent
    //    的第一个参数是 localPos（默认取 position()）。首轮把按钮**局部**中心传进
    //    localPos ⇒ 点击落在窗口左上角（40,20 那种）⇒ 按钮的 onClicked 不触发、
    //    剪贴板恒空（EC-13 首轮红）。T40 `ShortcutCheck` 就是传 (scenePos, scenePos)。
    QMouseEvent press(QEvent::MouseButtonPress, scenePos, scenePos, Qt::LeftButton,
                      Qt::LeftButton, Qt::NoModifier);
    QMouseEvent release(QEvent::MouseButtonRelease, scenePos, scenePos, Qt::LeftButton,
                        Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(window, &press);
    QCoreApplication::sendEvent(window, &release);
    return true;
}

int stackIndex(QQuickWindow *w)
{
    const QList<QQuickItem *> s = byName(w, QStringLiteral("pageStack"));
    if (s.isEmpty())
        return -1;
    return s.first()->property("currentIndex").toInt();
}

int widgetCount() { return QApplication::allWidgets().size(); }

int visibleDialogs()
{
    int n = 0;
    for (QWidget *w : QApplication::allWidgets())
        if (w && w->isWindow() && w->isVisible() && w->inherits("QDialog"))
            ++n;
    return n;
}

void closeVisibleDialogs()
{
    for (QWidget *w : QApplication::allWidgets())
        if (w && w->isWindow() && w->isVisible() && w->inherits("QDialog"))
            w->close();
}

QVariantMap at(const QVariantList &rows, int i)
{
    return i >= 0 && i < rows.size() ? rows.at(i).toMap() : QVariantMap();
}

struct Ctx;
struct Step
{
    int delayAfter = 0;
    std::function<void(Ctx *)> body;
};

struct Ctx
{
    QCoreApplication *app = nullptr;
    QQuickWindow *window = nullptr;
    ActionRouter *router = nullptr;
    ErrorModel *model = nullptr;
    CheckResult result;
    QMap<QString, QVariant> baseConf;
    QSettings *conf = nullptr;
    QList<QPair<QString, bool>> items;   //!< 台账（显式 id + 通过与否）
    QSet<QString> marked;                //!< 已被 mark 过的 id（用于"静默丢 mark"自证）
    //! ⚠️ 收割回调**必须是成员**（陷阱 84：漏赋值 ⇒ std::bad_function_call、零输出）。
    std::function<void(const CheckResult &)> onDone;

    void note(const QString &s) { result.details << s; }
    //! 记一条判据。⚠️ **台账按 note 的**首词**关联**（陷阱 85：mark 与台账 id 必须
    //! 逐字一致，不一致就**静默丢 mark** —— 症状是"判据 N/N 却 VERDICT=FAIL"）。
    //! 首轮本判据就栽在这：`mark()` 只写了 details、没写台账 ⇒ 14 条全 PASS 却 0/14。
    void mark(bool ok, const QString &s)
    {
        const QString id = s.section(QLatin1Char(' '), 0, 0);
        for (auto &it : items)
            if (it.first == id)
            {
                it.second = ok;
                marked.insert(id);
                break;
            }
        result.details << QStringLiteral("[%1] %2").arg(ok ? QStringLiteral("PASS")
                                                          : QStringLiteral("FAIL"), s);
    }
    void markNa(const QString &s)
    {
        const QString id = s.section(QLatin1Char(' '), 0, 0);
        marked.insert(id);
        result.details << QStringLiteral("[NA] %1").arg(s);
    }
    void finish()
    {
        int pass = 0;
        for (const auto &it : items)
            if (it.second)
                ++pass;
        result.ran = true;
        result.passed = pass;
        result.total = items.size();
        result.pass = (pass == items.size());
        // 自证：台账里若有 id 从未被 mark 过，说明"字符串拼出来的身份"失配了 ——
        // 显式报出来（否则就是静默丢判据，陷阱 85）。
        QStringList never;
        for (const auto &it : items)
            if (!marked.contains(it.first))
                never << it.first;
        result.summary = QStringLiteral("状态与错误页自检：判据 %1/%2").arg(pass).arg(items.size());
        if (!never.isEmpty())
            result.summary += QStringLiteral("｜⚠️ 未被记账的 id：%1")
                                  .arg(never.join(QStringLiteral(",")));
        if (onDone)
            onDone(result);
    }
};

} // namespace

void ErrorCheck::run(QCoreApplication *app,
                     QQuickWindow *window,
                     ActionRouter *router,
                     ErrorModel *model,
                     bool engineBooted,
                     const std::function<void(const Result &)> &onDone,
                     int delayMs)
{
    auto ctx = std::make_shared<Ctx>();
    ctx->app = app;
    ctx->window = window;
    ctx->router = router;
    ctx->model = model;
    ctx->onDone = onDone;   // ⚠️ 第一句就赋值（陷阱 84）

    auto steps = std::make_shared<QVector<Step>>();
    auto tick = std::make_shared<std::function<void(int)>>();

    const auto runStep = [steps, ctx, tick](int i) {
        if (i >= steps->size())
        {
            ctx->finish();
            return;
        }
        const Step &st = steps->at(i);
        st.body(ctx.get());
        if (st.delayAfter > 0)
            QTimer::singleShot(st.delayAfter, ctx->app, [tick, i]() { (*tick)(i + 1); });
        else
            (*tick)(i + 1);
    };
    *tick = [runStep](int i) { runStep(i); };
    QTimer::singleShot(delayMs, app, [runStep]() { runStep(0); });

    if (engineBooted)
    {
        // ══════════ A 组：引擎可用（正题）══════════
        //! 台账：**显式 id 列表**（陷阱 85 —— `arg(i)` 拼出来的 id 与 note 首词
        //! 不一致时会静默丢 mark，症状是"判据 N/N 却 VERDICT=FAIL"）。
        const QStringList ids{QStringLiteral("EC-01"), QStringLiteral("EC-02"),
                              QStringLiteral("EC-03"), QStringLiteral("EC-04"),
                              QStringLiteral("EC-05"), QStringLiteral("EC-06"),
                              QStringLiteral("EC-07"), QStringLiteral("EC-08"),
                              QStringLiteral("EC-09"), QStringLiteral("EC-10"),
                              QStringLiteral("EC-11"), QStringLiteral("EC-12"),
                              QStringLiteral("EC-13"), QStringLiteral("EC-14"),
                              QStringLiteral("EC-15")};
        for (const QString &id : ids)
            ctx->items.append(qMakePair(id, false));

        // ── S1 布场 ────────────────────────────────────────────────────────
        steps->append(Step{0, [](Ctx *c) {
            c->conf = StelApp::getInstance().getSettings();
            c->baseConf = dumpSettings(c->conf);
            c->note(QStringLiteral("布场：配置文件 %1（%2 键）｜QWidget %3｜可见 QDialog %4｜"
                                   "接管 %5 个｜页栈 index %6")
                        .arg(c->conf ? c->conf->fileName() : QStringLiteral("(null)"))
                        .arg(c->baseConf.size())
                        .arg(widgetCount())
                        .arg(visibleDialogs())
                        .arg(c->router->hostTakeoverIds().size())
                        .arg(stackIndex(c->window)));
        }});

        // ── S2 EC-01/02/03 资源路径面 ──────────────────────────────────────
        steps->append(Step{0, [](Ctx *c) {
            const QVariantList rows = c->model->pathRows();
            const int problems = c->model->pathProblemCount();
            bool labelsOk = true;
            for (const QVariant &v : rows)
                if (v.toMap().value(QStringLiteral("label")).toString().isEmpty())
                    labelsOk = false;
            c->mark(rows.size() == 8 && problems == 0 && labelsOk,
                    QStringLiteral("EC-01 资源路径表 %1 行（应 8）｜problem %2（应 0）｜"
                                   "标签齐备=%3")
                        .arg(rows.size()).arg(problems).arg(labelsOk ? QStringLiteral("是")
                                                                     : QStringLiteral("否")));

            const QString userDir = c->model->userDir();
            c->mark(userDir.endsWith(QStringLiteral("-quick")),
                    QStringLiteral("EC-02 用户目录以 `-quick` 结尾（T36 隔离运行期读数）：%1")
                        .arg(userDir));

            const QString cfg = c->model->configPath();
            const QString log = c->model->logPath();
            const QString cfgState = at(rows, 6).value(QStringLiteral("state")).toString();
            const QString logState = at(rows, 7).value(QStringLiteral("state")).toString();
            c->mark(!cfg.isEmpty() && !log.isEmpty() && cfgState == QLatin1String("ok")
                        && logState == QLatin1String("ok"),
                    QStringLiteral("EC-03 配置文件/日志文件就位：cfg=%1(%2)｜log=%3(%4)")
                        .arg(cfg.isEmpty() ? QStringLiteral("空") : QStringLiteral("有"))
                        .arg(cfgState, log.isEmpty() ? QStringLiteral("空") : QStringLiteral("有"),
                             logState));
        }});

        // ── S3 EC-04/05 状态面与"无错不喊狼" ───────────────────────────────
        steps->append(Step{0, [](Ctx *c) {
            const QVariantList rows = c->model->statusRows();
            QStringList bad;
            for (const QVariant &v : rows)
                if (v.toMap().value(QStringLiteral("state")).toString() == QLatin1String("error"))
                    bad << v.toMap().value(QStringLiteral("label")).toString();
            c->mark(rows.size() == 4 && c->model->statusProblemCount() == 0,
                    QStringLiteral("EC-04 状态表 %1 行（应 4）｜error 行 %2%3")
                        .arg(rows.size()).arg(c->model->statusProblemCount())
                        .arg(bad.isEmpty() ? QString()
                                           : QStringLiteral("：") + bad.join(QStringLiteral(","))));

            c->mark(!c->model->hasError() && c->model->errorHeadline().isEmpty(),
                    QStringLiteral("EC-05 引擎已引导 + 后端已注入 ⇒ hasError=%1（应 false，"
                                   "无错时不该报错）｜headline「%2」")
                        .arg(c->model->hasError())
                        .arg(c->model->errorHeadline()));
        }});

        // ── S4 EC-06/07 未支持项清单（含**真源交叉验算**）──────────────────
        steps->append(Step{0, [](Ctx *c) {
            const QVariantList rows = c->model->unsupportedRows();
            c->mark(rows.size() == 7,
                    QStringLiteral("EC-06 未支持项 %1 条（应 7：4 老窗口 + 插件设置 + "
                                   "legacy 手势 + T39 两控件）").arg(rows.size()));

            // 真源交叉验算：4 个老窗口项的 actionId 必须在注册表里，且 feature 名
            // 必须与 getText() **逐字相同**（清单不许凭记忆写 —— 陷阱 67 的同族）。
            StelActionMgr *mgr = StelApp::getInstance().getStelActionManager();
            int checked = 0;
            QStringList mism;
            for (int i = 0; i < rows.size() && i < 4; ++i)
            {
                const QVariantMap m = at(rows, i);
                const QString id = m.value(QStringLiteral("actionId")).toString();
                StelAction *a = mgr ? mgr->findAction(id) : nullptr;
                if (!a)
                {
                    mism << QStringLiteral("%1(不在注册表)").arg(id);
                    continue;
                }
                ++checked;
                if (a->getText() != m.value(QStringLiteral("feature")).toString())
                    mism << QStringLiteral("%1(名不符：注册表「%2」vs 清单「%3」)")
                                .arg(id, a->getText(),
                                     m.value(QStringLiteral("feature")).toString());
            }
            c->mark(checked == 4 && mism.isEmpty(),
                    QStringLiteral("EC-07 未支持项 4 个老窗口动作与注册表交叉验算：命中 %1/4｜"
                                   "不符 %2")
                        .arg(checked)
                        .arg(mism.isEmpty() ? QStringLiteral("无（清单与真源一致）")
                                            : mism.join(QStringLiteral("; "))));
        }});

        // ── S5 EC-08 接管集合 ──────────────────────────────────────────────
        steps->append(Step{0, [](Ctx *c) {
            const QStringList ids = c->router->hostTakeoverIds();
            const QStringList must{QStringLiteral("actionShow_Help_Window_Global"),
                                   QStringLiteral("actionShow_Search_Window_Global"),
                                   QStringLiteral("actionShow_SkyView_Window_Global"),
                                   QStringLiteral("actionShow_DateTime_Window_Global"),
                                   QStringLiteral("actionShow_Location_Window_Global"),
                                   QStringLiteral("actionShow_Shortcuts_Window_Global"),
                                   QStringLiteral("actionShow_Configuration_Window_Global"),
                                   QStringLiteral("actionShow_AstroCalc_Window_Global"),
                                   QStringLiteral("actionShow_ScriptConsole_Window_Global"),
                                   QStringLiteral("actionShow_ObsList_Window_Global")};
            QStringList missing;
            for (const QString &m : must)
                if (!ids.contains(m))
                    missing << m;
            c->mark(ids.size() == 10 && missing.isEmpty(),
                    QStringLiteral("EC-08 接管表 %1 个（应 10 = T41 的 6 + T42 的 4）｜缺 %2")
                        .arg(ids.size())
                        .arg(missing.isEmpty() ? QStringLiteral("无")
                                               : missing.join(QStringLiteral(","))));
        }});

        // ── S6 EC-09 未支持动作 trigger ⇒ 不弹老窗口 + 弹提示 + 配置零变化 ──
        steps->append(Step{250, [](Ctx *c) {
            const QStringList targets{QStringLiteral("actionShow_Configuration_Window_Global"),
                                      QStringLiteral("actionShow_AstroCalc_Window_Global"),
                                      QStringLiteral("actionShow_ScriptConsole_Window_Global"),
                                      QStringLiteral("actionShow_ObsList_Window_Global")};
            const QMap<QString, QVariant> before = dumpSettings(c->conf);
            int w0 = widgetCount();
            QStringList fails;
            QStringList seen;
            for (const QString &id : targets)
            {
                const int n0 = widgetCount();
                c->router->trigger(id);
                const int n1 = widgetCount();
                if (n1 != n0)
                    fails << QStringLiteral("%1(QWidget Δ%2)").arg(id).arg(n1 - n0);

                auto *dlg = c->window->findChild<QObject *>(QStringLiteral("unsupportedDialog"));
                const bool vis = dlg && dlg->property("visible").toBool();
                const QString feat = dlg ? dlg->property("featureName").toString() : QString();
                if (!vis || feat.isEmpty())
                    fails << QStringLiteral("%1(提示未出现/无功能名)").arg(id);
                else
                    seen << feat;
                if (dlg)
                    QMetaObject::invokeMethod(dlg, "close");
            }
            const int wDelta = widgetCount() - w0;
            const QMap<QString, QVariant> after = dumpSettings(c->conf);
            const QStringList confDiff = diffKeys(before, after);
            if (wDelta != 0)
                fails << QStringLiteral("总 QWidget Δ%1（应 0 —— 老对话框会各建几十上百个）")
                             .arg(wDelta);
            if (!confDiff.isEmpty())
                fails << QStringLiteral("配置被写：%1").arg(confDiff.join(QStringLiteral(", ")));
            c->mark(fails.isEmpty(),
                    QStringLiteral("EC-09 4 个未支持动作逐个 trigger ⇒ QWidget Δ=%1（应 0）｜"
                                   "提示出现 4/4（%2）｜配置写入 %3 项（应 0）%4")
                        .arg(wDelta)
                        .arg(seen.join(QStringLiteral("、")))
                        .arg(confDiff.size())
                        .arg(fails.isEmpty() ? QString()
                                             : QStringLiteral("｜FAIL 细节：")
                                                   + fails.join(QStringLiteral("; "))));
            // 🔴 **本步造成的写入必须本步自净**（负控 A 下 EC-10 走早退分支、
            //    不会代为还原 —— 实测 negA 把 `DialogSizes/Configuration`
            //    765,605 → 770,656 留在了真实 config.ini 里）。按布场基线还原：
            //    新增键删除、变化键回填。红项判定用的是还原**前**的读数，不受影响。
            {
                const QMap<QString, QVariant> now = dumpSettings(c->conf);
                QStringList added, changed;
                for (const QString &k : diffKeys(c->baseConf, now))
                {
                    if (k.startsWith(QLatin1Char('+')))
                        added << k.mid(1);
                    else
                        changed << k;
                }
                for (const QString &k : changed)
                    c->conf->setValue(k, c->baseConf.value(k));
                for (const QString &k : added)
                    c->conf->remove(k);
                if (!added.isEmpty() || !changed.isEmpty())
                    c->conf->sync();
                if (!added.isEmpty() || !changed.isEmpty())
                    c->note(QStringLiteral("EC-09 收尾：本步造成的配置写入 %1 项已按基线还原"
                                           "（新增 %2）")
                                .arg(added.size() + changed.size())
                                .arg(added.isEmpty() ? QStringLiteral("无")
                                                     : added.join(QStringLiteral(", "))));
            }
            closeVisibleDialogs();   // 每步自己收尾
        }});

        // ── S7 EC-10 判别对照：撤销接管 ⇒ 老窗口**真的会弹** ───────────────
        steps->append(Step{250, [](Ctx *c) {
            const QString id = QStringLiteral("actionShow_AstroCalc_Window_Global");
            // ⚠️ 恢复只恢复到**布场态**（陷阱 90：无条件恢复会自己把被负控打断的
            //    路径修好 ⇒ 负控下本判据假绿）。
            const bool was = c->router->isHostTakeover(id);
            if (!was)
            {
                c->markNa(QStringLiteral("EC-10 判别对照：%1 布场时未被接管"
                                         "（负控 ERROR_TAKEOVER_OFF 生效中）⇒ 无可撤销"));
                // 本项在负控下**不适用**：按 id 找到台账条目并置通过（不用魔法下标）。
                for (auto &it : c->items)
                    if (it.first == QLatin1String("EC-10"))
                        it.second = true;
                return;
            }
            c->router->setHostTakeover(id, false);
            const int n0 = widgetCount();
            c->router->trigger(id);
            const int n1 = widgetCount();
            // 还原（老对话框只隐藏不销毁 ⇒ Δ 常驻是既有语义，不作判据）
            closeVisibleDialogs();
            if (was)
                c->router->setHostTakeover(id, true);
            // 🔴 **每步自己收尾**：老对话框会把自己的尺寸写进 `QSettings`
            //    （`DialogSizes/AstroCalc` —— T42-A 探针 Q9 实测），不清理就会
            //    污染 EC-14 的"配置差异 0"（首轮实测：EC-14 红在 `+DialogSizes/AstroCalc`）。
            //    按布场基线还原（新增键删除、变化键回填）。
            {
                const QMap<QString, QVariant> now = dumpSettings(c->conf);
                QStringList added, changed;
                for (const QString &k : diffKeys(c->baseConf, now))
                {
                    if (k.startsWith(QLatin1Char('+')))
                        added << k.mid(1);
                    else
                        changed << k;
                }
                for (const QString &k : changed)
                    c->conf->setValue(k, c->baseConf.value(k));
                for (const QString &k : added)
                    c->conf->remove(k);
                if (!added.isEmpty() || !changed.isEmpty())
                    c->conf->sync();
                c->note(QStringLiteral("EC-10 收尾：判别对照造成的配置写入 %1 项已按基线还原"
                                       "（新增 %2）")
                            .arg(added.size() + changed.size())
                            .arg(added.isEmpty() ? QStringLiteral("无")
                                                 : added.join(QStringLiteral(", "))));
            }
            c->mark(n1 > n0,
                    QStringLiteral("EC-10 判别对照：撤销接管后 trigger ⇒ QWidget %1→%2"
                                   "（Δ%3>0，老对话框**真的会弹**）⇒ EC-09 的 Δ=0 是接管的功劳")
                        .arg(n0).arg(n1).arg(n1 - n0));
        }});

        // ── S8 EC-11/12 错误页 UI 控件（递归视觉树 + 几何覆盖）──────────────
        steps->append(Step{200, [](Ctx *c) {
            // 确保停在错误页（前一节的 trigger 不会切页，但显式保险）
            c->window->setProperty("__noop", false);
            QStringList missing;
            for (int i = 0; i < 4; ++i)
                if (byName(c->window, QStringLiteral("errorStatusRow_%1").arg(i)).isEmpty())
                    missing << QStringLiteral("statusRow_%1").arg(i);
            for (int i = 0; i < 8; ++i)
                if (byName(c->window, QStringLiteral("errorPathRow_%1").arg(i)).isEmpty())
                    missing << QStringLiteral("pathRow_%1").arg(i);
            for (int i = 0; i < 7; ++i)
                if (byName(c->window, QStringLiteral("errorUnsupportedRow_%1").arg(i)).isEmpty())
                    missing << QStringLiteral("unsupportedRow_%1").arg(i);
            c->mark(missing.isEmpty(),
                    QStringLiteral("EC-11 错误页控件（视觉树递归）：状态行 4/4｜路径行 8/8｜"
                                   "未支持行 7/7｜缺 %1")
                        .arg(missing.isEmpty() ? QStringLiteral("无")
                                               : missing.join(QStringLiteral(","))));

            const QStringList buttons{QStringLiteral("errorRefreshButton"),
                                      QStringLiteral("errorCopyDiagnosticsButton"),
                                      QStringLiteral("errorOpenLogButton"),
                                      QStringLiteral("errorOpenConfigButton")};
            QStringList bad;
            for (const QString &b : buttons)
            {
                const QList<QQuickItem *> found = byName(c->window, b);
                if (found.isEmpty())
                {
                    bad << QStringLiteral("%1(不存在)").arg(b);
                    continue;
                }
                const QPointF p = itemCenterInWindow(found.first());
                if (!pointCoveredByAncestors(found.first(), p))
                    bad << QStringLiteral("%1(中心 %2,%3 不可达)").arg(b).arg(p.x()).arg(p.y());
            }
            c->mark(bad.isEmpty(),
                    QStringLiteral("EC-12 操作行 4 按钮存在且落点可达（几何覆盖判据）：%1")
                        .arg(bad.isEmpty() ? QStringLiteral("4/4 可达")
                                           : bad.join(QStringLiteral("; "))));
        }});

        // ── S9 EC-13 复制诊断信息（真实点击 → 剪贴板）───────────────────────
        steps->append(Step{200, [](Ctx *c) {
            QGuiApplication::clipboard()->clear();
            QString why;
            const bool clicked = clickButton(c->window, QStringLiteral("errorCopyDiagnosticsButton"),
                                             &why);
            const QString text = QGuiApplication::clipboard()->text();
            const bool ok = clicked && !text.isEmpty()
                            && text.contains(c->model->fullVersion())
                            && text.contains(QStringLiteral("未支持项"));
            c->mark(ok,
                    QStringLiteral("EC-13 真实点击「复制诊断信息」⇒ 剪贴板 %1 字符｜含版本行=%2｜"
                                   "含未支持项小节=%3%4")
                        .arg(text.size())
                        .arg(text.contains(c->model->fullVersion()) ? QStringLiteral("是")
                                                                    : QStringLiteral("否"))
                        .arg(text.contains(QStringLiteral("未支持项")) ? QStringLiteral("是")
                                                                       : QStringLiteral("否"))
                        .arg(clicked ? QString() : QStringLiteral("（点击失败：%1）").arg(why)));
        }});

        // ── S10 EC-15 openPath 白名单闸（真模式下也安全：拒收不弹任何东西）──
        // ⚠️ 只验"被声称的命题"（陷阱 75）：产品声称的是"**白名单**制 —— 只认
        //    log/config/userDir/installDir/cacheDir，未知 kind 拒收"。"真的唤起
        //    系统程序"这一半在批跑环境不可验（会弹 Finder），由
        //    STELQUICK_ERROR_OPEN_OFF（受理但不唤起）留给交互式人工腿。
        steps->append(Step{0, [](Ctx *c) {
            const bool refused = !c->model->openPath(QStringLiteral("bogus/nonexistent-kind"));
            // 已知 kind 的"受理"半边只在 OPEN_OFF 下无副作用 ⇒ 只有该开关置位才验它
            const bool gateMode = qEnvironmentVariableIsSet("STELQUICK_ERROR_OPEN_OFF");
            bool accepted = true;
            if (gateMode)
                accepted = c->model->openPath(QStringLiteral("log"));
            c->mark(refused && accepted,
                    QStringLiteral("EC-15 openPath 白名单闸：未知 kind 拒收=%1（应 true）"
                                   "%2")
                        .arg(refused)
                        .arg(gateMode
                                 ? QStringLiteral("｜OPEN_OFF 下已知 kind「log」受理=%1"
                                                  "（应 true，不弹 Finder）").arg(accepted)
                                 : QString()));
        }});

        // ── S11 EC-14 复原（配置指纹 + 接管集合）────────────────────────────
        steps->append(Step{0, [](Ctx *c) {
            const QStringList diff = diffKeys(c->baseConf, dumpSettings(c->conf));
            const QStringList ids = c->router->hostTakeoverIds();
            c->mark(diff.isEmpty() && ids.size() == 10,
                    QStringLiteral("EC-14 复原：配置差异 %1 项（应 0）%2｜接管 %3 个（应 10）")
                        .arg(diff.size())
                        .arg(diff.isEmpty() ? QString()
                                            : QStringLiteral("：")
                                                  + diff.join(QStringLiteral(", ")))
                        .arg(ids.size()));
        }});
        return;
    }

    // ══════════ B 组：引擎不可用（失败腿 —— 用 STEL_USERDIR 造真实失败）══════════
    const QStringList ids{QStringLiteral("EC-01"), QStringLiteral("EC-02"),
                          QStringLiteral("EC-03"), QStringLiteral("EC-04"),
                          QStringLiteral("EC-05")};
    for (const QString &id : ids)
        ctx->items.append(qMakePair(id, false));

    steps->append(Step{0, [](Ctx *c) {
        c->note(QStringLiteral("失败腿布场：STEL_USERDIR=「%1」｜引擎 initialized=%2｜页栈 index %3")
                    .arg(qEnvironmentVariable("STEL_USERDIR"))
                    .arg(StelApp::isInitialized() ? 1 : 0)
                    .arg(stackIndex(c->window)));
    }});

    // EC-01 hasError + headline
    steps->append(Step{0, [](Ctx *c) {
        c->mark(c->model->hasError() && !c->model->errorHeadline().isEmpty(),
                QStringLiteral("EC-01 引导失败 ⇒ hasError=%1（应 true）｜headline「%2」")
                    .arg(c->model->hasError())
                    .arg(c->model->errorHeadline()));
    }});

    // EC-02 detail 与失败原因一致（**判据自己从环境推期望值**，不复述注入串）
    steps->append(Step{0, [](Ctx *c) {
        const QString detail = c->model->errorDetail();
        const QString userDirEnv = qEnvironmentVariable("STEL_USERDIR");
        // 期望：失败原因里应**提到**那个不可创建的路径（boot 的 fail() 文案带 .arg(cand)）
        const bool mentions = !userDirEnv.isEmpty() && detail.contains(userDirEnv);
        c->mark(!detail.isEmpty() && mentions,
                QStringLiteral("EC-02 错误详情非空=%1｜提及不可创建的 STEL_USERDIR=%2（应 true）"
                               "｜detail「%3」")
                    .arg(!detail.isEmpty()).arg(mentions).arg(detail));
    }});

    // EC-03 错误块 UI 可见 + 文本非空
    steps->append(Step{200, [](Ctx *c) {
        const QList<QQuickItem *> panel = byName(c->window, QStringLiteral("errorPanel"));
        const QList<QQuickItem *> head = byName(c->window, QStringLiteral("errorHeadlineLabel"));
        const QList<QQuickItem *> det = byName(c->window, QStringLiteral("errorDetailLabel"));
        const bool panelVis = !panel.isEmpty() && panel.first()->isVisible();
        const QString headTxt = head.isEmpty() ? QString()
                                               : head.first()->property("text").toString();
        const QString detTxt = det.isEmpty() ? QString()
                                             : det.first()->property("text").toString();
        c->mark(panelVis && !headTxt.isEmpty() && !detTxt.isEmpty(),
                QStringLiteral("EC-03 错误块 UI：panel 可见=%1｜headline %2 字符｜detail %3 字符")
                    .arg(panelVis).arg(headTxt.size()).arg(detTxt.size()));
    }});

    // EC-04 状态表"引擎引导"行 = error
    steps->append(Step{0, [](Ctx *c) {
        const QVariantList rows = c->model->statusRows();
        const QVariantMap first = rows.isEmpty() ? QVariantMap() : rows.first().toMap();
        c->mark(!rows.isEmpty()
                    && first.value(QStringLiteral("state")).toString() == QLatin1String("error"),
                QStringLiteral("EC-04 状态表首行「%1」state=%2（应 error）")
                    .arg(first.value(QStringLiteral("label")).toString(),
                         first.value(QStringLiteral("state")).toString()));
    }});

    // EC-05 资源路径不崩不空（引导失败也要能给出排查线索）
    steps->append(Step{0, [](Ctx *c) {
        const QVariantList rows = c->model->pathRows();
        c->mark(rows.size() >= 6,
                QStringLiteral("EC-05 引导失败时资源路径仍可展示：%1 行（≥6 即算给出排查线索）")
                    .arg(rows.size()));
    }});
}

} // namespace stelapp

#endif // STELQUICK_HAS_ENGINE
