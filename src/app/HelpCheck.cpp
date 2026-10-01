/*
 * HelpCheck — 实现（T41-C 帮助/版本/许可证自检）。判据清单与设计依据见头注。
 *
 * 驱动方式沿用 ShortcutCheck（T40-C）的线性步骤表。
 * ⚠️ delayAfter 语义 = "跑完**本步**之后的等待"（陷阱 41/69）。
 * 🔴 两处 T40 血泪的直接复用：
 *   · `ctx->onDone = onDone;` **必填**（漏了 = 14 步跑完收尾 bad_function_call，零输出）；
 *   · 判据台账 id 用**显式列表**（`arg(i)` 生成的 "HC-1" ≠ note 首词 "HC-01"，
 *     前 9 条 mark 会静默丢失）。
 * 🔴 HelpModel 用 **main.cpp 传入的、被 QML 绑定的那个实例**（陷阱 84：判据自建
 *   副本 ≠ 接线实例 —— 自建副本永远有数据，QML 是空表，判据照样绿）。
 */
#include "app/HelpCheck.hpp"

#include "app/ActionRouter.hpp"
#include "app/HelpModel.hpp"

#include <QApplication>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDialog>
#include <QFile>
#include <QKeySequence>
#include <QMouseEvent>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSet>
#include <QSettings>
#include <QTimer>
#include <QVector>
#include <QWidget>

#include <algorithm>
#include <functional>
#include <memory>

#if defined(STELQUICK_HAS_ENGINE)
#include "core/StelApp.hpp"
#include "core/StelUtils.hpp"
#include "gui/ContributorsList.hpp"
#endif

namespace stelapp {

namespace {

using CheckResult = HelpCheck::Result;

#if defined(STELQUICK_HAS_ENGINE) && defined(STELQUICK_WIDGETS_HOST)

// ── 工具（ShortcutCheck 同款；各 check 自带一份，不跨文件共享匿名命名空间）────

QMap<QString, QVariant> dumpSettings(QSettings *s, const QString &prefix = QString())
{
    QMap<QString, QVariant> out;
    const QStringList keys = s->allKeys();
    for (const QString &k : keys)
        out.insert(prefix + k, s->value(k));
    const QStringList groups = s->childGroups();
    for (const QString &g : groups)
    {
        s->beginGroup(g);
        const QMap<QString, QVariant> sub = dumpSettings(s, prefix + g + QLatin1Char('/'));
        s->endGroup();
        for (auto it = sub.constBegin(); it != sub.constEnd(); ++it)
            out.insert(it.key(), it.value());
    }
    return out;
}

int countDiff(const QMap<QString, QVariant> &before, const QMap<QString, QVariant> &after)
{
    int diff = 0;
    for (auto it = after.constBegin(); it != after.constEnd(); ++it)
    {
        const auto b = before.constFind(it.key());
        if (b == before.constEnd() || b.value() != it.value())
            ++diff;
    }
    for (auto it = before.constBegin(); it != before.constEnd(); ++it)
        if (!after.contains(it.key()))
            ++diff;
    return diff;
}

//! 视觉树里 objectName 相符的全部项（陷阱 45：一律 childItems 递归，findChild 扫不到）。
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

//! 几何覆盖（陷阱 47：判"点击砸没砸中"要看落点是否在按钮及**全部祖先**矩形内）。
bool pointCoveredByAncestors(QQuickItem *item, const QPointF &scenePos)
{
    for (QQuickItem *it = item; it; it = it->parentItem())
    {
        const QPointF p = it->mapFromScene(scenePos);
        if (p.x() < 0 || p.y() < 0 || p.x() > it->width() || p.y() > it->height())
            return false;
    }
    return true;
}

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

QPointF itemCenter(QQuickItem *it)
{
    return it ? QPointF(it->width() / 2.0, it->height() / 2.0) : QPointF();
}

//! 找导航/页内按钮并**真实点击**（几何覆盖先验；返回 false = 找不到或落点存疑）。
bool clickButton(QQuickWindow *window, const QString &objectName, QString *why = nullptr)
{
    const auto items = collectByObjectName(window->contentItem(), objectName);
    if (items.isEmpty())
    {
        if (why)
            *why = QStringLiteral("视觉树里找不到 %1").arg(objectName);
        return false;
    }
    QQuickItem *btn = items.first();
    const QPointF scene = btn->mapToScene(itemCenter(btn));
    if (!pointCoveredByAncestors(btn, scene))
    {
        if (why)
            *why = QStringLiteral("%1 的中心点 (%2,%3) 未被按钮及祖先矩形覆盖（陷阱 47）")
                       .arg(objectName)
                       .arg(scene.x())
                       .arg(scene.y());
        return false;
    }
    return clickAt(window, scene);
}

//! pageStack 的 currentIndex（返回环判据的既定锚点）。
int stackIndex(QQuickWindow *window)
{
    const QQuickItem *stack = window->findChild<QQuickItem *>(QStringLiteral("pageStack"));
    return stack ? stack->property("currentIndex").toInt() : -1;
}

int widgetCount()
{
    return QApplication::allWidgets().size();
}

//! 可见顶层窗口快照（类名集合，排序）。HC-17 用 —— 隐藏 widget 不算（S12 判别对照
//! 会留下 46 个隐藏 widget 常驻，那是 Q14 已证的老对话框惰性析构行为，不是回归）。
QStringList visibleTops()
{
    QStringList out;
    for (QWidget *w : QApplication::allWidgets())
    {
        if (!w || !w->isWindow() || !w->isVisible())
            continue;
        const QString cn = QString::fromLatin1(w->metaObject()->className());
        // ⚠️ 排除启动画面瞬态（QSplashScreen / QProgressBar）：引擎引导完成后它们
        // 会消失 —— 基线若混入它们，复原快照必然不同（首轮 HC-17 就这么红过，
        // 纯属布场时机问题，不是产品回归）。
        if (cn == QLatin1String("QSplashScreen") || cn == QLatin1String("QProgressBar"))
            continue;
        out << cn;
    }
    out.sort();
    return out;
}

//! 把 objectName 对应的控件滚进 Flickable 视口（T39 教训：isVisible() 不看视口
//! 裁剪 —— 控件"可见"但真实点击点不到；首轮 HC-11/HC-13 的按钮中心 y=1036/879，
//! 窗口只有 640 高）。返回：空串=成功（含"本来就在视口"）；非空=失败原因。
QString ensureVisible(QQuickWindow *window, const QString &objectName)
{
    const auto items = collectByObjectName(window->contentItem(), objectName);
    if (items.isEmpty())
        return QStringLiteral("视觉树里找不到 %1").arg(objectName);
    QQuickItem *target = items.first();
    // 沿父链找 Flickable（Qt 6 的 ScrollView 内部就是 QQuickFlickable）
    QQuickItem *flick = nullptr;
    for (QQuickItem *it = target->parentItem(); it; it = it->parentItem())
    {
        if (QString::fromLatin1(it->metaObject()->className())
                .contains(QLatin1String("Flickable")))
        {
            flick = it;
            break;
        }
    }
    if (!flick)
        return QString();   // 不在滚动容器里 ⇒ 本来就全可见
    // mapToItem 给的是**视口坐标**（content item 的 y = -contentY 已被算进去）
    const QPointF p = target->mapToItem(flick, QPointF(0, 0));
    const qreal h = flick->height();
    if (p.y() >= 0 && p.y() + target->height() <= h)
        return QString();   // 已在视口
    const qreal contentH = flick->property("contentHeight").toReal();
    qreal newCY = flick->property("contentY").toReal()
                  + p.y() - (h - target->height()) / 2.0;
    newCY = qBound<qreal>(0.0, newCY, qMax<qreal>(0.0, contentH - h));
    flick->setProperty("contentY", newCY);
    return QStringLiteral("滚动了（contentY → %1）").arg(newCY);
}

struct Ctx
{
    QCoreApplication *app = nullptr;
    QQuickWindow *window = nullptr;
    ActionRouter *router = nullptr;
    HelpModel *model = nullptr;          // 🔴 被 QML 绑定的实例（main.cpp 传入）
    std::function<void(const CheckResult &)> onDone;
    CheckResult result;
    QStringList lines;

    QSettings *conf = nullptr;
    QMap<QString, QVariant> baseAll;
    QStringList baseTops;
    int hostSignals = 0;

    struct Item
    {
        bool done = false;
        bool pass = false;
        bool na = false;
    };
    QVector<QPair<QString, Item>> items;
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
                             ? QStringLiteral("帮助/版本/许可证自检：INCONCLUSIVE（%1 条测不出，%2/%3 过）")
                                   .arg(inconclusiveCount)
                                   .arg(passed)
                                   .arg(judged)
                             : QStringLiteral("帮助/版本/许可证自检：判据 %1/%2").arg(passed).arg(judged);
        result.details = lines;
        onDone(result);
    }

    void note(const QString &line) { lines.append(line); }
};

struct Step
{
    int delayAfter = 0;
    std::function<void(Ctx *)> body;
};

//! 在 rows（QVariantList of {label,value}）里按 label 取 value；缺行返回空串。
QString rowValue(const QVariantList &rows, const QString &label)
{
    for (const QVariant &v : rows)
    {
        const QVariantMap m = v.toMap();
        if (m.value(QStringLiteral("label")).toString() == label)
            return m.value(QStringLiteral("value")).toString();
    }
    return QString();
}

#endif  // STELQUICK_HAS_ENGINE && STELQUICK_WIDGETS_HOST

} // namespace

void HelpCheck::run(QCoreApplication *app,
                    QQuickWindow *window,
                    ActionRouter *router,
                    HelpModel *model,
                    const std::function<void(const Result &)> &onDone,
                    int delayMs)
{
#if !defined(STELQUICK_HAS_ENGINE) || !defined(STELQUICK_WIDGETS_HOST)
    Q_UNUSED(app)
    Q_UNUSED(window)
    Q_UNUSED(router)
    Q_UNUSED(model)
    Q_UNUSED(delayMs)
    Result r;
    r.unavailable = true;
    r.summary = QStringLiteral("帮助/版本/许可证自检需要合流形态构建");
    onDone(r);
#else
    if (!StelApp::isInitialized())
    {
        Result r;
        r.unavailable = true;
        r.summary = QStringLiteral("引擎未引导 ⇒ 版本面拿不到，帮助自检无从断言");
        onDone(r);
        return;
    }

    auto ctx = new Ctx();
    ctx->app = app;
    ctx->window = window;
    ctx->router = router;
    ctx->model = model;
    // 🔴 必填（陷阱 83）：finish() 用的是 **Ctx::onDone** —— 漏赋值的形态是
    // "全部步骤跑完、收尾 std::bad_function_call、零判据输出（RC=134）"。
    ctx->onDone = onDone;
    ctx->result.ran = true;
    // 🔴 台账 id 显式列表（陷阱 83）：必须与各 note 首词**逐字一致**。
    {
        const QStringList ids{
            QStringLiteral("HC-01"), QStringLiteral("HC-02"), QStringLiteral("HC-03"),
            QStringLiteral("HC-04"), QStringLiteral("HC-05"), QStringLiteral("HC-06"),
            QStringLiteral("HC-07"), QStringLiteral("HC-08"), QStringLiteral("HC-09"),
            QStringLiteral("HC-10"), QStringLiteral("HC-11"), QStringLiteral("HC-12"),
            QStringLiteral("HC-13"), QStringLiteral("HC-14"), QStringLiteral("HC-15"),
            QStringLiteral("HC-16"), QStringLiteral("HC-17")};
        for (const QString &id : ids)
            ctx->items.append(qMakePair(id, Ctx::Item()));
    }

    // hostActionRequested 计数（HC-15/16 用）。⚠️ 用 **3 参数** connect：4 参数版的
    // context 必须是 QObject，而 Ctx 不是（首轮编译在此报 no matching function）。
    // ctx 的生存期覆盖整个自检（进程退出前不会析构），router 是 main 的栈对象
    // 且更晚析构 ⇒ 不会出现"信号打到已销毁的 ctx"。
    QObject::connect(router, &ActionRouter::hostActionRequested,
                     [ctx](const QString &) { ++ctx->hostSignals; });

    auto steps = std::make_shared<QVector<Step>>();
    // ⚠️ tick 的签名是 **void(int)**（下一步序号）—— 首轮写成 void()，
    // `(*tick)(i + 1)` 传参不匹配（no matching function for call to object）。
    auto tick = std::make_shared<std::function<void(int)>>();

    auto runStep = [steps, ctx, tick](int i) {
        if (i >= steps->size())
        {
            ctx->finish();
            return;
        }
        const Step &st = steps->at(i);
        st.body(ctx);
        QTimer::singleShot(qMax(1, st.delayAfter), [tick, i]() { (*tick)(i + 1); });
    };
    *tick = [runStep](int i) { runStep(i); };
    QTimer::singleShot(delayMs, [runStep]() { runStep(0); });

    // ── S1 布场（全程只读：记配置指纹 + widget 基线 + 可见顶层快照）──────────
    steps->append(Step{1, [delayMs](Ctx *c) {
        c->conf = StelApp::getInstance().getSettings();
        c->baseAll = dumpSettings(c->conf);
        // 🔴 用**被 QML 绑定的实例**（main.cpp 已 refresh 过；再调一次幂等）。
        c->model->refresh();
        c->baseTops = visibleTops();
        c->note(QStringLiteral("布场：配置 %1（%2 键）｜可见顶层 %3｜versionRows %4 行｜"
                               "贡献者 %5｜手势 %6（legacy %7）｜接管 %8 个｜判据步延迟 %9ms")
                    .arg(c->conf->fileName())
                    .arg(c->baseAll.size())
                    .arg(c->baseTops.join(QLatin1Char(',')))
                    .arg(c->model->versionRows().size())
                    .arg(c->model->contributorCount())
                    .arg(c->model->gestures().size())
                    .arg(c->model->legacyGestureCount())
                    .arg(c->router->hostTakeoverIds().size())
                    .arg(delayMs));
    }});

    // ── S2 HC-01 版本行（含"源分离"判别对照）─────────────────────────────────
    steps->append(Step{0, [](Ctx *c) {
        const QVariantList rows = c->model->versionRows();
        const QString full = StelUtils::getApplicationVersion();
        bool ok = !rows.isEmpty()
                  && rowValue(rows, QStringLiteral("完整版本")) == full;
#ifdef GIT_REVISION
        ok = ok && rowValue(rows, QStringLiteral("Git 修订")) == QStringLiteral(GIT_REVISION);
#endif
        // 判别对照：`QCoreApplication::applicationVersion()`（探针 Q4 实测 = "1.0.0"，
        // 没被设成产品版本）**不得等于**完整版本 —— 物证"版本行走的是 StelUtils 那条
        // 源、不是 applicationVersion"。⚠️ 若未来有人把 applicationVersion 设成产品
        // 版本，这条会红 —— 那时产品没错，是对照失效了，更新对照即可。
        ok = ok && QCoreApplication::applicationVersion() != full;
        c->mark(ok, QStringLiteral("HC-01 版本行：%1 行｜完整版本「%2」== StelUtils｜"
                                  "Git 修订一致｜applicationVersion「%3」≠ 完整版本（源分离）")
                          .arg(rows.size())
                          .arg(full)
                          .arg(QCoreApplication::applicationVersion()));
    }});

    // ── S3 HC-02 系统行 ─────────────────────────────────────────────────────
    steps->append(Step{0, [](Ctx *c) {
        const QVariantList rows = c->model->systemRows();
        bool ok = !rows.isEmpty()
                  && rowValue(rows, QStringLiteral("操作系统"))
                         == StelUtils::getOperatingSystemInfo()
                  && rowValue(rows, QStringLiteral("Qt（编译期）")) == QStringLiteral(QT_VERSION_STR)
                  && rowValue(rows, QStringLiteral("Qt（运行期）")) == QString::fromLatin1(qVersion())
                  && rowValue(rows, QStringLiteral("Qt（编译期）"))
                         == rowValue(rows, QStringLiteral("Qt（运行期）"));
        c->mark(ok, QStringLiteral("HC-02 系统行：%1 行｜OS「%2」｜Qt 编译期==运行期==%3")
                          .arg(rows.size())
                          .arg(StelUtils::getOperatingSystemInfo())
                          .arg(QStringLiteral(QT_VERSION_STR)));
    }});

    // ── S4 HC-03 贡献者（排序口径 = 大小写不敏感；先去重后排序）────────────────
    steps->append(Step{0, [](Ctx *c) {
        const QStringList raw = StelContributors::contributorsList;
        QStringList expect = raw;
        expect.removeDuplicates();
        std::sort(expect.begin(), expect.end(),
                  [](const QString &a, const QString &b) {
                      return a.compare(b, Qt::CaseInsensitive) < 0;
                  });
        const QStringList got = c->model->contributors();
        QSet<QString> uniq(got.begin(), got.end());
        bool hasNonAscii = false;
        for (const QString &s : got)
        {
            for (const QChar ch : s)
                if (ch.unicode() > 127)
                {
                    hasNonAscii = true;
                    break;
                }
            if (hasNonAscii)
                break;
        }
        const bool ok = got == expect
                        && !got.isEmpty()
                        && uniq.size() == got.size()
                        && got.first() == QStringLiteral("adalava")
                        && hasNonAscii;
        c->mark(ok, QStringLiteral("HC-03 贡献者：%1（原表 %2）｜无重复｜首项「%3」"
                                  "（大小写不敏感排序）｜非 ASCII 名在场")
                          .arg(got.size())
                          .arg(raw.size())
                          .arg(got.value(0)));
    }});

    // ── S5 HC-04 / HC-05 许可证（内容 + 资源与磁盘原文逐字节一致）──────────────
    steps->append(Step{0, [](Ctx *c) {
        const bool hc04 = c->model->licenseLoaded()
                          && c->model->licenseError().isEmpty()
                          && c->model->licenseLineCount() > 300
                          && c->model->licenseText().contains(QLatin1String("GNU GENERAL PUBLIC LICENSE"))
                          && c->model->licenseText().contains(QLatin1String("Version 2"));
        c->mark(hc04, QStringLiteral("HC-04 许可证内容：loaded=%1｜%2 行｜含 GPL 标题与 Version 2")
                                .arg(c->model->licenseLoaded())
                                .arg(c->model->licenseLineCount()));

        // 资源路径一致性 + 与源码树磁盘 COPYING 比对（__FILE__ 推根，HelpProbe Q5 同款）
        const QString res = c->model->licenseResource();
        QString why;
        bool hc05 = res == QLatin1String(":/StelQuickUI/COPYING");
        QByteArray md5Res;
        if (hc05)
        {
            QFile rf(res);
            if (rf.open(QIODevice::ReadOnly))
                md5Res = QCryptographicHash::hash(rf.readAll(), QCryptographicHash::Md5);
            else
                why = QStringLiteral("资源 %1 打不开：%2").arg(res, rf.errorString());
        }
        const QString thisFile = QStringLiteral(__FILE__);
        const int pos = thisFile.indexOf(QLatin1String("/src/app/HelpCheck.cpp"));
        if (hc05 && pos > 0)
        {
            QFile df(thisFile.left(pos) + QStringLiteral("/COPYING"));
            if (df.open(QIODevice::ReadOnly))
            {
                const QByteArray md5Disk =
                    QCryptographicHash::hash(df.readAll(), QCryptographicHash::Md5);
                hc05 = md5Res == md5Disk;
                if (!hc05)
                    why = QStringLiteral("qrc 内容与磁盘 COPYING md5 不一致");
            }
            else
            {
                hc05 = false;
                why = QStringLiteral("磁盘 %1 打不开（__FILE__ 推根失败？）").arg(df.fileName());
            }
        }
        else if (hc05)
        {
            hc05 = false;
            why = QStringLiteral("__FILE__ 不是预期的 /src/app/ 形态：%1").arg(thisFile);
        }
        c->mark(hc05, QStringLiteral("HC-05 许可证资源：%1｜md5 == 磁盘 COPYING（%2）")
                                .arg(res, why.isEmpty() ? QStringLiteral("一致") : why));
    }});

    // ── S6 HC-06 / HC-07 / HC-08 手势表 + 外链 + 键名平台化 ────────────────────
    steps->append(Step{0, [](Ctx *c) {
        const QVariantList gs = c->model->gestures();
        int sky = 0;
        int legacy = 0;
        bool scopeOk = true;
        for (const QVariant &v : gs)
        {
            const QString s = v.toMap().value(QStringLiteral("scope")).toString();
            if (s == QLatin1String("sky"))
                ++sky;
            else if (s == QLatin1String("legacy"))
                ++legacy;
            else
                scopeOk = false;
        }
        const bool hc06 = scopeOk && !gs.isEmpty()
                          && sky + legacy == gs.size()
                          && legacy == 10   // 脚本 3 + 天文计算 7（常量表，变了就该醒）
                          && sky == 13;
        c->mark(hc06, QStringLiteral("HC-06 手势表：%1 条（sky %2 + legacy %3）｜scope 全合法")
                                .arg(gs.size()).arg(sky).arg(legacy));

        const QVariantList links = c->model->webLinks();
        bool allHttps = !links.isEmpty();
        QString firstUrl;
        for (const QVariant &v : links)
        {
            const QString url = v.toMap().value(QStringLiteral("url")).toString();
            if (!url.startsWith(QLatin1String("https://")))
                allHttps = false;
            if (firstUrl.isEmpty())
                firstUrl = url;
        }
        // 判别对照：白名单**外**的 URL 必拒
        const bool hc07 = links.size() == 7 && allHttps
                          && c->model->isAllowedExternalUrl(firstUrl)
                          && !c->model->isAllowedExternalUrl(QStringLiteral("https://example.com/"));
        c->mark(hc07, QStringLiteral("HC-07 外链：%1 条全 https｜白名单内放行｜表外 URL 必拒")
                                .arg(links.size()));

        const QString ctrl = QKeySequence(Qt::CTRL).toString(QKeySequence::NativeText);
        bool hasCtrl = !ctrl.isEmpty();
        for (const QVariant &v : gs)
            if (v.toMap().value(QStringLiteral("keys")).toString().contains(ctrl))
            {
                hasCtrl = true;
                break;
            }
        const bool hc08 = hasCtrl;
        c->mark(hc08, QStringLiteral("HC-08 键名平台化：至少一条含 NativeText 修饰键「%1」"
                                     "（键名由 C++ 生成的物证）")
                                .arg(ctrl));
    }});

    // ── S7 HC-09 导航按钮（含"原有 8 个一个不少"回归）──────────────────────────
    steps->append(Step{0, [](Ctx *c) {
        QQuickItem *root = c->window->contentItem();
        const QStringList originals{
            QStringLiteral("skyReturnButton"), QStringLiteral("navDiagButton"),
            QStringLiteral("navSkyButton"), QStringLiteral("navSearchButton"),
            QStringLiteral("navTimeButton"), QStringLiteral("navLocationButton"),
            QStringLiteral("navDisplayButton"), QStringLiteral("navShortcutsButton")};
        QStringList missing;
        for (const QString &n : originals)
            if (collectByObjectName(root, n).size() != 1)
                missing << n;
        const bool ok = collectByObjectName(root, QStringLiteral("navHelpButton")).size() == 1
                        && collectByObjectName(root, QStringLiteral("navAboutButton")).size() == 1
                        && missing.isEmpty();
        c->mark(ok, QStringLiteral("HC-09 导航按钮：navHelpButton/navAboutButton 在场｜"
                                  "原有 %1 个导航按钮%2")
                              .arg(originals.size())
                              .arg(missing.isEmpty() ? QStringLiteral("一个不少")
                                                     : QStringLiteral("缺失：") + missing.join(',')));
    }});

    // ── S8 真实点击导航切到帮助页（单独一步 + 400ms：陷阱 45/84）────────────────
    steps->append(Step{400, [](Ctx *c) {
        QString why;
        if (!clickButton(c->window, QStringLiteral("navHelpButton"), &why))
        {
            c->markNa(QStringLiteral("HC-10 帮助页控件：切页点击失败（%1）").arg(why));
            return;
        }
        c->note(QStringLiteral("切页：真实点击 navHelpButton ⇒ pageStack.currentIndex=%1")
                    .arg(stackIndex(c->window)));
    }});

    // ── S9 HC-10 帮助页控件齐备 ─────────────────────────────────────────────
    steps->append(Step{0, [](Ctx *c) {
        QQuickItem *root = c->window->contentItem();
        const int expectRows = c->model->gestures().size();
        int gestureRows = 0;
        for (int i = 0; i < expectRows + 5; ++i)
            if (!collectByObjectName(root, QStringLiteral("helpGestureRow_%1").arg(i)).isEmpty())
                ++gestureRows;
        int linkRows = 0;
        for (int i = 0; i < 10; ++i)
            if (!collectByObjectName(root, QStringLiteral("helpLinkRow_%1").arg(i)).isEmpty())
                ++linkRows;
        // 可见 legacy 标记数（不是数 objectName —— 那对所有行都存在，数不出真假）
        int visibleLegacy = 0;
        for (int i = 0; i < expectRows + 5; ++i)
        {
            const auto items =
                collectByObjectName(root, QStringLiteral("helpGestureLegacy_%1").arg(i));
            if (!items.isEmpty() && items.first()->isVisible())
                ++visibleLegacy;
        }
        const auto gotoBtn =
            collectByObjectName(root, QStringLiteral("helpGotoShortcutsButton"));
        const auto summary = collectByObjectName(root, QStringLiteral("helpSummaryLabel"));
        const bool ok = gestureRows == expectRows
                        && linkRows == c->model->webLinks().size()
                        && visibleLegacy == c->model->legacyGestureCount()
                        && !gotoBtn.isEmpty() && !summary.isEmpty()
                        && !summary.first()->property("text").toString().isEmpty();
        c->mark(ok, QStringLiteral("HC-10 帮助页控件：手势行 %1/%2｜外链行 %3/%4｜"
                                  "可见 legacy 标记 %5/%6｜跳转按钮与摘要在场")
                              .arg(gestureRows).arg(expectRows)
                              .arg(linkRows).arg(c->model->webLinks().size())
                              .arg(visibleLegacy).arg(c->model->legacyGestureCount()));
    }});

    // ── S10 HC-11 跳转快捷键页（真实点击 → 同步读 currentIndex → 再点回）────────
    steps->append(Step{0, [](Ctx *c) {
        // 首轮教训：按钮沉底时中心点 y=1036 > 窗口 640，真实点击点不到（陷阱 47
        // 的几何覆盖先验拦住了）⇒ 产品侧已把按钮挪到页首；这里再保险滚一次
        //（已在视口时是 no-op，防更矮的窗口）。
        const QString scrolled =
            ensureVisible(c->window, QStringLiteral("helpGotoShortcutsButton"));
        if (!scrolled.isEmpty())
            c->note(QStringLiteral("HC-11 预滚：%1").arg(scrolled));
        QString why;
        if (!clickButton(c->window, QStringLiteral("helpGotoShortcutsButton"), &why))
        {
            c->markNa(QStringLiteral("HC-11 跳转快捷键页：点击失败（%1）").arg(why));
            return;
        }
        const int afterJump = stackIndex(c->window);
        // 回帮助页（恢复现场，也证按钮是可复用的）
        clickButton(c->window, QStringLiteral("navHelpButton"));
        const int afterBack = stackIndex(c->window);
        c->mark(afterJump == 6 && afterBack == 7,
                QStringLiteral("HC-11 跳转快捷键页：点击后 index=%1（期望 6）｜点回帮助 index=%2"
                               "（期望 7）")
                    .arg(afterJump)
                    .arg(afterBack));
    }});

    // ── S11 真实点击切到关于页（单独一步 + 400ms）────────────────────────────
    steps->append(Step{400, [](Ctx *c) {
        QString why;
        if (!clickButton(c->window, QStringLiteral("navAboutButton"), &why))
        {
            c->markNa(QStringLiteral("HC-12 关于页控件：切页点击失败（%1）").arg(why));
            return;
        }
        c->note(QStringLiteral("切页：真实点击 navAboutButton ⇒ pageStack.currentIndex=%1")
                    .arg(stackIndex(c->window)));
    }});

    // ── S12 HC-12 关于页控件 ────────────────────────────────────────────────
    steps->append(Step{0, [](Ctx *c) {
        QQuickItem *root = c->window->contentItem();
        auto countPrefix = [root](const QString &prefix, int upTo) {
            int n = 0;
            for (int i = 0; i < upTo; ++i)
                if (!collectByObjectName(root, prefix + QString::number(i)).isEmpty())
                    ++n;
            return n;
        };
        const int verRows = countPrefix(QStringLiteral("aboutVersionRow_"),
                                        c->model->versionRows().size() + 5);
        const int sysRows = countPrefix(QStringLiteral("aboutSystemRow_"),
                                        c->model->systemRows().size() + 5);
        // 贡献者 ListView（惰性 ⇒ 切页 + 400ms 后应有 delegate；陷阱 84 的场景）
        int contributorDelegates = 0;
        QString firstContributor;
        for (int i = 0; i < 8; ++i)
        {
            const auto it =
                collectByObjectName(root, QStringLiteral("aboutContributor_") + QString::number(i));
            if (!it.isEmpty())
            {
                ++contributorDelegates;
                if (firstContributor.isEmpty())
                    firstContributor = it.first()->property("text").toString();
            }
        }
        const bool ok = verRows == c->model->versionRows().size() && verRows > 0
                        && sysRows == c->model->systemRows().size() && sysRows > 0
                        && contributorDelegates > 0
                        && firstContributor.contains(QLatin1String("adalava"));
        c->mark(ok, QStringLiteral("HC-12 关于页控件：版本行 %1/%2｜系统行 %3/%4｜"
                                  "贡献者 delegate %5 条、首条「%6」")
                              .arg(verRows).arg(c->model->versionRows().size())
                              .arg(sysRows).arg(c->model->systemRows().size())
                              .arg(contributorDelegates).arg(firstContributor));
    }});

    // ── S12b 滚动使许可证按钮入视口（单独一步 + 400ms：滚动后要等一帧）────────
    // 首轮教训：许可证按钮中心 y=879 > 窗口 640 ⇒ 真实点击点不到（HC-13 NA）。
    // 这里与 HC-11 不同 —— 按钮在关于页中下部是**合理的布局**（上面是版本/系统），
    // 所以修仪器（滚动）而不是修产品。
    steps->append(Step{400, [](Ctx *c) {
        const QString r = ensureVisible(c->window, QStringLiteral("aboutLicenseButton"));
        c->note(QStringLiteral("HC-13 预滚：%1")
                    .arg(r.isEmpty() ? QStringLiteral("已在视口") : r));
    }});

    // ── S13 HC-13 许可证弹窗端到端 ──────────────────────────────────────────
    steps->append(Step{0, [](Ctx *c) {
        QString why;
        if (!clickButton(c->window, QStringLiteral("aboutLicenseButton"), &why))
        {
            c->markNa(QStringLiteral("HC-13 许可证端到端：按钮点击失败（%1）").arg(why));
            return;
        }
        QObject *dialog = c->window->findChild<QObject *>(QStringLiteral("aboutLicenseDialog"));
        if (!dialog)
        {
            c->markNa(QStringLiteral("HC-13 许可证端到端：aboutLicenseDialog 找不到"
                                     "（Popup 不在视觉树，但应在 QObject 父链上）"));
            return;
        }
        const bool visible = dialog->property("visible").toBool();
        // UI 文本（判据读 UI 自己的 TextArea —— T40 SC-13 教训：别拿孤立副本当参照）
        QString uiText;
        const auto area =
            collectByObjectName(c->window->contentItem(), QStringLiteral("aboutLicenseTextArea"));
        if (!area.isEmpty())
            uiText = area.first()->property("text").toString();
        const bool textOk = uiText == c->model->licenseText() && !uiText.isEmpty();
        // 关掉（恢复现场）
        QMetaObject::invokeMethod(dialog, "close");
        c->mark(visible && textOk,
                QStringLiteral("HC-13 许可证端到端：Dialog visible=%1｜UI 文本 %2 字符 "
                               "== 数据面 licenseText（%3）")
                    .arg(visible)
                    .arg(uiText.size())
                    .arg(textOk ? QStringLiteral("一致") : QStringLiteral("不一致")));
    }});

    // ── S14 HC-14 接管表内容 ──────────────────────────────────────────────────
    // ⚠️ T42 口径校正（下游补新能力后旧判据口径必须跟着走，TRAPS 11 镜像）：
    //    T41 时代预期 6 个 + "F2/F10/F12 刻意未接管"；**T42 把这 4 个（含 ⌥B）
    //    接管 + 弹"未支持"提示**（接管表真身 10 个）⇒ 预期改为 10 个全量，
    //    "未接管对照"反转为"T42 的 4 个已接管"（不再存在刻意未接管的窗口动作）。
    steps->append(Step{0, [](Ctx *c) {
        const QStringList got = c->router->hostTakeoverIds();
        QStringList expect{
            QStringLiteral("actionShow_DateTime_Window_Global"),
            QStringLiteral("actionShow_Help_Window_Global"),
            QStringLiteral("actionShow_Location_Window_Global"),
            QStringLiteral("actionShow_Search_Window_Global"),
            QStringLiteral("actionShow_Shortcuts_Window_Global"),
            QStringLiteral("actionShow_SkyView_Window_Global"),
            QStringLiteral("actionShow_Configuration_Window_Global"),
            QStringLiteral("actionShow_AstroCalc_Window_Global"),
            QStringLiteral("actionShow_ScriptConsole_Window_Global"),
            QStringLiteral("actionShow_ObsList_Window_Global")};
        std::sort(expect.begin(), expect.end());
        const bool t42FourOn =
            c->router->isHostTakeover(QStringLiteral("actionShow_Configuration_Window_Global"))
            && c->router->isHostTakeover(QStringLiteral("actionShow_AstroCalc_Window_Global"))
            && c->router->isHostTakeover(QStringLiteral("actionShow_ScriptConsole_Window_Global"))
            && c->router->isHostTakeover(QStringLiteral("actionShow_ObsList_Window_Global"));
        c->mark(got == expect && t42FourOn,
                QStringLiteral("HC-14 接管表：%1 个 == 预期 10（T42 后全量，%2）｜"
                               "T42 四动作（F2/F10/F12/⌥B）已接管=%3")
                    .arg(got.size())
                    .arg(got.join(QLatin1Char(',')))
                    .arg(t42FourOn));
    }});

    // ── S15 切回天空页（给 HC-15/16 的 index 断言以判别力）────────────────────
    steps->append(Step{100, [](Ctx *c) {
        clickButton(c->window, QStringLiteral("navSkyButton"));
        c->note(QStringLiteral("切页：真实点击 navSkyButton ⇒ pageStack.currentIndex=%1")
                    .arg(stackIndex(c->window)));
    }});

    // ── S16 HC-15 接管生效（trigger 路径 + 判别对照）──────────────────────────
    steps->append(Step{0, [](Ctx *c) {
        const QString helpId = QStringLiteral("actionShow_Help_Window_Global");
        const int signalsBefore = c->hostSignals;
        const int n0 = widgetCount();
        const bool triggered = c->router->trigger(helpId);
        const int idx = stackIndex(c->window);
        const int n1 = widgetCount();
        const bool positive = triggered
                              && c->hostSignals == signalsBefore + 1
                              && idx == 7          // 接管 → QML 导航到 help
                              && n1 == n0;         // 且**没有**弹出老对话框
        c->mark(positive, QStringLiteral("HC-15 接管生效（trigger）：triggered=%1｜hostActionRequested "
                                         "Δ=%2（%3→%4）｜index=%5｜QWidget %6→%7（Δ%8，应为 0）")
                              .arg(triggered)
                              .arg(c->hostSignals - signalsBefore)
                              .arg(signalsBefore).arg(c->hostSignals)
                              .arg(idx)
                              .arg(n0).arg(n1).arg(n1 - n0));

        // ── 判别对照：撤销接管 ⇒ 老 HelpDialog 真的弹出来（Q14 的产品级复验）──
        // 只有证到"不接管会弹"，上面的"没弹"才是接管在起作用（而不是弹不出来）。
        // ⚠️ 恢复只恢复到**布场时的状态**：负控 A（TAKEOVER_OFF）下布场就没有
        //   接管 —— 若这里无条件 setHostTakeover(true)，判据会**自己把被负控
        //   打断的路径修好** ⇒ HC-16 假绿（首轮实抓：负控 A 下 HC-16 显示
        //   hostActionRequested 0→1、index=7，看着像接管生效，其实是判据刚
        //   注册的接管在干活 —— "诊断不得污染被诊断的量"的又一面）。
        const bool wasTakeover = c->router->isHostTakeover(helpId);
        c->router->setHostTakeover(helpId, false);
        const int m0 = widgetCount();
        c->router->trigger(helpId);   // 现在会透传引擎 → 老 HelpDialog 弹出
        const int m1 = widgetCount();
        // 还原：关掉弹出的对话框 + 恢复到布场态
        for (QWidget *w : QApplication::allWidgets())
            if (w && w->isWindow() && w->isVisible() && w->inherits("QDialog"))
                w->close();
        if (wasTakeover)
            c->router->setHostTakeover(helpId, true);
        c->mark(m1 > m0,
                QStringLiteral("HC-15 判别对照：撤销接管后 trigger ⇒ QWidget %1→%2（Δ%3>0，"
                               "老对话框真的弹出）⇒ 正题的 Δ=0 是接管的功劳；已关对话框并恢复接管")
                    .arg(m0).arg(m1).arg(m1 - m0));
    }});

    // ── S17 HC-16 键盘路径接管（routeKey F1）────────────────────────────────
    steps->append(Step{0, [](Ctx *c) {
        // S16 结束时栈在 help 页（接管触发的导航）⇒ 先切回天空，index 断言才有判别力
        clickButton(c->window, QStringLiteral("navSkyButton"));
        if (!c->router->canDispatchToSky())
        {
            c->markNa(QStringLiteral("HC-16 键盘路径：焦点在可编辑控件，routeKey 守卫拒收"
                                     "（布场问题，非产品问题）"));
            return;
        }
        const int signalsBefore = c->hostSignals;
        const int n0 = widgetCount();
        const bool routed = c->router->routeKey(Qt::Key_F1, 0);
        const int idx = stackIndex(c->window);
        const int n1 = widgetCount();
        // 清理：若本步透传了（= 负控 A 下必然发生），会留下**可见**的老对话框
        // ⇒ 不清的话 HC-17 的"可见顶层回基线"会连带红，那根因其实是本步而不是复原。
        // 每步自己收尾，后面的判据才只反映自己那件事。
        for (QWidget *w : QApplication::allWidgets())
            if (w && w->isWindow() && w->isVisible() && w->inherits("QDialog"))
                w->close();
        const bool ok = routed && c->hostSignals == signalsBefore + 1
                        && idx == 7 && n1 == n0;
        c->mark(ok, QStringLiteral("HC-16 键盘路径：routeKey(F1) 命中=%1｜hostActionRequested "
                                   "Δ=%2（%3→%4）｜index=%5｜QWidget Δ=%6（应为 0）")
                              .arg(routed)
                              .arg(c->hostSignals - signalsBefore)
                              .arg(signalsBefore).arg(c->hostSignals)
                              .arg(idx).arg(n1 - n0));
    }});

    // ── S18 HC-17 复原：配置零污染 + 可见顶层回基线 ───────────────────────────
    steps->append(Step{0, [](Ctx *c) {
        const int diff = countDiff(c->baseAll, dumpSettings(c->conf));
        const QStringList tops = visibleTops();
        const bool ok = diff == 0 && tops == c->baseTops;
        c->mark(ok, QStringLiteral("HC-17 复原：配置指纹差异 %1（应为 0，本自检全程只读）｜"
                                   "可见顶层%2（基线 %3）")
                              .arg(diff)
                              .arg(tops == c->baseTops ? QStringLiteral("回基线")
                                                       : QStringLiteral("异常：") + tops.join(','))
                              .arg(c->baseTops.join(QLatin1Char(','))));
    }});

#endif
}

} // namespace stelapp
