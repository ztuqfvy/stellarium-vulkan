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

    Rectangle {
        anchors.fill: parent
        color: "#000000"   // 与 A2FrameCheck 的半透明探针期望一致
    }

    SkyViewport {
        id: viewport
        objectName: "skyViewport"   // C++ 装配点按此名查找到本项
        anchors.fill: parent
    }

    // 开发期对照实验：普通 Image 元素加载同一张测试图案。
    // Image 显示而 SkyViewport 不显示 → 问题在 QSGImageNode/纹理路径；
    // Image 也不显示 → 问题在窗口渲染/抓帧路径。
    // 位置特意避开全部探针点（色条从 y≈202 逻辑像素开始）。
    Image {
        source: "file:///tmp/stel_pattern.png"
        x: 200; y: 50; width: 240; height: 140
        fillMode: Image.Stretch
        cache: false
        onStatusChanged: if (status === Image.Error) console.warn("调试 Image 加载失败:", source)
    }
}
