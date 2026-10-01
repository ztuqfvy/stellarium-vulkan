/*
 * ErrorModel — 实现（T42-B）。口径、范围来路与测试口子见同名头文件。
 */
#include "app/ErrorModel.hpp"

#include "core/StelActionMgr.hpp"
#include "core/StelApp.hpp"
#include "core/StelFileMgr.hpp"
#include "core/StelUtils.hpp"
#include "StelLogger.hpp"

#include <QClipboard>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QKeySequence>
#include <QSettings>
#include <QUrl>
#include <QVariantMap>

namespace stelapp {

namespace {

//! 资源路径一行的状态词（QML 侧据此上色；判据据此计数）。
QString stateOfDir(const QString &path)
{
    if (path.isEmpty())
        return QStringLiteral("unset");   //!< 引擎未提供（如惰性创建的目录）
    const QFileInfo fi(path);
    if (!fi.exists() || !fi.isDir())
        return QStringLiteral("missing");
    return fi.isWritable() ? QStringLiteral("ok") : QStringLiteral("readonly");
}

QString stateOfFile(const QString &path)
{
    if (path.isEmpty())
        return QStringLiteral("unset");
    const QFileInfo fi(path);
    if (!fi.exists() || !fi.isFile())
        return QStringLiteral("missing");
    return fi.isReadable() ? QStringLiteral("ok") : QStringLiteral("readonly");
}

QVariantMap row(const QString &label, const QString &path, const QString &state,
                const QString &note)
{
    QVariantMap m;
    m.insert(QStringLiteral("label"), label);
    m.insert(QStringLiteral("path"), path);
    m.insert(QStringLiteral("state"), state);
    m.insert(QStringLiteral("note"), note);
    return m;
}

QVariantMap statusRow(const QString &label, const QString &value, const QString &state)
{
    QVariantMap m;
    m.insert(QStringLiteral("label"), label);
    m.insert(QStringLiteral("value"), value);
    m.insert(QStringLiteral("state"), state);
    return m;
}

bool isProblemState(const QString &s)
{
    return s == QLatin1String("missing") || s == QLatin1String("readonly");
}

//! 「未支持项」里的老窗口动作 —— **白名单式**（只列这 4 个 + 常量项），
//! 不从注册表"自动发现全部未接管动作"（那会把 15 个组里大量正常动作也卷进来）。
//! 每一项的**显示名与键位都从注册表真源取**（T40 铁律：键名一律 NativeText），
//! 避免清单与真源漂移。
struct UnsupportedWindow
{
    const char *id;
    const char *reason;
};

const UnsupportedWindow kUnsupportedWindows[] = {
    {"actionShow_Configuration_Window_Global",
     "A-1.0『不做』：全部插件设置（个人版用显示页/本页替代常规设置）"},
    {"actionShow_AstroCalc_Window_Global",
     "A-1.0『不做』：高级天文计算"},
    {"actionShow_ScriptConsole_Window_Global",
     "A-1.0『不做』：脚本控制台"},
    {"actionShow_ObsList_Window_Global",
     "个人版未纳入：观测列表（老 QWidget 对话框）"},
};

} // namespace

ErrorModel::ErrorModel(QObject *parent) : QObject(parent) {}

bool ErrorModel::pathsDisabled()
{
    return qEnvironmentVariableIsSet("STELQUICK_ERROR_PATHS_OFF");
}

bool ErrorModel::unsupportedDisabled()
{
    return qEnvironmentVariableIsSet("STELQUICK_ERROR_UNSUPPORTED_OFF");
}

bool ErrorModel::openDisabled()
{
    return qEnvironmentVariableIsSet("STELQUICK_ERROR_OPEN_OFF");
}

void ErrorModel::setBootResult(bool ok, const QString &errorText)
{
    m_bootOk = ok;
    m_bootError = ok ? QString() : errorText;
}

void ErrorModel::setBackend(bool ok, const QString &apiName, const QString &probeError)
{
    m_backendInjected = true;
    m_backendOk = ok;
    m_backendName = apiName;
    m_backendError = probeError;
}

void ErrorModel::setFramePathAlive(bool alive) { m_frameAlive = alive; }

void ErrorModel::setConfigHealth(int severity, const QString &healthText)
{
    m_cfgInjected = true;
    m_cfgSeverity = severity;
    m_cfgHealthText = healthText;
}

QString ErrorModel::appName() const { return StelUtils::getApplicationName(); }
QString ErrorModel::fullVersion() const { return StelUtils::getApplicationVersion(); }

// ─────────────────────────────────────────────────────────────────────────────
// 资源路径面（A-1.0「资源路径 | 最小 | 必须」）
// ─────────────────────────────────────────────────────────────────────────────
void ErrorModel::rebuildPaths()
{
    m_pathRows.clear();
    if (pathsDisabled())
    {
        // 负控：路径表留空（判据应红）。产品正常路径不会走到这里。
        m_userDir.clear();
        m_logPath.clear();
        m_configPath.clear();
        return;
    }

    m_userDir = StelFileMgr::getUserDir();
    m_logPath = StelLogger::getLogFileName();
    QSettings *conf = StelApp::isInitialized() ? StelApp::getInstance().getSettings() : nullptr;
    m_configPath = conf ? conf->fileName() : QString();

    const auto add = [this](const QString &label, const QString &path, const QString &state,
                            const QString &note) {
        m_pathRows.append(row(label, path, state, note));
    };

    add(QStringLiteral("用户目录（个人版）"), m_userDir, stateOfDir(m_userDir),
        m_userDir.endsWith(QStringLiteral("-quick"))
            ? QStringLiteral("T36 隔离生效：与原版目录**同级**，不写穿原程序设置")
            : QStringLiteral("⚠️ 未见 `-quick` 后缀（隔离可能被关闭）"));

    const QString installDir = StelFileMgr::getInstallationDir();
    add(QStringLiteral("程序资源目录"), installDir, stateOfDir(installDir),
        installDir == QLatin1String(".")
            ? QStringLiteral("相对路径（源码树形态）；分发形态下为应用包内目录")
            : QString());

    const QString cacheDir = StelFileMgr::getCacheDir();
    add(QStringLiteral("缓存目录"), cacheDir, stateOfDir(cacheDir), QString());

    const QString shotDir = StelFileMgr::getScreenshotDir();
    add(QStringLiteral("截图目录"), shotDir, stateOfDir(shotDir),
        shotDir.isEmpty() ? QStringLiteral("引擎未设置（首次截图时惰性创建）") : QString());

    const QString obsDir = StelFileMgr::getObsListDir();
    add(QStringLiteral("观测列表目录"), obsDir, stateOfDir(obsDir),
        obsDir.isEmpty() ? QStringLiteral("引擎未设置（本形态未纳入观测列表）") : QString());

    const QString localeDir = StelFileMgr::getLocaleDir();
    add(QStringLiteral("语言资源目录"), localeDir, stateOfDir(localeDir), QString());

    add(QStringLiteral("配置文件"), m_configPath, stateOfFile(m_configPath),
        m_configPath.isEmpty() ? QStringLiteral("引擎未引导") : QString());

    add(QStringLiteral("日志文件"), m_logPath, stateOfFile(m_logPath),
        m_logPath.isEmpty() ? QStringLiteral("引擎未引导") : QString());
}

int ErrorModel::pathProblemCount() const
{
    int n = 0;
    for (const QVariant &v : m_pathRows)
        if (isProblemState(v.toMap().value(QStringLiteral("state")).toString()))
            ++n;
    return n;
}

// ─────────────────────────────────────────────────────────────────────────────
// 运行状态面（"错误页"的主干）
// ─────────────────────────────────────────────────────────────────────────────
void ErrorModel::rebuildStatus()
{
    m_statusRows.clear();

    const bool engineUp = StelApp::isInitialized();
    if (m_bootOk && engineUp)
        m_statusRows.append(statusRow(QStringLiteral("引擎引导"), QStringLiteral("已完成"),
                                      QStringLiteral("ok")));
    else if (!m_bootError.isEmpty())
        m_statusRows.append(statusRow(QStringLiteral("引擎引导"), m_bootError,
                                      QStringLiteral("error")));
    else
        m_statusRows.append(statusRow(QStringLiteral("引擎引导"),
                                      QStringLiteral("未启动（本形态默认不引导引擎）"),
                                      QStringLiteral("warn")));

    if (!m_backendInjected)
        m_statusRows.append(statusRow(QStringLiteral("图形后端"), QStringLiteral("未探测"),
                                      QStringLiteral("info")));
    else if (m_backendOk)
        m_statusRows.append(statusRow(QStringLiteral("图形后端"),
                                      m_backendName.isEmpty() ? QStringLiteral("就绪")
                                                              : m_backendName,
                                      QStringLiteral("ok")));
    else
        m_statusRows.append(statusRow(QStringLiteral("图形后端"),
                                      m_backendError.isEmpty() ? QStringLiteral("不可用")
                                                               : m_backendError,
                                      QStringLiteral("error")));

    if (m_userDir.isEmpty())
        m_statusRows.append(statusRow(QStringLiteral("配置目录"), QStringLiteral("未知"),
                                      QStringLiteral("error")));
    else
    {
        const QString st = stateOfDir(m_userDir);
        m_statusRows.append(statusRow(
            QStringLiteral("配置目录"),
            (st == QLatin1String("ok")
                 ? QStringLiteral("可写（个人版隔离）")
                 : (st == QLatin1String("readonly") ? QStringLiteral("只读") : QStringLiteral("不可用"))),
            st == QLatin1String("ok") ? QStringLiteral("ok") : QStringLiteral("error")));
    }

    // ── T45-B/B2：「配置文件」行（P-CFG-01 的"明确提示"出口）─────────────────
    //    真源 = 引导期 `ConfigIsolation` 的配置健康判定（**严重度**，只渲染）。
    //    损坏**不致命**（回退默认照常跑）⇒ 不进 errorHeadline，只标状态行。
    if (!m_cfgInjected)
        m_statusRows.append(statusRow(QStringLiteral("配置文件"), QStringLiteral("未探测"),
                                      QStringLiteral("info")));
    else
    {
        const char *st = m_cfgSeverity <= 0 ? "info"
                         : m_cfgSeverity == 1 ? "ok"
                         : m_cfgSeverity == 2 ? "warn"
                                              : "error";
        m_statusRows.append(statusRow(
            QStringLiteral("配置文件"),
            m_cfgHealthText.isEmpty()
                ? (m_cfgSeverity == 1 ? QStringLiteral("正常")
                                      : QStringLiteral("未探测（severity=%1）").arg(m_cfgSeverity))
                : m_cfgHealthText,
            QString::fromLatin1(st)));
    }

    m_statusRows.append(statusRow(QStringLiteral("天空帧通路"),
                                  m_frameAlive ? QStringLiteral("在投帧")
                                               : QStringLiteral("无帧（未启引擎/帧泵）"),
                                  m_frameAlive ? QStringLiteral("ok") : QStringLiteral("warn")));
}

int ErrorModel::statusProblemCount() const
{
    int n = 0;
    for (const QVariant &v : m_statusRows)
        if (v.toMap().value(QStringLiteral("state")).toString() == QLatin1String("error"))
            ++n;
    return n;
}

// ─────────────────────────────────────────────────────────────────────────────
// 未支持项清单（指导文档「未支持项有清单和明确提示」+ A-1.0「不静默打开旧对话框」）
// ─────────────────────────────────────────────────────────────────────────────
void ErrorModel::rebuildUnsupported()
{
    m_unsupportedRows.clear();
    if (unsupportedDisabled())
        return;   // 负控：清单留空（判据应红）

    StelActionMgr *mgr = StelApp::isInitialized() ? StelApp::getInstance().getStelActionManager()
                                                  : nullptr;

    // ① 老窗口动作（4 个未接管的）—— 显示名/键位**从注册表取真源**
    for (const UnsupportedWindow &w : kUnsupportedWindows)
    {
        const QString id = QString::fromLatin1(w.id);
        QString name = id;
        QString keys;
        bool inRegistry = false;
        if (mgr)
        {
            if (StelAction *a = mgr->findAction(id))
            {
                inRegistry = true;
                name = a->getText();
                keys = a->getShortcut().toString(QKeySequence::NativeText);
            }
        }
        QVariantMap m;
        m.insert(QStringLiteral("feature"), name);
        m.insert(QStringLiteral("keys"), keys);
        m.insert(QStringLiteral("actionId"), id);
        QString detail = QString::fromUtf8(w.reason);
        if (!inRegistry)
            detail += QStringLiteral("｜⚠️ 注册表里未找到该动作（清单与真源不符，需复核）");
        m.insert(QStringLiteral("detail"), detail);
        m_unsupportedRows.append(m);
    }

    // ② 插件设置（A-1.0「不做」的第三项）—— **动作数从注册表真源数出来**，不是拍脑袋
    if (mgr)
    {
        const QStringList pluginGroups{QStringLiteral("Oculars"), QStringLiteral("Satellites"),
                                       QStringLiteral("Solar System Editor"),
                                       QStringLiteral("Meteor Showers"),
                                       QStringLiteral("Bright Novae"),
                                       QStringLiteral("Exoplanets")};
        int pluginActions = 0;
        QStringList names;
        for (const QString &g : pluginGroups)
        {
            const int n = mgr->getActionList(g).size();
            pluginActions += n;
            if (n > 0)
                names << QStringLiteral("%1(%2)").arg(g).arg(n);
        }
        QVariantMap m;
        m.insert(QStringLiteral("feature"), QStringLiteral("全部插件设置"));
        m.insert(QStringLiteral("keys"), QString());
        m.insert(QStringLiteral("actionId"), QString());
        m.insert(QStringLiteral("detail"),
                 QStringLiteral("A-1.0『不做』：插件配置界面未纳入个人版。"
                                "注册表里插件动作 %1 个：%2")
                     .arg(pluginActions)
                     .arg(names.join(QStringLiteral("、"))));
        m_unsupportedRows.append(m);
    }

    // ③ 帮助页上的 legacy 手势（T41 已定案：10 条，脚本控制台 3 + 天文计算 7）
    {
        QVariantMap m;
        m.insert(QStringLiteral("feature"), QStringLiteral("快捷键手势（legacy 部分）"));
        m.insert(QStringLiteral("keys"), QString());
        m.insert(QStringLiteral("actionId"), QString());
        m.insert(QStringLiteral("detail"),
                 QStringLiteral("帮助页标红的 10 条手势属老窗口局部键（脚本控制台 3 + 天文计算 7）"
                                "—— 本形态不在范围内，帮助页已逐条标注 scope=legacy"));
        m_unsupportedRows.append(m);
    }

    // ④ T39 定案的"搬过来也对 QML 无效"的两个控件
    {
        QVariantMap m;
        m.insert(QStringLiteral("feature"), QStringLiteral("界面字体大小 / 屏幕按钮缩放"));
        m.insert(QStringLiteral("keys"), QString());
        m.insert(QStringLiteral("actionId"), QString());
        m.insert(QStringLiteral("detail"),
                 QStringLiteral("T39 探针实测：这两个设置只作用于老 QWidget 对话框，"
                                "对 QML 视觉树零影响 ⇒ 未提供控件（避免造永远为假的开关）"));
        m_unsupportedRows.append(m);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// 错误详情（有错才填；无错时整块不显示）
// ─────────────────────────────────────────────────────────────────────────────
void ErrorModel::rebuildError()
{
    m_errorHeadline.clear();
    m_errorDetail.clear();
    m_errorHint.clear();

    m_ready = true;

    if (!m_bootError.isEmpty())
    {
        m_errorHeadline = QStringLiteral("引擎引导失败 —— 天空与时间等功能不可用");
        m_errorDetail = m_bootError;
        m_errorHint = QStringLiteral(
            "常见原因：用户目录不可创建/不可写、配置目录被占用、图形驱动初始化失败。"
            "可先查看下面的「资源路径」与日志文件；若用户目录指向了不存在的位置，"
            "请检查启动环境变量 STEL_USERDIR。");
        return;
    }

    if (m_backendInjected && !m_backendOk)
    {
        m_errorHeadline = QStringLiteral("图形后端不可用");
        m_errorDetail = m_backendError.isEmpty()
                            ? QStringLiteral("未探测到可用的图形后端。")
                            : m_backendError;
        m_errorHint = QStringLiteral("界面仍可浏览；天空渲染需要可用的图形后端（Metal/Vulkan）。");
        return;
    }

    if (!m_userDir.isEmpty() && stateOfDir(m_userDir) != QLatin1String("ok"))
    {
        m_errorHeadline = QStringLiteral("配置目录不可写 —— 设置与快捷键可能无法保存");
        m_errorDetail = m_userDir;
        m_errorHint = QStringLiteral("请确认该目录存在且当前用户可写。");
        return;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
void ErrorModel::refresh()
{
    rebuildPaths();
    rebuildStatus();
    rebuildUnsupported();
    rebuildError();
    emit refreshed();
}

bool ErrorModel::openPath(const QString &kind)
{
    // 白名单制（T41 `HelpModel::openExternal` 先例）：只认这几种 kind，
    // **不接受任意路径或 URL** —— 这个入口只服务"打开日志/配置/目录"三个用途。
    QString target;
    QString toOpen;
    if (kind == QLatin1String("log"))
    {
        target = m_logPath;
        toOpen = target.isEmpty() ? QString() : QFileInfo(target).absolutePath();
    }
    else if (kind == QLatin1String("config"))
    {
        target = m_configPath;
        toOpen = target.isEmpty() ? QString() : QFileInfo(target).absolutePath();
    }
    else if (kind == QLatin1String("userDir"))
        toOpen = m_userDir;
    else if (kind == QLatin1String("installDir"))
        toOpen = StelFileMgr::getInstallationDir();
    else if (kind == QLatin1String("cacheDir"))
        toOpen = StelFileMgr::getCacheDir();
    else
        return false;   // 未知 kind ⇒ 拒收

    if (toOpen.isEmpty() || !QFileInfo::exists(toOpen))
        return false;

    if (openDisabled())
        return true;   // 判据模式：受理但不真的唤起系统程序（不弹 Finder）

    return QDesktopServices::openUrl(QUrl::fromLocalFile(toOpen));
}

QString ErrorModel::buildDiagnostics() const
{
    QStringList lines;
    lines << QStringLiteral("%1 %2").arg(appName(), fullVersion());
    lines << QStringLiteral("—— 运行状态 ——");
    for (const QVariant &v : m_statusRows)
    {
        const QVariantMap m = v.toMap();
        lines << QStringLiteral("[%1] %2：%3")
                     .arg(m.value(QStringLiteral("state")).toString(),
                          m.value(QStringLiteral("label")).toString(),
                          m.value(QStringLiteral("value")).toString());
    }
    lines << QStringLiteral("—— 资源路径 ——");
    for (const QVariant &v : m_pathRows)
    {
        const QVariantMap m = v.toMap();
        lines << QStringLiteral("[%1] %2 = %3")
                     .arg(m.value(QStringLiteral("state")).toString(),
                          m.value(QStringLiteral("label")).toString(),
                          m.value(QStringLiteral("path")).toString());
    }
    if (hasError())
    {
        lines << QStringLiteral("—— 错误 ——");
        lines << m_errorHeadline;
        lines << m_errorDetail;
        lines << m_errorHint;
    }
    lines << QStringLiteral("—— 未支持项（%1）——").arg(m_unsupportedRows.size());
    for (const QVariant &v : m_unsupportedRows)
    {
        const QVariantMap m = v.toMap();
        lines << QStringLiteral("%1%2：%3")
                     .arg(m.value(QStringLiteral("feature")).toString(),
                          m.value(QStringLiteral("keys")).toString().isEmpty()
                              ? QString()
                              : QStringLiteral("（%1）").arg(m.value(QStringLiteral("keys")).toString()),
                          m.value(QStringLiteral("detail")).toString());
    }
    return lines.join(QLatin1Char('\n'));
}

bool ErrorModel::copyDiagnostics()
{
    QClipboard *cb = QGuiApplication::clipboard();
    if (!cb)
        return false;
    cb->setText(buildDiagnostics());
    return true;
}

} // namespace stelapp
