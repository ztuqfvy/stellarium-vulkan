// LocationPage.qml — T33-B：观察地点页。
//
// A-alpha 出口判据（测试文档 `2026-09-17-03` §8 第一条）要求"第 3 节标'必须'的
// A-alpha 功能全部可操作：拖动/缩放/选中/跟踪、时间、**地点**、搜索、信息"。
// 其中"地点"在 T33 之前**一行未做** —— 本页是那一项的落地。
//
// ── 口径全部来自 T33-A 的地点数据面探针（证据 docs/evidence/2026-09-29-t33-location/）
//   ① 地点库在合流形态下加载正常：33501 条 / 193 区域 / 496 时区名。
//   ② `locationForString(单名)` **不报错也不命中**，返回 role='!' 的无效地点
//      ⇒ 本页一律用 **完整 ID**（`getID()` = "name, region"）与引擎对话；
//      搜索结果直接就是 ID 列表，点一条就 `setLocationById`。
//   ③ 引擎 `isValid()` **不校验经纬度范围** ⇒ 范围闸在 AppFacade 里
//      （`setLocationByCoordinates`），本页只负责"填的是不是数字"。
//   ④ 写后**时区联动**（`setObserver` 里 `setCurrentTimeZone(iana)`）；
//      按坐标写 ad-hoc 地点时 iana 留空 ⇒ 引擎跳过 ⇒ **时区不动**（设计取舍）。
//
// ── 分工（与 T17/T19 一致）
//   · 命令走 AppFacade（`setLocationByCoordinates` / `setLocationById` / `findLocations`）；
//     本页**不碰引擎**，也不做任何坐标换算。
//   · 状态一律看 **token**（`lastLocationRefusal`），文案只是把 token 翻成人话。
//     ⚠️ `lastLocationRefusal` 在 C++ 侧刻意声明成 **Q_PROPERTY**（不是 Q_INVOKABLE）：
//        写在绑定里时只有属性才拿得到字符串并建立依赖；方法读到的会是**函数对象**
//        （比较恒 false）。这是 T19 的血泪，照抄它的处置。
//   · 当前地点那几项是**轮询**（500 ms Timer），不建绑定 —— 它们全是 Q_INVOKABLE，
//     没有 NOTIFY 就没有依赖，绑定第一次求值后再不重算。
//   · 刻意**不回填输入框**（纬度/经度/高度三个 TextField）：回填会打断用户正在
//     输入的数字。只回填"当前地点"那张只读卡片 —— 于是也不需要 `syncing` 守卫。
//
// 禁区（docs/CODING_STANDARD.zh_CN.md 第 5 节）：本目录禁止 GL/Vulkan 特定调用。
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: locationPage

    // ── 当前地点（轮询填充；NaN 表示引擎不可用）──────────────────────────────
    property string curName: ""
    property string curId: ""
    property string curPlanet: ""
    property string curTz: ""
    property real curLat: NaN
    property real curLon: NaN
    property real curAlt: NaN
    property string engineTz: ""

    //! 搜索结果：**完整 ID** 列表（直接喂 setLocationById）。
    property var hits: []

    //! 坐标写入结果（本地即时反馈；空串表示"还没写过"）。
    property string statusText: ""

    function fmt(v, digits) {
        return isNaN(v) ? "—" : v.toFixed(digits)
    }

    function refresh() {
        curName    = appFacade.locationName()
        curId      = appFacade.locationId()
        curPlanet  = appFacade.locationPlanet()
        curTz      = appFacade.locationTimeZone()
        curLat     = appFacade.locationLatitude()
        curLon     = appFacade.locationLongitude()
        curAlt     = appFacade.locationAltitudeMeters()
        engineTz   = appFacade.timeZoneId()
    }

    function applyCoordinates() {
        var ls = latField.text.trim()
        var os = lonField.text.trim()
        var as = altField.text.trim()
        if (ls === "" || os === "") {
            statusText = "请填写纬度和经度。"
            return
        }
        // 高度留空按 0 处理（海拔不是必须项）；其余交给 AppFacade 的范围闸。
        var la = Number(ls)
        var lo = Number(os)
        var al = (as === "") ? 0 : Number(as)
        var ok = appFacade.setLocationByCoordinates(la, lo, al)
        var detail = appFacade.locationRefusalText()
        // 文案取"人话"，判定永远看 token（appFacade.lastLocationRefusal）。
        statusText = ok ? "已切换。" : (detail !== "" ? detail : "写入失败。")
        refresh()
        results.model = []      // 手动改坐标后，旧搜索结果不该留在屏幕上冒充"当前"
        hits = []
    }

    function runSearch() {
        var q = locSearchField.text.trim()
        if (q === "") {
            hits = []
            return
        }
        hits = appFacade.findLocations(q, 30)
        statusText = hits.length > 0
                     ? ("找到 " + hits.length + " 个地点，点击其中一条即可切换。")
                     : "没有匹配的地点。"
    }

    Component.onCompleted: refresh()

    // 当前地点轮询：引擎侧可能被其它入口（旧界面/脚本）改掉，本页不建绑定。
    Timer {
        interval: 500
        running: true
        repeat: true
        onTriggered: locationPage.refresh()
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 10

        // ── 左列：当前地点（只读投影）───────────────────────────────────────
        Rectangle {
            Layout.preferredWidth: 400
            Layout.fillHeight: true
            color: "#ffffff"
            border.color: "#e0e0e0"

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 8

                Label { text: "当前观察地点"; font.bold: true }

                Label {
                    id: curNameLabel
                    objectName: "locCurrentName"
                    text: locationPage.curName === "" ? "—" : locationPage.curName
                    font.pixelSize: 20
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                }

                Label {
                    id: curIdLabel
                    objectName: "locCurrentId"
                    text: locationPage.curId === "" ? "" : ("ID：" + locationPage.curId)
                    color: "#757575"
                    font.pixelSize: 11
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                }

                Rectangle { Layout.fillWidth: true; height: 1; color: "#eeeeee" }

                GridLayout {
                    columns: 2
                    columnSpacing: 12
                    rowSpacing: 6
                    Layout.fillWidth: true

                    Label { text: "纬度"; color: "#757575" }
                    Label {
                        id: curLatLabel
                        objectName: "locCurrentLat"
                        text: locationPage.fmt(locationPage.curLat, 4) + "°"
                    }

                    Label { text: "经度"; color: "#757575" }
                    Label {
                        id: curLonLabel
                        objectName: "locCurrentLon"
                        text: locationPage.fmt(locationPage.curLon, 4) + "°"
                    }

                    Label { text: "海拔"; color: "#757575" }
                    Label {
                        id: curAltLabel
                        objectName: "locCurrentAlt"
                        text: locationPage.fmt(locationPage.curAlt, 1) + " m"
                    }

                    Label { text: "所属行星"; color: "#757575" }
                    Label {
                        id: curPlanetLabel
                        objectName: "locCurrentPlanet"
                        text: locationPage.curPlanet === "" ? "—" : locationPage.curPlanet
                    }

                    Label { text: "地点时区"; color: "#757575" }
                    Label {
                        id: curTzLabel
                        objectName: "locCurrentTz"
                        text: locationPage.curTz === "" ? "（地点未指定）" : locationPage.curTz
                    }

                    Label { text: "引擎时区"; color: "#757575" }
                    Label {
                        id: engineTzLabel
                        objectName: "locCurrentEngineTz"
                        text: locationPage.engineTz === "" ? "—" : locationPage.engineTz
                        color: "#757575"
                    }
                }

                Label {
                    text: "「地点时区」是地点自带的默认值；「引擎时区」是当前生效值。"
                          + "按坐标切换时**不**改时区（保守做法）。"
                    color: "#9e9e9e"
                    font.pixelSize: 11
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                }

                Item { Layout.fillHeight: true }
            }
        }

        // ── 右列：搜索地点 + 按坐标设置 ─────────────────────────────────────
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "#ffffff"
            border.color: "#e0e0e0"

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 8

                Label { text: "搜索地点（按名称/区域/行星）"; font.bold: true }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6

                    TextField {
                        id: locSearchField
                        objectName: "locSearchField"
                        Layout.fillWidth: true
                        placeholderText: "例如 Beijing / Paris / Mars"
                        onAccepted: locationPage.runSearch()
                    }
                    Button {
                        id: locSearchButton
                        objectName: "locSearchButton"
                        text: "搜索"
                        onClicked: locationPage.runSearch()
                    }
                }

                // 搜索结果：值为**完整 ID**（探针 ②：单名不可用）。
                ListView {
                    id: results
                    objectName: "locResultList"
                    Layout.fillWidth: true
                    Layout.preferredHeight: 180
                    clip: true
                    model: locationPage.hits
                    boundsBehavior: Flickable.StopAtBounds

                    ScrollBar.vertical: ScrollBar { }

                    delegate: ItemDelegate {
                        required property string modelData
                        width: results.width
                        text: modelData
                        font.pixelSize: 12
                        onClicked: {
                            var ok = appFacade.setLocationById(modelData)
                            var detail = appFacade.locationRefusalText()
                            locationPage.statusText = ok
                                ? ("已切换到：" + modelData)
                                : (detail !== "" ? detail : "切换失败。")
                            locationPage.refresh()
                        }
                    }

                    Label {
                        anchors.centerIn: parent
                        visible: results.count === 0
                        text: "（搜索结果会显示在这里）"
                        color: "#bdbdbd"
                        font.pixelSize: 12
                    }
                }

                Rectangle { Layout.fillWidth: true; height: 1; color: "#eeeeee" }

                Label { text: "按坐标设置（纬度 / 经度 / 海拔）"; font.bold: true }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6

                    TextField {
                        id: latField
                        objectName: "locLatField"
                        Layout.fillWidth: true
                        placeholderText: "纬度 -90..90"
                    }
                    TextField {
                        id: lonField
                        objectName: "locLonField"
                        Layout.fillWidth: true
                        placeholderText: "经度 -180..180"
                    }
                    TextField {
                        id: altField
                        objectName: "locAltField"
                        Layout.preferredWidth: 110
                        placeholderText: "海拔 m"
                    }
                    Button {
                        id: applyButton
                        objectName: "locApplyButton"
                        text: "应用"
                        onClicked: locationPage.applyCoordinates()
                    }
                }

                Label {
                    id: statusLabel
                    objectName: "locStatusLabel"
                    text: locationPage.statusText
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    color: appFacade.lastLocationRefusal === "ok" ? "#2e7d32" : "#c62828"
                }

                Item { Layout.fillHeight: true }
            }
        }
    }
}
