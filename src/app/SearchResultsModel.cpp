/*
 * SearchResultsModel — 实现（T17）。
 *
 * 引擎依赖全部用 STELQUICK_HAS_ENGINE 守卫：独立工程形态（无引擎）下 collect()
 * 编为安全空实现并给出明确的空态理由（"未链接引擎"），QML 搜索框可输入但不崩。
 * 设计与禁止事项见 SearchResultsModel.hpp 头注。
 */
#include "app/SearchResultsModel.hpp"
#include "app/SearchRanker.hpp"
#include "app/PinyinIndex.hpp"

#if defined(STELQUICK_HAS_ENGINE)
#include "StelApp.hpp"
#include "StelObject.hpp"
#include "StelObjectMgr.hpp"
#include "StelModuleMgr.hpp"
#include "StelObjectModule.hpp"
#endif

#include <algorithm>
#include <QHash>

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
    case RankRole:        return index.row();   // T21 起即相关度序（排序已在本层生效）
    case QualityRole:     return r.quality;
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
        { QualityRole,     "quality" },
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
    m_lastPinyin = 0;

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

    // ── ⓪ T24 拼音检索：ASCII 查询补充"中文名拼音命中"的候选 ──────────────
    // 引擎检索对 "yueqiu" 这类拼音串必然空手而归（字面 contains 不认汉字名），
    // 中文界面下"用键盘找天体"这条路只有这里能救——这是**召回**修复，不是排序。
    // 候选源：各模块 listAllObjects(false)（翻译名）。只对含汉字的名字做拼音
    // 匹配（英文名引擎那路已覆盖）；与引擎候选进入同一套 ①去重 ②排序 ③截断。
    // m_lastRaw 语义保持"引擎检索返回条数"，拼音候选数单独记 m_lastPinyin
    //（判据 SRC-05b 的 "raw ≥ rows" 口径在拼音分支下按 raw+pinyin 校正）。
    QVector<QPair<QString, StelObjectP>> pinyinMatches;
    if (PinyinIndex::isPinyinQuery(q) && PinyinIndex::available()) {
        const QString qLower = q.toLower();
        // 候选源 = 全部 StelObjectModule（经 StelModuleMgr 枚举后 dynamic_cast 收敛）。
        // StelObjectMgr::objectsModules 是 private —— 不为读一个列表去动引擎，
        // StelModuleMgr::getAllModules() 是公开接口，cast 语义与引擎聚合内部一致
        // （StelObjectMgr::registerObject 也是把 StelObjectModule* 塞进同一张表）。
        const auto allModules = StelApp::getInstance().getModuleMgr().getAllModules();
        for (StelModule *m : allModules) {
            auto *mod = dynamic_cast<StelObjectModule *>(m);
            if (!mod)
                continue;
            const auto all = mod->listAllObjects(false);
            for (const auto &pr : all) {
                if (!pr.second || pr.first.isEmpty() || !PinyinIndex::hasCjk(pr.first))
                    continue;
                const MatchQuality pq = PinyinIndex::matchQuality(pr.first, qLower);
                if (pq == MatchQuality::None)
                    continue;
                pinyinMatches.append(pr);
            }
        }
    }
    m_lastPinyin = pinyinMatches.size();

    // ── ① 去重（按 stableId）：保留**相关度最高**的那一行 ──────────────────
    // 引擎把**翻译名表**与**英文名表**各枚举一遍（StelObjectModule.cpp:52
    // `objs << listAllObjects(false) << listAllObjects(true)`），只要该天体有 ≥2 个
    // 名字含查询串就**各产出一条**；`StelObjectMgr` 只做拼接 + 字典序重排，**不去重**。
    // 实测搜 "Moon" 出 5 条，其中 2 对是同一 stableId（NGC 6781 ×2、NGC 1647 ×2）。
    //
    // ⚠️ T21 连带的语义改动：去重时**保留哪一行**决定用户看到的 `name`
    //（两行的 name 可能是翻译名 vs 英文名）。T17-T20 保留的是"遍历顺序最先"那条
    //（因聚合层排过字典序，等价于"字典序最前"）；T21 起改为**按相关度比**。
    // 否则会出现"去重留下了低相关度的那个名字，排序再准也白搭"。
    QVector<Row> cand;
    QHash<QString, int> seenIndex;   //!< stableId → cand 下标
    cand.reserve(matches.size());
    for (const QPair<QString, StelObjectP> &m : matches) {
        const StelObjectP &obj = m.second;
        if (!obj)                    // 引擎理论上不给空，但空指针一旦入表就是悬空隐患
            continue;                // ——此处直接丢弃该行，宁可少一行也不留隐患。
        Row r;
        r.name        = m.first;
        r.englishName = obj->getEnglishName();
        r.objectType  = obj->getObjectTypeI18n();
        r.typeName    = obj->getType();
        r.stableId    = QStringLiteral("%1:%2").arg(r.typeName, obj->getID());
        r.quality     = static_cast<int>(SearchRanker::quality(r.name, q));

        const auto it = seenIndex.constFind(r.stableId);
        if (it != seenIndex.constEnd()) {
            const Row &prev = cand.at(it.value());
            if (SearchRanker::less(SearchRanker::key(r.name, r.typeName, q),
                                   SearchRanker::key(prev.name, prev.typeName, q)))
                cand[it.value()] = r;    // 新的这个更相关 ⇒ 换掉
            continue;
        }
        seenIndex.insert(r.stableId, cand.size());
        cand.append(r);
        // obj 到此为止：**不存指针**。契约"不向 QML 传悬空裸指针"就在这一行落实。
    }

    // ── ①' 拼音候选并入同一套去重（T24）：同一 stableId 若引擎检索也给了，──
    // 保留**相关度更高**的那一行（拼音行 quality 由 PinyinIndex 判定，通常低于
    // 字面命中——同名冲突时正确让位）。
    for (const auto &pr : pinyinMatches) {
        const StelObjectP &obj = pr.second;
        if (!obj)
            continue;
        Row r;
        r.name        = pr.first;
        r.englishName = obj->getEnglishName();
        r.objectType  = obj->getObjectTypeI18n();
        r.typeName    = obj->getType();
        r.stableId    = QStringLiteral("%1:%2").arg(r.typeName, obj->getID());
        r.quality     = static_cast<int>(PinyinIndex::matchQuality(r.name, q.toLower()));

        const auto it = seenIndex.constFind(r.stableId);
        if (it != seenIndex.constEnd()) {
            const Row &prev = cand.at(it.value());
            if (r.quality < prev.quality)
                cand[it.value()] = r;
            continue;
        }
        seenIndex.insert(r.stableId, cand.size());
        cand.append(r);
    }

    // ── ② 相关度排序（T21）：恢复被聚合层 `std::sort` 抹掉的完全匹配优先序 ──
    // 用 stable_sort：比较键里已经带了名称字典序作尾键（全序），稳定排序只是
    // 双重保险，避免"同键元素的相对顺序未定义"这种不可复现的来源。
    std::stable_sort(cand.begin(), cand.end(), [&q](const Row &a, const Row &b) {
        return SearchRanker::less(SearchRanker::key(a.name, a.typeName, q),
                                  SearchRanker::key(b.name, b.typeName, q));
    });

    // ── ③ 截断到总量上限（排序之后才截断，否则截断会先杀掉高相关度候选）──
    const int n = std::min<int>(cand.size(), cap);
    rows.reserve(n);
    for (int i = 0; i < n; ++i)
        rows.append(cand.at(i));

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
