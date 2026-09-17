/*
 * ActionRouter — 动作 ID、可用/选中状态、快捷键与输入焦点路由（骨架，未实现）。
 *
 * 职责：统一命令路由；新 QML 界面的全部动作经此分发到引擎语义。
 * 禁止：同一次按键同时触发新旧 QAction（焦点在 QML 输入控件时不得触发天空快捷键）。
 *
 * 背景：StelActionMgr.cpp:66-70 在 action 创建时取 StelMainView 并注册 QAction，
 * 本类负责拆除"必须注册到 QWidget"的假设，先复用动作语义。
 * 实现顺序：A3。测试用例：U-ACT-01..03（软件测试文档 3.2）。
 */
#pragma once

#include <QObject>

namespace stelapp {

class ActionRouter : public QObject
{
    Q_OBJECT
public:
    explicit ActionRouter(QObject *parent = nullptr);

    // THREAD: gui
    // 注册动作：动作 ID、旧 StelAction 对应语义、默认快捷键。
    // void registerAction(const QString &actionId, /* StelAction * 或语义句柄 */);

    // THREAD: gui
    // 焦点守卫：QML 输入控件持有焦点时返回 false，按键不下发天空动作。
    // bool canDispatchToSky() const;

signals:
    // 动作可用性/选中状态变化，供 QML 工具栏绑定。
    // void actionEnabledChanged(const QString &actionId, bool enabled);
    // void actionCheckedChanged(const QString &actionId, bool checked);

private:
    // m_actions、m_focusGuard 等实现时补充。
};

} // namespace stelapp
