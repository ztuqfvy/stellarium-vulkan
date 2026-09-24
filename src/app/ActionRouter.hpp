/*
 * ActionRouter — 动作 ID、可用/选中状态、快捷键与输入焦点路由。
 *
 * 职责：统一命令路由；新 QML 界面的全部动作经此分发到引擎语义。
 * 禁止：同一次按键同时触发新旧 QAction（焦点在 QML 输入控件时不得触发天空快捷键）。
 *
 * 背景：StelActionMgr.cpp 在 action 创建时取 StelMainView 并注册 QAction，
 * T15 拆除了"必须注册到 QWidget"的假设（StelAction::setWidgetShortcutDispatchEnabled(false)
 * ——合流形态宿主不再创建/挂接 QAction），本类成为唯一的命令入口：
 *
 *   ① 注册表路径：QML 按钮 → trigger(id) → AppFacade 命令（编译期装配）。
 *   ② 键盘路径：QML Keys / routeKey(key, modifiers) → 构建 QKeySequence →
 *      **复用 StelAction::matches 单点匹配**（T14 审计 §3.2：禁止另建键位表）
 *      → 首个 ExactMatch 触发一次。
 *
 * 测试用例：U-ACT-01..03（软件测试文档 3.2）：
 *   U-ACT-01 同一键不双触发 —— widget 分发已拆除 + 本类单点（AppFacadeCheck 断言）；
 *   U-ACT-02 禁用态不触发 —— Entry::enabled 门（AppFacadeCheck 断言）；
 *   U-ACT-03 焦点守卫 —— canDispatchToSky()（QML 侧 Keys 路径调用）。
 */
#pragma once

#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>
#include <functional>

namespace stelapp {

class ActionRouter : public QObject
{
    Q_OBJECT
public:
    using Handler = std::function<void()>;
    //! 可用性门（U-ACT-02）：返回 false 时 trigger 拒绝执行且不改任何状态。
    using Availability = std::function<bool()>;
    struct Entry
    {
        Handler run;
        Availability enabled;   //!< 可为空（视为恒可用）
        QString title;          //!< QML 工具栏可读名（可本地化后接）
    };

    explicit ActionRouter(QObject *parent = nullptr);

    //! 注册宿主命令（main.cpp 装配期调用；重复注册同 ID 视为装配错误，覆盖并告警）。
    void registerAction(const QString &actionId, const Entry &entry);

    //! QML 统一入口：先查注册表；未命中则透传引擎 StelAction（findAction）。
    //! 返回是否实际执行（禁用态/未命中返回 false，不产生任何状态变化）。
    Q_INVOKABLE bool trigger(const QString &actionId);

    //! 可用状态（注册表项用 enabled 门；引擎 StelAction 恒可用——引擎侧无禁用概念）。
    Q_INVOKABLE bool isAvailable(const QString &actionId) const;

    Q_INVOKABLE QStringList actionIds() const;
    Q_INVOKABLE QString actionTitle(const QString &actionId) const;

    //! 键盘路由（U-ACT-01/03 的入口）：
    //!  1. 焦点守卫：QML 可编辑控件持有焦点 → 直接拒收（不下发天空快捷键）；
    //!  2. 构建 QKeySequence(modifiers|key)，遍历引擎 StelAction 匹配，
    //!     首个 ExactMatch 触发一次并返回 true。
    //! 注册表命令无键位（按钮直达），键盘只走引擎动作路径。
    Q_INVOKABLE bool routeKey(int key, int modifiers);

    //! 焦点守卫本体：焦点对象是可编辑文本输入（QLineEdit/TextInput/TextArea）时返回 false。
    //! 独立出来便于 AppFacadeCheck 断言（U-ACT-03）。
    Q_INVOKABLE bool canDispatchToSky() const;

    //! 已执行命令计数（U-ACT-01 计数器断言的挂点；只增不减）。
    quint64 dispatchCount() const { return m_dispatchCount; }

signals:
    //! 每次请求的处置结果（执行与否），供 QML/自检观测。
    void dispatched(const QString &actionId, bool executed);
    //! 动作可用性变化，供 QML 工具栏绑定（T15 仅注册表项在门变化时补发；
    //! 引擎 StelAction 的可用性恒真，不订阅）。
    void actionEnabledChanged(const QString &actionId, bool enabled);

private:
    QHash<QString, Entry> m_entries;
    quint64 m_dispatchCount = 0;
};

} // namespace stelapp
