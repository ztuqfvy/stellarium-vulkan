// MainWindow.qml — 验证宿主主窗口：承载诊断页（A1）、天空视口页（A2）与搜索/信息页（T17）。
// 禁区（docs/CODING_STANDARD.zh_CN.md 第 5 节）：本目录禁止 GL/Vulkan 特定调用。
//
// T15：新增命令栏（暂停/继续 + 视场缩放）——全部经 ActionRouter 单点分发，
//   QML 不直接触碰引擎。
// T17：新增搜索/信息页；**并修复 T15 遗留的键盘挂载缺陷**（见下方 keySink 注释）。
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import StelQuickUI 1.0   // BackendInfo 单例；缺此 import 时引用处报 ReferenceError

ApplicationWindow {
    id: root
    width: 960
    height: 640
    visible: true
    title: "Stellarium Quick UI — A1/A2/A3 验证宿主"
    color: "#f5f6f8"

    // 由 C++ 上下文属性注入："diag"（诊断页）| "sky"（天空视口页）| "search"（搜索/信息页）
    property string startPage: "diag"

    // 页名 → StackLayout 索引（一处定义，切换器与 C++ 的 startPage 共用，
    // 避免"加了页面忘了改另一处"这类只在运行时才暴露的错位）。
    readonly property var pageIndex: ({ "diag": 0, "sky": 1, "search": 2 })

    // ── 键盘挂载点（T17 修复 T15 的缺陷）───────────────────────────────────────
    //
    // 背景：T15 把 `Keys.onPressed` 直接写在 **ApplicationWindow** 上，但 `Keys` 是
    // **Item 的附加属性**，而 ApplicationWindow 继承自 Window、不是 Item ——
    // 那条挂载**从未生效**，运行时每份日志里都会出现：
    //     Could not attach Keys property to: MainWindow_QMLTYPE  is not an Item
    // 后果是"键盘经 ActionRouter 单点路由"在 QML 侧其实是**死代码**：C++ 侧的
    // routeKey 判据照样全绿（它是直接调用），所以仪器测不到这个缺口。
    //
    // 修法：挂到一个**真正可聚焦**的 Item 上，并让它做全部内容的父节点 ——
    // 这样即使焦点在子控件（如搜索框）里，按键仍会沿父链冒泡到这里；
    // 是否该下发给天空快捷键由 ActionRouter::routeKey 内部的焦点守卫判定（U-ACT-03），
    // QML 侧不需要、也不该重复实现这套判断。
    Item {
        id: keySink
        objectName: "skyKeySink"   // AppFacadeCheck AC-12 按此名查找挂载点
        anchors.fill: parent
        focus: true

        Keys.onPressed: (event) => {
            if (ActionRouter.routeKey(event.key, event.modifiers))
                event.accepted = true
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            // 页头：A2 开发期的临时页切换器。A4 起由真实工具栏取代，届时删除本行。
            RowLayout {
                Layout.fillWidth: true
                Layout.margins: 6
                spacing: 6

                Button {
                    text: "诊断页（A1）"
                    onClicked: stack.currentIndex = 0
                }
                Button {
                    text: "天空视口（A2 静态图）"
                    onClicked: stack.currentIndex = 1
                }
                // T17：搜索 / 信息页（搜索 → 选择 → 信息页纵向链路）
                Button {
                    text: "搜索天体（T17）"
                    onClicked: stack.currentIndex = 2
                }
                Item { Layout.fillWidth: true }
                Label {
                    text: "渲染后端：" + BackendInfo.runtimeApiName
                    color: BackendInfo.backendOk ? "#2e7d32" : "#c62828"
                }
            }

            // T15 命令栏：QML 按钮 → ActionRouter.trigger → AppFacade → 引擎。
            // 每命令恰执行一次（路由器单点 + 幂等闸）；无引擎形态下点击安全 no-op。
            RowLayout {
                Layout.fillWidth: true
                Layout.margins: 6
                spacing: 6

                Button {
                    text: appFacade.simulationPaused ? "▶ 继续" : "⏸ 暂停"
                    onClicked: ActionRouter.trigger("app.togglePause")
                }
                Button {
                    text: "🔍＋（放大）"
                    onClicked: ActionRouter.trigger("app.zoomIn")
                }
                Button {
                    text: "🔍－（缩小）"
                    onClicked: ActionRouter.trigger("app.zoomOut")
                }
                Item { Layout.fillWidth: true }
                Label {
                    // 视场显示：fieldOfView 是 Q_PROPERTY（属性读取，不能加括号调用）；
                    // zoomTo 是 0.4s 动画，用轻量定时器轮询刷新。
                    id: fovLabel
                    text: "视场：" + (appFacade.fieldOfView > 0
                                       ? appFacade.fieldOfView.toFixed(2) + "°" : "—")
                    Timer {
                        interval: 500
                        running: true
                        repeat: true
                        onTriggered: fovLabel.text = "视场：" + (appFacade.fieldOfView > 0
                                                   ? appFacade.fieldOfView.toFixed(2) + "°" : "—")
                    }
                }
            }

            StackLayout {
                id: stack
                Layout.fillWidth: true
                Layout.fillHeight: true
                // 未知页名回落到诊断页（0），不静默停在错误的页上。
                currentIndex: root.pageIndex[root.startPage] !== undefined
                              ? root.pageIndex[root.startPage] : 0

                DiagnosticPage { }
                SkyTestPage { }
                SearchPage { }
            }
        }
    }
}
