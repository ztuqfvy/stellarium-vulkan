/*
 * SearchResultsModel — 本地天体搜索结果增量列表模型（骨架，未实现）。
 *
 * 职责：包装 StelObjectMgr::listMatchingObjects 的异步结果，带请求编号。
 * 禁止：过期搜索结果不能覆盖新查询；不向 QML 传悬空裸指针（使用稳定对象标识）。
 *
 * @see StelObjectMgr.hpp:78-105 findAndSelect / listMatchingObjects
 * 测试用例：U-SRC-01..04（软件测试文档 3.3）。实现顺序：A3。
 */
#pragma once

#include <QAbstractListModel>

namespace stelapp {

class SearchResultsModel : public QAbstractListModel
{
    Q_OBJECT
    // 搜索中状态，驱动 QML 空态/加载态。
    Q_PROPERTY(bool searching READ searching NOTIFY searchingChanged)
public:
    explicit SearchResultsModel(QObject *parent = nullptr);

    // 角色定稿在实现时补充：显示名、类型、稳定标识（stableId）、赤经赤纬等。

    // THREAD: gui
    // 发起搜索并递增请求编号；返回 requestId。
    // quint32 search(const QString &query);

    // THREAD: gui
    // 结果到达时校验 requestId：过期结果直接丢弃（不覆盖新查询）。
    // void onResultsReady(quint32 requestId, /* 结果列表 */);

    bool searching() const;

signals:
    void searchingChanged(bool searching);

private:
    bool m_searching = false;
    quint32 m_requestId = 0;     // 请求编号，防过期结果覆盖
    quint32 m_lastCompleted = 0;
};

} // namespace stelapp
