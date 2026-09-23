// SkyTestPage.qml — A2 静态图验证页：SkyViewport 铺满整页。
//
// 两条硬约束（STELQUICK_A2_CHECK 逐像素校验依赖它们）：
//   1. 页面底色必须是不透明纯色（半透明探针的合成期望值取决于它）；
//   2. 页内不得叠加任何浮动控件或文字覆盖层——会污染探针区域的像素。
// 诊断信息一律走 stdout 与诊断页，不往这页贴东西。
import QtQuick
import StelQuickUI 1.0

Item {
    id: page

    // 由 C++ 侧（main.cpp）按 STELQUICK_PATTERN_DUMP 注入的调试对照图路径。
    // 为空字符串时对照 Image 不显示也不加载（正式验收/日常运行路径）。
    property string debugPatternSource: ""

    Rectangle {
        anchors.fill: parent
        color: "#000000"   // 与 A2FrameCheck 的半透明探针期望一致
    }

    SkyViewport {
        id: viewport
        objectName: "skyViewport"   // C++ 装配点按此名查找到本项
        anchors.fill: parent
    }

    // P-BRG-04：进入降级预览（显示帧率低于阈值）时必须明确告警。
    // 只在 degraded 为真时可见——A2 逐像素校验路径不设阈值（degraded 恒 false），
    // 因此探针区域不被污染；本角标同时是"降级透明"这条验收的可视证据。
    Rectangle {
        id: degradeBadge
        visible: viewport.degraded
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 12
        width: badgeText.implicitWidth + 24
        height: badgeText.implicitHeight + 16
        radius: 6
        color: "#CC7A1F1F"          // 半透明深红：醒目且不遮挡判读
        border.color: "#FFE0A0A0"
        border.width: 1

        Text {
            id: badgeText
            anchors.centerIn: parent
            color: "#FFF0E0E0"
            font.pixelSize: 16
            font.bold: true
            text: qsTr("降级预览 · %1 fps").arg(Math.round(viewport.displayedFps))
        }
    }

    // 开发期对照实验：普通 Image 元素加载同一张测试图案。
    // Image 显示而 SkyViewport 不显示 → 问题在 QSGImageNode/纹理路径；
    // Image 也不显示 → 问题在窗口渲染/抓帧路径。
    // 位置特意避开全部探针点（色条从 y≈202 逻辑像素开始）。
    //
    // 路径来源：StaticFrameSource 在 STELQUICK_PATTERN_DUMP 指定时存盘的 PNG。
    // 未设置该环境变量时 source 为空 → Image 不加载、不报错（正常路径）。
    // 注意：不要硬编码 /tmp/...，Windows 上不存在（2026-09-21 修）。
    Image {
        id: debugImage
        source: debugPatternSource
        x: 200; y: 50; width: 240; height: 140
        fillMode: Image.Stretch
        cache: false
        visible: source !== ""
        onStatusChanged: if (status === Image.Error) console.warn("调试 Image 加载失败:", source)
    }
}
