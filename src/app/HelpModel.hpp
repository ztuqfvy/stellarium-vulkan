/*
 * HelpModel — T41-B **帮助 / 版本 / 许可证**三件事的数据面。
 *
 * 出处：A-1.0 范围表倒数第二格
 *   `|快捷键编辑、帮助、版本与许可证页面 | 可延后 | 必须 | 中文输入焦点不能触发天空快捷键|`
 * —— 第一列的「快捷键编辑」由 T40 交付（`ShortcutModel` + `ShortcutsPage.qml`），
 * 本轮交付**第一列剩下的三项**。第三列那条约束已由 T29 兜住，不重复实现。
 *
 * ── 口径来自 T41-A 探针（app/HelpProbe.*，读数归档于
 *    docs/evidence/2026-09-30-t41-help/mac/probe-help-mac.txt）──────────────
 *  ① **版本面全现成**：`StelUtils::getApplicationName()`=「Stellarium 26.1+」、
 *     `getApplicationVersion()`=「26.1.273-4132aeb [main]」、`getApplicationPublicVersion()`、
 *     `getApplicationSeries()`；编译期宏 `PACKAGE_VERSION` / `STELLARIUM_BUIDING_VERSION`
 *     （**上游拼写如此，少一个 L**）/ `STELLARIUM_PUBLIC_VERSION` / `STELLARIUM_SERIES` /
 *     `STELLARIUM_COPYRIGHT` / `GIT_REVISION` / `GIT_BRANCH` 全部在 `stelQuickUI` 的
 *     编译单元里可见（`ADD_DEFINITIONS` 是全局的）。
 *     ⚠️ **`QCoreApplication::applicationVersion()` 是「1.0.0」**（没设成产品版本）
 *     ⇒ 版本页**绝不能用它**，必须走 `StelUtils::getApplicationVersion()`。
 *  ② **贡献者 246 条 ⇒ 排序去重后 245 条**（原表自带 1 条精确重复）；含非 ASCII 名 21 条
 *     ⇒ 展示层必须 UTF-8 安全（`QStringList` 天然是）。排序口径 = **大小写不敏感**
 *     （探针读数首条是 `adalava` 而非大写开头的名字 ⇒ 不是默认的码点排序）。
 *  ③ 🔴 **`COPYING` 不在 app bundle 里**（`Contents/` 只有 Info.plist/MacOS/translations；
 *     `Contents/Resources/` 这个目录**都不存在**）。探针里 `findFile("COPYING")` 之所以
 *     命中「./COPYING」，纯粹因为当时的 cwd 恰好是源码树根（`getInstallationDir()` 返回
 *     `"."`）⇒ **分发后必然失效**。⇒ GPL 全文必须**编进 qrc**（见 `loadLicense()`），
 *     这是唯一自包含的方案。
 *  ④ **翻译链路健康**：`Stellarium Help`→「Stellarium 帮助」、`Keys`→「快捷键」、
 *     `Version`→「版本」、`Based on Qt`→「基于Qt」、`Contributors`→「贡献者」、
 *     `Further Reading`→「了解更多」；但 **`Copyright` 保持英文是对的**（老代码明说
 *     版权串 not suitable for translation，那行是法律声明不是 UI 文案）。
 *  ⑤ **帮助页的键位表结构**（老 `HelpDialog.cpp::updateHelpText`）：`<tr>` 40 处、
 *     `hotkeyTextWrapper(` 14 处 ⇒ 可拆成两半：
 *       · **没有 action 的手势**（鼠标/滚轮组合）—— 只能硬编码，本类 `gestures()` 收它们；
 *       · **来自引擎注册表的动作**（505 条）—— **T40 的 `ShortcutModel` 已经覆盖且可编辑**
 *         ⇒ 本轮**刻意不复制**（复制 = 造一份会与真源漂移的影子表）。
 *     老版把两半都塞进一份只读 HTML；我们让第二半留在他自己的页上，并给一个跳转按钮。
 *  ⑥ **老宿主 8 个窗口动作全部活着**（`actionShow_*_Window_Global`，F1..F12 里占了 8 个），
 *     `StelApp::getGui()` 非空；`trigger()` 会真的弹出老 QWidget 对话框（探针 Q14：
 *     QWidget 总数 **12 → 58**）⇒ 接管机制在 `ActionRouter`（见该类头注），**不在本类**。
 *
 * ── 职责边界 ───────────────────────────────────────────────────────────────
 *   · 本类只做**只读数据面**（读版本宏 / 读静态表 / 读 qrc）+ 一个受白名单约束的外链口。
 *   · 键位编辑、冲突、落盘**一概不在这里**（那是 `ShortcutModel` 的域）。
 *   · QML 只做展示与布局；任何格式化/校验/读取回本类。
 */
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>

namespace stelapp {

class HelpModel : public QObject
{
    Q_OBJECT
public:
    explicit HelpModel(QObject *parent = nullptr);

    // ── 帮助页 ────────────────────────────────────────────────────────────
    //! 手势表：`[{group, action, keys, scope}]`。`scope` 是**诚实性字段**：
    //!   `"sky"`    = 天空视口内的手势（引擎侧在处理，本形态可用）；
    //!   `"legacy"` = **老式窗口内**的局部键（本形态**没有**那个窗口 ⇒ 标出来，
    //!               不假装可用；对应项记入 T42 的「未支持项」清单）。
    Q_PROPERTY(QVariantList gestures READ gestures NOTIFY contentChanged)
    Q_PROPERTY(int gestureCount READ gestureCount NOTIFY contentChanged)
    Q_PROPERTY(int legacyGestureCount READ legacyGestureCount NOTIFY contentChanged)
    //! 延伸阅读外链：`[{title, url, note}]`。同时是 `openExternal()` 的白名单。
    Q_PROPERTY(QVariantList webLinks READ webLinks NOTIFY contentChanged)

    // ── 关于页 ────────────────────────────────────────────────────────────
    //! 版本行 / 系统行：`[{label, value}]`。**引擎引导后才非空**（`refresh()` 负责）。
    Q_PROPERTY(QVariantList versionRows READ versionRows NOTIFY contentChanged)
    Q_PROPERTY(QVariantList systemRows READ systemRows NOTIFY contentChanged)
    //! 贡献者（已排序去重）。246 条原表 ⇒ 245 条。
    Q_PROPERTY(QStringList contributors READ contributors NOTIFY contentChanged)
    Q_PROPERTY(int contributorCount READ contributorCount NOTIFY contentChanged)
    Q_PROPERTY(QString appName READ appName NOTIFY contentChanged)
    //! 完整版本串（`StelUtils::getApplicationVersion()`）。**不是**
    //! `QCoreApplication::applicationVersion()`（那个是「1.0.0」—— 探针 Q4 实测）。
    Q_PROPERTY(QString fullVersion READ fullVersion NOTIFY contentChanged)
    Q_PROPERTY(QString copyrightText READ copyrightText NOTIFY contentChanged)
    //! 版本页信息是否已就绪（引擎引导后由 `refresh()` 置真）。
    Q_PROPERTY(bool ready READ ready NOTIFY contentChanged)

    // ── 许可证 ────────────────────────────────────────────────────────────
    Q_PROPERTY(QString licenseText READ licenseText NOTIFY licenseChanged)
    Q_PROPERTY(bool licenseLoaded READ licenseLoaded NOTIFY licenseChanged)
    Q_PROPERTY(QString licenseError READ licenseError NOTIFY licenseChanged)
    Q_PROPERTY(int licenseLineCount READ licenseLineCount NOTIFY licenseChanged)
    //! 资源路径（判据要拿它跟 CMake 侧登记的那条逐字比）。
    Q_PROPERTY(QString licenseResourcePath READ licenseResourcePath NOTIFY licenseChanged)

    QVariantList gestures() const { return m_gestures; }
    int gestureCount() const;
    int legacyGestureCount() const;
    QVariantList webLinks() const { return m_webLinks; }
    QVariantList versionRows() const { return m_versionRows; }
    QVariantList systemRows() const { return m_systemRows; }
    QStringList contributors() const { return m_contributors; }
    int contributorCount() const { return m_contributors.size(); }
    QString appName() const { return m_appName; }
    QString fullVersion() const { return m_fullVersion; }
    QString copyrightText() const { return m_copyright; }
    bool ready() const { return !m_versionRows.isEmpty(); }
    QString licenseText() const { return m_licenseText; }
    bool licenseLoaded() const { return !m_licenseText.isEmpty(); }
    QString licenseError() const { return m_licenseError; }
    int licenseLineCount() const { return m_licenseLineCount; }
    QString licenseResourcePath() const;

    // ── 命令 ───────────────────────────────────────────────────────────────
    //! 重建版本/系统行（引擎引导完成后调用；引导前调用只会得到空行）。
    Q_INVOKABLE void refresh();
    //! 打开外链。**白名单制**（只放行 `webLinks()` 里的 URL）—— 不给 QML 任意开 URL 的口。
    Q_INVOKABLE bool openExternal(const QString &url);
    Q_INVOKABLE bool isAllowedExternalUrl(const QString &url) const;
    //! 从 qrc 读 GPL 全文（构造时已调一次；可重入）。
    Q_INVOKABLE bool loadLicense();

    // ── 测试用（负控）────────────────────────────────────────────────────
    //! `STELQUICK_HELP_LICENSE_OFF=1` ⇒ 不读 GPL 全文（期望许可证判据红）。
    static bool licenseDisabled();

    //! GPL 全文的 qrc 路径（**单一真源**：CMake 的 qt_add_resources 必须与之一致）。
    static QString licenseResource();

signals:
    void contentChanged();
    void licenseChanged();

private:
    void buildGestures();
    void buildWebLinks();
    void buildContributors();

    QVariantList m_gestures;
    QVariantList m_webLinks;
    QVariantList m_versionRows;
    QVariantList m_systemRows;
    QStringList m_contributors;
    QString m_appName;
    QString m_fullVersion;
    QString m_copyright;
    QString m_licenseText;
    QString m_licenseError;
    int m_licenseLineCount = 0;
};

} // namespace stelapp
