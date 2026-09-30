// AboutPage.qml — T41-B：版本与许可证页（A-1.0 范围表「帮助、版本与许可证」的第二、三项）。
//
// 与老 HelpDialog 的 About 标签页同源（版本串 / 版权 / 贡献者），但有两处强化：
//
// ① **贡献者是完整 245 条**（老版是在一个 QTextBrowser 里铺一段纯文本；这里给
//    独立可滚列表 + 计数，245 条不截断、不省略）。
// ② **GPL 全文真的读得到**。T41-A 探针坐实：`COPYING` **不在 app bundle 里**
//    （`Contents/` 只有 Info.plist / MacOS / translations，`Contents/Resources/`
//    这个目录都不存在）；探针里 `findFile("COPYING")` 之所以命中，纯属当时 cwd
//    恰好是源码树根 ⇒ **分发后必然失效**。⇒ 全文**编进 qrc**
//    （`:/StelQuickUI/COPYING`，见 `src/ui/CMakeLists.txt` 与 `HelpModel::loadLicense()`）。
//
// ⚠️ 版本信息**引擎引导后才有**（`StelUtils` 要走 `StelFileMgr`）。`HelpModel.refresh()`
//   在引导完成后调用；页面用 `HelpModel.ready` 区分"正在读取"与"读到了"。
//   ⚠️ 尤其**不能**用 `QCoreApplication::applicationVersion()`（那是「1.0.0」，
//     没被设成产品版本 —— 探针 Q4 实测）。
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import StelQuickUI 1.0   // BackendInfo 单例（渲染后端名）

Item {
    id: aboutPage
    objectName: "aboutPage"

    signal navigate(string page)

    function kvRow(rows, i) {
        return rows[i]
    }

    ScrollView {
        id: aboutScroll
        objectName: "aboutScroll"
        anchors.fill: parent
        contentWidth: availableWidth
        clip: true

        ColumnLayout {
            width: aboutScroll.availableWidth
            spacing: 10

            // ── 页头 ────────────────────────────────────────────────────────
            Label {
                objectName: "aboutAppNameLabel"
                Layout.fillWidth: true
                Layout.topMargin: 10
                Layout.leftMargin: 12
                Layout.rightMargin: 12
                text: HelpModel.appName
                font.pointSize: 20
                font.bold: true
                color: "#1565c0"
            }
            Label {
                objectName: "aboutVersionLabel"
                Layout.fillWidth: true
                Layout.leftMargin: 12
                Layout.rightMargin: 12
                text: HelpModel.fullVersion.length > 0
                      ? ("版本 " + HelpModel.fullVersion)
                      : "（版本信息读取中…）"
                color: "#555555"
                font.family: "Menlo"
            }
            Label {
                objectName: "aboutBuildStateLabel"
                Layout.fillWidth: true
                Layout.leftMargin: 12
                Layout.rightMargin: 12
                wrapMode: Text.WordWrap
                color: "#777777"
                text: "本界面是 Stellarium 引擎的 QML/Vulkan 重写形态（合流宿主）。"
                      + "星历与天文计算由原引擎提供，界面由 Qt Quick 重建。"
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.leftMargin: 12
                Layout.rightMargin: 12
                height: 1
                color: "#e0e0e0"
            }

            // ── 版本行 ──────────────────────────────────────────────────────
            Label {
                objectName: "aboutVersionHeaderLabel"
                Layout.fillWidth: true
                Layout.leftMargin: 12
                Layout.rightMargin: 12
                text: "版本"
                font.bold: true
                font.pointSize: 12
                color: "#1565c0"
            }
            Repeater {
                id: versionRepeater
                objectName: "aboutVersionRepeater"
                model: HelpModel.versionRows

                delegate: RowLayout {
                    id: versionRow
                    required property var modelData
                    required property int index
                    objectName: "aboutVersionRow_" + index
                    Layout.fillWidth: true
                    Layout.leftMargin: 12
                    Layout.rightMargin: 12
                    spacing: 10

                    Label {
                        objectName: "aboutVersionLabel_" + versionRow.index
                        Layout.preferredWidth: 120
                        text: versionRow.modelData.label
                        color: "#555555"
                    }
                    Label {
                        objectName: "aboutVersionValue_" + versionRow.index
                        Layout.fillWidth: true
                        text: versionRow.modelData.value
                        font.family: "Menlo"
                        wrapMode: Text.WordWrap
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.leftMargin: 12
                Layout.rightMargin: 12
                Layout.topMargin: 6
                height: 1
                color: "#e0e0e0"
            }

            // ── 系统行 ──────────────────────────────────────────────────────
            Label {
                objectName: "aboutSystemHeaderLabel"
                Layout.fillWidth: true
                Layout.leftMargin: 12
                Layout.rightMargin: 12
                text: "运行环境"
                font.bold: true
                font.pointSize: 12
                color: "#1565c0"
            }
            Repeater {
                id: systemRepeater
                objectName: "aboutSystemRepeater"
                model: HelpModel.systemRows

                delegate: RowLayout {
                    id: systemRow
                    required property var modelData
                    required property int index
                    objectName: "aboutSystemRow_" + index
                    Layout.fillWidth: true
                    Layout.leftMargin: 12
                    Layout.rightMargin: 12
                    spacing: 10

                    Label {
                        objectName: "aboutSystemLabel_" + systemRow.index
                        Layout.preferredWidth: 120
                        text: systemRow.modelData.label
                        color: "#555555"
                    }
                    Label {
                        objectName: "aboutSystemValue_" + systemRow.index
                        Layout.fillWidth: true
                        text: systemRow.modelData.value
                        font.family: "Menlo"
                        wrapMode: Text.WordWrap
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 12
                Layout.rightMargin: 12
                spacing: 10
                Label {
                    Layout.preferredWidth: 120
                    text: "渲染后端"
                    color: "#555555"
                }
                Label {
                    objectName: "aboutBackendValue"
                    Layout.fillWidth: true
                    font.family: "Menlo"
                    // BackendInfo 是 T39 落的运行时诊断面（backendOk/runtimeApiName）。
                    text: BackendInfo.runtimeApiName
                          + (BackendInfo.backendOk ? "" : "（未就绪）")
                    color: BackendInfo.backendOk ? "#2e7d32" : "#c62828"
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.leftMargin: 12
                Layout.rightMargin: 12
                Layout.topMargin: 6
                height: 1
                color: "#e0e0e0"
            }

            // ── 许可证 ──────────────────────────────────────────────────────
            Label {
                objectName: "aboutLicenseHeaderLabel"
                Layout.fillWidth: true
                Layout.leftMargin: 12
                Layout.rightMargin: 12
                text: "许可证"
                font.bold: true
                font.pointSize: 12
                color: "#1565c0"
            }
            Label {
                objectName: "aboutCopyrightLabel"
                Layout.fillWidth: true
                Layout.leftMargin: 12
                Layout.rightMargin: 12
                color: "#555555"
                // 版权串**刻意不翻译**：老 HelpDialog 明说版权行 not suitable for
                // translation —— 那是法律声明，不是 UI 文案。
                text: HelpModel.copyrightText
            }
            Label {
                objectName: "aboutLicenseSummaryLabel"
                Layout.fillWidth: true
                Layout.leftMargin: 12
                Layout.rightMargin: 12
                wrapMode: Text.WordWrap
                color: "#777777"
                text: "本程序是自由软件；你可以依据自由软件基金会发布的 GNU 通用公共许可证"
                      + "（第 2 版或你选择的任何更晚版本）条款重新发布和/或修改它。"
                      + "本程序按「无任何担保」分发。"
            }
            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 12
                Layout.rightMargin: 12
                spacing: 10

                Button {
                    objectName: "aboutLicenseButton"
                    text: "查看 GNU GPL v2 全文"
                    onClicked: licenseDialog.open()
                }
                Label {
                    objectName: "aboutLicenseStateLabel"
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: HelpModel.licenseLoaded ? "#2e7d32" : "#c62828"
                    text: HelpModel.licenseLoaded
                          ? ("已装入全文 " + HelpModel.licenseLineCount + " 行（内置资源 "
                             + HelpModel.licenseResourcePath + "）")
                          : ("许可证不可达：" + HelpModel.licenseError)
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.leftMargin: 12
                Layout.rightMargin: 12
                Layout.topMargin: 6
                height: 1
                color: "#e0e0e0"
            }

            // ── 贡献者 ──────────────────────────────────────────────────────
            Label {
                objectName: "aboutContributorHeaderLabel"
                Layout.fillWidth: true
                Layout.leftMargin: 12
                Layout.rightMargin: 12
                text: "贡献者（" + HelpModel.contributorCount + "）"
                font.bold: true
                font.pointSize: 12
                color: "#1565c0"
            }
            Label {
                objectName: "aboutContributorNote"
                Layout.fillWidth: true
                Layout.leftMargin: 12
                Layout.rightMargin: 12
                wrapMode: Text.WordWrap
                color: "#777777"
                text: "以下为 Stellarium 项目的贡献者名单（含译者与地景/脚本作者）。"
            }
            // 245 条 ⇒ 这里用 ListView（惰性）而不是 Repeater：Repeater 会立即
            // 创建 245 个 item，白占内存。代价是"隐藏页尺寸 0 时不建 delegate"
            // —— 判据已知此坑（T40），切页后等一帧再找控件。
            ListView {
                id: contributorList
                objectName: "aboutContributorList"
                Layout.fillWidth: true
                Layout.leftMargin: 12
                Layout.rightMargin: 12
                Layout.preferredHeight: 200
                clip: true
                model: HelpModel.contributors
                spacing: 1
                delegate: Label {
                    required property string modelData
                    required property int index
                    objectName: "aboutContributor_" + index
                    width: contributorList.width
                    text: (index + 1) + ".  " + modelData
                    elide: Text.ElideRight
                }
            }

            Item { Layout.preferredHeight: 12 }
        }
    }

    // GPL 全文弹窗。全文 340 行 / 约 18 KB，一次性显示即可（不虚拟化）。
    Dialog {
        id: licenseDialog
        objectName: "aboutLicenseDialog"
        title: "GNU 通用公共许可证 第 2 版"
        modal: true
        // ⚠️ Popup **不是 Item**，不要写 anchors.centerIn（那是 Item 的 API）。
        // Qt 6 的 Popup 默认就居中于其 parent。
        width: Math.min(aboutPage.width - 40, 820)
        height: Math.min(aboutPage.height - 40, 620)
        standardButtons: Dialog.Close

        ScrollView {
            id: licenseScroll
            objectName: "aboutLicenseScroll"
            anchors.fill: parent
            clip: true

            TextArea {
                objectName: "aboutLicenseTextArea"
                // 判据读的就是这里（真实 UI 上的文本，不是另开一个数据副本）。
                text: HelpModel.licenseText
                readOnly: true
                wrapMode: TextArea.NoWrap
                font.family: "Menlo"
                font.pointSize: 9
            }
        }
    }
}
