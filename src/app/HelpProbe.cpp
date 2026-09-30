// HelpProbe 实现。设计说明与问题表见 HelpProbe.hpp（同一套口径）。
#include "app/HelpProbe.hpp"

#if defined(STELQUICK_HAS_ENGINE)

#include "core/StelActionMgr.hpp"   // StelAction / StelActionMgr 完整定义（StelApp.hpp 里只有前向声明）
#include "core/StelApp.hpp"
#include "core/StelFileMgr.hpp"
#include "core/StelTranslator.hpp"
#include "core/StelUtils.hpp"
#include "gui/ContributorsList.hpp"

#include <QApplication>   // QApplication::allWidgets()（Q13 老 GUI 活性裁决）
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QSettings>
#include <QSysInfo>
#include <QTimer>
#include <QWidget>

#include <cstdio>

namespace stelapp {

namespace {

using ProbeResult = HelpProbe::Result;

// ─────────────────────────────────────────────────────────────────────────────
// 小工具（全部只读）
// ─────────────────────────────────────────────────────────────────────────────

//! 一个文件的"人读一行"：存在性 + 字节数 + 行数 + md5（前 12 位）。
//! 不存在时只回报"缺失"，不抛错（缺失本身就是读数）。
QString fileFact(const QString &path)
{
    const QFileInfo fi(path);
    if (!fi.exists())
        return QStringLiteral("缺失（%1）").arg(path);
    if (!fi.isFile())
        return QStringLiteral("非普通文件（%1）").arg(path);
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return QStringLiteral("存在但打不开（%1，%2 B）").arg(path).arg(fi.size());
    const QByteArray data = f.readAll();
    f.close();
    int lines = 0;
    for (const char c : data)
        if (c == '\n')
            ++lines;
    const QString md5 = QString::fromLatin1(
        QCryptographicHash::hash(data, QCryptographicHash::Md5).toHex().left(12));
    return QStringLiteral("存在｜%1 B｜%2 行｜md5=%3｜%4")
        .arg(fi.size())
        .arg(lines)
        .arg(md5, path);
}

//! 列一个目录的条目数 + 前几个名字（目录不存在时如实说）。
QString dirFact(const QString &path)
{
    QDir d(path);
    if (!d.exists())
        return QStringLiteral("目录不存在（%1）").arg(path);
    const QStringList names = d.entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
    if (names.isEmpty())
        return QStringLiteral("空目录（%1）").arg(path);
    const QStringList head = names.mid(0, 6);
    return QStringLiteral("%1 项[%2%3]（%4）")
        .arg(names.size())
        .arg(head.join(QStringLiteral(", ")))
        .arg(names.size() > head.size() ? QStringLiteral(", …") : QString(), path);
}

//! 只读探测一个 QFile 路径（含 `:/` 与 `qrc:/` 两种前缀）。
QString qrcFact(const QString &resPath)
{
    QFile f(resPath);
    if (!f.exists())
        return QStringLiteral("未命中（%1）").arg(resPath);
    if (!f.open(QIODevice::ReadOnly))
        return QStringLiteral("命中但打不开（%1）").arg(resPath);
    const qint64 n = f.size();
    f.close();
    return QStringLiteral("命中｜%1 B（%2）").arg(n).arg(resPath);
}

//! 把 `QStringList` 压成一行（超过 8 条折叠）。
QString briefList(const QStringList &v, int keep = 8)
{
    if (v.isEmpty())
        return QStringLiteral("∅");
    if (v.size() <= keep)
        return v.join(QStringLiteral(", "));
    return v.mid(0, keep).join(QStringLiteral(", "))
        + QStringLiteral(" …(共 %1)").arg(v.size());
}

//! 当前"可见顶层窗口"快照（含 widget 总数），用于 Q14 的"触发前/后"对照。
//! ⚠️ 只统计 `isWindow() && isVisible()`，且**按类名+标题**（不是裸计数 ——
//! 计数相同不代表集合相同，T40 的"负控样本撞车"同族教训）。
#if defined(STELQUICK_WIDGETS_HOST)
QString widgetSnapshot(int *totalOut = nullptr)
{
    const QList<QWidget *> ws = QApplication::allWidgets();
    if (totalOut)
        *totalOut = ws.size();
    QStringList vis;
    for (QWidget *w : ws)
    {
        if (!w || !w->isWindow() || !w->isVisible())
            continue;
        const QString cn = QString::fromLatin1(w->metaObject()->className());
        const QString t = w->windowTitle();
        vis << (t.isEmpty() ? cn : QStringLiteral("%1(「%2」)").arg(cn, t));
    }
    return briefList(vis, 10);
}
#else
QString widgetSnapshot(int *totalOut = nullptr)
{
    if (totalOut)
        *totalOut = 0;
    return QStringLiteral("（非 Widgets 宿主）");
}
#endif

// ─────────────────────────────────────────────────────────────────────────────
// 上下文
// ─────────────────────────────────────────────────────────────────────────────

struct Ctx
{
    QCoreApplication *app = nullptr;
    ProbeResult r;
    std::function<void(const ProbeResult &)> onDone;   //!< ⚠️ **必须是成员**（T40 血泪：
                                                       //!< finish() 里调的若是成员而 run()
                                                       //!< 忘了赋值 ⇒ std::bad_function_call）
    QStringList touchedUnused;   //!< 占位（本轮零写入，保留槽位以便将来扩展）
    QString widgetBaseline;      //!< Q14：触发前的可见顶层窗口快照
    int widgetBaseTotal = 0;     //!< Q14：触发前的 QWidget 总数
    void note(const QString &s) { r.details << s; }

    void finish()
    {
        r.summary = QStringLiteral("帮助/版本/许可证数据面读数完毕（只读，零写入）");
        if (onDone)
            onDone(r);
    }
};

struct Step
{
    int delayAfter = 0;
    std::function<void(Ctx *)> body;
};

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// run
// ─────────────────────────────────────────────────────────────────────────────

void HelpProbe::run(QCoreApplication *app,
                    const std::function<void(const Result &)> &onDone,
                    int delayMs)
{
#if !defined(STELQUICK_HAS_ENGINE)
    Q_UNUSED(app)
    Q_UNUSED(delayMs)
    Result r;
    r.unavailable = true;
    r.summary = QStringLiteral("帮助/版本探针需要合流形态构建（STELQUICK_HAS_ENGINE）");
    onDone(r);
#else
    if (!StelApp::isInitialized())
    {
        Result r;
        r.unavailable = true;
        r.summary = QStringLiteral("引擎未引导 ⇒ StelFileMgr 的搜索路径未建立，"
                                   "版本/许可证可达性无从观测");
        onDone(r);
        return;
    }

    auto ctx = new Ctx();
    ctx->app = app;
    ctx->onDone = onDone;          // ⚠️ 先赋值再跑（T40 血泪）
    ctx->r.ran = true;

    auto steps = std::make_shared<QVector<Step>>();
    auto idx = std::make_shared<int>(0);
    auto tick = std::make_shared<std::function<void()>>();

    // ═══════════════════════════════════════════════════════════════════════
    // S1 版本面：四件套 + 编译期宏（Q1/Q2）
    // ═══════════════════════════════════════════════════════════════════════
    steps->append(Step{0, [](Ctx *c) {
        c->note(QStringLiteral("Q1 getApplicationName()          = 「%1」")
                    .arg(StelUtils::getApplicationName()));
        c->note(QStringLiteral("Q1 getApplicationVersion()       = 「%1」")
                    .arg(StelUtils::getApplicationVersion()));
        c->note(QStringLiteral("Q1 getApplicationPublicVersion() = 「%1」")
                    .arg(StelUtils::getApplicationPublicVersion()));
        c->note(QStringLiteral("Q1 getApplicationSeries()        = 「%1」")
                    .arg(StelUtils::getApplicationSeries()));

        // 编译期宏：CMakeLists.txt:101-112 是 ADD_DEFINITIONS（全局）⇒ 产品侧直接可用。
#ifdef PACKAGE_VERSION
        c->note(QStringLiteral("Q2 PACKAGE_VERSION            = 「%1」").arg(PACKAGE_VERSION));
#else
        c->note(QStringLiteral("Q2 PACKAGE_VERSION            = **未定义**"));
#endif
#ifdef STELLARIUM_BUIDING_VERSION
        c->note(QStringLiteral("Q2 STELLARIUM_BUIDING_VERSION = 「%1」")
                    .arg(STELLARIUM_BUIDING_VERSION));
#else
        c->note(QStringLiteral("Q2 STELLARIUM_BUIDING_VERSION = **未定义**"));
#endif
#ifdef STELLARIUM_PUBLIC_VERSION
        c->note(QStringLiteral("Q2 STELLARIUM_PUBLIC_VERSION  = 「%1」")
                    .arg(STELLARIUM_PUBLIC_VERSION));
#else
        c->note(QStringLiteral("Q2 STELLARIUM_PUBLIC_VERSION  = **未定义**"));
#endif
#ifdef STELLARIUM_SERIES
        c->note(QStringLiteral("Q2 STELLARIUM_SERIES          = 「%1」").arg(STELLARIUM_SERIES));
#else
        c->note(QStringLiteral("Q2 STELLARIUM_SERIES          = **未定义**"));
#endif
#ifdef STELLARIUM_COPYRIGHT
        // ⚠️ 这个宏里带 (C) 与空格，是"许可证页/关于页"那一行版权的**唯一权威来源**。
        c->note(QStringLiteral("Q7 STELLARIUM_COPYRIGHT       = 「%1」")
                    .arg(STELLARIUM_COPYRIGHT));
#else
        c->note(QStringLiteral("Q7 STELLARIUM_COPYRIGHT       = **未定义**（关于页版权行无源）"));
#endif
#ifdef GIT_REVISION
        c->note(QStringLiteral("Q1 GIT_REVISION               = 「%1」").arg(GIT_REVISION));
#else
        c->note(QStringLiteral("Q1 GIT_REVISION               = **未定义**"
                               "（版本串退化成纯 PACKAGE_VERSION）"));
#endif
#ifdef GIT_BRANCH
        c->note(QStringLiteral("Q1 GIT_BRANCH                 = 「%1」").arg(GIT_BRANCH));
#endif
    }});

    // ═══════════════════════════════════════════════════════════════════════
    // S2 运行环境串（Q3）：版本页要显示、且要中文化的几条
    // ═══════════════════════════════════════════════════════════════════════
    steps->append(Step{0, [](Ctx *c) {
        c->note(QStringLiteral("Q3 getOperatingSystemInfo()      = 「%1」")
                    .arg(StelUtils::getOperatingSystemInfo()));
        c->note(QStringLiteral("Q3 getAddressingMode()           = 「%1」")
                    .arg(StelUtils::getAddressingMode()));
        c->note(QStringLiteral("Q3 getUserAgentString()          = 「%1」")
                    .arg(StelUtils::getUserAgentString()));
        c->note(QStringLiteral("Q3 QSysInfo::prettyProductName() = 「%1」")
                    .arg(QSysInfo::prettyProductName()));
        c->note(QStringLiteral("Q3 QSysInfo::kernelVersion()     = 「%1」")
                    .arg(QSysInfo::kernelVersion()));
        c->note(QStringLiteral("Q3 QSysInfo::cpuArchitecture()   = 「%1」")
                    .arg(QSysInfo::currentCpuArchitecture()));
        c->note(QStringLiteral("Q3 QSysInfo::productVersion()    = 「%1」")
                    .arg(QSysInfo::productVersion()));
        c->note(QStringLiteral("Q3 QSysInfo::buildAbi()          = 「%1」")
                    .arg(QSysInfo::buildAbi()));
        c->note(QStringLiteral("Q3 QSysInfo::buildCpuArchitecture() = 「%1」")
                    .arg(QSysInfo::buildCpuArchitecture()));
    }});

    // ═══════════════════════════════════════════════════════════════════════
    // S3 编译期 vs 运行期 Qt（Q4）：不一致 ⇒ 版本页必须如实显示
    // ═══════════════════════════════════════════════════════════════════════
    steps->append(Step{0, [](Ctx *c) {
        const QString built = QStringLiteral(QT_VERSION_STR);
        const QString running = QString::fromLatin1(qVersion());
        c->note(QStringLiteral("Q4 QT_VERSION_STR（编译期）= 「%1」").arg(built));
        c->note(QStringLiteral("Q4 qVersion()（运行期）    = 「%1」 ⇒ 一致=%2")
                    .arg(running,
                         built == running ? QStringLiteral("是") : QStringLiteral("**否**")));
        c->note(QStringLiteral("Q4 QCoreApplication::applicationDirPath() = 「%1」")
                    .arg(QCoreApplication::applicationDirPath()));
        c->note(QStringLiteral("Q4 QCoreApplication::applicationVersion() = 「%1」")
                    .arg(QCoreApplication::applicationVersion().isEmpty()
                             ? QStringLiteral("∅（未设置）")
                             : QCoreApplication::applicationVersion()));
        c->note(QStringLiteral("Q4 QCoreApplication::applicationName()    = 「%1」")
                    .arg(QCoreApplication::applicationName()));
    }});

    // ═══════════════════════════════════════════════════════════════════════
    // S4 贡献者名单（关于页数据面）
    // ═══════════════════════════════════════════════════════════════════════
    steps->append(Step{0, [](Ctx *c) {
        const QStringList raw = StelContributors::contributorsList;
        QStringList sorted = raw;
        sorted.sort(Qt::CaseInsensitive);
        sorted.removeDuplicates();
        int nonAscii = 0;
        for (const QString &s : sorted)
            for (const QChar ch : s)
                if (ch.unicode() > 127) { ++nonAscii; break; }
        c->note(QStringLiteral("S4 贡献者：原表 %1 条｜排序去重后 %2 条｜含非 ASCII 名 %3 条")
                    .arg(raw.size()).arg(sorted.size()).arg(nonAscii));
        c->note(QStringLiteral("S4 前 6 条：%1").arg(briefList(sorted, 6)));
        c->note(QStringLiteral("S4 末 4 条：%1")
                    .arg(briefList(sorted.mid(qMax(0, sorted.size() - 4)), 4)));
    }});

    // ═══════════════════════════════════════════════════════════════════════
    // S5 🔴 许可证文件可达性（Q5）—— 本轮最大风险点
    // ═══════════════════════════════════════════════════════════════════════
    steps->append(Step{0, [](Ctx *c) {
        c->note(QStringLiteral("── Q5 许可证文件（COPYING，GPL-2.0 全文）可达性 ──"));

        // ① 源码树：用编译期 __FILE__ 反推（CMake 一般给绝对路径）。
        const QString thisFile = QString::fromUtf8(__FILE__);
        c->note(QStringLiteral("Q5 __FILE__（编译期）= 「%1」 绝对=%2")
                    .arg(thisFile, QFileInfo(thisFile).isAbsolute() ? QStringLiteral("是")
                                                                    : QStringLiteral("否")));
        const QDir srcApp = QFileInfo(thisFile).absoluteDir();      // …/src/app
        const QString srcRoot = QDir::cleanPath(srcApp.absoluteFilePath(QStringLiteral("../..")));
        c->note(QStringLiteral("Q5 推导源码树根 = 「%1」").arg(srcRoot));
        c->note(QStringLiteral("Q5 源码树 ./COPYING ⇒ %1").arg(fileFact(srcRoot + "/COPYING")));
        c->note(QStringLiteral("Q5 源码树 ./src/gui/HelpDialog.cpp ⇒ %1")
                    .arg(fileFact(srcRoot + "/src/gui/HelpDialog.cpp")));

        // ② 引擎的搜索路径：findFile 是否命中（GPL 副本有没有被装到数据目录）
        QString ff;
        ff = StelFileMgr::findFile(QStringLiteral("COPYING"));
        c->note(QStringLiteral("Q5 StelFileMgr::findFile(\"COPYING\") ⇒ %1")
                    .arg(ff.isEmpty() ? QStringLiteral("**未命中**") : QStringLiteral("命中「%1」").arg(ff)));
        ff = StelFileMgr::findFile(QStringLiteral("LICENSE"));
        c->note(QStringLiteral("Q5 StelFileMgr::findFile(\"LICENSE\") ⇒ %1")
                    .arg(ff.isEmpty() ? QStringLiteral("**未命中**") : QStringLiteral("命中「%1」").arg(ff)));
        ff = StelFileMgr::findFile(QStringLiteral("gpl-2.0.txt"));
        c->note(QStringLiteral("Q5 StelFileMgr::findFile(\"gpl-2.0.txt\") ⇒ %1")
                    .arg(ff.isEmpty() ? QStringLiteral("**未命中**") : QStringLiteral("命中「%1」").arg(ff)));

        // ③ 引擎各目录实况
        c->note(QStringLiteral("Q5 StelFileMgr::getUserDir()         = 「%1」")
                    .arg(StelFileMgr::getUserDir()));
        c->note(QStringLiteral("Q5 StelFileMgr::getInstallationDir() = 「%1」")
                    .arg(StelFileMgr::getInstallationDir()));
        c->note(QStringLiteral("Q5 用户目录 ⇒ %1").arg(dirFact(StelFileMgr::getUserDir())));
        c->note(QStringLiteral("Q5 安装目录 ⇒ %1").arg(dirFact(StelFileMgr::getInstallationDir())));

        // ④ app bundle 实况（运行时到底带没带）
        const QDir appDir(QCoreApplication::applicationDirPath());          // …/stelQuickUI.app/Contents/MacOS
        const QString contents = QDir::cleanPath(appDir.absoluteFilePath(QStringLiteral("..")));
        c->note(QStringLiteral("Q5 applicationDirPath() = 「%1」").arg(appDir.absolutePath()));
        c->note(QStringLiteral("Q5 bundle Contents/ ⇒ %1").arg(dirFact(contents)));
        c->note(QStringLiteral("Q5 bundle Contents/Resources/ ⇒ %1")
                    .arg(dirFact(contents + "/Resources")));
        c->note(QStringLiteral("Q5 bundle Contents/COPYING ⇒ %1").arg(fileFact(contents + "/COPYING")));
        c->note(QStringLiteral("Q5 bundle Contents/Resources/COPYING ⇒ %1")
                    .arg(fileFact(contents + "/Resources/COPYING")));
        // 二进制的**同级**也看一眼（非 bundle 布局的 fallback）
        c->note(QStringLiteral("Q5 可执行同级 COPYING ⇒ %1")
                    .arg(fileFact(appDir.absoluteFilePath(QStringLiteral("COPYING")))));
    }});

    // ═══════════════════════════════════════════════════════════════════════
    // S6 🔴 帮助动作悬空裁决 + F1..F12 归属表（Q8）
    // ═══════════════════════════════════════════════════════════════════════
    steps->append(Step{0, [](Ctx *c) {
        StelActionMgr *m = StelApp::getInstance().getStelActionManager();
        if (!m)
        {
            c->note(QStringLiteral("Q8 **StelActionMgr 为 null** ⇒ 动作面无从观测"));
            return;
        }
        c->note(QStringLiteral("── Q8 帮助入口 + 窗口动作归属 ──"));

        // 老宿主的 8 个窗口动作 id（全部注册在 src/gui/StelGui.cpp:260-271）
        const QStringList windowIds = {
            QStringLiteral("actionShow_Help_Window_Global"),
            QStringLiteral("actionShow_ScriptConsole_Window_Global"),
            QStringLiteral("actionShow_Configuration_Window_Global"),
            QStringLiteral("actionShow_Search_Window_Global"),
            QStringLiteral("actionShow_SkyView_Window_Global"),
            QStringLiteral("actionShow_DateTime_Window_Global"),
            QStringLiteral("actionShow_Location_Window_Global"),
            QStringLiteral("actionShow_Shortcuts_Window_Global"),
            QStringLiteral("actionShow_AstroCalc_Window_Global")
        };
        for (const QString &id : windowIds)
        {
            StelAction *a = m->findAction(id);
            c->note(QStringLiteral("Q8 %1 ⇒ %2")
                        .arg(id,
                             a ? QStringLiteral("**在**（组=%1 键=%2 文本=「%3」）")
                                     .arg(a->getGroup())
                                     .arg(a->getShortcut().toString().isEmpty()
                                              ? QStringLiteral("∅")
                                              : a->getShortcut().toString())
                                     .arg(a->getText())
                               : QStringLiteral("**不在注册表**（悬空）")));
        }

        // 注册表里 text 含 help 的动作（大小写不敏感）
        QStringList helpTexts;
        for (StelAction *a : m->getActionList())
        {
            if (a->getText().contains(QLatin1String("help"), Qt::CaseInsensitive)
                || a->getText().contains(QStringLiteral("帮助")))
                helpTexts << QStringLiteral("%1「%2」").arg(a->getId(), a->getText());
        }
        c->note(QStringLiteral("Q8 注册表 text 含 help/帮助 的动作：%1").arg(briefList(helpTexts)));

        // F 键归属：谁占着 F1..F12（决定 T41 的帮助入口能不能用 F1）
        const QStringList fkeys = { QStringLiteral("F1"), QStringLiteral("F2"), QStringLiteral("F3"),
                                    QStringLiteral("F4"), QStringLiteral("F5"), QStringLiteral("F6"),
                                    QStringLiteral("F7"), QStringLiteral("F8"), QStringLiteral("F9"),
                                    QStringLiteral("F10"), QStringLiteral("F11"), QStringLiteral("F12") };
        QStringList owners;
        for (const QString &k : fkeys)
        {
            StelAction *a = m->findActionFromShortcut(k);
            owners << QStringLiteral("%1→%2")
                          .arg(k, a ? a->getId() : QStringLiteral("∅"));
        }
        c->note(QStringLiteral("Q8 F1..F12 归属：%1").arg(briefList(owners, 12)));

        // routeKey 的真实反应：F1 按下会命中谁（拿 ActionRouter 的面当参照）
        StelAction *f1 = m->findActionFromShortcut(QStringLiteral("F1"));
        c->note(QStringLiteral("Q8 「F1」按 `findActionFromShortcut` 命中 = %1")
                    .arg(f1 ? f1->getId() : QStringLiteral("**没人**（F1 是空键）")));
    }});

    // ═══════════════════════════════════════════════════════════════════════
    // S7 🔴 "Windows" 组之谜（Q9）：T40 读数说分组里有它，源码里只有 StelGui 注册
    // ═══════════════════════════════════════════════════════════════════════
    steps->append(Step{0, [](Ctx *c) {
        StelActionMgr *m = StelApp::getInstance().getStelActionManager();
        if (!m)
            return;
        const QStringList groups = m->getGroupList();
        c->note(QStringLiteral("Q9 分组数 = %1").arg(groups.size()));
        c->note(QStringLiteral("Q9 分组名（顺序即 getGroupList 顺序）：%1")
                    .arg(briefList(groups, 20)));
        int wi = groups.indexOf(QStringLiteral("Windows"));
        c->note(QStringLiteral("Q9 「Windows」组存在 = %1（下标 %2）")
                    .arg(wi >= 0 ? QStringLiteral("**是**") : QStringLiteral("**否**"))
                    .arg(wi));
        if (wi >= 0)
        {
            const QList<StelAction *> acts = m->getActionList(QStringLiteral("Windows"));
            QStringList ids;
            for (StelAction *a : acts)
                ids << QStringLiteral("%1(键=%2)").arg(a->getId(),
                            a->getShortcut().toString().isEmpty() ? QStringLiteral("∅")
                                                                  : a->getShortcut().toString());
            c->note(QStringLiteral("Q9 「Windows」组成员 %1 个：%2")
                        .arg(acts.size()).arg(briefList(ids, 10)));
        }
        // 顺带：全表按组计数（给产品侧"帮助页按组列出"作规模参照）
        QStringList counts;
        for (const QString &g : groups)
            counts << QStringLiteral("%1=%2").arg(g).arg(m->getActionList(g).size());
        c->note(QStringLiteral("Q9 各组动作数：%1").arg(briefList(counts, 20)));
    }});

    // ═══════════════════════════════════════════════════════════════════════
    // S8 帮助页"手势条目"规模（Q10b）：老 HTML 里硬编码的 <tr> 有多少条
    // ═══════════════════════════════════════════════════════════════════════
    steps->append(Step{0, [](Ctx *c) {
        const QString thisFile = QString::fromUtf8(__FILE__);
        const QDir srcApp = QFileInfo(thisFile).absoluteDir();
        const QString srcRoot = QDir::cleanPath(srcApp.absoluteFilePath(QStringLiteral("../..")));
        const QString helpCpp = srcRoot + "/src/gui/HelpDialog.cpp";
        QFile f(helpCpp);
        if (!f.open(QIODevice::ReadOnly))
        {
            c->note(QStringLiteral("Q10b 读不到老 HelpDialog.cpp（%1）⇒ 手势条目数需人工统计")
                        .arg(helpCpp));
            return;
        }
        const QString text = QString::fromUtf8(f.readAll());
        f.close();
        const int trCount = text.count(QLatin1String("<tr>"));
        const int hotkeyWrap = text.count(QLatin1String("hotkeyTextWrapper("));
        const int h2 = text.count(QLatin1String("<h2 "));
        const int h3 = text.count(QLatin1String("<h3>"));
        c->note(QStringLiteral("Q10b 老 HelpDialog.cpp：%1 B｜`<tr>` %2 处｜"
                               "`hotkeyTextWrapper(` %3 处｜`<h2>` %4 处｜`<h3>` %5 处")
                    .arg(text.size()).arg(trCount).arg(hotkeyWrap).arg(h2).arg(h3));
        c->note(QStringLiteral("Q10b 判读：`<tr>` 里每一行是一张键位表行；"
                               "`hotkeyTextWrapper` 的调用点 = **来自引擎注册表**的那一半，"
                               "其余 = **硬编码手势**那一半"));
        // 老 HelpDialog.cpp 里出现的 html 行样例（前 3 条含 hotkeyTextWrapper 的）
        const QStringList lines = text.split(QLatin1Char('\n'));
        int shown = 0;
        for (const QString &ln : lines)
        {
            if (!ln.contains(QLatin1String("<tr>")))
                continue;
            c->note(QStringLiteral("Q10b 样例行：%1").arg(ln.trimmed().left(140)));
            if (++shown >= 3)
                break;
        }
    }});

    // ═══════════════════════════════════════════════════════════════════════
    // S9 翻译串可达性（Q11）：帮助页文案能不能拿到中文
    // ═══════════════════════════════════════════════════════════════════════
    steps->append(Step{0, [](Ctx *c) {
        struct Pair { const char *src; const char *note; };
        const Pair pairs[] = {
            { "Stellarium Help",  "帮助页标题" },
            { "Help window",      "帮助动作名（老 GUI）" },
            { "Keys",             "帮助页「键位」节标题" },
            { "Further Reading",  "帮助页「延伸阅读」节标题" },
            { "Version",          "关于页「版本」" },
            { "Based on Qt",      "关于页「基于 Qt」" },
            { "Contributors",     "关于页「贡献者」" },
            { "Developers",       "关于页「开发者」" },
            { "Financial support","关于页「资助」" },
            { "Copyright",        "版权" }
        };
        for (const Pair &p : pairs)
        {
            const QString en = QString::fromUtf8(p.src);
            const QString zh = q_(p.src);
            c->note(QStringLiteral("Q11 「%1」 → 「%2」%3（%4）")
                        .arg(en, zh,
                             zh == en ? QStringLiteral(" **未翻译**")
                                      : QStringLiteral(" 有译文"),
                             QString::fromUtf8(p.note)));
        }
    }});

    // ═══════════════════════════════════════════════════════════════════════
    // S10 🔴 老 GUI 的活性裁决（Q13）—— 首轮探针推翻"src/gui 不在构建里"的推断：
    //     8 个窗口动作**都在**注册表里、`getText()` 还是中文、分组里有 "Windows"、
    //     引导日志里赫然写着 `Creating GUI ...` ⇒ 必须查清老 QWidget GUI（含
    //     `HelpDialog`）在本形态下是不是**活着的**。这决定 T41 是"接管"还是"并存"。
    // ═══════════════════════════════════════════════════════════════════════
    steps->append(Step{0, [](Ctx *c) {
        c->note(QStringLiteral("── Q13 老 QWidget GUI 活性 ──"));
        StelGuiBase *gui = StelApp::getInstance().getGui();
        c->note(QStringLiteral("Q13 StelApp::getGui() 非空 = %1")
                    .arg(gui ? QStringLiteral("**是**（老 QWidget GUI 活着）")
                             : QStringLiteral("**否**（null）")));

        StelActionMgr *m = StelApp::getInstance().getStelActionManager();
        StelAction *help = m ? m->findAction(QStringLiteral("actionShow_Help_Window_Global")) : nullptr;
        if (help)
        {
            c->note(QStringLiteral("Q13 帮助动作：checkable=%1｜组=「%2」｜文本=「%3」｜键=「%4」")
                        .arg(help->isCheckable() ? QStringLiteral("是") : QStringLiteral("否"))
                        .arg(help->getGroup(), help->getText(),
                             help->getShortcut().toString()));
            c->note(QStringLiteral("Q13 ⇒ 老 GUI 若活着，该动作 toggle 的是**老 HelpDialog 的 "
                                   "visible 属性**（QWidget），QML 界面里看不见它"));
        }

        // 扫 QObject 树里类名含 Dialog / Gui 的实例（老对话框是否被创建）
        QStringList dialogClasses;
        QStringList guiClasses;
        {
            const QList<QObject *> kids = StelApp::getInstance().findChildren<QObject *>();
            QSet<QString> seen;
            for (QObject *o : kids)
            {
                if (!o)
                    continue;
                const QString cn = QString::fromLatin1(o->metaObject()->className());
                if (cn.contains(QLatin1String("Dialog")) && !seen.contains(cn))
                {
                    seen.insert(cn);
                    dialogClasses << cn;
                }
                if (cn.contains(QLatin1String("Gui")) && !guiClasses.contains(cn))
                    guiClasses << cn;
            }
        }
        c->note(QStringLiteral("Q13 树里类名含 Gui 的实例：%1").arg(briefList(guiClasses, 6)));
        c->note(QStringLiteral("Q13 树里类名含 Dialog 的实例 %1 种：%2")
                    .arg(dialogClasses.size()).arg(briefList(dialogClasses, 12)));

        // 最直接：QApplication 的全部 widget —— 有没有顶层窗口活着
#ifdef STELQUICK_WIDGETS_HOST
        {
            const QList<QWidget *> ws = QApplication::allWidgets();
            int visibleTop = 0;
            QStringList topNames;
            for (QWidget *w : ws)
            {
                if (!w)
                    continue;
                if (w->isWindow())
                {
                    const QString cn = QString::fromLatin1(w->metaObject()->className());
                    if (w->isVisible())
                    {
                        ++visibleTop;
                        topNames << QStringLiteral("%1(「%2」)").arg(cn, w->windowTitle());
                    }
                }
            }
            c->note(QStringLiteral("Q13 QWidget 总数=%1｜顶层窗口=%2｜可见顶层=%3")
                        .arg(ws.size())
                        .arg([&ws]{
                            int n = 0;
                            for (QWidget *w : ws)
                                if (w && w->isWindow()) ++n;
                            return n;
                        }())
                        .arg(visibleTop));
            c->note(QStringLiteral("Q13 可见顶层窗口：%1").arg(briefList(topNames, 8)));
        }
#else
        c->note(QStringLiteral("Q13 非 Widgets 宿主 ⇒ 跳过 QApplication::allWidgets()"));
#endif
        c->note(QStringLiteral("Q13 判读：老 GUI 活着 ⇒ T41 要决定是**接管** F1（重定向到 "
                               "QML 帮助页，抑制老对话框）还是**并存**（QML 工具栏入口独立）"));
    }});

    // ═══════════════════════════════════════════════════════════════════════
    // S11 🔴 F1 触发实测（Q14）—— Q13 只证"动作在册 + getGui() 非空"，
    //     还没证"按下去到底发生什么"。`StelDialog::setVisible(true)` 是**惰性建 UI**
    //     （`StelDialog.cpp:112 new QDialog(nullptr)` + `:116 createDialogContent()`）
    //     并挂到 `StelMainView` 的 QGraphicsScene 上 ⇒ 触发一次就能看到真相。
    //     ⚠️ 三步式（布场 → 触发 → 还原），每步隔 400ms 让惰性创建走完。
    // ═══════════════════════════════════════════════════════════════════════
    steps->append(Step{0, [](Ctx *c) {
        c->widgetBaseline = widgetSnapshot(&c->widgetBaseTotal);
        QStringList allWidgetClasses;
        {
            QSet<QString> seen;
            for (QWidget *w : QApplication::allWidgets())
            {
                if (!w)
                    continue;
                const QString cn = QString::fromLatin1(w->metaObject()->className());
                if (!seen.contains(cn))
                {
                    seen.insert(cn);
                    allWidgetClasses << cn;
                }
            }
        }
        c->note(QStringLiteral("Q14 触发前：QWidget 总数=%1｜可见顶层=%2")
                    .arg(c->widgetBaseTotal).arg(c->widgetBaseline));
        c->note(QStringLiteral("Q14 触发前全部 widget 类（去重）：%1")
                    .arg(briefList(allWidgetClasses, 14)));
    }});

    steps->append(Step{400, [](Ctx *c) {
        StelActionMgr *m = StelApp::getInstance().getStelActionManager();
        StelAction *help = m ? m->findAction(QStringLiteral("actionShow_Help_Window_Global")) : nullptr;
        if (!help)
        {
            c->note(QStringLiteral("Q14 帮助动作不在册 ⇒ 跳过触发实测"));
            return;
        }
        c->note(QStringLiteral("Q14 `trigger()` 前 checked=%1")
                    .arg(help->isCheckable() ? help->isChecked() : false));
        help->trigger();
        int now = 0;
        const QString after = widgetSnapshot(&now);
        c->note(QStringLiteral("Q14 `trigger()` 后：QWidget 总数=%1（Δ%2）｜可见顶层=%3")
                    .arg(now).arg(now - c->widgetBaseTotal).arg(after));
        c->note(QStringLiteral("Q14 `trigger()` 后 checked=%1")
                    .arg(help->isCheckable() ? help->isChecked() : false));
        // 老 HelpDialog 惰性创建后，树里该出现类名含 Dialog 的实例
        QStringList dialogs;
        {
            QSet<QString> seen;
            for (QObject *o : QApplication::allWidgets())
            {
                if (!o)
                    continue;
                const QString cn = QString::fromLatin1(o->metaObject()->className());
                if (cn.contains(QLatin1String("Dialog")) && !seen.contains(cn))
                {
                    seen.insert(cn);
                    dialogs << cn;
                }
            }
        }
        c->note(QStringLiteral("Q14 触发后类名含 Dialog 的 widget：%1").arg(briefList(dialogs, 12)));
    }});

    steps->append(Step{400, [](Ctx *c) {
        StelActionMgr *m = StelApp::getInstance().getStelActionManager();
        StelAction *help = m ? m->findAction(QStringLiteral("actionShow_Help_Window_Global")) : nullptr;
        if (!help)
            return;
        // 还原：再 toggle 一次（checkable 动作触发两次 = 回原状）
        help->trigger();
        int now = 0;
        const QString after = widgetSnapshot(&now);
        c->note(QStringLiteral("Q14 **还原**后：QWidget 总数=%1（相对基线 Δ%2）｜可见顶层=%3")
                    .arg(now).arg(now - c->widgetBaseTotal).arg(after));
        c->note(QStringLiteral("Q14 判读：Δ>0 且出现 HelpDialog ⇒ **F1 在本形态下会弹出老 QWidget "
                               "对话框**（QML 界面之上）⇒ T41 必须决定「接管」还是「并存」；"
                               "Δ=0 ⇒ 老对话框在本形态下起不来，T41 只管自建 QML 入口"));
    }});

    // ═══════════════════════════════════════════════════════════════════════
    // S12 收尾（Q12）：只读探针 ⇒ 配置指纹应与基线逐项相同
    // ═══════════════════════════════════════════════════════════════════════
    steps->append(Step{0, [](Ctx *c) {
        QSettings *conf = StelApp::getInstance().getSettings();
        if (!conf)
        {
            c->note(QStringLiteral("Q12 QSettings 为 null ⇒ 跳过配置核对"));
            return;
        }
        c->note(QStringLiteral("Q12 配置文件 = 「%1」｜顶层组数 = %2")
                    .arg(conf->fileName()).arg(conf->childGroups().size()));
        c->note(QStringLiteral("Q12 本轮探针**只读**：未调用任何 setValue / saveShortcuts / "
                               "setShortcut ⇒ 配置零写入（结构性成立）"));
        c->note(QStringLiteral("Q12 shortcuts 组项数 = %1（应与 T40 收尾后的值一致）")
                    .arg([conf]{
                        conf->beginGroup(QStringLiteral("shortcuts"));
                        const int n = conf->childKeys().size();
                        conf->endGroup();
                        return n;
                    }()));
    }});

    // ── tick 链 ──────────────────────────────────────────────────────────
    *tick = [ctx, steps, idx, tick]() {
        if (*idx >= steps->size())
        {
            ctx->finish();
            return;
        }
        const Step &st = (*steps)[*idx];
        st.body(ctx);
        ++(*idx);
        if (st.delayAfter > 0)
            QTimer::singleShot(st.delayAfter, ctx->app, [tick]() { (*tick)(); });
        else
            (*tick)();
    };

    QTimer::singleShot(delayMs, app, [tick]() { (*tick)(); });
#endif  // STELQUICK_HAS_ENGINE
}

} // namespace stelapp

#endif  // STELQUICK_HAS_ENGINE（文件外层守卫：无引擎符号时整文件不编译）
