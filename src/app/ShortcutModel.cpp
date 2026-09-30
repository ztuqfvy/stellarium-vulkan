/*
 * ShortcutModel — 实现（T40-B）。设计依据与十二条探针口径见头注。
 */
#include "app/ShortcutModel.hpp"

#include <QCoreApplication>
#include <QKeySequence>
#include <QSettings>

#include <algorithm>

#if defined(STELQUICK_HAS_ENGINE)
#include "StelApp.hpp"
#include "StelActionMgr.hpp"
#endif

namespace stelapp {

namespace {

//! 引擎分组英文键 → 中文标题。**未收录一律回落英文原文**（不假装覆盖：
//! 上游加了新分组时，页面上会诚实地显示英文，而不是显示空或错的中文）。
const QHash<QString, QString> &groupTitleTable()
{
    static const QHash<QString, QString> table{
        {QStringLiteral("Miscellaneous"), QStringLiteral("杂项")},
        {QStringLiteral("Selected object information"), QStringLiteral("选中天体信息")},
        {QStringLiteral("Display Options"), QStringLiteral("显示选项")},
        {QStringLiteral("Movement and Selection"), QStringLiteral("移动与选择")},
        {QStringLiteral("Field of View"), QStringLiteral("视场")},
        {QStringLiteral("Date and Time"), QStringLiteral("日期与时间")},
        {QStringLiteral("Specific Time"), QStringLiteral("特定时间")},
        {QStringLiteral("Scripts"), QStringLiteral("脚本")},
        {QStringLiteral("Windows"), QStringLiteral("窗口")},
        {QStringLiteral("Exoplanets"), QStringLiteral("系外行星")},
        {QStringLiteral("Meteor Showers"), QStringLiteral("流星雨")},
        {QStringLiteral("Bright Novae"), QStringLiteral("亮新星")},
        {QStringLiteral("Oculars"), QStringLiteral("目镜")},
        {QStringLiteral("Satellites"), QStringLiteral("卫星")},
        {QStringLiteral("Solar System Editor"), QStringLiteral("太阳系编辑器")},
    };
    return table;
}

} // namespace

bool ShortcutModel::writeDisabled()
{
    return qEnvironmentVariableIsSet("STELQUICK_SHORTCUT_WRITE_OFF");
}

bool ShortcutModel::saveDisabled()
{
    return qEnvironmentVariableIsSet("STELQUICK_SHORTCUT_SAVE_OFF");
}

ShortcutModel::ShortcutModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int ShortcutModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_view.size();
}

QVariant ShortcutModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_view.size())
        return {};
    const Row &r = m_all.at(m_view.at(index.row()));
    switch (role)
    {
    case ActionIdRole:     return r.id;
    case GroupKeyRole:     return r.groupKey;
    case GroupTitleRole:   return r.groupTitle;
    case TitleRole:        return r.title;
    case PrimaryRole:      return r.primary;
    case AltRole:          return r.alt;
    case CheckableRole:    return r.checkable;
    case CustomizedRole:   return r.customized;
    case ConflictRole:     return r.conflict;
    case ConflictWithRole: return r.conflictWith;
    default:               return {};
    }
}

QHash<int, QByteArray> ShortcutModel::roleNames() const
{
    return {
        {ActionIdRole,     QByteArrayLiteral("actionId")},
        {GroupKeyRole,     QByteArrayLiteral("groupKey")},
        {GroupTitleRole,   QByteArrayLiteral("groupTitle")},
        {TitleRole,        QByteArrayLiteral("title")},
        {PrimaryRole,      QByteArrayLiteral("primaryKey")},
        {AltRole,          QByteArrayLiteral("altKey")},
        {CheckableRole,    QByteArrayLiteral("checkable")},
        {CustomizedRole,   QByteArrayLiteral("customized")},
        {ConflictRole,     QByteArrayLiteral("conflict")},
        {ConflictWithRole, QByteArrayLiteral("conflictWith")},
    };
}

// ── 过滤 ─────────────────────────────────────────────────────────────────────

void ShortcutModel::setFilter(const QString &f)
{
    if (m_filter == f)
        return;
    m_filter = f;
    emit filterChanged();
    beginResetModel();
    rebuildView();
    endResetModel();
    emit countChanged();
}

// ── 键名/校验（单一真源）────────────────────────────────────────────────────

QString ShortcutModel::keySequenceFromEvent(int key, int modifiers) const
{
    // 组 0（key<=0）与纯修饰键 ⇒ 空串 = "还是中间态，别提交"。
    if (key <= 0 || isModifierKey(key))
        return QString();
    return QKeySequence(modifiers | key).toString();
}

bool ShortcutModel::isModifierKey(int key) const
{
    switch (key)
    {
    case Qt::Key_Shift:
    case Qt::Key_Control:
    case Qt::Key_Alt:
    case Qt::Key_Meta:
    case Qt::Key_AltGr:
    case Qt::Key_CapsLock:
    case Qt::Key_NumLock:
    case Qt::Key_ScrollLock:
        return true;
    default:
        return false;
    }
}

bool ShortcutModel::isValidKeySequence(const QString &seq) const
{
    const QString t = seq.trimmed();
    if (t.isEmpty())
        return false;                       // 空串的语义是"移除"，不是"合法键位"
    const QKeySequence k(t);
    return k.count() >= 1 && k.count() <= 4;
}

QString ShortcutModel::normalizeKeySequence(const QString &seq) const
{
    return QKeySequence(seq).toString();
}

QString ShortcutModel::groupTitle(const QString &groupKey) const
{
    return groupTitleTable().value(groupKey, groupKey);
}

// ── 重建 ─────────────────────────────────────────────────────────────────────

void ShortcutModel::rebuildFromEngine()
{
    m_all.clear();
#if defined(STELQUICK_HAS_ENGINE)
    if (!StelApp::isInitialized())
        return;
    StelActionMgr *mgr = StelApp::getInstance().getStelActionManager();
    QSettings *conf = StelApp::getInstance().getSettings();
    const QStringList groups = mgr->getGroupList();
    for (int gi = 0; gi < groups.size(); ++gi)
    {
        const QString &gk = groups.at(gi);
        const QList<StelAction *> acts = mgr->getActionList(gk);
        for (StelAction *a : acts)
        {
            Row r;
            r.id = a->getId();
            r.groupKey = gk;
            r.groupTitle = groupTitleTable().value(gk, gk);
            r.groupIndex = gi;
            r.title = a->getText();
            r.primary = a->getShortcut().toString();
            r.alt = a->getAltShortcut().toString();
            r.checkable = a->isCheckable();
            // 口径⑪：与引擎构造（StelActionMgr.cpp:59）**同一个判据**。
            r.customized = conf->contains(QStringLiteral("shortcuts/") + r.id);
            m_all.append(r);
        }
    }
    // 组顺序保持引擎注册顺序；组内按标题排（中文标题用 localeAwareCompare）。
    std::stable_sort(m_all.begin(), m_all.end(), [](const Row &a, const Row &b) {
        if (a.groupIndex != b.groupIndex)
            return a.groupIndex < b.groupIndex;
        return a.title.localeAwareCompare(b.title) < 0;
    });
#endif
}

void ShortcutModel::recomputeConflicts()
{
    QHash<QString, QStringList> owners;   // 键串 → 动作 id（主备都算）
    for (const Row &r : m_all)
    {
        const QString keys[2] = {r.primary, r.alt};
        for (const QString &k : keys)
            if (!k.isEmpty())
                owners[k].append(r.id);
    }
    int n = 0;
    for (Row &r : m_all)
    {
        QStringList who;
        const QString keys[2] = {r.primary, r.alt};
        for (const QString &k : keys)
        {
            if (k.isEmpty())
                continue;
            const QStringList lst = owners.value(k);
            if (lst.size() > 1)
                for (const QString &id : lst)
                    if (id != r.id && !who.contains(id))
                        who << id;
        }
        // 主备自撞也算冲突（引擎只来得及触发主键，备键形同虚设）。
        if (!r.primary.isEmpty() && r.primary == r.alt)
            who << QStringLiteral("（主、备键相同）");
        r.conflict = !who.isEmpty();
        r.conflictWith = who.join(QStringLiteral(", "));
        if (r.conflict)
            ++n;
    }
    m_conflictCount = n;
}

void ShortcutModel::rebuildView()
{
    m_view.clear();
    const QString f = m_filter.trimmed();
    for (int i = 0; i < m_all.size(); ++i)
    {
        const Row &r = m_all.at(i);
        if (!f.isEmpty())
        {
            const bool hit =
                r.title.contains(f, Qt::CaseInsensitive)
                || r.groupTitle.contains(f, Qt::CaseInsensitive)
                || r.groupKey.contains(f, Qt::CaseInsensitive)
                || r.id.contains(f, Qt::CaseInsensitive)
                || r.primary.contains(f, Qt::CaseInsensitive)
                || r.alt.contains(f, Qt::CaseInsensitive);
            if (!hit)
                continue;
        }
        m_view.append(i);
    }
}

void ShortcutModel::refresh()
{
    beginResetModel();
    rebuildFromEngine();
    recomputeConflicts();
    rebuildView();
    endResetModel();
    emit countChanged();
    emit conflictCountChanged();
}

int ShortcutModel::customizedCount() const
{
    int n = 0;
    for (const Row &r : m_all)
        if (r.customized)
            ++n;
    return n;
}

bool ShortcutModel::rowInView(int row, int &allIndex) const
{
    if (row < 0 || row >= m_view.size())
        return false;
    allIndex = m_view.at(row);
    return true;
}

bool ShortcutModel::rowCustomized(int row) const
{
    int ai = -1;
    return rowInView(row, ai) && m_all.at(ai).customized;
}

QString ShortcutModel::rowActionId(int row) const
{
    int ai = -1;
    return rowInView(row, ai) ? m_all.at(ai).id : QString();
}

void ShortcutModel::setStatus(const QString &text, bool ok)
{
    if (m_statusText == text && m_statusOk == ok)
        return;
    m_statusText = text;
    m_statusOk = ok;
    emit statusTextChanged();
}

// ── 命令 ─────────────────────────────────────────────────────────────────────

#if !defined(STELQUICK_HAS_ENGINE)
bool ShortcutModel::setKey(int row, int which, const QString &seq)
{
    Q_UNUSED(row)
    Q_UNUSED(which)
    Q_UNUSED(seq)
    setStatus(QStringLiteral("非合流形态：没有可编辑的动作注册表"), false);
    return false;
}
bool ShortcutModel::restoreDefault(int row)
{
    Q_UNUSED(row)
    setStatus(QStringLiteral("非合流形态：没有可编辑的动作注册表"), false);
    return false;
}
bool ShortcutModel::restoreAll()
{
    setStatus(QStringLiteral("非合流形态：没有可编辑的动作注册表"), false);
    return false;
}
#else
bool ShortcutModel::setKey(int row, int which, const QString &seq)
{
    int ai = -1;
    if (!rowInView(row, ai))
    {
        setStatus(QStringLiteral("行号越界：%1（当前 %2 行）").arg(row).arg(m_view.size()), false);
        return false;
    }
    if (which != Primary && which != Alternative)
    {
        setStatus(QStringLiteral("槽位非法：%1").arg(which), false);
        return false;
    }
    if (!StelApp::isInitialized())
    {
        setStatus(QStringLiteral("引擎未就绪，暂时不能改键"), false);
        return false;
    }

    const Row snapshot = m_all.at(ai);
    const QString cur = (which == Primary) ? snapshot.primary : snapshot.alt;

    // 空输入 = 移除（口径⑤，原版就这么设计的，是合法操作不是错误）。
    QString norm;
    if (!seq.trimmed().isEmpty())
    {
        const QKeySequence parsed(seq);
        norm = parsed.toString();
        // 🔴 闸的**关键那一半是 `norm.isEmpty()`** —— T40-C 的 SC-10 抓到的真缺陷：
        //    `QKeySequence("PageUp").count()` 是 **1**（Qt 把它当"一个未知组合"存下了），
        //    但 `toString()` 是**空**。只查 count 就放行 ⇒ norm 变空串 ⇒ 引擎侧等于
        //    **静默删除该快捷键**（这正是探针①警告的形态，也是本判据存在的理由）。
        //    ⇒ "用户给了非空输入、解析后却是空序列" = 非法，必须拒绝。
        if (parsed.count() < 1 || parsed.count() > 4 || norm.isEmpty())
        {
            setStatus(QStringLiteral("无法识别的键位「%1」"
                                     "（要清空请按「清除」；键名由界面采集，别手打）")
                          .arg(seq),
                      false);
            return false;
        }
    }

    if (cur == norm)
    {
        setStatus(QStringLiteral("键位未变化"), true);
        return true;
    }

    StelActionMgr *mgr = StelApp::getInstance().getStelActionManager();
    StelAction *a = mgr->findAction(snapshot.id);
    if (!a)
    {
        setStatus(QStringLiteral("引擎里找不到动作 %1").arg(snapshot.id), false);
        return false;
    }

    // ⚠️ 负控 `STELQUICK_SHORTCUT_WRITE_OFF` **只作用于本函数**（改键的引擎写入腿）。
    //    刻意不覆盖 restoreDefault / restoreAll —— 那样负控 A 会连带打中"恢复默认"腿，
    //    红项集合与负控 B 混在一起，区分不出"哪个环节坏了"。
    if (!writeDisabled())
    {
        if (which == Primary)
            a->setShortcut(norm);
        else
            a->setAltShortcut(norm);
    }
    if (!saveDisabled())
        mgr->saveShortcuts();   // 口径④：只动 shortcuts 组，不碰别的

    refresh();
    setStatus(norm.isEmpty()
                  ? QStringLiteral("已移除「%1」的%2键")
                        .arg(snapshot.title,
                             which == Primary ? QStringLiteral("主") : QStringLiteral("备"))
                  : QStringLiteral("「%1」%2键 → %3")
                        .arg(snapshot.title,
                             which == Primary ? QStringLiteral("主") : QStringLiteral("备"),
                             norm),
              true);
    return true;
}

bool ShortcutModel::restoreDefault(int row)
{
    int ai = -1;
    if (!rowInView(row, ai))
    {
        setStatus(QStringLiteral("行号越界：%1").arg(row), false);
        return false;
    }
    if (!StelApp::isInitialized())
    {
        setStatus(QStringLiteral("引擎未就绪"), false);
        return false;
    }
    const Row snapshot = m_all.at(ai);
    StelActionMgr *mgr = StelApp::getInstance().getStelActionManager();
    StelAction *a = mgr->findAction(snapshot.id);
    if (!a)
    {
        setStatus(QStringLiteral("引擎里找不到动作 %1").arg(snapshot.id), false);
        return false;
    }
    mgr->restoreDefaultShortcut(a);   // 内含 saveShortcuts()；口径⑦：不冲别人的键;

    refresh();
    setStatus(QStringLiteral("「%1」已恢复出厂键位").arg(snapshot.title), true);
    return true;
}

bool ShortcutModel::restoreAll()
{
    if (!StelApp::isInitialized())
    {
        setStatus(QStringLiteral("引擎未就绪"), false);
        return false;
    }
    const int before = customizedCount();
    StelApp::getInstance().getStelActionManager()->restoreDefaultShortcuts();;

    refresh();
    setStatus(QStringLiteral("全部恢复出厂键位（此前有 %1 个动作被改过）").arg(before), true);
    return true;
}
#endif

} // namespace stelapp
