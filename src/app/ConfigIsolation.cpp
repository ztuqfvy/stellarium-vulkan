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
#include <QSettings>

#include <cstdio>
#include <exception>

#if defined(STELQUICK_HAS_ENGINE)
#include "StelFileMgr.hpp"
#include "StelIniParser.hpp"
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
    //! T45-B/B2：配置健康读数（判据 CFGHEALTHCHECK 的原始量对照面）。
    std::printf("CONFIGISO: configHealth severity=%d status=%d bytes=%lld keys=%d "
                "repaired=%d backup=%s text=%s\n",
                r.configSeverity, r.configStatus, static_cast<long long>(r.configFileBytes),
                r.configParsedKeys, r.configRepaired ? 1 : 0,
                r.configCorruptBackup.isEmpty() ? "(none)"
                                                : qPrintable(r.configCorruptBackup),
                r.configHealthText.isEmpty() ? "(none)" : qPrintable(r.configHealthText));
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

    // ── T45-B/B2：引导期读一次配置健康（P-CFG-01 的"明确提示"）────────────────
    //    探针 Q4b 证实的缺口：损坏配置 Qt 全静默 ⇒ 产品此前零提示出口。
    //    唯一真相源在这读一次；ErrorModel 只渲染严重度（陷阱 86：判定只有一份）。
    //
    //    ⚠️ 为什么不能只看 `QSettings::status()`（T45-A Q2/Q3 实测）：
    //      五种损坏形态（截断/二进制垃圾/坏行/非法 UTF-8/空文件）**全部 NoError**；
    //      而只读文件上单纯的 `sync()` 也不报错（Q3 只在 setValue+**写**路径上
    //      才给 AccessError）⇒ 光看 status 等于**恒说正常**。
    //      所以这里补三条**产品侧可确定判定**的规则（全部基于确定性事实，
    //      不含任何启发式阈值以外的猜测）：
    //        ① 文件不可写（`QFileInfo::isWritable()` —— 探针 Q3b 已证
    //           `QSettings::isWritable()` 是**静态**方法，答的是全局状态，不能用）
    //        ② 有内容却解析出 **0 个键**（二进制垃圾的确定特征）
    //        ③ 键数 **不足随包默认的一半**（截断/坏行/非法 UTF-8 的共同特征：
    //           Qt 静默跳过坏行，键数塌陷）
    //      ⚠️ ③ 是**阈值式**判定（唯一一条非绝对判定）：随包默认
    //      `data/default_cfg.ini` 是产品自己的基线，个人版配置由它播种/由引擎
    //      整体重写 ⇒ 键数**只会增长**，塌到一半以下只可能是文件被破坏。
    //      取"一半"而非"绝对值"是为了不把这条判定的成立绑死在某个版本上。
    if (QFileInfo::exists(personalConfig))
    {
        const QFileInfo cfgInfo(personalConfig);
        r.configFileBytes = cfgInfo.size();

        // 🔴🔴 格式必须用引擎同款 `StelIniFormat`，**不许**用 plain `QSettings::IniFormat`：
        //    2026-10-01 实测（T45 收口批次）抓到真缺陷：同一个 config.ini，plain
        //    IniFormat 解析出 771 键、引擎的 StelIniFormat 只解析出 688 键 ——
        //    两个解析器对同一文件**分歧 83 键**（健康读说"正常（771 键）"，
        //    同一次运行里 CFG-05 用 StelIniFormat 判"缺 80"）。健康读的职责是
        //    "替引擎预判它能不能起来"，所以判定基准必须与引擎**同一个解析器**；
        //    用 plain 解析轻则误报健康，重则**误触发修复**（把用户好端端的配置
        //    备份改名重建）。cfgiso 判据（CFG-05）一直用的就是 StelIniFormat，
        //    本处此前用了 plain —— 已改。
        QSettings cfg(personalConfig, StelIniFormat);
        // 🔴 顺序要紧：`status()` 是**惰性定型**的 —— 构造/`sync()` 之后它可能**还是
        //    NoError**，真正的解析错误要等第一次真键访问（`allKeys()`）才落到 status 上。
        //    2026-10-01 实测踩到：先读 status 再读 allKeys ⇒ 二进制垃圾形态报
        //    `status=0`（NoError）却走了 FormatError 分支，两个读数**自相矛盾**。
        //    ⇒ 一律"**先触发解析，再读 status**"，且**只读一次**（后面用 r.configStatus
        //    比对，不再重复调用 —— 重复调用等于在同一处再判一次）。
        r.configParsedKeys = cfg.allKeys().size();
        cfg.sync();
        r.configStatus = int(cfg.status());

        // 基线键数（随包默认）。取不到就不做阈值判定（缺前提不许退化成平凡真）。
        int defaultKeys = -1;
        const QString defaultCfg = StelFileMgr::findFile(QStringLiteral("data/default_cfg.ini"));
        if (!defaultCfg.isEmpty())
        {
            QSettings dc(defaultCfg, StelIniFormat);
            dc.sync();
            defaultKeys = dc.allKeys().size();
        }

        // 负控开关（只用于证明 CFGHEALTHCHECK 的判据承重；正题恒不设）
        //  `STELQUICK_CFG_STATUS_OFF` 关掉**健康报告**（可观测性）；
        //  `STELQUICK_CFG_REPAIR_OFF` 关掉**引导修复**（正确性）。
        //  ⚠️ 两者分开是有意的：修复是"不修就起不来"，报告是"用户看不看得见"。
        //     把它们绑在一个开关上，负控就会互相串扰（关报告顺手把修复也关了 ⇒
        //     引擎崩 ⇒ 连判据都跑不出来，什么也证明不了）。
        const bool healthRead = !qEnvironmentVariableIsSet("STELQUICK_CFG_STATUS_OFF");
        const bool repairOn = !qEnvironmentVariableIsSet("STELQUICK_CFG_REPAIR_OFF");

        // ── 分层：修复 = 正确性（无条件做）；报告 = 可观测性（可被负控关掉）──────
        //    🔴 2026-10-01 实测（本机）：
        //      · 0 键配置（二进制垃圾 / 0 字节）⇒ 引擎 **SIGSEGV rc=139**；
        //      · **99 键**的截断配置（随包默认 252 键）⇒ **同样 SIGSEGV**；
        //      · 崩点恒为 `LandscapeMgr: initialized Cache for 100 MB.` 之后
        //        —— 与头注里"空用户目录"那条**同一个崩点**。
        //    ⇒ "损坏"不只是显示问题：**键不全就起不来**，P-CFG-01 的"启动可恢复"
        //      在修复之前**根本不成立**。上游 `src/main.cpp:398-403` 的
        //      `copyDefaultConfigFile()` 只覆盖"文件不存在"，本处把它扩到
        //      "**存在但不完整**"。
        //    判据取"键数 < 随包默认"而不是某个拍的绝对值：`data/default_cfg.ini`
        //      是产品自己的基线，个人版配置**由它播种、由引擎整体重写** ⇒ 键数只会
        //      增长；比基线还少只可能是文件被破坏（截断/垃圾/手改）。
        const bool writable = cfgInfo.isWritable();
        const bool incomplete = defaultKeys > 0 ? (r.configParsedKeys < defaultKeys)
                                                : (r.configParsedKeys == 0);

        if (incomplete && writable && repairOn)
        {
            // 备份**先于**覆盖（用户原始字节一个都不能丢）；备份名取第一个空闲位。
            const QString base = personalConfig + QStringLiteral(".corrupt");
            QString bak = base;
            for (int k = 1; k < 10 && QFileInfo::exists(bak); ++k)
                bak = base + QStringLiteral(".%1").arg(k);
            if (QFile::rename(personalConfig, bak))
            {
                r.configCorruptBackup = bak;
                const QString fallbackCfg =
                    StelFileMgr::findFile(QStringLiteral("data/default_cfg.ini"));
                if (!fallbackCfg.isEmpty() && QFile::copy(fallbackCfg, personalConfig))
                {
                    QFile::setPermissions(personalConfig,
                                          QFile::permissions(personalConfig) |
                                              QFileDevice::WriteOwner);
                    r.configRepaired = true;
                }
                else if (r.note.isEmpty())
                {
                    r.note = QStringLiteral("引导修复：找到了不完整的 config.ini 但重建失败（"
                                            "找不到/拷不动 data/default_cfg.ini）");
                }
            }
            else if (r.note.isEmpty())
            {
                r.note = QStringLiteral("引导修复：不完整的 config.ini 备份失败：") + personalConfig;
            }
        }

        if (!healthRead)
        {
            r.configStatus = -1;
            r.configFileBytes = -1;
            r.configParsedKeys = -1;
            r.configSeverity = 0;
            r.configHealthText =
                QStringLiteral("负控 STELQUICK_CFG_STATUS_OFF：未读配置健康"
                               "（但引导修复腿照常工作 —— 修复是正确性，不是可观测性）");
        }
        else if (!writable)
        {
            r.configSeverity = 3;
            r.configHealthText =
                incomplete
                    ? QStringLiteral("配置文件不可写（本次改动无法保存）**且内容不完整**"
                                     "（%1 键 / 随包默认 %2 键）—— 无法自动修复，"
                                     "请检查文件权限后删除该文件让产品重新生成")
                          .arg(r.configParsedKeys)
                          .arg(defaultKeys)
                    : QStringLiteral("配置文件不可写（本次改动无法保存）—— 检查文件权限");
        }
        else if (incomplete)
        {
            // 归因**如实**：修成了说修成了，没修成说为什么。⚠️ 文案里的键数/字节数都是
            // **修复之前**的读数 —— 健康行描述的是"发现时是什么样"（判据 CH-05 依赖这点）。
            r.configSeverity = 3;
            r.configHealthText =
                r.configRepaired
                    ? QStringLiteral("配置文件不完整（%1 键，随包默认 %2 键；%3 字节）"
                                     "—— 已按可读部分无法启动，**已备份为 %4 并重建为随包默认配置**")
                          .arg(r.configParsedKeys)
                          .arg(defaultKeys)
                          .arg(r.configFileBytes)
                          .arg(QFileInfo(r.configCorruptBackup).fileName())
                    : QStringLiteral("配置文件不完整（%1 键，随包默认 %2 键）"
                                     "—— 修复未生效（%3），请从「状态与错误」页打开配置目录")
                          .arg(r.configParsedKeys)
                          .arg(defaultKeys)
                          .arg(r.note.isEmpty() ? QStringLiteral("原因未记录") : r.note);
        }
        else if (r.configStatus == int(QSettings::AccessError))
        {
            r.configSeverity = 3;
            r.configHealthText =
                QStringLiteral("配置文件访问被拒（改动无法保存）—— 检查文件权限");
        }
        else
        {
            r.configSeverity = 1;
            r.configHealthText = QStringLiteral("正常（%1 键）").arg(r.configParsedKeys);
        }
    }
    else
    {
        r.configSeverity = 2;
        r.configHealthText = QStringLiteral("不存在（首启将播种默认配置）");
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
