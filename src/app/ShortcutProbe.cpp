/*
 * ShortcutProbe — 实现（T40-A 快捷键编辑命令面探针）。设计与十二个问题见头注。
 *
 * 驱动方式沿用 DisplayProbe / LocationProbe / ToolbarProbe / HiDpiProbe 的线性步骤表：
 * 每步 = (跑完后等多少 ms, 步骤体)。
 * ⚠️ delayAfter 语义 = "跑完**本步**之后的等待"（T33 陷阱 41 / T37 陷阱 69 / T38 首轮）：
 * 写 0 时下一步与本步只隔一个事件循环；**凡"写后要读"的配对，读一律放下一步**。
 */
#include "app/ShortcutProbe.hpp"

#include "app/ActionRouter.hpp"

#include <QCoreApplication>
#include <QFileInfo>
#include <QGuiApplication>
#include <QKeySequence>
#include <QMap>
#include <QMetaObject>
#include <QMultiMap>
#include <QRegularExpression>
#include <QSettings>
#include <QTimer>
#include <QVariant>
#include <QVector>

#include <functional>
#include <memory>

#if defined(STELQUICK_HAS_ENGINE)
#include "StelApp.hpp"
#include "StelActionMgr.hpp"
#endif

namespace stelapp {

namespace {

using ProbeResult = ShortcutProbe::Result;

#if defined(STELQUICK_HAS_ENGINE)

//! 递归 dump 一个 QSettings 的全部键值（含子组）；键统一写成长 `组/键`。
//! 用于 Q4「不写穿」的整本指纹对照（不是只看 shortcuts 组）。
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

//! 两个指纹的差异摘要：新增 / 删除 / 值变 各自计数 + 前几个样例。
QString diffDumpText(const QMap<QString, QVariant> &before, const QMap<QString, QVariant> &after)
{
    QStringList added, removed, changed;
    for (auto it = after.constBegin(); it != after.constEnd(); ++it)
    {
        const auto b = before.constFind(it.key());
        if (b == before.constEnd())
            added << it.key();
        else if (b.value() != it.value())
            changed << QStringLiteral("%1(%2→%3)")
                           .arg(it.key(), b.value().toString(), it.value().toString());
    }
    for (auto it = before.constBegin(); it != before.constEnd(); ++it)
        if (!after.contains(it.key()))
            removed << it.key();
    auto brief = [](const QStringList &v) {
        if (v.isEmpty())
            return QStringLiteral("无");
        if (v.size() <= 4)
            return v.join(QStringLiteral(", "));
        return v.mid(0, 4).join(QStringLiteral(", ")) + QStringLiteral(" …(共 %1)").arg(v.size());
    };
    return QStringLiteral("新增 %1｜删除 %2｜值变 %3 ⇒ [新增:%4][删除:%5][值变:%6]")
        .arg(added.size())
        .arg(removed.size())
        .arg(changed.size())
        .arg(brief(added), brief(removed), brief(changed));
}

//! 一个动作的"人读一行"。⚠️ `QString::arg` 的 QString 多参重载只到 4 个 ⇒ 一律链式。
QString actionLine(StelAction *a)
{
    const QString main = a->getShortcut().toString();
    const QString alt = a->getAltShortcut().toString();
    return QStringLiteral("%1[%2]「%3」主=%4 备=%5 %6")
        .arg(a->getId())
        .arg(a->getGroup())
        .arg(a->getText())
        .arg(main.isEmpty() ? QStringLiteral("∅") : main)
        .arg(alt.isEmpty() ? QStringLiteral("∅") : alt)
        .arg(a->isCheckable() ? QStringLiteral("checkable") : QStringLiteral("slot"));
}

struct Ctx
{
    QCoreApplication *app = nullptr;
    ActionRouter *router = nullptr;
    ProbeResult r;
    StelActionMgr *mgr = nullptr;
    QSettings *conf = nullptr;

    // ── 基线（S1 存档，收尾核对）──
    QMap<QString, QVariant> baseAll;      //!< 整本配置指纹（含 shortcuts 组）
    QMap<QString, QVariant> baseShort;    //!< 只 shortcuts 组（人读用）

    // ── 动过的动作（收尾逐项还原）──
    QStringList touched;                  //!< id 列表
    QHash<QString, QString> origMain;     //!< id → 原始主键串
    QHash<QString, QString> origAlt;      //!< id → 原始备键串

    // ── 试验品 ──
    StelAction *subject = nullptr;        //!< 改键 + 往返 + 落盘的靶子
    StelAction *toggle = nullptr;         //!< routeKey 真触发的靶子（须幂等 toggle）

    // ── Q2 信号计数 ──
    std::shared_ptr<int> changedHits = std::make_shared<int>(0);
    std::shared_ptr<int> shortcutsChangedHits = std::make_shared<int>(0);
    QMetaObject::Connection connChanged;
    QMetaObject::Connection connShortcuts;

    void note(const QString &s) { r.details << s; }

    //! 记下动作的当前键（只记一次；收尾还原用）。
    void remember(StelAction *a)
    {
        if (!a)
            return;
        const QString id = a->getId();
        if (origMain.contains(id))
            return;
        touched << id;
        origMain.insert(id, a->getShortcut().toString());
        origAlt.insert(id, a->getAltShortcut().toString());
    }
};

//! 驱动步骤：跑完本步后等 delayAfter 毫秒进下一步。
struct Step
{
    int delayAfter = 0;
    std::function<void(Ctx *)> body;
};

//! 挑试验品。返回 nullptr 表示注册表里没有可用项。
//! 选法：主键非空 + 备键空（改动面最小）；toggle 靶子额外要求 checkable。
bool pickSubjects(StelActionMgr *mgr, StelAction *&subject, StelAction *&toggle, QString &why)
{
    const QList<StelAction *> all = mgr->getActionList();
    // ① 改键靶子：偏好 id 里带 "toggle"/"grid" 的显示类（改它风险最低），否则取第一个合格项。
    for (StelAction *a : all)
    {
        if (a->getShortcut().toString().isEmpty())
            continue;
        if (!a->getAltShortcut().toString().isEmpty())
            continue;
        if (a->getId().contains(QLatin1String("grid"), Qt::CaseInsensitive)
            || a->getId().contains(QLatin1String("lines"), Qt::CaseInsensitive))
        {
            subject = a;
            break;
        }
        if (!subject)
            subject = a;
    }
    // ② routeKey 靶子：必须是 checkable（触发两次能回原状），且自带键位。
    //    ⚠️ **必须排除 subject** —— 首轮探针两个靶撞成同一个动作（它同时含 "lines"
    //    且 checkable）⇒ S4/S5 互相覆盖键、S9 的"另一个自定义项"退化成自己，
    //    三条读数全部失真（"仪器把被测量和参照量搞混了"）。
    for (StelAction *a : all)
    {
        if (a == subject)
            continue;
        if (!a->isCheckable() || a->getShortcut().toString().isEmpty())
            continue;
        if (a->getId().contains(QLatin1String("grid"), Qt::CaseInsensitive)
            || a->getId().contains(QLatin1String("lines"), Qt::CaseInsensitive)
            || a->getId().contains(QLatin1String("azimuthal"), Qt::CaseInsensitive))
        {
            toggle = a;
            break;
        }
        if (!toggle)
            toggle = a;
    }
    why = QStringLiteral("改键靶=%1（%2）｜routeKey 靶=%3（%4）")
              .arg(subject ? subject->getId() : QStringLiteral("∅"),
                   subject ? subject->getGroup() : QStringLiteral("-"),
                   toggle ? toggle->getId() : QStringLiteral("∅"),
                   toggle ? toggle->getGroup() : QStringLiteral("-"));
    return subject != nullptr && toggle != nullptr;
}

#endif  // STELQUICK_HAS_ENGINE

} // namespace

void ShortcutProbe::run(QCoreApplication *app,
                        ActionRouter *router,
                        const std::function<void(const Result &)> &onDone,
                        int delayMs)
{
#if !defined(STELQUICK_HAS_ENGINE)
    Q_UNUSED(app)
    Q_UNUSED(router)
    Q_UNUSED(delayMs)
    Result r;
    r.unavailable = true;
    r.summary = QStringLiteral("快捷键探针需要合流形态构建（STELQUICK_HAS_ENGINE）");
    onDone(r);
#else
    if (!StelApp::isInitialized())
    {
        Result r;
        r.unavailable = true;
        r.summary = QStringLiteral("引擎未引导 ⇒ 动作注册表为空，快捷键面无从观测");
        onDone(r);
        return;
    }

    auto ctx = new Ctx();
    ctx->app = app;
    ctx->router = router;
    ctx->mgr = StelApp::getInstance().getStelActionManager();
    ctx->conf = StelApp::getInstance().getSettings();
    ctx->r.ran = true;

    auto steps = std::make_shared<QVector<Step>>();
    auto idx = std::make_shared<int>(0);
    auto tick = std::make_shared<std::function<void()>>();

    // ─────────────────────────────────────────────────────────────────────
    // S1 台账 + 基线存档
    // ─────────────────────────────────────────────────────────────────────
    steps->append(Step{0, [](Ctx *c) {
        StelActionMgr *m = c->mgr;
        const QList<StelAction *> all = m->getActionList();
        const QStringList groups = m->getGroupList();
        const QStringList shortcuts = m->getShortcutsList();
        c->note(QStringLiteral("Q10 注册表：动作 %1 个｜分组 %2 个｜非空快捷键串 %3 条")
                    .arg(all.size())
                    .arg(groups.size())
                    .arg(shortcuts.size()));
        c->note(QStringLiteral("Q10 分组名：%1").arg(groups.join(QStringLiteral(" / "))));
        // 多键序列（Emacs 风格 "Ctrl+E, Ctrl+2"）在注册表里有多少 —— 它决定编辑页
        // 要不要支持"按两下键"，也决定落盘格式的边界。
        // ⚠️ **别用"串里有没有逗号"判断** —— 首轮就这么写的，结果 `actionShow_Ecliptic_Line`
        //    的键是 **`Qt::Key_Comma`（toString = 「,」）**，被误计成多键序列。
        //    正解 = `QKeySequence::count()`（组合个数）。
        int multi = 0;
        int commaKey = 0;
        QStringList multiSample;
        for (StelAction *a : all)
        {
            if (a->getShortcut().count() > 1)
            {
                ++multi;
                if (multiSample.size() < 3)
                    multiSample << QStringLiteral("%1=「%2」(count=%3)")
                                       .arg(a->getId(), a->getShortcut().toString())
                                       .arg(a->getShortcut().count());
            }
            else if (a->getShortcut().toString() == QStringLiteral(","))
                ++commaKey;
        }
        c->note(QStringLiteral("Q10 多键序列（count()>1）**%1 个**%2｜另有「纯逗号键」%3 个"
                               "（`toString()` 就是「,」⇒ 用逗号判多键序列会误计）")
                    .arg(multi)
                    .arg(multiSample.isEmpty()
                             ? QString()
                             : QStringLiteral("；") + multiSample.join(QStringLiteral("｜")))
                    .arg(commaKey));
        // 代表动作（前 5 个 + 带中文的一个）
        int shown = 0;
        for (StelAction *a : all)
        {
            if (shown >= 5)
                break;
            c->note(QStringLiteral("Q10 样本 %1").arg(actionLine(a)));
            ++shown;
        }
        c->note(QStringLiteral("Q11 配置文件：%1｜组数 %2")
                    .arg(c->conf->fileName())
                    .arg(c->conf->childGroups().size()));

        // 基线指纹
        c->baseAll = dumpSettings(c->conf);
        c->conf->beginGroup(QStringLiteral("shortcuts"));
        c->baseShort.clear();
        const QStringList sk = c->conf->allKeys();
        for (const QString &k : sk)
            c->baseShort.insert(k, c->conf->value(k));
        const int customCount = sk.size();
        c->conf->endGroup();
        c->note(QStringLiteral("S1 基线：整本配置 %1 键｜shortcuts 组 **%2 项**（= 已被改过的动作数）")
                    .arg(c->baseAll.size())
                    .arg(customCount));
        if (customCount > 0)
        {
            QStringList sample;
            for (auto it = c->baseShort.constBegin(); it != c->baseShort.constEnd(); ++it)
            {
                if (sample.size() >= 5)
                    break;
                sample << QStringLiteral("%1=%2").arg(it.key(), it.value().toString());
            }
            c->note(QStringLiteral("S1 shortcuts 组现存样例：%1").arg(sample.join(QStringLiteral("｜"))));
        }

        // 挑靶子
        QString why;
        if (pickSubjects(m, c->subject, c->toggle, why))
        {
            c->remember(c->subject);
            c->remember(c->toggle);
            c->note(QStringLiteral("S1 %1").arg(why));
            c->note(QStringLiteral("S1 改键靶原始：%1").arg(actionLine(c->subject)));
            c->note(QStringLiteral("S1 路由靶原始：%1").arg(actionLine(c->toggle)));
        }
        else
            c->note(QStringLiteral("S1 ⚠️ 挑不出靶子：%1").arg(why));
    }});

    // ─────────────────────────────────────────────────────────────────────
    // S2 Q1 往返恒等（合成样本，不依赖注册表）
    // ─────────────────────────────────────────────────────────────────────
    steps->append(Step{0, [](Ctx *c) {
        const QStringList samples{
            QStringLiteral("A"),
            QStringLiteral("Ctrl+N"),
            QStringLiteral("Alt+P"),
            QStringLiteral("Ctrl+Shift+A"),
            QStringLiteral("Meta+K"),
            QStringLiteral("Ctrl+Alt+Shift+F9"),
            QStringLiteral("F1"),
            QStringLiteral("Up"),
            QStringLiteral("PageUp"),
            QStringLiteral("Space"),
            QStringLiteral("Ctrl++"),
            QStringLiteral("Ctrl+-"),
            QStringLiteral("Ctrl+E, Ctrl+2"),
            QStringLiteral("Ctrl+E, Ctrl+2, Ctrl+3, Ctrl+4"),
            QStringLiteral("Ctrl+Alt+Del"),
        };
        int ok = 0;
        QStringList bad;
        for (const QString &s : samples)
        {
            const QKeySequence a(s);
            const QString r1 = a.toString();
            const QKeySequence b(r1);
            const QString r2 = b.toString();
            // 判据：①解析出来的序列非空 ②二次 toString 恒等
            const bool same = (r1 == r2) && !r1.isEmpty();
            if (same)
                ++ok;
            else
                bad << QStringLiteral("「%1」→toString「%2」→再解析「%3」").arg(s, r1, r2);
        }
        c->note(QStringLiteral("Q1 往返恒等 %1/%2%3")
                    .arg(ok)
                    .arg(samples.size())
                    .arg(bad.isEmpty() ? QString()
                                       : QStringLiteral("；不等：") + bad.join(QStringLiteral("｜"))));
        // 🔴 首轮实测的不等项是「PageUp」⇒ Qt 的键名是 **PgUp**，别名解析成**空序列**。
        //    这条决定了产品侧的一条硬规矩：**键名一律由 C++ 从 key/modifiers 生成**，
        //    QML 里绝不自己拼字符串（自己拼 = 静默变成"移除快捷键"）。
        const QStringList alias{QStringLiteral("PgUp"), QStringLiteral("PgDown"),
                                QStringLiteral("PageUp"), QStringLiteral("PageDown"),
                                QStringLiteral("Del"), QStringLiteral("Escape")};
        QStringList aliasRep;
        for (const QString &s : alias)
        {
            const QString t = QKeySequence(s).toString();
            aliasRep << QStringLiteral("「%1」→「%2」").arg(s, t.isEmpty() ? QStringLiteral("∅空") : t);
        }
        c->note(QStringLiteral("Q1 键名别名对照：%1").arg(aliasRep.join(QStringLiteral("｜"))));
        // 组合个数的口径（产品侧判"这是不是多键序列"只能靠它，不能靠逗号）：
        const QStringList cntProbe{QStringLiteral("C"), QStringLiteral(","),
                                   QStringLiteral("Ctrl+E, Ctrl+2"),
                                   QStringLiteral("Ctrl+Shift+A")};
        QStringList cntRep;
        for (const QString &s : cntProbe)
            cntRep << QStringLiteral("「%1」count=%2")
                          .arg(s)
                          .arg(QKeySequence(s).count());
        c->note(QStringLiteral("Q1 组合个数：%1").arg(cntRep.join(QStringLiteral("｜"))));

        // 注册表实测：取前 8 个非空主键，看往返是否恒等
        int rok = 0, rn = 0;
        QStringList rbad;
        for (StelAction *a : c->mgr->getActionList())
        {
            const QString s = a->getShortcut().toString();
            if (s.isEmpty())
                continue;
            if (rn >= 8)
                break;
            ++rn;
            const QString s2 = QKeySequence(QKeySequence(s).toString()).toString();
            if (s2 == s)
                ++rok;
            else
                rbad << QStringLiteral("%1:「%2」→「%3」").arg(a->getId(), s, s2);
        }
        c->note(QStringLiteral("Q1 注册表现存键往返 %1/%2%3")
                    .arg(rok)
                    .arg(rn)
                    .arg(rbad.isEmpty() ? QString()
                                        : QStringLiteral("；不等：") + rbad.join(QStringLiteral("｜"))));
    }});

    // ─────────────────────────────────────────────────────────────────────
    // S3 Q11 平台键位格式（Portable vs Native）
    // ─────────────────────────────────────────────────────────────────────
    steps->append(Step{0, [](Ctx *c) {
        c->note(QStringLiteral("Q11 平台：%1").arg(QGuiApplication::platformName()));
        const QStringList probes{QStringLiteral("Ctrl+N"), QStringLiteral("Meta+N"),
                                 QStringLiteral("Alt+P"), QStringLiteral("Ctrl+Shift+A")};
        for (const QString &p : probes)
        {
            const QKeySequence k(p);
            c->note(QStringLiteral("Q11 「%1」: Portable=「%2」｜Native=「%3」")
                        .arg(p,
                             k.toString(QKeySequence::PortableText),
                             k.toString(QKeySequence::NativeText)));
        }
        // 反解：把 Native 串再喂回去，能不能回到同一个序列
        int back = 0;
        QStringList bad;
        for (const QString &p : probes)
        {
            const QKeySequence k(p);
            const QString native = k.toString(QKeySequence::NativeText);
            const QKeySequence k2(native, QKeySequence::NativeText);
            if (k2.toString(QKeySequence::PortableText)
                == k.toString(QKeySequence::PortableText))
                ++back;
            else
                bad << QStringLiteral("「%1」Native「%2」回解→「%3」")
                           .arg(p, native, k2.toString(QKeySequence::PortableText));
        }
        c->note(QStringLiteral("Q11 NativeText 往返（按 NativeText 解析）%1/%2%3")
                    .arg(back)
                    .arg(probes.size())
                    .arg(bad.isEmpty() ? QString()
                                       : QStringLiteral("；不等：") + bad.join(QStringLiteral("｜"))));
    }});

    // ─────────────────────────────────────────────────────────────────────
    // S4 Q2 setShortcut 即时性 + 信号 + routeKey 可路由
    // ─────────────────────────────────────────────────────────────────────
    steps->append(Step{1, [](Ctx *c) {
        if (!c->subject) { c->note(QStringLiteral("Q2 跳过（无目标）")); return; }
        const QString orig = c->subject->getShortcut().toString();
        const QString probe = QStringLiteral("Ctrl+Alt+Shift+F9");

        // 挂信号计数（sticky，本步到收尾都在数）
        c->connChanged = QObject::connect(c->subject, &StelAction::changed,
                                          [hits = c->changedHits]() { ++(*hits); });
        c->connShortcuts = QObject::connect(c->mgr, &StelActionMgr::shortcutsChanged,
                                            [hits = c->shortcutsChangedHits]() { ++(*hits); });
        *c->changedHits = 0;
        *c->shortcutsChangedHits = 0;

        c->subject->setShortcut(probe);

        // 立刻回读（同一步内：内存写是否立刻可见）
        const QString back = c->subject->getShortcut().toString();
        StelAction *found = c->mgr->findActionFromShortcut(probe);
        c->note(QStringLiteral("Q2 setShortcut(「%1」)：回读=「%2」%3｜findActionFromShortcut=%4 %5")
                    .arg(probe, back)
                    .arg(back == probe ? QStringLiteral("✓") : QStringLiteral("✗"))
                    .arg(found ? found->getId() : QStringLiteral("∅"))
                    .arg((found == c->subject) ? QStringLiteral("(命中本动作)")
                                               : QStringLiteral("(非本动作)")));
        c->note(QStringLiteral("Q2 信号：changed()=%1｜shortcutsChanged()=%2"
                               "（⇒ setShortcut 只发自己那个 changed，**不发**组级"
                               " shortcutsChanged ⇒ 下游要自己转发）")
                    .arg(*c->changedHits)
                    .arg(*c->shortcutsChangedHits));
        c->note(QStringLiteral("Q2 原始键保留在 origMain 里：%1").arg(orig));
    }});

    // ─────────────────────────────────────────────────────────────────────
    // S5 Q2 routeKey 真触发（用幂等 toggle 靶子；触发两次回原状）
    // ─────────────────────────────────────────────────────────────────────
    steps->append(Step{1, [](Ctx *c) {
        if (!c->toggle) { c->note(QStringLiteral("Q2 routeKey 腿跳过（无 toggle 靶）")); return; }
        const QString oldKey = c->toggle->getShortcut().toString();
        const QString newKey = QStringLiteral("Ctrl+Alt+Shift+F8");
        const bool wasChecked = c->toggle->isChecked();
        c->toggle->setShortcut(newKey);

        const int before = c->router ? static_cast<int>(c->router->dispatchCount()) : -1;
        const bool routed = c->router
                                ? c->router->routeKey(Qt::Key_F8,
                                                      Qt::CTRL | Qt::ALT | Qt::SHIFT)
                                : false;
        const int after = c->router ? static_cast<int>(c->router->dispatchCount()) : -1;
        const bool stateMoved = (c->toggle->isChecked() != wasChecked);

        // 立刻原路再按一次，把 toggle 状态按回去
        if (stateMoved && c->router)
            c->router->routeKey(Qt::Key_F8, Qt::CTRL | Qt::ALT | Qt::SHIFT);
        const bool restoredState = (c->toggle->isChecked() == wasChecked);

        c->note(QStringLiteral("Q2 routeKey(F8, Ctrl+Alt+Shift)：改键后=「%1」命中=%2"
                               "｜dispatchCount %3→%4｜toggle 状态动过=%5 已按回=%6"
                               "｜(原键「%7」)")
                    .arg(newKey)
                    .arg(routed ? QStringLiteral("true") : QStringLiteral("false"))
                    .arg(before)
                    .arg(after)
                    .arg(stateMoved ? QStringLiteral("是") : QStringLiteral("否"))
                    .arg(restoredState ? QStringLiteral("是") : QStringLiteral("否"))
                    .arg(oldKey));
    }});

    // ─────────────────────────────────────────────────────────────────────
    // S6 Q3/Q4 落盘 + 不写穿
    // ─────────────────────────────────────────────────────────────────────
    steps->append(Step{2, [](Ctx *c) {
        const QMap<QString, QVariant> beforeAll = dumpSettings(c->conf);
        c->conf->beginGroup(QStringLiteral("shortcuts"));
        const QStringList beforeShort = c->conf->allKeys();
        c->conf->endGroup();

        const int changedBefore = *c->changedHits;
        const int scBefore = *c->shortcutsChangedHits;
        c->mgr->saveShortcuts();
        const int changedAfter = *c->changedHits;
        const int scAfter = *c->shortcutsChangedHits;

        const QMap<QString, QVariant> afterAll = dumpSettings(c->conf);
        c->conf->beginGroup(QStringLiteral("shortcuts"));
        const QStringList afterShort = c->conf->allKeys();
        QStringList lines;
        for (const QString &k : afterShort)
            lines << QStringLiteral("%1=%2").arg(k, c->conf->value(k).toString());
        c->conf->endGroup();

        c->note(QStringLiteral("Q3 saveShortcuts()：shortcuts 组 %1 项 → %2 项")
                    .arg(beforeShort.size())
                    .arg(afterShort.size()));
        c->note(QStringLiteral("Q3 落盘内容：%1")
                    .arg(lines.isEmpty() ? QStringLiteral("（空）")
                                         : lines.mid(0, 8).join(QStringLiteral("｜"))
                                               + (lines.size() > 8
                                                      ? QStringLiteral(" …(共 %1)").arg(lines.size())
                                                      : QString())));
        c->note(QStringLiteral("Q2 saveShortcuts 期间信号增量：changed() +%1｜shortcutsChanged() +%2"
                               "（⇒ setShortcut 自己不 emit shortcutsChanged）")
                    .arg(changedAfter - changedBefore)
                    .arg(scAfter - scBefore));
        // Q4 不写穿：整本指纹对照（除 shortcuts 组外必须零差异）
        const QString d = diffDumpText(beforeAll, afterAll);
        c->note(QStringLiteral("Q4 整本指纹（saveShortcuts 前后）：%1").arg(d));
        // 单独看"非 shortcuts 组"的差异
        QMap<QString, QVariant> b2 = beforeAll, a2 = afterAll;
        for (auto it = b2.begin(); it != b2.end();)
            it = it.key().startsWith(QLatin1String("shortcuts/")) ? b2.erase(it) : ++it;
        for (auto it = a2.begin(); it != a2.end();)
            it = it.key().startsWith(QLatin1String("shortcuts/")) ? a2.erase(it) : ++it;
        c->note(QStringLiteral("Q4 其中**非 shortcuts 组**：%1").arg(diffDumpText(b2, a2)));
        // 文件实况
        QFileInfo fi(c->conf->fileName());
        c->note(QStringLiteral("Q3 文件：size=%1 ｜ mtime=%2")
                    .arg(fi.size())
                    .arg(fi.lastModified().toString(Qt::ISODate)));
    }});

    // ─────────────────────────────────────────────────────────────────────
    // S6b Q3b 多键序列的落盘往返（Emacs 风格）
    // ─────────────────────────────────────────────────────────────────────
    steps->append(Step{1, [](Ctx *c) {
        if (!c->subject) { c->note(QStringLiteral("Q3b 跳过（无目标）")); return; }
        const QString seq = QStringLiteral("Ctrl+E, Ctrl+2");
        c->subject->setShortcut(seq);
        c->mgr->saveShortcuts();
        c->conf->sync();

        QSettings fresh(c->conf->fileName(), c->conf->format());
        fresh.beginGroup(QStringLiteral("shortcuts"));
        const QString raw = fresh.value(c->subject->getId()).toString();
        fresh.endGroup();

        static const QRegularExpression sp(QStringLiteral("\\s+"));
        const QStringList parts = raw.split(sp);
        const QString first = parts.value(0);
        const QString back = QKeySequence(first).toString();
        c->note(QStringLiteral("Q3b 多键序列「%1」：内存=「%2」｜磁盘原样=「%3」"
                               "｜split %4 段 ⇒ 首段「%5」回解=「%6」%7")
                    .arg(seq, c->subject->getShortcut().toString(), raw)
                    .arg(parts.size())
                    .arg(first, back.isEmpty() ? QStringLiteral("∅空") : back,
                         (!back.isEmpty() && back == seq) ? QStringLiteral(" ✓ 恒等")
                                                          : QStringLiteral(" ✗ 不等")));
        c->note(QStringLiteral("Q3b 说明：saveShortcuts 里 `.toString().replace(\" \", \"\")` "
                               "把分隔空格删掉 ⇒ 落盘是 `Ctrl+E,Ctrl+2`（逗号分隔、无空格），"
                               "重启时 split(\\s+) **切不开** ⇒ 整段喂回 QKeySequence 正是多键序列"));
    }});

    // ─────────────────────────────────────────────────────────────────────
    // S7 Q6 重启等价（新 QSettings 实例直读同一文件；分 sync 前/后）
    // ─────────────────────────────────────────────────────────────────────
    steps->append(Step{0, [](Ctx *c) {
        if (!c->subject) { c->note(QStringLiteral("Q6 跳过（无目标）")); return; }
        const QString id = c->subject->getId();
        const QString mem = c->subject->getShortcut().toString();

        auto readDisk = [c](const QString &key) -> QString {
            QSettings fresh(c->conf->fileName(), c->conf->format());
            fresh.beginGroup(QStringLiteral("shortcuts"));
            const QString v = fresh.contains(key) ? fresh.value(key).toString()
                                                  : QStringLiteral("（无此项）");
            fresh.endGroup();
            return v;
        };

        const QString preSync = readDisk(id);
        c->conf->sync();
        const QString postSync = readDisk(id);
        // ⚠️ 首轮探针在这里报了一条**假不一致**：saveShortcuts 落盘的是「主键 备键」
        //    拼接串（备键为空时写 `""` 字面量）⇒ 拿磁盘整串与内存单键比必然不等。
        //    正确口径 = 按 StelActionMgr.cpp:59-67 的构造解析路径取**首段**。
        static const QRegularExpression spaceExp(QStringLiteral("\\s+"));
        const QString preMain = preSync.split(spaceExp).value(0);
        const QString postMain = postSync.split(spaceExp).value(0);
        c->note(QStringLiteral("Q6 目标「%1」内存=「%2」").arg(id, mem));
        c->note(QStringLiteral("Q6 磁盘原样（sync **前**）=「%1」｜（sync 后）=「%2」"
                               "（⚠️ 是「主键 备键」拼接串）")
                    .arg(preSync, postSync));
        c->note(QStringLiteral("Q6 sync 前 == sync 后：%1（⇒ QSettings 无需显式 sync）")
                    .arg(preSync == postSync ? QStringLiteral("是") : QStringLiteral("否")));
        // ⚠️ 第二处口径坑：落盘会**规范化字符串**（`Ctrl+E, Ctrl+2` → `Ctrl+E,Ctrl+2`，
        //    分隔空格被 `replace(" ","")` 删掉）⇒ 字符串比会误报"读不回"。
        //    正确口径 = 比**序列语义**（`QKeySequence ==`）。
        const bool sameSeq = (QKeySequence(postMain) == QKeySequence(mem));
        c->note(QStringLiteral("Q6 按构造解析路径取首段：磁盘「%1」vs 内存「%2」"
                               "｜字符串恒等=%3｜**序列语义恒等=%4** ⇒ %5")
                    .arg(postMain, mem)
                    .arg(postMain == mem ? QStringLiteral("是") : QStringLiteral("否"))
                    .arg(sameSeq ? QStringLiteral("是") : QStringLiteral("否"))
                    .arg(sameSeq ? QStringLiteral("重启读回同一序列 ✓（字符串形态可能被规范化）")
                                 : QStringLiteral("⚠️ 语义都不同 ⇒ 真读不回")));
    }});

    // ─────────────────────────────────────────────────────────────────────
    // S8 Q5 空串 = 移除（语义 + 落盘形态 + 恢复时怎么解析）
    // ─────────────────────────────────────────────────────────────────────
    steps->append(Step{1, [](Ctx *c) {
        if (!c->subject) { c->note(QStringLiteral("Q5 跳过（无目标）")); return; }
        const QString id = c->subject->getId();
        c->subject->setShortcut(QString());
        c->mgr->saveShortcuts();
        c->conf->sync();

        QSettings fresh(c->conf->fileName(), c->conf->format());
        fresh.beginGroup(QStringLiteral("shortcuts"));
        const bool present = fresh.contains(id);
        const QString raw = present ? fresh.value(id).toString() : QStringLiteral("（无此项）");
        fresh.endGroup();

        // 复刻 StelAction 构造的解析路径（StelActionMgr.cpp:59-67）
        static const QRegularExpression spaceExp(QStringLiteral("\\s+"));
        const QStringList parts = raw.split(spaceExp);
        QStringList parsed;
        for (const QString &p : parts)
            parsed << QStringLiteral("「%1」→%2")
                          .arg(p, QKeySequence(p).toString().isEmpty()
                                      ? QStringLiteral("空序列")
                                      : QKeySequence(p).toString());

        c->note(QStringLiteral("Q5 setShortcut(\"\") 后：磁盘项存在=%1｜原样值=「%2」（%3 字符）")
                    .arg(present ? QStringLiteral("是") : QStringLiteral("否"),
                         raw)
                    .arg(raw.size()));
        c->note(QStringLiteral("Q5 复刻构造解析：split → %1 段 ⇒ %2")
                    .arg(parts.size())
                    .arg(parsed.join(QStringLiteral("｜"))));
        c->note(QStringLiteral("Q5 直接解析对照：QKeySequence(\"\").toString()=「%1」"
                               "｜QKeySequence(\"\\\"\\\"\").toString()=「%2」")
                    .arg(QKeySequence(QString()).toString(),
                         QKeySequence(QStringLiteral("\"\"")).toString()));
        c->note(QStringLiteral("Q5 内存回读（setShortcut(\"\") 后）=「%1」")
                    .arg(c->subject->getShortcut().toString()));
    }});

    // ─────────────────────────────────────────────────────────────────────
    // S9 Q7 restoreDefaultShortcut 的波及面
    // ─────────────────────────────────────────────────────────────────────
    steps->append(Step{1, [](Ctx *c) {
        if (!c->subject) { c->note(QStringLiteral("Q7 跳过（无目标）")); return; }
        // 先造出"两个自定义项"的现场：subject 与 toggle 都设成自定义值
        c->remember(c->toggle);
        c->subject->setShortcut(QStringLiteral("Ctrl+Alt+Shift+F9"));
        c->toggle->setShortcut(QStringLiteral("Ctrl+Alt+Shift+F8"));
        c->mgr->saveShortcuts();
        c->conf->sync();

        auto shortGroup = [c]() {
            QMap<QString, QString> m;
            c->conf->beginGroup(QStringLiteral("shortcuts"));
            const QStringList ks = c->conf->allKeys();
            for (const QString &k : ks)
                m.insert(k, c->conf->value(k).toString());
            c->conf->endGroup();
            return m;
        };
        const QMap<QString, QString> before = shortGroup();

        // 只恢复 subject 一个
        const QString subBefore = c->subject->getShortcut().toString();
        c->mgr->restoreDefaultShortcut(c->subject);
        c->conf->sync();
        const QMap<QString, QString> after = shortGroup();

        const QString subAfter = c->subject->getShortcut().toString();
        const QString togAfter = c->toggle->getShortcut().toString();
        const bool subjectInGroup = after.contains(c->subject->getId());
        const bool toggleInGroup = after.contains(c->toggle->getId());

        c->note(QStringLiteral("Q7 单个恢复 subject：键「%1」→「%2」（默认）")
                    .arg(subBefore, subAfter));
        c->note(QStringLiteral("Q7 现场：shortcuts 组 %1 项 → %2 项")  // NOLINT
                    .arg(before.size())
                    .arg(after.size()));
        c->note(QStringLiteral("Q7 **被恢复者在组里**=%1（等于默认 ⇒ 应被 saveShortcuts 剔除）"
                               "｜**另一个自定义项在组里**=%2（键=「%3」，值=%4）")
                    .arg(subjectInGroup ? QStringLiteral("是") : QStringLiteral("否"),
                         toggleInGroup ? QStringLiteral("是") : QStringLiteral("否"),
                         togAfter,
                         after.contains(c->toggle->getId())
                             ? after.value(c->toggle->getId())
                             : QStringLiteral("（不在组里 ⇒ 已丢）")));
        c->note(QStringLiteral("Q7 结论：%1")
                    .arg(toggleInGroup
                             ? QStringLiteral("单个恢复**保留**其它自定义项（整组重写但按默认值剔除）")
                             : QStringLiteral("⚠️ 单个恢复**冲掉**其它自定义项 ⇒ 产品侧禁止直接用它")));
    }});

    // ─────────────────────────────────────────────────────────────────────
    // S10 Q8 冲突数据面
    // ─────────────────────────────────────────────────────────────────────
    steps->append(Step{0, [](Ctx *c) {
        const QList<StelAction *> all = c->mgr->getActionList();
        QMultiMap<QString, QString> byShortcut;   // 键 → 动作 id（主键 + 备键都算）
        for (StelAction *a : all)
        {
            const QString m = a->getShortcut().toString();
            if (!m.isEmpty())
                byShortcut.insert(m, a->getId());
            const QString alt = a->getAltShortcut().toString();
            if (!alt.isEmpty())
                byShortcut.insert(alt, a->getId() + QStringLiteral("(备)"));
        }
        const QStringList uniq = byShortcut.uniqueKeys();
        QStringList collided;
        for (const QString &k : uniq)
            if (byShortcut.count(k) > 1)
                collided << k;
        c->note(QStringLiteral("Q8 全量扫描：非空键 %1 种｜**重复键 %2 种**")
                    .arg(uniq.size())
                    .arg(collided.size()));
        if (!collided.isEmpty())
        {
            int shown = 0;
            for (const QString &k : collided)
            {
                if (shown >= 3)
                    break;
                c->note(QStringLiteral("Q8 冲突「%1」→ %2")
                            .arg(k, byShortcut.values(k).join(QStringLiteral(" + "))));
                ++shown;
            }
            // findActionFromShortcut vs routeKey 对同一冲突键各选谁
            const QString k0 = collided.first();
            StelAction *found = c->mgr->findActionFromShortcut(k0);
            const QStringList who = byShortcut.values(k0);
            c->note(QStringLiteral("Q8 冲突键「%1」：findActionFromShortcut 返「%2」"
                                   "｜参与方 %3 个（%4）")
                        .arg(k0,
                             found ? found->getId() : QStringLiteral("∅"))
                        .arg(who.size())
                        .arg(who.join(QStringLiteral(", "))));
            c->note(QStringLiteral("Q8 语义对照：findActionFromShortcut 取**遍历中最后一个**；"
                                   "ActionRouter::routeKey 取**首个 ExactMatch** ⇒ 两者相反"));
        }
        else
            c->note(QStringLiteral("Q8 出厂注册表**无重复键**"));
    }});

    // ─────────────────────────────────────────────────────────────────────
    // S11 Q9 actionsEnabled 门
    // ─────────────────────────────────────────────────────────────────────
    steps->append(Step{0, [](Ctx *c) {
        // 用一个几乎不可能被注册的键，避免真触发
        const int probeKey = Qt::Key_F24;
        c->mgr->setAllActionsEnabled(false);
        const bool off = c->mgr->pushKey(probeKey, false);
        c->mgr->setAllActionsEnabled(true);
        const bool on = c->mgr->pushKey(probeKey, false);
        c->note(QStringLiteral("Q9 pushKey(F24)：actionsEnabled=false → %1｜true → %2"
                               "（两者都应为 false —— 该键不该命中任何动作）")
                    .arg(off ? QStringLiteral("true") : QStringLiteral("false"),
                         on ? QStringLiteral("true") : QStringLiteral("false")));
        // 真门测试（**成对**，否则"门关返回 false"可能是"压根没匹配"的假绿）：
        // 用 S9 刚给 toggle 设的那个**确实存在**的组合键。
        if (c->toggle)
        {
            const int combo = Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_F8;
            const bool was = c->toggle->isChecked();
            c->mgr->setAllActionsEnabled(false);
            const bool gated = c->mgr->pushKey(combo, false);
            const bool froze = (c->toggle->isChecked() == was);
            c->mgr->setAllActionsEnabled(true);
            const bool open = c->mgr->pushKey(combo, false);
            const bool moved = (c->toggle->isChecked() != was);
            if (moved)   // 按回原状
                c->mgr->pushKey(combo, false);
            const bool backOk = (c->toggle->isChecked() == was);
            const QString gateTxt =
                QStringLiteral("门关=%1（状态冻结=%2）｜门开=%3（状态动了=%4，已按回=%5）")
                    .arg(gated ? QStringLiteral("true") : QStringLiteral("false"))
                    .arg(froze ? QStringLiteral("是") : QStringLiteral("否"))
                    .arg(open ? QStringLiteral("true") : QStringLiteral("false"))
                    .arg(moved ? QStringLiteral("是") : QStringLiteral("否"))
                    .arg(backOk ? QStringLiteral("是") : QStringLiteral("否"));
            c->note(QStringLiteral("Q9 成对：pushKey(「%1」) %2 ⇒ 门确实挡在 pushKey 最前（:248-249）")
                        .arg(c->toggle->getShortcut().toString(), gateTxt));
        }
        c->note(QStringLiteral("Q9 ⚠️ routeKey **不查** actionsEnabled（ActionRouter.cpp:141-160）"
                               "⇒ 编辑期间想屏蔽动作不能只靠它"));
    }});

    // ─────────────────────────────────────────────────────────────────────
    // S12 Q12 收尾还原 + 基线核对
    // ─────────────────────────────────────────────────────────────────────
    steps->append(Step{0, [](Ctx *c) {
        // 逐项还原动过的动作
        for (const QString &id : c->touched)
        {
            StelAction *a = c->mgr->findAction(id);
            if (!a)
                continue;
            a->setShortcut(c->origMain.value(id));
            a->setAltShortcut(c->origAlt.value(id));
        }
        c->mgr->saveShortcuts();
        c->conf->sync();

        QObject::disconnect(c->connChanged);
        QObject::disconnect(c->connShortcuts);

        const QMap<QString, QVariant> nowAll = dumpSettings(c->conf);
        QMap<QString, QVariant> nb = c->baseAll, na = nowAll;
        // 只比 shortcuts 组以外 + shortcuts 组本身
        const QString d = diffDumpText(nb, na);
        c->note(QStringLiteral("Q12 还原动作 %1 个：%2")
                    .arg(c->touched.size())
                    .arg(c->touched.join(QStringLiteral(", "))));
        c->note(QStringLiteral("Q12 还原后整本指纹 vs S1 基线：%1").arg(d));
        c->conf->beginGroup(QStringLiteral("shortcuts"));
        const QStringList nowShort = c->conf->allKeys();
        c->conf->endGroup();
        c->note(QStringLiteral("Q12 shortcuts 组：基线 %1 项 → 现在 %2 项 %3")
                    .arg(c->baseShort.size())
                    .arg(nowShort.size())
                    .arg((c->baseShort.size() == nowShort.size()) ? QStringLiteral("（项数一致）")
                                                                  : QStringLiteral("⚠️ 不一致")));
        c->r.summary = QStringLiteral("快捷键命令面读数完毕（动作 %1 / 分组 %2 / 靶 %3）")
                           .arg(c->mgr->getActionList().size())
                           .arg(c->mgr->getGroupList().size())
                           .arg(c->subject ? c->subject->getId() : QStringLiteral("∅"));
    }});

    // 步骤调度器（照抄 HiDpiProbe / DisplayCheck）：每步跑完等 delayAfter 再进下一步。
    *tick = [ctx, steps, idx, tick, onDone]() {
        if (*idx >= steps->size())
        {
            ProbeResult r = ctx->r;
            delete ctx;
            onDone(r);
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
