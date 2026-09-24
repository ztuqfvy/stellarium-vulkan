/*
 * SearchResultsModel — 实现（T17）。
 *
 * 引擎依赖全部用 STELQUICK_HAS_ENGINE 守卫：独立工程形态（无引擎）下 collect()
 * 编为安全空实现并给出明确的空态理由（"未链接引擎"），QML 搜索框可输入但不崩。
 * 设计与禁止事项见 SearchResultsModel.hpp 头注。
 */
#include "app/SearchResultsModel.hpp"

#if defined(STELQUICK_HAS_ENGINE)
#include "StelApp.hpp"
#include "StelObject.hpp"
#include "StelObjectMgr.hpp"
#endif

#include <algorithm>

namespace stelapp {

SearchResultsModel::SearchResultsModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

// ── QAbstractListModel ───────────────────────────────────────────────────────

int SearchResultsModel::rowCount(const QModelIndex &parent) const
{
    // 列表模型：只有顶层有行（树形父索引下恒 0，否则 QML 会因递归查询卡死）。
    if (parent.isValid())
        return 0;
    return m_rows.size();
}

QVariant SearchResultsModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return QVariant();

    const Row &r = m_rows.at(index.row());
    switch (role) {
    case NameRole:        return r.name;
    case EnglishNameRole: return r.englishName;
    case ObjectTypeRole:  return r.objectType;
    case TypeNameRole:    return r.typeName;
    case StableIdRole:    return r.stableId;
    case RankRole:        return index.row();
    default:              return QVariant();
    }
}

QHash<int, QByteArray> SearchResultsModel::roleNames() const
{
    // 角色名是 QML 的公开契约（delegate 里写 model.name / model.stableId）。
    static const QHash<int, QByteArray> roles{
        { NameRole,        "name" },
        { EnglishNameRole, "englishName" },
        { ObjectTypeRole,  "objectType" },
        { TypeNameRole,    "typeName" },
        { StableIdRole,    "stableId" },
        { RankRole,        "rank" },
    };
    return roles;
}

// ── 搜索与投递门 ─────────────────────────────────────────────────────────────

quint32 SearchResultsModel::search(const QString &query, int maxItems)
{
    const quint32 requestId = ++m_requestId;   // 单调递增：编号永不复用，故"过期"判定无歧义
    ++m_searchCount;

    if (query != m_lastQuery) {
        m_lastQuery = query;
        emit lastQueryChanged(m_lastQuery);
    }
    if (!m_searching) {
        m_searching = true;
        emit searchingChanged(true);
    }
    emit searchStarted(requestId, query);

    QVector<Row> rows;
    QString emptyReason;
    collect(query, maxItems, rows, emptyReason);

    // 同步路径同样过门——不是形式主义：插件钩子造成的重入会让"后发起、先完成"
    // 真实发生，此处正是拦截点（详见头注"请求编号门是不是装饰"）。
    applyResults(requestId, rows, emptyReason);
    return requestId;
}

bool SearchResultsModel::applyResults(quint32 requestId, const QVector<Row> &rows, const QString &emptyReason)
{
    // 门：编号为 0（无效）或不等最新 → 过期，丢弃且**不触碰任何可见状态**（U-SRC-01）。
    if (requestId == 0 || requestId != m_requestId) {
        ++m_discarded;
        emit requestDiscarded(requestId, m_requestId);
        return false;
    }

    beginResetModel();
    m_rows = rows;
    endResetModel();

    m_lastCompleted = requestId;

    const bool empty = m_rows.isEmpty();
    const QString reason = empty ? (emptyReason.isEmpty() ? tr("无匹配天体") : emptyReason)
                                 : QString();
    if (reason != m_emptyReason) {
        m_emptyReason = reason;
        emit emptyReasonChanged(m_emptyReason);
    }
    emit countChanged(m_rows.size());

    if (m_searching) {
        m_searching = false;
        emit searchingChanged(false);
    }

    emit resultsReady(requestId, m_rows.size());
    if (empty)
        emit resultsEmpty(m_lastQuery, m_emptyReason);   // U-SRC-02：明确空态，不静默

    return true;
}

void SearchResultsModel::clear()
{
    if (!m_rows.isEmpty()) {
        beginResetModel();
        m_rows.clear();
        endResetModel();
        emit countChanged(0);
    }
    if (!m_emptyReason.isEmpty()) {
        m_emptyReason.clear();
        emit emptyReasonChanged(m_emptyReason);
    }
    // 刻意**不**动 m_requestId：清空列表不等于"没有最新请求"，
    // 否则在途结果会因编号相等而被误收（防线不能自己开个口子）。
}

QString SearchResultsModel::stableIdAt(int row) const
{
    return (row >= 0 && row < m_rows.size()) ? m_rows.at(row).stableId : QString();
}

QString SearchResultsModel::nameAt(int row) const
{
    return (row >= 0 && row < m_rows.size()) ? m_rows.at(row).name : QString();
}

QString SearchResultsModel::englishNameAt(int row) const
{
    // 英文名是**身份比对**用的键（显示名会随界面语言变化，不能当身份用）。
    return (row >= 0 && row < m_rows.size()) ? m_rows.at(row).englishName : QString();
}

QString SearchResultsModel::typeNameAt(int row) const
{
    return (row >= 0 && row < m_rows.size()) ? m_rows.at(row).typeName : QString();
}

// ── 引擎取数 ─────────────────────────────────────────────────────────────────

#if defined(STELQUICK_HAS_ENGINE)

void SearchResultsModel::collect(const QString &query, int maxItems, QVector<Row> &rows, QString &emptyReason)
{
    rows.clear();
    emptyReason.clear();
    m_lastRaw = 0;

    const QString q = query.trimmed();
    if (q.isEmpty()) {
        emptyReason = tr("请输入天体名称");
        return;
    }
    if (!StelApp::isInitialized()) {
        // 注意：getStelObjectMgr() 是 `return *stelObjectMgr;`（解引用），
        // 未初始化时它可能为空指针——必须先判 isInitialized()，不能先取引用。
        emptyReason = tr("引擎未就绪");
        return;
    }

    // 总量上限（见头注"排序口径"）：引擎原语的 maxNbItem 是**每模块**上限，
    // 它在 StelObjectMgr.cpp 里把各模块结果 `result +=` 拼接后再按名称字典序重排，
    // 因此调用方拿到的是"至多 模块数 × maxNbItem"条。本层统一为总量上限。
    const int cap = std::max(1, maxItems);

    StelObjectMgr &mgr = StelApp::getInstance().getStelObjectMgr();
    const QVector<QPair<QString, StelObjectP>> matches = mgr.listMatchingObjects(q, cap, false);
    m_lastRaw = matches.size();

    rows.reserve(std::min<int>(matches.size(), cap));
    for (const QPair<QString, StelObjectP> &m : matches) {
        if (rows.size() >= cap)          // 总量截断：取字典序前 cap 条
            break;
        const StelObjectP &obj = m.second;
        if (!obj)                    // 引擎理论上不给空，但空指针一旦入表就是悬空隐患
            continue;                // ——此处直接丢弃该行，宁可少一行也不留隐患。
        Row r;
        r.name        = m.first;
        r.englishName = obj->getEnglishName();
        r.objectType  = obj->getObjectTypeI18n();
        r.typeName    = obj->getType();
        r.stableId    = QStringLiteral("%1:%2").arg(r.typeName, obj->getID());
        rows.append(r);
        // obj 到此为止：**不存指针**。契约"不向 QML 传悬空裸指针"就在这一行落实。
    }

    if (rows.isEmpty())
        emptyReason = tr("未找到匹配「%1」的天体").arg(q);
}

#else  // 独立工程形态（未链接引擎）

void SearchResultsModel::collect(const QString &query, int maxItems, QVector<Row> &rows, QString &emptyReason)
{
    Q_UNUSED(query);
    Q_UNUSED(maxItems);
    rows.clear();
    emptyReason = tr("未链接引擎（独立工程形态）");
}

#endif

} // namespace stelapp
