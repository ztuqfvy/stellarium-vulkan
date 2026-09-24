// MainWindow.qml — 验证宿主主窗口：承载诊断页（A1）与天空视口页（A2）。
// 禁区（docs/CODING_STANDARD.zh_CN.md 第 5 节）：本目录禁止 GL/Vulkan 特定调用。
//
// T15：新增命令栏（暂停/继续 + 视场缩放）——全部经 ActionRouter 单点分发，
//   QML 不直接触碰引擎；键盘经 Keys 转发 routeKey（焦点守卫在 C++ 侧）。
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
    title: "Stellarium Quick UI — A1/A2 验证宿主"
    color: "#f5f6f8"

    // 由 C++ 上下文属性注入："diag"（诊断页）| "sky"（天空视口页）
    property string startPage: "diag"

    // T15：键盘路由（U-ACT-01/03 入口）。焦点守卫（可编辑控件拒收）在
    // ActionRouter::canDispatchToSky 内实现；这里只负责转发与 accept。
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
            currentIndex: root.startPage === "sky" ? 1 : 0

            DiagnosticPage { }
            SkyTestPage { }
        }
    }
}
