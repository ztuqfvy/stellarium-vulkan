// ErrorPage.qml — T42-B：**状态与错误**页。
//
// 这一页同时是三条文档要求的落点（详见 ErrorModel.hpp 头注）：
//   ① A-1.0 第二格 `|资源路径、错误提示、… | 最小 | 必须|` 的**资源路径 + 错误提示**
//      （另两项：配置保存 = T36、渲染诊断 = T39）；
//   ② A5「错误页」；
//   ③ 开发指导「未支持项有清单和明确提示 / 旧 UI 禁止隐式回退」+ A-1.0「不静默打开旧对话框」
//      —— 本页承载**未支持项清单**，配合 ActionRouter 接管把 4 个老窗口动作改成明确提示。
//
// 与诊断页（T39 DiagnosticPage）的分工：那一页是**每帧变化的实时性能量**（轮询），
// 本页是**一次性静态健康量**（点「重新检查」才重算）。
//
// ⚠️ 两条来自 T41 的硬教训：
//   1. **可操作按钮放页首** —— 帮助页的跳转按钮曾因沉底（y=1036 > 窗口高）导致
//      "用户自己都点不到"（陷阱 92：先分清是被测物的问题还是观测姿势的问题）。
//   2. 列表一律用 `Repeater`（立即创建）而不是 `ListView`（隐藏页尺寸 0 ⇒ 不创建
//      delegate ⇒ 判据找不到控件）。
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: errorPage
    objectName: "errorPage"

    signal navigate(string page)

    readonly property int kMargin: 12

    // 状态词 → 颜色（浅色主题：深字 + 彩色标记）
    function stateColor(s) {
        if (s === "ok")
            return "#1b7f3b"
        if (s === "warn")
            return "#9a6700"
        if (s === "error" || s === "missing")
            return "#b3261e"
        if (s === "readonly")
            return "#9a6700"
        return "#5f6368"   // info / unset
    }

    function stateText(s) {
        if (s === "ok")
            return "正常"
        if (s === "warn")
            return "注意"
        if (s === "error")
            return "错误"
        if (s === "missing")
            return "缺失"
        if (s === "readonly")
            return "只读"
        if (s === "unset")
            return "未设置"
        return "信息"
    }

    ScrollView {
        id: errorScroll
        objectName: "errorScroll"
        anchors.fill: parent
        contentWidth: availableWidth
        clip: true

        ColumnLayout {
            width: errorScroll.availableWidth
            spacing: 10

            // ── 页头 ────────────────────────────────────────────────────────
            Label {
                objectName: "errorTitleLabel"
                Layout.fillWidth: true
                Layout.topMargin: 10
                Layout.leftMargin: errorPage.kMargin
                text: "状态与错误"
                font.pixelSize: 20
                font.bold: true
            }
            Label {
                objectName: "errorSubtitleLabel"
                Layout.fillWidth: true
                Layout.leftMargin: errorPage.kMargin
                text: "运行状态、资源路径与「未支持项」清单。" +
                      (ErrorModel.hasError ? "当前检测到问题，见下方红框。"
                                           : "当前未检测到问题。")
                color: ErrorModel.hasError ? "#b3261e" : "#5f6368"
                wrapMode: Text.WordWrap
            }

            // ── 操作行（**刻意放页首**，见文件头注）─────────────────────────
            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: errorPage.kMargin
                Layout.rightMargin: errorPage.kMargin
                spacing: 8

                Button {
                    objectName: "errorRefreshButton"
                    text: "重新检查"
                    onClicked: ErrorModel.refresh()
                }
                Button {
                    objectName: "errorCopyDiagnosticsButton"
                    text: "复制诊断信息"
                    onClicked: ErrorModel.copyDiagnostics()
                }
                Button {
                    objectName: "errorOpenLogButton"
                    text: "打开日志目录"
                    enabled: ErrorModel.logPath.length > 0
                    onClicked: ErrorModel.openPath("log")
                }
                Button {
                    objectName: "errorOpenConfigButton"
                    text: "打开配置目录"
                    enabled: ErrorModel.configPath.length > 0
                    onClicked: ErrorModel.openPath("config")
                }
                Item { Layout.fillWidth: true }
            }

            // ── 错误块（无错时整块隐藏）─────────────────────────────────────
            Rectangle {
                objectName: "errorPanel"
                Layout.fillWidth: true
                Layout.leftMargin: errorPage.kMargin
                Layout.rightMargin: errorPage.kMargin
                visible: ErrorModel.hasError
                color: "#fdecea"
                border.color: "#b3261e"
                border.width: 1
                radius: 6
                implicitHeight: errorCol.implicitHeight + 20

                ColumnLayout {
                    id: errorCol
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 6

                    Label {
                        objectName: "errorHeadlineLabel"
                        Layout.fillWidth: true
                        text: ErrorModel.errorHeadline
                        color: "#b3261e"
                        font.bold: true
                        font.pixelSize: 15
                        wrapMode: Text.WordWrap
                    }
                    Label {
                        objectName: "errorDetailLabel"
                        Layout.fillWidth: true
                        visible: text.length > 0
                        text: ErrorModel.errorDetail
                        color: "#5f1a16"
                        wrapMode: Text.WordWrap
                    }
                    Label {
                        objectName: "errorHintLabel"
                        Layout.fillWidth: true
                        visible: text.length > 0
                        text: ErrorModel.errorHint
                        color: "#5f6368"
                        wrapMode: Text.WordWrap
                    }
                }
            }

            // ── 运行状态区 ──────────────────────────────────────────────────
            Label {
                objectName: "errorStatusHeader"
                Layout.fillWidth: true
                Layout.leftMargin: errorPage.kMargin
                Layout.topMargin: 6
                text: "运行状态"
                font.bold: true
                font.pixelSize: 16
            }
            Repeater {
                model: ErrorModel.statusRows
                delegate: Rectangle {
                    required property int index
                    required property var modelData
                    objectName: "errorStatusRow_" + index
                    Layout.fillWidth: true
                    Layout.leftMargin: errorPage.kMargin
                    Layout.rightMargin: errorPage.kMargin
                    implicitHeight: statusRowLayout.implicitHeight + 8
                    color: index % 2 === 0 ? "#ffffff" : "#f5f6f7"

                    RowLayout {
                        id: statusRowLayout
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 8
                        spacing: 8

                        Label {
                            objectName: "errorStatusLabel_" + index
                            text: modelData.label
                            font.bold: true
                            Layout.preferredWidth: 120
                        }
                        Label {
                            objectName: "errorStatusValue_" + index
                            Layout.fillWidth: true
                            text: modelData.value
                            color: errorPage.stateColor(modelData.state)
                            wrapMode: Text.WordWrap
                        }
                        Label {
                            objectName: "errorStatusState_" + index
                            text: errorPage.stateText(modelData.state)
                            color: errorPage.stateColor(modelData.state)
                            font.pixelSize: 12
                        }
                    }
                }
            }

            // ── 资源路径区（A-1.0「资源路径 | 最小 | 必须」）─────────────────
            Label {
                objectName: "errorPathsHeader"
                Layout.fillWidth: true
                Layout.leftMargin: errorPage.kMargin
                Layout.topMargin: 6
                text: "资源路径（问题 " + ErrorModel.pathProblemCount + " 项）"
                font.bold: true
                font.pixelSize: 16
            }
            Repeater {
                model: ErrorModel.pathRows
                delegate: Rectangle {
                    required property int index
                    required property var modelData
                    objectName: "errorPathRow_" + index
                    Layout.fillWidth: true
                    Layout.leftMargin: errorPage.kMargin
                    Layout.rightMargin: errorPage.kMargin
                    implicitHeight: pathRowLayout.implicitHeight + 8
                    color: index % 2 === 0 ? "#ffffff" : "#f5f6f7"

                    ColumnLayout {
                        id: pathRowLayout
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 8
                        spacing: 2

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            Label {
                                objectName: "errorPathLabel_" + index
                                text: modelData.label
                                font.bold: true
                                Layout.preferredWidth: 140
                            }
                            Label {
                                objectName: "errorPathValue_" + index
                                Layout.fillWidth: true
                                text: modelData.path.length > 0 ? modelData.path : "（引擎未提供）"
                                color: errorPage.stateColor(modelData.state)
                                elide: Text.ElideMiddle
                            }
                            Label {
                                objectName: "errorPathState_" + index
                                text: errorPage.stateText(modelData.state)
                                color: errorPage.stateColor(modelData.state)
                                font.pixelSize: 12
                            }
                        }
                        Label {
                            objectName: "errorPathNote_" + index
                            Layout.fillWidth: true
                            visible: text.length > 0
                            text: modelData.note
                            color: "#5f6368"
                            font.pixelSize: 12
                            wrapMode: Text.WordWrap
                        }
                    }
                }
            }

            // ── 未支持项清单 ────────────────────────────────────────────────
            Label {
                objectName: "errorUnsupportedHeader"
                Layout.fillWidth: true
                Layout.leftMargin: errorPage.kMargin
                Layout.topMargin: 6
                text: "未支持项（" + ErrorModel.unsupportedCount + "）"
                font.bold: true
                font.pixelSize: 16
            }
            Label {
                objectName: "errorUnsupportedIntro"
                Layout.fillWidth: true
                Layout.leftMargin: errorPage.kMargin
                Layout.rightMargin: errorPage.kMargin
                text: "下列功能不在个人版范围内：对应按键不再打开老式窗口，" +
                      "而是给出明确提示（既不弹出旧界面，也不至于按了毫无反应）。"
                color: "#5f6368"
                font.pixelSize: 12
                wrapMode: Text.WordWrap
            }
            Repeater {
                model: ErrorModel.unsupportedRows
                delegate: Rectangle {
                    required property int index
                    required property var modelData
                    objectName: "errorUnsupportedRow_" + index
                    Layout.fillWidth: true
                    Layout.leftMargin: errorPage.kMargin
                    Layout.rightMargin: errorPage.kMargin
                    implicitHeight: unsupportedLayout.implicitHeight + 8
                    color: index % 2 === 0 ? "#ffffff" : "#f5f6f7"

                    ColumnLayout {
                        id: unsupportedLayout
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 8
                        spacing: 2

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            Label {
                                objectName: "errorUnsupportedFeature_" + index
                                text: modelData.feature
                                font.bold: true
                                Layout.fillWidth: true
                            }
                            Label {
                                objectName: "errorUnsupportedKeys_" + index
                                visible: text.length > 0
                                text: modelData.keys
                                color: "#5f6368"
                            }
                        }
                        Label {
                            objectName: "errorUnsupportedDetail_" + index
                            Layout.fillWidth: true
                            text: modelData.detail
                            color: "#5f6368"
                            font.pixelSize: 12
                            wrapMode: Text.WordWrap
                        }
                    }
                }
            }

            // ── 页脚 ────────────────────────────────────────────────────────
            Label {
                objectName: "errorFooterLabel"
                Layout.fillWidth: true
                Layout.leftMargin: errorPage.kMargin
                Layout.rightMargin: errorPage.kMargin
                Layout.topMargin: 8
                Layout.bottomMargin: 16
                text: ErrorModel.appName + " " + ErrorModel.fullVersion
                color: "#5f6368"
                font.pixelSize: 12
                wrapMode: Text.WordWrap
            }
        }
    }
}
