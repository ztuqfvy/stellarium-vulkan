// TimePage.qml — T19：「改时间」环（A4 固定流程 I-REP-02 的开机→搜月球→定位→**改时间**→返回）。
//
// 与旧 `src/gui/DateTimeDialog.ui` 的对应关系（**照抄它的口径，不重新发明**）：
//   · 旧对话框 `dateTimeTab` 的 6 个自旋框（year/month/day/hour/minute/second）
//     ↔ 本页左侧 6 个 SpinBox —— 这正是计划里说的"改时间对话框 ×6"；
//   · 旧对话框 `julianDateTab` 的 JD/MJD ↔ 本页右侧的 JD 只读显示（MJD 可后补）；
//   · 旧对话框"改一个框就立刻 core->setJD"（makeValidAndApply）
//     ↔ 本页改成**显式「应用」**。理由是**可测**：自动应用没有干净的反向控制，
//       而"改了框但没点应用 → 时钟必须不动"恰好是 UI 端到端判据的负控靶点。
//
// 分工（与 T17/T18 一致）：
//   · 命令走 AppFacade（setLocalDateTime / setTimeNow）；本页**不碰引擎**；
//   · 本地/UT 双日历与偏移都是 AppFacade 的只读投影，QML 不做任何时区算术——
//     一旦在 QML 里再写一份 ±offset，就又多了一个会与引擎漂移的口径。
//
// 关于"为什么连续量要轮询而不是属性绑定"：
//   JD 每帧都在变。若把它做成带 NOTIFY 的 Q_PROPERTY，QML 每帧都要重建绑定。
//   所以本页用 300/600 ms 的两个轻量 Timer 拉取（与 MainWindow 的 fovLabel 同惯例）。
//
// **反馈环守卫（`syncing`）**：回填 6 个 SpinBox 会触发它们的 valueChanged，
//   若不挡就会"回填 → 触发 → 又写一次引擎"。旧对话框用 disconnectSpinnerEvents
//   解决，QML 侧没有 disconnect，等价手段就是这个布尔守卫。
//   没有它的话，`dirty` 会永远为真，用户改的框会被自己的回填吃掉。
//
// 禁区（docs/CODING_STANDARD.zh_CN.md 第 5 节）：本目录禁止 GL/Vulkan 特定调用。
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: timePage

    //! 正在把引擎值写进 SpinBox（挡住 valueChanged 的反馈环，见头注）。
    property bool syncing: false
    //! 用户改过框但还没点「应用」。为真时**暂停自动回填**——否则用户正在输入的
    //! 数字会被时钟每 400 ms 覆盖一次（旧对话框也有这个问题，它靠"值没变就不碰"
    //! 绕过；这里显式建模成 dirty，语义更清楚）。
    property bool dirty: false

    function refill() {
        syncing = true
        yearField.value   = appFacade.localDateTimeField(0)
        monthField.value  = appFacade.localDateTimeField(1)
        dayField.value    = appFacade.localDateTimeField(2)
        hourField.value   = appFacade.localDateTimeField(3)
        minuteField.value = appFacade.localDateTimeField(4)
        secondField.value = appFacade.localDateTimeField(5)
        syncing = false
        dirty = false
    }

    function applyFields() {
        appFacade.setLocalDateTime(yearField.value, monthField.value, dayField.value,
                                   hourField.value, minuteField.value, secondField.value)
        dirty = false
    }

    Component.onCompleted: refill()

    // 自动回填：时钟在走，本地日历每秒都在变。dirty 时让位给用户。
    Timer {
        interval: 400
        running: true
        repeat: true
        onTriggered: { if (!timePage.dirty) timePage.refill() }
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 10

        // ── 左列：本地日历 6 个写入路径（"×6"）+ 应用/重置/现在 ────────────
        Rectangle {
            Layout.preferredWidth: 430
            Layout.fillHeight: true
            color: "#ffffff"
            border.color: "#e0e0e0"

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 8

                Label {
                    text: "本地日历（时区 UTC" + (timePage.offsetHours >= 0 ? "+" : "")
                          + timePage.offsetHours.toFixed(1) + "）"
                    font.bold: true
                }

                GridLayout {
                    columns: 4
                    columnSpacing: 6
                    rowSpacing: 6

                    Label { text: "年"; color: "#757575" }
                    SpinBox {
                        id: yearField
                        objectName: "timeYearField"
                        from: 1; to: 9999; editable: true
                        onValueChanged: { if (!timePage.syncing) timePage.dirty = true }
                    }
                    Label { text: "月"; color: "#757575" }
                    SpinBox {
                        id: monthField
                        objectName: "timeMonthField"
                        from: 1; to: 12; editable: true
                        onValueChanged: { if (!timePage.syncing) timePage.dirty = true }
                    }

                    Label { text: "日"; color: "#757575" }
                    SpinBox {
                        id: dayField
                        objectName: "timeDayField"
                        from: 1; to: 31; editable: true
                        onValueChanged: { if (!timePage.syncing) timePage.dirty = true }
                    }
                    Label { text: "时"; color: "#757575" }
                    SpinBox {
                        id: hourField
                        objectName: "timeHourField"
                        from: 0; to: 23; editable: true
                        onValueChanged: { if (!timePage.syncing) timePage.dirty = true }
                    }

                    Label { text: "分"; color: "#757575" }
                    SpinBox {
                        id: minuteField
                        objectName: "timeMinuteField"
                        from: 0; to: 59; editable: true
                        onValueChanged: { if (!timePage.syncing) timePage.dirty = true }
                    }
                    Label { text: "秒"; color: "#757575" }
                    SpinBox {
                        id: secondField
                        objectName: "timeSecondField"
                        from: 0; to: 59; editable: true
                        onValueChanged: { if (!timePage.syncing) timePage.dirty = true }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6

                    Button {
                        objectName: "timeApplyButton"
                        text: "应用"
                        onClicked: timePage.applyFields()
                    }
                    Button {
                        objectName: "timeResetButton"
                        // 只把框回填成引擎当前值，**不写时钟** —— UI 判据的负控靶点。
                        text: "重置"
                        onClicked: timePage.refill()
                    }
                    Button {
                        objectName: "timeNowButton"
                        text: "现在"
                        onClicked: appFacade.setTimeNow()
                    }
                    Item { Layout.fillWidth: true }
                    Label {
                        visible: timePage.dirty
                        text: "有未应用的改动"
                        color: "#ef6c00"
                    }
                }

                // 状态行：把稳定 token 翻成人话（判定永远看 token，不看文案）。
                Label {
                    objectName: "timeStatusLabel"
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    font.pixelSize: 11
                    color: appFacade.lastTimeRefusal === "ok" ? "#2e7d32" : "#c62828"
                    text: appFacade.lastTimeRefusal === "ok"
                          ? "时间已按写入生效。"
                          : appFacade.timeRefusalText()
                }

                Rectangle { Layout.fillWidth: true; height: 1; color: "#eeeeee" }

                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    font.pixelSize: 10
                    color: "#9e9e9e"
                    text: "说明：写入按**本地时区**解释（与旧日期时间对话框一致），"
                          + "内部换算为 UT 后交给引擎的单一跳转入口；"
                          + "星期/历法与闰年规则由引擎提供，本页不做任何日历运算。"
                }
            }
        }

        // ── 右列：UT 对照 + JD + 步进（走 ActionRouter，引擎既有动作）──────
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "#ffffff"
            border.color: "#e0e0e0"

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 8

                Label { text: "引擎时钟（唯一真源）"; font.bold: true }

                GridLayout {
                    columns: 2
                    columnSpacing: 10
                    rowSpacing: 4

                    Label { text: "本地（时区）"; color: "#757575" }
                    Label {
                        id: localLabel
                        text: timePage.localText
                        font.family: "monospace"
                    }
                    Label { text: "UT（世界时）"; color: "#757575" }
                    Label {
                        id: utcLabel
                        objectName: "timeUtcLabel"
                        text: timePage.utcText
                        font.family: "monospace"
                        color: "#1565c0"
                    }
                    Label { text: "JD（UT）"; color: "#757575" }
                    Label {
                        id: jdLabel
                        objectName: "timeJdLabel"
                        text: timePage.jdText
                        font.family: "monospace"
                    }
                    Label { text: "推进速率"; color: "#757575" }
                    Label { text: appFacade.simulationPaused ? "已暂停" : "运行中"; }
                }

                Rectangle { Layout.fillWidth: true; height: 1; color: "#eeeeee" }

                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    font.pixelSize: 11
                    color: "#616161"
                    text: "步进按钮**不新建键位表**：直接透传引擎既有的时间动作"
                          + "（ActionRouter 的 StelAction 通路），与旧界面的快捷键同源。"
                }

                RowLayout {
                    spacing: 6
                    // objectName：给最外层注入的 UI 判据当锚点（T19 的 UI-10）。
                    // 这四个动作 id 是引擎既有的（StelCore.cpp:324-329 注册），
                    // 本页只做透传；判据要能真的点到按钮，否则"透传链是否活着"
                    // 又是一条只能在人工点击时才暴露的接线。
                    Button {
                        objectName: "timeSubDayButton"
                        text: "−1 天"
                        onClicked: ActionRouter.trigger("actionSubtract_Solar_Day")
                    }
                    Button {
                        objectName: "timeAddDayButton"
                        text: "+1 天"
                        onClicked: ActionRouter.trigger("actionAdd_Solar_Day")
                    }
                    Button {
                        objectName: "timeSubHourButton"
                        text: "−1 时"
                        onClicked: ActionRouter.trigger("actionSubtract_Solar_Hour")
                    }
                    Button {
                        objectName: "timeAddHourButton"
                        text: "+1 时"
                        onClicked: ActionRouter.trigger("actionAdd_Solar_Hour")
                    }
                }

                Item { Layout.fillHeight: true }
            }
        }
    }

    // ── 只读投影（轮询，见头注）────────────────────────────────────────────
    property real offsetHours: 0
    property string localText: "—"
    property string utcText: "—"
    property string jdText: "—"

    Timer {
        interval: 300
        running: true
        repeat: true
        onTriggered: {
            timePage.offsetHours = appFacade.utcOffsetHours()
            timePage.localText = appFacade.localDateTimeText()
            timePage.utcText = appFacade.utcDateTimeText()
            timePage.jdText = appFacade.julianDay().toFixed(6)
        }
    }
}
