// DiagnosticPage.qml — A1 诊断页。
// 显示：Qt Quick 运行时实际 API、Vulkan 设备/版本/驱动、窗口 DPR、
//       后端校验状态（绿色 PASS / 红色 FAIL，FAIL 必含退出提示）。
// 数据源：BackendInfo 单例（C++ 侧只读聚合，无 GL/Vulkan 句柄外露）。
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import StelQuickUI 1.0

Pane {
    id: page
    padding: 0

    // 状态色：PASS 绿、FAIL 红、未知黄
    readonly property color okColor: "#2e7d32"
    readonly property color failColor: "#c62828"
    readonly property color unknownColor: "#ef6c00"
    readonly property color statusColor: BackendInfo.runtimeApiName === "" ? unknownColor
                                        : (BackendInfo.backendOk ? okColor : failColor)

    ColumnLayout {
        anchors.fill: parent
        spacing: 16

        Label {
            text: "Qt Quick Vulkan 后端诊断"
            font.pixelSize: 22
            font.bold: true
            color: "#1a1c1e"
        }

        // 状态横幅
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 56
            radius: 8
            color: page.statusColor

            Label {
                anchors.centerIn: parent
                color: "white"
                font.pixelSize: 18
                font.bold: true
                text: {
                    if (BackendInfo.runtimeApiName === "")
                        return "校验中……（等待场景图初始化）"
                    if (BackendInfo.backendOk)
                        return "PASS：实际后端 " + BackendInfo.runtimeApiName
                    return "FAIL：请求 Vulkan，实际 " + BackendInfo.runtimeApiName + "（程序将退出）"
                }
            }
        }

        // 信息表
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: 8
            color: "white"
            border.color: "#e0e2e6"

            GridLayout {
                anchors.fill: parent
                anchors.margins: 20
                columns: 2
                columnSpacing: 24
                rowSpacing: 14

                Label { text: "实际渲染 API"; color: "#5a5e66"; font.pixelSize: 14 }
                Label { text: BackendInfo.runtimeApiName === "" ? "（初始化中）" : BackendInfo.runtimeApiName
                        font.pixelSize: 14; font.bold: true }

                Label { text: "物理设备"; color: "#5a5e66"; font.pixelSize: 14 }
                Label { text: BackendInfo.deviceName === "" ? "（探针无结果）" : BackendInfo.deviceName
                        font.pixelSize: 14 }

                Label { text: "Vulkan API 版本"; color: "#5a5e66"; font.pixelSize: 14 }
                Label { text: BackendInfo.vulkanVersion; font.pixelSize: 14 }

                Label { text: "驱动类型"; color: "#5a5e66"; font.pixelSize: 14 }
                Label {
                    // 跨平台注明：这一行是"驱动形态"这个自变量的显式记录。
                    // 对照实验（macOS MoltenVK vs Windows 原生驱动）必须同时记录本行，
                    // 否则无法把渲染差异归因到驱动形态上。
                    text: BackendInfo.probeError !== "" ? "（探针无结果）"
                          : (BackendInfo.portabilityDriver
                             ? "portability 转译层（MoltenVK）"
                             : "平台原生驱动（" + Qt.platform.os + "）")
                    font.pixelSize: 14
                    font.bold: true
                }

                Label { text: "驱动版本"; color: "#5a5e66"; font.pixelSize: 14 }
                Label { text: BackendInfo.driverVersion; font.pixelSize: 14 }

                Label { text: "屏幕"; color: "#5a5e66"; font.pixelSize: 14 }
                Label { text: root ? root.screen.name : ""
                        font.pixelSize: 14 }

                Label { text: "设备像素比 (DPR)"; color: "#5a5e66"; font.pixelSize: 14 }
                Label { text: root ? root.screen.devicePixelRatio : ""
                        font.pixelSize: 14 }

                Label { text: "窗口逻辑尺寸"; color: "#5a5e66"; font.pixelSize: 14 }
                Label { text: root ? root.width + " × " + root.height : ""
                        font.pixelSize: 14 }

                Item { Layout.fillHeight: true; Layout.columnSpan: 2 }

                Label {
                    visible: BackendInfo.probeError !== ""
                    Layout.columnSpan: 2
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    color: page.failColor
                    font.pixelSize: 13
                    text: "探针错误：" + BackendInfo.probeError
                }

                Label {
                    Layout.columnSpan: 2
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    color: "#8a8e96"
                    font.pixelSize: 12
                    text: (Qt.platform.os === "osx"
                           ? "说明：macOS 上 Vulkan 经 MoltenVK 转译到 Metal，属合规路径；"
                           : "说明：本平台 Vulkan 由显卡驱动原生提供，无转译层；")
                          + "Qt Quick 后端仍必须为 Vulkan（禁止以 Metal/D3D11 直接充当完成）。"
                          + "验收项：缩放、关闭、重开无错（自动测量 STELQUICK_WINDOW_TEST）。"
                }
            }
        }
    }
}
