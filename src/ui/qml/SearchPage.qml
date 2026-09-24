// SearchPage.qml — T17：搜索 → 选择 → 信息页（纵向链路）。
//
// 分工（与 docs/T17 的约定一致）：
//   · 列表数据来自模型本体：`model: searchResults`（ListModel/QAIM 直供，不经过 AppFacade）；
//   · 命令走 AppFacade：searchObjects / selectSearchResult / clearSelection。
//   · 本页面**不碰引擎**：没有 GL/Vulkan 调用，也不知道对象是怎么被选中的
//     （C++ 侧把 stableId 解析成引擎对象——QML 永远只拿字符串）。
//
// 禁区（docs/CODING_STANDARD.zh_CN.md 第 5 节）：本目录禁止 GL/Vulkan 特定调用。
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: searchPage

    // 供 MainWindow 的页切换器使用；也可由 C++ startPage="search" 直接进入。
    property alias queryText: queryField.text

    RowLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 8

        // ── 左列：搜索框 + 结果列表 ─────────────────────────────────────────
        ColumnLayout {
            Layout.fillHeight: true
            Layout.preferredWidth: 300
            spacing: 6

            Label {
                text: "搜索天体"
                font.bold: true
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 4

                TextField {
                    id: queryField
                    Layout.fillWidth: true
                    placeholderText: "如 Sirius / Mars / Polaris"
                    selectByMouse: true
                    // 焦点在本输入框时，ActionRouter 的焦点守卫会拒收天空快捷键
                    // （U-ACT-03）——所以这里无需再写任何键盘屏蔽代码。
                    onAccepted: appFacade.searchObjects(queryField.text)
                }
                Button {
                    text: "搜索"
                    enabled: queryField.text.trim().length > 0
                    onClicked: appFacade.searchObjects(queryField.text)
                }
            }

            // 状态行：搜索中 / 空态理由 / 命中数
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: searchResults.count > 0 ? "#2e7d32" : "#757575"
                text: {
                    if (searchResults.searching)
                        return "搜索中…"
                    if (searchResults.count > 0)
                        return searchResults.count + " 条结果（" + searchResults.lastQuery + "）"
                    if (searchResults.emptyReason.length > 0)
                        return searchResults.emptyReason
                    return "输入名称开始搜索"
                }
            }

            ListView {
                id: resultList
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                // 直接消费模型本体（模型是数据，AppFacade 是命令）。
                model: searchResults
                boundsBehavior: Flickable.StopAtBounds

                delegate: Rectangle {
                    width: resultList.width
                    height: 46
                    color: ListView.isCurrentItem ? "#e3f2fd" : "transparent"
                    border.color: "#e0e0e0"
                    border.width: 1

                    Column {
                        anchors.fill: parent
                        anchors.margins: 6
                        spacing: 2
                        Label {
                            text: model.name
                            font.bold: true
                            elide: Text.ElideRight
                            width: parent.width
                        }
                        Label {
                            // 引擎给的天文类型（i18n）；英文名作副信息（也是身份比对键）
                            text: model.objectType + " · " + model.englishName
                            color: "#757575"
                            font.pixelSize: 11
                            elide: Text.ElideRight
                            width: parent.width
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        onClicked: {
                            resultList.currentIndex = index
                            // 索引交给 AppFacade：它取该行的 stableId 再让引擎解析选中。
                            appFacade.selectSearchResult(index)
                        }
                    }
                }
            }

            Button {
                Layout.fillWidth: true
                text: "清除选中"
                enabled: objectInfo.hasSelection
                onClicked: appFacade.clearSelection()
            }
        }

        // ── 右列：信息页 ───────────────────────────────────────────────────
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "#ffffff"
            border.color: "#e0e0e0"

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 6

                Label {
                    text: "天体信息"
                    font.bold: true
                }

                // 空态：三档区分——引擎没起来 / 没选中 / 有选中
                Label {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    wrapMode: Text.WordWrap
                    color: "#9e9e9e"
                    visible: !objectInfo.hasSelection
                    text: {
                        if (!objectInfo.engineReady)
                            return "引擎未就绪（本构建未链接引擎，或引擎尚未引导）"
                        return "未选中天体。在左侧搜索并点击一条结果。"
                    }
                }

                Flickable {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: objectInfo.hasSelection
                    clip: true
                    contentWidth: infoColumn.width
                    contentHeight: infoColumn.height
                    ScrollBar.vertical: ScrollBar { }

                    Column {
                        id: infoColumn
                        width: parent.width
                        spacing: 6

                        Label {
                            text: objectInfo.displayName
                            font.bold: true
                            font.pixelSize: 18
                            wrapMode: Text.WordWrap
                            width: parent.width
                        }

                        GridLayout {
                            columns: 2
                            columnSpacing: 10
                            rowSpacing: 3
                            width: parent.width

                            Label { text: "英文名"; color: "#757575" }
                            Label { text: objectInfo.englishName; }
                            Label { text: "类型"; color: "#757575" }
                            Label { text: objectInfo.objectType + "（" + objectInfo.typeName + "）"; }
                            Label { text: "稳定标识"; color: "#757575" }
                            Label {
                                text: objectInfo.stableId
                                color: "#1565c0"
                                font.family: "monospace"
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                            Label { text: "赤经(°)"; color: "#757575" }
                            Label {
                                text: objectInfo.infoMap.ra !== undefined
                                      ? Number(objectInfo.infoMap.ra).toFixed(4) : "—"
                            }
                            Label { text: "赤纬(°)"; color: "#757575" }
                            Label {
                                text: objectInfo.infoMap.dec !== undefined
                                      ? Number(objectInfo.infoMap.dec).toFixed(4) : "—"
                            }
                        }

                        Rectangle { width: parent.width; height: 1; color: "#eeeeee" }

                        // 引擎给的完整信息串（含 HTML 标记）→ 用 RichText 渲染。
                        // 注意：引擎的 InfoStringGroup 口径是 DefaultInfo（见 ObjectInfoModel.cpp）。
                        Text {
                            width: parent.width
                            text: objectInfo.infoText
                            textFormat: Text.RichText
                            wrapMode: Text.WordWrap
                        }
                    }
                }
            }
        }
    }
}
