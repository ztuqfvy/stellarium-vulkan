// MainWindow.qml — 验证宿主主窗口：承载诊断页（A1）、天空视口页（A2）与搜索/信息页（T17）。
// 禁区（docs/CODING_STANDARD.zh_CN.md 第 5 节）：本目录禁止 GL/Vulkan 特定调用。
//
// T15：新增命令栏（暂停/继续 + 视场缩放）——全部经 ActionRouter 单点分发，
//   QML 不直接触碰引擎。
// T17：新增搜索/信息页；**并修复 T15 遗留的键盘挂载缺陷**（见下方 keySink 注释）。
// T20：新增「返回天空」——I-REP-02 固定流程「开机→搜月球→定位→改时间→**返回**」
//   的最后一环。语义已定案（2026-09-27）：**返回 = 切回天空视口页 + 状态全保留**，
//   不是撤销。详见 docs/T20_RETURN_RING.zh_CN.md 与 root.returnToSky() 的注释。
// T29：修掉「Esc 绕过焦点守卫」——焦点在搜索框（含输入法组合中）时按 Esc 不再跳页。
//   详见下方 keySink 里 Esc 分支的注释（含 Qt 源码实证的冒泡路径）。
// T32：在 T29 的守卫之上给**搜索框**加**两段式 Esc**（浏览器习惯）：
//   ① 框里有文本 → 清空（留在本页）；② 已空 → 返回天空页。组合态（IME）除外。
//   适用面**只到搜索框**（时间页的 SpinBox 字段 `text` 是绑定，赋值会销毁绑定）。
//   详见 root.focusedSearchField() 与 keySink 里 Esc 分支的 T32 段。
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import StelQuickUI 1.0   // BackendInfo 单例；缺此 import 时引用处报 ReferenceError

ApplicationWindow {
    id: root
    width: 960
    height: 640
    visible: true
    title: "Stellarium Quick UI — A1/A2/A3 验证宿主"
    color: "#f5f6f8"

    // 由 C++ 上下文属性注入："diag"（诊断页）| "sky"（天空视口页）
    // | "search"（搜索/信息页）| "time"（时间页）
    property string startPage: "diag"

    // 页名 → StackLayout 索引（一处定义，切换器与 C++ 的 startPage 共用，
    // 避免"加了页面忘了改另一处"这类只在运行时才暴露的错位）。
    readonly property var pageIndex: ({ "diag": 0, "sky": 1, "search": 2, "time": 3 })

    // ── T20「返回」环的唯一实现点 ─────────────────────────────────────────────
    //
    // 语义（2026-09-27 定案，用户拍板）：
    //   **返回 = 切回天空视口页 + 状态全保留**。不改时间、不取消选中、不解除跟踪
    //   —— "返回"不是撤销。理由：I-REP-02 是"改完时间**回来看**"的用户旅程，
    //   而 I-DYN-02 的判据（改日期后星空位置变化可验证）恰好落在返回后的天空页上；
    //   旧版工具窗（DateTimeDialog 等）本来就是瞬态的，关掉即回到天空。
    //
    // 为什么**只**切 StackLayout、不走 ActionRouter/AppFacade：
    //   "页"是纯 UI 概念，引擎不知道有页这回事。把它塞进命令层只会给
    //   ActionRouter 添一条与引擎无关的假动作，还会让"单点键位路由"多出一个
    //   需要维护的例外。状态之所以"全保留"，正是因为**这条路径根本没碰引擎**。
    //
    // ⚠️ 注意：这是**唯一**的返回实现点。按钮与 Esc 都调它 —— 两个入口一条路径，
    //   否则"按钮返回"与"Esc 返回"迟早会漂移成两套语义。
    function returnToSky() {
        stack.currentIndex = root.pageIndex["sky"]
    }

    // ── T32：两段式 Esc 的**适用面 = 搜索框**（不是"所有文本框"）──────────────
    //
    // 为什么不广撒到"凡可编辑控件都两段式"（那看着更贴合"口径只有一份"）：
    // 时间页那六个输入框是 `SpinBox`，它的 `contentItem.text` 是**绑定**
    //   —— `QtQuick/Controls/Basic/SpinBox.qml:31` 写着 `text: control.displayText`。
    // QML 的赋值语义里，**给一个带绑定的属性赋值会销毁该绑定** ⇒ 字段从此空白，
    // 而且不会自己回来。那不是"清空可重打"，是把控件弄坏。
    // ⇒ 两段式的适用面**故意窄于**守卫的适用面（窄 = 保守 = 不碰没测过的东西）：
    //   守卫照旧管一切可编辑控件（它们在 Esc 下仍然只是"被尊重"：不清空、不跳页，
    //   即 T29 语义），只有搜索框多出"第一段"。
    //
    // 判"是不是搜索框"用**对象同一性**，不用控件名/类型字符串 —— 后者会随 Qt
    // 实现漂移（T17 拿 `className` 复刻守卫口径，结果守卫在真实 TextField 上变死代码、
    // 假绿 15 个任务）。同一性判法的兜底不是"希望"，而是判据：**IT-17/IT-18** 一旦
    // 发现这个条件不成立就会立刻红，不会悄悄失效。
    function focusedSearchField() {
        return (root.activeFocusItem === searchPage.queryFieldItem)
               ? searchPage.queryFieldItem : null
    }

    // ── 键盘挂载点（T17 修复 T15 的缺陷）───────────────────────────────────────
    //
    // 背景：T15 把 `Keys.onPressed` 直接写在 **ApplicationWindow** 上，但 `Keys` 是
    // **Item 的附加属性**，而 ApplicationWindow 继承自 Window、不是 Item ——
    // 那条挂载**从未生效**，运行时每份日志里都会出现：
    //     Could not attach Keys property to: MainWindow_QMLTYPE  is not an Item
    // 后果是"键盘经 ActionRouter 单点路由"在 QML 侧其实是**死代码**：C++ 侧的
    // routeKey 判据照样全绿（它是直接调用），所以仪器测不到这个缺口。
    //
    // 修法：挂到一个**真正可聚焦**的 Item 上，并让它做全部内容的父节点 ——
    // 这样即使焦点在子控件（如搜索框）里，按键仍会沿父链冒泡到这里；
    // 是否该下发给天空快捷键由 ActionRouter::routeKey 内部的焦点守卫判定（U-ACT-03），
    // QML 侧不需要、也不该重复实现这套判断。
    Item {
        id: keySink
        objectName: "skyKeySink"   // AppFacadeCheck AC-12 按此名查找挂载点
        anchors.fill: parent
        focus: true

        Keys.onPressed: (event) => {
            // T20「返回」环：Esc 在**非天空页** = 返回天空（走 root.returnToSky，
            // 与「返回天空」按钮**同一条路径**，见该函数的注释）。
            //
            // 为什么在这里拦而不是在 ActionRouter 里加一条动作：
            //   Esc 关掉当前工具页是**纯 UI 习惯**（旧版 Qt 工具窗即如此），
            //   不是引擎动作。全 `src/` 无 `Key_Escape` 字面量、无 .ui 快捷键
            //   声明 ⇒ 实测 Esc 在引擎侧空闲，在 QML 层拦掉不会造成双轨。
            //
            // 天空页上的 Esc **不拦**：照旧透传 ActionRouter（当前无绑定 ⇒
            //   routeKey 返回 false，无副作用；但保留给后续引擎动作，不抢）。
            //
            // ── T29：Esc 分支必须**服从焦点守卫** ────────────────────────────
            // 缺陷（Qt 6.11.2 源码实证，非猜测）：QQuickTextInput::processKeyEvent
            //   （qquicktextinput.cpp:4747-4797）对"未命中任何编辑键 + 无可用文本"
            //   的键走 `event->ignore()`；Esc 不在它处理的 QKeySequence 列表里
            //   （该文件全文无 `QKeySequence::Cancel`）⇒ `ignore()`
            //   ⇒ 经 QQuickDeliveryAgentPrivate::deliverKeyEvent 的
            //     `while (!e->isAccepted() && (item = item->parentItem()))`
            //     （qquickdeliveryagent.cpp:994-999）沿父链冒泡到本 sink。
            //   而 Esc 分支此前写在守卫**之前** ⇒ 焦点在搜索框时按 Esc 直接跳页，
            //   把用户半途的输入丢在搜索框里 —— 与 U-ACT-03「焦点在可编辑控件
            //   时不触发天空快捷键」自相矛盾：**守卫被绕过**。
            // 修法：先问守卫。口径只有一份，在 C++ `ActionRouter::canDispatchToSky()`
            //   （QML 侧不复刻任何判断——T17 的血泪：复刻就会漂移）。
            //
            // ── T32：两段式 Esc（在此之上加"清空"这一段）──────────────────────
            // T29 当时刻意**没做**这件事（"清空输入框"是新增交互特性、要单独立项），
            //   本轮就是那个立项。语义照浏览器习惯：
            //     ① 框里有文本 → **第一段**：清空文本，留在本页（用户要"重新输"）
            //     ② 框里已空   → **第二段**：返回天空页（用户要"离开搜索"）
            // 为什么要分两段：Esc 在输入框里同时被赋予了两个意图（放弃本次输入 /
            //   离开这个页面），一次按下去无法区分。分两段 = 让用户**表达两次**，
            //   第一段永远是"温和、可逆"的那个（清空可重打，跳页不可逆）。
            //
            // ⚠️ **组合态（IME）必须排除**：输入法组合中 Esc 是"取消候选"，
            //   此时 `text` 往往是空的（组合串在 preeditText 里、不在 text 里）
            //   ⇒ 若照走"已空 ⇒ 返回天空"，用户按 Esc 取消一次候选就会被甩回天空页。
            //   实测坐实：IT-14（组合态注入 Esc）在漏掉这一条时必红。
            //
            // ⚠️ 两段式**只对搜索框**生效（见 root.focusedSearchField 的注释：时间页的
            //   SpinBox 字段动不得）；焦点在别的可编辑控件里时，行为与 T29 完全一致。
            if (event.key === Qt.Key_Escape) {
                if (!ActionRouter.canDispatchToSky()) {
                    var edit = root.focusedSearchField()
                    if (edit && edit.inputMethodComposing !== true) {
                        if (edit.text.length > 0) {
                            edit.text = ""                      // 第一段：清空
                            event.accepted = true
                            return
                        }
                        if (stack.currentIndex !== root.pageIndex["sky"])
                            root.returnToSky()                  // 第二段：返回天空
                        event.accepted = true
                        return
                    }
                    // 组合态，或"焦在别的可编辑控件里"：维持 T29 —— 收下键、
                    // 什么都不做（不跳页、不丢上下文、不清空）。
                    event.accepted = true
                    return
                }
                if (stack.currentIndex !== root.pageIndex["sky"]) {
                    root.returnToSky()
                    event.accepted = true
                    return
                }
            }
            if (ActionRouter.routeKey(event.key, event.modifiers))
                event.accepted = true
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            // 页头：A2 开发期的临时页切换器。A4 起由真实工具栏取代，届时删除本行。
            RowLayout {
                Layout.fillWidth: true
                Layout.margins: 6
                spacing: 6

                // T20：I-REP-02 的「返回」环入口。objectName 是"最外层注入"判据的锚点。
                //   **始终可用**（即使已在天空页）——这样"在天空页点它必须什么都不发生"
                //   才是一条有检验力的负控；若此处绑 `enabled: !onSky`，负控就退化成
                //   "点了一个禁用按钮"，测不到 no-op 路径。
                Button {
                    objectName: "skyReturnButton"
                    text: "🏠 返回天空"
                    onClicked: root.returnToSky()
                }
                Rectangle { width: 1; height: 20; color: "#d0d0d0" }

                Button {
                    objectName: "navDiagButton"
                    text: "诊断页（A1）"
                    onClicked: stack.currentIndex = 0
                }
                Button {
                    objectName: "navSkyButton"
                    text: "天空视口（A2 静态图）"
                    onClicked: stack.currentIndex = 1
                }
                // T17：搜索 / 信息页（搜索 → 选择 → 信息页纵向链路）
                Button {
                    objectName: "navSearchButton"
                    text: "搜索天体（T17）"
                    onClicked: stack.currentIndex = 2
                }
                // T19：时间页（"改时间"环：6 个写入路径 + 现在 + 步进）
                Button {
                    objectName: "navTimeButton"
                    text: "时间（T19）"
                    onClicked: stack.currentIndex = 3
                }
                Item { Layout.fillWidth: true }
                Label {
                    text: "渲染后端：" + BackendInfo.runtimeApiName
                    color: BackendInfo.backendOk ? "#2e7d32" : "#c62828"
                }
            }

            // T15 命令栏：QML 按钮 → ActionRouter.trigger → AppFacade → 引擎。
            // 每命令恰执行一次（路由器单点 + 幂等闸）；无引擎形态下点击安全 no-op。
            RowLayout {
                Layout.fillWidth: true
                Layout.margins: 6
                spacing: 6

                Button {
                    text: appFacade.simulationPaused ? "▶ 继续" : "⏸ 暂停"
                    onClicked: ActionRouter.trigger("app.togglePause")
                }
                Button {
                    text: "🔍＋（放大）"
                    onClicked: ActionRouter.trigger("app.zoomIn")
                }
                Button {
                    text: "🔍－（缩小）"
                    onClicked: ActionRouter.trigger("app.zoomOut")
                }
                // T18：定位/跟踪。**不绑键位**——引擎自带的对准/跟踪快捷键
                // 经 ActionRouter 的引擎透传已经可用，这里再绑一次就是双轨。
                Button {
                    text: appFacade.tracking ? "🛑 取消跟踪" : "🎯 定位并跟踪"
                    enabled: objectInfo.hasSelection
                    onClicked: appFacade.tracking ? appFacade.setTracking(false)
                                                  : appFacade.locateSelected(true)
                }
                Item { Layout.fillWidth: true }
                Label {
                    // T18：天球上的跟踪状态。用 trackingChanged 驱动（属性变更通知），
                    // 不像右边的视场那样轮询——跟踪是离散状态，没有动画中间值。
                    color: appFacade.tracking ? "#2e7d32" : "#757575"
                    text: appFacade.tracking ? ("跟踪中：" + appFacade.trackedName)
                                             : "未跟踪"
                }
                Label {
                    // 视场显示：fieldOfView 是 Q_PROPERTY（属性读取，不能加括号调用）；
                    // zoomTo 是 0.4s 动画，用轻量定时器轮询刷新。
                    id: fovLabel
                    text: "视场：" + (appFacade.fieldOfView > 0
                                       ? appFacade.fieldOfView.toFixed(2) + "°" : "—")
                    Timer {
                        interval: 500
                        running: true
                        repeat: true
                        onTriggered: fovLabel.text = "视场：" + (appFacade.fieldOfView > 0
                                                   ? appFacade.fieldOfView.toFixed(2) + "°" : "—")
                    }
                }
            }

            StackLayout {
                id: stack
                // T20：给"最外层注入"判据当锚点 —— 返回环的判据①就是读它的
                // currentIndex（页面到底切没切回去，只能从外部观测这个量）。
                objectName: "pageStack"
                Layout.fillWidth: true
                Layout.fillHeight: true
                // 未知页名回落到诊断页（0），不静默停在错误的页上。
                currentIndex: root.pageIndex[root.startPage] !== undefined
                              ? root.pageIndex[root.startPage] : 0

                DiagnosticPage { }
                SkyTestPage { }
                SearchPage { id: searchPage }   // T32：两段式 Esc 按对象同一性认它
                TimePage { }
            }
        }
    }
}
