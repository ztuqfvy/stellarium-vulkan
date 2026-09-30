/*
 * ShortcutModel — T40-B **快捷键编辑**的模型与命令面。
 *
 * 出处：A-1.0 范围表倒数第二格 `|快捷键编辑、帮助、版本与许可证页面 | 可延后 | 必须 |`
 * —— 本类 + `ShortcutsPage.qml` 交付第一列那个页面（帮助/版本归 T41）。
 * 第三列的约束「中文输入焦点不能触发天空快捷键」已由 T29 兜住（`keySink` 守卫 +
 * `ActionRouter::canDispatchToSky()`），本任务**不重复实现**。
 *
 * ── 口径全部来自 T40-A 探针（见 app/ShortcutProbe.hpp 与
 *    docs/evidence/2026-09-30-t40-shortcuts/mac/probe-shortcut-mac.txt，12 条）──────
 *  ① **`QKeySequence` 往返恒等 14/15** —— 唯一不等的 `PageUp` 是 **Qt 的键名别名**
 *     （正名 `PgUp`；`PageUp`/`PageDown` 解析成**空序列**，`Escape`→`Esc`）。
 *     ⇒ 🔴 **键名一律由 `keySequenceFromEvent(key, modifiers)` 生成**，QML 绝不自己拼
 *     —— 自己拼出来的串一旦不被 Qt 认识，`setShortcut` 会**静默变成"删除快捷键"**。
 *  ② **判"多键序列"只能用 `QKeySequence::count()`**：注册表里有 `actionShow_Ecliptic_Line`
 *     的键是 `Qt::Key_Comma`（`toString() = ","`）⇒ 用"串里有逗号"判会误计（探针首轮踩过）。
 *  ③ **`setShortcut()` 即时生效**（回读一致 / `findActionFromShortcut` 命中 /
 *     `routeKey` 立刻可路由 —— 探针真按了一次 F8 组合，`dispatchCount 0→1`），
 *     但**只发自己的 `changed()`、不发 `StelActionMgr::shortcutsChanged()`**
 *     ⇒ **下游要自己刷新**（本模型改键后主动 `refresh()`）。
 *  ④ **`saveShortcuts()` 落盘语义**：整本只有 `shortcuts` 组会变 —— 非 `shortcuts` 组
 *     **零差异**（探针 Q4 实测，这是"编辑快捷键不毁用户其它设置"的唯一证据）；
 *     落盘格式是 `"主键 备键"` 拼接串，**备键为空写 `""` 字面量**、**主键为空也写 `""`**
 *     （= 原版"允许删除快捷键"的实现）。
 *  ⑤ **空串 = 移除**：`setShortcut("")` → 落盘 `"" ""` → 重启时 `split` 出 2 段、
 *     都回解成空序列 ⇒ 主备键皆空。**合法操作，不是错误**。
 *  ⑥ **落盘会规范化字符串**（`Ctrl+E, Ctrl+2` → `Ctrl+E,Ctrl+2`，分隔空格被删）
 *     ⇒ 比"改没改成功"要比**序列语义**（`QKeySequence ==`），不能比字符串。
 *  ⑦ **`restoreDefaultShortcut(action)` 安全**：它内部虽走整组重写，但按"等于默认则跳过"
 *     剔除 ⇒ **不会冲掉别的动作的自定义键**（探针 Q7：2 项 → 1 项，另一个保留）。
 *     ⚠️ 探针**首轮**曾得出相反结论，那是"两个靶撞成同一动作"的仪器缺陷 —— 见陷阱。
 *  ⑧ **出厂注册表无重复键**（133 种非空键 / 0 重复）⇒ 冲突只可能由**用户改**出来，
 *     冲突检测是新写入的责任，不是出厂数据的问题。
 *  ⑨ `StelActionMgr::setAllActionsEnabled(false)` 的门**只挡 `pushKey`**
 *     （成对实测：门关 false + 状态冻结 / 门开 true + 状态动了），
 *     **`ActionRouter::routeKey` 不查它** ⇒ 不能拿它当"编辑期屏蔽动作"的完整方案。
 *     本模型的捕获态靠 **QML 侧吃键**（`Keys.onPressed` 里 `accepted = true`），
 *     不依赖它。
 * ⑩ 注册表规模：**505 动作 / 15 分组**；`getText()` 是**中文**（T24 翻译链路红利），
 *     但 `getGroup()` 是**英文** ⇒ 分组标题需产品侧映射（命中则中文，否则**回落英文原文**，
 *     不假装覆盖）。
 * ⑪ **是否被用户改过**的真源 = `conf->contains("shortcuts/"+id)`
 *     —— 与引擎构造函数（`StelActionMgr.cpp:59`）**同一个判据**，不用另立标准。
 *     （`defaultKeySequence` 是 `StelAction` 的私有成员，读不到"出厂默认键"，
 *     所以 UI 只显示**当前键** + 一个"恢复默认"动作，不显示"默认值"这一列。）
 * ⑫ 配置落点是 T36 的个人版目录 `.../Stellarium-quick/config.ini`（探针实测），
 *     **不碰原版目录**。
 *
 * ── 职责边界 ───────────────────────────────────────────────────────────────
 *   · 本类**持有**快捷键编辑域的全部逻辑（读表 / 改键 / 冲突 / 持久化）；
 *     `AppFacade` 不放这些（它不是"什么都塞"的门面）。
 *   · QML 只做展示与键事件采集；**任何格式化、校验、写入都回本类**。
 */
#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

namespace stelapp {

class ShortcutModel : public QAbstractListModel
{
    Q_OBJECT
public:
    //! 主键 / 备键（`setKey` 的 which 参数）。
    enum Slot { Primary = 0, Alternative = 1 };

    enum Roles {
        ActionIdRole = Qt::UserRole + 1,
        GroupKeyRole,
        GroupTitleRole,
        TitleRole,
        PrimaryRole,
        AltRole,
        CheckableRole,
        CustomizedRole,   //!< 被用户改过（= conf 里有 shortcuts/<id>，与引擎同判据）
        ConflictRole,     //!< 主/备键与别的动作撞车，或主备自撞
        ConflictWithRole, //!< 撞车对象（人读串，空 = 无）
    };

    explicit ShortcutModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // ── 过滤（空 = 全量）。分组键/中文分组名/动作名/主备键 都参与匹配 ──────────
    Q_PROPERTY(QString filter READ filter WRITE setFilter NOTIFY filterChanged)
    QString filter() const { return m_filter; }
    void setFilter(const QString &f);

    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(int totalCount READ totalCount NOTIFY countChanged)
    Q_PROPERTY(int conflictCount READ conflictCount NOTIFY conflictCountChanged)
    Q_PROPERTY(int customizedCount READ customizedCount NOTIFY countChanged)
    int count() const { return m_view.size(); }
    int totalCount() const { return m_all.size(); }
    int conflictCount() const { return m_conflictCount; }
    int customizedCount() const;

    //! 最近一次写入的反馈（空串 = 还没写过）。`statusOk` 决定配色。
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
    Q_PROPERTY(bool statusOk READ statusOk NOTIFY statusTextChanged)
    QString statusText() const { return m_statusText; }
    bool statusOk() const { return m_statusOk; }

    // ── 键名/校验的**单一真源**（探针①：QML 绝不自拼键名）──────────────────
    //! 由 QML 的 `event.key` / `event.modifiers` 生成键名；修饰键单独按下返回**空串**
    //! （调用方应视为"中间态，别提交"）。
    Q_INVOKABLE QString keySequenceFromEvent(int key, int modifiers) const;
    //! 该键是不是纯修饰键（Shift/Ctrl/Alt/Meta/AltGr/CapsLock/NumLock）。
    Q_INVOKABLE bool isModifierKey(int key) const;
    //! 合法键位：非空 + 能被 `QKeySequence` 解析出 ≥1 个组合 + 组合数 ≤ 4（QKeySequence 上限）。
    Q_INVOKABLE bool isValidKeySequence(const QString &seq) const;
    //! 归一化（`QKeySequence(seq).toString()`）；不可解析返回空串。
    Q_INVOKABLE QString normalizeKeySequence(const QString &seq) const;
    //! 分组英文键 → 中文标题（未收录则**回落英文原文**，不假装覆盖）。
    Q_INVOKABLE QString groupTitle(const QString &groupKey) const;

    // ── 命令 ───────────────────────────────────────────────────────────────
    //! 改一格键。@p row 是**过滤后**的行号；@p which 见 Slot。空串 = 移除该键。
    //! 成功 = 写了引擎 + `saveShortcuts()` + 重算冲突 + 刷新视图。
    Q_INVOKABLE bool setKey(int row, int which, const QString &seq);
    //! 恢复这一格到出厂默认（两种 slot 一起恢复 —— 引擎 API 就是按动作整体恢复的）。
    Q_INVOKABLE bool restoreDefault(int row);
    //! 全部动作恢复出厂默认。返回是否真的动了（用户没改过时是 no-op）。
    Q_INVOKABLE bool restoreAll();
    //! 从引擎重读全表（引导完成后 / 外部改动后）。
    Q_INVOKABLE void refresh();
    //! 该行当前是否被用户改过（UI 用它决定"恢复默认"按钮显不显示）。
    Q_INVOKABLE bool rowCustomized(int row) const;
    //! 该行的动作 id（判据定位用；objectName 用 index 拼会被过滤顺序影响）。
    Q_INVOKABLE QString rowActionId(int row) const;

    //! 引擎是否已就绪（注册表非空）。QML 用它显示空态提示。
    Q_PROPERTY(bool engineReady READ engineReady NOTIFY countChanged)
    bool engineReady() const { return !m_all.isEmpty(); }

    // ── 测试用（负控）────────────────────────────────────────────────────
    //! `STELQUICK_SHORTCUT_WRITE_OFF=1` ⇒ 只改模型不写引擎（期望判据红）。
    static bool writeDisabled();
    //! `STELQUICK_SHORTCUT_SAVE_OFF=1`  ⇒ 引擎改了但不落盘（期望判据红）。
    static bool saveDisabled();

signals:
    void filterChanged();
    void countChanged();
    void conflictCountChanged();
    void statusTextChanged();

private:
    struct Row
    {
        QString id;
        QString groupKey;
        QString groupTitle;
        int groupIndex = 0;   //!< 引擎 `getGroupList()` 的顺序（排序主键）
        QString title;
        QString primary;
        QString alt;
        bool checkable = false;
        bool customized = false;
        bool conflict = false;
        QString conflictWith;
    };

    void rebuildFromEngine();     //!< 拉数据 + 排序 + 算冲突（不含过滤）
    void rebuildView();           //!< 重算 `m_view`（不含模型 reset）
    bool rowInView(int row, int &allIndex) const;
    void setStatus(const QString &text, bool ok);
    void recomputeConflicts();

    QVector<Row> m_all;
    QVector<int> m_view;
    QString m_filter;
    int m_conflictCount = 0;
    QString m_statusText;
    bool m_statusOk = true;
};

} // namespace stelapp
