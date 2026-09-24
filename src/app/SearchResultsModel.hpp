/*
 * SearchResultsModel — 本地天体搜索结果增量列表模型（T17 落地）。
 *
 * 职责：包装 StelObjectMgr::listMatchingObjects 的结果，经**请求编号门**投递；
 *       向 QML 只暴露展示字段与**稳定对象标识**（stableId），不传任何指针。
 * 禁止：过期搜索结果不得覆盖新查询；不向 QML 传悬空裸指针。
 *
 * ── 稳定标识为什么是 "type:id" ────────────────────────────────────────────
 * 依据引擎自己的注释（不是本层发明）：
 *   StelObject.hpp:365-377 —— getID() 在**同一类型内**唯一，但「可与其他类型的 ID
 *   自由冲突，因此 getType() 必须一并参与判定」（"may freely conflict with IDs of
 *   other types, so getType() must also be tested"）。
 * 故 stableId = getType() + ":" + getID()；回查走 StelObjectMgr::searchByID(type, id)
 * （StelObjectMgr.hpp:150-157，它会在同一类型的所有 ID 变体里搜索）。
 *
 * ── 为什么一个指针都不能存 ────────────────────────────────────────────────
 * StelObjectType.hpp:44 里 `using StelObjectP = QSharedPointerNoDelete<StelObject>`——
 * 它是**不持有所有权**的共享指针，对象生命周期仍归各自模块所有。把它存进模型
 * **不会延长对象寿命**，于是"模型还在、对象已销毁"就是必然会发生的事。
 * 所以本模型内部只保留字符串；任何需要对象实体（选择、取信息）的动作都在
 * **当下**经引擎重新解析（见 ObjectInfoModel::selectByStableId）。
 *
 * ── 请求编号门是不是装饰（U-SRC-01）──────────────────────────────────────
 * 当前引擎搜索是**同步**的（listMatchingObjects 在 GUI 线程内返回），所以不存在
 * 真并发下的"旧请求后到达"。但这道门不是摆设，三个理由：
 *   ① 引擎搜索会经插件钩子产生**重入**——一次 search 的执行过程中触发另一次 search；
 *   ② 无条件重置列表会让"重入"表现为结果错乱（QML 上看到的是后发起、先完成的那个）；
 *   ③ 本层语义应独立于"引擎今天恰好是同步的"这一实现细节。
 * 门控真实生效且有可观测计数（discardedRequests），自检可直接构造"旧请求后到达"验证它。
 *
 * @see StelObjectMgr.hpp:105 listMatchingObjects / :157 searchByID
 * 测试用例：U-SRC-01..04（软件测试文档 3.3）。实现：T17（A3 正题）。
 */
#pragma once

#include <QAbstractListModel>
#include <QString>
#include <QVector>
#include <QVariant>

namespace stelapp {

class SearchResultsModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(bool searching READ searching NOTIFY searchingChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(QString lastQuery READ lastQuery NOTIFY lastQueryChanged)
    //! 空态文案（U-SRC-02）：无结果/未就绪时给出**明确**理由，而不是让 QML 猜。
    Q_PROPERTY(QString emptyReason READ emptyReason NOTIFY emptyReasonChanged)

public:
    //! 结果行。**全部是值语义字符串**——这是"不传悬空裸指针"的落实点。
    struct Row
    {
        QString name;         //!< 引擎给出的匹配名（listMatchingObjects 的 first）
        QString englishName;  //!< StelObject::getEnglishName()
        QString objectType;   //!< getObjectTypeI18n()：天文类型（"行星"/"恒星"…）
        QString typeName;     //!< getType()：Stellarium 类名（回查用，见头注）
        QString stableId;     //!< "typeName:id"
    };

    enum Roles
    {
        NameRole = Qt::UserRole + 1,
        EnglishNameRole,
        ObjectTypeRole,
        TypeNameRole,
        StableIdRole,
        RankRole,             //!< 行序（0 基）。**不是相关度序**——见下方"排序口径"。
    };
    Q_ENUM(Roles)

    //! 排序口径（依据引擎源码 StelObjectMgr.cpp::listMatchingObjects，非推测）：
    //!   该方法遍历**所有**注册模块，各取至多 maxNbItem 条后 `result += ...` 拼接，
    //!   最后 `std::sort` 按**名称字符串字典序**排。
    //!   ⇒ ① 引擎头注写的"by order of relevance（按相关度）"对**聚合结果不成立**，
    //!        因为拼接后又被字典序重排了；
    //!     ② 调用方给的 maxNbItem 只是"**每模块**上限"，总量可达 模块数 × maxNbItem。
    //!   本层因此把 maxItems 统一解释为**总量上限**（取字典序前 maxItems 条），
    //!   使 QML 侧的"最多要 N 条"是真的 N 条；原始匹配数经 lastRawMatchCount()
    //!   暴露出来，便于证明上限真的生效过。
    //!   更进一步的排序策略（相关度/类型分组）留给 A4 搜索页，不在本层臆造。

    explicit SearchResultsModel(QObject *parent = nullptr);

    // ---- QAbstractListModel ----
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    //! THREAD: gui
    //! 发起搜索：递增请求编号 → 取结果 → 经 applyResults 门投递。
    //! @param maxItems 结果**总条数**上限（注意：引擎原语是"每模块"上限，
    //!                 本层已把语义统一为总量，理由见"排序口径"注释）
    //! @return 本次请求编号（恒 > 0；前端无需关心，供自检对账）
    Q_INVOKABLE quint32 search(const QString &query, int maxItems = 20);

    //! 结果投递门（U-SRC-01）。**公开**是为了让自检能构造"旧请求后到达"的场景
    //! （同步实现下该门仍需真实存在，理由见头注）。
    //! @return true=已投递；false=过期被丢弃（列表与计数均按"丢弃"处置）
    bool applyResults(quint32 requestId, const QVector<Row> &rows, const QString &emptyReason = QString());

    //! 清空列表与空态（不发请求、不改请求编号——不清除"最新请求"这一事实）。
    Q_INVOKABLE void clear();

    //! 行查询（选择路径用：AppFacade 取 stableId 后交引擎解析）。
    Q_INVOKABLE QString stableIdAt(int row) const;
    Q_INVOKABLE QString nameAt(int row) const;
    Q_INVOKABLE QString englishNameAt(int row) const;
    Q_INVOKABLE QString typeNameAt(int row) const;

    bool searching() const { return m_searching; }
    int count() const { return m_rows.size(); }
    QString lastQuery() const { return m_lastQuery; }
    QString emptyReason() const { return m_emptyReason; }

    // ---- 观测量（自检挂点，只增不减）----
    quint32 currentRequestId() const { return m_requestId; }        //!< 最新请求编号
    quint32 lastCompletedRequestId() const { return m_lastCompleted; } //!< 最后一次**被接受**的编号
    quint64 discardedRequests() const { return m_discarded; }        //!< 过期丢弃次数（U-SRC-01 判据）
    quint64 searchCount() const { return m_searchCount; }
    //! 最近一次引擎取数的**原始**匹配条数（截断前）。用来证明"总量上限真的生效"：
    //! 正常环境下它应大于等于最终行数；两者相等即说明本次未触发截断。
    int lastRawMatchCount() const { return m_lastRaw; }

signals:
    void searchingChanged(bool searching);
    void countChanged(int count);
    void lastQueryChanged(const QString &query);
    void emptyReasonChanged(const QString &reason);
    //! 请求已发出（供 QML 显示"搜索中"、供自检对账编号）。
    void searchStarted(quint32 requestId, const QString &query);
    //! 结果被接受（含空结果：此时 count=0，由 resultsEmpty 表达空态）。
    void resultsReady(quint32 requestId, int count);
    //! 空态信号（U-SRC-02）：无匹配/未就绪时补发，QML 据此显示 emptyReason。
    void resultsEmpty(const QString &query, const QString &reason);
    //! 过期结果被丢弃（U-SRC-01 的可观测点）。
    void requestDiscarded(quint32 staleRequestId, quint32 currentRequestId);

private:
    //! 取结果（唯一触达引擎的地方；无引擎形态编为安全空实现）。
    //! 非 const：会记录 m_lastRaw（原始匹配数）供观测量读取。
    void collect(const QString &query, int maxItems, QVector<Row> &rows, QString &emptyReason);

    QVector<Row> m_rows;
    quint32 m_requestId = 0;      //!< 单调递增，永不复用
    quint32 m_lastCompleted = 0;
    quint64 m_discarded = 0;
    quint64 m_searchCount = 0;
    int m_lastRaw = 0;            //!< 最近一次取数的原始匹配数（截断前）
    bool m_searching = false;
    QString m_lastQuery;
    QString m_emptyReason;
};

} // namespace stelapp
