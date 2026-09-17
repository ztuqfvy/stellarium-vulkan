/*
 * ObjectInfoModel — 选中天体只读信息模型（骨架，未实现）。
 *
 * 职责：通过只读接口向 QML 提供选中天体信息（名称、类型、距离、星等、赤经赤纬…）。
 * 禁止：不让 QML 持有失效天体指针；不传悬空裸指针。
 *
 * 对象失效（被引擎移除/取消选中）时发布安全空值或失效标记，测试 U-SRC-03。
 * 实现顺序：A3。
 */
#pragma once

#include <QObject>

namespace stelapp {

class ObjectInfoModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool hasSelection READ hasSelection NOTIFY selectionChanged)
    Q_PROPERTY(QString displayName READ displayName NOTIFY selectionChanged)
    // 其余只读属性（类型/距离/星等/坐标）实现时按白名单补充。

public:
    explicit ObjectInfoModel(QObject *parent = nullptr);

    bool hasSelection() const;
    QString displayName() const;

signals:
    void selectionChanged();

private:
    // m_stableId：稳定对象标识；跨帧有效，失效时置空并通知。
    bool m_hasSelection = false;
    QString m_displayName;
};

} // namespace stelapp
