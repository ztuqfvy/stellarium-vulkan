/*
 * ConfigIsolation.cpp — 见 ConfigIsolation.hpp 的设计说明。
 *
 * 与 app/ 下其它文件同款：**引擎依赖全部用 `STELQUICK_HAS_ENGINE` 守卫**，
 * 独立工程形态（无引擎）编译成安全空实现 —— 不靠 CMake 排除文件。
 */

#include "ConfigIsolation.hpp"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>

#include <cstdio>
#include <exception>

#if defined(STELQUICK_HAS_ENGINE)
#include "StelFileMgr.hpp"
#endif

namespace stelapp
{

namespace
{

//! 引导记录。静态单例：`bootstrapPersonalConfigDir()` 幂等，只写一次。
ConfigIsolationReport g_report;
bool g_ran = false;

//! 播种时**刻意不搬**的文件（派生/瞬时产物；原目录里它们是原版自己的运行痕迹）。
//!   log.txt    —— 原版日志，留在原目录；个人版自己写自己的。
//!   output.txt —— 引擎 stdout 落盘残留（本机实测 0 字节）。
//!   config.old —— 原版的上一份配置备份，对个人版无意义。
bool isTransient(const QString &fileName)
{
    return fileName == QLatin1String("log.txt") || fileName == QLatin1String("output.txt") ||
           fileName == QLatin1String("config.old");
}

//! 首启播种：把原目录的用户数据搬到个人版目录（**已存在的不覆盖**）。
void seedFromOriginal(const QString &originalDir, const QString &personalDir,
                      ConfigIsolationReport *r)
{
    if (!QFileInfo(originalDir).isDir())
    {
        r->info = QStringLiteral("原始用户目录不存在，无可播种（个人版目录自行新建）");
        return;
    }

    QDirIterator it(originalDir, QDir::Files | QDir::NoSymLinks, QDirIterator::Subdirectories);
    while (it.hasNext())
    {
        const QString src = it.next();
        const QString rel = QDir(originalDir).relativeFilePath(src);
        if (isTransient(QFileInfo(src).fileName()))
            continue;

        const QString dst = personalDir + QLatin1Char('/') + rel;
        if (QFileInfo::exists(dst))
        {
            ++r->skippedExisting;
            continue;
        }
        if (!QDir().mkpath(QFileInfo(dst).absolutePath()))
        {
            r->note = QStringLiteral("播种时无法创建目录：") + QFileInfo(dst).absolutePath();
            return;
        }
        if (!QFile::copy(src, dst))
        {
            r->note = QStringLiteral("播种拷贝失败：") + rel;
            return;
        }
        ++r->migratedFiles;
        r->migratedBytes += QFileInfo(dst).size();
    }
}

void dumpReport(const ConfigIsolationReport &r)
{
    std::printf("CONFIGISO: original=%s\n", qPrintable(r.originalDir));
    std::printf("CONFIGISO: personal=%s\n", qPrintable(r.personalDir));
    std::printf("CONFIGISO: isolated=%d\n", r.isolated ? 1 : 0);
    std::printf("CONFIGISO: migration=%s files=%d bytes=%lld skippedExisting=%d defaultConfigSeeded=%d\n",
                r.migrationRan ? "ran" : "skipped", r.migratedFiles,
                static_cast<long long>(r.migratedBytes), r.skippedExisting,
                r.defaultConfigSeeded ? 1 : 0);
    std::printf("CONFIGISO: note=%s\n", r.note.isEmpty() ? "(none)" : qPrintable(r.note));
    std::printf("CONFIGISO: info=%s\n", r.info.isEmpty() ? "(none)" : qPrintable(r.info));
    std::fflush(stdout);
}

} // namespace

ConfigIsolationReport bootstrapPersonalConfigDir()
{
    if (g_ran)
        return g_report; // 幂等：第二次调用不再改目录、不再播种

#if !defined(STELQUICK_HAS_ENGINE)
    // 独立工程形态：没有引擎、没有用户目录概念。明确登记，不假装做过隔离。
    ConfigIsolationReport r;
    r.note = QStringLiteral("无引擎形态（STELQUICK_HAS_ENGINE 未定义），不做配置隔离");
    g_report = r;
    g_ran = true;
    return r;
#else
    ConfigIsolationReport r;
    r.originalDir = StelFileMgr::getUserDir();

    // ── 负控：关掉隔离（行为回到修复前：直接用原目录）─────────────────────────
    if (qEnvironmentVariableIsSet("STELQUICK_CFG_ISOLATE_OFF"))
    {
        r.personalDir = r.originalDir;
        r.info = QStringLiteral("负控 STELQUICK_CFG_ISOLATE_OFF：隔离关闭");
        g_report = r;
        g_ran = true;
        dumpReport(r);
        return r;
    }

    // ── 派生个人版目录：**引擎默认目录 + 后缀**（不复刻平台语义）─────────────
    //    用一级后缀而不是原目录的子目录：原目录**整体**不动（连 mtime 都不变），
    //    "原目录零改动"因此是结构性成立、可以按字节比对的。
    const QString personalDir = r.originalDir + QStringLiteral("-quick");

    try
    {
        StelFileMgr::setUserDir(personalDir);
    }
    catch (const std::exception &e)
    {
        // 不许静默降级：如实记 note（判据会因它报红），让"隔离没生效"看得见。
        r.personalDir = r.originalDir;
        r.note = QString::fromUtf8("setUserDir 失败，退回原目录：") + QString::fromUtf8(e.what());
        g_report = r;
        g_ran = true;
        dumpReport(r);
        return r;
    }
    r.personalDir = StelFileMgr::getUserDir();
    r.isolated = true;

    // ── 首次播种 ─────────────────────────────────────────────────────────────
    //    触发条件 = 个人版目录里还没有 config.ini（= 这台机器上第一次跑个人版）。
    //    负控 STELQUICK_CFG_MIGRATE_OFF 只关播种，不动隔离。
    const QString seedProbe = r.personalDir + QStringLiteral("/config.ini");
    if (qEnvironmentVariableIsSet("STELQUICK_CFG_MIGRATE_OFF"))
    {
        r.info = QStringLiteral("负控 STELQUICK_CFG_MIGRATE_OFF：跳过播种");
    }
    else if (!QFileInfo::exists(seedProbe))
    {
        r.migrationRan = true;
        seedFromOriginal(r.originalDir, r.personalDir, &r);
    }
    else
    {
        r.info = QStringLiteral("个人版目录已存在 config.ini，跳过播种（用户改过的值优先）");
    }

    // ── 兜底：个人版目录里**必须**有一份 config.ini ───────────────────────────
    //   没有旧配置可搬时也要能起来。上游 `src/main.cpp:398-403` 就是这么做的
    //   （`config.ini` 不存在 ⇒ `copyDefaultConfigFile()` 拷 `data/default_cfg.ini`）。
    //   **合流形态此前没有这一步**：空用户目录 ⇒ 引擎在引导中途 SIGSEGV（rc=139），
    //   崩溃点是 `LandscapeMgr: initialized Cache` 之后（探针见头注）。
    //   这条腿**不随** `STELQUICK_CFG_MIGRATE_OFF` 关闭 —— 播种是"搬旧东西"，
    //   兜底是"没有旧东西也得起来"，两件事。
    const QString personalConfig = r.personalDir + QStringLiteral("/config.ini");
    if (!QFileInfo::exists(personalConfig))
    {
        const QString defaultCfg = StelFileMgr::findFile(QStringLiteral("data/default_cfg.ini"));
        if (defaultCfg.isEmpty())
        {
            r.note = QStringLiteral("找不到 data/default_cfg.ini，无法兜底生成个人版 config.ini");
        }
        else if (QFile::copy(defaultCfg, personalConfig))
        {
            QFile::setPermissions(personalConfig,
                                  QFile::permissions(personalConfig) | QFileDevice::WriteOwner);
            r.defaultConfigSeeded = true;
        }
        else
        {
            r.note = QStringLiteral("兜底拷贝 data/default_cfg.ini 失败：") + personalConfig;
        }
    }

    g_report = r;
    g_ran = true;
    dumpReport(r);
    return r;
#endif
}

const ConfigIsolationReport &configIsolationReport()
{
    return g_report;
}

} // namespace stelapp
