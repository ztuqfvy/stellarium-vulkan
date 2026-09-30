// ShortcutsPage.qml — T40-B：快捷键编辑页。
//
// A-1.0 范围表倒数第二格：`|快捷键编辑、帮助、版本与许可证页面 | 可延后 | 必须 |
// 中文输入焦点不能触发天空快捷键|`。第三列那条约束**已由 T29 兜住**（`keySink` 的
// Esc 分支先问 `ActionRouter::canDispatchToSky()`），本页只交付第一列的页面本体。
//
// ── 口径全部来自 T40-A 探针（见 app/ShortcutModel.hpp 头注的十二条）──────────
// 本页自己的三条硬规矩：
//  ① 🔴 **绝不自己拼键名**。捕获到按键后只把 `event.key` / `event.modifiers` 交给
//     `ShortcutModel.keySequenceFromEvent()`，由 C++ 侧 `QKeySequence(modifiers|key)
//     .toString()` 生成。理由（探针①）：Qt 的键名有别名陷阱 —— `PageUp`/`PageDown`
//     解析成**空序列**（正名 `PgUp`/`PgDown`），而空串在引擎里等于**"移除快捷键"**。
//     自己拼 = 用户按了没反应 / 键被意外删掉，且**不报错**。
//  ② **捕获态必须吃键**（`event.accepted = true`）。否则事件会冒泡到 MainWindow 的
//     `keySink`：那儿对非 Esc 键会调 `ActionRouter.routeKey` —— 用户想绑 `C`，结果
//     顺手把"星座连线"开关了。吃键是本页唯一的防串扰手段（探针⑨：引擎的
//     `setAllActionsEnabled` 门**挡不住 routeKey**，不能当完整方案用）。
//  ③ **Esc 在捕获态 = 取消**（不写值），在非捕获态 = T20 的"返回天空"（不动它）。
//
// 分工（T19/T33/T38 同款）：本页不碰引擎、不做任何键位解析；读表/改键/冲突/落盘
// 全在 `ShortcutModel`（C++）。本页只做展示 + 键事件采集。
//
// 禁区（docs/CODING_STANDARD.zh_CN.md 第 5 节）：本目录禁止 GL/Vulkan 特定调用。
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: shortcutsPage
    objectName: "shortcutsPage"

    // ── 捕获态（哪一行的哪个槽在等键）──────────────────────────────────────
    property int captureRow: -1
    property int captureSlot: -1
    //! 进捕获前谁持有焦点 —— 取消/完成后还回去（不还的话键盘会停在 1×1 的捕获器上，
    //! 用户以为界面卡死了）。
    property var previousFocus: null

    readonly property string captureHint: "按键…（Esc 取消）"

    function startCapture(row, slot) {
        previousFocus = Window.window ? Window.window.activeFocusItem : null
        captureRow = row
        captureSlot = slot
        keyCatcher.forceActiveFocus()
    }

    function endCapture() {
        captureRow = -1
        captureSlot = -1
        var f = previousFocus
        previousFocus = null
        if (f) {
            try { f.forceActiveFocus() } catch (e) { /* 对象已被销毁 */ }
        }
    }

    function isCapturing(row, slot) {
        return captureRow === row && captureSlot === slot
    }

    // ── 按键捕获器 ─────────────────────────────────────────────────────────
    // 刻意做成 1×1：既能持焦点收键，又不遮挡任何点击（visible:false 的项收不到焦点，
    // 所以不能用 visible 藏）。`Keys.enabled` 控制是否吃键 —— 不在捕获态一律放行。
    Item {
        id: keyCatcher
        objectName: "shortcutKeyCatcher"
        width: 1
        height: 1
        anchors.left: parent.left
        anchors.top: parent.top
        Keys.enabled: shortcutsPage.captureRow >= 0
        Keys.onPressed: (event) => {
            if (shortcutsPage.captureRow < 0)
                return          // 非捕获态：放行给 keySink（Esc 返回环、routeKey 照旧）
            event.accepted = true
            if (event.key === Qt.Key_Escape) {
                shortcutsPage.endCapture()     // 规矩③：Esc = 取消本次录入
                return
            }
            // 规矩①：只交原始 key/modifiers，键名由 C++ 生成。
            const seq = ShortcutModel.keySequenceFromEvent(event.key, event.modifiers)
            if (seq === "")
                return          // 纯修饰键（Shift/Ctrl/Alt/⌘）⇒ 中间态，继续等真正的键
            const r = shortcutsPage.captureRow
            const s = shortcutsPage.captureSlot
            shortcutsPage.endCapture()
            ShortcutModel.setKey(r, s, seq)
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 6

        Label {
            text: "快捷键编辑"
            font.bold: true
            font.pixelSize: 16
            Layout.margins: 8
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 8
            Layout.rightMargin: 8
            spacing: 8

            TextField {
                id: filterField
                objectName: "shortcutFilterField"
                Layout.preferredWidth: 260
                placeholderText: "搜索：动作名 / 分组 / 键位"
                // ⚠️ 本框获得焦点时打字不会触发天空快捷键 —— 由 ActionRouter 的
                //   焦点守卫（canDispatchToSky）在 C++ 侧单点判定，本页不复刻（T17 血泪）。
                onTextChanged: ShortcutModel.filter = text
            }

            Label {
                objectName: "shortcutStatsLabel"
                text: "共 " + ShortcutModel.totalCount + " 个动作"
                      + "｜显示 " + ShortcutModel.count
                      + "｜冲突 " + ShortcutModel.conflictCount
                      + "｜已改 " + ShortcutModel.customizedCount
            }

            Item { Layout.fillWidth: true }

            Button {
                objectName: "shortcutRestoreAllButton"
                text: "全部恢复出厂"
                enabled: ShortcutModel.customizedCount > 0
                onClicked: ShortcutModel.restoreAll()
            }
        }

        Label {
            objectName: "shortcutStatusLabel"
            Layout.leftMargin: 8
            text: ShortcutModel.statusText === "" ? "　" : ShortcutModel.statusText
            color: ShortcutModel.statusOk ? "#2e7d32" : "#c62828"
        }

        // 空态：引擎是**延迟引导**的（A3），本页可能在动作注册表就绪前就被实例化。
        Label {
            objectName: "shortcutEmptyLabel"
            visible: !ShortcutModel.engineReady
            Layout.leftMargin: 8
            color: "#757575"
            text: "动作注册表尚未就绪（引擎仍在引导）…"
        }

        ListView {
            id: list
            objectName: "shortcutList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: ShortcutModel
            // 分组表头。按 groupKey 分节（= 引擎注册顺序，稳定）；标题用中文映射，
            // 未收录的分组**回落英文原文**（ShortcutModel.groupTitle）。
            section.property: "groupKey"
            section.criteria: ViewSection.FullString
            section.delegate: Rectangle {
                required property string section
                width: list.width
                height: 26
                color: "#e8eef5"
                Label {
                    anchors.left: parent.left
                    anchors.leftMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    text: ShortcutModel.groupTitle(section)
                    font.bold: true
                }
            }

            delegate: Rectangle {
                id: rowRect
                required property int index
                required property string actionId
                required property string title
                required property string primaryKey
                required property string altKey
                required property bool conflict
                required property string conflictWith
                required property bool customized

                width: list.width
                height: 38
                color: conflict ? "#ffe9e9" : (index % 2 === 0 ? "#ffffff" : "#fafafa")
                // 行身份给判据用（objectName 用 index 拼会被过滤顺序影响，所以另放属性）。
                property string rowActionId: actionId

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    anchors.rightMargin: 10
                    spacing: 8

                    Label {
                        Layout.fillWidth: true
                        text: rowRect.title
                        elide: Text.ElideRight
                        color: rowRect.conflict ? "#b71c1c" : "#212121"
                    }

                    Label {
                        visible: rowRect.conflict
                        text: "冲突：" + rowRect.conflictWith
                        color: "#c62828"
                        elide: Text.ElideRight
                        Layout.maximumWidth: 240
                    }

                    Button {
                        objectName: "shortcutPrimary_" + rowRect.index
                        text: shortcutsPage.isCapturing(rowRect.index, 0)
                              ? shortcutsPage.captureHint
                              : (rowRect.primaryKey === "" ? "（空）" : rowRect.primaryKey)
                        onClicked: shortcutsPage.startCapture(rowRect.index, 0)
                    }

                    Button {
                        objectName: "shortcutAlt_" + rowRect.index
                        text: shortcutsPage.isCapturing(rowRect.index, 1)
                              ? shortcutsPage.captureHint
                              : (rowRect.altKey === "" ? "（空）" : rowRect.altKey)
                        onClicked: shortcutsPage.startCapture(rowRect.index, 1)
                    }

                    Button {
                        objectName: "shortcutRestore_" + rowRect.index
                        text: "恢复默认"
                        enabled: rowRect.customized
                        onClicked: ShortcutModel.restoreDefault(rowRect.index)
                    }
                }
            }
        }
    }
}
