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

        // T25：滚轮 → 引擎缩放（修复自 T10 起合流形态滚轮死路）。
        //
        // 背景：旧宿主的滚轮链路是 StelMainView::wheelEvent → StelApp::handleWheel，
        // 合流形态一直没接 —— QML 里此前没有任何 WheelHandler/onWheel，
        // 用户在天空页滚动滚轮，引擎纹丝不动。T15 的键盘死代码是同款缺陷
        // （键盘已在 T17 修复），滚轮是这条交互面剩下的另一半。
        //
        // 语义边界：这里**只转发**窗口真实 angleDelta/modifiers，交给
        // AppFacade::wheelZoom → StelApp::handleWheel；缩放倍率、Ctrl+滚轮改
        // 时间等 modifiers 语义全在引擎侧，QML 侧复刻任何一条都是双轨。
        //
        // 页守卫天然成立：本 Handler 挂在 SkyViewport（只在天空页可见）上，
        // StackLayout 切页后事件落不到这里 ⇒ 时间页/搜索页滚轮不缩放天空
        // （INTERACTCHECK IT-04 就是这条负控）。
        // 本页硬约束（页头注释 1/2 条）不受影响：WheelHandler 不渲染任何内容。
        WheelHandler {
            onWheel: (wheel) => appFacade.wheelZoom(wheel.angleDelta.x,
                                                    wheel.angleDelta.y,
                                                    wheel.modifiers)
        }

        // T27：鼠标 → 引擎点击选中 / 拖拽平移（修复自 T10 起合流形态鼠标死路）。
        //
        // 背景：旧宿主的鼠标链路是 StelMainView 的 mousePress/Release →
        // StelApp::handleClick、mouseMove → StelApp::handleMove，合流形态一直没接
        // —— QML 天空页此前没有任何 MouseArea/DragHandler（T25 滚轮死路的同款
        // 缺陷，同一条交互面的最后一半）。
        //
        // 语义边界：这里**只转发**窗口真实坐标/按键/修饰键；press 置 isDragging、
        // release 无拖拽时选中对象、右键反选、Ctrl+拖拽改时间等语义全在引擎侧
        // （QML 不复刻任何一条）。y 翻转（QML y 向下 → 引擎 y 向上）在
        // AppFacade::skyMouse* 内做，与旧宿主 convertMouseEvent 的同一行适配。
        //
        // 页守卫天然成立：本 MouseArea 挂在 SkyViewport（只在天空页可见）上，
        // 切页后事件落不到这里。A2 逐像素硬约束（页头注释）不受影响：
        // MouseArea 不渲染任何内容。
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            onPressed: (mouse) => appFacade.skyMousePress(mouse.x, mouse.y, width, height,
                                                           mouse.button, mouse.modifiers)
            onPositionChanged: (mouse) => appFacade.skyMouseMove(mouse.x, mouse.y, width, height,
                                                                  mouse.buttons)
            onReleased: (mouse) => appFacade.skyMouseRelease(mouse.x, mouse.y, width, height,
                                                              mouse.button, mouse.modifiers)
        }
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
