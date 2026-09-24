/*
 * ObjectInfoModel — 选中天体只读信息模型（T17 落地）。
 *
 * 职责：把引擎当前选中对象投影成 QML 可读的**只读属性**（名称、类型、稳定标识、
 *       结构化信息、原始信息串）；提供 stableId → 对象的**回查**能力。
 * 禁止：不让 QML 持有失效天体指针；不传悬空裸指针。
 *
 * ── 本类为什么一个天体指针都不存 ──────────────────────────────────────────
 * StelObjectType.hpp:44 `using StelObjectP = QSharedPointerNoDelete<StelObject>` ——
 * 不持有所有权的共享指针。存它**不会**延长对象寿命，对象仍可能在下一帧被模块销毁，
 * 于是"模型里那个指针"随时会变成悬空指针。本类的做法是：
 *   · 属性面全部是**值**（QString / QVariantMap）；
 *   · 每次 refresh() 都**当场**向引擎重新取一次选中对象，取不到就发布安全空值；
 *   · 需要"回到某个对象"时用 stableId 重新解析（selectByStableId），不复用旧指针。
 * 这样"对象被移除"这件事在本类里表现为**空值**，而不是崩溃（U-SRC-03）。
 *
 * ── 对象失效（U-SRC-03）的两种入口 ────────────────────────────────────────
 *   ① 引擎侧取消选中/对象消失 → refresh() 读到空选中 → 属性归零（hasSelection=false）；
 *   ② stableId 已无法解析（对象被卸载）→ selectByStableId() 返回 false 并归零，
 *      **不**保留上一次的陈旧值（陈旧值比空值更危险：QML 会当成当前状态渲染）。
 *
 * @see StelObjectMgr.hpp:129 getSelectedObject / :150-157 searchByID / :120 setSelectedObject
 * 测试用例：U-SRC-03..04（软件测试文档 3.3）。实现：T17（A3 正题）。
 */
#pragma once

#include <QObject>
#include <QString>
#include <QVariantMap>

namespace stelapp {

class ObjectInfoModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool hasSelection READ hasSelection NOTIFY selectionChanged)
    Q_PROPERTY(QString displayName READ displayName NOTIFY selectionChanged)
    Q_PROPERTY(QString englishName READ englishName NOTIFY selectionChanged)
    Q_PROPERTY(QString objectType READ objectType NOTIFY selectionChanged)
    Q_PROPERTY(QString typeName READ typeName NOTIFY selectionChanged)
    Q_PROPERTY(QString stableId READ stableId NOTIFY selectionChanged)
    //! 引擎 getInfoString 的结果（**含 HTML 标记**，QML 侧用 Text.RichText 渲染）。
    Q_PROPERTY(QString infoText READ infoText NOTIFY selectionChanged)
    //! 引擎 getInfoMap 的结构化结果（键如 ra/dec/iauConstellation…），供页面自行挑选。
    Q_PROPERTY(QVariantMap infoMap READ infoMap NOTIFY selectionChanged)
    //! 引擎不可用/未引导（QML 据此显示"引擎未就绪"而不是空信息页）。
    Q_PROPERTY(bool engineReady READ engineReady NOTIFY engineReadyChanged)

public:
    explicit ObjectInfoModel(QObject *parent = nullptr);

    bool hasSelection() const { return m_hasSelection; }
    QString displayName() const { return m_displayName; }
    QString englishName() const { return m_englishName; }
    QString objectType() const { return m_objectType; }
    QString typeName() const { return m_typeName; }
    QString stableId() const { return m_stableId; }
    QString infoText() const { return m_infoText; }
    QVariantMap infoMap() const { return m_infoMap; }
    bool engineReady() const;

    //! THREAD: gui
    //! 从引擎当前选中对象重新取一遍；取不到（未选/已失效/引擎未就绪）→ 归零。
    //! @return 是否成功取到选中对象
    Q_INVOKABLE bool refresh();

    //! THREAD: gui
    //! 按稳定标识选中并回填（U-SRC-04 的"定位回查"能力）。
    //! @return true=解析并选中成功；false=标识非法或对象已不存在（此时属性归零）
    Q_INVOKABLE bool selectByStableId(const QString &stableId);

    //! THREAD: gui
    //! 引擎侧取消选中（unSelect）并归零本模型。
    Q_INVOKABLE void clearSelection();

    //! 只归零本模型，不触碰引擎（自检与关闭路径用；U-SRC-03 的"安全空值"本体）。
    void clear();

signals:
    void selectionChanged();
    void engineReadyChanged(bool ready);

private:
    //! 安全空值：清掉全部属性并**仅在有变化时**发一次 selectionChanged。
    void resetToEmpty();
    //! 拆 "type:id" —— id 自身可能含 ':'，故只按**首个**冒号切分。
    static bool splitStableId(const QString &stableId, QString *type, QString *id);

    bool m_hasSelection = false;
    QString m_displayName;
    QString m_englishName;
    QString m_objectType;
    QString m_typeName;
    QString m_stableId;
    QString m_infoText;
    QVariantMap m_infoMap;
    bool m_engineReady = false;   //!< 上次查询时的引擎可用性（用于 engineReady 边沿通知）
};

} // namespace stelapp
