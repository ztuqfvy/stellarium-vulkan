/*
 * HelpModel — 实现（T41-B）。口径见同名头文件与 T41-A 探针读数。
 */
#include "app/HelpModel.hpp"

#include "core/StelUtils.hpp"
#include "gui/ContributorsList.hpp"

#include <QDebug>
#include <QDesktopServices>
#include <QFile>
#include <QKeySequence>
#include <QSysInfo>
#include <QUrl>
#include <QVariantMap>

#include <algorithm>

namespace stelapp {

namespace {

//! GPL 全文的资源路径。**单一真源** —— CMake 侧 `qt_add_resources` 登记的
//! PREFIX/BASE 必须让文件落在正好这个路径上（`src/ui/CMakeLists.txt`）。
constexpr const char *kLicenseResource = ":/StelQuickUI/COPYING";

//! 平台修饰键的**原生显示名**（macOS 上是 ⌘，其它平台是 Ctrl）。
//! 刻意用 `QKeySequence::NativeText` 生成 —— 与老 `HelpDialog::hotkeyTextWrapper`
//! 同一口径，也和 T40「键名一律由 C++ 生成、QML 绝不自拼」同一条铁律。
QString nativeCtrl()
{
    return QKeySequence(Qt::CTRL).toString(QKeySequence::NativeText);
}

//! 组合显示的连接词。老代码在 macOS 上用 `q_("&")`（translation 注释：the char mean "and"），
//! 本形态 UI 是中文，直接用「和」——不假装走翻译链路。
QString conj()
{
    return QStringLiteral(" 和 ");
}

} // namespace

HelpModel::HelpModel(QObject *parent)
    : QObject(parent)
{
    buildGestures();
    buildWebLinks();
    buildContributors();
    // 版本/系统行留空 —— 它们依赖引擎（`StelUtils` 走 `StelFileMgr`），
    // 由 `refresh()` 在引导完成后填。QML 用 `ready` 显示空态。
    m_appName = QStringLiteral("Stellarium");
    m_copyright = QStringLiteral("Copyright (C) 2000-2026 Stellarium Developers");
    loadLicense();
}

// ─────────────────────────────────────────────────────────────────────────────
// 帮助页：手势表（老 HelpDialog 里"没有 action、只能硬编码"的那一半）
// ─────────────────────────────────────────────────────────────────────────────
void HelpModel::buildGestures()
{
    m_gestures.clear();
    const QString ctrl = nativeCtrl();
    // ⚠️ 变量名**不能叫 `and`** —— `and` / `or` / `not` 是 C++ 的**替代记号**
    //（alternative token，分别等于 `&&` / `||` / `!`）⇒ `const QString and = ...`
    // 是语法错误（`expected unqualified-id`，且报错行指向的是下一处，很绕）。
    const QString andWord = conj();

    auto add = [this](const QString &group, const QString &action, const QString &keys,
                      const QString &scope) {
        m_gestures << QVariantMap{ { QStringLiteral("group"), group },
                                   { QStringLiteral("action"), action },
                                   { QStringLiteral("keys"), keys },
                                   { QStringLiteral("scope"), scope } };
    };

    // ── 一、天空视口手势（engine 侧真的在处理）────────────────────────────
    const QString g_view = QStringLiteral("视图导航");
    add(g_view, QStringLiteral("环视天空"), QStringLiteral("方向键 或 左键拖拽"),
        QStringLiteral("sky"));
    add(g_view, QStringLiteral("缩放视场"),
        QStringLiteral("PgUp / PgDn"), QStringLiteral("sky"));
    add(g_view, QStringLiteral("缩放视场（微调）"),
        QStringLiteral("Ctrl+↑ / Ctrl+↓").replace(QStringLiteral("Ctrl"), ctrl),
        QStringLiteral("sky"));

    const QString g_time = QStringLiteral("时间操纵");
    add(g_time, QStringLiteral("拖动时间"),
        QStringLiteral("Ctrl + 左键拖拽").replace(QStringLiteral("Ctrl"), ctrl),
        QStringLiteral("sky"));
    add(g_time, QStringLiteral("时间滚动：分钟"),
        QStringLiteral("Ctrl + 滚轮").replace(QStringLiteral("Ctrl"), ctrl),
        QStringLiteral("sky"));
    add(g_time, QStringLiteral("时间滚动：小时"),
        QStringLiteral("Ctrl+Shift + 滚轮").replace(QStringLiteral("Ctrl"), ctrl),
        QStringLiteral("sky"));
    add(g_time, QStringLiteral("时间滚动：日"),
        QStringLiteral("Ctrl+Alt + 滚轮").replace(QStringLiteral("Ctrl"), ctrl),
        QStringLiteral("sky"));
    add(g_time, QStringLiteral("时间滚动：年"),
        QStringLiteral("Ctrl+Alt+Shift + 滚轮").replace(QStringLiteral("Ctrl"), ctrl),
        QStringLiteral("sky"));

    const QString g_pick = QStringLiteral("天体选择与标记");
    add(g_pick, QStringLiteral("选择天体"), QStringLiteral("左键单击"), QStringLiteral("sky"));
#if defined(Q_OS_MACOS)
    add(g_pick, QStringLiteral("清除选择"),
        ctrl + andWord + QStringLiteral("左键单击"), QStringLiteral("sky"));
#else
    add(g_pick, QStringLiteral("清除选择"), QStringLiteral("右键单击"), QStringLiteral("sky"));
#endif
    add(g_pick, QStringLiteral("添加自定义标记"),
        QStringLiteral("Shift + 左键单击"), QStringLiteral("sky"));
    add(g_pick, QStringLiteral("删除离光标最近的标记"),
        QStringLiteral("Shift + 右键单击"), QStringLiteral("sky"));
    add(g_pick, QStringLiteral("删除全部自定义标记"),
        QStringLiteral("Shift+Alt + 右键单击"), QStringLiteral("sky"));

    // ── 二、老式窗口内的局部键（本形态**没有**那些窗口 ⇒ 标 legacy）────────
    //    ⚠️ 这一组是**诚实性字段**：老 HelpDialog 把它们印在同一张表里（`<tr>` 里
    //       只写着"这些热键在特定窗口/标签页内局部可用"），但合流形态里
    //       脚本控制台窗口（F12）与天文计算窗口（F10）**都未接入 QML**
    //       ⇒ 标出来，并把这些项记入 T42 的「未支持项」清单。不假装可用。
    const QString g_script = QStringLiteral("脚本控制台（旧式窗口）");
    add(g_script, QStringLiteral("从文件加载脚本"),
        QStringLiteral("Ctrl+Shift+O").replace(QStringLiteral("Ctrl"), ctrl),
        QStringLiteral("legacy"));
    add(g_script, QStringLiteral("保存脚本到文件"),
        QStringLiteral("Ctrl+Shift+S").replace(QStringLiteral("Ctrl"), ctrl),
        QStringLiteral("legacy"));
    add(g_script, QStringLiteral("运行脚本"),
        QStringLiteral("Ctrl+Return").replace(QStringLiteral("Ctrl"), ctrl),
        QStringLiteral("legacy"));

    const QString g_calc = QStringLiteral("天文计算窗口（旧式窗口）");
    const QString shiftF10 = QStringLiteral("Shift+F10");
    for (const QString &label : { QStringLiteral("更新位置"), QStringLiteral("计算星历"),
                                  QStringLiteral("计算升中落（RTS）"),
                                  QStringLiteral("计算天象"), QStringLiteral("计算日食"),
                                  QStringLiteral("计算月食"), QStringLiteral("计算凌日") })
        add(g_calc, label, shiftF10, QStringLiteral("legacy"));
}

int HelpModel::gestureCount() const
{
    int n = 0;
    for (const QVariant &v : m_gestures)
        if (v.toMap().value(QStringLiteral("scope")).toString() == QLatin1String("sky"))
            ++n;
    return n;
}

int HelpModel::legacyGestureCount() const
{
    return m_gestures.size() - gestureCount();
}

// ─────────────────────────────────────────────────────────────────────────────
// 帮助页：延伸阅读（与老 HelpDialog 的 links 节一一对应；同时是外链白名单）
// ─────────────────────────────────────────────────────────────────────────────
void HelpModel::buildWebLinks()
{
    m_webLinks.clear();
    auto add = [this](const QString &title, const QString &url, const QString &note) {
        m_webLinks << QVariantMap{ { QStringLiteral("title"), title },
                                   { QStringLiteral("url"), url },
                                   { QStringLiteral("note"), note } };
    };
    add(QStringLiteral("常见问题（FAQ）"),
        QStringLiteral("https://github.com/Stellarium/stellarium/wiki/FAQ"),
        QStringLiteral("常见疑问与解答"));
    add(QStringLiteral("Stellarium Wiki"),
        QStringLiteral("https://github.com/Stellarium/stellarium/wiki"),
        QStringLiteral("综合资料"));
    add(QStringLiteral("星空地景"),
        QStringLiteral("https://stellarium.org/landscapes.html"),
        QStringLiteral("用户贡献的地景资源"));
    add(QStringLiteral("脚本库"),
        QStringLiteral("https://stellarium.org/scripts.html"),
        QStringLiteral("官方与用户贡献的脚本"));
    add(QStringLiteral("缺陷报告与功能请求"),
        QStringLiteral("https://github.com/Stellarium/stellarium/issues"),
        QStringLiteral("报告问题或提新需求"));
    add(QStringLiteral("讨论组（Google Groups）"),
        QStringLiteral("https://groups.google.com/forum/#!forum/stellarium"),
        QStringLiteral("与其他用户交流"));
    add(QStringLiteral("Open Collective"),
        QStringLiteral("https://opencollective.com/stellarium"),
        QStringLiteral("资助开发团队"));
}

// ─────────────────────────────────────────────────────────────────────────────
// 关于页：贡献者（口径照老 HelpDialog::updateAboutText）
// ─────────────────────────────────────────────────────────────────────────────
void HelpModel::buildContributors()
{
    m_contributors = StelContributors::contributorsList;
    // ⚠️ 顺序不能反：先精确去重（原表自带 1 条重复，246→245），再**大小写不敏感**排序。
    //    探针读数首条是 `adalava` 而不是任何大写开头的名字 ⇒ 老口径不是默认码点排序。
    m_contributors.removeDuplicates();
    std::sort(m_contributors.begin(), m_contributors.end(),
              [](const QString &a, const QString &b) {
                  return a.compare(b, Qt::CaseInsensitive) < 0;
              });
}

// ─────────────────────────────────────────────────────────────────────────────
// 关于页：版本 / 系统（引擎引导后才有货）
// ─────────────────────────────────────────────────────────────────────────────
void HelpModel::refresh()
{
    m_versionRows.clear();
    m_systemRows.clear();

    auto addVer = [this](const QString &label, const QString &value) {
        if (!value.isEmpty())
            m_versionRows << QVariantMap{ { QStringLiteral("label"), label },
                                          { QStringLiteral("value"), value } };
    };
    auto addSys = [this](const QString &label, const QString &value) {
        if (!value.isEmpty())
            m_systemRows << QVariantMap{ { QStringLiteral("label"), label },
                                         { QStringLiteral("value"), value } };
    };

    // ── 版本（⚠️ 一律走 StelUtils；`QCoreApplication::applicationVersion()` 是「1.0.0」）──
    m_appName = StelUtils::getApplicationName();
    m_fullVersion = StelUtils::getApplicationVersion();
    addVer(QStringLiteral("应用"), m_appName);
    addVer(QStringLiteral("完整版本"), m_fullVersion);
    addVer(QStringLiteral("公开发行版"), StelUtils::getApplicationPublicVersion());
    addVer(QStringLiteral("版本系列"), StelUtils::getApplicationSeries());
#ifdef GIT_REVISION
    addVer(QStringLiteral("Git 修订"), QStringLiteral(GIT_REVISION));
#endif
#ifdef GIT_BRANCH
    addVer(QStringLiteral("Git 分支"), QStringLiteral(GIT_BRANCH));
#endif
#ifdef STELLARIUM_BUIDING_VERSION
    addVer(QStringLiteral("构建版本"), QStringLiteral(STELLARIUM_BUIDING_VERSION));
#endif
#ifdef PACKAGE_VERSION
    addVer(QStringLiteral("包版本"), QStringLiteral(PACKAGE_VERSION));
#endif

    // ── 版权（**刻意不翻译** —— 老代码明说 not suitable for translation）──
#ifdef STELLARIUM_COPYRIGHT
    m_copyright = QStringLiteral(STELLARIUM_COPYRIGHT);
#endif
    addVer(QStringLiteral("版权"), m_copyright);

    // ── 系统 ──
    addSys(QStringLiteral("操作系统"), StelUtils::getOperatingSystemInfo());
    addSys(QStringLiteral("寻址模式"), StelUtils::getAddressingMode());
    addSys(QStringLiteral("CPU 架构"), QSysInfo::currentCpuArchitecture());
    addSys(QStringLiteral("内核版本"), QSysInfo::kernelVersion());
    addSys(QStringLiteral("Qt（编译期）"), QStringLiteral(QT_VERSION_STR));
    addSys(QStringLiteral("Qt（运行期）"), QString::fromLatin1(qVersion()));

    emit contentChanged();
}

// ─────────────────────────────────────────────────────────────────────────────
// 许可证
// ─────────────────────────────────────────────────────────────────────────────
QString HelpModel::licenseResource()
{
    return QString::fromLatin1(kLicenseResource);
}

QString HelpModel::licenseResourcePath() const
{
    return licenseResource();
}

bool HelpModel::licenseDisabled()
{
    return qEnvironmentVariableIsSet("STELQUICK_HELP_LICENSE_OFF");
}

bool HelpModel::loadLicense()
{
    m_licenseText.clear();
    m_licenseError.clear();
    m_licenseLineCount = 0;

    if (licenseDisabled())
    {
        m_licenseError = QStringLiteral("（负控）STELQUICK_HELP_LICENSE_OFF=1 —— 未装入 GPL 全文");
        emit licenseChanged();
        return false;
    }

    QFile f(licenseResource());
    if (!f.open(QIODevice::ReadOnly))
    {
        m_licenseError = QStringLiteral("无法打开 %1：%2")
                             .arg(licenseResource(), f.errorString());
        qWarning() << "HelpModel:" << m_licenseError;
        emit licenseChanged();
        return false;
    }
    m_licenseText = QString::fromUtf8(f.readAll());
    if (m_licenseText.isEmpty())
    {
        m_licenseError = QStringLiteral("%1 是空的").arg(licenseResource());
        emit licenseChanged();
        return false;
    }
    m_licenseLineCount = m_licenseText.count(QLatin1Char('\n')) + 1;
    emit licenseChanged();
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// 外链（白名单制）
// ─────────────────────────────────────────────────────────────────────────────
bool HelpModel::isAllowedExternalUrl(const QString &url) const
{
    for (const QVariant &v : m_webLinks)
        if (v.toMap().value(QStringLiteral("url")).toString() == url)
            return true;
    return false;
}

bool HelpModel::openExternal(const QString &url)
{
    // 白名单 = 本模型自己那张表。**不给 QML 任意开 URL 的口**（QML 里的 url 可能来自
    // 用户输入或未来的动态数据；一旦放行就成了"点哪开哪"的万能跳板）。
    if (!isAllowedExternalUrl(url))
    {
        qWarning() << "HelpModel: 拒绝打开非白名单链接：" << url;
        return false;
    }
    return QDesktopServices::openUrl(QUrl(url));
}

} // namespace stelapp
