/*
 * ShortcutCheck — 实现（T40-C 快捷键编辑自检）。判据清单与设计依据见头注。
 *
 * 驱动方式沿用 DisplayCheck / NightModeCheck / HiDpiCheck 的线性步骤表。
 * ⚠️ delayAfter 语义 = "跑完**本步**之后的等待"（陷阱 41/69）：
 * `setKey()` 内部是同步写+同步刷新（产品函数的返回后状态），读可以同事件循环；
 * 但 **`saveShortcuts()` 的磁盘可见性**一律隔一步再读（QSettings 虽然探针实测
 * 无需显式 sync，仍按纪律走）。
 */
#include "app/ShortcutCheck.hpp"

#include "app/ShortcutModel.hpp"

#include <QCoreApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QSettings>
#include <QSet>
#include <QTimer>
#include <QVector>

#include <functional>
#include <memory>

#if defined(STELQUICK_HAS_ENGINE)
#include "StelActionMgr.hpp"
#include "StelApp.hpp"
#endif

namespace stelapp {

namespace {

using CheckResult = ShortcutCheck::Result;

#if defined(STELQUICK_HAS_ENGINE) && defined(STELQUICK_WIDGETS_HOST)

//! 递归 dump 配置全本（同 ShortcutProbe —— 探针/判据用**同一把尺子**量指纹）。
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

//! 差异条数（对称差 + 值变）。@p skipPrefix 非空时跳过该前缀（"shortcuts/"）。
int countDiff(const QMap<QString, QVariant> &before, const QMap<QString, QVariant> &after,
              const QString &skipPrefix = QString())
{
    auto skip = [&skipPrefix](const QString &k) {
        return !skipPrefix.isEmpty() && k.startsWith(skipPrefix);
    };
    int diff = 0;
    for (auto it = after.constBegin(); it != after.constEnd(); ++it)
    {
        if (skip(it.key()))
            continue;
        const auto b = before.constFind(it.key());
        if (b == before.constEnd() || b.value() != it.value())
            ++diff;
    }
    for (auto it = before.constBegin(); it != before.constEnd(); ++it)
    {
        if (skip(it.key()))
            continue;
        if (!after.contains(it.key()))
            ++diff;
    }
    return diff;
}

//! 视觉树里 objectName 相符的全部项（陷阱 45：一律 childItems 递归）。
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

//! 真实鼠标点击（HiDpiCheck 同款：最外层注入，不走 click() 捷径）。
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

//! 复刻 StelAction 构造（StelActionMgr.cpp:59-67）的磁盘值解析路径，取首段。
QString diskFirstSegment(const QString &raw)
{
    static const QRegularExpression spaceExp(QStringLiteral("\\s+"));
    return raw.split(spaceExp).value(0);
}

struct Ctx
{
    QCoreApplication *app = nullptr;
    QQuickWindow *window = nullptr;
    std::function<void(const CheckResult &)> onDone;
    CheckResult result;
    QStringList lines;

    ShortcutModel model;          //!< 判据自建（不依赖 main.cpp 的接线）
    QSettings *conf = nullptr;
    StelActionMgr *mgr = nullptr;

    QMap<QString, QVariant> baseAll;   //!< S1 基线（SC-14 复原核对用）
    QString subjectId;                 //!< row0 的动作 id（SC-05/08/09/10/13 的靶）
    QString subject2Id;                //!< row1 的动作 id（SC-07/08 的"另一个"）

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
                             ? QStringLiteral("快捷键自检：INCONCLUSIVE（%1 条测不出，%2/%3 过）")
                                   .arg(inconclusiveCount)
                                   .arg(passed)
                                   .arg(judged)
                             : QStringLiteral("快捷键自检：判据 %1/%2").arg(passed).arg(judged);
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

#endif  // STELQUICK_HAS_ENGINE && STELQUICK_WIDGETS_HOST

} // namespace

void ShortcutCheck::run(QCoreApplication *app,
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
    r.summary = QStringLiteral("快捷键自检需要合流形态构建");
    onDone(r);
#else
    Q_UNUSED(facade)
    Q_UNUSED(mailbox)
    if (!StelApp::isInitialized())
    {
        Result r;
        r.unavailable = true;
        r.summary = QStringLiteral("引擎未引导 ⇒ 动作注册表为空，快捷键面无从断言");
        onDone(r);
        return;
    }

    auto ctx = new Ctx();
    ctx->app = app;
    ctx->window = window;
    ctx->conf = StelApp::getInstance().getSettings();
    ctx->mgr = StelApp::getInstance().getStelActionManager();
    // 🔴 必填：`finish()` 用的是 **Ctx::onDone**，不是 tick 捕获的那个 ——
    // 漏了这一句的形态是"14 步全跑完、收尾时 std::bad_function_call"（零判据输出），
    // 极难从现象反推（首轮就是这么崩的：RC=134 且一条判据都没打印）。
    ctx->onDone = onDone;
    ctx->result.ran = true;
    // 🔴 判据台账的 id 必须与各 note 的**首词逐字一致**。踩过的坑：用
    //    `QStringLiteral("SC-%1").arg(i)` 生成的是 "SC-1".."SC-14"（**无前导零**），
    //    而 note 首词写的是 "SC-01".."SC-14" ⇒ 只有 SC-10..SC-14 匹配得上，
    //    前 9 条 mark **静默丢失**，汇总显示"判据 5/5 却 VERDICT=FAIL"（judged ≠ items.size()）。
    //    显式列表最稳（也让"拆出来的派生条"有名有姓）。
    {
        const QStringList ids{
            QStringLiteral("SC-01"), QStringLiteral("SC-02"), QStringLiteral("SC-03"),
            QStringLiteral("SC-04"), QStringLiteral("SC-05a"), QStringLiteral("SC-05b"),
            QStringLiteral("SC-06"), QStringLiteral("SC-07"), QStringLiteral("SC-08"),
            QStringLiteral("SC-09"), QStringLiteral("SC-10"), QStringLiteral("SC-11"),
            QStringLiteral("SC-12"), QStringLiteral("SC-13"), QStringLiteral("SC-14")};
        for (const QString &id : ids)
            ctx->items.append(qMakePair(id, Ctx::Item()));
    }

    auto steps = std::make_shared<QVector<Step>>();
    auto idx = std::make_shared<int>(0);
    auto tick = std::make_shared<std::function<void()>>();

    // ── S1 布场 ────────────────────────────────────────────────────────────
    steps->append(Step{1, [](Ctx *c) {
        c->baseAll = dumpSettings(c->conf);
        c->model.refresh();
        c->note(QStringLiteral("布场：模型 %1 行（引擎 %2 动作）｜配置 %3｜shortcuts 组基线 %4 项")
                    .arg(c->model.totalCount())
                    .arg(c->mgr->getActionList().size())
                    .arg(c->conf->fileName())
                    .arg([&c] {
                        c->conf->beginGroup(QStringLiteral("shortcuts"));
                        const int n = c->conf->allKeys().size();
                        c->conf->endGroup();
                        return n;
                    }()));
        c->subjectId = c->model.rowActionId(0);
        c->subject2Id = c->model.rowActionId(1);
        c->note(QStringLiteral("布场：SC 靶 row0=%1｜row1=%2").arg(c->subjectId, c->subject2Id));
    }});

    // ── S2 SC-01 / SC-02 / SC-03 ──────────────────────────────────────────
    steps->append(Step{0, [](Ctx *c) {
        // SC-01 表完整性
        const int engineN = c->mgr->getActionList().size();
        const int engineG = c->mgr->getGroupList().size();
        QSet<QString> groups;
        for (int i = 0; i < c->model.rowCount(); ++i)
            groups.insert(c->model.index(i, 0).data(ShortcutModel::GroupKeyRole).toString());
        const bool sc01 = c->model.totalCount() == engineN && groups.size() == engineG;
        c->mark(sc01, QStringLiteral("SC-01 表完整性：模型 %1 vs 引擎 %2 动作｜分组 %3 vs %4")
                          .arg(c->model.totalCount())
                          .arg(engineN)
                          .arg(groups.size())
                          .arg(engineG));

        // SC-02 行数据抽样（前 10 行，与引擎逐项独立回读）
        bool sc02 = true;
        QStringList bad;
        const int n = qMin(10, c->model.rowCount());
        for (int i = 0; i < n; ++i)
        {
            const QModelIndex ix = c->model.index(i, 0);
            const QString id = ix.data(ShortcutModel::ActionIdRole).toString();
            StelAction *a = c->mgr->findAction(id);
            if (!a)
            {
                sc02 = false;
                bad << QStringLiteral("%1（引擎缺）").arg(id);
                continue;
            }
            if (ix.data(ShortcutModel::TitleRole).toString() != a->getText())
            {
                sc02 = false;
                bad << id + QStringLiteral(".title");
            }
            if (ix.data(ShortcutModel::PrimaryRole).toString() != a->getShortcut().toString())
            {
                sc02 = false;
                bad << id + QStringLiteral(".主键");
            }
            if (ix.data(ShortcutModel::AltRole).toString() != a->getAltShortcut().toString())
            {
                sc02 = false;
                bad << id + QStringLiteral(".备键");
            }
        }
        c->mark(sc02, QStringLiteral("SC-02 行数据抽样（%1 行）：%2")
                          .arg(n)
                          .arg(sc02 ? QStringLiteral("逐项一致") : bad.join(QStringLiteral("、"))));

        // SC-03 customized 口径（全表 vs conf->contains —— 引擎构造的同款判据）
        bool sc03 = true;
        QStringList cbad;
        for (int i = 0; i < c->model.rowCount(); ++i)
        {
            const QModelIndex ix = c->model.index(i, 0);
            const QString id = ix.data(ShortcutModel::ActionIdRole).toString();
            const bool m = ix.data(ShortcutModel::CustomizedRole).toBool();
            const bool e = c->conf->contains(QStringLiteral("shortcuts/") + id);
            if (m != e)
            {
                sc03 = false;
                cbad << id;
            }
        }
        c->mark(sc03, QStringLiteral("SC-03 customized 口径（全表 %1 行）：%2")
                          .arg(c->model.rowCount())
                          .arg(sc03 ? QStringLiteral("与引擎判据一致")
                                    : QStringLiteral("不一致：") + cbad.join(QStringLiteral("、"))));
    }});

    // ── S3 SC-04 键名生成单点（含判别对照）──────────────────────────────────
    steps->append(Step{0, [](Ctx *c) {
        struct Sample
        {
            int key;
            int mods;
            const char *expect;
        };
        const QVector<Sample> samples{
            {Qt::Key_A, int(Qt::NoModifier), "A"},
            {Qt::Key_C, int(Qt::NoModifier), "C"},
            {Qt::Key_A, int(Qt::ControlModifier), "Ctrl+A"},
            {Qt::Key_K, int(Qt::MetaModifier), "Meta+K"},
            {Qt::Key_P, int(Qt::AltModifier), "Alt+P"},
            {Qt::Key_F9, int(Qt::ControlModifier | Qt::AltModifier | Qt::ShiftModifier),
             "Ctrl+Alt+Shift+F9"},
        };
        bool ok = true;
        QStringList bad;
        for (const Sample &s : samples)
        {
            const QString got = c->model.keySequenceFromEvent(s.key, s.mods);
            if (got != QLatin1String(s.expect))
            {
                ok = false;
                bad << QStringLiteral("key=%1 ⇒「%2」≠「%3」").arg(s.key).arg(got, s.expect);
            }
        }
        // 修饰键单独按下 ⇒ 空串（中间态）
        const bool modOk = c->model.keySequenceFromEvent(Qt::Key_Shift, int(Qt::ShiftModifier))
                               .isEmpty()
                           && c->model.isModifierKey(Qt::Key_Control)
                           && !c->model.isModifierKey(Qt::Key_A);
        // 🔴 判别对照：手拼 "PageUp" 直构必须为空序列 —— 这是"键名必须由 C++ 生成"
        //    的物证（别名键名静默变空 ⇒ 引擎里等于"移除快捷键"）。
        const QString alias = QKeySequence(QStringLiteral("PageUp")).toString();
        const bool aliasOk = alias.isEmpty();
        c->note(QStringLiteral("SC-04 判别对照：QKeySequence(\"PageUp\") 直构=「%1」⇒ %2")
                    .arg(alias.isEmpty() ? QStringLiteral("∅空") : alias)
                    .arg(aliasOk
                             ? QStringLiteral("别名键名确实静默变空，生成器口径成立")
                             : QStringLiteral("⚠️ Qt 键名行为与本判据的假设不符，复核")));
        c->mark(ok && modOk && aliasOk,
                QStringLiteral("SC-04 键名生成：%1｜修饰键中间态=%2｜别名对照=%3")
                    .arg(ok ? QStringLiteral("6 样本恒等")
                            : QStringLiteral("不等：") + bad.join(QStringLiteral("、")))
                    .arg(modOk ? QStringLiteral("✓") : QStringLiteral("✗"))
                    .arg(aliasOk ? QStringLiteral("✓") : QStringLiteral("✗")));
    }});

    // ── S4 SC-05a/05b 写入步（setKey row0 = Ctrl+Alt+Shift+F9）───────────────
    steps->append(Step{1, [](Ctx *c) {
        const bool ok = c->model.setKey(0, 0, QStringLiteral("Ctrl+Alt+Shift+F9"));
        c->note(QStringLiteral("SC-05 写入步：setKey(row0, 主, 「Ctrl+Alt+Shift+F9」) 返回 %1"
                               "｜模型状态=「%2」")
                    .arg(ok ? QStringLiteral("true") : QStringLiteral("false"),
                         c->model.statusText()));
    }});

    // ── S5 读步：SC-05a 引擎回读 / SC-05b 落盘 / SC-06 不写穿 ────────────────
    steps->append(Step{1, [](Ctx *c) {
        // SC-05a：引擎 getter 独立回读（不复述模型）
        StelAction *a = c->mgr->findAction(c->subjectId);
        const QString got = a ? a->getShortcut().toString() : QString();
        c->mark(got == QStringLiteral("Ctrl+Alt+Shift+F9"),
                QStringLiteral("SC-05a 改键写引擎：引擎「%1」%2")
                    .arg(got,
                         got == QStringLiteral("Ctrl+Alt+Shift+F9")
                             ? QStringLiteral("== 新键 ✓")
                             : QStringLiteral("≠ 新键（期望 Ctrl+Alt+Shift+F9）")));

        // SC-05b：新 QSettings 实例读磁盘，按构造解析路径取首段、比序列语义
        QSettings fresh(c->conf->fileName(), c->conf->format());
        fresh.beginGroup(QStringLiteral("shortcuts"));
        const QString raw = fresh.contains(c->subjectId)
                                ? fresh.value(c->subjectId).toString()
                                : QString();
        fresh.endGroup();
        const QString first = diskFirstSegment(raw);
        const bool diskOk = !first.isEmpty()
                            && QKeySequence(first)
                                   == QKeySequence(QStringLiteral("Ctrl+Alt+Shift+F9"));
        c->mark(diskOk, QStringLiteral("SC-05b 改键落盘：磁盘原样「%1」⇒ 首段「%2」%3")
                            .arg(raw.isEmpty() ? QStringLiteral("（无此项）") : raw, first)
                            .arg(diskOk ? QStringLiteral("序列语义 == 新键 ✓")
                                        : QStringLiteral("≠ 新键")));

        // SC-06：非 shortcuts 组指纹零差异
        const QMap<QString, QVariant> now = dumpSettings(c->conf);
        const int other = countDiff(c->baseAll, now, QStringLiteral("shortcuts/"));
        c->mark(other == 0,
                QStringLiteral("SC-06 不写穿：改键后非 shortcuts 组差异数 = %1（基线 %2 键）")
                    .arg(other)
                    .arg(c->baseAll.size()));
    }});

    // ── S6 SC-07 冲突：制造（row1 设成与 row0 相同）─────────────────────────
    steps->append(Step{1, [](Ctx *c) {
        const bool ok = c->model.setKey(1, 0, QStringLiteral("Ctrl+Alt+Shift+F9"));
        c->note(QStringLiteral("SC-07 冲突写入步：setKey(row1, 主, 同键) 返回 %1")
                    .arg(ok ? QStringLiteral("true") : QStringLiteral("false")));
    }});

    // ── S7 SC-07 读步：检出 + 判别对照（消解归零）────────────────────────────
    steps->append(Step{0, [](Ctx *c) {
        const int cc1 = c->model.conflictCount();
        const bool r0 = c->model.index(0, 0).data(ShortcutModel::ConflictRole).toBool();
        const bool r1 = c->model.index(1, 0).data(ShortcutModel::ConflictRole).toBool();
        // 判别对照：把 row1 改成别的键 ⇒ 冲突必须归零
        c->model.setKey(1, 0, QStringLiteral("Ctrl+Alt+Shift+F8"));
        const int cc2 = c->model.conflictCount();
        c->mark(cc1 >= 1 && r0 && r1 && cc2 == 0,
                QStringLiteral("SC-07 冲突检测：撞键后 count=%1（row0=%2 row1=%3）"
                               "｜消解后 count=%4 ⇒ %5")
                    .arg(cc1)
                    .arg(r0 ? QStringLiteral("红") : QStringLiteral("绿"))
                    .arg(r1 ? QStringLiteral("红") : QStringLiteral("绿"))
                    .arg(cc2)
                    .arg(cc1 >= 1 && r0 && r1 && cc2 == 0
                             ? QStringLiteral("检出与消解都成立")
                             : QStringLiteral("⚠️ 检出或消解失败")));
    }});

    // ── S8 SC-08 恢复默认（恢复 row0；row1 的自定义键必须还在）────────────────
    steps->append(Step{1, [](Ctx *c) {
        c->model.restoreDefault(0);
        // 引擎级证据：conf 里该项被剔除（= 与出厂默认相同，saveShortcuts 跳过）
        const bool goneFromConf = !c->conf->contains(QStringLiteral("shortcuts/") + c->subjectId);
        StelAction *b = c->mgr->findAction(c->subject2Id);
        const QString otherKey = b ? b->getShortcut().toString() : QString();
        const bool otherKept = otherKey == QStringLiteral("Ctrl+Alt+Shift+F8");
        c->mark(goneFromConf && otherKept,
                QStringLiteral("SC-08 恢复默认：row0 的 conf 项被剔除=%1"
                               "｜row1 自定义键「%2」保留=%3 ⇒ %4")
                    .arg(goneFromConf ? QStringLiteral("是") : QStringLiteral("否"), otherKey)
                    .arg(otherKept ? QStringLiteral("是") : QStringLiteral("否"))
                    .arg(goneFromConf && otherKept
                             ? QStringLiteral("单个恢复不冲别人（探针⑦产品级复验）")
                             : QStringLiteral("⚠️ 恢复默认有全局副作用")));
    }});

    // ── S9 SC-09 空串 = 移除 ────────────────────────────────────────────────
    steps->append(Step{1, [](Ctx *c) {
        c->model.setKey(0, 0, QString());
        StelAction *a = c->mgr->findAction(c->subjectId);
        const bool engineEmpty = a && a->getShortcut().isEmpty();
        QSettings fresh(c->conf->fileName(), c->conf->format());
        fresh.beginGroup(QStringLiteral("shortcuts"));
        const bool present = fresh.contains(c->subjectId);
        const QString raw = present ? fresh.value(c->subjectId).toString() : QString();
        fresh.endGroup();
        const QStringList parts = raw.split(QRegularExpression(QStringLiteral("\\s+")));
        bool allEmpty = !parts.isEmpty();
        for (const QString &p : parts)
            if (!QKeySequence(p).toString().isEmpty())
                allEmpty = false;
        c->mark(engineEmpty && present && parts.size() == 2 && allEmpty,
                QStringLiteral("SC-09 空串移除：引擎键空=%1｜磁盘项在=%2 原样「%3」(%4 段)"
                               "｜复刻构造解析全部回解为空=%5")
                    .arg(engineEmpty ? QStringLiteral("是") : QStringLiteral("否"))
                    .arg(present ? QStringLiteral("是") : QStringLiteral("否"), raw)
                    .arg(parts.size())
                    .arg(allEmpty ? QStringLiteral("是") : QStringLiteral("否")));
    }});

    // ── S10 SC-10 非法键名闸（判别对照：合法输入在上面的步骤已通过）────────────
    steps->append(Step{1, [](Ctx *c) {
        StelAction *a = c->mgr->findAction(c->subjectId);
        const QString before = a ? a->getShortcut().toString() : QString();
        // 证据先行：别名键名在 Qt 里的两个读数（count=1、toString=空）——
        // 这就是"只查 count 的闸会放行、然后静默删键"的物证（首轮 SC-10 红）。
        const QKeySequence aliasSeq(QStringLiteral("PageUp"));
        c->note(QStringLiteral("SC-10 物证：QKeySequence(\"PageUp\") count=%1｜toString=「%2」")
                    .arg(aliasSeq.count())
                    .arg(aliasSeq.toString().isEmpty() ? QStringLiteral("∅空")
                                                       : aliasSeq.toString()));
        const bool rejected = !c->model.setKey(0, 0, QStringLiteral("PageUp"));
        const QString after = a ? a->getShortcut().toString() : QString();
        const QString st = c->model.statusText();
        c->mark(rejected && after == before && !st.isEmpty(),
                QStringLiteral("SC-10 非法键名闸：「PageUp」被拒=%1｜引擎键「%2」未变=%3"
                               "｜状态文本非空=%4（防止手拼键名静默变删除）")
                    .arg(rejected ? QStringLiteral("是") : QStringLiteral("否"), before)
                    .arg(after == before ? QStringLiteral("是") : QStringLiteral("否"))
                    .arg(!st.isEmpty() ? QStringLiteral("是") : QStringLiteral("否")));
    }});

    // ── S11 SC-11 全部恢复 ──────────────────────────────────────────────────
    steps->append(Step{1, [](Ctx *c) {
        const int beforeN = c->model.customizedCount();
        c->model.restoreAll();
        const int afterN = c->model.customizedCount();
        QSettings fresh(c->conf->fileName(), c->conf->format());
        fresh.beginGroup(QStringLiteral("shortcuts"));
        const int diskN = fresh.allKeys().size();
        fresh.endGroup();
        c->mark(beforeN >= 2 && afterN == 0 && diskN == 0,
                QStringLiteral("SC-11 全部恢复：此前 %1 个被改 ⇒ 之后 %2 个｜磁盘 shortcuts 组 "
                               "%3 项 ⇒ %4")
                    .arg(beforeN)
                    .arg(afterN)
                    .arg(diskN)
                    .arg(beforeN >= 2 && afterN == 0 && diskN == 0
                             ? QStringLiteral("清干净")
                             : QStringLiteral("⚠️ 有残留")));
    }});

    // ── S12a SC-12 前半：真实点导航切页（**单独一步**）────────────────────────
    // ⚠️ 必须与"找 delegate"分开，且**隔 ≥1 帧**：StackLayout 的隐藏页 ListView
    // 尺寸是 0 ⇒ delegate 一个都不创建；切页后要等布局帧才有 delegate（T39 同款
    // 血泪 + 400ms 先例）。首轮隔 1ms ⇒ SC-12 报"delegate 按钮缺失"、SC-13 连带 NA。
    steps->append(Step{400, [](Ctx *c) {
        if (!c->window)
        {
            c->markNa(QStringLiteral("SC-12 无窗口（不可能走到这）"));
            return;
        }
        QQuickItem *root = c->window->contentItem();
        const QVector<QQuickItem *> nav =
            collectByObjectName(root, QStringLiteral("navShortcutsButton"));
        if (nav.isEmpty())
        {
            c->markNa(QStringLiteral("SC-12 找不到 navShortcutsButton（工具栏缺按钮？）"));
            return;
        }
        const bool clicked = clickAt(c->window, nav.first()->mapToScene(itemCenter(nav.first())));
        c->note(QStringLiteral("SC-12 切页步：真实点击 navShortcutsButton ⇒ %1")
                    .arg(clicked ? QStringLiteral("已投递") : QStringLiteral("未受理")));
    }});

    // ── S12b SC-12 后半：视觉树递归找控件 ─────────────────────────────────────
    steps->append(Step{1, [](Ctx *c) {
        if (!c->window)
            return;   // S12a 已记 NA
        QQuickItem *root = c->window->contentItem();
        const QStringList names{QStringLiteral("shortcutsPage"),
                                QStringLiteral("shortcutFilterField"),
                                QStringLiteral("shortcutList"),
                                QStringLiteral("shortcutPrimary_0"),
                                QStringLiteral("shortcutAlt_0"),
                                QStringLiteral("shortcutRestore_0"),
                                QStringLiteral("shortcutRestoreAllButton"),
                                QStringLiteral("shortcutKeyCatcher"),
                                QStringLiteral("shortcutStatsLabel")};
        bool all = true;
        QStringList missing;
        for (const QString &n : names)
            if (collectByObjectName(root, n).isEmpty())
            {
                all = false;
                missing << n;
            }
        c->mark(all, QStringLiteral("SC-12 UI 控件齐备：%1%2")
                         .arg(all ? QStringLiteral("9 个控件全部在场（已真实点击导航切页）")
                                  : QStringLiteral("缺失："))
                         .arg(all ? QString() : missing.join(QStringLiteral("、"))));
    }});

    // ── S13a SC-13 交互写入步（真实点主键按钮 + 注入真实 QKeyEvent）───────────
    steps->append(Step{300, [](Ctx *c) {
        if (!c->window)
        {
            c->markNa(QStringLiteral("SC-13 无窗口"));
            return;
        }
        // 布场（W-T41 跨平台复验 2026-10-01）：无头/后台会话里 QQuickWindow 可能
        // **从未被激活**，此时 QQuickWindowPrivate::deliverKeyEvent 没有可路由的
        // activeFocusItem，合成键盘事件会**静默消失**。Windows 实测症状正是如此：
        // 点击生效、UI 已进捕获态（按钮显示「按键…（Esc 取消）」），但引擎键纹丝不动
        // ⇒ SC-13 假红。先请求激活；并把可观测状态打进 note —— 万一仍失败，
        // 这行就是判据自己的判别依据（不是"重试到绿"）。
        c->window->requestActivate();
        QCoreApplication::processEvents();
        {
            QQuickItem *afi = c->window->activeFocusItem();
            c->note(QStringLiteral("SC-13 布场：window->isActive=%1｜activeFocusItem=%2")
                        .arg(c->window->isActive() ? 1 : 0)
                        .arg(afi ? (afi->objectName().isEmpty()
                                        ? QString::fromLatin1(afi->metaObject()->className())
                                        : afi->objectName())
                                 : QStringLiteral("(null)")));
        }
        QQuickItem *root = c->window->contentItem();
        const QVector<QQuickItem *> btn =
            collectByObjectName(root, QStringLiteral("shortcutPrimary_0"));
        if (btn.isEmpty())
        {
            c->markNa(QStringLiteral("SC-13 找不到 shortcutPrimary_0"));
            return;
        }
        StelAction *a = c->mgr->findAction(c->subjectId);
        c->note(QStringLiteral("SC-13 写入步：点击前引擎键=「%1」")
                    .arg(a ? a->getShortcut().toString() : QString()));
        // 真实点击主键按钮 ⇒ 进捕获态（QML 侧 keyCatcher.forceActiveFocus()）
        clickAt(c->window, btn.first()->mapToScene(itemCenter(btn.first())));
        // 注入真实键盘事件（捕获器 accepted=true ⇒ 不冒泡到 keySink/routeKey）
        const int mods = int(Qt::ControlModifier | Qt::AltModifier | Qt::ShiftModifier);
        QKeyEvent press(QEvent::KeyPress, Qt::Key_F7, Qt::KeyboardModifiers(mods));
        QKeyEvent release(QEvent::KeyRelease, Qt::Key_F7, Qt::KeyboardModifiers(mods));
        QCoreApplication::sendEvent(c->window, &press);
        QCoreApplication::sendEvent(c->window, &release);
        c->note(QStringLiteral("SC-13 写入步：已真实点击 shortcutPrimary_0 并注入 "
                               "Ctrl+Alt+Shift+F7（读 UI 放下一步）"));
    }});

    // ── S13b SC-13 判：引擎 + **UI 按钮文字** 双确认 ──────────────────────────
    // 🔴 首轮这里判的是"**判据自建的 model** 跟随"——错的：交互发生在 QML 侧，
    //    改的是 main.cpp 里那个 **context property** 实例，判据自建的副本压根不参与，
    //    于是"引擎对了、模型说不跟随"（引擎读数已证明链路是通的）。
    //    ⇒ 交互腿的产物必须去 **UI 自己能观测到的地方**读，别拿孤立副本当参照。
    steps->append(Step{250, [](Ctx *c) {
        if (!c->window)
            return;
        QQuickItem *root = c->window->contentItem();
        StelAction *a = c->mgr->findAction(c->subjectId);
        const QString after = a ? a->getShortcut().toString() : QString();
        const QVector<QQuickItem *> btn =
            collectByObjectName(root, QStringLiteral("shortcutPrimary_0"));
        const QString btnText = btn.isEmpty()
                                    ? QStringLiteral("（按钮不在）")
                                    : btn.first()->property("text").toString();
        const bool engineOk = (after == QStringLiteral("Ctrl+Alt+Shift+F7"));
        const bool uiOk = (btnText == after);
        if (!engineOk)
        {
            // 判别依据：注入之后捕获器是否真的持有焦点。若这里显示的不是
            // shortcutKeyCatcher，则"键没被收到"是**事件路由**问题，而不是改键逻辑
            // 的问题 —— 这两种解释的处置完全不同，不能糊在一起。
            QQuickItem *afi = c->window->activeFocusItem();
            c->note(QStringLiteral("SC-13 诊断：注入后 window->isActive=%1｜activeFocusItem=%2"
                                   "（捕获器 objectName = shortcutKeyCatcher）")
                        .arg(c->window->isActive() ? 1 : 0)
                        .arg(afi ? (afi->objectName().isEmpty()
                                        ? QString::fromLatin1(afi->metaObject()->className())
                                        : afi->objectName())
                                 : QStringLiteral("(null)")));
        }
        c->mark(engineOk && uiOk,
                QStringLiteral("SC-13 UI 交互端到端：引擎键=「%1」%2｜UI 按钮文字=「%3」%4")
                    .arg(after,
                         engineOk ? QStringLiteral("✓") : QStringLiteral("✗"),
                         btnText,
                         uiOk ? QStringLiteral("✓") : QStringLiteral("✗")));
    }});

    // ── S14 SC-14 复原 + 指纹核对 ──────────────────────────────────────────
    steps->append(Step{0, [](Ctx *c) {
        c->model.restoreAll();
        const QMap<QString, QVariant> now = dumpSettings(c->conf);
        const int total = countDiff(c->baseAll, now);
        c->mark(total == 0,
                QStringLiteral("SC-14 复原：整本指纹 vs 基线差异数 = %1（含 shortcuts 组）"
                               " ⇒ %2")
                    .arg(total)
                    .arg(total == 0 ? QStringLiteral("用户配置零污染 ✓")
                                    : QStringLiteral("⚠️ 有残留，重跑本自检可清")));
        // finish() 由步骤调度器在步骤耗尽时统一调（这里再调会 onDone 两次）。
    }});

    // 步骤调度器（同 HiDpiProbe / DisplayCheck）。
    *tick = [ctx, steps, idx, tick, onDone]() {
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
