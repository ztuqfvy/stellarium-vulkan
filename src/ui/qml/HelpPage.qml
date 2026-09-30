// HelpPage.qml — T41-B：帮助页（A-1.0 范围表「帮助、版本与许可证」三项之一）。
//
// 内容取自老 HelpDialog 的 `keys` / `links` 两节，但**刻意改了两处结构**：
//
// ① 老版把**两半**都塞进一份只读 HTML：
//      · "没有 action 的手势"（鼠标/滚轮组合）—— 只能硬编码；
//      · "来自引擎注册表的 505 条动作"—— 引擎自己就有真源。
//    本页**只做前半**（`HelpModel.gestures`），后半给一个跳转按钮去**快捷键页**
//    —— 那里是**可编辑的真源**（T40 `ShortcutModel`，跟随用户的改键实时变化）。
//    复制一份影子表 = 迟早与真源漂移；而且老版那份还是只读的。
//
// ② `legacy` 行**显式标注**。老 HelpDialog 把它们印在同一张表里，只靠一句
//    "All these hotkeys are locally available to run when specific window or tab
//    is opened" 带过。但合流形态里**脚本控制台窗口（F12）与天文计算窗口（F10）
//    都未接入 QML** ⇒ 这些键在当前形态按下去没反应。标出来（红字「旧式窗口」），
//    并记入 T42 的「未支持项」清单 —— **不假装可用**（陷阱 82：UI 只提供/宣称
//    被验证过真的有效的东西）。
//
// ⚠️ 列表一律用 `Repeater` 而不是 `ListView`：
//   `StackLayout` 里**隐藏页的宽高是 0**，而 ListView 只在"有可视区域"时才创建
//   delegate（T40 实测：切页后要等 ≥1 帧才有 delegate，判据因此踩过坑）。
//   Repeater 是**立即全部创建**，与尺寸无关 —— 手势表只有几十行，代价可以忽略，
//   换来的是"控件在任何时候都真的存在"。
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: helpPage
    objectName: "helpPage"

    // 由 MainWindow 注入：引擎注册表里可绑定快捷键的动作总数（真源在 ShortcutModel）。
    // 刻意不在本页直接读 ShortcutModel —— 帮助页只该知道"有一个数字"，不该依赖
    // 快捷键编辑域的对象（耦合越小，将来拆页越自由）。
    property int engineActionCount: 0

    signal navigate(string page)

    // 扁平手势表按 `group` 分段显示：只在"组名变了"的行上显示组标题。
    // （用扁平表 + 条件标题，而不是嵌套 Repeater —— 嵌套会让 objectName 的
    //  序号变成两级拼接，判据定位又难写又易错。）
    function isGroupHead(i) {
        if (i === 0)
            return true
        var rows = HelpModel.gestures
        return rows[i - 1].group !== rows[i].group
    }

    function openLink(url) {
        if (!HelpModel.openExternal(url))
            console.warn("HelpPage: 链接被拒绝：" + url)
    }

    ScrollView {
        id: helpScroll
        objectName: "helpScroll"
        anchors.fill: parent
        contentWidth: availableWidth
        clip: true

        ColumnLayout {
            width: helpScroll.availableWidth
            spacing: 10

            // ── 页头 ────────────────────────────────────────────────────────
            Label {
                objectName: "helpTitleLabel"
                Layout.fillWidth: true
                Layout.topMargin: 10
                Layout.leftMargin: 12
                Layout.rightMargin: 12
                text: "帮助"
                font.pointSize: 18
                font.bold: true
                color: "#1565c0"
            }
            Label {
                objectName: "helpSummaryLabel"
                Layout.fillWidth: true
                Layout.leftMargin: 12
                Layout.rightMargin: 12
                wrapMode: Text.WordWrap
                color: "#555555"
                text: "下列手势没有对应的引擎动作，因此无法在快捷键页修改 —— 这里列出它们的实际行为。"
                      + "\n本形态可用手势 " + HelpModel.gestureCount + " 条"
                      + "｜旧式窗口局部键 " + HelpModel.legacyGestureCount + " 条"
                      + "｜延伸阅读 " + HelpModel.webLinks.length + " 条"
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.leftMargin: 12
                Layout.rightMargin: 12
                height: 1
                color: "#e0e0e0"
            }

            // ── 全部键盘快捷键 → 快捷键页（真源在那边，可编辑）──────────────
            // ⚠️ 刻意放在**页首**（手势表之前）：手势表有 23 行，按钮沉底的话在
            //   640 高的窗口里**根本点不到**（T41-C 判据首轮实抓：按钮中心 y=1036
            //   > 窗口 640 —— 不只是判据点不到，用户也一样点不到）。查键位是高频
            //   动作，入口必须在首屏。
            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 12
                Layout.rightMargin: 12
                spacing: 10

                Label {
                    objectName: "helpShortcutHintLabel"
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: "#555555"
                    text: "引擎注册表里另有 " + helpPage.engineActionCount
                          + " 个动作可以绑定快捷键。本页只列没有动作的手势；"
                          + "带动作的快捷键请在快捷键页查看或修改 —— 那里是唯一真源，会跟随你的改键。"
                }
                Button {
                    objectName: "helpGotoShortcutsButton"
                    text: "打开快捷键页"
                    onClicked: helpPage.navigate("shortcuts")
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.leftMargin: 12
                Layout.rightMargin: 12
                height: 1
                color: "#e0e0e0"
            }

            // ── 手势表（扁平 Repeater + 条件组标题）──────────────────────────
            Repeater {
                id: helpGestureRepeater
                objectName: "helpGestureRepeater"
                model: HelpModel.gestures

                delegate: ColumnLayout {
                    id: gestureRow
                    required property var modelData
                    required property int index
                    objectName: "helpGestureRow_" + index
                    Layout.fillWidth: true
                    Layout.leftMargin: 12
                    Layout.rightMargin: 12
                    spacing: 2

                    Label {
                        objectName: "helpGestureGroupHead_" + gestureRow.index
                        Layout.fillWidth: true
                        Layout.topMargin: gestureRow.index === 0 ? 0 : 8
                        visible: helpPage.isGroupHead(gestureRow.index)
                        text: gestureRow.modelData.group
                        font.bold: true
                        font.pointSize: 12
                        color: "#1565c0"
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10

                        Label {
                            objectName: "helpGestureAction_" + gestureRow.index
                            Layout.preferredWidth: 190
                            text: gestureRow.modelData.action
                            wrapMode: Text.WordWrap
                        }
                        Label {
                            objectName: "helpGestureKeys_" + gestureRow.index
                            text: gestureRow.modelData.keys
                            font.bold: true
                            font.family: "Menlo"
                            color: "#2e7d32"
                        }
                        Label {
                            objectName: "helpGestureLegacy_" + gestureRow.index
                            // legacy 行才可见 ⇒ 判据按"可见的 legacy 标记数 ==
                            // HelpModel.legacyGestureCount"来验（而不是数 objectName，
                            // 因为 objectName 对所有行都存在，数不出真假）。
                            visible: gestureRow.modelData.scope === "legacy"
                            text: "（旧式窗口内，本形态未接入）"
                            color: "#c62828"
                        }
                        Item { Layout.fillWidth: true }
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

            // ── 延伸阅读（外链）─────────────────────────────────────────────
            Label {
                objectName: "helpLinksHeaderLabel"
                Layout.fillWidth: true
                Layout.leftMargin: 12
                Layout.rightMargin: 12
                text: "延伸阅读"
                font.bold: true
                font.pointSize: 12
                color: "#1565c0"
            }
            Label {
                objectName: "helpLinksNote"
                Layout.fillWidth: true
                Layout.leftMargin: 12
                Layout.rightMargin: 12
                wrapMode: Text.WordWrap
                color: "#777777"
                text: "以下为外部链接，将在你的浏览器中打开。"
            }

            Repeater {
                id: helpLinkRepeater
                objectName: "helpLinkRepeater"
                model: HelpModel.webLinks

                delegate: RowLayout {
                    id: linkRow
                    required property var modelData
                    required property int index
                    objectName: "helpLinkRow_" + index
                    Layout.fillWidth: true
                    Layout.leftMargin: 12
                    Layout.rightMargin: 12
                    spacing: 8

                    // ⚠️ 用 Button 而不是 Link 控件：判据要往这里**注入真实点击**
                    //（T40 血泪：`invokeMethod` 不产生真交互）。Button 的点击路径
                    // 稳定可注入；Link 在 macOS 原生 style 下的命中区不好保证。
                    Button {
                        objectName: "helpLinkButton_" + linkRow.index
                        text: linkRow.modelData.title
                        flat: true
                        onClicked: helpPage.openLink(linkRow.modelData.url)
                    }
                    Label {
                        objectName: "helpLinkNote_" + linkRow.index
                        Layout.fillWidth: true
                        text: "— " + linkRow.modelData.note
                        color: "#777777"
                    }
                }
            }

            Item { Layout.preferredHeight: 12 }
        }
    }
}
