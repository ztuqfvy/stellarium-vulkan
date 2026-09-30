// Toolbar.qml — T34：真实工具栏（A4 的最后一项）。
//
// 组成（两段式）：
//   第一行：导航区（返回天空 + 4 个工具页按钮，objectName **保持不变** ——
//          RETURNUICHECK / INTERACTCHECK / AppFacadeCheck 的"最外层注入"判据都锚着它们）
//          + 右侧渲染后端标签。
//   第二行：12 个显示开关（Flow，吃满窗口宽度，窄了自动换行）。
//
// ── 命令路径（刻意单点）────────────────────────────────────────────────────
// 开关点击 → `ActionRouter.trigger(id)` → 注册表未命中 → **引擎透传**
// （findAction → StelAction::trigger）。QML **不复刻**任何开关语义；
// 键盘快捷键（C/V/R/E/Z/G/Q/A/D/O/R…）走 routeKey 也落在同一个 StelAction 上
// ⇒ 按钮态、引擎态、快捷键态天然只有一份真源。
//
// ── checked 绑定为什么长这样（T15 铁律）──────────────────────────────────
//   `engineOn: { appFacade.displayTogglesRevision;            // ← 必须真的读 token
//                return appFacade.actionChecked(modelData.actionId) }`
// QML 绑定只登记"绑定表达式里实际读过的属性"。`actionChecked()` 是 Q_INVOKABLE
// 方法、不是属性——引擎翻转后 QML 不会自动重算；revision 是 Q_PROPERTY+NOTIFY，
// 引擎每翻转一次 +1。**只调方法不读 token** 的绑定第一次求值后就再也不重算
// （血泪：不是"晚点重算"，是"永远没有重算的时机"）。
//
// ── 🔴 布局两坑（T34 实测，都是"判据全绿但界面是坏的"）───────────────────
// ① 根是裸 `Rectangle` ⇒ `implicitHeight = 0` ⇒ ColumnLayout 给的高度是 0，
//    整条工具栏布局全乱（RowLayout 高度 -12、开关溢出窗口），而 Rectangle
//    **默认不裁剪** ⇒ 按钮照样画出来、可见性判据全绿——假绿掩护。
//    ⇒ 修法：`implicitHeight: col.implicitHeight + 12`。
// ② `Flow` 不能直接当 RowLayout 的 fillWidth 子项：Flow 的 implicitWidth 是
//    **单行排完**的宽度（实测 1263 > 窗口 948）⇒ 行布局整体溢出。而且若同时
//    存在第二个 fillWidth 项（撑开的 filler），两者对分剩余宽度 ⇒ 开关区只剩
//    159px、竖成 12 行、implicitHeight 回喂把工具栏撑到 446px（占窗口 70%）。
//    ⇒ 修法：开关区的 Flow 独占一行、吃满宽度；导航行里只留一个 filler。
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import StelQuickUI 1.0

Rectangle {
    id: toolbar
    color: "#ffffff"
    // 见文件头坑①：高度必须显式回喂，否则 ColumnLayout 给 0。
    // tbLayoutBreak = TOOLBARCHECK 的负控开关③（把高度打回 0，复现"假绿掩护"
    // 缺陷形态，证"落点覆盖"自证承重；正常跑恒 false）。
    implicitHeight: BackendInfo.tbLayoutBreak ? 0 : col.implicitHeight + 12  // 上下 margins 6×2

    // 页名 → StackLayout 索引（由 MainWindow 注入，单一真源在 MainWindow.pageIndex）
    property var pageIndex
    // 当前 StackLayout 索引（高亮当前页按钮用；MainWindow 绑 stack.currentIndex）
    property int currentPageIndex: -1

    signal navigate(string page)
    signal returnToSkyRequested()

    // ── T34 显示开关清单（照桌面底栏）─────────────────────────────────────
    // actionId = 引擎 action ID（**一字不差**——findAction 精确匹配，写错就是"点了没反应"）。
    // 前四个（连线/网格/地面/夜间）是 TOOLBARCHECK 写入腿的代表样本。
    readonly property var toggles: [
        { actionId: "actionShow_Constellation_Lines", label: "连线" },
        { actionId: "actionShow_Constellation_Labels", label: "名称" },
        { actionId: "actionShow_Constellation_Art", label: "插图" },
        { actionId: "actionShow_Equatorial_Grid", label: "赤道网格" },
        { actionId: "actionShow_Azimuthal_Grid", label: "地平网格" },
        { actionId: "actionShow_Ground", label: "地面" },
        { actionId: "actionShow_Cardinal_Points", label: "方位点" },
        { actionId: "actionShow_Atmosphere", label: "大气" },
        { actionId: "actionShow_Nebulas", label: "深空天体" },
        { actionId: "actionShow_Planets_Labels", label: "行星标签" },
        { actionId: "actionShow_Planets_Orbits", label: "行星轨道" },
        { actionId: "actionShow_Night_Mode", label: "夜间模式" }
    ]

    ColumnLayout {
        id: col
        anchors.fill: parent
        anchors.margins: 6
        spacing: 4

        // ── 第一行：导航区 + 右侧信息 ────────────────────────────────────────
        RowLayout {
            id: navRow
            Layout.fillWidth: true
            spacing: 6

            // T20「返回」：**始终可用**（在天空页点它必须什么都不发生——那是
            //   "可用但应无效果"的负控，绑 enabled 就把它退化成"点禁用按钮"了）。
            Button {
                objectName: "skyReturnButton"
                text: "🏠 返回天空"
                onClicked: toolbar.returnToSkyRequested()
            }
            Rectangle { width: 1; Layout.fillHeight: true; color: "#d0d0d0" }

            Button {
                objectName: "navDiagButton"
                text: "诊断"
                onClicked: toolbar.navigate("diag")
            }
            Button {
                objectName: "navSkyButton"
                text: "天空"
                onClicked: toolbar.navigate("sky")
            }
            Button {
                objectName: "navSearchButton"
                text: "搜索"
                onClicked: toolbar.navigate("search")
            }
            Button {
                objectName: "navTimeButton"
                text: "时间"
                onClicked: toolbar.navigate("time")
            }
            Button {
                objectName: "navLocationButton"
                text: "地点"
                onClicked: toolbar.navigate("location")
            }
            // T38：显示参数页（亮度/星等 · 视场 · 投影）。
            // ⚠️ 上面 6 个导航按钮的 objectName **一个都没动** ——
            //   RETURNUICHECK / INTERACTCHECK / AppFacadeCheck 的注入判据锚着它们。
            Button {
                objectName: "navDisplayButton"
                text: "显示"
                onClicked: toolbar.navigate("display")
            }

            // 唯一 filler：把右侧信息顶到最右（见文件头坑②，别再加第二个）。
            Item { Layout.fillWidth: true }

            Label {
                text: "渲染后端：" + BackendInfo.runtimeApiName
                color: BackendInfo.backendOk ? "#2e7d32" : "#c62828"
            }
        }

        // ── 第二行：显示开关（Flow 吃满宽度，窄了换行）────────────────────────
        // ⚠️ 刻意**不用** `checkable: true`：AbstractButton 在点击路径里会
        // **命令式写 checked**（nextCheckState → setChecked）——而 QML 语义里
        // 对带绑定的属性赋值会**销毁绑定**（T32 在 SpinBox contentItem.text 上
        // 踩过同族坑）⇒ 第一次点击后按钮态就再也不跟引擎了。
        // 规避：普通按钮，引擎态由我们自己的属性（engineOn）驱动视觉 ——
        // "谁写 checked"这条路径不存在，绑定就不会被弄坏。
        Flow {
            id: toggleFlow
            objectName: "toolbarToggleFlow"
            Layout.fillWidth: true
            spacing: 4

            Repeater {
                id: toggleRepeater
                objectName: "toolbarToggleRepeater"
                model: toolbar.toggles

                delegate: Button {
                    id: toggleDelegate

                    required property var modelData

                    // 引擎开关态（只读投影）。⚠️ 绑定里必须**真的读** revision
                    // （文件头 T15 铁律注释：只调 actionChecked() 不读 token，
                    // 引擎翻转后绑定永远不重算——不是"晚点重算"）。
                    // tbTokenBindingOff = TOOLBARCHECK 的负控开关（制造"没读 token"
                    // 的缺陷形态，证 TB-12 承重；正常跑恒 false）。
                    property bool engineOn: {
                        if (BackendInfo.tbTokenBindingOff)
                            return appFacade.actionChecked(modelData.actionId)
                        appFacade.displayTogglesRevision
                        return appFacade.actionChecked(modelData.actionId)
                    }

                    objectName: "toolToggle_" + modelData.actionId
                    text: (engineOn ? "● " : "○ ") + modelData.label
                    // 状态用**字重 + 前缀符号**表达。刻意不覆盖 background：
                    // 本机 QML 走 native（macOS）style，控件定制被拒
                    //（"The current style does not support customization"，
                    //  实测 12 个按钮各刷一条告警且背景覆盖**静默失效**）。
                    font.bold: engineOn
                    // tbClickOff = TOOLBARCHECK 的负控开关②（制造"点击没接上"的
                    // 缺陷形态，证 TB-10 承重；正常跑恒 false）。
                    onClicked: {
                        if (!BackendInfo.tbClickOff)
                            ActionRouter.trigger(modelData.actionId)
                    }
                }
            }
        }
    }
}
