/*
 * ErrorModel — T42-B：**错误 / 运行状态 / 资源路径 / 未支持项**的只读数据面。
 *
 * ── 这一页到底回应哪几条文档要求（T42 的范围来路）────────────────────────
 *  ① A-1.0 范围表第二格 `|资源路径、错误提示、渲染诊断、配置保存 | 最小 | 必须|`
 *     —— T36 交付「配置保存」、T39 交付「渲染诊断」，**余下两项（资源路径 + 错误提示）
 *     由本页交付**。
 *  ② A5 行 `A-1.0 全部页面、配置迁移、夜视、**错误页**` —— 本页即「错误页」。
 *  ③ 开发指导 `:230`「**旧 UI 禁止作为隐式回退**…**未支持项有清单和明确提示**」
 *     + A-1.0 末行「…**不静默打开旧对话框**」—— 本页承载**未支持项清单**，
 *     并配合 `ActionRouter` 的宿主接管把 4 个老窗口动作改成**明确提示**。
 *
 * ── 与 T39 诊断页的分工（别把两者搞混）──────────────────────────────────
 *  · `DiagnosticPage` / `BackendInfo`（T39）= **实时性能面**：帧号、投递/丢弃计数、
 *    邮箱槽位、帧龄、字节数 —— 每帧都可能变的**连续量**，用 Timer 轮询。
 *  · 本页 = **静态健康面**：一次性读数（路径、配置目录、引导结果、未支持项）。
 *    点「重新检查」才重算 —— 没有轮询，没有每帧变化的量。
 *
 * ── 关键读数从哪来（**不复刻引擎逻辑**，全部走公开 API 或真实失败）────────
 *  · 资源路径：`StelFileMgr` 的目录/文件 API（T42-A 探针已实测每个的返回值形态）。
 *  · 引导结果：**由 main.cpp 注入**（`setBootResult`）—— `LiveSkyRuntime::boot()`
 *    的 `errorOut` 原文就是"可操作原因"，本类不重新判定、不重新措辞。
 *  · 图形后端：`BackendInfo`（T39）已有 `backendOk` / `runtimeApiName` / `probeError`，
 *    由 main.cpp 注入（`setBackend`）—— **判定只有一份**（T41 修 `backendOk` 的教训）。
 *  · 未支持项：**从注册表真源生成**（T42-A Q7 实测：`Windows` 组 10 个成员，
 *    其中 6 个已被 T41 接管、**4 个未接管**）；legacy 手势与 T39 的两个控件是
 *    **常量**（各自的任务文档已定案），不是拍脑袋写的清单。
 *
 * ── 测试口子（负控）─────────────────────────────────────────────────────
 *  · `STELQUICK_ERROR_PATHS_OFF=1`  ⇒ 资源路径表留空（判据应红）。
 *  · `STELQUICK_ERROR_UNSUPPORTED_OFF=1` ⇒ 未支持项清单留空（判据应红）。
 *  · `STELQUICK_ERROR_OPEN_OFF=1`   ⇒ `openPath` 不真的唤起系统程序
 *    （判据跑的时候不该弹出 Finder —— T41 交互腿的教训：环境副作用主动隔离）。
 */
#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>

namespace stelapp {

class ErrorModel : public QObject
{
    Q_OBJECT
    // ── 总状态 ────────────────────────────────────────────────────────────
    Q_PROPERTY(bool ready READ ready NOTIFY refreshed)
    Q_PROPERTY(bool hasError READ hasError NOTIFY refreshed)
    Q_PROPERTY(QString errorHeadline READ errorHeadline NOTIFY refreshed)
    Q_PROPERTY(QString errorDetail READ errorDetail NOTIFY refreshed)
    Q_PROPERTY(QString errorHint READ errorHint NOTIFY refreshed)
    // ── 状态分区（运行健康）───────────────────────────────────────────────
    Q_PROPERTY(QVariantList statusRows READ statusRows NOTIFY refreshed)
    Q_PROPERTY(int statusProblemCount READ statusProblemCount NOTIFY refreshed)
    // ── 资源路径（A-1.0「资源路径 | 最小 | 必须」）────────────────────────
    Q_PROPERTY(QVariantList pathRows READ pathRows NOTIFY refreshed)
    Q_PROPERTY(int pathProblemCount READ pathProblemCount NOTIFY refreshed)
    // ── 未支持项清单（指导文档「未支持项有清单和明确提示」）──────────────
    Q_PROPERTY(QVariantList unsupportedRows READ unsupportedRows NOTIFY refreshed)
    Q_PROPERTY(int unsupportedCount READ unsupportedCount NOTIFY refreshed)
    // ── 版本 / 路径（诊断文本与按钮用）──────────────────────────────────
    Q_PROPERTY(QString appName READ appName CONSTANT)
    Q_PROPERTY(QString fullVersion READ fullVersion CONSTANT)
    Q_PROPERTY(QString logPath READ logPath NOTIFY refreshed)
    Q_PROPERTY(QString configPath READ configPath NOTIFY refreshed)
    Q_PROPERTY(QString userDir READ userDir NOTIFY refreshed)

public:
    explicit ErrorModel(QObject *parent = nullptr);

    //! 引导结果注入（main.cpp 调；**失败原因原文照收**，不重新措辞）。
    void setBootResult(bool ok, const QString &errorText);
    //! 图形后端状态注入（T39 的 `BackendInfo` 是唯一判定处）。
    void setBackend(bool ok, const QString &apiName, const QString &probeError);
    //! 帧通路是否在投帧（可选注入；未启引擎时为 false）。
    void setFramePathAlive(bool alive);

    Q_INVOKABLE void refresh();
    //! 打开白名单内的路径（kind ∈ log / config / userDir / installDir / cacheDir）。
    //! 返回是否受理（未知 kind ⇒ false，**不接受任意路径/URL** —— T41 白名单先例）。
    Q_INVOKABLE bool openPath(const QString &kind);
    //! 生成诊断文本（状态 + 路径 + 版本）——给「复制诊断信息」按钮与判据用。
    Q_INVOKABLE QString buildDiagnostics() const;
    //! 诊断文本 → 系统剪贴板。返回是否成功。
    Q_INVOKABLE bool copyDiagnostics();

    // ── 只读访问器（QML 与判据共用）────────────────────────────────────────
    bool ready() const { return m_ready; }
    bool hasError() const { return !m_errorHeadline.isEmpty(); }
    QString errorHeadline() const { return m_errorHeadline; }
    QString errorDetail() const { return m_errorDetail; }
    QString errorHint() const { return m_errorHint; }
    QVariantList statusRows() const { return m_statusRows; }
    int statusProblemCount() const;
    QVariantList pathRows() const { return m_pathRows; }
    int pathProblemCount() const;
    QVariantList unsupportedRows() const { return m_unsupportedRows; }
    int unsupportedCount() const { return m_unsupportedRows.size(); }
    QString appName() const;
    QString fullVersion() const;
    QString logPath() const { return m_logPath; }
    QString configPath() const { return m_configPath; }
    QString userDir() const { return m_userDir; }

    // ── 负控开关（判据用；产品逻辑只读它们，不改语义）──────────────────────
    static bool pathsDisabled();
    static bool unsupportedDisabled();
    static bool openDisabled();

signals:
    void refreshed();

private:
    void rebuildStatus();
    void rebuildPaths();
    void rebuildUnsupported();
    void rebuildError();

    bool m_ready = false;
    bool m_bootOk = false;
    QString m_bootError;
    bool m_backendInjected = false;
    bool m_backendOk = false;
    QString m_backendName;
    QString m_backendError;
    bool m_frameAlive = false;

    QString m_errorHeadline;
    QString m_errorDetail;
    QString m_errorHint;

    QVariantList m_statusRows;
    QVariantList m_pathRows;
    QVariantList m_unsupportedRows;

    QString m_logPath;
    QString m_configPath;
    QString m_userDir;
};

} // namespace stelapp
