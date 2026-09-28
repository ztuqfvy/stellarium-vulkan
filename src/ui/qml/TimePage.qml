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
//   T22 起这个守卫还兼管时区的两个 CheckBox —— 它们的 `toggled` 在**程序赋值时
//   也会发**（与 SpinBox 的 valueChanged 同类性质），不挡就是同一个反馈环。
//
// ── T22（2026-09-28）：时间页收尾四项 ────────────────────────────────────────
//   把计划里挂着的「显示历法 / MJD / 速率 GUI / 时区选择器」补齐。三条纪律：
//   ① **照抄旧界面口径**，不发明：MJD = JD − 2400000.5（StelCore.cpp:1275）；
//      历法判定 = `jd < 2299161`（DateTimeDialog.cpp:251、StelUtils.cpp:848）；
//      时区取数/写入 = `QTimeZone` + `setCurrentTimeZone`（LocationDialog.cpp:490/292）；
//      速率换算 = `StelGuiItems.cpp:885-907` 的四档单位跳档。
//   ② **速率控制全部透传引擎既有动作**（`ActionRouter.trigger`），不新建速率表、
//      不直接写 `timeRate` —— 与"单点键位路由"同一条理由：另开一条写路径就会
//      与快捷键双轨，两边的状态迟早对不上。
//   ③ **回填一律轮询**（页尾 300 ms Timer），不建绑定。原因很硬：那批读接口是
//      `Q_INVOKABLE`，**不是属性** —— 写在绑定里读到的是**函数对象**（恒 false），
//      而且没有 NOTIFY 就没有依赖、永不重算。离散量本想用 Q_PROPERTY，但它们的
//      变化源在引擎（用户在旧界面/快捷键改时区、加速度），本类并不知情，
//      转发信号要额外接线；300 ms 轮询读的只是一个字符串，成本可忽略。
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

    //! T22：IANA 时区 id 列表。**只在完成时取一次** —— 这个列表有 600+ 项，
    //! 放进 300 ms 轮询会把组合框拖慢（C++ 侧也做了缓存，但 QML 侧不必反复要）。
    property var tzIds: []

    //! 找 id 的下标。不直接用 `tzIds.indexOf()`：QStringList 过 QML 边界后的
    //! JS 表示在不同 Qt 版本上未必是纯 Array，手写循环最稳（600 次比较，一次性）。
    function tzIndexOf(id) {
        for (var i = 0; i < tzIds.length; ++i)
            if (tzIds[i] === id)
                return i
        return -1
    }

    Component.onCompleted: {
        refill()
        timePage.tzIds = appFacade.availableTimeZoneIds()
    }

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

                // ── T22 时区选择器（照抄旧 LocationDialog 的口径）────────────────
                // 取数 = `QTimeZone::availableTimeZoneIds()`（LocationDialog.cpp:490），
                // 写入 = `core->setCurrentTimeZone()`。旧界面把「使用自定义时区」开关
                // 与列表**分开**（LocationDialog.cpp:187-191），这里照抄这个分工，
                // 只是在列表里选一下时**显式**再打开开关（否则"选了没生效"）。
                //
                // ⚠️ 刻意**不写 `checked: appFacade.useCustomTimeZone()` 这样的绑定**：
                //   那三个是 Q_INVOKABLE **不是属性**，绑定里读到的是**函数对象**
                //   （恒为 false），而且没有 NOTIFY 就没有依赖、永不重算。
                //   回填一律走页尾的 300 ms 轮询 + `syncing` 守卫挡反馈环。
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    CheckBox {
                        id: customTzCheck
                        objectName: "timeCustomTzCheck"
                        text: "使用自定义时区"
                        // 守卫：轮询回填时不许反过来再写一次引擎（与 SpinBox 同一手法）。
                        onToggled: { if (!timePage.syncing) appFacade.setUseCustomTimeZone(checked) }
                    }
                    CheckBox {
                        id: dstCheck
                        objectName: "timeDstCheck"
                        text: "夏令时"
                        onToggled: { if (!timePage.syncing) appFacade.setUseDST(checked) }
                    }
                    Item { Layout.fillWidth: true }
                }

                ComboBox {
                    id: tzCombo
                    objectName: "timeTimeZoneCombo"
                    Layout.fillWidth: true
                    model: timePage.tzIds
                    // 程序赋值 currentIndex **不会**发 activated（只有用户交互才发），
                    // 所以轮询回填不会误触发这里。
                    onActivated: (index) => {
                        appFacade.setTimeZoneId(timePage.tzIds[index])
                        appFacade.setUseCustomTimeZone(true)
                    }
                }

                Rectangle { Layout.fillWidth: true; height: 1; color: "#eeeeee" }

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
                    // ── T22 新增：MJD 与显示历法 ──────────────────────────────
                    // MJD = JD − 2400000.5（UT 尺度）。**只读**：写入路径仍是
                    // 左侧本地日历 ×6 与「现在」——多一个写入面就多一个口径，
                    // 而旧界面那个 MJD 自旋框（DateTimeDialog 的 spinner_mjd）
                    // 与 JD 自旋框是同一件事的两种表示，本页已有 JD 行可核对。
                    Label { text: "MJD（UT）"; color: "#757575" }
                    Label {
                        id: mjdLabel
                        objectName: "timeMjdLabel"
                        text: "—"
                        font.family: "monospace"
                    }
                    Label { text: "显示历法"; color: "#757575" }
                    Label {
                        id: calendarLabel
                        objectName: "timeCalendarLabel"
                        text: "—"
                        color: "#6a1b9a"
                    }
                    Label { text: "推进速率"; color: "#757575" }
                    Label {
                        id: rateLabel
                        objectName: "timeRateLabel"
                        text: "—"
                        font.family: "monospace"
                    }
                    Label { text: "时间方向"; color: "#757575" }
                    Label {
                        id: dirLabel
                        objectName: "timeDirectionLabel"
                        text: "—"
                    }
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

                RowLayout {
                    spacing: 4
                    // ── T22 速率控制：**全部透传引擎既有动作**（StelCore.cpp:314-321 注册），
                    //    与上面那排步进按钮同一纪律 —— 不新建速率表、不直接写 timeRate。
                    //    这样"按按钮"与"按快捷键"走的是同一条通路，QML 侧读到的
                    //    `appFacade.timeRateText()` 也必然跟着变（它是引擎投影）。
                    Label { text: "速率"; color: "#757575" }
                    Button {
                        objectName: "timeSpeedDownButton"
                        text: "−"
                        onClicked: ActionRouter.trigger("actionDecrease_Time_Speed")
                    }
                    Button {
                        objectName: "timeSpeedDownLessButton"
                        text: "−少"
                        onClicked: ActionRouter.trigger("actionDecrease_Time_Speed_Less")
                    }
                    Button {
                        objectName: "timeRealTimeSpeedButton"
                        text: "实时"
                        onClicked: ActionRouter.trigger("actionSet_Real_Time_Speed")
                    }
                    Button {
                        objectName: "timeZeroRateButton"
                        text: "零"
                        onClicked: ActionRouter.trigger("actionSet_Time_Rate_Zero")
                    }
                    Button {
                        objectName: "timeReverseButton"
                        text: "反向"
                        onClicked: ActionRouter.trigger("actionSet_Time_Reverse")
                    }
                    Button {
                        objectName: "timeSpeedUpLessButton"
                        text: "+少"
                        onClicked: ActionRouter.trigger("actionIncrease_Time_Speed_Less")
                    }
                    Button {
                        objectName: "timeSpeedUpButton"
                        text: "+"
                        onClicked: ActionRouter.trigger("actionIncrease_Time_Speed")
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

            // ── T22 新增读数（同样轮询，理由见头注）─────────────────────────
            mjdLabel.text = appFacade.modifiedJulianDay().toFixed(6)
            calendarLabel.text = appFacade.dateCalendarText()
            // 速率**必须**轮询：引擎的速率动作（L/J/K/7/9/0）不经过 AppFacade，
            // `timeRateChanged` 不会发。这个值现在是引擎 timeSpeed 的投影。
            rateLabel.text = appFacade.timeRateText()
            var dir = appFacade.timeDirection()
            dirLabel.text = dir === "forward" ? "前进"
                          : dir === "backward" ? "反向"
                          : dir === "stopped" ? "停（零速率）" : "—"
            dirLabel.color = dir === "backward" ? "#c62828"
                           : dir === "stopped" ? "#757575" : "#2e7d32"

            // ── T22 时区回填：走"命令 + 轮询"，不建绑定 ────────────────────
            // ① 回填期间开 syncing，挡掉 CheckBox 的 toggled 反馈环（否则
            //    "回填 → 又写一次引擎"，与 6 个 SpinBox 的问题同源）。
            timePage.syncing = true
            var ct = appFacade.useCustomTimeZone()
            if (customTzCheck.checked !== ct)
                customTzCheck.checked = ct
            var dst = appFacade.useDST()
            if (dstCheck.checked !== dst)
                dstCheck.checked = dst
            timePage.syncing = false
            // ② 组合框：程序赋值 currentIndex 不发 activated，安全。
            var idx = timePage.tzIndexOf(appFacade.timeZoneId())
            if (idx >= 0 && tzCombo.currentIndex !== idx)
                tzCombo.currentIndex = idx
        }
    }
}
