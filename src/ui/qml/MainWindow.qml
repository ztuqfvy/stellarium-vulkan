// MainWindow.qml — 验证宿主主窗口：承载诊断页（A1）与天空视口页（A2）。
// 禁区（docs/CODING_STANDARD.zh_CN.md 第 5 节）：本目录禁止 GL/Vulkan 特定调用。
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
