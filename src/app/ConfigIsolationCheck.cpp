/*
 * ConfigIsolationCheck.cpp — 实现（T36-C）。判据清单/负控口径/退出码见头注。
 */

#include "app/ConfigIsolationCheck.hpp"

#include "app/ActionRouter.hpp"
#include "app/ConfigIsolation.hpp"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QTimer>

#include <cstdio>

#if defined(STELQUICK_HAS_ENGINE)
#include "StelApp.hpp"
#include "StelFileMgr.hpp"
#include "StelIniParser.hpp"
#endif

namespace stelapp
{

namespace
{

//! 匿名命名空间在 `stelapp` 里 ⇒ 嵌套类型要显式限定才可见。
using Result = ConfigIsolationCheck::Result;

//! 与 ConfigIsolation.cpp 的 isTransient 同一份口径（播种不搬的东西，也不参与"清单完整"比对）。
bool isTransient(const QString &fileName)
{
    return fileName == QLatin1String("log.txt") || fileName == QLatin1String("output.txt") ||
           fileName == QLatin1String("config.old");
}

QString readAllText(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return QString();
    return QString::fromUtf8(f.readAll());
}

QString md5OfFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return QStringLiteral("<unreadable>");
    return QString::fromLatin1(QCryptographicHash::hash(f.readAll(), QCryptographicHash::Md5)
                                   .toHex());
}

//! 从 stel ini 原文里取某个键的值（**原文**解析：判据看的是磁盘字节，不是任何对象状态）。
//! stel ini 的写法是 `key<spaces>= value`，逐行匹配 `^\s*key\s*=\s*(.*?)\s*$`。
QString iniValueFromText(const QString &text, const QString &key)
{
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (const QString &line : lines)
    {
        const int eq = line.indexOf(QLatin1Char('='));
        if (eq < 0)
            continue;
        if (line.trimmed().startsWith(QLatin1Char(';')) || line.trimmed().startsWith(QLatin1Char('[')))
            continue;
        if (line.left(eq).trimmed() == key)
            return line.mid(eq + 1).trimmed();
    }
    return QString();
}

//! 路径规范化（存在则取 canonical，避免符号链接导致的假不等）。
QString norm(const QString &p)
{
    const QFileInfo fi(p);
    const QString c = fi.canonicalFilePath();
    return c.isEmpty() ? QDir::cleanPath(fi.absoluteFilePath()) : c;
}

//! @p child 是否位于 @p parent 之下（含自身）。规范化之后按路径段比较，避免
//! `/a/bc` 被 `/a/b` 前缀命中的经典误判。
bool isUnder(const QString &child, const QString &parent)
{
    const QString c = norm(child);
    const QString p = norm(parent);
    if (c == p)
        return true;
    return c.startsWith(p.endsWith(QLatin1Char('/')) ? p : p + QLatin1Char('/'));
}

struct Ctx
{
    QCoreApplication *app = nullptr;
    ActionRouter *router = nullptr;
    std::function<void(const Result &)> onDone;
    Result result;
    bool ok = true;

    QString originalDir;
    QString personalDir;
    QString personalConfig;
    QString originalConfig;

    void mark(bool cond, const QString &line)
    {
        ++result.total;
        if (cond)
        {
            ++result.passed;
            result.details.append(QStringLiteral("  \u2713 %1").arg(line));
        }
        else
        {
            ok = false;
            result.details.append(QStringLiteral("  \u2717 %1").arg(line));
        }
    }
    void note(const QString &line) { result.details.append(QStringLiteral("    %1").arg(line)); }

    void finish()
    {
        result.ran = true;
        result.pass = (result.passed == result.total) && result.total > 0;
        result.summary = QStringLiteral("\u5224\u636e %1/%2  VERDICT=%3")
                             .arg(result.passed)
                             .arg(result.total)
                             .arg(result.pass ? QStringLiteral("PASS") : QStringLiteral("FAIL"));
        onDone(result);
    }

    void unavailable(const QString &why)
    {
        result.ran = true;
        result.unavailable = true;
        result.summary = QStringLiteral("UNAVAILABLE\uff08%1\uff09").arg(why);
        onDone(result);
    }
};

//! CFG-01..CFG-05、CFG-08：纯读侧。
void runReadSide(Ctx *c, const ConfigIsolationReport &iso)
{
    // ── CFG-01 隔离读数自洽 ──────────────────────────────────────────────────
    c->mark(iso.isolated && iso.note.isEmpty() &&
                norm(StelFileMgr::getUserDir()) == norm(iso.personalDir) &&
                norm(iso.personalDir) != norm(iso.originalDir),
            QStringLiteral("CFG-01 \u9694\u79bb\u8bfb\u6570\u81ea\u6d3d\uff1aisolated=%1 note=[%2] "
                           "\u72ec\u7acb\u91cd\u8bfb userDir=%3 == personal=%4\u3001\u2260 original=%5")
                .arg(iso.isolated ? 1 : 0)
                .arg(iso.note)
                .arg(norm(StelFileMgr::getUserDir()))
                .arg(norm(iso.personalDir))
                .arg(norm(iso.originalDir)));

    // ── CFG-02 目录独立（存在 / 是目录 / 可写 / 不是原目录的子树）────────────
    const QFileInfo pd(c->personalDir);
    const QString probe = c->personalDir + QStringLiteral("/.cfgiso-write-probe");
    QFile pf(probe);
    const bool writable = pf.open(QIODevice::WriteOnly | QIODevice::Truncate);
    if (writable)
    {
        pf.write("probe");
        pf.close();
        pf.remove();
    }
    const bool distinct = norm(c->personalDir) != norm(c->originalDir);
    // ★ 结构性防线：个人版目录**不许**是原目录的子路径。用子目录方案的话，
    //   创建/写入子目录会改原目录自身的 mtime ⇒ "原目录零改动"按字节比对就不成立了。
    const bool notChild = !isUnder(c->personalDir, c->originalDir);
    c->mark(pd.isDir() && writable && distinct && notChild,
            QStringLiteral("CFG-02 \u4e2a\u4eba\u7248\u76ee\u5f55\u72ec\u7acb\uff1aisDir=%1 writable=%2 "
                           "\u4e0e\u539f\u76ee\u5f55\u540c\u8def\u5f84=%3 \u662f\u539f\u76ee\u5f55\u5b50\u8def\u5f84=%4")
                .arg(pd.isDir() ? 1 : 0)
                .arg(writable ? 1 : 0)
                .arg(distinct ? 0 : 1)
                .arg(notChild ? 0 : 1));
    c->note(QStringLiteral("\u8bfb\u6570\uff1apersonalDir=%1\uff1boriginalDir=%2")
                .arg(c->personalDir)
                .arg(c->originalDir));

    // ── CFG-03 配置写侧落点（QSettings::fileName —— 引擎此刻真会写的文件）──
    //    ★ 条件里带上"个人版目录 ≠ 原目录"：否则隔离关掉时这条会**自洽假绿**
    //      （落点当然还是"那个目录"，因为两个目录是同一个）。
    QSettings *s = StelApp::getInstance().getSettings();
    const QString settingsFile = s ? s->fileName() : QString();
    const bool writeSideOk = !settingsFile.isEmpty() && isUnder(settingsFile, c->personalDir) &&
                             norm(c->personalDir) != norm(c->originalDir);
    c->mark(writeSideOk,
            QStringLiteral("CFG-03 \u914d\u7f6e\u5199\u4fa7\u843d\u70b9\uff1aQSettings::fileName()=%1 "
                           "\u843d\u5728 %2 \u4e4b\u4e0b=%3\u3001\u76ee\u5f55\u4e0e\u539f\u76ee\u5f55\u4e0d\u91cd\u5408=%4")
                .arg(settingsFile)
                .arg(c->personalDir)
                .arg(isUnder(settingsFile, c->personalDir) ? 1 : 0)
                .arg(norm(c->personalDir) != norm(c->originalDir) ? 1 : 0));

    // ── CFG-04 日志落点 ─────────────────────────────────────────────────────
    const QString plog = c->personalDir + QStringLiteral("/log.txt");
    const QString olog = c->originalDir + QStringLiteral("/log.txt");
    const QFileInfo pfi(plog);
    const QFileInfo ofi(olog);
    const bool logOk = pfi.exists() && pfi.size() > 0 &&
                       (!ofi.exists() || pfi.lastModified() > ofi.lastModified());
    c->mark(logOk,
            QStringLiteral("CFG-04 \u65e5\u5fd7\u843d\u70b9\uff1apersonal/log.txt \u5b58\u5728=%1 \u5b57\u8282=%2 "
                           "\u6bd4 original/log.txt \u65b0=%3")
                .arg(pfi.exists() ? 1 : 0)
                .arg(pfi.size())
                .arg((!ofi.exists() || pfi.lastModified() > ofi.lastModified()) ? 1 : 0));

    // ── CFG-05 配置种子完整（键集 ⊇；独立解析）──────────────────────────────
    //  两种场景，两种要求（**都不能省**）：
    //    · 原目录有 config.ini ⇒ 个人版必须**继承齐**它的每个键（丢键 = 用户设置丢了）；
    //    · 原目录**没有** config.ini（全新机器）⇒ 只要求个人版自己有一份非空的
    //      （内容由 `data/default_cfg.ini` 兜底，见 ConfigIsolation.hpp 第二条腿）。
    int missingKeys = 0;
    QString firstMissing;
    const QFileInfo pcInfo(c->personalConfig);
    const bool origHasCfg = QFileInfo::exists(c->originalConfig);
    if (pcInfo.exists() && pcInfo.size() > 0 && origHasCfg)
    {
        QSettings orig(c->originalConfig, StelIniFormat);
        QSettings mine(c->personalConfig, StelIniFormat);
        const QStringList origKeys = orig.allKeys();
        const QStringList myKeys = mine.allKeys();
        for (const QString &k : origKeys)
        {
            if (!myKeys.contains(k))
            {
                ++missingKeys;
                if (firstMissing.isEmpty())
                    firstMissing = k;
            }
        }
        c->mark(missingKeys == 0,
                QStringLiteral("CFG-05 \u914d\u7f6e\u79cd\u5b50\u5b8c\u6574\uff08\u539f\u76ee\u5f55\u6709\u914d\u7f6e\uff09\uff1a"
                               "\u539f\u76ee\u5f55\u952e\u6570=%1 \u4e2a\u4eba\u7248\u7f3a\u5931=%2\uff08\u9996\u4e2a=%3\uff09"
                               "\u4e2a\u4eba\u7248\u5b57\u8282=%4")
                    .arg(origKeys.size())
                    .arg(missingKeys)
                    .arg(firstMissing.isEmpty() ? QStringLiteral("-") : firstMissing)
                    .arg(pcInfo.size()));
    }
    else
    {
        c->mark(pcInfo.exists() && pcInfo.size() > 0,
                QStringLiteral("CFG-05 \u914d\u7f6e\u79cd\u5b50\u5b8c\u6574\uff08\u539f\u76ee\u5f55\u65e0\u914d\u7f6e\uff09\uff1a"
                               "\u4e2a\u7248 config.ini \u5b58\u5728=%1 \u5b57\u8282=%2\uff08\u9700\u975e\u7a7a\uff09")
                    .arg(pcInfo.exists() ? 1 : 0)
                    .arg(pcInfo.size()));
    }

    // ── CFG-08 播种清单完整（原目录除瞬态外的每个文件都在个人版目录里）──────
    //  ⚠️ 门写成 `isDir(originalDir) && missingFiles == 0` 而不是 `expectedFiles > 0`：
    //    后者会让**全新机器**（原目录空目录）恒红；前者能抓到真正要抓的东西
    //    ——"原目录根本没被找对"（路径写错/不存在 ⇒ 立刻红），同时允许"真的没东西可搬"。
    int missingFiles = 0;
    int expectedFiles = 0;
    QString firstMissingFile;
    const bool origDirOk = QFileInfo(c->originalDir).isDir();
    if (origDirOk)
    {
        QDirIterator it(c->originalDir, QDir::Files | QDir::NoSymLinks,
                        QDirIterator::Subdirectories);
        while (it.hasNext())
        {
            const QString src = it.next();
            if (isTransient(QFileInfo(src).fileName()))
                continue;
            ++expectedFiles;
            const QString rel = QDir(c->originalDir).relativeFilePath(src);
            if (!QFileInfo::exists(c->personalDir + QLatin1Char('/') + rel))
            {
                ++missingFiles;
                if (firstMissingFile.isEmpty())
                    firstMissingFile = rel;
            }
        }
    }
    c->mark(origDirOk && missingFiles == 0,
            QStringLiteral("CFG-08 \u64ad\u79cd\u6e05\u5355\u5b8c\u6574\uff1a\u539f\u76ee\u5f55\u662f\u76ee\u5f55=%1 "
                           "\u975e\u77ac\u6001\u6587\u4ef6=%2 \u4e2a\u4eba\u7248\u7f3a\u5931=%3\uff08\u9996\u4e2a=%4\uff09")
                .arg(origDirOk ? 1 : 0)
                .arg(expectedFiles)
                .arg(missingFiles)
                .arg(firstMissingFile.isEmpty() ? QStringLiteral("-") : firstMissingFile));
}

//! CFG-06：哨兵成对。
void runSentinel(Ctx *c)
{
    const QString value = QStringLiteral("cfgiso-") + QString::number(QCoreApplication::applicationPid()) +
                          QStringLiteral("-") +
                          QString::number(QDateTime::currentMSecsSinceEpoch());
    QSettings *s = StelApp::getInstance().getSettings();
    if (!s)
    {
        c->mark(false, QStringLiteral("CFG-06 \u5199\u4fa7\u4e0d\u89e6\u539f\u76ee\u5f55\uff1a\u62ff\u4e0d\u5230 QSettings"));
        return;
    }
    // ★ 空机器上的**非平凡化**：原目录**原本没有** config.ini 时，"原目录里不含哨兵"
    //   天然为真（文件都读不到）⇒ 判据退化成平凡真。补一条硬要求：跑完它也不许
    //   **冒出来**（写穿会创建它）。这样无论机器上有没有 config.ini，这条都承重。
    const bool origCfgExistedBefore = QFileInfo::exists(c->originalConfig);

    s->setValue(QStringLiteral("stellarium_quick/cfgiso_sentinel"), value);
    s->sync();

    const QString personalText = readAllText(c->personalConfig);
    const QString originalText = readAllText(c->originalConfig);
    const bool inPersonal = personalText.contains(value);
    const bool inOriginal = originalText.contains(value);
    const bool origNotCreated = origCfgExistedBefore || !QFileInfo::exists(c->originalConfig);

    s->remove(QStringLiteral("stellarium_quick/cfgiso_sentinel"));
    s->sync();

    c->mark(inPersonal && !inOriginal && origNotCreated,
            QStringLiteral("CFG-06 \u5199\u4fa7\u4e0d\u89e6\u539f\u76ee\u5f55\uff1a\u54e8\u5175\u5728\u4e2a\u4eba\u7248=%1 "
                           "\u5728\u539f\u76ee\u5f55=%2\uff08\u671f\u671b 1/0\uff09\u539f\u672c\u65e0\u914d\u7f6e\u65f6\u4e5f\u672a\u88ab\u521b\u5efa=%3"
                           "\uff08\u54e8\u5175\u5df2\u56de\u6536\uff09")
                .arg(inPersonal ? 1 : 0)
                .arg(inOriginal ? 1 : 0)
                .arg(origNotCreated ? 1 : 0));
}

//! CFG-07：产品路径（ActionRouter \u2192 \u5f15\u64ce \u2192 immediateSave）成对。
void runProductPath(Ctx *c)
{
    static const QString kAction = QStringLiteral("actionShow_Constellation_Lines");
    static const QString kKey = QStringLiteral("flag_constellation_drawing");

    if (!c->router)
    {
        c->mark(false, QStringLiteral("CFG-07 \u4ea7\u54c1\u8def\u5f84\u843d\u70b9\uff1a\u65e0 ActionRouter"));
        return;
    }

    QSettings *s = StelApp::getInstance().getSettings();
    const QString before = iniValueFromText(readAllText(c->personalConfig), kKey);
    // ★ 同 CFG-06 的非平凡化：原目录原本无 config.ini 时，md5 比对是"两个
    //   <unreadable> 相等"的退化真 ⇒ 补"跑完仍不存在"这一条。
    const bool origCfgExistedBefore = QFileInfo::exists(c->originalConfig);
    const QString origMd5Before = md5OfFile(c->originalConfig);

    // \u9690\u542b\u524d\u63d0\u81ea\u5df1\u8bfb + \u5f3a\u5236 + \u6536\u5c3e\u8fd8\u539f\uff08\u9677\u9631 44\uff09
    const bool immediateWas = StelApp::getInstance().getFlagImmediateSave();
    StelApp::getInstance().setFlagImmediateSave(true);

    const bool fired = c->router->trigger(kAction);
    s->sync();
    const QString after = iniValueFromText(readAllText(c->personalConfig), kKey);
    const QString origMd5AfterFlip = md5OfFile(c->originalConfig);

    // \u6536\u5c3e\u8fd8\u539f\uff1a\u518d trigger \u4e00\u6b21\u56de\u5230\u539f\u503c
    c->router->trigger(kAction);
    s->sync();
    const QString restored = iniValueFromText(readAllText(c->personalConfig), kKey);
    StelApp::getInstance().setFlagImmediateSave(immediateWas);

    const bool personalChanged = (after != before) && !after.isEmpty();
    const bool originalUntouched =
        (origMd5AfterFlip == origMd5Before) &&
        (origCfgExistedBefore || !QFileInfo::exists(c->originalConfig));
    const bool restoredOk = (restored == before);

    c->mark(personalChanged && originalUntouched,
            QStringLiteral("CFG-07 \u4ea7\u54c1\u8def\u5f84\u843d\u70b9\uff1atrigger=%1 \u4e2a\u4eba\u7248 %2: [%3]->[%4] "
                           "\u539f\u76ee\u5f55\u672a\u88ab\u6539\u52a8=%5")
                .arg(fired ? 1 : 0)
                .arg(kKey)
                .arg(before.isEmpty() ? QStringLiteral("(\u65e0)") : before)
                .arg(after.isEmpty() ? QStringLiteral("(\u65e0)") : after)
                .arg(originalUntouched ? 1 : 0));
    c->note(QStringLiteral("\u524d\u63d0\uff1a\u539f\u76ee\u5f55 config.ini \u539f\u672c\u5b58\u5728=%1"
                           "\uff08\u4e0d\u5b58\u5728\u65f6\u6539\u5224\u201c\u8dd1\u5b8c\u4ecd\u4e0d\u5b58\u5728\u201d\uff0c"
                           "\u907f\u514d\u4e24\u4e2a <unreadable> \u76f8\u7b49\u7684\u9000\u5316\u771f\uff09")
                .arg(origCfgExistedBefore ? 1 : 0));
    c->note(QStringLiteral("\u6536\u5c3e\u8fd8\u539f\uff1a%1=[%2]\uff08\u5199\u524d=[%3]\uff09\uff1b"
                           "flagImmediateSave \u5df2\u8fd8\u539f\u4e3a %4")
                .arg(kKey)
                .arg(restored.isEmpty() ? QStringLiteral("(\u65e0)") : restored)
                .arg(before.isEmpty() ? QStringLiteral("(\u65e0)") : before)
                .arg(immediateWas ? 1 : 0));
    if (!restoredOk)
        c->note(QStringLiteral("\u26a0\ufe0f \u8fd8\u539f\u672a\u56de\u5230\u5199\u524d\u503c\uff01"));
}

} // namespace

void ConfigIsolationCheck::run(QCoreApplication *app,
                               ActionRouter *router,
                               const std::function<void(const Result &)> &onDone,
                               int delayMs)
{
    auto *c = new Ctx();
    c->app = app;
    c->router = router;
    c->onDone = onDone;

    // ⚠️ Ctx **不是** QObject ⇒ singleShot 的 context 只能给 `app`（Ctx 由 lambda 捕获）。
    QTimer::singleShot(delayMs, app, [c]() {
        const ConfigIsolationReport iso = configIsolationReport();
        c->originalDir = iso.originalDir;
        c->personalDir = iso.personalDir;
        c->personalConfig = c->personalDir + QStringLiteral("/config.ini");
        c->originalConfig = c->originalDir + QStringLiteral("/config.ini");

        // \u524d\u63d0\u95e8\uff1a\u6570\u636e\u9762\u7f3a\u5931\u5219\u4e00\u6761\u5224\u636e\u90fd\u4e0d\u8ba1\u3002
        if (!StelApp::isInitialized())
        {
            c->unavailable(QStringLiteral("\u5f15\u64ce\u672a\u5f15\u5bfc"));
            delete c;
            return;
        }
        if (!StelApp::getInstance().getSettings())
        {
            c->unavailable(QStringLiteral("\u62ff\u4e0d\u5230\u5f15\u64ce QSettings"));
            delete c;
            return;
        }
        if (c->originalDir.isEmpty() || c->personalDir.isEmpty())
        {
            c->unavailable(QStringLiteral("\u7528\u6237\u76ee\u5f55\u8bfb\u6570\u4e3a\u7a7a"));
            delete c;
            return;
        }

        c->note(QStringLiteral("\u5f15\u5bfc\u8bb0\u5f55\uff1aisolated=%1 migration=%2 files=%3 bytes=%4 "
                               "skippedExisting=%5 defaultConfigSeeded=%6 note=[%7] info=[%8]")
                    .arg(iso.isolated ? 1 : 0)
                    .arg(iso.migrationRan ? QStringLiteral("ran") : QStringLiteral("skipped"))
                    .arg(iso.migratedFiles)
                    .arg(iso.migratedBytes)
                    .arg(iso.skippedExisting)
                    .arg(iso.defaultConfigSeeded ? 1 : 0)
                    .arg(iso.note)
                    .arg(iso.info));

        runReadSide(c, iso);
        runSentinel(c);
        runProductPath(c);
        c->finish();
        delete c;
    });
}

} // namespace stelapp
