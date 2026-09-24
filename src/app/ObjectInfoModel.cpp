/*
 * ObjectInfoModel — 实现（T17）。
 *
 * 引擎依赖用 STELQUICK_HAS_ENGINE 守卫：独立工程形态下 refresh() 恒返回 false、
 * 属性恒为空，QML 信息页显示"未链接引擎"而不是崩溃。
 * 不持有天体指针的纪律见 ObjectInfoModel.hpp 头注。
 */
#include "app/ObjectInfoModel.hpp"

#if defined(STELQUICK_HAS_ENGINE)
#include "StelApp.hpp"
#include "StelCore.hpp"
#include "StelObject.hpp"
#include "StelObjectMgr.hpp"
#endif

namespace stelapp {

//! stableId 的分隔符：与 SearchResultsModel::collect 的拼法保持一致（改一处必须改两处）。
static const QChar kStableIdSep = QLatin1Char(':');

ObjectInfoModel::ObjectInfoModel(QObject *parent)
    : QObject(parent)
{
    m_engineReady = engineReady();
}

bool ObjectInfoModel::engineReady() const
{
#if defined(STELQUICK_HAS_ENGINE)
    return StelApp::isInitialized();
#else
    return false;
#endif
}

// ── 读引擎 ───────────────────────────────────────────────────────────────────

#if defined(STELQUICK_HAS_ENGINE)

bool ObjectInfoModel::refresh()
{
    const bool ready = StelApp::isInitialized();
    if (ready != m_engineReady) {          // 引擎可用性边沿：只在此处通知一次
        m_engineReady = ready;
        emit engineReadyChanged(m_engineReady);
    }
    if (!ready) {
        resetToEmpty();
        return false;
    }

    StelObjectMgr &mgr = StelApp::getInstance().getStelObjectMgr();
    const QList<StelObjectP> &sel = mgr.getSelectedObject();
    if (sel.isEmpty() || !sel.first()) {   // 未选中，或选中的是空指针
        resetToEmpty();
        return false;
    }

    const StelObjectP &obj = sel.first();
    StelCore *core = StelApp::getInstance().getCore();

    // 先在**局部**把值取全，再一次性写入 + 通知：宁可多算一次，
    // 也不要在"写了一半"的状态下让 QML 读到（属性之间必须自洽）。
    const QString name    = obj->getNameI18n();
    const QString english = obj->getEnglishName();
    const QString otype   = obj->getObjectTypeI18n();
    const QString tname   = obj->getType();
    const QString sid     = QStringLiteral("%1%2%3").arg(tname, kStableIdSep, obj->getID());
    // DefaultInfo 是引擎自己给面板用的口径（比 AllInfo 轻，比 ShortInfo 有料）。
    const QString text    = core ? obj->getInfoString(core, StelObject::DefaultInfo) : QString();
    const QVariantMap map = core ? obj->getInfoMap(core) : QVariantMap();

    m_hasSelection = true;
    m_displayName  = name.isEmpty() ? english : name;
    m_englishName  = english;
    m_objectType   = otype;
    m_typeName     = tname;
    m_stableId     = sid;
    m_infoText     = text;
    m_infoMap      = map;
    emit selectionChanged();
    return true;
}

bool ObjectInfoModel::selectByStableId(const QString &stableId)
{
    QString type, id;
    if (!splitStableId(stableId, &type, &id) || !StelApp::isInitialized()) {
        resetToEmpty();
        return false;
    }

    StelObjectMgr &mgr = StelApp::getInstance().getStelObjectMgr();
    // 回查：在"当下"重新解析（U-SRC-04）。对象被卸载时会拿到空指针——
    // 这正是"失效"的正解信号，归零即可，绝不沿用旧值。
    const StelObjectP obj = mgr.searchByID(type, id);
    if (!obj) {
        resetToEmpty();
        return false;
    }

    // ── T18 幂等闸：重选**同一个**对象时不要重发 setSelectedObject ────────────
    // 为什么必须加：StelMovementMgr::selectedObjectChange()（StelMovementMgr.cpp:757-766）
    // 在"确有选中"的每次选择变化时都会 **无条件** `setFlagTracking(false)`。
    // 于是"重复点同一条搜索结果"（QML 列表重入、信息页刷新、回到列表再点一次）
    // 会**静默取消正在进行的跟踪**——用户看到的是"跟踪自己断了"。
    // 判据用 (type, id) 而不是 stableId 字符串：与 selectByStableId 进来的
    // 参数是同一份语义（splitStableId 已保证 type/id 非空）。
    const QList<StelObjectP> &cur = mgr.getSelectedObject();
    const bool alreadySame = !cur.isEmpty() && cur.first()
                             && cur.first()->getType() == type
                             && cur.first()->getID() == id;
    if (!alreadySame) {
        if (!mgr.setSelectedObject(obj)) {
            resetToEmpty();
            return false;
        }
    } else {
        // 已选中同一对象：直接刷新信息面（走 refresh() 的那条路），
        // 引擎侧一次 setSelectedObject 都不发 ⇒ 跟踪不被打断。
        emit selectionChanged();     // 让 QML 知道"命令收到了"（值不变，幂等）
        return refresh();
    }
    return refresh();
}

void ObjectInfoModel::clearSelection()
{
    if (StelApp::isInitialized())
        StelApp::getInstance().getStelObjectMgr().unSelect();
    resetToEmpty();
}

#else  // 独立工程形态（未链接引擎）

bool ObjectInfoModel::refresh()
{
    if (m_engineReady) {          // 极端情形（形态切换）下的边沿通知
        m_engineReady = false;
        emit engineReadyChanged(false);
    }
    resetToEmpty();
    return false;
}

bool ObjectInfoModel::selectByStableId(const QString &stableId)
{
    Q_UNUSED(stableId);
    resetToEmpty();
    return false;
}

void ObjectInfoModel::clearSelection()
{
    resetToEmpty();
}

#endif

// ── 安全空值 ─────────────────────────────────────────────────────────────────

void ObjectInfoModel::clear()
{
    resetToEmpty();
}

bool ObjectInfoModel::splitStableId(const QString &stableId, QString *type, QString *id)
{
    // 只按**首个**冒号切分：天体 ID 自身可能含冒号（如某些目录号），
    // 从右边切会把类型名和 ID 都切错。
    const int pos = stableId.indexOf(kStableIdSep);
    if (pos <= 0 || pos >= stableId.size() - 1)
        return false;                 // 缺分隔符 / 类型名为空 / ID 为空 → 非法
    if (type)
        *type = stableId.left(pos);
    if (id)
        *id = stableId.mid(pos + 1);
    return true;
}

void ObjectInfoModel::resetToEmpty()
{
    // 只在**确有变化**时发通知：本方法会在"每次 refresh() 无选中"时被调用，
    // 无条件 emit 会让 QML 侧每帧重算信息页。
    const bool changed = m_hasSelection
                         || !m_displayName.isEmpty()
                         || !m_englishName.isEmpty()
                         || !m_objectType.isEmpty()
                         || !m_typeName.isEmpty()
                         || !m_stableId.isEmpty()
                         || !m_infoText.isEmpty()
                         || !m_infoMap.isEmpty();
    m_hasSelection = false;
    m_displayName.clear();
    m_englishName.clear();
    m_objectType.clear();
    m_typeName.clear();
    m_stableId.clear();
    m_infoText.clear();
    m_infoMap.clear();
    if (changed)
        emit selectionChanged();
}

} // namespace stelapp
