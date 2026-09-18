// MainWindow.qml — A1 主窗口：承载诊断页。
// 禁区（docs/CODING_STANDARD.zh_CN.md 第 5 节）：本目录禁止 GL/Vulkan 特定调用。
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window

ApplicationWindow {
    id: root
    width: 720
    height: 480
    visible: true
    title: "Stellarium Quick UI — A1 诊断（后端校验）"
    color: "#f5f6f8"

    DiagnosticPage {
        anchors.fill: parent
        anchors.margins: 24
    }
}
