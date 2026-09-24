/*
 * ActionRouter — 实现（T15）。
 *
 * 引擎交互（StelAction 匹配/透传）用 STELQUICK_HAS_ENGINE 守卫：
 * 独立工程形态下只有注册表路径可用，routeKey 恒 false。
 */
#include "app/ActionRouter.hpp"

#include <QCoreApplication>
#include <QDebug>
#include <QGuiApplication>
#include <QKeySequence>

#if defined(STELQUICK_HAS_ENGINE)
#include "StelActionMgr.hpp"
#include "StelApp.hpp"
#endif

#ifdef QT_WIDGETS_LIB
#include <QApplication>
#include <QLineEdit>
#include <QTextEdit>
#include <QWidget>
#endif

namespace stelapp {

ActionRouter::ActionRouter(QObject *parent)
    : QObject(parent)
{
}

void ActionRouter::registerAction(const QString &actionId, const Entry &entry)
{
    if (m_entries.contains(actionId))
        qWarning() << "ActionRouter: 动作重复注册（覆盖）：" << actionId;
    m_entries.insert(actionId, entry);
}

bool ActionRouter::trigger(const QString &actionId)
{
    // ① 注册表路径（宿主命令：AppFacade 等）
    const auto it = m_entries.constFind(actionId);
    if (it != m_entries.constEnd())
    {
        if (it->enabled && !it->enabled())
        {
            // U-ACT-02：禁用态不触发、不改状态、不算执行
            qDebug() << "ActionRouter: 拒绝（禁用态）：" << actionId;
            emit dispatched(actionId, false);
            return false;
        }
        it->run();
        ++m_dispatchCount;
        emit dispatched(actionId, true);
        return true;
    }

    // ② 引擎 StelAction 透传（ID 直呼）
#if defined(STELQUICK_HAS_ENGINE)
    if (StelApp::isInitialized())
    {
        if (StelAction *action = StelApp::getInstance().getStelActionManager()->findAction(actionId))
        {
            action->trigger();   // checkable 语义（toggle/setChecked）由 StelAction 自管
            ++m_dispatchCount;
            emit dispatched(actionId, true);
            return true;
        }
    }
#endif

    qDebug() << "ActionRouter: 未命中：" << actionId;
    emit dispatched(actionId, false);
    return false;
}

bool ActionRouter::isAvailable(const QString &actionId) const
{
    const auto it = m_entries.constFind(actionId);
    if (it != m_entries.constEnd())
        return !(it->enabled && !it->enabled());
#if defined(STELQUICK_HAS_ENGINE)
    if (StelApp::isInitialized())
        return StelApp::getInstance().getStelActionManager()->findAction(actionId) != nullptr;
#endif
    return false;
}

QStringList ActionRouter::actionIds() const
{
    // 只列注册表——引擎 700+ 动作不进 QML 菜单白名单（白名单制，见 AppFacade.hpp 头注）。
    return QStringList(m_entries.keyBegin(), m_entries.keyEnd());
}

QString ActionRouter::actionTitle(const QString &actionId) const
{
    const auto it = m_entries.constFind(actionId);
    if (it != m_entries.constEnd())
        return it->title;
#if defined(STELQUICK_HAS_ENGINE)
    if (StelApp::isInitialized())
    {
        if (const StelAction *action =
                StelApp::getInstance().getStelActionManager()->findAction(actionId))
            return action->getText();
    }
#endif
    return QString();
}

bool ActionRouter::canDispatchToSky() const
{
    // U-ACT-03：焦点在 QML 可编辑输入控件（或原生 QLineEdit）时不触发天空快捷键。
    // QML 的 TextInput/TextArea 不是 QWidget，经 QGuiApplication::focusObject 呈现；
    // QWidget 形态的 QLineEdit 经 focusWidget 呈现。两者都查。
    if (QObject *focus = QGuiApplication::focusObject())
    {
        const QString className = focus->metaObject()->className();
        if (className == QLatin1String("QQuickTextInput")          // TextInput
            || className == QLatin1String("QQuickTextArea"))       // TextArea
            return false;
    }
#ifdef QT_WIDGETS_LIB
    // focusWidget 属 QApplication（Widgets），QGuiApplication 没有该接口——
    // 合流形态宿主是 QApplication（Widgets 宿主），此路径可用。
    if (QWidget *widget = QApplication::focusWidget())
    {
        if (qobject_cast<QLineEdit *>(widget) || qobject_cast<QTextEdit *>(widget))
            return false;
    }
#endif
    return true;
}

bool ActionRouter::routeKey(int key, int modifiers)
{
    if (!canDispatchToSky())
        return false;
#if defined(STELQUICK_HAS_ENGINE)
    if (StelApp::isInitialized())
    {
        const QKeySequence sequence(modifiers | key);
        const QList<StelAction *> actions =
            StelApp::getInstance().getStelActionManager()->getActionList();
        for (StelAction *action : actions)
        {
            if (action->matches(sequence) == QKeySequence::ExactMatch)
            {
                // 单点触发：widget QAction 分发已拆除（见头注），此处是唯一入口，
                // 同一次按键不可能新旧双触发（U-ACT-01）。
                action->trigger();
                ++m_dispatchCount;
                emit dispatched(action->getId(), true);
                return true;
            }
        }
    }
#else
    Q_UNUSED(key);
    Q_UNUSED(modifiers);
#endif
    return false;
}

} // namespace stelapp
