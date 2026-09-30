// DisplayPage.qml — T38-B：显示参数页（亮度/星等 · 视场 · 投影）。
//
// A-1.0 范围表把「亮度/星等、视场、投影、主题/夜视、高 DPI」列为**必须（基础级）**；
// T37 做掉了其中"主题/夜视"，本页是另外三项的落地（高 DPI 归 T39）。
//
// ── 口径全部来自 T38-A 的命令面探针（见 app/DisplayProbe.hpp 的"实测结论"段）
//   ① 引擎四个亮度/星等 setter **一个都不夹取**（写 4/3/5/12.5 全部原样落库，
//      还会 `immediateSave` 落盘）⇒ 范围闸在 **AppFacade** 里；本页的滑块
//      直接把 min/max 绑到 façade 的常量上，**两边不各写一份范围**。
//   ② `StelMovementMgr::setFov` 自带夹取，但 **maxFov 是投影的函数**
//      （Perspective=120 / Stereographic=235 / Fisheye=360 …）⇒ 视场滑块的上限
//      必须绑 `appFacade.maxFieldOfView`（活属性）——否则切到透视投影后拖到 300
//      会被引擎静默夹回 120（"拖了没反应"）。
//   ③ 投影写**必须白名单闸**：引擎对非法 key 不报错、会静默落到 Stereographic
//      并落盘（探针实测）⇒ 本页只从 `projectionTypeKeys()` 里出按钮，
//      写入走 `setProjectionTypeKey()`（façade 再闸一次）。
//   ④ 🔴 **别拿 `getLimitMagnitude()` 当"星等限制"** —— 那是引擎按大气/光污染
//      算出的**有效值**（白天 ≈ -4.4，与用户设定无关）。用户设定的真值是
//      `customStarMagLimit`，且**只有 `flagStarMagnitudeLimit` 打开时才生效**
//      （生效点在 `ZoneArray.cpp:449` 的星表截断）⇒ 本页照原版 ViewDialog 的
//      语义：一个**复选框** + 一个数值滑块（不看复选框，用户会"拖了没反应"）。
//   ⑤ 视场**不走 `setFieldOfView()`**：那条路是 `zoomTo(deg, 0.4s)` 动画
//      （T28 捏合/滚轮拍的）——滑块每拖一次发一次动画会互相打架。本页用
//      `setFieldOfViewNow()`（引擎 `setFov`，立即落定）。
//   ⑥ 引擎 NOTIFY 静置期 0 发射（探针实测 1.5s 内 0/0/0）⇒ 这些量**不是每帧
//      变的连续量**，可以直接绑定，不会引发 T15 说的重算风暴。
//
// ── 分工（与 T19/T33 一致）
//   · 命令与状态都走 AppFacade；本页**不碰引擎**，也不做任何单位换算。
//   · 拒绝理由看 **token**（`lastDisplayRefusal`，C++ 侧刻意是 Q_PROPERTY —— 
//     Q_INVOKABLE 在绑定里读到的是函数对象，比较恒 false，T19 血泪）。
//
// 禁区（docs/CODING_STANDARD.zh_CN.md 第 5 节）：本目录禁止 GL/Vulkan 特定调用。
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: displayPage

    //! 本地状态行（空串 = 还没写过；非空 = 本次会话的写入反馈）。
    property string statusText: ""

    function noteRefusal() {
        var t = appFacade.displayRefusalText
        statusText = (t === "") ? "" : t
    }

    // 光污染亮度是对数量级（1e-6 … 1e-1）⇒ 滑块走 [-6,-1] 的指数刻度。
    readonly property real lpLogMin: Math.log(10) * -6
    readonly property real lpLogMax: Math.log(10) * -1

    ScrollView {
        anchors.fill: parent
        clip: true

        ColumnLayout {
            width: displayPage.width
            spacing: 10

            Label {
                text: "显示参数（亮度 · 星等 · 视场 · 投影）"
                font.bold: true
                font.pixelSize: 16
                Layout.margins: 8
            }

            Label {
                objectName: "displayRefusalLabel"
                text: displayPage.statusText === "" ? "　" : displayPage.statusText
                color: displayPage.statusText === "" ? "#757575" : "#c62828"
                Layout.leftMargin: 8
            }

            // ── 星点亮度 ─────────────────────────────────────────────────────
            GroupBox {
                Layout.fillWidth: true
                Layout.margins: 8
                title: "星点亮度"

                GridLayout {
                    columns: 3
                    columnSpacing: 12
                    rowSpacing: 6
                    anchors.fill: parent

                    Label { text: "相对亮度" }
                    Slider {
                        objectName: "displayRelScaleSlider"
                        Layout.fillWidth: true
                        from: appFacade.starRelativeScaleMin
                        to: appFacade.starRelativeScaleMax
                        stepSize: 0.05
                        // ⚠️ 绑定：引擎 NOTIFY 静置期不发 ⇒ 不会重算风暴（探针⑥）。
                        value: appFacade.starRelativeScale
                        onMoved: {
                            appFacade.starRelativeScale = value
                            displayPage.noteRefusal()
                        }
                    }
                    Label {
                        objectName: "displayRelScaleValue"
                        Layout.preferredWidth: 56
                        text: appFacade.starRelativeScale.toFixed(2)
                        color: "#37474f"
                    }

                    Label { text: "星点尺寸" }
                    Slider {
                        objectName: "displayAbsScaleSlider"
                        Layout.fillWidth: true
                        from: appFacade.starAbsoluteScaleMin
                        to: appFacade.starAbsoluteScaleMax
                        stepSize: 0.05
                        value: appFacade.starAbsoluteScale
                        onMoved: {
                            appFacade.starAbsoluteScale = value
                            displayPage.noteRefusal()
                        }
                    }
                    Label {
                        objectName: "displayAbsScaleValue"
                        Layout.preferredWidth: 56
                        text: appFacade.starAbsoluteScale.toFixed(2)
                        color: "#37474f"
                    }

                    Label { text: "夜空背景亮度" }
                    Slider {
                        objectName: "displayLightPollutionSlider"
                        Layout.fillWidth: true
                        // 对数刻度：滑块的线性值指数量级（1e-6 … 1e-1）。
                        from: displayPage.lpLogMin
                        to: displayPage.lpLogMax
                        value: Math.log(Math.max(appFacade.lightPollutionLuminance, 1e-9))
                        onMoved: {
                            appFacade.lightPollutionLuminance = Math.exp(value)
                            displayPage.noteRefusal()
                        }
                    }
                    Label {
                        objectName: "displayLightPollutionValue"
                        Layout.preferredWidth: 88
                        text: appFacade.lightPollutionLuminance.toExponential(1)
                        color: "#37474f"
                    }
                }
            }

            // ── 极限星等（复选框 + 数值，照原版 ViewDialog 语义）──────────────
            GroupBox {
                Layout.fillWidth: true
                Layout.margins: 8
                title: "极限星等（星表截断）"

                GridLayout {
                    columns: 3
                    columnSpacing: 12
                    rowSpacing: 6
                    anchors.fill: parent

                    CheckBox {
                        objectName: "displayMagLimitCheck"
                        text: "手动"
                        checked: appFacade.starMagnitudeLimitEnabled
                        onToggled: {
                            appFacade.starMagnitudeLimitEnabled = checked
                            displayPage.noteRefusal()
                        }
                    }
                    Slider {
                        objectName: "displayMagLimitSlider"
                        Layout.fillWidth: true
                        enabled: appFacade.starMagnitudeLimitEnabled
                        from: appFacade.starMagnitudeLimitMin
                        to: appFacade.starMagnitudeLimitMax
                        stepSize: 0.1
                        value: appFacade.starMagnitudeLimit
                        onMoved: {
                            appFacade.starMagnitudeLimit = value
                            displayPage.noteRefusal()
                        }
                    }
                    Label {
                        objectName: "displayMagLimitValue"
                        Layout.preferredWidth: 56
                        text: appFacade.starMagnitudeLimit.toFixed(1) + " 等"
                        color: appFacade.starMagnitudeLimitEnabled ? "#37474f" : "#9e9e9e"
                    }

                    Label {
                        Layout.columnSpan: 3
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                        color: "#757575"
                        text: "未勾选「手动」时不生效 —— 此时由引擎按大气/光污染自动决定。" +
                              "星表截断只在有星星可见时看得出来（白昼天空下请先关掉大气层）。"
                    }
                }
            }

            // ── 视场 ─────────────────────────────────────────────────────────
            GroupBox {
                Layout.fillWidth: true
                Layout.margins: 8
                title: "视场"

                GridLayout {
                    columns: 3
                    columnSpacing: 12
                    rowSpacing: 6
                    anchors.fill: parent

                    Label { text: "视场角" }
                    Slider {
                        objectName: "displayFovSlider"
                        Layout.fillWidth: true
                        // ⚠️ 上限是**活属性**：随投影变（探针②）。
                        from: appFacade.minFieldOfView
                        to: appFacade.maxFieldOfView
                        value: appFacade.fieldOfView
                        onMoved: {
                            appFacade.setFieldOfViewNow(value)
                            displayPage.noteRefusal()
                        }
                    }
                    Label {
                        objectName: "displayFovValue"
                        Layout.preferredWidth: 72
                        text: appFacade.fieldOfView.toFixed(2) + "°"
                        color: "#37474f"
                    }

                    Label {
                        Layout.columnSpan: 3
                        color: "#757575"
                        text: "范围 " + appFacade.minFieldOfView.toFixed(3) + "° … "
                              + appFacade.maxFieldOfView.toFixed(0) + "°（上限随投影变化）"
                    }
                }
            }

            // ── 投影 ─────────────────────────────────────────────────────────
            GroupBox {
                Layout.fillWidth: true
                Layout.margins: 8
                title: "投影（当前：" + appFacade.projectionTypeName + "）"

                Flow {
                    anchors.fill: parent
                    spacing: 6

                    Repeater {
                        objectName: "displayProjectionRepeater"
                        // 🔴 必须是**属性**（不是 `projectionTypeKeys()` 函数调用）：
                        // 合流形态先 engine.load()、后 boot() ⇒ 函数式绑定没有依赖、只求值
                        // 一次，那一次引擎还没起来 ⇒ 拿到空表 ⇒ 12 个按钮一个都不出现，
                        // 且永不重算（T38-C DP-06 实测 0/12）。
                        // 属性 + NOTIFY，由 AppFacade 引导完成时发一次"开机唤醒"驱动重算。
                        model: appFacade.projectionTypeKeys
                        delegate: Button {
                            objectName: "proj_" + modelData
                            // 刻意不用 checkable（T32/T34 同族：命令式写 checked
                            // 会销毁绑定）—— 高亮由 current 属性驱动视觉。
                            readonly property bool current:
                                appFacade.projectionTypeKey === modelData
                            text: appFacade.projectionKeyName(modelData)
                            font.bold: current
                            highlighted: current
                            onClicked: {
                                appFacade.setProjectionTypeKey(modelData)
                                displayPage.noteRefusal()
                            }
                        }
                    }
                }
            }

            // ── T39 界面字号（高 DPI）────────────────────────────────────────
            // 出处：A-1.0 范围表最后一格"必须"里的「高 DPI」。
            // ⚠️ **诚实标注作用域**（T39-A 探针实测）：
            //   · `screenFontSize` 是合流形态下**唯一可见**的旋钮 —— 它改的是引擎侧
            //     天空文本（星名、地景标签、星座名）：探针实测字号 13→40 让上游帧产生
            //     **4.054%** 的像素差异（噪声底逐位相同），还原后**逐位复原**。
            //   · `guiFontSize` / `screenButtonScale` 只作用于**老 QWidget 对话框与
            //     老 GUI 按钮**（本形态不渲染）；探针实测 QML 视觉树 325 项
            //     **一项都不跟随** ⇒ 这里**不给它们控件** —— 给一个改了没反应的
            //     滑块是假承诺，不如把作用域写清楚。
            //   · Qt Quick 界面自身的清晰度由 Qt 按**设备像素比**自动处理
            //     （探针实测合流宿主 DPR=2，引擎侧 devicePixelsPerPixel 同为 2）。
            GroupBox {
                Layout.fillWidth: true
                Layout.margins: 8
                title: "界面字号（天空文本）"

                GridLayout {
                    columns: 3
                    columnSpacing: 12
                    rowSpacing: 6
                    anchors.fill: parent

                    Label { text: "字号" }
                    Slider {
                        objectName: "displayScreenFontSlider"
                        Layout.fillWidth: true
                        stepSize: 1
                        snapMode: Slider.SnapAlways
                        from: appFacade.screenFontSizeMin
                        to: appFacade.screenFontSizeMax
                        value: appFacade.screenFontSize
                        onMoved: {
                            // ⚠️ 必须用**赋值语法**（走 Q_PROPERTY 的 WRITE）——
                            // 调 `setScreenFontSize(...)` 是调用 WRITE **函数**，
                            // 它不是 Q_INVOKABLE/slot ⇒ QML 找不到这个方法 ⇒
                            // 静默 TypeError（T39-C HP-08 真实点击抓到的缺陷；
                            // T38 的五个滑块用的都是赋值语法，无同病）。
                            appFacade.screenFontSize = Math.round(value)
                            displayPage.noteRefusal()
                        }
                    }
                    Label {
                        objectName: "displayScreenFontValue"
                        Layout.preferredWidth: 72
                        text: appFacade.screenFontSize + " px"
                        color: "#37474f"
                        font.bold: true
                    }

                    Label {
                        Layout.columnSpan: 3
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: "#757575"
                        text: "作用面：天空里的文本（星名、地景标签、星座名）。范围 "
                              + appFacade.screenFontSizeMin + "…" + appFacade.screenFontSizeMax
                              + "，默认 " + appFacade.screenFontSizeDefault + "。"
                    }

                    Label {
                        objectName: "displayScaleStatusLabel"
                        Layout.columnSpan: 3
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: "#8a8e96"
                        text: "缩放状态：设备像素比 "
                              + appFacade.devicePixelsPerPixel.toFixed(2)
                              + "｜天空缩放 " + appFacade.screenScale.toFixed(2)
                              + "｜界面缩放 " + appFacade.guiScale.toFixed(2)
                              + "。Qt Quick 界面按设备像素比自动渲染；引擎的「界面字体」"
                              + "与「按钮尺寸」只作用于旧式对话框（本形态不渲染），"
                              + "故不提供控件。"
                    }
                }
            }

            Item { Layout.fillHeight: true }
        }
    }
}
