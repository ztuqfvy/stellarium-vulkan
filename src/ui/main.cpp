/*
 * stelQuickUI — A1 阶段主程序：Vulkan QML 宿主 + 诊断页。
 *
 * 启动顺序（计划一 A1 硬性要求）：
 *   1. 创建任何窗口之前显式 QQuickWindow::setGraphicsApi(Vulkan)。
 *   2. VkDeviceProbe 探针枚举物理设备（诊断数据）。
 *   3. 加载 QML，场景图初始化后读实际 API；非 Vulkan 则 backendOk=false，
 *      延迟数秒让诊断页显示错误后以退出码 3 结束——禁止静默回退 Metal/GL。
 *
 * 自动化验收（测试文档 P-LIF / Q- / I-STC- 系列前置）：
 *   STELQUICK_AUTOTEST_SECONDS=N → N 秒后自动退出（返回码见下）。
 *   STELQUICK_WINDOW_TEST=1      → 交互回归自测（缩放 + 隐藏/显示），见文件末尾。
 *   STELQUICK_A2_CHECK=1         → A2 静态图逐像素校验（用例 I-STC-01/02）。
 *   STELQUICK_DYN_CHECK=1        → 动态帧通路自检（I-DYN / P-BRG-01 前置 / P-BRG-04）。
 *   STELQUICK_LONGRUN=1          → 消费侧全量计量长跑（P-BRG-01 验收：warmup + measure
 *                                  两段，逐秒 + 逐帧上屏 CSV，环境前置不合规拒绝测量）。
 *                                  T13 起生产者可切：STELQUICK_LONGRUN_PRODUCER=engine
 *                                  走**真实引擎**（合流形态长跑），默认替身场景。
 *   STELQUICK_LIVE=1             → 手动查看动态帧流（人眼观察，不自动退出）。
 *   STELQUICK_LOCATE_CHECK=1     → T18 定位/跟踪自检（**C++ 侧**：守卫/锁定角距/
 *                                  推进时间判别性对照，走 AppFacade 公共 API）。
 *   STELQUICK_UI_CHECK=1         → T18 **UI 层端到端**自检：按 objectName 找真实
 *                                  QML 控件 + 向窗口投递真实鼠标事件，断言引擎状态
 *                                  因此改变（否则"QML 接线是死代码"测不出来）。
 *   STELQUICK_TIME_CHECK=1       → T19「改时间」环自检（往返恒等 / 与旧对话框公式
 *                                  逐位一致 / UTC 时区方向 / 星空随时刻变化 /
 *                                  写入不被帧泵拽回；起始页切到 "time"）。
 *   STELQUICK_TIME_UI_CHECK=1    → T19 **UI 层端到端**自检：6 个自旋框 + 应用/重置/
 *                                  现在按钮的**真实点击链路**（含负控）。
 *   STELQUICK_RETURN_UI_CHECK=1  → T20「返回」环的 **UI 层端到端**自检：返回语义
 *                                  （切回天空页 + 状态全保留，**不是撤销**）+ Esc
 *                                  返回 + 两条负控。语义定案见 MainWindow.returnToSky。
 *   STELQUICK_REPLAY_CHECK=1     → T20 **I-REP-02 全流程回放**（A-alpha 出口测试）：
 *                                  开机→搜月球→定位→改时间→返回，全程只投递真实
 *                                  鼠标事件；末态四连断言（页面/时间/跟踪/星空）。
 *   STELQUICK_INTERACT_UI_CHECK=1 → T25/T27/T28/T29 **交互级**自检：窗口真实滚轮
 *                                  （滚轮缩放活链 + 页守卫负控 + 方向对照）、真实
 *                                  L 键（routeKey QML 活链 + 焦点守卫 U-ACT-03）、
 *                                  真实鼠标（点击选中 / 拖拽平移 / 右键反选）、
 *                                  真实原生捏合（捏合缩放 + 方向对照 + 页守卫负控）与
 *                                  真实输入法事件（preedit/commit 活链 + 组合期间按键
 *                                  不得抢 + Esc 走守卫不跳页 + 判别性对照）。
 *   STELQUICK_LOC_PROBE=1        → T33-A **地点数据面探针**（见 app/LocationProbe.hpp）：
 *                                  只报读数、**不下 PASS/FAIL** —— cwd/installDir/
 *                                  findFile 命中、地点库条数、locationForString 解析、
 *                                  瞬时写入后的回读与时区联动、引擎 isValid 边界。
 *                                  目的：先证数据面在合流 bundle 布局下可用，再写 UI
 *                                  （T24「翻译从未加载」潜伏 14 个任务的同款风险）。
 *   STELQUICK_LOC_CHECK=1        → T33-C **地点写入面自检**（见 app/LocationCheck.hpp）：
 *                                  范围闸纯谓词（恒可跑）+ 活引擎腿（写入生效/时区联动/
 *                                  判别腿/天极高度 = 观测者纬度 的天文学恒等式/非法值无
 *                                  副作用/not-found/往返/计数）。起始页切到 "location"。
 *                                  10 条判据。负控开关：`STELQUICK_LOC_NODELAY=1`
 *                                  （写入步不给延迟）+ `STELQUICK_LOC_GATE_OFF=1`
 *                                  （关就绪门）——两个一起开复现间歇红，只开前者由门兜住。
 *   STELQUICK_CONFIG_CHECK=1     → T36-C **配置目录隔离自检**（见
 *                                  app/ConfigIsolationCheck.hpp）：目录独立/配置与日志
 *                                  落点/种子完整/写侧不触原版目录（哨兵成对 + 产品
 *                                  路径成对）。8 条判据。**不启帧泵**。
 *                                  负控开关：`STELQUICK_CFG_ISOLATE_OFF=1`（关隔离）
 *                                  + `STELQUICK_CFG_MIGRATE_OFF=1`（关播种）。
 *   STELQUICK_NIGHT_PROBE=1      → T37-A **夜视链路数据面探针**（见
 *                                  app/NightModeProbe.hpp）：只报读数、**不下 PASS/FAIL**。
 *                                  冻结仿真后，用**两条独立读回路径**（FrameMailbox 原始
 *                                  RGBA / QQuickWindow::grabWindow）测夜视 OFF↔ON 的像素
 *                                  差异，并以星座线 OFF↔ON 作**判别性对照**。目的：先证
 *                                  "引擎旧夜视后处理在合流形态的读回帧上可不可见"，
 *                                  再决定夜视是复用引擎还是在 Qt Quick 侧自实现。
 *   STELQUICK_DISPLAY_PROBE=1    → T38-A **显示参数命令面探针**（见
 *                                  app/DisplayProbe.hpp）：亮度/星等（StelSkyDrawer
 *                                  Q_PROPERTY 群）/ 视场（StelMovementMgr）/ 投影
 *                                  （StelCore 12 个 key）的**写读往返 + 夹取语义 +
 *                                  非法值落点**，只报读数、**不下 PASS/FAIL**
 *                                  （断言在 T38-C 的 DisplayCheck）。
 *   STELQUICK_DISPLAY_CHECK=1    → T38-C **显示参数自检**（见 app/DisplayCheck.hpp）：
 *                                  判据 DP-00..DP-11 —— 状态面写读往返 / 范围闸（含
 *                                  视场上限随投影变化）/ 投影 12 key + 白名单闸 / UI
 *                                  控件存在性与绑定 / 视场与投影的**帧效果**（含判别
 *                                  对照）/ 星等截断 / 全量复原。起始页停在 "sky"。
 *                                  负控：`STELQUICK_DISPLAY_GATE_OFF=1`（范围闸与
 *                                  白名单闸旁路）⇒ 预期恰红 [DP-02,DP-04]；
 *                                  `STELQUICK_DISPLAY_FWD_OFF=1`（断引擎→façade
 *                                  转发）⇒ 预期恰红 [DP-07]。
 *   STELQUICK_HIDPI_PROBE=1      → T39-A **高 DPI + 渲染诊断数据面探针**（见
 *                                  app/HiDpiProbe.hpp）：screenFontSize / guiFontSize /
 *                                  screenButtonScale 三属性的写读往返 + 夹取语义 +
 *                                  派生量（getScreenScale / getGuiScale）算术验算 +
 *                                  `getGuiFontSize()` 的来源（是否读全局 app font）+
 *                                  `setGuiFontSize()` 对 QML 视觉树的波及面 +
 *                                  screenFontSize 的帧级效应 + FrameMailbox::Stats
 *                                  运行时诊断量。只报读数、**不下 PASS/FAIL**
 *                                  （断言在 T39-C 的 HiDpiCheck）。
 *   STELQUICK_HIDPI_CHECK=1      → T39-C **高 DPI + 渲染诊断自检**（见
 *                                  app/HiDpiCheck.hpp）：判据 HP-00..HP-11。
 *                                  负控复用 T38 两个开关：`STELQUICK_DISPLAY_GATE_OFF=1`
 *                                  ⇒ 预期恰红 [HP-02,HP-03,HP-04]；
 *                                  `STELQUICK_DISPLAY_FWD_OFF=1` ⇒ 预期恰红
 *                                  [HP-07,HP-08]。起始页停在 "sky"。
 *   STELQUICK_LEGACY_HOST_TEST=1 → A2 主体 T6 自检：旧宿主显式帧驱动 + 读回。
 *                                  **在创建任何窗口之前**同步执行、不进入事件循环。
 * 退出码：0 正常；2 窗口创建失败；3 后端校验失败（实际 API 非 Vulkan）；4 交互自测失败；
 *         5 A2 静态图校验失败；6 A2 校验手段不可用（不得据此声称通过）；
 *         7 被测可执行文件不存在（Windows run_autotest.cmd）；8 T6 显式帧驱动自检失败
 *           （DYN 动态帧通路自检 / 长跑判据失败同用 8）；
 *         9 长跑环境前置不合规（未接电源 / 低电量模式开启，未开始测量）。
 *
 * 运行：直接双击 stelQuickUI.app 即可（main.cpp 自动定位 Vulkan 加载库）。
 */
#include "ui/LegacyHostCheck.hpp"

#include <QGuiApplication>
#if defined(STELQUICK_WIDGETS_HOST)
// A3 前置探针（2026-09-23）：宿主换成 QApplication（QGuiApplication 的严格超集）。
// 动机：引擎**现成的**无头引导路径 LegacyAppCheck A3-C01 依赖
//   new StelMainView(...) + show() + WA_DontShowOnScreen → initializeGL → StelApp::init
// 而 StelMainView : public QGraphicsView 是 Widgets 类；QGuiApplication 下创建
// 任何 QWidget 会直接 abort（"Cannot create a QWidget without QApplication"）。
// 本形态由 CMake 选项 STELQUICKUI_WIDGETS_HOST 开启，默认关闭以保持既有基线。
#include <QApplication>
#endif
#if defined(STELQUICK_HAS_ENGINE)
// A3 前置探针：引擎无头引导所需的头（使用面照抄 src/main.cpp）
#include "StelMainView.hpp"
#include "core/StelApp.hpp"
#include "core/StelCore.hpp"
#include "core/StelUtils.hpp"   // T19：getJDFromDate / getJDFromSystem（算期望 JD，不另发明天文换算）
// T20：返回环与 I-REP-02 回放要量"星空动了多少"——判据必须用**可验算的观测量**
// （目标的天平坐标），而不是"时间源已被写入"这种自证。与 TimeCheck 用同一口径。
#include "core/StelObject.hpp"
#include "core/StelObjectMgr.hpp"
#include "core/StelMovementMgr.hpp" // T27：IT-07 拖拽判据读 getViewDirectionJ2000
#include "core/StelProjector.hpp"  // T27：DIAG 投影视口尺寸与月球星下点像素
#include "core/VecMath.hpp"
#include <QKeyEvent>            // T20：Esc 判据要投递**真实**键盘事件（先例见 AppFacadeCheck AC-12）
#include <QDateTime>            // T22：按偏移挑时区候选（与"跑在哪一天"解耦）
#include <QTimeZone>            // T22
#include <QColor>               // T30：视觉层判据读控件声明色与有效背景色算对比度
#include <cmath>                // T30：std::pow（WCAG 亮度分量）
#include "core/StelFileMgr.hpp"
#include "core/StelTranslator.hpp"
#include "core/StelIniParser.hpp"
#include "StelLogger.hpp"
// T11 核心机制验证（C-05..C-07）：借引擎上下文 + 真实引擎帧驱动 + 读回。
// LegacySkyHost 的 GlContextMode::kBorrowed 就是为"与旧宿主同进程"预留的路径。
#include "render/legacy/FrameMailbox.hpp"
#include "render/legacy/LegacySkyHost.hpp"
#include <QDir>
#include <QEventLoop>
#include <QImage>
#include <QSettings>
#include <QThread>
#ifdef Q_OS_WIN
#include <io.h> //!< MSVC：unistd.h 的等价子集（isatty 等）
#else
#include <unistd.h>
#endif
#endif
#include <QElapsedTimer>
#include <QSet>                 // T20：RP-04b 的结果去重检查
#include <QFileInfo>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QMouseEvent>
#include <QInputMethodEvent>    // T29：合成输入法 preedit/commit 事件（真实投递链入口）
#include <QPointingDevice>      // T28：合成 QNativeGestureEvent 需要"主指针设备"
#include <QSGRendererInterface>
#include <QTimer>
#include <QUrl>
#include <QVulkanInstance>
// 私有头：取 Qt 全局唯一的默认 Vulkan 实例（避免应用自建实例与 Qt 内部实例并存，
// 见下方"QVulkanInstance 的来源"注释）。Qt6::GuiPrivate 已在 CMakeLists 中链接。
#include <QtGui/private/qvulkandefaultinstance_p.h>
#include <QtMath>
#include <cstdio>
#include <memory>

namespace {

// ── macOS + Vulkan 渲染兼容开关（2026-09-18 实测）─────────────────────────
//
// 故障：Qt 6.11.2 默认的线程化渲染循环下，macOS 的 QMetalLayer 显示锁
//   （qtbase/src/gui/platform/darwin/qmetallayer.mm）每帧触发
//   "Timed out waiting for display lock"（5 秒超时），rhi->endFrame()
//   被阻塞 5008ms → 窗口缩放 / Command+Tab 切换 / Command+Q 退出各卡 1-2 秒，
//   期间图层用旧 drawable 填充新 bounds 呈现"拉伸而非等比例缩放"。
//   实测 12 秒只渲染 3 帧（0.25 FPS）。
//
// 处置：应用 Qt 上游提交 9122d826 预留的官方逃生门（该提交引入了这套锁机制，
//   并明确保留 QT_MTL_NO_TRANSACTION 作为"万一机制出问题时的退出选项"）。
//   默认保留线程化渲染循环，仅关闭锁定的 Metal 图层路径。
//
// 覆盖（用于 A/B 测量，见 docs/BUILD_RECORD.zh_CN.md）：
//   STELQUICK_RENDER_WORKAROUND=transaction-off（默认）→ QT_MTL_NO_TRANSACTION=1
//   STELQUICK_RENDER_WORKAROUND=basic-loop            → QSG_RENDER_LOOP=basic
//   STELQUICK_RENDER_WORKAROUND=none                  → 不干预（复现原始故障）
void applyMacOsVulkanWorkaround()
{
#ifdef Q_OS_MACOS
    QByteArray mode = qgetenv("STELQUICK_RENDER_WORKAROUND");
    if (mode.isEmpty())
        mode = "transaction-off";

    if (mode == "transaction-off") {
        // 用户已显式设置 QT_MTL_NO_TRANSACTION 时尊重之
        if (!qEnvironmentVariableIsSet("QT_MTL_NO_TRANSACTION"))
            qputenv("QT_MTL_NO_TRANSACTION", "1");
    } else if (mode == "basic-loop") {
        qputenv("QSG_RENDER_LOOP", "basic");
    } else {
        mode = "none";
    }
    std::printf("STELQUICK: macOS 渲染兼容模式 = %s\n", mode.constData());
    std::fflush(stdout);
#endif
}

// 自动定位 Vulkan 加载库：QT_VULKAN_LIB 未设时按候选路径探测。
// 目的：双击/直接运行也能起来，不要求用户手动 export（2026-09-18 用户反馈）。
// 顺序：环境变量 > 应用包内 Frameworks > Homebrew/本地安装路径。
void ensureVulkanLoaderPath()
{
#ifdef Q_OS_MACOS
    if (!qEnvironmentVariableIsEmpty("QT_VULKAN_LIB"))
        return; // 用户显式指定，尊重

    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        appDir + QStringLiteral("/../Frameworks/libvulkan.1.dylib"), // 打包后的自包含位置
        QStringLiteral("/opt/homebrew/opt/vulkan-loader/lib/libvulkan.1.dylib"),
        QStringLiteral("/opt/homebrew/lib/libvulkan.1.dylib"),
        QStringLiteral("/usr/local/lib/libvulkan.1.dylib"),
    };
    for (const QString &path : candidates) {
        if (QFileInfo::exists(path)) {
            qputenv("QT_VULKAN_LIB", path.toUtf8());
            std::printf("STELQUICK: 自动定位 Vulkan 加载库 %s\n", path.toUtf8().constData());
            std::fflush(stdout);
            return;
        }
    }
#endif // Q_OS_MACOS
}

} // namespace

#include "ui/quick/BackendInfo.hpp"
#ifdef STELQUICK_VULKAN_PROBE
#include "render/vulkan/VkDeviceProbe.hpp"
#endif
// A2：静态帧通路（邮箱 → 视口 → 上屏 → 自动像素校验）
#include "render/legacy/FrameMailbox.hpp"
#include "render/legacy/StaticFrameSource.hpp"
#include "ui/A2FrameCheck.hpp"
#include "ui/DynFrameCheck.hpp"
#include "ui/IFrameProducer.hpp"
#include "ui/LiveFrameSource.hpp"
#include "ui/SkyLongRun.hpp"
#include "ui/quick/SkyViewport.hpp"
// T15：命令通路（AppFacade + ActionRouter + 自检）。三者在无引擎形态下也是
// 可编译的 no-op/注册表路径，无条件包含。
#include "app/AppFacade.hpp"
#include "app/ActionRouter.hpp"
#include "app/AppFacadeCheck.hpp"
// T17：搜索/选择模型 + 自检。模型是"数据"，命令走 AppFacade——
// QML 侧约定 `model: searchResults` / `appFacade.searchObjects(...)`。
#include "app/SearchModelCheck.hpp"
#include "app/SearchResultsModel.hpp"
#include "app/ObjectInfoModel.hpp"
// T18：定位/跟踪（A4 固定流程的"定位"环）+ 自检。
#include "app/LocateCheck.hpp"
#include "app/LocationProbe.hpp"
#include "app/LocationCheck.hpp"
#include "app/ToolbarProbe.hpp"
#include "app/ToolbarCheck.hpp"
#include "app/TimeLinkProbe.hpp"
#include "app/TimeLinkCheck.hpp"
// T37-A：夜视链路数据面探针（引擎旧后处理在合流形态的读回帧上到底可不可见）。
// 只报读数、不下 PASS/FAIL —— 断言在 T37-C 的 NightModeCheck。
#include "app/NightModeProbe.hpp"
#include "app/NightModeCheck.hpp"
// T38-A：显示参数命令面探针（亮度/星等 · 视场 · 投影 —— 写读往返 / 夹取语义 /
// 非法值落点）。只报读数、不下 PASS/FAIL（断言在 T38-C 的 DisplayCheck）。
#include "app/DisplayProbe.hpp"
// T38-C：显示参数自检（判据 DP-00..DP-11 —— 状态面往返 / 范围闸 / 投影白名单 /
// UI 控件存在性与绑定 / 视场与投影的**帧效果** + 判别对照 / 星等截断 / 复原）。
#include "app/DisplayCheck.hpp"
// T39-A：高 DPI + 渲染诊断数据面探针（screenFontSize / guiFontSize /
// screenButtonScale 的往返与夹取、派生量算术验算、getGuiFontSize 的来源、
// setGuiFontSize 对 QML 视觉树的波及面、FrameMailbox::Stats）。只报读数。
#include "app/HiDpiProbe.hpp"
// T39-C：高 DPI + 渲染诊断自检（判据 HP-00..HP-11 —— 状态面往返 / 范围闸 /
// 派生量算术验算 / UI 控件与绑定（含"交互后绑定仍活"）/ 字号帧效应成对 + 判别
// 对照 / 诊断数据面独立验算 / 复原）。
#include "app/HiDpiCheck.hpp"
// T40-A：快捷键编辑命令面探针（QKeySequence 往返恒等 / setShortcut 即时性与信号 /
// saveShortcuts 落盘语义与"不写穿" / 空串=移除 / 新 QSettings 实例重读（等价重启）/
// restoreDefaultShortcut 的波及面 / 冲突数据面两支语义相反 / actionsEnabled 门 /
// 平台键位格式 / 收尾还原）。只报读数。
#include "app/ShortcutProbe.hpp"
// T40-B：快捷键编辑的模型与命令面（读表 / 改键 / 冲突 / 持久化；键名生成的单一真源）。
#include "app/ShortcutModel.hpp"
// T40-C：快捷键编辑自检（判据 SC-01..SC-14 —— 表完整性与行数据一致性 / 键名生成
// 单点（含"PageUp 别名静默变空"的判别对照）/ 改键写引擎与落盘（新 QSettings 实例 +
// 序列语义）/ 不写穿 / 冲突检出与消解 / 恢复默认不冲别人 / 空串移除 / 非法键名闸 /
// 全部恢复 / UI 控件与真实点击+真实按键的交互端到端 / 复原）。
#include "app/ShortcutCheck.hpp"
// T41-A：帮助/版本/许可证**数据面探针**（版本四件套与编译期宏 / 运行环境串 /
// 编译期vs运行期 Qt / 贡献者名单 / 🔴 COPYING 在运行时的可达性（源码树·StelFileMgr
// 各目录·app bundle·qrc）/ 🔴 帮助动作悬空裁决（老 8 个窗口动作全注册在未编译的
// src/gui/StelGui.cpp ⇒ F1 是否有主）/ "Windows" 组之谜 / 帮助页两半数据源规模 /
// 翻译串可达性）。只报读数、零写入。
#include "app/HelpProbe.hpp"
#include "app/HelpModel.hpp"   // T41-B：帮助/版本/许可证数据面
#include "app/HelpCheck.hpp"   // T41-C：帮助/版本/许可证自检
// T36：配置目录隔离（引导期）与其自检。**引导期**那一半由 LiveSkyRuntime::boot()
// 调用——位置即判据，别挪到 StelLogger / config.ini 之后。
#include "app/ConfigIsolationCheck.hpp"
// T19：改时间（A4 固定流程的"改时间"环）+ 自检。
#include "app/TimeCheck.hpp"
// T16：单一仿真时钟。控制器是 core 层的纯逻辑类（无 GL / 无 QObject），
// 故无条件包含——STELQUICK_CLOCK_CHECK 分支在独立工程形态下同样可用。
#include "core/StelClockController.hpp"
#if defined(STELQUICK_HAS_ENGINE) && defined(STELQUICK_WIDGETS_HOST)
// T11：真实引擎进程内帧驱动（替代 LiveFrameSource 的"真实引擎"形态）
#include "ui/LiveSkyRuntime.hpp"
// T15：合流形态拆除 StelAction 的 QWidget 注册假设（setWidgetShortcutDispatchEnabled）
#include "StelActionMgr.hpp"
#endif

namespace {

const char *apiName(QSGRendererInterface::GraphicsApi api)
{
    switch (api) {
    case QSGRendererInterface::Vulkan: return "Vulkan";
    case QSGRendererInterface::OpenGL: return "OpenGL";
    case QSGRendererInterface::Metal: return "Metal";
    case QSGRendererInterface::Direct3D11: return "Direct3D11";
    case QSGRendererInterface::Software: return "Software";
    default: return "Unknown";
    }
}

// ══════════════════════════════════════════════════════════════════════════
// 引擎相关操作前的**顺序纪律**：先让 QML 场景图完成首次渲染，再碰引擎。
//
// 2026-09-23 T11 实测（代价不小，务必别丢）：顺序反了（先引导引擎、后让场景图
// 初始化）会让 QML 的图形后端从**请求值漂移**（setGraphicsApi(Metal) → 运行时
// runtimeApi=Vulkan），进而触发 MoltenVK 静态纹理黑屏缺陷——视口全黑，第一眼
// 会被误判成"引擎帧没投递"。
//
// 注意必须用 window->update() **主动请求重绘**：Qt Quick 是按需渲染，静态页面
// 渲完就停，不请求就永远等不到 isSceneGraphInitialized()。
// ══════════════════════════════════════════════════════════════════════════
void warmUpSceneGraph(QQuickWindow *window, int timeoutMs = 5000)
{
    QElapsedTimer warm;
    warm.start();
    while (warm.elapsed() < timeoutMs && !window->isSceneGraphInitialized())
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        window->update();
    }
    // 场景图就绪后再给 300ms 让首帧真正渲出来（RHI 设备在首次渲染时创建）
    QElapsedTimer settle;
    settle.start();
    while (settle.elapsed() < 300)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        window->update();
    }
}

// ══════════════════════════════════════════════════════════════════════════
// 引擎接管 GPU 前的**静默期**：让 QML 场景图把已排队的渲染做完、并进入空闲。
//
// 2026-09-23 T13 实测（根因，务必别丢）：warmUpSceneGraph 的最后 300ms 一直在
// `window->update()`，**退出时 QML 仍有在途的渲染请求**；紧接着 boot 引擎
// （`new StelMainView` + show → initializeGL 建重 GL 资源）、再启动帧泵首帧
// `stelApp.draw()`（初始化 planets shaders），于是 QML 的 Vulkan/MoltenVK 提交与
// 引擎的 OpenGL/Metal 提交在 GPU 上**并发**，MoltenVK 侧丢设备：
//
//   vkDebug: VK_ERROR_OUT_OF_DEVICE_MEMORY ... kIOGPUCommandBufferCallbackErrorPageFault
//   Device loss detected in vkWaitForFences()
//   Graphics device lost, cleaning up scenegraph and releasing RHI
//
// 丢一次本身能自愈（18:22 那次丢了照样跑完 300s），但**重建撞上 Metal 熔断**
// （`SubmissionsIgnored (for causing prior/excessive GPU errors)`）就变成
// `Failed to create swapchain: -4` 直接死。判别证据（同一构建同机器）：
//   · 纯引擎路径（STELQUICK_LEGACY_HOST_TEST，无 QML）   → 14/14 PASS，0 次丢设备
//   · 纯 QML 路径（替身生产者，无引擎）                   → PASS 54.5fps，0 次丢设备
//   · 两条路径**同时**跑（engine 生产者）                 → 必丢设备，重建成败随机
//
// 处置与实测边界（**重要，别把结论读歪**）：
//   · 加静默期**不足以**避免丢设备：实测（QUIESCE_MS=800）仍丢 2 次。它的
//     实际价值是把结局从"Qt 内部致命路径 → SIGSEGV(139)"变成"RHI 重建失败 →
//     场景图瘫掉、QML 显示 0fps，但进程活着、判据可读（退出码 8）"——失败可控。
//   · **真因是 MoltenVK**：同一份合流场景，QML 用 Vulkan/MoltenVK 后端丢设备
//     4/4 次（重建成败随机），换原生 Metal RHI（STELQUICK_GRAPHICS_API=metal）
//     丢设备 0/2 次、11/11 判据全绿。⇒ 属 MoltenVK 与 Apple-OpenGL 同进程共存
//     的缺陷，不是本项目代码问题。详见 docs/BUILD_RECORD.zh_CN.md 的 T13 节。
// 时长可用 STELQUICK_QUIESCE_MS 覆盖（诊断用；默认 800ms）。
// ══════════════════════════════════════════════════════════════════════════
void quiesceSceneGraph(int ms)
{
    QElapsedTimer quiet;
    quiet.start();
    while (quiet.elapsed() < ms)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
}

//! 静默时长（ms）：环境变量 STELQUICK_QUIESCE_MS 覆盖，默认 800。
int quiesceMs()
{
    const int v = qEnvironmentVariableIntValue("STELQUICK_QUIESCE_MS");
    return v > 0 ? v : 800;
}

#if defined(STELQUICK_HAS_ENGINE) && defined(STELQUICK_WIDGETS_HOST)
// engine 生产者的**设计产能**（判据下限的基数，也是帧泵名义速率）。
// 为什么不是 UI 刷新上限 60fps：真实引擎每帧要跑完整场景（星表 + DSO + 大气 +
// 地面）再 glReadPixels 读回，开销远超 16.7ms。2026-09-23 T12 实测：稳态在
// 35~52 fps 区间随机器负载波动，且**需约 10s 才能从 warmup 爬到稳态**。
// 取 50 作为产能档，判据下限 0.6×50 = 30 fps 留出合理余量。
constexpr double kEngineNominalFps = 50.0;

// 从环境变量构造引擎帧泵配置（LIVE_FPS / LIVE_SIZE / LIVE_SIMRATE）。
// simRate 与 fps 的默认值由调用方给，因为两种用法的需求不同：
//   · LIVE_ENGINE 手动查看 → simRate 0.02（1 天 / 50s，接近真实观感）
//   · DYN 自检 engine 生产者 → simRate 0.1（8~20 秒走足够天数，保证 D1-C06
//     "两次抓帧不同"有足够像素位移，不靠运气）
stelapp::LiveSkyRuntime::Config engineConfigFromEnv(double defaultSimRate,
                                                    double defaultFps = 60.0)
{
    stelapp::LiveSkyRuntime::Config cfg;
    const QByteArray fpsEnv = qgetenv("STELQUICK_LIVE_FPS");
    if (!fpsEnv.isEmpty())
        cfg.fps = fpsEnv.toDouble();
    else
        cfg.fps = defaultFps;
    const QByteArray sizeEnv = qgetenv("STELQUICK_LIVE_SIZE");
    if (!sizeEnv.isEmpty())
    {
        const QList<QByteArray> wh = sizeEnv.split('x');
        if (wh.size() == 2)
            cfg.renderSize = QSize(wh.at(0).toInt(), wh.at(1).toInt());
    }
    const QByteArray simEnv = qgetenv("STELQUICK_LIVE_SIMRATE");
    cfg.simRate = simEnv.isEmpty() ? defaultSimRate : simEnv.toDouble();
    return cfg;
}
#endif

// ══════════════════════════════════════════════════════════════════════════
// 交互回归自测：STELQUICK_WINDOW_TEST=1
//
// 把 A1 的手动验收项「缩放、关闭、重开无错」变成可重复的自动测量，
// 不依赖人工点击。测量三个指标（直接对应"卡顿"的主观感受）：
//   maxStallMs   — 主线程事件循环最大停顿（16ms 心跳定时器的实测延迟）
//   maxResizeMs  — 单次窗口几何变更阻塞主线程的最长时间
//   maxFrameGap  — 相邻两帧之间最长的间隔（渲染节流/阻塞的直接体现）
// 判定阈值 250ms：健康的窗口交互应是个位数毫秒；出现 5 秒锁超时时必然超标。
// ══════════════════════════════════════════════════════════════════════════
constexpr qint64 kStallThresholdMs = 250;

struct InteractionTest
{
    QQuickWindow *window = nullptr;
    int baseW = 0;
    int baseH = 0;
    int steps = 24;          // 放大/缩小各 steps 次
    int phase = 0;           // 0 放大 / 1 缩小 / 2 隐藏显示
    int index = 0;
    int warmupFrames = 0;    // 预热帧计数：首帧要编译着色器/字形图集，不计入
    bool measuring = false;
    qint64 maxStallMs = 0;
    qint64 maxResizeMs = 0;
    qint64 maxFrameGapMs = 0;
    QElapsedTimer lastBeat;
    QElapsedTimer lastFrame;
    QTimer *timer = nullptr;
};

void finishInteractionTest(QGuiApplication *app, InteractionTest *test);

// 单步推进（每次 QTimer 触发执行一步），步间留出时间让渲染真正发生
void advanceInteractionTest(QGuiApplication *app, std::shared_ptr<InteractionTest> test)
{
    // 事件循环停顿测量：心跳回调本身被延迟多少，就是主线程被阻塞了多久
    test->maxStallMs = qMax(test->maxStallMs, test->lastBeat.restart());
    test->maxFrameGapMs = qMax(test->maxFrameGapMs, test->lastFrame.restart());

    if (test->phase == 2) {
        if (test->index >= 3) {
            finishInteractionTest(app, test.get());
            return;
        }
        QElapsedTimer block;
        block.start();
        if (test->index % 2 == 0)
            test->window->hide();   // 关闭代理：隐藏窗口（真·关闭重开见 P-LIF ×100 回归）
        else
            test->window->show();
        test->maxResizeMs = qMax(test->maxResizeMs, block.elapsed());
        ++test->index;
        return;
    }

    const int n = test->steps;
    const int i = test->phase == 0 ? test->index : (n - 1 - test->index);
    const int w = test->baseW + i * 20;
    const int h = test->baseH + i * 15;

    QElapsedTimer block;
    block.start();
    test->window->setGeometry(test->window->x(), test->window->y(), w, h);
    test->maxResizeMs = qMax(test->maxResizeMs, block.elapsed());

    ++test->index;
    if (test->index >= n) {
        test->index = 0;
        ++test->phase;
        std::printf("WINDOWTEST: 阶段 %d 完成（累计 maxStall=%lldms maxResize=%lldms）\n",
                    test->phase, test->maxStallMs, test->maxResizeMs);
        std::fflush(stdout);
    }
}

void finishInteractionTest(QGuiApplication *app, InteractionTest *test)
{
    test->timer->stop();
    const bool pass = test->maxStallMs <= kStallThresholdMs
                   && test->maxResizeMs <= kStallThresholdMs
                   && test->maxFrameGapMs <= kStallThresholdMs;
    std::printf("WINDOWTEST: 结果 steps=%d maxStallMs=%lld maxResizeMs=%lld maxFrameGapMs=%lld 阈值=%lldms VERDICT=%s\n",
                test->steps * 2 + 6,
                test->maxStallMs, test->maxResizeMs, test->maxFrameGapMs,
                kStallThresholdMs, pass ? "PASS" : "FAIL");
    std::fflush(stdout);
    app->exit(pass ? 0 : 4);
}

void runInteractionTest(QGuiApplication *app, QQuickWindow *window, int baseW, int baseH)
{
    auto test = std::make_shared<InteractionTest>();
    test->window = window;
    test->baseW = baseW;
    test->baseH = baseH;
    const int envSteps = qEnvironmentVariableIntValue("STELQUICK_WINDOW_TEST_STEPS");
    if (envSteps > 0)
        test->steps = envSteps;

    test->lastBeat.start();
    test->lastFrame.start();

    auto *heartbeat = new QTimer(app);
    heartbeat->setInterval(16); // 16ms ≈ 60Hz，被延迟多少 = 主线程停顿多少
    QObject::connect(heartbeat, &QTimer::timeout, app, [test]() {
        if (test->measuring)
            test->maxStallMs = qMax(test->maxStallMs, test->lastBeat.restart());
        else
            test->lastBeat.restart();
    });
    heartbeat->start();

    test->timer = new QTimer(app);
    test->timer->setInterval(60); // 每步 60ms，留出 3-4 帧
    QObject::connect(test->timer, &QTimer::timeout, app, [app, test]() {
        advanceInteractionTest(app, test);
    });

    // 帧间隔：直接挂在 QQuickWindow::frameSwapped 上。
    // 预热 3 帧（着色器/字形图集/pipeline cache 首次编译）不计入测量。
    QObject::connect(window, &QQuickWindow::frameSwapped, app, [test]() {
        if (!test->measuring) {
            if (++test->warmupFrames < 3)
                return;
            test->measuring = true;
            test->lastBeat.restart();
            test->lastFrame.restart();
            test->timer->start();
            std::printf("WINDOWTEST: 预热 %d 帧完成，开始测量\n", test->warmupFrames);
            std::fflush(stdout);
            return;
        }
        test->maxFrameGapMs = qMax(test->maxFrameGapMs, test->lastFrame.restart());
    });

    std::printf("WINDOWTEST: 开始（放大 %d 步 → 缩小 %d 步 → 隐藏/显示 3 次）\n",
                test->steps, test->steps);
    std::fflush(stdout);
}

#if defined(STELQUICK_HAS_ENGINE) && defined(STELQUICK_WIDGETS_HOST)
// ══════════════════════════════════════════════════════════════════════════
// UI 层端到端自检：STELQUICK_UI_CHECK=1
//
// **为什么必须单独有这一条**（T15 血泪教训的同类风险，见 MEMORY 陷阱 3）：
//   LocateCheck（src/app）走的全是 AppFacade 的 C++ 公共 API。它证明得了"定位
//   逻辑对"，但**证明不了** QML 里的 `onClicked: appFacade.locateSelected(true)`
//   是活的 —— 万一名字写错、或按钮压根没接上，C++ 侧自检照样 14/14 全绿。
//   T15 正是这么栽的：`Keys` 是 **Item** 的附加属性，挂在 `ApplicationWindow`
//   （继承 Window）上从未生效，QML 键盘路由整段是死代码，而 AC-5 抓不到
//   （它直接调 routeKey）——埋了两个任务才被人肉发现。
//
// 所以本检查**从最外层注入**：
//   1. 按 `objectName` 找到**真实的 QML 控件**（找不到即接线断了）；
//   2. 向 QQuickWindow 投递**真实的鼠标按下/抬起事件**（不是直接 emit clicked()、
//      也不是直接调 AppFacade —— 那两种都测不到 onClicked 这段接线）；
//   3. 断言**引擎状态真的变了**（isTracking / trackedName）；
//   4. 反向也验：读 QML 控件的真实属性（enabled / Label.text），证明 C++ → QML
//      的绑定同样活着，而不是硬编码。
//
// 负控（UI-06）：往一个**非按钮**控件（状态 Label）中心点一下，tracking 必须纹丝不动。
//   没有这一条，UI-04 的 PASS 也可能只是"点哪都算"。
//
// 判据 10 条：UI-01..10（T23 新增 UI-09/UI-10：「清除选中」unSelect 路径的
// 活引擎腿）。退出码沿用既有约定：0=PASS / 10=FAIL / 6=UNAVAILABLE
//   （无可用 fixture —— 环境条件，不是接线缺陷，照实报而不伪装通过）。
// ══════════════════════════════════════════════════════════════════════════

struct UiLocateCheck
{
    QQuickWindow *window = nullptr;
    stelapp::AppFacade *facade = nullptr;
    QQuickItem *locateButton = nullptr;
    QQuickItem *untrackButton = nullptr;
    QQuickItem *statusLabel = nullptr;
    QQuickItem *clearSelButton = nullptr;   // T23：「清除选中」（走 unSelect 的真实 UI 路径）
    QStringList details;
    int passed = 0;
    int total = 0;
    int phase = 0;
    int tickMs = 300;        // 相位间留一拍：让布局 polish 与 QML 重绑定真正发生
    QString expectName;      // 被选中天体的显示名 —— trackedName 的期望值
    QString trackedBefore;   // 负控前的 trackedName 快照
    QTimer *timer = nullptr;
};

void uiLocateMark(UiLocateCheck *c, bool ok, const QString &line)
{
    ++c->total;
    if (ok)
        ++c->passed;
    c->details.append(line
                      + (ok ? QStringLiteral("：OK") : QStringLiteral("：FAIL")));
}

//! 控件中心点在**场景坐标**（= 窗口坐标系）里的位置。
QPointF uiLocateCenter(QQuickItem *item)
{
    return item->mapToScene(QPointF(item->width() / 2.0, item->height() / 2.0));
}

// ── 布局就绪门（T22 新增；T35 放宽一档）───────────────────────────────────────
//! 有界等待上限：25 × 100ms = 2.5s。超过就**明确判红**，不静默放行。
//! ⚠️ T35 实测：**批次连跑**（11 套件背靠背，每个都要新建 Metal 上下文）时 polish
//! 滞后会超过 2.5s —— `regression-replaycheck` 出现过 RP-04 假红（读数 **207×0**），
//! 而**单独复跑同一条命令两次都 11/11**。假红与假绿一样有害（都会浪费判据的可信度），
//! 故按 T22 先例把**就绪门**放宽到 60 × 100ms = 6s。断言本身**不动**：真坏了（布局
//! 永远不完成）6s 后照样判红，门仍然有牙。
constexpr int kUiLayoutWaitTries = 60;

//! 该控件是否"已经完成布局"：有正尺寸、且在场景里可见。
//! **为什么需要这个门**：Qt Quick 的布局/polish 由**渲染循环**驱动。`StackLayout`
//! 刚切页时，新页里的控件先以"布局前尺寸"存在，要等下一次 polish 才拿到真实几何。
//! 本机在 Spotlight `mdbulkindex` 重压（实测 load average 6.0→11.5）时，这个 polish
//! 会被拖到相位定时器之后 —— 于是判据读到 `resultList` = **207×0**，并照这个坐标投递
//! 真实点击 ⇒ 点击落空 ⇒ 连带整串判据红。这是本仓库反复复发的
//! "**仪器没接在实况上**"（T14/T15 血泪第 3 条），不是被测行为。
//! 处置沿 T18/T19 的 DYN 先例：**改判据协议、不改判据** —— 点击前有界等待就绪；
//! 等不到就判红并写明"仪器没接上"，**绝不洗成 PASS**。
bool uiLayoutReady(QQuickItem *item)
{
    return item && item->width() > 1.0 && item->height() > 1.0 && item->isVisible();
}

//! 向窗口投递一次真实鼠标点击（按下 + 同点抬起）。返回事件是否被窗口受理。
//! 不用 QtTest：本项目未引入该依赖，且 AC-12 已确立"从窗口投递真实事件"的先例。
bool uiLocateClick(QQuickWindow *window, const QPointF &scenePos)
{
    const QPoint global = window->mapToGlobal(scenePos.toPoint());
    QMouseEvent press(QEvent::MouseButtonPress, scenePos, scenePos,
                      QPointF(global), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QMouseEvent release(QEvent::MouseButtonRelease, scenePos, scenePos,
                        QPointF(global), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(window, &press);
    QCoreApplication::sendEvent(window, &release);
    return press.isAccepted() || release.isAccepted();
}

//! 经 AppFacade 公共 API 挑一个可用 fixture（与 LocateCheck 同一候选表/同一口径）。
//! 引擎聚合结果按**名称字典序**排序（T17 实测），故必须按下标逐个比对名字。
bool uiLocatePickFixture(stelapp::AppFacade *facade, QString *nameOut)
{
    static const char *const candidates[] = {"Moon", "Jupiter", "Sirius", "Vega", "Polaris"};
    for (const char *want : candidates)
    {
        const QString name = QString::fromLatin1(want);
        const int n = facade->searchObjects(name, 8);
        for (int i = 0; i < n; ++i)
        {
            if (!facade->selectSearchResult(i))
                continue;
            const QString en = facade->objectInfo()->englishName();
            const QString dn = facade->objectInfo()->displayName();
            if (en.compare(name, Qt::CaseInsensitive) != 0
                && dn.compare(name, Qt::CaseInsensitive) != 0)
                continue;
            if (facade->objectInfo()->stableId().isEmpty())
                continue;
            *nameOut = dn.isEmpty() ? en : dn;
            return true;
        }
    }
    return false;
}

void uiLocateFinish(QGuiApplication *app, UiLocateCheck *c)
{
    c->timer->stop();
    const bool pass = c->total > 0 && c->passed == c->total;
    for (const QString &line : c->details)
        std::printf("UICHECK: %s\n", line.toUtf8().constData());
    std::printf("UICHECK: 判据 %d/%d\n", c->passed, c->total);
    std::printf("UICHECK: VERDICT=%s\n", pass ? "PASS" : "FAIL");
    std::fflush(stdout);
    app->exit(pass ? 0 : 10);
}

void uiLocateUnavailable(QGuiApplication *app, UiLocateCheck *c, const QString &why)
{
    c->timer->stop();
    for (const QString &line : c->details)
        std::printf("UICHECK: %s\n", line.toUtf8().constData());
    std::printf("UICHECK: %s\n", why.toUtf8().constData());
    std::printf("UICHECK: VERDICT=UNAVAILABLE\n");
    std::fflush(stdout);
    app->exit(6);
}

void uiLocateAdvance(QGuiApplication *app, std::shared_ptr<UiLocateCheck> c);

void uiLocateStep(QGuiApplication *app, std::shared_ptr<UiLocateCheck> c)
{
    switch (c->phase)
    {
    case 0: {
        // ── UI-01：锚点存在 ─────────────────────────────────────────────
        c->locateButton = c->window->findChild<QQuickItem *>(QStringLiteral("locateButton"));
        c->untrackButton = c->window->findChild<QQuickItem *>(QStringLiteral("untrackButton"));
        c->statusLabel = c->window->findChild<QQuickItem *>(QStringLiteral("locateStatusLabel"));
        c->clearSelButton = c->window->findChild<QQuickItem *>(QStringLiteral("clearSelectionButton"));
        const bool anchors = c->locateButton && c->untrackButton && c->statusLabel
                             && c->clearSelButton;
        uiLocateMark(c.get(), anchors && c->locateButton->isVisible(),
                     QStringLiteral("UI-01 搜索页控件锚点可寻且可见（locateButton=%1 "
                                    "untrackButton=%2 statusLabel=%3，visible=%4）")
                         .arg(c->locateButton ? QStringLiteral("有") : QStringLiteral("无"),
                              c->untrackButton ? QStringLiteral("有") : QStringLiteral("无"),
                              c->statusLabel ? QStringLiteral("有") : QStringLiteral("无"),
                              (c->locateButton && c->locateButton->isVisible())
                                  ? QStringLiteral("true") : QStringLiteral("false")));
        if (!anchors)
        {
            uiLocateUnavailable(app, c.get(),
                                QStringLiteral("缺 QML 锚点：搜索页里 locateButton / "
                                               "untrackButton / locateStatusLabel 至少一个 "
                                               "objectName 找不到 —— 接线断了"));
            return;
        }
        // 摆好"无选中"这个前置状态，下一拍再读控件的真实属性
        c->facade->clearSelection();
        break;
    }
    case 1: {
        // ── UI-02：无选中 → 按钮被**绑定**禁用（读 QML 真实属性）────────
        const bool locOff = c->locateButton && !c->locateButton->isEnabled();
        const bool untOff = c->untrackButton && !c->untrackButton->isEnabled();
        uiLocateMark(c.get(), locOff && untOff,
                     QStringLiteral("UI-02 无选中时两个按钮均禁用（enabled: 定位=%1 取消=%2）")
                         .arg(c->locateButton && c->locateButton->isEnabled()
                                  ? QStringLiteral("true") : QStringLiteral("false"),
                              c->untrackButton && c->untrackButton->isEnabled()
                                  ? QStringLiteral("true") : QStringLiteral("false")));
        // ── 选出一个 fixture，作为下一拍的靶子 ───────────────────────────
        if (!uiLocatePickFixture(c->facade, &c->expectName))
        {
            uiLocateUnavailable(app, c.get(),
                                QStringLiteral("无可用 fixture（Moon/Jupiter/Sirius/Vega/"
                                               "Polaris 全部搜不到）—— 环境条件，非接线缺陷"));
            return;
        }
        c->details.append(QStringLiteral("UI-fixture 选中 %1（stableId=%2）")
                              .arg(c->expectName, c->facade->objectInfo()->stableId()));
        break;
    }
    case 2: {
        // ── UI-03：选中后定位按钮**由绑定**转为可用（C++ → QML 绑定活）──
        const bool locOn = c->locateButton && c->locateButton->isEnabled();
        const bool untOff = c->untrackButton && !c->untrackButton->isEnabled();
        uiLocateMark(c.get(), locOn && untOff,
                     QStringLiteral("UI-03 选中后定位按钮可用 / 取消跟踪仍禁用"
                                    "（enabled: 定位=%1 取消=%2）")
                         .arg(locOn ? QStringLiteral("true") : QStringLiteral("false"),
                              c->untrackButton && c->untrackButton->isEnabled()
                                  ? QStringLiteral("true") : QStringLiteral("false")));
        // ── 真实点击（最外层注入）────────────────────────────────────────
        const QPointF p = uiLocateCenter(c->locateButton);
        c->details.append(QStringLiteral("UI-note 向窗口投递真实点击 @(%1,%2)——定位按钮"
                                         "尺寸 %3×%4，窗口 %5×%6，窗口受理=%7")
                              .arg(p.x(), 0, 'f', 1).arg(p.y(), 0, 'f', 1)
                              .arg(c->locateButton->width(), 0, 'f', 0)
                              .arg(c->locateButton->height(), 0, 'f', 0)
                              .arg(c->window->width()).arg(c->window->height())
                              .arg(uiLocateClick(c->window, p) ? QStringLiteral("true")
                                                               : QStringLiteral("false")));
        break;
    }
    case 3: {
        // ── UI-04：点击真的把引擎推进了跟踪（QML → C++ 端到端）─────────
        const bool tracking = c->facade->isTracking();
        const QString tn = c->facade->trackedName();
        const bool nameOk = !tn.isEmpty() && tn == c->expectName;
        uiLocateMark(c.get(), tracking && nameOk,
                     QStringLiteral("UI-04 真实点击「定位并跟踪」→ 引擎 isTracking=%1 "
                                    "trackedName=\"%2\"（期望 \"%3\"）")
                         .arg(tracking ? QStringLiteral("true") : QStringLiteral("false"),
                              tn, c->expectName));
        if (!(tracking && nameOk))
        {
            // 诊断（不计入判据数）：区分两种失败 ——
            //   ① 合成的事件没落到按钮上（输入合成问题，产品无缺陷）
            //   ② 接线本身是死代码（产品缺陷，需修 QML）
            const bool sigOk = QMetaObject::invokeMethod(c->locateButton, "clicked");
            c->details.append(QStringLiteral("UI-04-diag 直接 emit clicked() 返回=%1，"
                                             "此后 isTracking=%2 —— 若此处为 true 而 "
                                             "UI-04 为 FAIL，则缺陷在「事件没落到按钮」"
                                             "（输入合成），反之才是 QML 接线死代码")
                                  .arg(sigOk ? QStringLiteral("true") : QStringLiteral("false"),
                                       c->facade->isTracking() ? QStringLiteral("true")
                                                               : QStringLiteral("false")));
        }
        break;
    }
    case 4: {
        // ── UI-05：状态文案跟着跟踪状态重绑（C++ → QML，且不谎报）────────
        const QString text = c->statusLabel->property("text").toString();
        uiLocateMark(c.get(), text.startsWith(QStringLiteral("正在跟踪：")) && text.contains(c->expectName),
                     QStringLiteral("UI-05 跟踪中状态文案=\"%1\"（应以「正在跟踪：」开头且含 %2）")
                         .arg(text, c->expectName));
        c->trackedBefore = c->facade->trackedName();
        // ── 负控：往**非按钮**控件（状态 Label）投递同一次真实点击 ───────
        // 打印靶点几何：否则 UI-06 的 PASS 无法排除"点了个寂寞"（既没落到
        // Label 也没落到任何按钮），那样这条负控就是空转。
        const QPointF np = uiLocateCenter(c->statusLabel);
        c->details.append(QStringLiteral("UI-06-note 负控靶点 = 状态 Label 中心 @(%1,%2)，"
                                         "Label 尺寸 %3×%4（非按钮，无交互处理器）")
                              .arg(np.x(), 0, 'f', 1).arg(np.y(), 0, 'f', 1)
                              .arg(c->statusLabel->width(), 0, 'f', 1)
                              .arg(c->statusLabel->height(), 0, 'f', 1));
        uiLocateClick(c->window, np);
        break;
    }
    case 5: {
        // ── UI-06：负控 —— 点在 Label 上，跟踪状态必须纹丝不动 ──────────
        const bool unchanged = c->facade->isTracking()
                            && c->facade->trackedName() == c->trackedBefore;
        uiLocateMark(c.get(), unchanged,
                     QStringLiteral("UI-06 负控：点在非按钮控件上 → isTracking=%1 "
                                    "trackedName=\"%2\"（应与此前 \"%3\" 相同）")
                         .arg(c->facade->isTracking() ? QStringLiteral("true")
                                                      : QStringLiteral("false"),
                              c->facade->trackedName(), c->trackedBefore));
        // ── 真实点击「取消跟踪」──────────────────────────────────────────
        uiLocateClick(c->window, uiLocateCenter(c->untrackButton));
        break;
    }
    case 6: {
        // ── UI-07：取消跟踪按钮同样收到真实点击 ──────────────────────────
        const bool off = !c->facade->isTracking();
        uiLocateMark(c.get(), off,
                     QStringLiteral("UI-07 真实点击「取消跟踪」→ isTracking=%1")
                         .arg(c->facade->isTracking() ? QStringLiteral("true")
                                                      : QStringLiteral("false")));
        break;
    }
    case 7: {
        // ── UI-09（T23）：重进跟踪 → 真实点击「清除选中」（unSelect 路径）──
        // 这是根治路径的**活引擎腿**：T18 的 UI 腿只盖了「取消跟踪」按钮
        // （setTracking(false)），没盖「清除选中」（ObjectInfoModel::clearSelection
        // → StelObjectMgr::unSelect）。修复前这条会红：unSelect 先清选中再发信号，
        // 引擎原始标志泄漏为 true。前提腿：这里必须真的进过跟踪。
        // UI-07（取消跟踪）不清选中 ⇒ 这里直接重进跟踪即可（不必也不应重选：
        // 幂等闸会拦截同 id 重选，发了也白发）。
        const bool entered = c->facade->locateSelected(true);
        uiLocateMark(c.get(), entered && c->facade->isTracking(),
                     QStringLiteral("UI-09-prep 重进跟踪（locateSelected=%1，"
                                    "isTracking=%2）—— 前提不成立则下一条无效")
                         .arg(entered ? QStringLiteral("true") : QStringLiteral("false"),
                              c->facade->isTracking() ? QStringLiteral("true")
                                                      : QStringLiteral("false")));
        if (!(entered && c->facade->isTracking()))
        {
            // 前提没立起来：照实判红，别让 UI-10 在无效前提下假绿。
            uiLocateMark(c.get(), false,
                         QStringLiteral("UI-09 前提失败：无法重进跟踪，UI-10 无效"));
            uiLocateFinish(app, c.get());
            return;
        }
        uiLocateClick(c->window, uiLocateCenter(c->clearSelButton));
        break;
    }
    case 8: {
        // ── UI-08：文案档（原 default，不得丢——曾因 case 8 提前 finish 短路）──
        const QString text = c->statusLabel->property("text").toString();
        uiLocateMark(c.get(), !text.startsWith(QStringLiteral("正在跟踪")),
                     QStringLiteral("UI-08 清除选中后文案=\"%1\"（不得再声称正在跟踪）")
                         .arg(text));
        // ── UI-10（T23）：unSelect 之后跟踪必须**彻底**归零 ──────────────
        // 引擎原始标志（isTracking 现在就是原始标志，见 AppFacade::isTracking）
        // 与 trackedName 双断言。
        const bool off = !c->facade->isTracking()
                      && c->facade->trackedName().isEmpty();
        uiLocateMark(c.get(), off,
                     QStringLiteral("UI-10 真实点击「清除选中」→ isTracking=%1 "
                                    "trackedName=\"%2\"（T23 根治断言：unSelect 必须关跟踪）")
                         .arg(c->facade->isTracking() ? QStringLiteral("true")
                                                      : QStringLiteral("false"),
                              c->facade->trackedName()));
        uiLocateFinish(app, c.get());
        return;
    }
    default: {
        // ── UI-08：取消后文案回到不声称跟踪的档（三档文案的第三档）───────
        const QString text = c->statusLabel->property("text").toString();
        uiLocateMark(c.get(), !text.startsWith(QStringLiteral("正在跟踪")),
                     QStringLiteral("UI-08 取消跟踪后文案=\"%1\"（不得再声称正在跟踪）")
                         .arg(text));
        uiLocateFinish(app, c.get());
        return;
    }
    }
    ++c->phase;
    uiLocateAdvance(app, c);
}

void uiLocateAdvance(QGuiApplication *app, std::shared_ptr<UiLocateCheck> c)
{
    QTimer::singleShot(c->tickMs, app, [app, c]() { uiLocateStep(app, c); });
}

int runUiLocateCheck(QGuiApplication *app, QQuickWindow *window, stelapp::AppFacade *facade)
{
    auto c = std::make_shared<UiLocateCheck>();
    c->window = window;
    c->facade = facade;
    c->timer = new QTimer(app);
    std::printf("UICHECK: 开始（最外层注入：objectName 定位控件 + 窗口真实鼠标事件，"
                "共 8 条判据，每相位 %dms）\n", c->tickMs);
    std::fflush(stdout);
    uiLocateStep(app, c);
    return 0;
}
// ══════════════════════════════════════════════════════════════════════════
// T19 时间页 UI 层端到端自检：STELQUICK_TIME_UI_CHECK=1
//
// 与 STELQUICK_UI_CHECK（T18，搜索/定位页）**同一动机、同一手法**：
//   TimeCheck（src/app）走的全是 AppFacade 的 C++ 公共 API —— 它证明得了"换算与
//   写入逻辑对"，但**证明不了** TimePage.qml 里这几条接线是活的：
//     · `onClicked: timePage.applyFields()`  ← 六个自旋框的"×6"写入路径
//     · `onClicked: appFacade.setTimeNow()`  · `onClicked: timePage.refill()`
//     · `onClicked: ActionRouter.trigger("actionAdd_Solar_Day")`（步进透传）
//     · 状态行绑定 `appFacade.lastTimeRefusal === "ok"`
//   名字写错、按钮没接上、绑定读到函数对象，C++ 侧自检照样全绿。
//   **首跑就抓到后两条里的第三条**：`lastTimeRefusal` 当时只是 Q_INVOKABLE 方法，
//   QML 里 `=== "ok"` 恒 false ⇒ "已生效"分支是死代码，而 timeRefusalText() 在 ok
//   时返回空串 ⇒ 写入成功反而显示**空白 + 红字**。见 AppFacade.hpp 的 Q_PROPERTY 注。
//
// 手法（同 T18，不再复述理由）：objectName 找真实控件 → 向 QQuickWindow 投递
//   真实鼠标按下/抬起 → 断言引擎状态**精确**改变 → 反向读 QML 控件真实属性。
//
// 本页特有的两条：
//   ·「×6」= 六个自旋框，逐个都要能被读到。UI-02/03 用**两个不同**的本地时刻做
//     判别性：若 QML 把某个字段写死，两组就会读出同一个值。
//   · **天然负控**：「改了框但没点应用 ⇒ 时钟一个字节都不动」（UI-06）。这正是
//     TimePage 头注里"改成显式应用是为了可测"所指向的那条；它同时验了 QML 的
//     `dirty` 守卫是活的（挡住 400ms 自动回填，见 UI-04）。
//
// 判据 27 条：UI-01..UI-16（含 UI-09a/09b、UI-14a/14b）+ UI-17..UI-24（T30 视觉层）。
// 退出码：0=PASS / 10=FAIL / 6=UNAVAILABLE。
// T22（2026-09-28）追加 UI-12..UI-16：新控件锚点、MJD 投影行、历法行**成对**、
// 速率**成对**（真实点击「+」→ 引擎变了 ∧ 仪表跟着变）、时区组合框回填。
// T30（2026-09-29）追加 UI-17..UI-24：**视觉层**（几何非退化 / 不被祖先裁掉 /
// 两两不重叠 / 可见性成对 / 文本非空非占位 / 文本不截断 / 对比度下限 / 度量的纯逻辑腿）。
// 见 `uiLinChannel` 上方那段注释，那里写了动机、阈值出处与负控口径。
// ══════════════════════════════════════════════════════════════════════════

//! 本地时刻用例（年/月/日/时/分/秒）。四组**互不相同**，用来判别"QML 有没有把值写死"。
const int kUiTimeA[6] = {2030,  6, 15,  9, 30,  0};   // C++ 写 → 看 QML 字段跟不跟
const int kUiTimeB[6] = {1999,  1,  1,  0,  0,  0};   // 第二组（与 A 不同 ⇒ 判别性）
const int kUiTimeC[6] = {2011, 11, 11, 11, 11, 11};   // 在 UI 上改框后点「应用」
const int kUiTimeD[6] = {2022,  2, 22, 22, 22, 22};   // 改了框但**不点应用**（负控）

// ══════════════════════════════════════════════════════════════════════════
// T30（2026-09-29）「时间页**视觉层**」判据组的度量
//
// 交付物出处（A4 移交清单）："QML 交互级测试：时间页新增控件的视觉层 ⬜ 仍待做"。
//
// 动机：UI-01..UI-16 断言的全是**数据面** —— 锚点找得到、值对不对、点击有没有落到
// 引擎。它们对"控件存在但**看不见**"完全无感，而 Qt Quick 最典型的布局缺陷正是这
// 一类（本仓库已经真金白银踩过其中一条：T22 实测到 `resultList` = **207×0**，那是
// "布局/polish 还没跑完"的前尺寸，照它算坐标投递点击就是空点）：
//   · 零尺寸（控件在、但宽或高为 0）
//   · 被祖先裁掉 / 溢出面板（RowLayout 挤不下 ⇒ 右列按钮跑到页外）          → UI-18
//   · 叠在一处（两个控件矩形相交，上面那个把下面那个盖住）                  → UI-19
//   · 不可见（visible=false，或某个祖先把整支子树隐藏了）                    → UI-20
//   · 文本为空串（Label 画出来是一片空白）                                   → UI-21
//   · **点不到**（透明覆盖层 / z 序错 / 被兄弟盖住 —— 矩形检查看不出来）     → UI-22
//   · 前景 ≈ 背景（文字"存在但读不到"）                                      → UI-23
//
//  「文本被截断」这一类**故意不立判据**：Label 的宽度取自 `implicitWidth`，而
//  QQuickText 的 implicitWidth 就是 `ceil(contentWidth)` ⇒ 两者之差恒在 [0,1) 的
//  取整区间里，任何阈值都只会给出恒真的结论（假绿）。实测六行余量 0.2~0.7 px 正是
//  这个取整余量。它改作 **读数** 打在 UI-21 的 note 里 —— 有余量记录、但不能假装
//  它是一条判据。
//
// 手法（与 UI-01..UI-16 同一纪律）：
//   · **只读** QQuickItem 的原始几何/属性（`mapToItem` + width/height + isVisible +
//     text/color），**不复刻**任何被测逻辑（血泪第 4 条）；
//   · 采一次**快照**，后续判据只读快照 —— 免得判据读到一半被布局改掉；
//   · 两个自写的小函数（WCAG 对比度、矩形包含/交叠）另立**纯逻辑腿**（UI-24）用
//     **已知输入 ⇒ 已知输出**验算。没有这条腿，任何一个函数写成 `return 21.0` /
//     `return true`，活体判据都会全绿，而"全绿"看起来毫无破绽（血泪第 8 条）。
//
// **本判据组不改产品代码**：页面/面板一律沿**父链**取（不新增 QML 锚点）。
// ══════════════════════════════════════════════════════════════════════════

//! 对比度下限：低于它就属于"文字存在但读不到"。
//! 为什么是 2.0 而不是 WCAG 正文的 4.5：本判据的职责是**回归护栏**（抓"前景被改成
//! 与背景同色"这类真实缺陷），不是重新评审设计。产品里几处浅灰说明文字本来就低于
//! AA，按 4.5 判会把**既有的设计选择**判红 —— 那不是本任务要的。
//! 达标情况会作为读数一并打印（≥4.5 / ≥3.0 各几项），供人工评审。
const double kUiContrastFloor = 2.0;

//! sRGB 8bit 分量 → WCAG 线性亮度分量。
double uiLinChannel(double c8)
{
    const double c = c8 / 255.0;
    return (c <= 0.04045) ? (c / 12.92) : std::pow((c + 0.055) / 1.055, 2.4);
}

//! WCAG 相对亮度。
double uiRelLuminance(const QColor &c)
{
    return 0.2126 * uiLinChannel(c.red()) + 0.7152 * uiLinChannel(c.green())
         + 0.0722 * uiLinChannel(c.blue());
}

//! WCAG 对比度 (L_hi + 0.05) / (L_lo + 0.05)。**纯函数、无副作用** ⇒ UI-24 用
//! 标准值（黑白 = 21）与自明值（同色 = 1）验算它，不由被测代码反推。
double uiContrastRatio(const QColor &a, const QColor &b)
{
    const double la = uiRelLuminance(a), lb = uiRelLuminance(b);
    const double hi = qMax(la, lb), lo = qMin(la, lb);
    return (hi + 0.05) / (lo + 0.05);
}

//! 控件在 root 坐标系里的矩形。用 `mapToItem` 一次映射，**不**自己累加 x/y
//! （累加会在祖先带 scale/transform 时悄悄错掉）。
QRectF uiRectIn(QQuickItem *item, QQuickItem *root)
{
    if (!item || !root)
        return QRectF();
    const QPointF tl = item->mapToItem(root, QPointF(0.0, 0.0));
    return QRectF(tl, QSizeF(item->width(), item->height()));
}

//! 两矩形交叠面积（相交但某边为 0 ⇒ 面积 0，即"贴边不算重叠"）。纯函数。
double uiIntersectArea(const QRectF &a, const QRectF &b)
{
    const QRectF i = a.intersected(b);
    return (i.width() > 0.0 && i.height() > 0.0) ? i.width() * i.height() : 0.0;
}

//! inner 是否落在 outer 内（容差 eps）。纯函数 —— UI-18 的祖先链检查与 UI-24 的
//! 纯逻辑腿**共用**这一个判定，所以 UI-24 在反例上给 false 就能证明 UI-18 不是恒真。
bool uiRectInside(const QRectF &inner, const QRectF &outer, double eps)
{
    return inner.left() >= outer.left() - eps && inner.top() >= outer.top() - eps
        && inner.right() <= outer.right() + eps && inner.bottom() <= outer.bottom() + eps;
}

//! 给定矩形 r（item 自身或"文本绘制矩形"）沿父链一直查到 root：若**任一**祖先装不下
//! 它就返回该祖先的描述；全部装得下则返回空串。这就是"**被祖先裁掉 / 溢出面板**"
//! 的定义，`uiAncestorOverflowDetail` 与 UI-22 共用它。
QString uiRectOverflowDetail(const QRectF &r, QQuickItem *item, QQuickItem *root, double eps)
{
    if (!item || !root)
        return QStringLiteral("(缺锚点)");
    for (QQuickItem *a = item->parentItem(); a; a = a->parentItem())
    {
        const QRectF ar = uiRectIn(a, root);
        if (!uiRectInside(r, ar, eps))
        {
            return QStringLiteral("被 %1 裁掉（自身 %2,%3 %4×%5；祖先 %6,%7 %8×%9）")
                .arg(a->objectName().isEmpty()
                         ? QString::fromLatin1(a->metaObject()->className())
                         : a->objectName())
                .arg(r.x(), 0, 'f', 1).arg(r.y(), 0, 'f', 1)
                .arg(r.width(), 0, 'f', 1).arg(r.height(), 0, 'f', 1)
                .arg(ar.x(), 0, 'f', 1).arg(ar.y(), 0, 'f', 1)
                .arg(ar.width(), 0, 'f', 1).arg(ar.height(), 0, 'f', 1);
        }
        if (a == root)
            break;
    }
    return QString();
}

//! 控件自身的矩形是否被祖先裁掉（UI-18 用）。
QString uiAncestorOverflowDetail(QQuickItem *item, QQuickItem *root, double eps)
{
    if (!item || !root)
        return QStringLiteral("(缺锚点)");
    return uiRectOverflowDetail(uiRectIn(item, root), item, root, eps);
}

//! 命中判定的**纯几何核**：父矩形 + 一组"按叠加顺序排列（**列表末尾在最上层**）"的
//! 子矩形 + 点 ⇒ 返回命中的子下标；都不命中返回 -1。
//! 抽成纯函数是为了 UI-24 能用已知输入验算"**最上层那个赢**"这条语义 —— 否则
//! 下降逻辑只能靠"跑起来看着对"来背书（血泪第 8 条）。
int uiHitPickChild(const QRectF &parentRect, const QList<QRectF> &childRects, const QPointF &p)
{
    if (!parentRect.contains(p))
        return -1;
    for (int i = childRects.size() - 1; i >= 0; --i)
        if (childRects.at(i).contains(p))
            return i;
    return -1;
}

//! 手写的命中下降（**不用 `QQuickItem::childAt`**）：实测它在本工程的窗口结构下
//! 只下降一层就停 —— `pageStack->childAt(...)` 返回的是 `TimePage` 本身，而不是
//! 它里面那个按钮（`ApplicationWindowContentControl` 那一层同样）。既然它给不出
//! 可用的结果，就自己走一遍，语义按场景图来：
//!   · 只考虑 `isVisible()` 的子项；
//!   · **子项顺序的反向** = 叠加顺序（QML 里后声明的画在上面）；
//!   · 命中即递归；子项都不命中时返回 `self`（点落在自己身上）。
//! ⚠️ 刻意**不**建模 `z` 与 `clip`（本页没有设过 `z`；`clip` 只影响"能否看到超出
//! 父边界的部分"，而 UI-18 已经断言没有任何控件越出祖先）。这条限制写在判据的
//! 报告里，不藏着。
QQuickItem *uiHitAt(QQuickItem *self, const QPointF &pInSelf)
{
    if (!self || !self->isVisible())
        return nullptr;
    const QRectF selfRect(0.0, 0.0, self->width(), self->height());
    QList<QRectF> childRects;
    QList<QQuickItem *> childItems;
    const QList<QQuickItem *> kids = self->childItems();
    for (QQuickItem *k : kids)
    {
        if (!k->isVisible())
            continue;
        childRects.append(uiRectIn(k, self));
        childItems.append(k);
    }
    const int idx = uiHitPickChild(selfRect, childRects, pInSelf);
    if (idx < 0)
        return self;
    QQuickItem *k = childItems.at(idx);
    if (QQuickItem *deep = uiHitAt(k, k->mapFromItem(self, pInSelf)))
        return deep;
    return k;
}

//! 靶控件**中心点**命中测试是否落回它自己（含它的后代 —— 按钮的文字/背景子项算命中）。
//! 这是"**点得到**"的定义，也是本组里唯一能抓住"透明覆盖层 / z 序错 / 被兄弟盖住"
//! 三类缺陷的判据（矩形检查对它们**统统无感**）。
bool uiHitTestHits(QQuickItem *root, QQuickItem *target)
{
    if (!root || !target)
        return false;
    const QPointF c = target->mapToItem(
        root, QPointF(target->width() / 2.0, target->height() / 2.0));
    QQuickItem *hit = uiHitAt(root, c);
    for (QQuickItem *p = hit; p; p = p->parentItem())
        if (p == target)
            return true;
    return false;
}

//! 控件的**有效背景色**：沿父链找第一个"真的声明了不透明 color"的祖先。
//! 找不到就落回 fallback。Button/CheckBox/ComboBox 的文字底色由 style 绘制，
//! 读不到 color ⇒ 那一类**不纳入**对比度判据（不假装覆盖）。
QColor uiEffectiveBackground(QQuickItem *item, const QColor &fallback)
{
    for (QQuickItem *a = item ? item->parentItem() : nullptr; a; a = a->parentItem())
    {
        const QVariant v = a->property("color");
        if (v.isValid() && v.canConvert<QColor>())
        {
            const QColor c = v.value<QColor>();
            if (c.isValid() && c.alpha() > 0)
                return c;
        }
    }
    return fallback;
}

//! 视觉层几何面的靶控件（20 个**可交互**控件：6 自旋框 + 3 日历按钮 + 4 步进按钮
//! + 7 速率按钮）。名字都是既有的 objectName（T19/T22 加的锚点），全部照抄，
//! 不新增、不改名。
const char *const kUiVisGeoNames[20] = {
    "timeYearField", "timeMonthField", "timeDayField", "timeHourField",
    "timeMinuteField", "timeSecondField",
    "timeApplyButton", "timeResetButton", "timeNowButton",
    "timeSubDayButton", "timeAddDayButton", "timeSubHourButton", "timeAddHourButton",
    "timeSpeedDownButton", "timeSpeedDownLessButton", "timeRealTimeSpeedButton",
    "timeZeroRateButton", "timeReverseButton", "timeSpeedUpLessButton",
    "timeSpeedUpButton"};

//! 视觉层文本面的靶控件（7 个文本行）。前 6 个是**非换行**的单行投影，
//! 第 7 个（状态行）是 WordWrap ⇒ 不纳入"不截断"判据。
const char *const kUiVisTextNames[7] = {
    "timeUtcLabel", "timeJdLabel", "timeMjdLabel", "timeCalendarLabel",
    "timeRateLabel", "timeDirectionLabel", "timeStatusLabel"};
const char *const kUiVisTextZH[7] = {"UT", "JD", "MJD", "历法", "速率", "方向", "状态"};

struct UiTimeCheck
{
    QQuickWindow *window = nullptr;
    stelapp::AppFacade *facade = nullptr;
    QQuickItem *fields[6] = { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };
    QQuickItem *applyButton = nullptr;
    QQuickItem *resetButton = nullptr;
    QQuickItem *nowButton = nullptr;
    QQuickItem *subDayButton = nullptr;
    QQuickItem *addDayButton = nullptr;
    QQuickItem *statusLabel = nullptr;
    QQuickItem *jdLabel = nullptr;
    // ── T22 新增锚点 ─────────────────────────────────────────────────────────
    QQuickItem *mjdLabel = nullptr;
    QQuickItem *calendarLabel = nullptr;
    QQuickItem *rateLabel = nullptr;
    QQuickItem *dirLabel = nullptr;
    QQuickItem *tzCombo = nullptr;
    QQuickItem *customTzCheck = nullptr;
    QQuickItem *speedUpButton = nullptr;
    QStringList details;
    int passed = 0;
    int total = 0;
    int phase = 0;
    //! 相位间留一拍。QML 里有两个 Timer（回填 400ms / 轮询 300ms），间隔必须 ≥ 它们，
    //! 否则"回填该发生却没发生"这类否定式判据（UI-04）就没有检验力。
    int tickMs = 500;

    // 跨相位传递
    double jdBeforeApply = 0.0;    //!< 点「应用」之前的引擎 JD
    double jdBeforeNoop = 0.0;     //!< 负控之前的引擎 JD
    double jdBeforeIllegal = 0.0;  //!< 非法写入之前的引擎 JD
    double jdBeforeNow = 0.0;      //!< 点「现在」之前（已在 1900 年）的引擎 JD
    double jdLabel0 = 0.0;         //!< UI-10a 读到的 JD Label 值
    QString nowText;               //!< UI-09a 读到的状态文案
    // T22
    double rateBeforeClick = 0.0;  //!< UI-15 点击「+」之前的引擎速率
    QString rateTextBefore;        //!< UI-15 点击之前的速率行文本
    QString tzTargetId;            //!< UI-16 要切入的时区 id（UI-15 相位里挑好）

    // ── T30 视觉层（UI-17..UI-24）────────────────────────────────────────────
    //! 采集时刻的快照。判据只读快照，**不再**触碰控件（免得读到一半被布局改掉）。
    QList<QQuickItem *> visItems;   //!< 几何面靶控件（找到几个算几个，与 visNames 对齐）
    QStringList visNames;
    QList<QRectF> visRects;         //!< 上者在 window contentItem 坐标系里的矩形
    QString visGeometryWhy;         //!< 采集阶段没找到的锚点（空串 = 全找到）
    //! 文本面靶控件（与 `kUiVisTextNames` 同下标；nullptr = 该锚点没找到）。
    QQuickItem *visText[7] = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
    QList<QRectF> visTextRects;     //!< 上者在 window contentItem 坐标系里的矩形
    QQuickItem *pageStack = nullptr;
    QQuickItem *searchQueryField = nullptr;  //!< 判别性对照：**非当前页**的控件
    bool negCtlApplied = false;     //!< STELQUICK_TIME_VISUAL_NEGCTL 的扰动只做一次
};

void uiTimeMark(UiTimeCheck *c, bool ok, const QString &line)
{
    ++c->total;
    if (ok)
        ++c->passed;
    c->details.append(line + (ok ? QStringLiteral("：OK") : QStringLiteral("：FAIL")));
}

QString uiTimeFieldsText(const int *v)
{
    return QStringLiteral("%1-%2-%3 %4:%5:%6")
        .arg(v[0], 4, 10, QLatin1Char('0')).arg(v[1], 2, 10, QLatin1Char('0'))
        .arg(v[2], 2, 10, QLatin1Char('0')).arg(v[3], 2, 10, QLatin1Char('0'))
        .arg(v[4], 2, 10, QLatin1Char('0')).arg(v[5], 2, 10, QLatin1Char('0'));
}

//! 把 6 个自旋框设成目标值。**注意**：这走 setProperty，QML 的 onValueChanged 会照常
//! 触发（于是 `dirty` 置真）—— 这正是我们要的：判据测的是"框里的值经 onClicked 走到
//! 引擎"，所以要像用户那样先把值放进框里，而不是绕过框直接把值传给 AppFacade。
void uiTimeSetFields(UiTimeCheck *c, const int *v)
{
    for (int k = 0; k < 6; ++k)
        c->fields[k]->setProperty("value", v[k]);
}

bool uiTimeFieldsEq(const UiTimeCheck *c, const int *v)
{
    for (int k = 0; k < 6; ++k)
        if (c->fields[k]->property("value").toInt() != v[k])
            return false;
    return true;
}

void uiTimeReadFields(const UiTimeCheck *c, int *out)
{
    for (int k = 0; k < 6; ++k)
        out[k] = c->fields[k]->property("value").toInt();
}

void uiTimeEngineFields(stelapp::AppFacade *f, int *out)
{
    for (int k = 0; k < 6; ++k)
        out[k] = f->localDateTimeField(k);
}

bool uiTimeFieldsMatchEngine(const int *a, const int *b)
{
    for (int k = 0; k < 6; ++k)
        if (a[k] != b[k])
            return false;
    return true;
}

//! 「本地日历 → 期望 UT JD」。用的就是旧 DateTimeDialog::newJd() 的口径
//!   `getJDFromDate(本地) - getUTCOffset(校正前的 jd)/24`，与 AppFacade 的
//!   setLocalDateTime 同一公式（TimeCheck TC-05 已把两者逐位比对过，差 0）。
//! 这里**只**用它算期望值，不在判据里另发明一套换算。
bool uiTimeExpectedJd(stelapp::AppFacade *f, const int *v, double *jdOut)
{
    Q_UNUSED(f);
    double cjd = 0.0;
    if (!StelUtils::getJDFromDate(&cjd, v[0], v[1], v[2], v[3], v[4],
                                  static_cast<float>(v[5])))
        return false;
#if defined(STELQUICK_HAS_ENGINE)
    StelCore *core = StelApp::isInitialized() ? StelApp::getInstance().getCore() : nullptr;
    if (!core)
        return false;
    cjd -= core->getUTCOffset(cjd) / 24.0;
#endif
    *jdOut = cjd;
    return true;
}

void uiTimeFinish(QGuiApplication *app, UiTimeCheck *c)
{
    const bool pass = c->total > 0 && c->passed == c->total;
    for (const QString &line : c->details)
        std::printf("TIMEUICHECK: %s\n", line.toUtf8().constData());
    std::printf("TIMEUICHECK: 判据 %d/%d\n", c->passed, c->total);
    std::printf("TIMEUICHECK: VERDICT=%s\n", pass ? "PASS" : "FAIL");
    std::fflush(stdout);
    app->exit(pass ? 0 : 10);
}

void uiTimeUnavailable(QGuiApplication *app, UiTimeCheck *c, const QString &why)
{
    for (const QString &line : c->details)
        std::printf("TIMEUICHECK: %s\n", line.toUtf8().constData());
    std::printf("TIMEUICHECK: %s\n", why.toUtf8().constData());
    std::printf("TIMEUICHECK: VERDICT=UNAVAILABLE\n");
    std::fflush(stdout);
    app->exit(6);
}

void uiTimeAdvance(QGuiApplication *app, std::shared_ptr<UiTimeCheck> c);

void uiTimeStep(QGuiApplication *app, std::shared_ptr<UiTimeCheck> c)
{
    switch (c->phase)
    {
    case 0: {
        // ── UI-01：锚点可寻（找不到即接线/命名断了）──────────────────────
        static const char *const kFieldNames[6] = {"timeYearField", "timeMonthField",
                                                   "timeDayField", "timeHourField",
                                                   "timeMinuteField", "timeSecondField"};
        int found = 0;
        for (int k = 0; k < 6; ++k)
        {
            c->fields[k] = c->window->findChild<QQuickItem *>(QString::fromLatin1(kFieldNames[k]));
            if (c->fields[k])
                ++found;
        }
        c->applyButton  = c->window->findChild<QQuickItem *>(QStringLiteral("timeApplyButton"));
        c->resetButton  = c->window->findChild<QQuickItem *>(QStringLiteral("timeResetButton"));
        c->nowButton    = c->window->findChild<QQuickItem *>(QStringLiteral("timeNowButton"));
        c->subDayButton = c->window->findChild<QQuickItem *>(QStringLiteral("timeSubDayButton"));
        c->addDayButton = c->window->findChild<QQuickItem *>(QStringLiteral("timeAddDayButton"));
        c->statusLabel  = c->window->findChild<QQuickItem *>(QStringLiteral("timeStatusLabel"));
        c->jdLabel      = c->window->findChild<QQuickItem *>(QStringLiteral("timeJdLabel"));
        // T22 新增锚点（时间页收尾四项的观测面）
        c->mjdLabel      = c->window->findChild<QQuickItem *>(QStringLiteral("timeMjdLabel"));
        c->calendarLabel = c->window->findChild<QQuickItem *>(QStringLiteral("timeCalendarLabel"));
        c->rateLabel     = c->window->findChild<QQuickItem *>(QStringLiteral("timeRateLabel"));
        c->dirLabel      = c->window->findChild<QQuickItem *>(QStringLiteral("timeDirectionLabel"));
        c->tzCombo       = c->window->findChild<QQuickItem *>(QStringLiteral("timeTimeZoneCombo"));
        c->customTzCheck = c->window->findChild<QQuickItem *>(QStringLiteral("timeCustomTzCheck"));
        c->speedUpButton = c->window->findChild<QQuickItem *>(QStringLiteral("timeSpeedUpButton"));
        const int btns = (c->applyButton ? 1 : 0) + (c->resetButton ? 1 : 0)
                       + (c->nowButton ? 1 : 0) + (c->subDayButton ? 1 : 0)
                       + (c->addDayButton ? 1 : 0);
        const bool anchors = (found == 6) && (btns == 5) && c->statusLabel && c->jdLabel;
        uiTimeMark(c.get(), anchors && c->fields[0]->isVisible(),
                   QStringLiteral("UI-01 时间页锚点可寻且可见（6 个自旋框找到 %1/6；"
                                  "应用/重置/现在/−1天/+1天 找到 %2/5；状态行=%3 JD 行=%4；"
                                  "年框 visible=%5）")
                       .arg(found).arg(btns)
                       .arg(c->statusLabel ? QStringLiteral("有") : QStringLiteral("无"),
                            c->jdLabel ? QStringLiteral("有") : QStringLiteral("无"),
                            (c->fields[0] && c->fields[0]->isVisible())
                                ? QStringLiteral("true") : QStringLiteral("false")));
        if (!anchors)
        {
            uiTimeUnavailable(app, c.get(),
                              QStringLiteral("缺 QML 锚点：TimePage 的 objectName 至少一个"
                                             "找不到 —— 接线或命名断了，判据无法进行"));
            return;
        }
        // ── UI-12：T22 新控件锚点可寻（单列一条，不混进 UI-01 的口径）────────
        const int t22Anchors = (c->mjdLabel ? 1 : 0) + (c->calendarLabel ? 1 : 0)
                             + (c->rateLabel ? 1 : 0) + (c->dirLabel ? 1 : 0)
                             + (c->tzCombo ? 1 : 0) + (c->customTzCheck ? 1 : 0)
                             + (c->speedUpButton ? 1 : 0);
        uiTimeMark(c.get(), t22Anchors == 7,
                   QStringLiteral("UI-12 T22 新控件锚点可寻（MJD 行/历法行/速率行/方向行/"
                                  "时区组合框/自定义时区勾选/速率「+」按钮）找到 %1/7；"
                                  "MJD 行可见=%2、组合框可见=%3")
                       .arg(t22Anchors)
                       .arg((c->mjdLabel && c->mjdLabel->isVisible()) ? QStringLiteral("true")
                                                                      : QStringLiteral("false"),
                            (c->tzCombo && c->tzCombo->isVisible()) ? QStringLiteral("true")
                                                                    : QStringLiteral("false")));
        // 暂停时钟：本检查全部是"写入后读回"的等式，时钟若在走会把 JD 比较淹掉。
        // 也顺带让 QML 的投影 Label 稳定，UI-10 才有意义。
        c->facade->setSimulationPaused(true);
        c->details.append(QStringLiteral("TIMEUICHECK-note 已暂停时钟（scale=0），"
                                         "全部 JD 断言在冻结区执行；tick=%1ms")
                              .arg(c->tickMs));
        break;
    }
    case 1: {
        // ── 写 A（C++ 侧命令）→ 下一拍看 QML 的 6 个框有没有跟着回填 ──────
        const bool wrote = c->facade->setLocalDateTime(kUiTimeA[0], kUiTimeA[1], kUiTimeA[2],
                                                       kUiTimeA[3], kUiTimeA[4], kUiTimeA[5]);
        c->details.append(QStringLiteral("UI-02-prep C++ setLocalDateTime(%1) → %2（token=%3）")
                              .arg(uiTimeFieldsText(kUiTimeA),
                                   wrote ? QStringLiteral("true") : QStringLiteral("false"),
                                   c->facade->lastTimeRefusal()));
        break;
    }
    case 2: {
        // ── UI-02：C++ → QML 回填绑定是活的（读 SpinBox 真实 value）──────
        int got[6];
        uiTimeReadFields(c.get(), got);
        uiTimeMark(c.get(), uiTimeFieldsEq(c.get(), kUiTimeA),
                   QStringLiteral("UI-02 C++ 写入后 QML 自旋框跟着回填（框内=%1，目标=%2）")
                       .arg(uiTimeFieldsText(got), uiTimeFieldsText(kUiTimeA)));
        // 写 B（与 A 不同）—— 下一拍若还是 A，就说明某个字段是写死的
        c->facade->setLocalDateTime(kUiTimeB[0], kUiTimeB[1], kUiTimeB[2],
                                    kUiTimeB[3], kUiTimeB[4], kUiTimeB[5]);
        break;
    }
    case 3: {
        // ── UI-03：换一个**不同**的时刻，框必须跟着变（判别性）───────────
        int got[6];
        uiTimeReadFields(c.get(), got);
        uiTimeMark(c.get(), uiTimeFieldsEq(c.get(), kUiTimeB),
                   QStringLiteral("UI-03 判别性：换写 %1（与上组不同）后框内=%2 —— "
                                  "排除\"某字段写死/回填只回一部分\"")
                       .arg(uiTimeFieldsText(kUiTimeB), uiTimeFieldsText(got)));
        break;
    }
    case 4: {
        // ── 把框改成 C（像用户那样先改框），记下此刻 JD ──────────────────
        uiTimeSetFields(c.get(), kUiTimeC);
        c->jdBeforeApply = c->facade->julianDay();
        int got[6];
        uiTimeReadFields(c.get(), got);
        c->details.append(QStringLiteral("UI-04-prep 已在 QML 里把 6 个框改成 %1，"
                                         "此刻引擎 JD=%2（**还没点应用**）")
                              .arg(uiTimeFieldsText(got))
                              .arg(c->jdBeforeApply, 0, 'f', 6));
        break;
    }
    case 5: {
        // ── UI-04：改框后**自动回填被 dirty 挡住**（框内保持用户改的值）──
        //   本页头注说"不挡就会：回填 → 触发 → 又写一次引擎"。这条判据就是要验
        //   那个 syncing/dirty 守卫真的在起作用 —— 否则用户改的数字会被每秒覆盖。
        int got[6];
        uiTimeReadFields(c.get(), got);
        uiTimeMark(c.get(), uiTimeFieldsEq(c.get(), kUiTimeC),
                   QStringLiteral("UI-04 改框后等 %1ms（≥ 400ms 自动回填周期），框内仍=%2"
                                  "（dirty 守卫挡住了回填）")
                       .arg(c->tickMs).arg(uiTimeFieldsText(got)));
        // ── 真实点击「应用」──────────────────────────────────────────────
        const QPointF p = uiLocateCenter(c->applyButton);
        c->details.append(QStringLiteral("UI-05-note 向窗口投递真实点击 @(%1,%2)——「应用」"
                                         "按钮尺寸 %3×%4，窗口 %5×%6，窗口受理=%7")
                              .arg(p.x(), 0, 'f', 1).arg(p.y(), 0, 'f', 1)
                              .arg(c->applyButton->width(), 0, 'f', 0)
                              .arg(c->applyButton->height(), 0, 'f', 0)
                              .arg(c->window->width()).arg(c->window->height())
                              .arg(uiLocateClick(c->window, p) ? QStringLiteral("true")
                                                               : QStringLiteral("false")));
        break;
    }
    case 6: {
        // ── UI-05：点「应用」→ 引擎 JD 落到**公式算出的那个值**（精确，不是"变了就行"）
        double want = 0.0;
        const bool haveWant = uiTimeExpectedJd(c->facade, kUiTimeC, &want);
        const double jd = c->facade->julianDay();
        const double diff = haveWant ? qAbs(jd - want) : -1.0;
        int eng[6];
        uiTimeEngineFields(c->facade, eng);
        uiTimeMark(c.get(), haveWant && diff < 1e-6 && uiTimeFieldsMatchEngine(eng, kUiTimeC),
                   QStringLiteral("UI-05 真实点击「应用」→ 引擎 JD=%1，期望 %2（差 %3 天，"
                                  "容差 1e-6；引擎本地读回=%4，目标 %5）")
                       .arg(jd, 0, 'f', 9).arg(want, 0, 'f', 9)
                       .arg(diff, 0, 'e', 2)
                       .arg(uiTimeFieldsText(eng), uiTimeFieldsText(kUiTimeC)));
        break;
    }
    case 7: {
        // ── 把框改成 D（另一个值），记下此刻 JD —— 准备负控 ──────────────
        uiTimeSetFields(c.get(), kUiTimeD);
        c->jdBeforeNoop = c->facade->julianDay();
        const QPointF np = uiLocateCenter(c->statusLabel);
        c->details.append(QStringLiteral("UI-06-note 框已改成 %1；负控靶点 = 状态行中心 "
                                         "@(%2,%3)，Label 尺寸 %4×%5（非按钮，无交互处理器）")
                              .arg(uiTimeFieldsText(kUiTimeD))
                              .arg(np.x(), 0, 'f', 1).arg(np.y(), 0, 'f', 1)
                              .arg(c->statusLabel->width(), 0, 'f', 1)
                              .arg(c->statusLabel->height(), 0, 'f', 1));
        uiLocateClick(c->window, np);
        break;
    }
    case 8: {
        // ── UI-06：负控 —— 改了框但没点应用，时钟必须一个字节都不动 ───────
        const double jd = c->facade->julianDay();
        const double d = qAbs(jd - c->jdBeforeNoop);
        int got[6];
        uiTimeReadFields(c.get(), got);
        uiTimeMark(c.get(), d < 1e-12 && uiTimeFieldsEq(c.get(), kUiTimeD),
                   QStringLiteral("UI-06 负控：改了框（%1）但**不点应用**、只点非按钮控件 → "
                                  "JD 位移 %2 天（要求 <1e-12）；框内仍=%3")
                       .arg(uiTimeFieldsText(kUiTimeD)).arg(d, 0, 'e', 2)
                       .arg(uiTimeFieldsText(got)));
        break;
    }
    case 9: {
        // ── 真实点击「重置」──────────────────────────────────────────────
        uiLocateClick(c->window, uiLocateCenter(c->resetButton));
        break;
    }
    case 10: {
        // ── UI-07：重置只**回填**引擎当前值，不写时钟 ────────────────────
        int eng[6], got[6];
        uiTimeEngineFields(c->facade, eng);
        uiTimeReadFields(c.get(), got);
        const double d = qAbs(c->facade->julianDay() - c->jdBeforeNoop);
        bool same = true;
        for (int k = 0; k < 6; ++k)
            same = same && (got[k] == eng[k]);
        uiTimeMark(c.get(), same && d < 1e-12,
                   QStringLiteral("UI-07 真实点击「重置」→ 框内=%1 回到引擎当前值=%2，"
                                  "且 JD 位移 %3 天（<1e-12，即重置**不写时钟**）")
                       .arg(uiTimeFieldsText(got), uiTimeFieldsText(eng))
                       .arg(d, 0, 'e', 2));
        // ── 先跳到 1900 年，让"现在"这条判据有判别性 ────────────────────
        double jd1900 = 0.0;
        StelUtils::getJDFromDate(&jd1900, 1900, 1, 1, 0, 0, 0.f);
        c->facade->setJulianDay(jd1900);
        c->jdBeforeNow = c->facade->julianDay();
        c->details.append(QStringLiteral("UI-08-prep 已跳到 JD=%1（1900-01-01），"
                                         "与系统时刻差 %2 天 —— 判别性前提")
                              .arg(c->jdBeforeNow, 0, 'f', 4)
                              .arg(qAbs(c->jdBeforeNow - StelUtils::getJDFromSystem()), 0, 'f', 2));
        uiLocateClick(c->window, uiLocateCenter(c->nowButton));
        break;
    }
    case 11: {
        // ── UI-08：点「现在」→ 引擎 JD 就是系统当前时刻 ──────────────────
        const double diff = qAbs(c->facade->julianDay() - StelUtils::getJDFromSystem());
        const double contrast = qAbs(c->jdBeforeNow - StelUtils::getJDFromSystem());
        uiTimeMark(c.get(), diff < 1e-3,
                   QStringLiteral("UI-08 真实点击「现在」→ 与系统时刻差 %1 天（容差 1e-3；"
                                  "跳转前是 %2 天，判别性成立=%3）")
                       .arg(diff, 0, 'e', 3).arg(contrast, 0, 'f', 2)
                       .arg(contrast > 100.0 ? QStringLiteral("true") : QStringLiteral("false")));
        // 下一拍读状态文案（绑定靠 lastTimeRefusalChanged 重算）
        break;
    }
    case 12: {
        // ── UI-09a：成功写入后，状态行必须声称"已生效"────────────────────
        //   这一条正是抓到 Q_PROPERTY 缺陷的那条：修前 `appFacade.lastTimeRefusal`
        //   读到函数对象 ⇒ 恒走 else 分支 ⇒ timeRefusalText() 在 ok 时返回空串
        //   ⇒ 文案是空串（且颜色被标红）。
        c->nowText = c->statusLabel->property("text").toString();
        const bool okToken = c->facade->lastTimeRefusal() == QStringLiteral("ok");
        uiTimeMark(c.get(), okToken && !c->nowText.isEmpty()
                                && c->nowText.contains(QStringLiteral("已按写入生效")),
                   QStringLiteral("UI-09a 写入成功后状态行文案=\"%1\"（token=%2，"
                                  "要求非空且声称已生效）")
                       .arg(c->nowText, c->facade->lastTimeRefusal()));
        // ── 做一次**非法写入**（13 月），看状态行会不会跟着改口 ──────────
        c->jdBeforeIllegal = c->facade->julianDay();
        const bool landed = c->facade->setLocalDateTime(2026, 13, 1, 0, 0, 0);
        c->details.append(QStringLiteral("UI-09b-prep setLocalDateTime(2026-13-01 …) → %1"
                                         "（token=%2）")
                              .arg(landed ? QStringLiteral("true") : QStringLiteral("false"),
                                   c->facade->lastTimeRefusal()));
        break;
    }
    case 13: {
        // ── UI-09b：非法写入后状态行**不再**声称生效，且时钟未动 ─────────
        const QString text = c->statusLabel->property("text").toString();
        const double d = qAbs(c->facade->julianDay() - c->jdBeforeIllegal);
        const bool tokOk = c->facade->lastTimeRefusal() == QStringLiteral("invalid-date");
        uiTimeMark(c.get(), tokOk && !text.isEmpty()
                                && !text.contains(QStringLiteral("已按写入生效"))
                                && d < 1e-12,
                   QStringLiteral("UI-09b 非法写入后状态行文案=\"%1\"（token=%2，"
                                  "不得再声称已生效）；JD 位移 %3 天（<1e-12）")
                       .arg(text, c->facade->lastTimeRefusal()).arg(d, 0, 'e', 2));
        break;
    }
    case 14: {
        // ── UI-10a：JD 只读投影（300ms 轮询）与 C++ 侧同源、且不是"—" ────
        const QString t = c->jdLabel->property("text").toString();
        c->jdLabel0 = t.toDouble();
        const double cur = c->facade->julianDay();
        uiTimeMark(c.get(), t != QStringLiteral("—") && c->jdLabel0 > 0.0
                                && qAbs(c->jdLabel0 - cur) < 1e-3,
                   QStringLiteral("UI-10a JD 投影行文本=\"%1\"（解析 %2，C++ 侧 %3，"
                                  "差 %4 天；容差 1e-3）")
                       .arg(t).arg(c->jdLabel0, 0, 'f', 6).arg(cur, 0, 'f', 6)
                       .arg(qAbs(c->jdLabel0 - cur), 0, 'e', 2));
        // ── 跳 +1 年，下一拍看投影行会不会跟着变（不是常量）──────────────
        c->facade->setJulianDay(cur + 365.25);
        break;
    }
    case 15: {
        // ── UI-10b：投影行确实跟着时钟走（排除"只画了个常量"）────────────
        const QString t = c->jdLabel->property("text").toString();
        const double now = t.toDouble();
        const double want = c->jdLabel0 + 365.25;
        uiTimeMark(c.get(), qAbs(now - want) < 0.01,
                   QStringLiteral("UI-10b 时钟 +365.25 天后投影行=%1（期望 %2，差 %3 天）"
                                  "—— 证明它是活的投影而不是常量")
                       .arg(now, 0, 'f', 6).arg(want, 0, 'f', 6)
                       .arg(qAbs(now - want), 0, 'e', 2));
        // ── 步进按钮：真实点击「+1 天」（透传引擎 StelAction）────────────
        c->jdBeforeNow = c->facade->julianDay();
        const QPointF p = uiLocateCenter(c->addDayButton);
        c->details.append(QStringLiteral("UI-11-note 向窗口投递真实点击 @(%1,%2)——「+1 天」"
                                         "按钮尺寸 %3×%4（ActionRouter 透传 "
                                         "actionAdd_Solar_Day）")
                              .arg(p.x(), 0, 'f', 1).arg(p.y(), 0, 'f', 1)
                              .arg(c->addDayButton->width(), 0, 'f', 0)
                              .arg(c->addDayButton->height(), 0, 'f', 0));
        uiLocateClick(c->window, p);
        break;
    }
    case 16: {
        // ── UI-11：步进透传链活着（QML 按钮 → ActionRouter → 引擎 StelAction）
        const double d = c->facade->julianDay() - c->jdBeforeNow;
        uiTimeMark(c.get(), qAbs(d - 1.0) < 1e-6,
                   QStringLiteral("UI-11 真实点击「+1 天」→ JD 位移 %1 天（期望 1，"
                                  "容差 1e-6）—— QML→ActionRouter→引擎 透传链是活的")
                       .arg(d, 0, 'f', 9));
        break;
    }
    // ══ T22（2026-09-28）时间页收尾四项的 UI 判据 ═════════════════════════
    //  与 T19 的 UI-01..11 同一手法：**从最外层注入真实事件**、读 QML 真实属性。
    //  新增的读数全部走 300 ms 轮询（见 TimePage 头注 ③：那批读接口是
    //  Q_INVOKABLE 不是属性，不能建绑定），所以每条"读 Label"的判据前面都有一拍
    //  （tickMs = 500 ms > 300 ms）的等待 —— 由相位划分天然提供。
    case 17: {
        // ── UI-13：MJD 投影行与 C++ 侧同源 ─────────────────────────────
        const QString t = c->mjdLabel->property("text").toString();
        const double shown = t.toDouble();
        const double cpp = c->facade->modifiedJulianDay();
        uiTimeMark(c.get(), t != QStringLiteral("—") && shown > 0.0
                                && qAbs(shown - cpp) < 1e-6,
                   QStringLiteral("UI-13 MJD 投影行=\"%1\"（解析 %2；C++ 侧 %3；差 %4 天，"
                                  "容差 1e-6）—— 且它与 JD 行同源（JD=%5）")
                       .arg(t).arg(shown, 0, 'f', 6).arg(cpp, 0, 'f', 6)
                       .arg(qAbs(shown - cpp), 0, 'e', 2)
                       .arg(c->facade->julianDay(), 0, 'f', 6));
        // 准备 UI-14a：跳进 1582 换历边界**之前**（历法行必须自报儒略历）
        c->facade->setJulianDay(2299160.5);
        c->details.append(QStringLiteral("UI-14a-prep 时钟跳到 JD=2299160.5"
                                         "（1582-10-04，儒略历区间）"));
        break;
    }
    case 18: {
        // ── UI-14a：历法行跟着实况走（1582 → 儒略历）────────────────────
        const QString t = c->calendarLabel->property("text").toString();
        const QString tok = c->facade->dateCalendarToken();
        uiTimeMark(c.get(), t.contains(QStringLiteral("儒略"))
                                && tok == QStringLiteral("julian"),
                   QStringLiteral("UI-14a 历法行=\"%1\"（token=%2）—— 跳进 1582 年区间后"
                                  "必须自报儒略历").arg(t, tok));
        // 准备 UI-14b：跳回现代（同一控件必须改口）—— 与上一条**成对**
        c->facade->setJulianDay(StelUtils::getJDFromSystem());
        break;
    }
    case 19: {
        // ── UI-14b：判别性 —— 同一控件在两条判据里给出**不同**文案 ──────
        const QString t = c->calendarLabel->property("text").toString();
        const QString tok = c->facade->dateCalendarToken();
        uiTimeMark(c.get(), t.contains(QStringLiteral("格里高利"))
                                && tok == QStringLiteral("gregorian"),
                   QStringLiteral("UI-14b 历法行=\"%1\"（token=%2）—— 与 UI-14a 的文案不同"
                                  "⇒ 它是活的投影，不是常量").arg(t, tok));
        // 准备 UI-15：记下点击前的引擎速率与速率行文本，然后**真实点击**「+」
        c->rateBeforeClick = c->facade->timeRate();
        c->rateTextBefore = c->rateLabel->property("text").toString();
        const QPointF p = uiLocateCenter(c->speedUpButton);
        c->details.append(QStringLiteral("UI-15-note 向窗口投递真实点击 @(%1,%2)——速率「+」"
                                         "按钮（ActionRouter 透传 actionIncrease_Time_Speed）；"
                                         "点击前引擎速率 %3、速率行=\"%4\"")
                              .arg(p.x(), 0, 'f', 1).arg(p.y(), 0, 'f', 1)
                              .arg(c->rateBeforeClick, 0, 'e', 6).arg(c->rateTextBefore));
        uiLocateClick(c->window, p);
        break;
    }
    case 20: {
        // ── UI-15：速率**成对** ——「世界确实动了」∧「仪表跟着动了」────────
        //  这是本轮的靶心判据。只测后者，"Label 恒为常量"也能绿；只测前者，
        //  就漏掉"按了 L 键 UI 纹丝不动"这个原缺陷（T22 修的就是它）。
        const double after = c->facade->timeRate();
        const QString rateTextAfter = c->rateLabel->property("text").toString();
        const bool engineMoved = qAbs(after - c->rateBeforeClick)
                                 > qAbs(c->rateBeforeClick) * 1e-6 + 1e-18;
        const bool labelMoved = (rateTextAfter != c->rateTextBefore);
        uiTimeMark(c.get(), engineMoved && labelMoved,
                   QStringLiteral("UI-15 真实点击「+」→ 引擎速率 %1 → %2（确实变了=%3）；"
                                  "速率行 \"%4\" → \"%5\"（跟着变了=%6）")
                       .arg(c->rateBeforeClick, 0, 'e', 6).arg(after, 0, 'e', 6)
                       .arg(engineMoved ? QStringLiteral("true") : QStringLiteral("false"))
                       .arg(c->rateTextBefore, rateTextAfter)
                       .arg(labelMoved ? QStringLiteral("true") : QStringLiteral("false")));
        // 准备 UI-16：从**引擎接受的名单**里挑一个"偏移 0"的时区（与跑在哪天无关）
        const QStringList avail = c->facade->availableTimeZoneIds();
        const QDateTime nowUtc = QDateTime::currentDateTimeUtc();
        for (const QString &id : avail)
        {
            const QTimeZone z(id.toUtf8());
            if (z.isValid() && z.offsetFromUtc(nowUtc) == 0
                && id != c->facade->timeZoneId())
            {
                c->tzTargetId = id;
                break;
            }
        }
        if (c->tzTargetId.isEmpty())
        {
            c->details.append(QStringLiteral("UI-16 SKIP：名单（%1 项）里找不到偏移 0 且"
                                             "不同于当前值的时区 —— 环境条件")
                                  .arg(avail.size()));
        }
        else
        {
            const bool ok = c->facade->setTimeZoneId(c->tzTargetId);
            c->details.append(QStringLiteral("UI-16-prep setTimeZoneId(\"%1\") → %2"
                                             "（当前 %3）")
                                  .arg(c->tzTargetId,
                                       ok ? QStringLiteral("true") : QStringLiteral("false"),
                                       c->facade->timeZoneId()));
        }
        break;
    }
    case 21: {
        // ── UI-16：时区组合框跟着**实况**回填（C++ 写入 → 300 ms 轮询 → ComboBox）──
        //  这条测的是"程序赋值 currentIndex"这条回填路 —— 它**不会**触发 activated
        //  （只有用户交互才触发），所以回填不会反过来再写一次引擎。
        if (!c->tzTargetId.isEmpty())
        {
            const QString shown = c->tzCombo->property("currentText").toString();
            uiTimeMark(c.get(), shown == c->tzTargetId,
                       QStringLiteral("UI-16 组合框当前项=\"%1\"（目标=\"%2\"，C++ 侧 "
                                      "timeZoneId=\"%3\"）—— 命令写入后轮询把控件带回实况")
                           .arg(shown, c->tzTargetId, c->facade->timeZoneId()));
        }
        break;
    }
    // ══ T30（2026-09-29）时间页**视觉层**判据组（UI-17..UI-24）═════════════════
    //  交付物出处：A4 移交清单"QML 交互级测试：时间页新增控件的视觉层 ⬜ 仍待做"。
    //  设计理由、阈值出处、以及"为什么不新增 QML 锚点"见文件上方 `uiLinChannel`
    //  那一段注释。这里只重复两条**执行**纪律：
    //    · 一条判据要么**成对**（正例 ∧ 反例），要么有**判别性对照**（纯逻辑腿）；
    //    · 负控扰动只动**观测面**、不动产品代码，且**不洗成 PASS**。
    case 22: {
        // ── 负控扰动（一次性；仅环境变量置位时）────────────────────────────
        //  四个扰动分别打中"几何 / 可见性 / 文本 / 颜色"四族判据：
        //    ① 月框 `height=0`      ⇒ 几何退化（UI-17）必须红；
        //    ② 年框 `visible=false` ⇒ 可见性（UI-20）与命中测试（UI-22）必须红；
        //    ③ JD 行 `text=""`      ⇒ 文本非空（UI-21）必须红；
        //    ④ 状态行前景色改成它**自己的有效背景色** ⇒ 对比度 = 1.0（UI-23）必须红。
        //  ⚠️ 为什么这些扰动"打得到"：
        //    · `height` 直接赋值后，布局的 polish 要等**事件循环下一拍**才会把它改回去，
        //      而采集就在**同一个同步块**里紧接着做 ⇒ 读到的一定是被扰动后的几何。
        //      （这也是为什么 UI-18/UI-19 不会跟着红：它们用的是下一拍的快照。）
        //    · `visible` 不是布局管的属性，改了就一直有效。
        //    · `setProperty` 赋给**带绑定的**属性会**解绑**（JD 行的 text 绑定
        //      `timePage.jdText`、状态行的 color 绑定 `lastTimeRefusal`），所以不会被
        //      随后的 300 ms 轮询改回去 —— 这正是负控需要的"稳定破坏"。
        //  扰动发生在 UI-01..UI-16 **全部跑完之后**，故那些判据看到的是干净页（脚本
        //  会断言它们零 FAIL）。
        if (qEnvironmentVariableIsSet("STELQUICK_TIME_VISUAL_NEGCTL") && !c->negCtlApplied)
        {
            c->negCtlApplied = true;
            const QColor bg = c->statusLabel
                                  ? uiEffectiveBackground(c->statusLabel, QColor(QStringLiteral("#ffffff")))
                                  : QColor(QStringLiteral("#ffffff"));
            c->details.append(QStringLiteral(
                "VIS-note 负控模式（STELQUICK_TIME_VISUAL_NEGCTL=1）：月框 height→0、"
                "年框 visible→false、JD 行 text→空、状态行前景色→它的有效背景色 %1 "
                "—— **只扰动观测面**，不改产品代码。期望 UI-17/UI-20/UI-21/UI-22/UI-23 "
                "判红，UI-18/UI-19 不受影响（它们的几何快照取自下一拍），"
                "既有 19 条零 FAIL").arg(bg.name()));
            if (c->fields[1])
                c->fields[1]->setProperty("height", 0.0);          // ①
            if (c->fields[0])
                c->fields[0]->setProperty("visible", false);        // ②
            QQuickItem *jd = c->window->findChild<QQuickItem *>(QStringLiteral("timeJdLabel"));
            if (jd)
                jd->setProperty("text", QString());                 // ③
            if (c->statusLabel)
                c->statusLabel->setProperty("color", bg);           // ④
        }

        // ── 采集靶控件快照（几何面 + 文本面）──────────────────────────────
        QStringList missing;
        c->visItems.clear();
        c->visNames.clear();
        for (int k = 0; k < 20; ++k)
        {
            const QString nm = QString::fromLatin1(kUiVisGeoNames[k]);
            QQuickItem *it = c->window->findChild<QQuickItem *>(nm);
            if (!it)
            {
                missing.append(nm);
                continue;   // 保持 visItems / visNames / visRects 三者**同下标对齐**
            }
            c->visItems.append(it);
            c->visNames.append(nm);
        }
        c->visGeometryWhy = missing.isEmpty()
                                ? QString()
                                : QStringLiteral("找不到 %1 个锚点：%2")
                                      .arg(missing.size())
                                      .arg(missing.join(QStringLiteral("、")));
        QQuickItem *root = c->window->contentItem();
        c->visRects.clear();
        for (QQuickItem *it : c->visItems)
            c->visRects.append(uiRectIn(it, root));
        // 文本面：7 个文本行（顺序与 kUiVisTextNames / kUiVisTextZH 对齐）
        c->visTextRects.clear();
        for (int k = 0; k < 7; ++k)
        {
            c->visText[k] = c->window->findChild<QQuickItem *>(
                QString::fromLatin1(kUiVisTextNames[k]));
            c->visTextRects.append(uiRectIn(c->visText[k], root));
        }
        if (!c->pageStack)
            c->pageStack = c->window->findChild<QQuickItem *>(QStringLiteral("pageStack"));
        if (!c->searchQueryField)
            c->searchQueryField =
                c->window->findChild<QQuickItem *>(QStringLiteral("searchQueryField"));

        // ── UI-17：几何**非退化**（有正尺寸）──────────────────────────────
        //  专门打"控件在、但宽或高是 0"这一类：T22 实测过的 207×0 就是它。
        int degenerate = 0;
        double minW = -1.0, minH = -1.0;
        for (QQuickItem *it : c->visItems)
        {
            if (it->width() <= 1.0 || it->height() <= 1.0)
            {
                ++degenerate;
                continue;
            }
            minW = (minW < 0.0) ? it->width() : qMin(minW, it->width());
            minH = (minH < 0.0) ? it->height() : qMin(minH, it->height());
        }
        uiTimeMark(c.get(),
                   c->visItems.size() == 20 && degenerate == 0 && c->visGeometryWhy.isEmpty(),
                   QStringLiteral("UI-17 视觉层：20 个可交互靶控件几何非退化（找到 %1/20；"
                                  "零尺寸 %2 个；最小 w×h=%3×%4；窗口 %5×%6）%7")
                       .arg(c->visItems.size()).arg(degenerate)
                       .arg(minW, 0, 'f', 1).arg(minH, 0, 'f', 1)
                       .arg(root ? root->width() : -1.0, 0, 'f', 0)
                       .arg(root ? root->height() : -1.0, 0, 'f', 0)
                       .arg(c->visGeometryWhy.isEmpty()
                                ? QString()
                                : QStringLiteral("；%1").arg(c->visGeometryWhy)));
        break;
    }
    case 23: {
        QQuickItem *root = c->window->contentItem();

        // ── UI-18：**不被任何祖先裁掉**（含页容器与两侧面板）───────────────
        //  为什么不新增 QML 锚点去拿"页矩形"：沿**父链**逐级比对**更强** —— 它同时
        //  覆盖"跑到页外"和"溢出自己所在的面板（白色圆角块）"两种越界，而且一个
        //  QML 锚点都不用加（本轮产品代码零改动）。
        QStringList clipped;
        for (int k = 0; k < c->visItems.size(); ++k)
        {
            const QString why = uiAncestorOverflowDetail(c->visItems[k], root, 1.0);
            if (!why.isEmpty())
                clipped.append(QStringLiteral("%1 %2").arg(c->visNames[k], why));
        }
        uiTimeMark(c.get(), c->visItems.size() == 20 && clipped.isEmpty(),
                   QStringLiteral("UI-18 视觉层：20 个靶控件都不被任何祖先裁掉（越界 %1 个；"
                                  "页容器=%2，%3×%4）%5")
                       .arg(clipped.size())
                       .arg(c->pageStack ? (c->pageStack->objectName().isEmpty()
                                                ? QStringLiteral("(未命名)")
                                                : c->pageStack->objectName())
                                         : QStringLiteral("(缺)"))
                       .arg(c->pageStack ? c->pageStack->width() : -1.0, 0, 'f', 0)
                       .arg(c->pageStack ? c->pageStack->height() : -1.0, 0, 'f', 0)
                       .arg(clipped.isEmpty() ? QString()
                                              : QStringLiteral("；首个：%1").arg(clipped.first())));

        // ── UI-19：两两**不重叠**（27 项 = 20 可交互 + 7 文本行）────────────
        //  打"塌在一处"这一族：布局彻底失效时所有控件会堆在 (0,0)，矩形互相包含，
        //  交叠面积很大 —— 而"锚点找得到""值也对"这类判据完全看不出来。
        //  文本行也纳入：`GridLayout` 里"文字压住旁边的输入框"是同一族的缺陷。
        QList<QRectF> allRects = c->visRects;
        allRects += c->visTextRects;
        QStringList allNames = c->visNames;
        for (int k = 0; k < 7; ++k)
            allNames.append(QStringLiteral("文本行·%1").arg(QString::fromUtf8(kUiVisTextZH[k])));
        int overlaps = 0;
        double worst = 0.0;
        QString worstPair;
        for (int i = 0; i < allRects.size(); ++i)
        {
            for (int j = i + 1; j < allRects.size(); ++j)
            {
                const double a = uiIntersectArea(allRects[i], allRects[j]);
                if (a > 1.0)
                {
                    ++overlaps;
                    if (a > worst)
                    {
                        worst = a;
                        worstPair = allNames[i] + QStringLiteral("×") + allNames[j];
                    }
                }
            }
        }
        uiTimeMark(c.get(), c->visRects.size() == 20 && allRects.size() == 27 && overlaps == 0,
                   QStringLiteral("UI-19 视觉层：27 项（20 可交互 + 7 文本行）两两不重叠"
                                  "（比对 %1 对；重叠 %2 对；最大交叠面积 %3 px²%4）—— "
                                  "贴边不算重叠，容差 1 px²")
                       .arg(allRects.size() * (allRects.size() - 1) / 2)
                       .arg(overlaps).arg(worst, 0, 'f', 1)
                       .arg(worstPair.isEmpty()
                                ? QString()
                                : QStringLiteral("，%1").arg(worstPair)));
        break;
    }
    case 24: {
        // ── UI-20：可见性**成对**（正例集合 ∧ 反例），兼作"可见性判定可判别"对照 ─
        //  正例：时间页上的 20 个靶控件**全部** isVisible()。
        //  反例：**非当前页**的 `searchQueryField` 必须 isVisible()==false
        //        （StackLayout 的契约就是"只有当前页可见"）。
        //  没有反例腿的话，"靶控件都可见"这条在 isVisible() 恒真的实现下也会绿。
        QStringList hidden;
        for (int k = 0; k < c->visItems.size(); ++k)
            if (!c->visItems[k]->isVisible())
                hidden.append(c->visNames[k]);
        const bool probeFound = (c->searchQueryField != nullptr);
        const bool probeVisible = probeFound && c->searchQueryField->isVisible();
        const int curIdx = c->pageStack ? c->pageStack->property("currentIndex").toInt() : -1;
        uiTimeMark(c.get(),
                   c->visItems.size() == 20 && hidden.isEmpty() && probeFound && !probeVisible,
                   QStringLiteral("UI-20 视觉层：可见性成对 —— 靶控件不可见 %1/%2；"
                                  "反例 searchQueryField（页栈 currentIndex=%3，非时间页）"
                                  "找到=%4 可见=%5（要求 false）%6")
                       .arg(hidden.size()).arg(c->visItems.size()).arg(curIdx)
                       .arg(probeFound ? QStringLiteral("true") : QStringLiteral("false"))
                       .arg(probeVisible ? QStringLiteral("true") : QStringLiteral("false"))
                       .arg(hidden.isEmpty()
                                ? QString()
                                : QStringLiteral("；隐藏的是：%1")
                                      .arg(hidden.join(QStringLiteral("、")))));

        // ── UI-21：七个文本行**都画出了内容**（非空、非占位符"?"）──────────
        //  与 UI-10a/UI-13/UI-14/UI-15 的分工：那几条管"值对不对"，这条管
        //  "**有没有东西**"—— 一条空串 Label 在视觉上就是一块空白，而它的值
        //  （空串）在数据面判据里可能根本不被断言（例如 UT 行从来没被判过）。
        QQuickItem *const *txt = c->visText;
        int blank = 0;
        QStringList shards;
        for (int k = 0; k < 7; ++k)
        {
            const QString t = txt[k] ? txt[k]->property("text").toString() : QString();
            const bool ok = txt[k] && !t.isEmpty() && t != QStringLiteral("—");
            if (!ok)
                ++blank;
            shards.append(QStringLiteral("%1=\"%2\"")
                              .arg(QString::fromUtf8(kUiVisTextZH[k]), t));
        }
        uiTimeMark(c.get(), blank == 0,
                   QStringLiteral("UI-21 视觉层：七个文本行都画出了内容（空/占位 %1 个）—— %2")
                       .arg(blank).arg(shards.join(QStringLiteral("  "))));
        // 读数（**不是判据**）：6 个单行文本"布局给的宽度 − 文本自然宽度"。
        // 为什么只当读数：QQuickText 的 implicitWidth = `ceil(contentWidth)`，而
        // Label 没设 `Layout.fillWidth` 时布局给的就是 implicitWidth ⇒ 这个差值
        // 恒在 [0,1) 的取整区间里。任何阈值都只会得出恒真的结论（假绿，血泪第 4 条）
        // —— 所以它记录余量、但不冒充判据。
        {
            QStringList slacks;
            for (int k = 0; k < 6; ++k)
                slacks.append(QStringLiteral("%1:%2")
                                  .arg(QString::fromUtf8(kUiVisTextZH[k]))
                                  .arg(c->visText[k]
                                           ? c->visText[k]->width()
                                                 - c->visText[k]->property("contentWidth").toDouble()
                                           : -999.0,
                                       0, 'f', 1));
            c->details.append(QStringLiteral(
                "VIS-note 单行文本余量 px（宽度−contentWidth）：**只作读数、不判** —— 它"
                "立不了阈值。宽度未被布局约束时它是 ceil(contentWidth)−contentWidth，"
                "落在 [0,1) 的取整区间里（这种小值没有信息量）；一旦该行被布局挤压或拉伸，"
                "它就跳到**任意量级**（负值 = 文本宽于自身 item、会被裁；正的偏大值 = item "
                "被拉开）⇒ 正常态没有分辨力、被破坏时又没边。见 "
                "docs/T30_TIME_VISUAL.zh_CN.md §4.2：%1")
                                  .arg(slacks.join(QStringLiteral(" "))));
        }
        break;
    }
    case 25: {
        // ── UI-22：**中心点命中测试落回自己**（"点得到"）────────────────────
        //  逐控件把"中心点"喂给场景图，问它这一点最深的**可见**子项是谁，再沿父链看
        //  能不能回到靶控件本身。这是本组里**唯一**一条能抓住下面这三类缺陷的判据，
        //  而"矩形在页内""不被祖先裁掉""两两不重叠"对它们**统统无感**：
        //    · 有透明覆盖层压在控件上（矩形都在、isVisible 也都真）
        //    · z 序错（后加的一个空 Item 盖在按钮上）
        //    · 被兄弟控件盖住（矩形相交但交叠面积 < 1 px²，UI-19 的容差放过去了）
        //  **负控会翻转它**：`STELQUICK_TIME_VISUAL_NEGCTL=1` 把年框置为 hidden 之后，
        //  它中心点命中的就不再是它自己 —— 所以这条判据不是恒真。
        QQuickItem *root = c->window->contentItem();
        int missed = 0;
        QStringList missedNames;
        for (int k = 0; k < c->visItems.size(); ++k)
        {
            if (!uiHitTestHits(root, c->visItems[k]))
            {
                ++missed;
                missedNames.append(c->visNames[k]);
            }
        }
        uiTimeMark(c.get(), c->visItems.size() == 20 && missed == 0,
                   QStringLiteral("UI-22 视觉层：20 个可交互靶控件的中心点命中测试都落回"
                                  "自己（落空 %1 个）—— 透明覆盖层/z 序错/被兄弟盖住这三类"
                                  "只有这条看得见%2")
                       .arg(missed)
                       .arg(missedNames.isEmpty()
                                ? QString()
                                : QStringLiteral("；落空的是：%1")
                                      .arg(missedNames.join(QStringLiteral("、")))));

        // ── UI-23：对比度下限（**活引擎腿**）───────────────────────────────
        //  只纳入**声明了 color** 的文本行（Button/CheckBox/ComboBox 的文字底色由
        //  style 绘制、读不到 color ⇒ 明确不纳入，不假装覆盖）。
        QQuickItem *const *txt = c->visText;
        int noColor = 0, belowFloor = 0, above45 = 0, above30 = 0;
        double minRatio = 1e9;
        QStringList ratios;
        for (int k = 0; k < 7; ++k)
        {
            if (!txt[k])
            {
                ++noColor;
                continue;
            }
            const QColor fg = txt[k]->property("color").value<QColor>();
            if (!fg.isValid())
            {
                ++noColor;
                continue;
            }
            const QColor bg = uiEffectiveBackground(txt[k], QColor(QStringLiteral("#ffffff")));
            const double r = uiContrastRatio(fg, bg);
            minRatio = qMin(minRatio, r);
            if (r < kUiContrastFloor)
                ++belowFloor;
            if (r >= 4.5)
                ++above45;
            if (r >= 3.0)
                ++above30;
            ratios.append(QStringLiteral("%1=%2:1(%3/%4)")
                              .arg(QString::fromUtf8(kUiVisTextZH[k]))
                              .arg(r, 0, 'f', 2).arg(fg.name(), bg.name()));
        }
        uiTimeMark(c.get(), belowFloor == 0 && noColor == 0,
                   QStringLiteral("UI-23 视觉层：文本对比度 ≥ %1（低于下限 %2 项、读不到色 %3 项）"
                                  "—— %4；最小 %5:1；参考：≥4.5 的 %6 项、≥3.0 的 %7 项")
                       .arg(kUiContrastFloor, 0, 'f', 1).arg(belowFloor).arg(noColor)
                       .arg(ratios.join(QStringLiteral(" ")))
                       .arg(minRatio < 1e9 ? minRatio : -1.0, 0, 'f', 2)
                       .arg(above45).arg(above30));
        break;
    }
    case 26: {
        // ── UI-24：度量的**纯逻辑腿**（判别性对照）──────────────────────────
        //  为什么必须有这条：UI-18/UI-19/UI-23 用的都是本文件自写的小函数。若其中
        //  任何一个写成 `return 21.0` / `return true`，活体判据会在**任何**输入下
        //  全绿 —— 而"全绿"看起来毫无破绽（血泪第 8 条：规则正确与规则被调用是两件
        //  事）。所以另立一条，用**已知输入 ⇒ 已知输出**验算它们：
        //    · WCAG 对比度：黑白 = **21**（WCAG 标准值，不从被测代码推出）；
        //      同色 = 1；交换参数不变（对称）。
        //    · 矩形包含：真矩形在自身内 = true；同一矩形右移 10000 px = false；
        //      放大到超出 = false。
        //    · 交叠面积：两个可手算的矩形 = **25**（(10−5)×(10−5)）。
        const double cBW = uiContrastRatio(QColor(QStringLiteral("#000000")),
                                          QColor(QStringLiteral("#ffffff")));
        const double cSame = uiContrastRatio(QColor(QStringLiteral("#9e9e9e")),
                                             QColor(QStringLiteral("#9e9e9e")));
        const double cAB = uiContrastRatio(QColor(QStringLiteral("#1565c0")),
                                           QColor(QStringLiteral("#ffffff")));
        const double cBA = uiContrastRatio(QColor(QStringLiteral("#ffffff")),
                                           QColor(QStringLiteral("#1565c0")));
        const QRectF outer(0.0, 0.0, 100.0, 50.0);
        const QRectF inner(10.0, 10.0, 30.0, 20.0);
        const bool inTrue = uiRectInside(inner, outer, 1.0);
        const bool inShift = uiRectInside(inner.translated(10000.0, 0.0), outer, 1.0);
        const bool inBig = uiRectInside(QRectF(10.0, 10.0, 500.0, 20.0), outer, 1.0);
        const double area = uiIntersectArea(QRectF(0.0, 0.0, 10.0, 10.0),
                                            QRectF(5.0, 5.0, 10.0, 10.0));
        const double areaTouch = uiIntersectArea(QRectF(0.0, 0.0, 10.0, 10.0),
                                                 QRectF(10.0, 0.0, 10.0, 10.0));
        //    · 命中判定（父 100×100；子 A(0,0,50,50)、子 B(25,0,50,50)，B 后声明 ⇒ 在上）：
        //      点 (30,25) 落在 A、B 的交集里 ⇒ 必须命中 **B（下标 1）**；
        //      点 (80,80) 两者都不含 ⇒ −1；点在父外 (200,200) ⇒ −1。
        const QRectF hitParent(0.0, 0.0, 100.0, 100.0);
        QList<QRectF> hitKids;
        hitKids << QRectF(0.0, 0.0, 50.0, 50.0) << QRectF(25.0, 0.0, 50.0, 50.0);
        const int hitTop = uiHitPickChild(hitParent, hitKids, QPointF(30.0, 25.0));
        const int hitNone = uiHitPickChild(hitParent, hitKids, QPointF(80.0, 80.0));
        const int hitOut = uiHitPickChild(hitParent, hitKids, QPointF(200.0, 200.0));
        const bool ok = qAbs(cBW - 21.0) < 1e-6 && qAbs(cSame - 1.0) < 1e-9
                     && qAbs(cAB - cBA) < 1e-12 && inTrue && !inShift && !inBig
                     && qAbs(area - 25.0) < 1e-9 && qAbs(areaTouch) < 1e-12
                     && hitTop == 1 && hitNone == -1 && hitOut == -1;
        uiTimeMark(c.get(), ok,
                   QStringLiteral("UI-24 视觉层纯逻辑腿（判别性对照）：对比度 黑白=%1"
                                  "（期望 21）、同色=%2（期望 1）、对称差=%3；"
                                  "矩形包含 真=%4/右移 10000px=%5/放大越界=%6"
                                  "（期望 true/false/false）；交叠面积=%7（期望 25）、"
                                  "贴边=%8（期望 0）；命中判定 交叠处=%9（期望 1=上层）、"
                                  "无子项命中=%10（期望 −1）、父外=%11（期望 −1）")
                       .arg(cBW, 0, 'f', 6).arg(cSame, 0, 'f', 9)
                       .arg(qAbs(cAB - cBA), 0, 'e', 1)
                       .arg(inTrue ? QStringLiteral("true") : QStringLiteral("false"),
                            inShift ? QStringLiteral("true") : QStringLiteral("false"),
                            inBig ? QStringLiteral("true") : QStringLiteral("false"))
                       .arg(area, 0, 'f', 3).arg(areaTouch, 0, 'f', 3)
                       .arg(hitTop).arg(hitNone).arg(hitOut));
        break;
    }
    default: {
        uiTimeFinish(app, c.get());
        return;
    }
    }
    ++c->phase;
    uiTimeAdvance(app, c);
}

void uiTimeAdvance(QGuiApplication *app, std::shared_ptr<UiTimeCheck> c)
{
    QTimer::singleShot(c->tickMs, app, [app, c]() { uiTimeStep(app, c); });
}

int runUiTimeCheck(QGuiApplication *app, QQuickWindow *window, stelapp::AppFacade *facade)
{
    auto c = std::make_shared<UiTimeCheck>();
    c->window = window;
    c->facade = facade;
    std::printf("TIMEUICHECK: 开始（最外层注入：objectName 定位控件 + 窗口真实鼠标事件，"
                "共 27 条判据 = 数据面 19（T19/T22）+ 视觉层 8（T30），每相位 %dms）\n",
                c->tickMs);
    std::fflush(stdout);
    uiTimeStep(app, c);
    return 0;
}

// ══════════════════════════════════════════════════════════════════════════
// T20「返回」环 UI 层端到端自检：STELQUICK_RETURN_UI_CHECK=1
//
// I-REP-02 固定流程「开机→搜月球→定位→改时间→**返回**」的最后一环。
// 语义定案（2026-09-27，见 MainWindow.returnToSky 的注释）：
//   **返回 = 切回天空视口页 + 状态全保留**（不是撤销）。
//
// 为什么这一环**只能**做 UI 端到端判据：
//   "返回"的整个实现就是一句 QML（切 StackLayout）。C++ 侧没有任何可断言的
//   对象 —— 没有引擎命令、没有 AppFacade 方法。唯一的观测面是
//   **真实控件 + 真实事件 + pageStack.currentIndex**。这正是 T15/T18/T19
//   反复踩的那条线：不接线就测不到。
//
// 判据设计的核心是**成对 + 一条判别性对照**：
//   · 配对①「页面确实切回去了」         RT-04（读 currentIndex）
//   · 配对②「状态纹丝不动」             RT-05/RT-06（JD / 跟踪 / 选中）
//   · 前置「状态非平凡」                 RT-03 —— 没有它，配对②就是空转
//     （"什么都没保住"与"本来就什么都没有"都会让 RT-05/06 PASS）
//   · 负控「切页往返本身不许动世界」      RT-07（不碰时间，往返一次，AltAz 必须 < 0.05°）
//   · **判别性对照「返回 ≠ 撤销」**       RT-08（改 1 小时 → 返回 → AltAz 必须 > 5°）
//     ↑ 这是唯一能把"返回"与"撤销"分开的判据：若有人把返回实现成恢复快照，
//       RT-04/05/06/07 全绿而 RT-08 会红。
//
// 判据 11 条：RT-01..RT-11（含负控 RT-07 与判别性对照 RT-08）。
// 退出码沿用既有约定：0=PASS / 10=FAIL / 6=UNAVAILABLE。
// ══════════════════════════════════════════════════════════════════════════

//! 相位之间的等待。**必须逐相位显式给出**，不许统一设一个"够大"的数：
//! 驱动器的语义是"跑完本步**之后**等 nextDelayMs 毫秒"（T19 的血泪教训——把等待
//! 挂在读取步上 ⇒ 写入→读取实测间隔 0 ms，判据读到的是上一个 JD 的观测量）。
//!   · 纯 UI 相位（点按钮、读 currentIndex）→ tick 就够；
//!   · **写完引擎的相位**（+1 小时）→ 必须等世界真算完才能进下一拍。
const int kUiReturnTickMs = 400;
const int kUiReturnWorldSettleMs = 1200;  //!< 与 TimeCheck 的 kWorldSettleMs 同量级、同理由

// ── T29-W：前导窗口激活门（Windows 前台锁）───────────────────────────────────
//
// 背景（2026-09-29 W-T29 首跑实证）：Windows 上由 `schtasks /it` 投递到交互会话的
// 进程受 **foreground lock** 限制，窗口不会自动成为前台窗口；而 QQuickWindow 的
// **键**事件派发依赖 `activeFocusItem`（`keySink->forceActiveFocus()` 在窗口未激活
// 时拿不到 active focus）⇒ 注入的键被静默丢弃或落到别处。
//
// 实证（同一份二进制、同一轮）：INTERACTCHECK **IT-05（该套件第一次键注入）红**，
// 紧接的 IT-06 相位做了 `requestActivate` 重试（日志 "窗口未激活（尝试 1/3）"），
// 窗口激活之后**余下 11 条键/手势判据全绿**；RETURNUI 完全没有激活门，而它**唯一**
// 的键注入 RT-10 恰好红。⇒ 缺的是仪器，不是产品。
//
// 处置沿 T22「有界就绪门」先例：在**任何判据之前**加有界激活门（≤3 次 ×400ms）。
// 门**超时不洗成 PASS**：只留一条 note，后续键类判据照原样判红（"仪器不可用"= 判红，
// 不是放宽）。门必须放在相位开头——放在判据之后会让重试重跑判据、计数虚高。
const int kUiActivationGateTries = 3;
const int kUiActivationGateMs = 400;

enum class UiGate
{
    Proceed,   //!< 可以进入本相位
    Retry      //!< 调用方必须**立即 return 且不 ++phase**，等重入
};

//! 前导窗口激活门。成功/超时都会把 `done` 置真（只在开头跑一次，不逐相位重试）。
UiGate uiWindowActivationGate(QQuickWindow *window, int &retry, bool &done,
                              QStringList &details)
{
    if (done)
        return UiGate::Proceed;
    if (window && window->isActive())
    {
        done = true;
        details.append(QStringLiteral("T29W-note 前导窗口激活门：窗口已激活（重试 %1 次）")
                           .arg(retry));
        return UiGate::Proceed;
    }
    if (retry < kUiActivationGateTries)
    {
        ++retry;
        if (window)
            window->requestActivate();
        details.append(QStringLiteral("T29W-note 前导窗口激活门：窗口未激活（尝试 %1/%2），"
                                      "requestActivate 后重入本相位")
                           .arg(retry).arg(kUiActivationGateTries));
        return UiGate::Retry;
    }
    done = true;
    details.append(QStringLiteral("T29W-note 前导窗口激活门超时：%1 次 requestActivate 后"
                                  "窗口仍未激活（isActive=false）⇒ 键类判据的前提不成立。"
                                  "**不洗成 PASS**：后续判据照原样判红")
                       .arg(retry));
    return UiGate::Proceed;
}

//! 焦点快照（T29-W 诊断）：给"键到没到、守卫有没有吞"提供一个**可直接读的事实**，
//! 免得靠"受理=?"这类间接读数反推（T29 的教训：`QKeyEvent` 的 accepted 语义不直观）。
//! 每一项都是**原始状态**，不复刻任何被测逻辑（血泪第 4 条）。
QString uiFocusSnapshot(QQuickWindow *window, QQuickItem *keySink,
                        const stelapp::ActionRouter *router)
{
    QObject *fo = QGuiApplication::focusObject();
    QQuickItem *afi = window ? window->activeFocusItem() : nullptr;
    return QStringLiteral("windowActive=%1 keySinkHasActiveFocus=%2 activeFocusItem=%3 "
                          "focusObject=%4 canDispatchToSky=%5")
        .arg(window && window->isActive() ? "true" : "false")
        .arg(keySink && keySink->hasActiveFocus() ? "true" : "false")
        .arg(afi ? (afi->objectName().isEmpty() ? QStringLiteral("(未命名)")
                                                : afi->objectName())
                 : QStringLiteral("(null)"))
        .arg(fo ? QString::fromLatin1(fo->metaObject()->className()) : QStringLiteral("(null)"))
        .arg(router ? (router->canDispatchToSky() ? QStringLiteral("true")
                                                  : QStringLiteral("false"))
                    : QStringLiteral("(n/a)"));
}

//! 页名 → 索引。**只在 QML 里定义一处**（MainWindow.pageIndex），C++ 不复制一份——
//! 否则"加了页面忘了改另一处"会退化成只在运行时才暴露的错位。
int uiPageIndexOf(QQuickWindow *w, const char *page)
{
    return w->property("pageIndex").toMap()
        .value(QString::fromLatin1(page), -1).toInt();
}

int uiCurrentPageIndex(QQuickItem *pageStack)
{
    return pageStack ? pageStack->property("currentIndex").toInt() : -1;
}

bool uiReturnClick(QQuickWindow *window, QQuickItem *item)
{
    return item ? uiLocateClick(window, uiLocateCenter(item)) : false;
}

int uiReturnSendKey(QQuickWindow *window, QQuickItem *keySink, int key)
{
    // ⚠️ 键盘事件要有 activeFocusItem 才会被派发到 QML 的 Keys 处理器。显式把焦点
    // 交给 skyKeySink（等价于用户先点一下窗口）——这不是"自己构造输入"：断言的对象
    // 仍是"经窗口投递的**真实事件**有没有走通那条接线"。返回是否拿到了焦点。
    if (keySink)
        keySink->forceActiveFocus();
    QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier);
    QKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier);
    QCoreApplication::sendEvent(window, &press);
    QCoreApplication::sendEvent(window, &release);
    return (press.isAccepted() || release.isAccepted()) ? 1 : 0;
}

//! 被选中目标当前的地平坐标单位向量 = "星空动了多少"的观测量。
//! 与 TimeCheck::objAltAz 同一口径（`getAltAzPosAuto` 依赖 `getJD()` ⇒ 必须让帧
//! 跑过一轮才看得到新值，所以等待要挂在**写入步**上，见 nextDelayMs）。
//! 零向量（没选中）也能算 angle，会静默变成"变化 0°" ⇒ 调用点必须先查长度。
Vec3d uiReturnAltAz()
{
    StelObjectMgr *m = StelApp::isInitialized() ? &StelApp::getInstance().getStelObjectMgr()
                                                : nullptr;
    if (!m)
        return Vec3d(0.);
    const QList<StelObjectP> &sel = m->getSelectedObject();
    if (sel.isEmpty() || !sel.first())
        return Vec3d(0.);
    StelCore *core = StelApp::getInstance().getCore();
    if (!core)
        return Vec3d(0.);
    Vec3d v = sel.first()->getAltAzPosAuto(core);
    v.normalize();
    return v;
}

bool uiReturnAltAzValid(const Vec3d &v)
{
    return v.norm() > 0.5;   //!< 归一化向量范数≈1；零向量 = 没有可测的目标
                             //!< （VecMath 刻意 delete 了 length()，用 norm()）
}

double uiReturnAngleDeg(const Vec3d &a, const Vec3d &b)
{
    return a.angle(b) * 180.0 / M_PI;
}

struct UiReturnCheck
{
    QQuickWindow *window = nullptr;
    stelapp::AppFacade *facade = nullptr;
    QQuickItem *returnButton = nullptr;
    QQuickItem *pageStack = nullptr;
    QQuickItem *navSearch = nullptr;
    QQuickItem *navTime = nullptr;
    QQuickItem *addHour = nullptr;
    QQuickItem *keySink = nullptr;
    QStringList details;
    int passed = 0;
    int total = 0;
    int phase = 0;
    int nextDelayMs = kUiReturnTickMs;

    int idxSky = -1;
    int idxSearch = -1;
    int idxTime = -1;

    // 跨相位快照
    double jdBefore = 0.0;
    QString trackedBefore;
    QString sidBefore;
    Vec3d altAzRoundTripRef = Vec3d(0.);
    Vec3d altAzBeforeHour = Vec3d(0.);
    int layoutWait = 0;   //!< T22 布局就绪门的重试计数（见 uiLayoutReady 注释）
    // T29-W：前导窗口激活门（见 uiWindowActivationGate 注释）
    stelapp::ActionRouter *router = nullptr;   //!< 只用于诊断读 canDispatchToSky()
    int prologueRetry = 0;
    bool prologueDone = false;
};

void uiReturnMark(UiReturnCheck *c, bool ok, const QString &line)
{
    ++c->total;
    if (ok)
        ++c->passed;
    c->details.append(line + (ok ? QStringLiteral("：OK") : QStringLiteral("：FAIL")));
}

void uiReturnFinish(QGuiApplication *app, UiReturnCheck *c)
{
    const bool pass = c->total > 0 && c->passed == c->total;
    for (const QString &line : c->details)
        std::printf("RETURNUICHECK: %s\n", line.toUtf8().constData());
    std::printf("RETURNUICHECK: 判据 %d/%d\n", c->passed, c->total);
    std::printf("RETURNUICHECK: VERDICT=%s\n", pass ? "PASS" : "FAIL");
    std::fflush(stdout);
    app->exit(pass ? 0 : 10);
}

void uiReturnUnavailable(QGuiApplication *app, UiReturnCheck *c, const QString &why)
{
    for (const QString &line : c->details)
        std::printf("RETURNUICHECK: %s\n", line.toUtf8().constData());
    std::printf("RETURNUICHECK: %s\n", why.toUtf8().constData());
    std::printf("RETURNUICHECK: VERDICT=UNAVAILABLE\n");
    std::fflush(stdout);
    app->exit(6);
}

void uiReturnAdvance(QGuiApplication *app, std::shared_ptr<UiReturnCheck> c);

void uiReturnStep(QGuiApplication *app, std::shared_ptr<UiReturnCheck> c)
{
    // 🔴 T29-W 前导窗口激活门 —— **必须在 switch 之前**（任何判据之前）。
    // 见 uiWindowActivationGate 的注释：门放在判据之后会让重试重跑判据、计数虚高。
    if (uiWindowActivationGate(c->window, c->prologueRetry, c->prologueDone, c->details)
        == UiGate::Retry)
    {
        QTimer::singleShot(kUiActivationGateMs, app, [app, c]() { uiReturnStep(app, c); });
        return;   // 不 ++phase：重入本相位
    }
    switch (c->phase)
    {
    case 0: {
        // ── 锚点 + 前置状态 ────────────────────────────────────────────────
        c->returnButton = c->window->findChild<QQuickItem *>(QStringLiteral("skyReturnButton"));
        c->pageStack    = c->window->findChild<QQuickItem *>(QStringLiteral("pageStack"));
        c->navSearch    = c->window->findChild<QQuickItem *>(QStringLiteral("navSearchButton"));
        c->navTime      = c->window->findChild<QQuickItem *>(QStringLiteral("navTimeButton"));
        c->addHour      = c->window->findChild<QQuickItem *>(QStringLiteral("timeAddHourButton"));
        c->keySink      = c->window->findChild<QQuickItem *>(QStringLiteral("skyKeySink"));

        c->idxSky    = uiPageIndexOf(c->window, "sky");
        c->idxSearch = uiPageIndexOf(c->window, "search");
        c->idxTime   = uiPageIndexOf(c->window, "time");

        const int have = (c->returnButton ? 1 : 0) + (c->pageStack ? 1 : 0)
                       + (c->navSearch ? 1 : 0) + (c->navTime ? 1 : 0)
                       + (c->addHour ? 1 : 0) + (c->keySink ? 1 : 0);
        const bool idxOk = c->idxSky >= 0 && c->idxSearch >= 0 && c->idxTime >= 0;
        uiReturnMark(c.get(), have == 6 && idxOk && c->returnButton->isVisible(),
                     QStringLiteral("RT-01 返回环锚点可寻且可见（skyReturnButton/pageStack/"
                                    "navSearchButton/navTimeButton/timeAddHourButton/skyKeySink "
                                    "找到 %1/6；页索引 sky=%2 search=%3 time=%4；"
                                    "返回按钮 visible=%5）")
                         .arg(have).arg(c->idxSky).arg(c->idxSearch).arg(c->idxTime)
                         .arg((c->returnButton && c->returnButton->isVisible())
                                  ? QStringLiteral("true") : QStringLiteral("false")));
        if (have != 6 || !idxOk || !c->returnButton->isVisible())
        {
            uiReturnUnavailable(app, c.get(),
                                QStringLiteral("缺 QML 锚点或页索引：返回环的全部判据都建立在"
                                               "「真实控件 + pageStack.currentIndex」上，"
                                               "缺一个即无法进行 —— 接线或命名断了"));
            return;
        }

        // 冻结时钟：本检查全部是"切页前后状态有没有变"的比较，时钟若在走会把
        // JD 位移比较淹掉，也会让 AltAz 自己漂。
        c->facade->setSimulationPaused(true);

        // 摆出**非平凡**的前置状态：选中一个天体并锁定跟踪。
        // 这一步是 RT-05/RT-06 有检验力的前提（见文件头 PK 说明）。
        QString fixture;
        if (!uiLocatePickFixture(c->facade, &fixture))
        {
            uiReturnUnavailable(app, c.get(),
                                QStringLiteral("无可用 fixture（Moon/Jupiter/Sirius/Vega/"
                                               "Polaris 全搜不到）—— 环境条件，非接线缺陷"));
            return;
        }
        c->facade->locateSelected(true);
        c->details.append(QStringLiteral("RT-fixture 已选中并锁定 %1（stableId=%2），"
                                         "时钟已暂停；页索引 sky=%3 search=%4 time=%5")
                              .arg(fixture, c->facade->objectInfo()->stableId())
                              .arg(c->idxSky).arg(c->idxSearch).arg(c->idxTime));

        // 切到时间页（真实点击）。下一拍才能读到 currentIndex 已变。
        uiReturnClick(c->window, c->navTime);
        c->nextDelayMs = kUiReturnTickMs;
        break;
    }
    case 1: {
        // ── RT-02：页面确实切到工具页（返回的起点成立）───────────────────
        const int cur = uiCurrentPageIndex(c->pageStack);
        uiReturnMark(c.get(), cur == c->idxTime,
                     QStringLiteral("RT-02 真实点击「时间（T19）」→ pageStack.currentIndex=%1"
                                    "（期望 %2）").arg(cur).arg(c->idxTime));
        // ── RT-03：前置状态**非平凡**（否则后面的"状态全保留"是空转）──────
        c->jdBefore      = c->facade->julianDay();
        c->trackedBefore = c->facade->trackedName();
        c->sidBefore     = c->facade->objectInfo()->stableId();
        const bool nontrivial = c->facade->isTracking()
                             && !c->sidBefore.isEmpty()
                             && c->facade->objectInfo()->hasSelection();
        uiReturnMark(c.get(), nontrivial,
                     QStringLiteral("RT-03 前置状态非平凡（跟踪=%1 选中=%2 stableId=\"%3\"）"
                                    "—— 没有这条，RT-05/06 的 PASS 可能只是"
                                    "\"本来就什么都没有\"")
                         .arg(c->facade->isTracking() ? QStringLiteral("true")
                                                      : QStringLiteral("false"),
                              c->facade->objectInfo()->hasSelection() ? QStringLiteral("true")
                                                                     : QStringLiteral("false"),
                              c->sidBefore));
        // 真实点击「返回天空」
        uiReturnClick(c->window, c->returnButton);
        c->nextDelayMs = kUiReturnTickMs;
        break;
    }
    case 2: {
        // ── RT-04：配对①——页面确实切回天空页 ─────────────────────────────
        const int cur = uiCurrentPageIndex(c->pageStack);
        uiReturnMark(c.get(), cur == c->idxSky,
                     QStringLiteral("RT-04 真实点击「返回天空」→ pageStack.currentIndex=%1"
                                    "（期望 %2）—— 配对①：页面确实切回去了")
                         .arg(cur).arg(c->idxSky));
        // ── RT-05：配对②——时间源纹丝不动（"返回"没有偷偷改时间）──────────
        const double d = std::fabs(c->facade->julianDay() - c->jdBefore);
        uiReturnMark(c.get(), d < 1e-12,
                     QStringLiteral("RT-05 返回后引擎 JD 位移 %1 天（要求 <1e-12）"
                                    "—— 配对②：返回不是撤销，也不是"
                                    "\"顺手重置一下\"")
                         .arg(d, 0, 'e', 3));
        // ── RT-06：配对②——选中与跟踪一并保留 ─────────────────────────────
        const bool keep = c->facade->isTracking()
                       && c->facade->trackedName() == c->trackedBefore
                       && c->facade->objectInfo()->stableId() == c->sidBefore;
        uiReturnMark(c.get(), keep,
                     QStringLiteral("RT-06 返回后跟踪/选中保留（tracking=%1 trackedName=\"%2\""
                                    "（此前 \"%3\"）stableId=\"%4\"（此前 \"%5\"））")
                         .arg(c->facade->isTracking() ? QStringLiteral("true")
                                                      : QStringLiteral("false"),
                              c->facade->trackedName(), c->trackedBefore,
                              c->facade->objectInfo()->stableId(), c->sidBefore));
        // 负控准备：记录当前 AltAz，然后**不碰时间**只做一次切页往返
        c->altAzRoundTripRef = uiReturnAltAz();
        uiReturnClick(c->window, c->navTime);
        c->nextDelayMs = kUiReturnTickMs;
        break;
    }
    case 3: {
        // 负控：立刻返回，中间什么都不做
        uiReturnClick(c->window, c->returnButton);
        c->nextDelayMs = kUiReturnTickMs;
        break;
    }
    case 4: {
        // ── RT-07：负控——"切页往返"本身不许动世界 ───────────────────────
        const Vec3d now = uiReturnAltAz();
        const bool valid = uiReturnAltAzValid(c->altAzRoundTripRef) && uiReturnAltAzValid(now);
        const double d = valid ? uiReturnAngleDeg(c->altAzRoundTripRef, now) : -1.0;
        uiReturnMark(c.get(), valid && d < 0.05,
                     QStringLiteral("RT-07 负控：不碰时间只做一次切页往返 → 目标 AltAz 变化 "
                                    "%1°（上限 <0.05）—— 排除\"切页本身就在动世界\"")
                         .arg(d, 0, 'f', 4));
        // 判别性对照准备：记录改时间前的 AltAz，切到时间页
        c->altAzBeforeHour = uiReturnAltAz();
        uiReturnClick(c->window, c->navTime);
        c->nextDelayMs = kUiReturnTickMs;
    }
        break;
    case 5: {
        // ── 写入步：真实点击「+1 时」（走 ActionRouter 透传引擎 StelAction）──
        // ⚠️ 等待挂在**本步**（nextDelayMs = 世界结算时间），不是下一步 —— T19 的坑。
        // ── T22：布局就绪门。时间页是**刚切过来**的，重压下页内控件还停留在
        //    "布局前尺寸"（实测 0 高），照这个坐标点就是空点 ⇒ RT-08 假红。
        //    理由与处置见 uiLayoutReady() 注释（改协议、不改判据、不洗 PASS）。
        if (uiCurrentPageIndex(c->pageStack) != c->idxTime || !uiLayoutReady(c->addHour)) {
            if (c->layoutWait < kUiLayoutWaitTries) {
                if (c->layoutWait == 0)
                    c->details.append(QStringLiteral(
                        "RT-note 时间页/「+1 时」按钮尚未就绪（页面=%1，期望 %2；按钮 %3×%4）"
                        "—— 有界等待（≤%5×100ms）")
                        .arg(uiCurrentPageIndex(c->pageStack)).arg(c->idxTime)
                        .arg(c->addHour->width(), 0, 'f', 0)
                        .arg(c->addHour->height(), 0, 'f', 0)
                        .arg(kUiLayoutWaitTries));
                ++c->layoutWait;
                if (uiCurrentPageIndex(c->pageStack) != c->idxTime)
                    uiReturnClick(c->window, c->navTime);   // 切页没生效就再点一次
                c->nextDelayMs = 100;
                uiReturnAdvance(app, c);   // ⚠️ 停在本相位：**不** ++phase
                return;
            }
            c->details.append(QStringLiteral(
                "RT-note 等待 %1ms 后时间页仍未就绪（页面=%2；按钮 %3×%4）⇒ 判红，"
                "**不洗成 PASS**")
                .arg(kUiLayoutWaitTries * 100).arg(uiCurrentPageIndex(c->pageStack))
                .arg(c->addHour->width(), 0, 'f', 0).arg(c->addHour->height(), 0, 'f', 0));
            uiReturnMark(c.get(), false,
                         QStringLiteral("RT-08 时间页未就绪（页面=%1/%2，按钮 %3×%4）—— "
                                        "仪器没接上：既不作退化证据，也**不作 PASS**")
                             .arg(uiCurrentPageIndex(c->pageStack)).arg(c->idxTime)
                             .arg(c->addHour->width(), 0, 'f', 0)
                             .arg(c->addHour->height(), 0, 'f', 0));
            uiReturnFinish(app, c.get());
            return;
        }
        uiReturnClick(c->window, c->addHour);
        c->nextDelayMs = kUiReturnWorldSettleMs;
        c->details.append(QStringLiteral("RT-note 已在时间页真实点击「+1 时」，等 %1ms 让引擎"
                                         "把新时刻算进位置，再返回")
                              .arg(kUiReturnWorldSettleMs));
        break;
    }
    case 6: {
        uiReturnClick(c->window, c->returnButton);
        c->nextDelayMs = kUiReturnTickMs;
        break;
    }
    case 7: {
        // ── RT-08：**判别性对照**——"返回 ≠ 撤销" ──────────────────────────
        const int cur = uiCurrentPageIndex(c->pageStack);
        const Vec3d now = uiReturnAltAz();
        const bool valid = uiReturnAltAzValid(c->altAzBeforeHour) && uiReturnAltAzValid(now);
        const double moved = valid ? uiReturnAngleDeg(c->altAzBeforeHour, now) : -1.0;
        uiReturnMark(c.get(), valid && cur == c->idxSky && moved > 5.0,
                     QStringLiteral("RT-08 判别性对照（返回≠撤销）：改 1 小时后返回 → "
                                    "页面=%1（期望 %2）且目标 AltAz 变化 %3°（门槛 >5.0）"
                                    "—— 若把返回实现成恢复快照，这条会红")
                         .arg(cur).arg(c->idxSky).arg(moved, 0, 'f', 4));
        // Esc 判据准备：切到搜索页
        uiReturnClick(c->window, c->navSearch);
        c->nextDelayMs = kUiReturnTickMs;
        break;
    }
    case 8: {
        // ── RT-09：起点确认（Esc 前确实在非天空页）──────────────────────
        const int cur = uiCurrentPageIndex(c->pageStack);
        uiReturnMark(c.get(), cur == c->idxSearch,
                     QStringLiteral("RT-09 Esc 起点：真实点击「搜索天体」→ currentIndex=%1"
                                    "（期望 %2）").arg(cur).arg(c->idxSearch));
        // 投递真实 Esc 键（先例：AC-12 从窗口投递 Q 键）
        const int accepted = uiReturnSendKey(c->window, c->keySink, Qt::Key_Escape);
        c->details.append(QStringLiteral("RT-note 已向窗口投递真实 Esc（是否被受理=%1）")
                              .arg(accepted ? QStringLiteral("true") : QStringLiteral("false")));
        // T29-W 诊断：Esc 派发后立刻取焦点快照。uiReturnSendKey 内部先
        // forceActiveFocus(keySink)，所以**这一拍的状态就是 QML 守卫当时看到的状态**
        // ——RT-10 红时靠它区分"键没送到"与"守卫把 Esc 吞了"（首跑靠间接读数反推，
        // 结论互相矛盾，才补的这条探针）。
        c->details.append(QStringLiteral("RT-note Esc 派发后焦点快照：%1")
                              .arg(uiFocusSnapshot(c->window, c->keySink, c->router)));
        c->nextDelayMs = kUiReturnTickMs;
        break;
    }
    case 9: {
        // ── RT-10：Esc 返回链路是活的 ─────────────────────────────────────
        const int cur = uiCurrentPageIndex(c->pageStack);
        uiReturnMark(c.get(), cur == c->idxSky,
                     QStringLiteral("RT-10 非天空页投递真实 Esc → currentIndex=%1"
                                    "（期望 %2）—— 键盘返回与按钮返回"
                                    "**走同一条路径**").arg(cur).arg(c->idxSky));
        // 负控：在天空页再按一次 Esc（页已在该页 → 必须什么都不发生）
        c->jdBefore = c->facade->julianDay();
        uiReturnSendKey(c->window, c->keySink, Qt::Key_Escape);
        c->nextDelayMs = kUiReturnTickMs;
        break;
    }
    default: {
        // ── RT-11：负控——天空页上的 Esc 无副作用 ─────────────────────────
        const int cur = uiCurrentPageIndex(c->pageStack);
        const double d = std::fabs(c->facade->julianDay() - c->jdBefore);
        uiReturnMark(c.get(), cur == c->idxSky && d < 1e-12,
                     QStringLiteral("RT-11 负控：天空页上按 Esc → currentIndex=%1（不变）"
                                    "且 JD 位移 %2 天（<1e-12）—— Esc 在天空页不被拦、"
                                    "也不产生副作用").arg(cur).arg(d, 0, 'e', 3));
        uiReturnFinish(app, c.get());
        return;
    }
    }
    ++c->phase;
    uiReturnAdvance(app, c);
}

void uiReturnAdvance(QGuiApplication *app, std::shared_ptr<UiReturnCheck> c)
{
    const int d = c->nextDelayMs;
    c->nextDelayMs = kUiReturnTickMs;   // 复位默认值，相位各自按需覆盖
    QTimer::singleShot(d, app, [app, c]() { uiReturnStep(app, c); });
}

// ══════════════════════════════════════════════════════════════════════════
// T25 交互级（键盘/滚轮）UI 层端到端自检：STELQUICK_INTERACT_UI_CHECK=1
//
// 覆盖两条此前只有"C++ 直调"证据、没有"最外层注入"证据的交互面：
//   · **滚轮**：旧宿主链路 StelMainView::wheelEvent → StelApp::handleWheel 在合流
//     形态从未接过（QML 此前无任何 WheelHandler ⇒ 滚轮死路，T15 键盘死代码的
//     同款缺陷）。T25 修复 = SkyTestPage 挂 WheelHandler → AppFacade::wheelZoom
//     → handleWheel 保真转发。本套件从窗口投递真实 QWheelEvent 验证 FOV 真的动。
//   · **键盘活链**：routeKey 的 C++ 直调判据（ActionCheck/AC-12）证明不了
//     QKeyEvent 经窗口 → keySink 的 Keys.onPressed → routeKey 这条 QML 链是活的
//     （T15 死代码事故正是"直调全绿、接线全断"）。本套件注入真实 L 键
//     （引擎 actionIncrease_Time_Speed），断言 timeRate 变化 + dispatched 信号。
//
// 判据设计的成对与对照：
//   · IT-02/03 成对（FOV before>0 + after 变小）+ IT-03 方向对照（反向滚变大，
//     只会"缩放"不会"平移"才可能绿）
//   · IT-04 负控：时间页滚轮必须不动 FOV（页守卫；SkyViewport 只在天空页可见）
//   · IT-05 成对（dispatched 收到 actionIncrease_Time_Speed + timeRate 真变了）
//   · IT-06 焦点守卫活链（U-ACT-03 的 QML 端到端腿）：真实点击搜索框聚焦后
//     注入同键必须被拦（timeRate 不变 + dispatched 不发）
//   · IT-07/08/09（T27）：拖拽平移（**段内冻结仿真时间** + 双向反向对照）/ 点击
//     选中（绝对位置腿）/ 右键反选
//   · IT-10/11/12（T28）：捏合缩放。IT-10 成对（FOV 变小）、IT-11 方向对照
//     （反向捏合必须回升**且回到原值**——只会单向缩放的假链会红）、IT-12 负控
//     （时间页捏合必须不动 FOV，即页守卫）
//   · IT-13/14/15/16（T29）：输入法组合键。**先看 Qt 6.11.2 源码定事实**：
//     ① 组合期间 macOS **根本不产生 QKeyEvent**（qnsview_keys.mm:137 的闸门
//        `if (m_sendKeyEvent && m_composingText.isEmpty())`）⇒ "组合中天空快捷键
//        被抢"在平台层就不成立。IT-14 用注入**刻意比真实更严苛**：即使这些键
//        到达，也必须被守卫拦住（负控无守卫时必红 ⇒ 判据非摆设）。
//     ② 非组合态真正的漏洞在 QML 侧：Esc 分支写在焦点守卫之前（见
//        MainWindow.qml keySink 的 T29 注释）⇒ 搜索框里按 Esc 直接跳页。
//        **IT-14 是修复前必红的那一条**。
//     IT-13 = IME preedit 到达搜索框（合流形态的 IME 通路此前从未被测过）且
//     **不污染 text**（成对）；IT-15 = commit 真的写进 text（组合态归位）；
//     IT-16 = **判别性对照**——焦点离开输入控件后按 Esc 必须仍然返回天空页
//     （证明修复没有把 Esc 返回链路一起杀掉；改动过宽时这条红）。
//   · IT-17/18（T32）：**两段式 Esc**（浏览器习惯）—— 焦点在文本输入控件时，
//     第一段清空文本、第二段才返回天空页。T29 当时刻意没做（"清空输入框"是新增
//     交互特性，要单独立项），本轮就是那个立项。
//     IT-17 = 第一段：注入前文本**非空**（前提腿）+ 注入后文本被清空 + **页没动、
//     没有动作被派发**（三腿成对；只断言"清空了"会放过"清空完顺手跳页"的写法）。
//     IT-18 = 第二段：空文本再按 Esc ⇒ **返回天空页**，且**判别腿** pin 住机制——
//     注入时 `canDispatchToSky()` 必须为 false（即键确实走的守卫路径）。
//     没有这条腿，IT-18 会**靠回退路径偶然通过**：失活时守卫回真 ⇒ Esc 走 T20 的
//     返回分支同样回天空页，"第二段"根本没被验证却显示绿（血泪第 20 条一族）。
//     IT-16 依旧是本组的判别性对照：**焦点不在文本控件时，一次 Esc 就该返回**
//     （两段式不得把这条一键返回也变成两段）。
//
// 判据 18 条：IT-01..IT-18（T27 追加 IT-07/08/09，T28 追加 IT-10/11/12，
// T29 追加 IT-13/14/15/16，**T32 追加 IT-17/IT-18**：两段式 Esc）。
// 退出码沿用既有约定：0=PASS / 10=FAIL / 6=UNAVAILABLE。
//
// ── T31：环境门降级（"标记但继续"）──────────────────────────────────────────
// 起因（T29 实测）：`pmset` 电池档 `displaysleep=2` ⇒ 构建几分钟后屏幕已睡 ⇒
// `requestActivate()` 再也拿不到焦点 ⇒ `focusObject()` 恒 `nullptr`。旧行为是
// **环境门一失败就 `exit(10)`**，而 IT-06 在相位 7 ⇒ **后面 9 条判据全不跑**
// （连续 3 跑卡在"判据 5/6"）—— 报告残缺，还把"仪器测不到"记成了"产品失败"。
//
// 现在：引入**第三种结果 UNAVAILABLE**，与 PASS / FAIL 并列。
//   · 门失败 ⇒ 只把**焦点依赖**的判据记 UNAVAILABLE（IT-06 / IT-13..16；依据是
//     Qt 的投递路径，见 `kFocusGatedIds`），**焦点无关的 IT-07..IT-12 照常跑**；
//   · 收尾报 `判据 p/t（另 N 条 UNAVAILABLE：…）` + `VERDICT=UNAVAILABLE` + `rc=6`
//     ⇒ 脚本层能区分"产品坏了(10)"与"仪器不可用(6)"，且**绝不洗成 PASS**；
//   · **相位编号一处未动**（靠 `focusGateFailed` 标志在相位内部跳过）—— 这正是当初
//     留待后续的顾虑（相位编号多任务共用，跳号的风险 > 收益）。
// **门在哪里判**：两处，都在**判据之前** ——
//   · **前导（相位 0）**：`uiWindowActivationGate` 超时 ⇒ 置标志（**这是主力路径**）。
//     ⚠️ 必须在 **IT-05（相位 6）之前**：Windows 实测窗口失活时键进不了 `keySink`
//     ⇒ IT-05 在 Windows 上也是激活依赖的（见 `kFocusGatedIds`）。
//   · **相位 7**：IT-06 自己的有界激活门（≤3 次）失败 ⇒ 置标志。
//   两处都只**置标志**（`uiInteractSetFocusGateFailed`）或就地记 IT-06
//   （`uiInteractEnterFocusUnavailable`）；记账规则见两个 helper 的注释。
// 负控：`STELQUICK_INTERACT_FORCE_FOCUSGATE_FAIL=1` 在相位 7 入口强制置标志
//   ⇒ 期望读数 = `IT-07..IT-12 全绿` + `UNAVAILABLE 恰好 IT-06,13,14,15,16,17,18` + `rc=6`
//   （前导门在负控里是**成功**的，所以 IT-05 照跑、不在跳过集合里）。
//   ⚠️ T32 追加 IT-17/IT-18 后：执行数从 12 变 11 + 收尾自检 1 = **仍是 12/12**，
//      只是 UNAVAILABLE 从 5 条变 7 条 —— 这正是"路径上少判一条、不报幽灵红/绿"的账。
// 收尾另有 `INTERACT-INTEGRITY` 纯逻辑腿：实际跳过序列必须等于分类表的**后缀**。
// ⚠️ 本降级**只针对环境门**；"切页后页码不对""视线基线无效"这类**真前提失败**仍旧
// 立即 `exit(10)`（那是套件布场坏了，继续跑没有意义）。
//
// 四个环境变量（全部**只在置位时有作用**，正题不设任何一个）：
//   · `STELQUICK_INTERACT_FORCE_INACTIVE=1`     —— 探针：强制窗口"不接受焦点"；
//   · `STELQUICK_INTERACT_PROBE_ACTIVATION=1`   —— 探针：门失败后**照跑**（测边界）；
//   · `STELQUICK_INTERACT_FORCE_FOCUSGATE_FAIL=1` —— 负控：相位 7 入口强制置失败标志；
//   · `STELQUICK_INTERACT_REQUEST_ACTIVATE=1`   —— **前台提升**：app 自己 `requestActivate()`
//      三次（0.15/0.40/0.90s）。原因见 `runUiInteractCheck` 里的长注释：脚本 `exec`
//      直启的进程不被 LaunchServices 认作 `.app` 实例，外部 `open -a` 只会另起幽灵
//      实例，且必然晚于前导激活门 1.2s 的窗口。
// ══════════════════════════════════════════════════════════════════════════

const int kUiInteractTickMs = 400;

struct UiInteractCheck
{
    QQuickWindow *window = nullptr;
    stelapp::AppFacade *facade = nullptr;
    QTimer *timer = nullptr;
    int phase = 0;
    int nextDelayMs = kUiInteractTickMs;
    int passed = 0;
    int total = 0;
    QStringList details;

    QQuickItem *keySink = nullptr;
    QQuickItem *pageStack = nullptr;
    QQuickItem *viewport = nullptr;
    QObject *pinchHandler = nullptr;      // T28：PinchHandler 是**对象**（handler），非 Item
    QQuickItem *navSky = nullptr;
    QQuickItem *navTime = nullptr;
    QQuickItem *navSearch = nullptr;
    QQuickItem *queryField = nullptr;
    int idxSky = -1;
    int idxTime = -1;
    int idxSearch = -1;

    double fovBefore = 0.0;      //!< IT-02 的写入步读数
    double fovAfterZoomIn = 0.0; //!< IT-02 断言后的读数（IT-03 的基线）
    double rateBefore = 0.0;     //!< IT-05 写入步读数
    double rateBefore2 = 0.0;    //!< IT-06 写入步读数
    int dispatchBase = 0;        //!< IT-05 写入步时的 dispatched 计数
    int dispatchBase2 = 0;       //!< IT-06 写入步时的 dispatched 计数
    int dispatchedCount = 0;     //!< 信号累计（连接在 run() 里）
    QString lastDispatchedId;
    // T27（IT-07..09）
    Vec3d dirBefore = Vec3d(0., 0., 0.);      //!< IT-07 冻结窗口的视线基线
    Vec3d dirAfterLeft = Vec3d(0., 0., 0.);   //!< IT-07 向左拖后
    Vec3d dirAfterRight = Vec3d(0., 0., 0.);  //!< IT-07 再向右拖后（反向对照）
    bool selBefore8 = false;              //!< IT-08 前提腿：点击前确无选中
    bool selAfter8 = false;               //!< IT-09 前提腿：点击后确有选中
    int activeRetry = 0;                  //!< IT-06 窗口激活门重试计数（有界 3 次）
    int centerRetry = 0;                  //!< IT-08 居中前提门重试计数（有界 2 次）
    // T28（IT-10..12）
    double fovBefore10 = 0.0;             //!< IT-10 捏开前的视场基线
    double fovAfter10 = 0.0;              //!< IT-10 捏开后（IT-11 的基线）
    double fovBefore12 = 0.0;             //!< IT-12 页守卫负控的基线
    // T29（IT-13..16）：输入法组合键
    QString imeTextBefore;                //!< IT-13 前提：注入 preedit 前的 text 快照
    QString imePreedit;                   //!< IT-13 读数：注入后的 preeditText
    bool imeComposing = false;            //!< IT-13 读数：注入后的 inputMethodComposing
    int pageBefore14 = -1;                //!< IT-14 前提：组合态按键前的页索引
    double rateBefore14 = 0.0;            //!< IT-14 前提：rate 基线
    int dispatchBase14 = 0;               //!< IT-14 前提：dispatched 基线
    int focusTries29 = 0;                 //!< IT-13 焦点门重试计数（有界 3 次）
    int pageBootstrap29 = 0;              //!< IT-16 布场重试计数（有界 2 次）
    // T29-W：前导窗口激活门（见 uiWindowActivationGate 注释）
    stelapp::ActionRouter *router = nullptr;  //!< 只用于诊断读 canDispatchToSky()
    int prologueRetry = 0;
    bool prologueDone = false;
    // T31：环境门降级（"标记但继续"）。语义见 uiInteractEnterFocusUnavailable 注释。
    bool focusGateFailed = false;  //!< 焦点门失败标志（置真后其余**焦点依赖**判据记 UNAVAILABLE）
    QString focusGateWhy;          //!< 失败原因（写进每条 UNAVAILABLE 行，供人读）
    int unavailable = 0;           //!< UNAVAILABLE 判据计数（**不计入** total/passed）
    QStringList unavailIds;        //!< 被跳过的判据 ID（收尾按"分类表后缀"自检，防漏跳/多跳）
    // T32（IT-17/IT-18）：两段式 Esc —— 有文本先清空（第一段）、空则返回天空（第二段）
    QString esc2TextBefore17;      //!< IT-17 前提腿：第一段 Esc 注入前的文本（**必须非空**）
    int esc2PageBefore17 = -1;     //!< IT-17：注入前的页索引（应=搜索页且注入后不动）
    int esc2DispatchBefore17 = 0;  //!< IT-17：注入前的 dispatched 基线（应不动）
    bool esc2It17Done = false;     //!< IT-17 是否已断言（case 26 会重入 ⇒ 防重复计数）
    QString esc2TextBefore18;      //!< IT-18 前提腿：第二段 Esc 注入前的文本（**必须为空**）
    int esc2PageBefore18 = -1;     //!< IT-18：注入前的页索引（必须=搜索页，不是已在天空页）
    bool esc2GuardBlocks = false;  //!< IT-18 判别腿：注入时 `canDispatchToSky()==false`
    int focusTries17 = 0;          //!< IT-17 焦点门重试计数（有界 3 次）
    int focusTries18 = 0;          //!< IT-18 焦点门重试计数（有界 3 次）
    int bootstrap32a = 0;          //!< IT-17 布场（切搜索页）重入计数（有界 2 次）
    int bootstrap32b = 0;          //!< IT-18 布场（回搜索页）重入计数（有界 2 次）
};

//! T31：**窗口激活依赖**判据的 ID 表（按相位序）。门失败时这些判据记 UNAVAILABLE。
//!
//! ⚠️ 这份表是**探针实测**出来的，不是推的 —— 而且中间被"**级联假红**"骗过一次：
//!   · 第一版按投递路径推"鼠标 `sendEvent` 直达窗口 ⇒ `deliverPointerEvent` 不查
//!     `isActive` ⇒ IT-07..12 不依赖"；
//!   · 负控首跑（窗口偶然 `isActive=false`）里 IT-07/08/09 **全红** ⇒ 我一度改判"依赖"；
//!   · 但那次红是**级联**：门失败后 case 8 被我 `break` 掉，连它末尾的"切回天空页"
//!     写入步也没执行 ⇒ IT-07 的舞台（页索引）错了 ⇒ 红的是**布场**、不是依赖关系。
//! 干净的测量 = **`STELQUICK_INTERACT_FORCE_INACTIVE=1` +
//! `STELQUICK_INTERACT_PROBE_ACTIVATION=1`**：强制 `Qt::WindowDoesNotAcceptFocus`
//! 且门失败后**照跑**，一趟就把边界全量取到（不必逐条重编）。
//!
//! 探针读数 —— ⚠️ **这是平台相关的，必须两个平台各测一遍**（本轮 W-T31 的实证收获）：
//!
//!   macOS（Metal，`判据 13/16`）：红 = **IT-06 / IT-13 / IT-14**
//!     · 激活**无关**（失活下仍绿）：IT-01..IT-05（锚点 / 滚轮 / **键**）与
//!       **IT-07..IT-12**（鼠标拖拽 / 点击 / 右键反选 / 捏合 —— `sendEvent` 直达窗口，
//!       确实不看 `isActive`）；
//!   Windows（原生 Vulkan，`判据 12/16`）：红 = **IT-05 / IT-06 / IT-13 / IT-16**
//!     · 🔴 **IT-05 在 Windows 上依赖激活**、在 macOS 上不依赖。原因就是 T29-W 那条：
//!       Windows 窗口未激活时 `forceActiveFocus()` 拿不到 active focus ⇒ 注入的键
//!       **根本进不了 `keySink`**（实测读数 `dispatched=0`、`lastActionId=""`）；
//!       macOS 的 `activeFocusItem` 在失活时仍被设置，所以键照样到。
//!     · 同理 IT-16（"焦点移开后 Esc 该返回天空页"）在 Windows 上红 —— 键也是靠激活才到。
//!
//! ⇒ **表取两平台的并集**（保守）：不是"依赖的判据都要跳"，而是"**跳掉可能报假红的**"。
//!   少判一条只是覆盖率损失；把"仪器测不到"报成 FAIL 才是语义错误。
//!
//! 表成员与理由：
//!   · **IT-05**：Windows 依赖（见上）；
//!   · **IT-06**：焦点守卫（`focusObject()`）；
//!   · **IT-13 / IT-14**：IME preedit / 组合态 Esc 都要 `focusObject()` 落在 TextInput 上；
//!   · **IT-15 / IT-16 探针下是绿的，仍归入依赖**：IT-15 的前提（"组合前 text"）由 IT-13
//!     建立，失活时它把空串当基线 ⇒ **偶然通过**、成对腿根本没成立；IT-16 是 IT-14 的
//!     **判别性对照**，对照的另一半不可判时它单独绿没有意义（血泪第 20 条）。
//!   · **IT-17 / IT-18（T32 追加）**：前提都是"焦点落在文本输入控件里"，与 IT-13 同源
//!     ⇒ 失活时前提不可能成立。IT-17 在失活下会**真红**（Esc 走了回退路径 ⇒ 跳页），
//!     IT-18 则相反 —— 回退路径同样"回到天空页"，**偶然通过**（同 IT-15/IT-16 一族）。
//!     两条都必须进表：一条会报幽灵 FAIL，一条会报幽灵 PASS，都是语义错误。
//!     ⚠️ **多平台并集的口径照旧**：IT-17/IT-18 靠**注入按键**（Windows 失活时键进不了
//!     `keySink`）+ 焦点前提，两个平台都依赖 ⇒ 并入表是安全的（表只可保守）。
//!
//! ⚠️ **顺序要求**：表按**相位序**书写（IT-05 在相位 6、IT-06 在相位 7、
//! IT-13..18 在相位 17+），收尾自检按"**从失败点起的后缀**"比对（见 uiInteractFinish）。
//! 因此**前导门的超时必须在 IT-05 之前就置标志**（见 `uiInteractStep` 顶部）—— 把门
//! 只放在相位 7 会让 IT-05 以 FAIL 的形式漏出去，那正是本任务要消灭的"幽灵产品缺陷"。
//! 同理，**新判据只能追加在表尾**（相位序在最后）：插在中间会让"后缀"不再是从失败点
//! 连续到末尾的那一段。
const char *const kFocusGatedIds[] = { "IT-05", "IT-06", "IT-13", "IT-14", "IT-15", "IT-16",
                                       "IT-17", "IT-18" };
const int kFocusGatedCount = int(sizeof(kFocusGatedIds) / sizeof(kFocusGatedIds[0]));

//! T31 **探针**：门失败时是否**照跑**（用来测量每条判据的窗口激活依赖边界）。
//! 必须与 `STELQUICK_INTERACT_FORCE_INACTIVE` 一起用：只有真的把窗口变成
//! `Qt::WindowDoesNotAcceptFocus`，"照跑"的读数才是边界数据；否则窗口是活的，
//! 探针跑出来和正题一样。
bool uiInteractProbeActivation()
{
    return qEnvironmentVariableIsSet("STELQUICK_INTERACT_PROBE_ACTIVATION");
}

void uiInteractMark(UiInteractCheck *c, bool ok, const QString &line)
{
    ++c->total;
    if (ok)
        ++c->passed;
    c->details.append(QStringLiteral("%1 %2").arg(ok ? "✓" : "✗", line));
}

//! T31：记一条 **UNAVAILABLE（不可判）** —— 与 PASS / FAIL 并列的**第三种结果**。
//!
//! 为什么不是 FAIL：FAIL 的语义是"**产品**有缺陷"；而"窗口拿不到系统焦点"是**仪器
//! 测不到**。把它记成 FAIL 会让人去追一个不存在的产品缺陷（T29 当时只能这么记，
//! 因为套件还没有第三态）。
//! 为什么不是 PASS：判据**根本没被验证** —— T29 的"不洗成 PASS"原则照旧成立。
//! ⇒ 独立成第三态：收尾报 `VERDICT=UNAVAILABLE` 且 `exit(6)`（沿用既有退出码约定
//! 0=PASS / 10=FAIL / **6=UNAVAILABLE**）。
void uiInteractUnavailable(UiInteractCheck *c, const char *id, const QString &why)
{
    ++c->unavailable;
    c->unavailIds << QString::fromLatin1(id);
    c->details.append(QStringLiteral("⊘ %1 UNAVAILABLE：%2 —— 本判据**未被验证**"
                                     "（既不是 PASS、也不是产品缺陷）")
                          .arg(QString::fromLatin1(id), why));
}

//! T31：环境门失败的**唯一置标志点**。真实失败（`isActive()` 重试 3 次后仍失活 /
//! 真实点击后 `focusObject()` 仍不是 TextInput）与**负控注入**
//! （`STELQUICK_INTERACT_FORCE_FOCUSGATE_FAIL=1`）**都调它** ⇒ 负控验证的就是这一句
//! 之后的所有控制流；而"重试循环"本身是既有代码（T27/T29 已有实测日志）。
//! 返回后**不终止套件**：焦点无关的判据（IT-07..IT-12）照常继续跑。
void uiInteractEnterFocusUnavailable(UiInteractCheck *c, const char *id, const QString &why)
{
    c->focusGateFailed = true;
    c->focusGateWhy = why;
    uiInteractUnavailable(c, id, why);
}

//! T31：**只置标志、不记账** —— 供"标志先于该判据的相位被置"的路径使用
//! （① 前导激活门超时；② IT-06 相位自身那轮激活门失败）。
//! 为什么不在这里记账：这两条路径都会**重入或后续才走到**对应判据的相位，记账统一由
//! **该判据自己的相位开头**做 ⇒ 每个 ID **恰好记一次**。
//! ⚠️ 重复记账会让收尾的"跳过集合 == 分类表后缀"自检**假红**（集合里同一 ID 出现两次），
//! 所以"谁记账"必须只有一个地方。
void uiInteractSetFocusGateFailed(UiInteractCheck *c, const QString &why)
{
    c->focusGateFailed = true;
    c->focusGateWhy = why;
}

void uiInteractFinish(QGuiApplication *app, UiInteractCheck *c)
{
    c->timer->stop();
    // T31：收尾**完整性自检**（纯逻辑腿）—— 门失败时"被跳过的 ID 序列"必须**恰好**
    // 等于分类表里**从失败点起的后缀**。漏跳（有激活依赖判据在错误前提下跑了）与
    // 多跳（把能跑的判据也丢了）都会让这条红。值来自两条**独立**路径：各相位实际记录
    // 的序列 vs 常量表 ⇒ 不是复述同一个事实（血泪第 4 条）。
    // ⚠️ 只在"门失败 ∧ 到此刻零 FAIL"时执行：若套件中途因**真前提失败**（如
    // "捏合判据需要天空页"）提前终止，被跳过的后缀本就不完整 —— 那是套件崩了，
    // 不是降级有缺陷（那种情况 rc 会走 10）。
    const bool preSelfFail = c->total == 0 || c->passed != c->total;
    if (c->focusGateFailed && !preSelfFail)
    {
        const QString first = c->unavailIds.isEmpty() ? QString() : c->unavailIds.first();
        int start = -1;
        for (int i = 0; i < kFocusGatedCount; ++i)
            if (first == QString::fromLatin1(kFocusGatedIds[i]))
            {
                start = i;
                break;
            }
        QStringList want;
        for (int i = qMax(start, 0); start >= 0 && i < kFocusGatedCount; ++i)
            want << QString::fromLatin1(kFocusGatedIds[i]);
        uiInteractMark(c, !want.isEmpty() && c->unavailIds == want,
                       QStringLiteral("INTERACT-INTEGRITY 环境门降级集合自检：实际跳过 [%1] "
                                      "↔ 期望后缀 [%2]（分类表取两平台并集 IT-05..IT-18，"
                                      "须逐项相等）")
                           .arg(c->unavailIds.join(QStringLiteral(",")),
                                want.join(QStringLiteral(","))));
    }
    // T31：退出码优先级 = **FAIL(10) > UNAVAILABLE(6) > PASS(0)**。理由：UNAVAILABLE
    // 表示"仪器不完整、该重跑"，而 FAIL 表示"真有东西坏了" —— 后者永远优先。
    const bool anyFail = c->total == 0 || c->passed != c->total;
    const bool partial = c->unavailable > 0;
    for (const QString &line : c->details)
        std::printf("INTERACTCHECK: %s\n", line.toUtf8().constData());
    if (partial)
        std::printf("INTERACTCHECK: 判据 %d/%d（另 %d 条 UNAVAILABLE：%s）\n", c->passed,
                    c->total, c->unavailable, c->unavailIds.join(QStringLiteral(","))
                                                   .toUtf8().constData());
    else
        std::printf("INTERACTCHECK: 判据 %d/%d\n", c->passed, c->total);
    std::printf("INTERACTCHECK: VERDICT=%s\n",
                anyFail ? "FAIL" : (partial ? "UNAVAILABLE" : "PASS"));
    std::fflush(stdout);
    app->exit(anyFail ? 10 : (partial ? 6 : 0));
}

//! 向窗口投递一次真实滚轮事件。返回事件是否被接受（诊断用，不作判据）。
int uiInteractSendWheel(QQuickWindow *window, const QPointF &scenePos, int dx, int dy)
{
    const QPointF global = window->mapToGlobal(scenePos.toPoint());
    QWheelEvent wheel(scenePos, global, QPoint(0, 0), QPoint(dx, dy),
                      Qt::NoButton, Qt::NoModifier, Qt::ScrollUpdate, false);
    QCoreApplication::sendEvent(window, &wheel);
    return wheel.isAccepted() ? 1 : 0;
}

//! T27：向窗口投递一次完整拖拽（press → N 步 move → release），全部真实事件。
//! move 带 LeftButton 按键态 ⇒ MouseArea onPositionChanged → skyMouseMove 链
//! 与真实拖拽一致。返回事件是否被受理（诊断用）。
int uiInteractSendDrag(QQuickWindow *window, const QPointF &from, const QPointF &to)
{
    const auto sendMove = [&](const QPointF &p) {
        const QPointF global = window->mapToGlobal(p.toPoint());
        QMouseEvent move(QEvent::MouseMove, p, p, QPointF(global),
                         Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(window, &move);
        return move.isAccepted() ? 1 : 0;
    };
    const QPointF globalFrom = window->mapToGlobal(from.toPoint());
    QMouseEvent press(QEvent::MouseButtonPress, from, from, QPointF(globalFrom),
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(window, &press);
    int acc = press.isAccepted() ? 1 : 0;
    const int steps = 5;
    for (int i = 1; i <= steps; ++i)
        acc += sendMove(from + (to - from) * (double(i) / steps));
    const QPointF globalTo = window->mapToGlobal(to.toPoint());
    QMouseEvent release(QEvent::MouseButtonRelease, to, to, QPointF(globalTo),
                        Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(window, &release);
    return acc + (release.isAccepted() ? 1 : 0);
}

//! T27：向窗口投递一次完整右键点击（press + release）。引擎 release 分支 =
//! 反选 deselect。QML MouseArea 必须先收到 press 才会 grab 并触发 onReleased
//! （只投 release 是死事件——与 IT-06"注入器隐式副作用"同族的教训）。
int uiInteractSendRightRelease(QQuickWindow *window, const QPointF &scenePos)
{
    const QPointF global = window->mapToGlobal(scenePos.toPoint());
    QMouseEvent press(QEvent::MouseButtonPress, scenePos, scenePos,
                      QPointF(global), Qt::RightButton, Qt::RightButton, Qt::NoModifier);
    QCoreApplication::sendEvent(window, &press);
    QMouseEvent release(QEvent::MouseButtonRelease, scenePos, scenePos,
                        QPointF(global), Qt::RightButton, Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(window, &release);
    return (press.isAccepted() || release.isAccepted()) ? 1 : 0;
}

//! T28：向窗口投递一次**原生捏合手势**（macOS 触控板的真实事件类型）。
//!
//! 真实链路（Qt 源码实证，非猜测）：
//!   macOS 平台层 beginGestureWithEvent/magnifyWithEvent/endGestureWithEvent
//!   → QNativeGestureEvent(Begin/Zoom/End, Zoom 的 value = **增量分数**)
//!   → QQuickDeliveryAgent::event() 的 `case QEvent::NativeGesture` 分支
//!     （`deliverSinglePointEventUntilAccepted`，**单点**投递）
//!   → QQuickPinchHandler（QQuickMultiPointHandler 显式放行 NativeGesture，
//!     qquickmultipointhandler.cpp:49）
//!   → setActiveScale(activeValue * (1 + value)) → scaleChanged(delta=乘法倍率)
//!   → QML onScaleChanged → AppFacade::pinchZoom。
//!
//! ⚠️ 两个易错点（首版注入实测踩中）：
//!   ① 必须发 **BeginNativeGesture / EndNativeGesture** 包住 Zoom——`setActive`
//!      只在 Begin 里做（qquickpinchhandler.cpp:516-528），裸发 Zoom 不是真实序列；
//!   ② `value` 是**增量分数**（0.25 ⇒ 引擎侧倍率 1.25），不是倍率本身。
//! 首版注入只发裸 Zoom 且 value 传成倍率 ⇒ FOV 60→60 不动（IT-10 红）。
int uiInteractSendNativePinch(QQuickWindow *window, const QPointF &scenePos, qreal factor)
{
    const QPointF global = window->mapToGlobal(scenePos.toPoint());
    const QPointingDevice *dev = QPointingDevice::primaryPointingDevice();
    auto send = [&](Qt::NativeGestureType type, qreal value) {
        QNativeGestureEvent ev(type, dev, 2, scenePos, scenePos, global, value, QPointF(0, 0));
        QCoreApplication::sendEvent(window, &ev);
        return ev.isAccepted() ? 1 : 0;
    };
    const int a1 = send(Qt::BeginNativeGesture, 0.0);
    const int a2 = send(Qt::ZoomNativeGesture, factor - 1.0);
    const int a3 = send(Qt::EndNativeGesture, 0.0);
    if (qEnvironmentVariableIsSet("STELQUICK_PINCH_DIAG"))
        std::printf("INTERACTCHECK: DIAG pinch 设备=%s type=%d caps=%d | Begin/Zoom(value=%g)/End "
                    "受理=%d/%d/%d\n",
                    dev ? dev->name().toUtf8().constData() : "(null)",
                    dev ? int(dev->type()) : -1,
                    dev ? int(dev->capabilities()) : -1,
                    factor - 1.0, a1, a2, a3);
    return a2;
}

//! 与 uiReturnSendKey 的唯一差别：**不 forceActiveFocus**。
//! IT-06（焦点守卫）的前提是"焦点真的在搜索框里"——uiReturnSendKey 会把焦点
//! 强行交给 keySink（T20 Esc 场景的兜底），那等于仪器亲手拆掉守卫前提，
//! IT-06 永远测不到真东西（首跑实测踩中）。真实用户场景本来就是：
//! 焦点在输入框 → 字母键进输入框 → 沿父链冒泡到 keySink → routeKey 被守卫拦。
int uiInteractSendKeyNoFocusGrab(QQuickWindow *window, int key)
{
    QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier);
    QKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier);
    QCoreApplication::sendEvent(window, &press);
    QCoreApplication::sendEvent(window, &release);
    return (press.isAccepted() || release.isAccepted()) ? 1 : 0;
}

//! T29：向窗口投递一次**输入法事件**（preedit 组合串 或 commit 提交串）。
//!
//! 真实链路（Qt 6.11.2 源码实证）：
//!   macOS 输入法 → QCocoaInputContext::sendInputMethodEvent
//!   → QInputMethodEvent
//!   → QQuickWindow::event() 的 `case QEvent::InputMethod`（qquickwindow.cpp:1643-1655）
//!   → QQuickDeliveryAgent::event() 同分支（qquickdeliveryagent.cpp:921-928）
//!       `QQuickItem *target = d->focusTargetItem();` ← **不是遍历、不是命中测试**：
//!       固定投给 activeFocusItem（无焦点时回落到 rootItem 的 scopedFocusItem）
//!   → QQuickTextInput::inputMethodEvent → processInputMethodEvent
//!       preedit 非空 ⇒ `hasImState = true`（qquicktextinput.cpp:3666）
//!                    ⇒ `inputMethodComposing` 变 true、`preeditText` 变组合串；
//!       commit 非空 ⇒ `internalInsert(commitString)`（:3624-3626）⇒ `text` 变。
//!   ⚠️ 所以本注入**必须落在窗口**上才测得到"焦点项是谁"这段判断；直接
//!      sendEvent 给搜索框会绕过它（=血泪第 3 条：仪器自己构造输入就测不到链路）。
//!
//! 组合串给一个 Cursor 属性——真实 IME 至少给光标位（Qt 侧 hasImState 只要求
//! preedit 非空，但多发一个属性让事件形状贴近真实；血泪第 18 条）。
int uiInteractSendInputMethod(QQuickWindow *window, const QString &preedit,
                              const QString &commit)
{
    // ⚠️ QInputMethodEvent 的拷贝赋值是 deleted ⇒ 不能"先默认构造再赋值"，
    // 必须一次构造到位（首版踩中，编译错 overload resolution selected deleted operator '='）。
    QList<QInputMethodEvent::Attribute> attrs;
    if (!preedit.isEmpty())
        attrs.append(QInputMethodEvent::Attribute(
            QInputMethodEvent::Cursor, preedit.size(), 1, QVariant()));
    QInputMethodEvent ev(preedit, attrs);
    if (!commit.isEmpty())
        ev.setCommitString(commit);
    QCoreApplication::sendEvent(window, &ev);
    return ev.isAccepted() ? 1 : 0;
}

//! T32：焦点是否落在**文本输入控件**里（= 守卫 `canDispatchToSky()` 的口径）。
//! 抽成一处是为了不让"焦点在输入框里吗"这个问题在套件里散成三份 lambda
//! （IT-06 / IT-13 各写过一个）。判型一律用 `inherits()` —— Qt Quick Controls 的
//! TextField 最派生类名是 `QQuickTextField`（QQuickTextInput 的子类），
//! 精确比 className 不命中（T25 的血泪：守卫因此假绿 15 个任务）。
bool uiInteractFocusIsTextItem()
{
    QObject *f = QGuiApplication::focusObject();
    return f && f->inherits("QQuickTextInput");
}

void uiInteractStep(QGuiApplication *app, std::shared_ptr<UiInteractCheck> c)
{
    // 🔴 T29-W 前导窗口激活门 —— **必须在 switch 之前**（任何判据之前）。
    // 首跑实证：IT-05（本套件第一次键注入）就是在窗口未激活时跑的 ⇒ 假红；
    // 激活后余下键/手势判据全绿。理由与有界性见 uiWindowActivationGate 注释。
    if (uiWindowActivationGate(c->window, c->prologueRetry, c->prologueDone, c->details)
        == UiGate::Retry)
    {
        QTimer::singleShot(kUiActivationGateMs, app, [app, c]() { uiInteractStep(app, c); });
        return;   // 不 ++phase：重入本相位
    }
    // ── T31：前导激活门**超时** ⇒ 在这里就把降级标志置上 ────────────────────────
    // ⚠️ 位置是关键：它必须在 **IT-05（相位 6）之前**。Windows 实测（W-T31 探针）表明
    // "窗口失活时键根本进不了 keySink" ⇒ **IT-05 在 Windows 上是激活依赖的**。
    // 若只把门放在相位 7（IT-06），IT-05 会以 **FAIL** 的形式漏出去 —— 那正是 T31 要
    // 消灭的"把仪器问题记成产品缺陷"。
    // 只在相位 0（尚未开始任何判据）判断一次；探针模式下只记 note（探针要照跑才测得到边界）。
    if (c->phase == 0 && !c->focusGateFailed && c->prologueDone && c->window
        && !c->window->isActive())
    {
        const QString why = QStringLiteral("前导窗口激活门超时：%1 次 requestActivate 后窗口"
                                           "仍未激活（isActive=false）")
                                .arg(c->prologueRetry);
        if (uiInteractProbeActivation())
        {
            c->details.append(QStringLiteral("PROBE-note 前导门超时（%1），探针模式下**照跑**，"
                                             "用来测各判据的激活依赖").arg(why));
        }
        else
        {
            uiInteractSetFocusGateFailed(c.get(), why);
        }
    }
    switch (c->phase)
    {
    case 0: {
        // ── IT-01：锚点存在 + 真实点击切到天空页 ─────────────────────────
        c->keySink = c->window->findChild<QQuickItem *>(QStringLiteral("skyKeySink"));
        c->pageStack = c->window->findChild<QQuickItem *>(QStringLiteral("pageStack"));
        c->viewport = c->window->findChild<QQuickItem *>(QStringLiteral("skyViewport"));
        // T28：PinchHandler 是 **QObject（handler）**，不是 Item —— findChild 靠
        // QObject::children() 递归，handler 由 QML 引擎设置为所属 Item 的子对象，
        // 故按名字查得到。查不到即"接线断了"，IT-10 无从谈起（硬锚点）。
        c->pinchHandler = c->window->findChild<QObject *>(QStringLiteral("skyPinchHandler"));
        c->navSky = c->window->findChild<QQuickItem *>(QStringLiteral("navSkyButton"));
        c->navTime = c->window->findChild<QQuickItem *>(QStringLiteral("navTimeButton"));
        c->navSearch = c->window->findChild<QQuickItem *>(QStringLiteral("navSearchButton"));
        c->queryField = c->window->findChild<QQuickItem *>(QStringLiteral("searchQueryField"));
        c->idxSky = uiPageIndexOf(c->window, "sky");
        c->idxTime = uiPageIndexOf(c->window, "time");
        c->idxSearch = uiPageIndexOf(c->window, "search");
        const bool anchors = c->keySink && c->pageStack && c->viewport && c->pinchHandler
                             && c->navSky && c->navTime && c->navSearch && c->queryField
                             && c->idxSky >= 0 && c->idxTime >= 0 && c->idxSearch >= 0;
        uiInteractMark(c.get(), anchors,
                       QStringLiteral("IT-01 交互锚点齐备（keySink=%1 pageStack=%2 "
                                      "viewport=%3 pinchHandler=%4 navSky/Time/Search=%5/%6/%7 "
                                      "queryField=%8 页索引=%9/%10/%11）")
                           .arg(c->keySink ? "有" : "无", c->pageStack ? "有" : "无",
                                c->viewport ? "有" : "无", c->pinchHandler ? "有" : "无",
                                c->navSky ? "有" : "无", c->navTime ? "有" : "无",
                                c->navSearch ? "有" : "无",
                                c->queryField ? "有" : "无")
                           .arg(c->idxSky).arg(c->idxTime).arg(c->idxSearch));
        if (!anchors)
        {
            std::printf("INTERACTCHECK: 缺锚点 —— 接线断了\nINTERACTCHECK: VERDICT=UNAVAILABLE\n");
            std::fflush(stdout);
            app->exit(6);
            return;
        }
        uiReturnClick(c->window, c->navSky);
        break;
    }
    case 1: {
        // ── IT-02 写入步：天空页投递真实滚轮（向前，10 格）─────────────────
        if (uiCurrentPageIndex(c->pageStack) != c->idxSky)
        {
            uiInteractMark(c.get(), false,
                           QStringLiteral("IT-02 前提失败：切天空页后 currentIndex=%1 "
                                          "（期望 %2）—— 天空页滚轮判据无效")
                               .arg(uiCurrentPageIndex(c->pageStack)).arg(c->idxSky));
            uiInteractFinish(app, c.get());
            return;
        }
        c->fovBefore = c->facade->fieldOfView();
        const QPointF p = uiLocateCenter(c->viewport);
        const int acc = uiInteractSendWheel(c->window, p, 0, 1200);
        c->details.append(QStringLiteral("IT-note 天空页视口中心 @(%1,%2) 投递滚轮"
                                         " dy=+1200（10 格），事件受理=%3，FOV before=%4")
                              .arg(p.x(), 0, 'f', 1).arg(p.y(), 0, 'f', 1).arg(acc)
                              .arg(c->fovBefore, 0, 'f', 3));
        break;
    }
    case 2: {
        // ── IT-02：滚轮向前 → FOV 真的变小（成对：before>0 且严格变小）────
        const double after = c->facade->fieldOfView();
        c->fovAfterZoomIn = after;
        uiInteractMark(c.get(), c->fovBefore > 0.0 && after > 0.0 && after < c->fovBefore,
                       QStringLiteral("IT-02 天空页滚轮向前 → FOV %1 → %2（应严格变小；"
                                      "不动即滚轮链路死，T10 起 T25 前的老病）")
                           .arg(c->fovBefore, 0, 'f', 4).arg(after, 0, 'f', 4));
        // 写入步：反向滚（向后，10 格）—— IT-03 的方向对照
        uiInteractSendWheel(c->window, uiLocateCenter(c->viewport), 0, -1200);
        break;
    }
    case 3: {
        // ── IT-03：方向对照——滚轮向后 → FOV 必须回升（只会单向缩放的假链会红）──
        const double after = c->facade->fieldOfView();
        uiInteractMark(c.get(), after > c->fovAfterZoomIn,
                       QStringLiteral("IT-03 方向对照：滚轮向后 → FOV %1 → %2（应回升，"
                                      "且回到接近 %3）")
                           .arg(c->fovAfterZoomIn, 0, 'f', 4).arg(after, 0, 'f', 4)
                           .arg(c->fovBefore, 0, 'f', 4));
        // 写入步：切到时间页（IT-04 负控的舞台）
        uiReturnClick(c->window, c->navTime);
        break;
    }
    case 4: {
        // ── IT-04 写入步：时间页投递同一次滚轮（负控：页守卫必须拦住）──────
        const int cur = uiCurrentPageIndex(c->pageStack);
        if (cur != c->idxTime)
        {
            uiInteractMark(c.get(), false,
                           QStringLiteral("IT-04 前提失败：切时间页后 currentIndex=%1 "
                                          "（期望 %2）—— 负控无效")
                               .arg(cur).arg(c->idxTime));
            uiInteractFinish(app, c.get());
            return;
        }
        c->fovBefore = c->facade->fieldOfView();   // 复用成员当"负控基线"
        uiInteractSendWheel(c->window, uiLocateCenter(c->pageStack), 0, 1200);
        break;
    }
    case 5: {
        // ── IT-05：负控——时间页滚轮后 FOV 纹丝不动 ───────────────────────
        const double after = c->facade->fieldOfView();
        uiInteractMark(c.get(), after == c->fovBefore,
                       QStringLiteral("IT-04 负控：时间页滚轮 dy=+1200 → FOV %1 → %2"
                                      "（应不变：WheelHandler 只挂天空页，引擎没收到就是"
                                      "没收到）").arg(c->fovBefore, 0, 'f', 4)
                           .arg(after, 0, 'f', 4));
        // 写入步：切回天空页 + 焦点交给 keySink + 注入真实 L 键
        uiReturnClick(c->window, c->navSky);
        c->rateBefore = c->facade->timeRate();
        c->dispatchBase = c->dispatchedCount;
        uiReturnSendKey(c->window, c->keySink, Qt::Key_L);
        c->details.append(QStringLiteral("IT-note 已投递真实 L 键（引擎 actionIncrease_"
                                         "Time_Speed），timeRate before=%1，dispatched "
                                         "基线=%2").arg(c->rateBefore).arg(c->dispatchBase));
        // T29-W 诊断：与 RT-note 同口径（见 uiFocusSnapshot 注释）
        c->details.append(QStringLiteral("IT-note L 键派发后焦点快照：%1")
                              .arg(uiFocusSnapshot(c->window, c->keySink, c->router)));
        break;
    }
    case 6: {
        // ── T31：门已失败（前导激活门超时）⇒ 本判据的前提同样不成立，记 UNAVAILABLE。
        //    ⚠️ **只在 Windows 上**它真的依赖激活（Mac 上键照样到，见 kFocusGatedIds 注释）；
        //    表取并集是"宁可少判一条，也不报幽灵 FAIL"。
        //    ⚠️ **写入步照做**（切搜索页）——"与判据分开"是本任务的硬纪律（首版整相位
        //    break 导致级联假红的教训）。
        if (c->focusGateFailed)
        {
            uiInteractUnavailable(c.get(), "IT-05", c->focusGateWhy);
            uiReturnClick(c->window, c->navSearch);
            break;
        }
        // ── IT-05：键盘活链——timeRate 真变了 + dispatched 信号真来了 ──────
        const double rate = c->facade->timeRate();
        const bool fired = c->dispatchedCount > c->dispatchBase;
        uiInteractMark(c.get(), rate != c->rateBefore && fired
                                    && c->lastDispatchedId == QStringLiteral("actionIncrease_Time_Speed"),
                       QStringLiteral("IT-05 天空页注入 L 键 → timeRate %1 → %2（应变），"
                                      "dispatched 累计=%3（应>基线 %4），"
                                      "lastActionId=\"%5\"（应=actionIncrease_Time_Speed）"
                                      "—— QKeyEvent→keySink→routeKey 的 QML 链是活的")
                           .arg(c->rateBefore).arg(rate)
                           .arg(c->dispatchedCount).arg(c->dispatchBase)
                           .arg(c->lastDispatchedId));
        // 写入步：切到搜索页，准备焦点守卫判据
        uiReturnClick(c->window, c->navSearch);
        break;
    }
    case 7: {
        // ── T31：门降级：**标志可能来自两个地方** —— ① 前导激活门超时（相位 0 置的，
        //    那时还没轮到 IT-06 记账）；② 本相位上一轮的激活门失败（重入）。
        //    两者都在这里**统一记一次** IT-06，然后跳过本相位的写入步与 case 8 的判据。
        //    ⚠️ 两条"置标志"的路径都**不记账**（uiInteractSetFocusGateFailed）—— 记账
        //    只发生在这里，保证每个 ID 恰好一次（重复记账会让收尾自检假红）。─────────
        if (c->focusGateFailed)
        {
            uiInteractUnavailable(c.get(), "IT-06", c->focusGateWhy);
            break;
        }
        if (qEnvironmentVariableIsSet("STELQUICK_INTERACT_FORCE_FOCUSGATE_FAIL"))
        {
            uiInteractEnterFocusUnavailable(
                c.get(), "IT-06",
                QStringLiteral("负控强制（STELQUICK_INTERACT_FORCE_FOCUSGATE_FAIL=1）：把"
                               "控制流按\"窗口拿不到系统焦点\"注入，用来验证**降级逻辑"
                               "自身**（不必真把显示器睡掉）"));
            break;
        }
        // ── IT-06 写入步：真实点击搜索框聚焦 → 再注入同键（守卫必须拦）────
        const int cur = uiCurrentPageIndex(c->pageStack);
        if (cur != c->idxSearch)
        {
            uiInteractMark(c.get(), false,
                           QStringLiteral("IT-06 前提失败：切搜索页后 currentIndex=%1 "
                                          "（期望 %2）—— 守卫判据无效")
                               .arg(cur).arg(c->idxSearch));
            uiInteractFinish(app, c.get());
            return;
        }
        // 🔴 窗口激活门（T27 加固，实测本判据偶发假红）：守卫用
        // QGuiApplication::focusObject() 判焦点，而**窗口未获得系统焦点时
        // focusObject() 恒为 nullptr**，点击搜索框也给不了焦点 ⇒ 前提不成立
        // （判据注入的按键是 sendEvent 直达窗口，绕过系统焦点，所以引擎照样
        // 收到 L 键 ⇒ 假红）。有界重试激活（3 次）；仍失活则**明确判红并标注
        // 仪器不可用**，绝不洗成 PASS。
        if (!c->window->isActive())
        {
            ++c->activeRetry;
            if (c->activeRetry <= 3)
            {
                c->window->requestActivate();
                c->details.append(QStringLiteral("IT-note 窗口未激活（尝试 %1/3），"
                                                 "requestActivate 后重试本相位")
                                      .arg(c->activeRetry));
                c->nextDelayMs = 400;
                QTimer::singleShot(400, app, [app, c]() { uiInteractStep(app, c); });
                return;   // 不 ++phase：重入本相位
            }
            // T31：**不再 exit** —— 记 UNAVAILABLE 后继续跑到收尾。此时激活无关的
            // IT-01..IT-04 与 **IT-07..IT-12** 早已/仍将跑完，只有 kFocusGatedIds 里那几条
            // 记为 UNAVAILABLE。"不洗成 PASS"由 `UNAVAILABLE ≠ PASS 且
            // VERDICT=UNAVAILABLE / rc=6` 体现。
            if (uiInteractProbeActivation())
            {
                c->details.append(QStringLiteral("PROBE-note 失活探针：窗口 %1 次 "
                                                 "requestActivate 后仍失活，探针模式下"
                                                 "**照跑**（测量各判据的激活依赖）")
                                      .arg(c->activeRetry));
                break;
            }
            // ⚠️ 这里**必须自己记账**：下面 `break` 会让相位号 +1（走到 case 8），
            // 不会再回到本相位开头那一段。相位开头那段只负责"标志由**前导门**置上"
            // 的情形（那时本相位的激活门根本没跑）。
            uiInteractEnterFocusUnavailable(
                c.get(), "IT-06",
                QStringLiteral("窗口 %1 次 requestActivate 后仍未获得系统焦点"
                               "（isActive=false）⇒ 指针事件 / 系统焦点类前提无法成立")
                    .arg(c->activeRetry));
            break;
        }
        // 真实点击搜索框：TextInput 获得焦点 ⇒ canDispatchToSky() 应判 false。
        // （不 forceActiveFocus 到 keySink —— 这正是要测的守卫前提。）
        // 🔴 有界焦点就绪门（T27 加固，实测本判据偶发假红）：点击注入偶尔因环境
        // 干扰（Spotlight 批量索引/显示器状态）未能把焦点交给搜索框，于是守卫
        // 前提不成立 ⇒ IT-06 红而代码无辜。处置沿 T22「有界就绪门」先例：
        // 最多重试 2 次，**门超时则明确判红且不洗成 PASS**（判据本身不放宽）。
        auto focusIsTextField = []() {
            QObject *f = QGuiApplication::focusObject();
            return f && f->inherits("QQuickTextInput");
        };
        int focusTries = 0;
        uiReturnClick(c->window, c->queryField);
        while (!focusIsTextField() && focusTries < 2)
        {
            ++focusTries;
            uiReturnClick(c->window, c->queryField);
        }
        if (!focusIsTextField())
        {
            QObject *f = QGuiApplication::focusObject();
            if (uiInteractProbeActivation())
            {
                c->details.append(QStringLiteral("PROBE-note 失活探针：点击搜索框 %1 次"
                                                 "后焦点仍不是 TextInput，探针模式下**照跑**")
                                      .arg(focusTries + 1));
                break;
            }
            uiInteractEnterFocusUnavailable(
                c.get(), "IT-06",
                QStringLiteral("点击搜索框 %1 次后焦点仍不是 QQuickTextInput（当前=%2）"
                               "⇒ 守卫前提无法成立")
                    .arg(focusTries + 1)
                    .arg(f ? f->metaObject()->className() : "(null)"));
            break;
        }
        c->rateBefore2 = c->facade->timeRate();
        c->dispatchBase2 = c->dispatchedCount;
        const int acc = uiInteractSendKeyNoFocusGrab(c->window, Qt::Key_L);
        c->details.append(QStringLiteral("IT-note 已真实点击搜索框并再次投递 L 键"
                                         "（不抢焦点变体；焦点门重试 %1 次），timeRate "
                                         "before=%2，dispatched 基线=%3，事件受理=%4")
                              .arg(focusTries).arg(c->rateBefore2).arg(c->dispatchBase2)
                              .arg(acc));
        // T29-W 诊断：守卫判据的"前提是否成立"要看快照，不看推断
        c->details.append(QStringLiteral("IT-note L 键（不抢焦点变体）派发后焦点快照：%1")
                              .arg(uiFocusSnapshot(c->window, c->keySink, c->router)));
        break;
    }
    case 8: {
        // T31：门失败 ⇒ IT-06 已在 case 7 记过 UNAVAILABLE，本相位**跳过判据**。
        // ⚠️ 但末尾的"切回天空页"写入步**必须照做** —— 它是 IT-07 的舞台。
        // 首版把整个相位 `break` 掉，结果 IT-07 在搜索页上拖拽 ⇒ **级联假红**，
        // 一度让我得出"IT-07 也依赖窗口激活"的错误结论（见 kFocusGatedIds 注释）。
        if (!c->focusGateFailed)
        {
            // ── IT-06：焦点守卫活链——输入框持焦时天空快捷键必须被拦 ──────
            const double rate = c->facade->timeRate();
            const bool fired = c->dispatchedCount > c->dispatchBase2;
            uiInteractMark(c.get(), rate == c->rateBefore2 && !fired,
                           QStringLiteral("IT-06 焦点守卫：搜索框聚焦时注入 L → timeRate "
                                          "%1 → %2（应不变），dispatched=%3（应=基线 %4）"
                                          "—— U-ACT-03 的 QML 端到端腿")
                               .arg(c->rateBefore2).arg(rate)
                               .arg(c->dispatchedCount).arg(c->dispatchBase2));
        }
        // 写入步（T27）：切回天空页（后续 IT-07 需要天空页可见才有点击目标）
        uiReturnClick(c->window, c->navSky);
        break;
    }
    case 9: {
        // ── IT-07 写入步：冻结时间 → 双向拖拽（真实 press→5×move→release）──
        // 🔴 关键前提（首跑实测）：合流形态 HostDriven 帧泵在 400ms 内推进仿真
        // 时间 **0.61 天**（≈1.5 天/秒，与 rate 读数脱钩；DIAG 读数 engineRate=10
        // 而 simJD 差 0.61 天）。视线锁定地平坐标时，J2000 视线随 JD 转 ⇒ 400ms
        // 自然漂移 **80°**，把拖拽效果淹掉 ⇒ 旧口径"视线转过 >0.2°"**无判别力**
        // （负控下仍绿，实测 46°）。处置：①段内冻结仿真时间（simScale=0，段尾
        // 还原）；②同相位内做**反向**拖拽对照——共模漂移在两次增量中同向叠加，
        // 反向断言抵抗残余漂移。这是血泪第 4 条（孤立断言可假绿）的又一实例。
        //
        // ✅ **T35（2026-09-30）把上面括注里的"与 rate 读数脱钩"定性推翻了**：
        // **观察对、解释错**。同刻量齐三个量后 `ΔJD == 真实窗口 × rate × scale`
        // 成立（TL-01 相对偏差 0.01%，恒星时绝对腿残差 0.0000°；见
        // `docs/T35_TIMELINK.zh_CN.md`）。三个数各有出处：①`getTimeRate()` 单位是
        // **JDay/sec**（`src/core/StelCore.hpp:595`）；②`engineRate=10` 是**本套件
        // 自己的 IT-05 注入 L 键**（`increaseTimeSpeed()` ×10 阶梯）抬上去的**移动靶**；
        // ③"400ms" 是相位名义 delay，从未与 ΔJD 同刻测过。
        // ⚠️ 但**本步的两条处置不受影响**——视线随时间漂是**真**的（T35 探针 Q6d
        // 实测 `getViewDirectionJ2000` 的 ΔRA 与 ΔLST 同阶），只是速率读数需换算。
        auto viewNow = []() {
            if (!StelApp::isInitialized() || !StelApp::getInstance().getCore()->getMovementMgr())
                return Vec3d(0., 0., 0.);
            return StelApp::getInstance().getCore()->getMovementMgr()->getViewDirectionJ2000();
        };
        const double simJdBefore = StelApp::isInitialized()
                                       ? StelApp::getInstance().getCore()->getSimClockJD() : 0.0;
        c->facade->setSimulationPaused(true);
        c->dirBefore = viewNow();
        if (c->dirBefore.norm() < 0.5)
        {
            c->facade->setSimulationPaused(false);
            uiInteractMark(c.get(), false,
                           QStringLiteral("IT-07 前提失败：视线基线无效（norm=%1）")
                               .arg(c->dirBefore.norm()));
            uiInteractFinish(app, c.get());
            return;
        }
        const QPointF vp = c->viewport->mapToScene(QPointF(0, 0));
        const double w = c->viewport->width(), h = c->viewport->height();
        const QPointF center(vp.x() + w / 2, vp.y() + h / 2);
        // 中央区域横向拖 160px：避开屏幕边缘（flagEnableMoveAtScreenEdge 默认关，
        // 但保持不贴边更稳）。
        const int accLeft = uiInteractSendDrag(c->window,
                                               center + QPointF(80, 0),
                                               center - QPointF(80, 0));
        c->dirAfterLeft = viewNow();
        const int accRight = uiInteractSendDrag(c->window,
                                                center - QPointF(80, 0),
                                                center + QPointF(80, 0));
        c->dirAfterRight = viewNow();
        c->facade->setSimulationPaused(false);   // 段内还原（血泪第 9 条）
        {
            const double simJdAfter = StelApp::isInitialized()
                                          ? StelApp::getInstance().getCore()->getSimClockJD() : 0.0;
            std::printf("INTERACTCHECK: IT-note DIAG9 冻结窗口内 simJD 推进=%.9f 天"
                        "（应≈0；冻结前 engineRate=%g）拖拽受理=%d/%d\n",
                        simJdAfter - simJdBefore,
                        StelApp::isInitialized()
                            ? StelApp::getInstance().getCore()->getTimeRate() : 0.0,
                        accLeft, accRight);
            std::fflush(stdout);
        }
        break;
    }
    case 10: {
        // ── IT-07：拖拽平移——正向转过阈值 ∧ 反向对照（两次增量方向相反）──
        const Vec3d d0 = c->dirBefore, d1 = c->dirAfterLeft, d2 = c->dirAfterRight;
        const double aLeft = (d0.norm() > 0.5 && d1.norm() > 0.5)
                                 ? d0.angle(d1) * 180.0 / M_PI : -1.0;
        const Vec3d v1 = d1 - d0, v2 = d2 - d1;
        Vec3d e1 = v1, e2 = v2;                 // Vec3d 只有 in-place normalize()
        if (e1.norm() > 1e-9) e1.normalize();
        if (e2.norm() > 1e-9) e2.normalize();
        const double dot12 = (v1.norm() > 1e-9 && v2.norm() > 1e-9) ? e1.dot(e2) : 9.99;
        const bool reverse = dot12 < 0.0;
        uiInteractMark(c.get(), aLeft > 0.2 && reverse,
                       QStringLiteral("IT-07 冻结时间后向左拖 160px → 视线转过 %1°"
                                      "（应>0.2°），再向右拖 → 两次增量点积 %2（应<0，"
                                      "反向）—— QML MouseArea→skyMouse*→handleClick/"
                                      "handleMove→dragView 的平移链是活的")
                           .arg(aLeft, 0, 'f', 4).arg(dot12, 0, 'f', 4));
        // 写入步（T27）：选中月球并定位跟踪（1.5s 动画）⇒ 下一相位月球居中，
        // IT-08 的"点击中心"才可判别（拖拽后的中心星野不可控，不能直接点）。
        const bool picked = c->facade->selectByStableId(QStringLiteral("Planet:Moon"))
                            && c->facade->locateSelected(true);
        if (!picked)
        {
            uiInteractMark(c.get(), false,
                           QStringLiteral("IT-08 前提失败：selectByStableId/"
                                          "locateSelected(Planet:Moon) 未成功"));
            uiInteractFinish(app, c.get());
            return;
        }
        c->nextDelayMs = 4000;   // 覆盖默认 tick：autoMoveDuration=1.5s 动画 + 跟踪收敛
                                 // （2.4s 实测残差 0.7°——从 35° 外起步不够收敛）
        break;
    }
    case 11: {
        // ── IT-08 写入步：月球应已居中（前提腿）→ 清选中 → 真实点击中心 ──
        const auto &objMgr = StelApp::getInstance().getStelObjectMgr();
        StelCore *core = StelApp::getInstance().getCore();
        Vec3d moonDir(0., 0., 0.), viewDir(0., 0., 0.);
        double centeredDeg = -1.0;
        if (objMgr.getWasSelected() && core->getMovementMgr())
        {
            // getJ2000EquatorialPos 返回**有量纲**位置向量（月球 norm≈0.0026 AU），
            // 必须归一化后再做方向角距（首跑踩中：norm>0.5 的卫兵恒假 ⇒ -1°）。
            moonDir = objMgr.getSelectedObject()[0]->getJ2000EquatorialPos(core);
            moonDir.normalize();
            viewDir = core->getMovementMgr()->getViewDirectionJ2000();
            viewDir.normalize();
            centeredDeg = moonDir.angle(viewDir) * 180.0 / M_PI;
        }
        // 前提腿：定位动画后月球居中（<0.5°，跟踪锁定后应≈0；旧值 3° 太宽——
        // 6 个月球直径，点击中心可能落在 findAndSelect 搜索半径外的月面旁）。
        // 居中后清选中（T23：跟踪随反选断开，月球 400ms 内仅漂移 ~0.03°，仍居中）
        // → 点击中心 → findAndSelect 必中月球。
        //
        // 🔴 有界前提就绪门（T28 加固，实测 IT-08 前提腿偶发未收敛：×5 的第 5 跑
        // 残差 0.6321° > 0.5°）：定位动画（autoMoveDuration≈1.5s）在机器负载高
        // （Spotlight 批量索引）时 4s 内没收敛完，于是**前提不成立**、整个套件在
        // IT-08 提前收尾（只跑出 8 条判据）——**与 T28 的捏合无关**（捏合在
        // case 13-16，在 IT-08 之后），但会让新判据拿不到读数。
        // 处置沿 T22/T27「有界就绪门」先例：**重发一次定位写入步 + 重入本相位**
        // （不 ++phase、不调 uiInteractMark ⇒ 不虚增判据数），最多 2 次；仍不居中
        // 则**明确判红**，判据阈值（0.5°）一字不放宽。
        if (centeredDeg < 0.0 || centeredDeg > 0.5)
        {
            ++c->centerRetry;
            if (c->centerRetry <= 2 && objMgr.getWasSelected())
            {
                c->facade->locateSelected(true);
                c->details.append(QStringLiteral("IT-note IT-08 前提门：定位后月球未居中"
                                                 "（角距=%1°>0.5°），重发定位并重入本相位"
                                                 "（尝试 %2/2）")
                                      .arg(centeredDeg, 0, 'f', 4).arg(c->centerRetry));
                QTimer::singleShot(4000, app, [app, c]() { uiInteractStep(app, c); });
                return;
            }
            uiInteractMark(c.get(), false,
                           QStringLiteral("IT-08 前提失败：定位后月球未居中"
                                          "（视线-月球角距=%1°，应<0.5；定位重试 %2 次）")
                               .arg(centeredDeg, 0, 'f', 4).arg(c->centerRetry - 1));
            uiInteractFinish(app, c.get());
            return;
        }
        // T27-DIAG：打印引擎投影视口尺寸 + 月球投影像素 + 本次点击坐标——
        // 排查"点击选不中"的坐标系错位（逻辑 px vs 引擎 px）。
        {
            const StelProjectorP prj = core->getProjection(StelCore::FrameJ2000);
            Vec3d win(0., 0., 0.);
            prj->project(objMgr.getSelectedObject()[0]->getJ2000EquatorialPos(core), win);
            std::printf("INTERACTCHECK: IT-note DIAG 引擎投影视口 %dx%d dppp=%g "
                        "月球投影=(%.1f,%.1f) 点击=(%.1f,%.1f) 居中角距=%.4f°\n",
                        prj->getViewportWidth(), prj->getViewportHeight(),
                        StelApp::getInstance().getDevicePixelsPerPixel(),
                        win.v[0], win.v[1],
                        c->viewport->mapToScene(QPointF(c->viewport->width() / 2.0,
                                                        c->viewport->height() / 2.0)).x(),
                        c->viewport->mapToScene(QPointF(c->viewport->width() / 2.0,
                                                        c->viewport->height() / 2.0)).y(),
                        centeredDeg);
            std::fflush(stdout);
        }
        c->facade->clearSelection();
        c->selBefore8 = objMgr.getWasSelected();
        uiLocateClick(c->window, c->viewport->mapToScene(
                                     QPointF(c->viewport->width() / 2.0,
                                             c->viewport->height() / 2.0)));
        const bool sel = objMgr.getWasSelected();
        QString name;
        if (sel && !objMgr.getSelectedObject().empty())
            name = objMgr.getSelectedObject()[0]->getEnglishName();
        c->selAfter8 = sel;
        uiInteractMark(c.get(), !c->selBefore8 && sel && name == QStringLiteral("Moon"),
                       QStringLiteral("IT-08 月球居中后：清选中（前=%1，应 false）→ 真实"
                                      "点击视口中心 → 选中=\"%2\"（应 Moon）—— 引擎 "
                                      "findAndSelect 经 QML MouseArea→skyMouse*→"
                                      "handleClick 链是活的")
                           .arg(c->selBefore8 ? "true" : "false").arg(name));
        break;
    }
    case 12: {
        // ── IT-09：右键释放反选——同一次真实投递必须把选中清掉（对照腿）──
        uiInteractSendRightRelease(c->window, c->viewport->mapToScene(
                                                  QPointF(c->viewport->width() / 2.0,
                                                          c->viewport->height() / 2.0)));
        const bool sel = StelApp::getInstance().getStelObjectMgr().getWasSelected();
        uiInteractMark(c.get(), c->selAfter8 && !sel,
                       QStringLiteral("IT-09 右键释放反选 → 选中 %1（点击后）→ %2"
                                      "（右键后，应 false）—— 引擎 deselection 经 "
                                      "MouseArea 链是活的（与 IT-08 成对）")
                           .arg(c->selAfter8 ? "true" : "false")
                           .arg(sel ? "true" : "false"));
        // 写入步（T28）：本相位起进入捏合判据。此处**不需要**切页——IT-08/09 之后
        // 一直停在天空页（case 8 切回后再没离开）。下面刻意不投任何鼠标事件，
        // 避免选中状态/拖动标志干扰后续断言。
        break;
    }
    case 13: {
        // ── IT-10 写入步：天空页投递真实原生捏合（ZoomNativeGesture，捏开 ×3）──
        //
        // 「捏开」= 两指张开 = 距离变大 = scale > 1 ⇒ 引擎 `zoomTo(previousFov/scale)`
        // ⇒ 视场**变小**（画面放大）。三次 1.25 ⇒ 理论 FOV ×(1/1.25)³ ≈ ×0.512，
        // 远大于读数噪声，又与旧宿主 0.5..2 的健全闸相容。
        if (uiCurrentPageIndex(c->pageStack) != c->idxSky)
        {
            uiInteractMark(c.get(), false,
                           QStringLiteral("IT-10 前提失败：捏合判据需要天空页，当前 "
                                          "currentIndex=%1（期望 %2）")
                               .arg(uiCurrentPageIndex(c->pageStack)).arg(c->idxSky));
            uiInteractFinish(app, c.get());
            return;
        }
        c->fovBefore10 = c->facade->fieldOfView();
        const QPointF p = uiLocateCenter(c->viewport);
        int acc = 0;
        for (int i = 0; i < 3; ++i)
            acc += uiInteractSendNativePinch(c->window, p, 1.25);
        c->details.append(QStringLiteral("IT-note 天空页视口中心 @(%1,%2) 投递 3× "
                                         "ZoomNativeGesture(1.25)，事件受理=%3/3，"
                                         "FOV before=%4")
                              .arg(p.x(), 0, 'f', 1).arg(p.y(), 0, 'f', 1).arg(acc)
                              .arg(c->fovBefore10, 0, 'f', 4));
        break;
    }
    case 14: {
        // ── IT-10：捏开 → FOV 真的变小（成对：before>0 且严格变小）──────────
        c->fovAfter10 = c->facade->fieldOfView();
        uiInteractMark(c.get(), c->fovBefore10 > 0.0 && c->fovAfter10 > 0.0
                                    && c->fovAfter10 < c->fovBefore10,
                       QStringLiteral("IT-10 天空页原生捏开 ×1.25³ → FOV %1 → %2"
                                      "（应严格变小；不动即捏合链路死，T10 起 T28 前的"
                                      "老病）")
                           .arg(c->fovBefore10, 0, 'f', 4).arg(c->fovAfter10, 0, 'f', 4));
        // 写入步：反向捏合（捏拢 0.8 ×3，理论把 FOV 乘回 ×(1/0.8)³ = ×1.953125）
        const QPointF p = uiLocateCenter(c->viewport);
        for (int i = 0; i < 3; ++i)
            uiInteractSendNativePinch(c->window, p, 0.8);
        break;
    }
    case 15: {
        // ── IT-11：方向对照——反向捏合必须回升**且回到原值**──────────────────
        // 断言两根腿：
        //   ① 方向：大于 IT-10 后的值（只会单向缩放的假链会红）；
        //   ② 幅值：回到捏合前的值 2% 以内（1.25³ 与 0.8³ 互逆 ⇒ 理论精确还原）。
        // 只写①会让"每次捏合都乘固定倍率"的实现蒙混过关。
        const double back = c->facade->fieldOfView();
        const double rel = (c->fovBefore10 > 0.0)
                               ? std::fabs(back - c->fovBefore10) / c->fovBefore10 : 9.99;
        uiInteractMark(c.get(), back > c->fovAfter10 && rel < 0.02,
                       QStringLiteral("IT-11 方向对照：反向捏拢 ×0.8³ → FOV %1 → %2"
                                      "（应回升且回到 %3，相对偏差 %4 应<0.02）")
                           .arg(c->fovAfter10, 0, 'f', 4).arg(back, 0, 'f', 4)
                           .arg(c->fovBefore10, 0, 'f', 4).arg(rel, 0, 'f', 5));
        // 写入步：切到时间页（IT-12 页守卫负控的舞台）
        uiReturnClick(c->window, c->navTime);
        break;
    }
    case 16: {
        // ── IT-12 负控：时间页捏合必须不动 FOV（页守卫）────────────────────
        const int cur = uiCurrentPageIndex(c->pageStack);
        if (cur != c->idxTime)
        {
            uiInteractMark(c.get(), false,
                           QStringLiteral("IT-12 前提失败：切时间页后 currentIndex=%1 "
                                          "（期望 %2）—— 负控无效")
                               .arg(cur).arg(c->idxTime));
            uiInteractFinish(app, c.get());
            return;
        }
        c->fovBefore12 = c->facade->fieldOfView();
        int acc = 0;
        for (int i = 0; i < 3; ++i)
            acc += uiInteractSendNativePinch(c->window, uiLocateCenter(c->pageStack), 1.25);
        const double after = c->facade->fieldOfView();
        uiInteractMark(c.get(), after == c->fovBefore12,
                       QStringLiteral("IT-12 负控：时间页捏合 ×1.25³（受理=%1/3）→ FOV "
                                      "%2 → %3（应不变：PinchHandler 只挂天空页，引擎"
                                      "没收到就是没收到）")
                           .arg(acc).arg(c->fovBefore12, 0, 'f', 4)
                           .arg(after, 0, 'f', 4));
        // 写入步（T29）：切到搜索页，为输入法组合键判据布场
        uiReturnClick(c->window, c->navSearch);
        break;
    }
    case 17: {
        // ── T29：IT-13 布场② —— 窗口激活门 + 页前提 ──────────────────────
        // 窗口激活门理由同 IT-06（qquickwindow 失活时 focusObject() 恒 nullptr，
        // 焦点前提不可能成立）；这里再叠一层"必须真的在搜索页"，否则点搜索框
        // 会点在别的页上（血泪第 10 条：切页后必须等布局）。
        // ── T31：门已失败（IT-06 或本相位自身）⇒ 输入法链路四相位的前提同样不成立。
        //    本相位**自己记 IT-13 的 UNAVAILABLE**（不能因为标志已置就静默跳过，
        //    否则收尾的"后缀"自检会看到集合缺项）。──────────────────────────
        if (c->focusGateFailed)
        {
            uiInteractUnavailable(c.get(), "IT-13", c->focusGateWhy);
            break;
        }
        if (!c->window->isActive())
        {
            ++c->activeRetry;
            if (c->activeRetry <= 3)
            {
                c->window->requestActivate();
                c->details.append(QStringLiteral("IT-note（T29）窗口未激活（尝试 %1/3），"
                                                 "requestActivate 后重试本相位")
                                      .arg(c->activeRetry));
                c->nextDelayMs = 400;
                QTimer::singleShot(400, app, [app, c]() { uiInteractStep(app, c); });
                return;
            }
            // T31：降级为"标记但继续"（同 IT-06）。
            if (uiInteractProbeActivation())
            {
                c->details.append(QStringLiteral("PROBE-note 失活探针：窗口 %1 次 "
                                                 "requestActivate 后仍失活，探针模式下"
                                                 "**照跑**").arg(c->activeRetry));
                break;
            }
            uiInteractEnterFocusUnavailable(
                c.get(), "IT-13",
                QStringLiteral("窗口 %1 次 requestActivate 后仍失活 ⇒ 焦点前提无法成立")
                    .arg(c->activeRetry));
            break;
        }
        const int cur = uiCurrentPageIndex(c->pageStack);
        if (cur != c->idxSearch)
        {
            uiInteractMark(c.get(), false,
                           QStringLiteral("IT-13 前提失败：切搜索页后 currentIndex=%1 "
                                          "（期望 %2）—— 输入法判据无效")
                               .arg(cur).arg(c->idxSearch));
            uiInteractFinish(app, c.get());
            return;
        }
        break;
    }
    case 18: {
        // T31：门失败 ⇒ IT-13 已在 case 17 记过，跳过写入步（不重复记）。
        if (c->focusGateFailed)
            break;
        // ── T29：IT-13 写入步 —— 真实点击搜索框聚焦 + 注入 preedit 组合串 ──
        // 有界焦点门（同 IT-06）：点击注入偶尔因环境干扰没把焦点交给搜索框。
        auto focusIsTextField = []() {
            QObject *f = QGuiApplication::focusObject();
            return f && f->inherits("QQuickTextInput");
        };
        uiReturnClick(c->window, c->queryField);
        while (!focusIsTextField() && c->focusTries29 < 3)
        {
            ++c->focusTries29;
            uiReturnClick(c->window, c->queryField);
        }
        if (!focusIsTextField())
        {
            QObject *f = QGuiApplication::focusObject();
            // T31：降级为"标记但继续"（同 IT-06）。
            if (uiInteractProbeActivation())
            {
                c->details.append(QStringLiteral("PROBE-note 失活探针：点击搜索框 %1 次"
                                                 "后焦点仍不是 TextInput，探针模式下**照跑**")
                                      .arg(c->focusTries29 + 1));
                break;
            }
            uiInteractEnterFocusUnavailable(
                c.get(), "IT-13",
                QStringLiteral("点击搜索框 %1 次后焦点仍不是 QQuickTextInput（当前=%2）")
                    .arg(c->focusTries29 + 1)
                    .arg(f ? f->metaObject()->className() : "(null)"));
            break;
        }
        c->imeTextBefore = c->queryField->property("text").toString();
        const int acc = uiInteractSendInputMethod(c->window, QStringLiteral("yueqiu"),
                                                  QString());
        c->details.append(QStringLiteral("IT-note（T29）已真实点击搜索框聚焦（焦点门重试 "
                                         "%1 次）并注入输入法 preedit=\"yueqiu\"（受理=%2），"
                                         "组合前 text=\"%3\"")
                              .arg(c->focusTries29).arg(acc).arg(c->imeTextBefore));
        break;
    }
    case 19: {
        // T31：门失败 ⇒ IT-13（本相位判据）已在 case 17 记过；IT-14 的写入步同样不跑
        // （IT-14 的 UNAVAILABLE 由 case 20 自己记，保持"谁判据谁记账"）。
        if (c->focusGateFailed)
            break;
        // ── IT-13：IME 组合链在合流形态是活的 + 不污染 text（成对）──────────
        // 「活」= preeditText 真的变成组合串 **且** inputMethodComposing 置位；
        // 「不污染」= text 在组合期间**不许**被写（preedit 是临时区，不是内容）。
        // 只断言前者会放过"把 preedit 直接当 text 插进去"的实现。
        c->imePreedit = c->queryField->property("preeditText").toString();
        c->imeComposing = c->queryField->property("inputMethodComposing").toBool();
        const QString textNow = c->queryField->property("text").toString();
        uiInteractMark(c.get(), c->imePreedit == QStringLiteral("yueqiu") && c->imeComposing
                                    && textNow == c->imeTextBefore,
                       QStringLiteral("IT-13 IME 组合链：注入 preedit → preeditText=\"%1\""
                                      "（应=yueqiu）、inputMethodComposing=%2（应=true）、"
                                      "text=\"%3\"（应仍=\"%4\"，组合不得污染正文）"
                                      "—— 窗口→focusTargetItem→TextInput 的输入法通路"
                                      "是活的（此前从未在合流形态被测过）")
                           .arg(c->imePreedit, c->imeComposing ? "true" : "false",
                                textNow, c->imeTextBefore));
        // 写入步：**组合中**注入 Esc（真机由 IME 消费、不产生 QKeyEvent；这里刻意
        // 比真实更严苛 —— 即使它到达，也必须被焦点守卫拦住）
        c->pageBefore14 = uiCurrentPageIndex(c->pageStack);
        c->rateBefore14 = c->facade->timeRate();
        c->dispatchBase14 = c->dispatchedCount;
        const int accEsc = uiInteractSendKeyNoFocusGrab(c->window, Qt::Key_Escape);
        c->details.append(QStringLiteral("IT-note（T29）组合态注入 Esc（受理=%1），前提："
                                         "页=%2 rate=%3 dispatched=%4 preeditText=\"%5\"")
                              .arg(accEsc).arg(c->pageBefore14).arg(c->rateBefore14)
                              .arg(c->dispatchBase14).arg(c->imePreedit));
        // T29-W 诊断：IT-14 是**否定式**判据（"没跳页"），必须能看到前提快照，
        // 否则"键根本没送到"也会让它绿（T29 已记过这条：否定式判据单独绿没意义）。
        c->details.append(QStringLiteral("IT-note 组合态 Esc 派发后焦点快照：%1")
                              .arg(uiFocusSnapshot(c->window, c->keySink, c->router)));
        break;
    }
    case 20: {
        // T31：门失败 ⇒ 本判据不可判（自己记账，见 case 17 的说明）。
        if (c->focusGateFailed)
        {
            uiInteractUnavailable(c.get(), "IT-14", c->focusGateWhy);
            break;
        }
        // ── IT-14：**组合态按 Esc 不得跳页**（修复前必红的那一条）────────────
        // 三腿成对：页面没切 + 引擎时间速率没动 + 没有动作被派发。只断言"没切页"
        // 会放过"页面没切但把 Esc 透传给了引擎"的实现。
        const int pageNow = uiCurrentPageIndex(c->pageStack);
        const double rateNow = c->facade->timeRate();
        uiInteractMark(c.get(), pageNow == c->idxSearch && rateNow == c->rateBefore14
                                    && c->dispatchedCount == c->dispatchBase14,
                       QStringLiteral("IT-14 组合态注入 Esc：页 %1 → %2（应仍=%3）、"
                                      "rate %4 → %5（应不变）、dispatched %6（应=基线 %7）"
                                      "—— 焦点在输入控件时 Esc 必须走守卫，不得跳页")
                           .arg(c->pageBefore14).arg(pageNow).arg(c->idxSearch)
                           .arg(c->rateBefore14).arg(rateNow)
                           .arg(c->dispatchedCount).arg(c->dispatchBase14));
        // 写入步：提交组合（真实 IME 选词后的 commitString）
        const int accCommit = uiInteractSendInputMethod(c->window, QString(), QStringLiteral("月球"));
        c->details.append(QStringLiteral("IT-note（T29）注入输入法 commit=\"月球\"（受理=%1）")
                              .arg(accCommit));
        break;
    }
    case 21: {
        // T31：门失败 ⇒ 本判据不可判（自己记账）。
        if (c->focusGateFailed)
        {
            uiInteractUnavailable(c.get(), "IT-15", c->focusGateWhy);
            break;
        }
        // ── IT-15：组合提交真的写进 text，且组合区归位（成对）──────────────
        // ⚠️ 判据解耦（首轮负控实测教训）：本条**不**断言"仍停在搜索页"——
        //   那本是 IT-14 的内容，抄进来会让负控①（撤守卫）把 IT-14 与 IT-15
        //   一起带红，红点糊成一片、看不出判别力落点。判据各管各的前提。
        const QString textNow = c->queryField->property("text").toString();
        const bool composing = c->queryField->property("inputMethodComposing").toBool();
        const QString preeditNow = c->queryField->property("preeditText").toString();
        const QString expect = c->imeTextBefore + QStringLiteral("月球");
        uiInteractMark(c.get(), textNow == expect && !composing && preeditNow.isEmpty(),
                       QStringLiteral("IT-15 IME 提交：commit=\"月球\" → text=\"%1\""
                                      "（应=\"%2\"=组合前正文+提交串）、"
                                      "inputMethodComposing=%3（应=false）、preeditText=\"%4\""
                                      "（应空：组合区已归位）")
                           .arg(textNow, expect, composing ? "true" : "false", preeditNow));
        break;
    }
    case 22: {
        // T31：门失败 ⇒ 跳过本写入步（IT-16 的 UNAVAILABLE 由 case 23 自己记）。
        if (c->focusGateFailed)
            break;
        // ── T29：IT-16 写入步 —— **判别性对照**：同一个键、同一个页，只把焦点
        //    从输入控件移开（forceActiveFocus(keySink) 等价于用户点一下窗口空白）
        //    ⇒ Esc 必须**仍然**能返回天空页。────────────────────────────────
        // 为什么必须有这条：IT-14 是"否定式判据"（Esc 不得跳页），把它单独实现成
        // "搜索页总是吞掉 Esc" 也会绿 —— 那样的修复把 T20 的返回链路一起杀了。
        // 这条对照就是 IT-14 的"证明它会红"（血泪第 20 条）。实测：负控②（无条件
        // 吞 Esc）下 IT-14 绿而本条红（currentIndex=2 未切天空页）。
        // 此处 forceActiveFocus 是**判据要的前提**（焦点不在输入控件），不是污染
        // ——与 IT-06 里它是污染的情形正相反（血泪第 14 条）。
        //
        // 前提自建（首轮负控实测教训）：本条的"非天空页"前提**不搭 IT-14 的便车**
        // —— 负控①下 IT-14 把页面切走了，若此处只报"前提失败"就又是一处级联红。
        // 自己布场：已在天空页则点搜索页按钮后重入本相位（最多 2 次）。
        int cur = uiCurrentPageIndex(c->pageStack);
        if (cur == c->idxSky)
        {
            ++c->pageBootstrap29;
            if (c->pageBootstrap29 <= 2)
            {
                uiReturnClick(c->window, c->navSearch);
                c->details.append(QStringLiteral("IT-note（T29）IT-16 布场：当前已在天空页，"
                                                 "点搜索页按钮后重入本相位（%1/2）")
                                      .arg(c->pageBootstrap29));
                c->nextDelayMs = 400;
                QTimer::singleShot(400, app, [app, c]() { uiInteractStep(app, c); });
                return;   // 不 ++phase：重入本相位（不虚增判据数）
            }
            uiInteractMark(c.get(), false,
                           QStringLiteral("IT-16 前提失败：2 次布场后仍未离开天空页"
                                          "（currentIndex=%1）").arg(cur));
            uiInteractFinish(app, c.get());
            return;
        }
        const int acc = uiReturnSendKey(c->window, c->keySink, Qt::Key_Escape);
        c->details.append(QStringLiteral("IT-note（T29）焦点移出输入控件后注入 Esc"
                                         "（受理=%1），Esc 前页=%2（非天空页）")
                              .arg(acc).arg(cur));
        break;
    }
    case 23: {
        // T31：门失败 ⇒ 本判据不可判（自己记账）。
        // ⚠️ T32：这里**不再收尾** —— 后面还追加了 IT-17/IT-18 两个相位，它们的
        //    UNAVAILABLE 由各自相位记录（本相位只记 IT-16，保证"每个 ID 恰好一次"）。
        if (c->focusGateFailed)
        {
            uiInteractUnavailable(c.get(), "IT-16", c->focusGateWhy);
            break;
        }
        // ── IT-16：判别性对照 —— 焦点不在输入控件时 Esc 必须返回天空页 ──────
        const int cur = uiCurrentPageIndex(c->pageStack);
        uiInteractMark(c.get(), cur == c->idxSky,
                       QStringLiteral("IT-16 判别性对照：焦点移出输入控件后注入 Esc → "
                                      "currentIndex=%1（应=%2 天空页）—— 与 IT-14 同一个键、"
                                      "同一个页，唯一差别是焦点位置；Esc 返回链路仍然活着"
                                      "（修复没有把它一起杀掉）")
                           .arg(cur).arg(c->idxSky));
        // T32：不再收尾 —— 继续到 IT-17（T32：两段式 Esc）与 IT-18。
        break;
    }
    case 24: {
        // ── T32：IT-17 布场① —— 切到搜索页 ──────────────────────────────────
        // 为什么单独占一个相位：血泪第 10 条 —— Qt Quick 的布局/polish 由**渲染循环**
        // 驱动，刚切页时新页控件还是"布局前尺寸"（实测 207×0），照它算坐标等于点空。
        // 所以"切页"与"点新页里的控件"必须分相位（case 25 才是那次点击）。
        if (c->focusGateFailed)
            break;   // 布场无意义；IT-17/IT-18 的 UNAVAILABLE 由各自相位记录
        const int cur = uiCurrentPageIndex(c->pageStack);
        if (cur != c->idxSearch)
        {
            ++c->bootstrap32a;
            if (c->bootstrap32a <= 2)
            {
                uiReturnClick(c->window, c->navSearch);
                c->details.append(QStringLiteral("IT-note（T32）IT-17 布场：当前页=%1 ≠ 搜索页"
                                                 "（%2），点搜索页按钮后重入本相位（%3/2）")
                                      .arg(cur).arg(c->idxSearch).arg(c->bootstrap32a));
                c->nextDelayMs = 400;
                QTimer::singleShot(400, app, [app, c]() { uiInteractStep(app, c); });
                return;   // 不 ++phase：重入本相位（不虚增判据数）
            }
            uiInteractMark(c.get(), false,
                           QStringLiteral("IT-17 前提失败：2 次布场后仍未切到搜索页"
                                          "（currentIndex=%1，期望 %2）")
                               .arg(cur).arg(c->idxSearch));
            uiInteractFinish(app, c.get());
            return;
        }
        break;
    }
    case 25: {
        // ── T32：IT-17 写入步 —— 聚焦搜索框 → 置非空文本 → 注入 Esc（第一段）────
        if (c->focusGateFailed)
            break;
        // 有界焦点门（同 IT-06 / IT-13）：点击注入偶尔因环境干扰没把焦点交给搜索框。
        uiReturnClick(c->window, c->queryField);
        while (!uiInteractFocusIsTextItem() && c->focusTries17 < 3)
        {
            ++c->focusTries17;
            uiReturnClick(c->window, c->queryField);
        }
        if (!uiInteractFocusIsTextItem())
        {
            QObject *f = QGuiApplication::focusObject();
            if (uiInteractProbeActivation())
            {
                c->details.append(QStringLiteral("PROBE-note 失活探针：点击搜索框 %1 次后焦点"
                                                 "仍不是 TextInput，探针模式下**照跑**"
                                                 "（缺口未布 ⇒ 本就是这条判据红）")
                                      .arg(c->focusTries17 + 1));
                break;
            }
            uiInteractEnterFocusUnavailable(
                c.get(), "IT-17",
                QStringLiteral("点击搜索框 %1 次后焦点仍不是 QQuickTextInput（当前=%2）"
                               "⇒ 两段式 Esc 的前提（焦点在文本控件里）无法成立")
                    .arg(c->focusTries17 + 1)
                    .arg(f ? f->metaObject()->className() : "(null)"));
            break;
        }
        // 置一个**非空**文本作为"第一段"的前提。直接写属性是**布场**，不是被测对象：
        // 被测的是"Esc 到达 keySink 之后做什么"，不是"用户怎么把字打进去"。
        // （键注入的变体 `uiInteractSendKeyNoFocusGrab` 构造的 QKeyEvent 不带 text()，
        //   所以它插不进字符 —— 见该函数的注释，这也是 IT-06 之后 text 仍为空的原因。）
        c->queryField->setProperty("text", QStringLiteral("mars"));
        c->esc2TextBefore17 = c->queryField->property("text").toString();
        c->esc2PageBefore17 = uiCurrentPageIndex(c->pageStack);
        c->esc2DispatchBefore17 = c->dispatchedCount;
        // ⚠️ 必须用**不抢焦点**的注入变体：抢焦点 = 仪器亲手拆掉"焦点在输入框里"
        //    这条前提（血泪第 14 条；T29 首跑就踩过）。
        const int acc = uiInteractSendKeyNoFocusGrab(c->window, Qt::Key_Escape);
        c->details.append(QStringLiteral("IT-note（T32）IT-17 写入步：点击搜索框聚焦（焦点门重试 "
                                         "%1 次）→ text=\"%2\"（页=%3 dispatched=%4）→ 注入 Esc"
                                         "（受理=%5，**不抢焦点**变体）")
                              .arg(c->focusTries17).arg(c->esc2TextBefore17)
                              .arg(c->esc2PageBefore17).arg(c->esc2DispatchBefore17).arg(acc));
        c->details.append(QStringLiteral("IT-note Esc 派发后焦点快照：%1")
                              .arg(uiFocusSnapshot(c->window, c->keySink, c->router)));
        break;
    }
    case 26: {
        // ── T32：IT-17 断言（成对三腿）＋ IT-18 布场 ──────────────────────────
        // 三腿成对：① 前提腿——注入前文本**非空**（否则"被清空"是空话，血泪第 4 条）；
        //           ② 清空腿——注入后文本变空；③ 无副作用腿——页没动、没有动作被派发。
        // 只断言②会放过"清空了但顺手跳了页"，只断言③会放过"压根没处理 Esc"。
        if (c->focusGateFailed)
        {
            if (!c->esc2It17Done)
            {
                uiInteractUnavailable(c.get(), "IT-17", c->focusGateWhy);
                c->esc2It17Done = true;
            }
            break;
        }
        if (!c->esc2It17Done)
        {
            c->esc2It17Done = true;
            const QString textNow = c->queryField->property("text").toString();
            const int pageNow = uiCurrentPageIndex(c->pageStack);
            uiInteractMark(c.get(),
                           !c->esc2TextBefore17.isEmpty() && textNow.isEmpty()
                               && pageNow == c->esc2PageBefore17 && pageNow == c->idxSearch
                               && c->dispatchedCount == c->esc2DispatchBefore17,
                           QStringLiteral("IT-17 两段式 Esc 第一段：text \"%1\" → \"%2\""
                                          "（应被清空）、页 %3 → %4（应不动，仍=搜索页 %5）、"
                                          "dispatched %6（应=基线 %7）—— 焦点在输入框且有文本时，"
                                          "Esc 只清空、不跳页")
                               .arg(c->esc2TextBefore17).arg(textNow)
                               .arg(c->esc2PageBefore17).arg(pageNow).arg(c->idxSearch)
                               .arg(c->dispatchedCount).arg(c->esc2DispatchBefore17));
        }
        // ── T32：IT-18 布场 —— **自建前提，不搭 IT-17 的便车** ────────────────
        // IT-18 与 IT-17 是同一条链上的两段。若直接沿用 IT-17 的舞台，第一段一旦坏掉，
        // 第二段就在错误的前提上判红 ⇒ "一次手术红两条"，看不出判别力落在哪
        // （T29 首轮负控正是这个形态）。所以这里**显式复位**：① 页回到搜索页
        // （不在就点导航按钮后重入本相位）；② 文本清空（第一段坏了也照样清）。
        // 焦点由 case 27 重新点回搜索框。
        if (uiCurrentPageIndex(c->pageStack) != c->idxSearch)
        {
            ++c->bootstrap32b;
            if (c->bootstrap32b <= 2)
            {
                uiReturnClick(c->window, c->navSearch);
                c->details.append(QStringLiteral("IT-note（T32）IT-18 布场：页不在搜索页，"
                                                 "点导航按钮后重入本相位（%1/2）")
                                      .arg(c->bootstrap32b));
                c->nextDelayMs = 400;
                QTimer::singleShot(400, app, [app, c]() { uiInteractStep(app, c); });
                return;   // 不 ++phase
            }
            uiInteractMark(c.get(), false,
                           QStringLiteral("IT-18 前提失败：2 次布场后仍未回到搜索页"
                                          "（currentIndex=%1，期望 %2）")
                               .arg(uiCurrentPageIndex(c->pageStack)).arg(c->idxSearch));
            uiInteractFinish(app, c.get());
            return;
        }
        if (!c->queryField->property("text").toString().isEmpty())
        {
            const QString stale = c->queryField->property("text").toString();
            c->queryField->setProperty("text", QString());
            c->details.append(QStringLiteral("IT-note（T32）IT-18 布场：清掉残留文本 \"%1\""
                                             "（自建\"空文本\"前提，不搭 IT-17 的便车）")
                                  .arg(stale));
        }
        break;
    }
    case 27: {
        // ── T32：IT-18 写入步 —— 重新聚焦搜索框 → 注入 Esc（第二段）──────────
        if (c->focusGateFailed)
            break;   // IT-18 的 UNAVAILABLE 由 case 28 记录
        uiReturnClick(c->window, c->queryField);
        while (!uiInteractFocusIsTextItem() && c->focusTries18 < 3)
        {
            ++c->focusTries18;
            uiReturnClick(c->window, c->queryField);
        }
        if (!uiInteractFocusIsTextItem())
        {
            QObject *f = QGuiApplication::focusObject();
            if (uiInteractProbeActivation())
            {
                c->details.append(QStringLiteral("PROBE-note 失活探针：点击搜索框 %1 次后焦点"
                                                 "仍不是 TextInput，探针模式下**照跑**")
                                      .arg(c->focusTries18 + 1));
                break;
            }
            uiInteractEnterFocusUnavailable(
                c.get(), "IT-18",
                QStringLiteral("点击搜索框 %1 次后焦点仍不是 QQuickTextInput（当前=%2）"
                               "⇒ 第二段 Esc 的前提无法成立")
                    .arg(c->focusTries18 + 1)
                    .arg(f ? f->metaObject()->className() : "(null)"));
            break;
        }
        c->esc2TextBefore18 = c->queryField->property("text").toString();
        c->esc2PageBefore18 = uiCurrentPageIndex(c->pageStack);
        // 判别腿的快照：注入**那一刻**守卫怎么判。读的是 C++ 侧那只函数本身
        // （口径只有一份），不是在这里复刻一遍"哪些控件算输入框"。
        // 没有这条腿，IT-18 会**依赖回退路径偶然通过** —— 失活时 `canDispatchToSky()`
        // 回真 ⇒ Esc 走 T20 的返回分支同样回天空页，于是"第二段"根本没被验证却显示绿。
        c->esc2GuardBlocks = c->router && !c->router->canDispatchToSky();
        const int acc = uiInteractSendKeyNoFocusGrab(c->window, Qt::Key_Escape);
        c->details.append(QStringLiteral("IT-note（T32）IT-18 写入步：重新点击搜索框聚焦"
                                         "（焦点门重试 %1 次）→ text=\"%2\"（页=%3）→ 注入 Esc"
                                         "（受理=%4，**不抢焦点**变体）；注入时守卫判定"
                                         "canDispatchToSky=%5")
                              .arg(c->focusTries18).arg(c->esc2TextBefore18)
                              .arg(c->esc2PageBefore18).arg(acc)
                              .arg(c->esc2GuardBlocks ? QStringLiteral("false（焦点在输入框）")
                                                      : QStringLiteral("true/未知")));
        c->details.append(QStringLiteral("IT-note Esc 派发后焦点快照：%1")
                              .arg(uiFocusSnapshot(c->window, c->keySink, c->router)));
        break;
    }
    case 28: {
        // ── T32：IT-18 断言（第三态收尾）──────────────────────────────────────
        if (c->focusGateFailed)
        {
            uiInteractUnavailable(c.get(), "IT-18", c->focusGateWhy);
            uiInteractFinish(app, c.get());
            return;
        }
        const int pageNow = uiCurrentPageIndex(c->pageStack);
        uiInteractMark(c.get(),
                       c->esc2TextBefore18.isEmpty() && c->esc2PageBefore18 == c->idxSearch
                           && c->esc2GuardBlocks && pageNow == c->idxSky,
                       QStringLiteral("IT-18 两段式 Esc 第二段：注入前 text=\"%1\"（应空）、"
                                      "页=%2（应=搜索页 %3）、守卫判定不可派发=%4（判别腿）"
                                      "→ 注入后 currentIndex=%5（应=%6 天空页）—— "
                                      "空框再按一次 Esc 才离开搜索，**且走的是守卫路径**"
                                      "（不是 canDispatchToSky 回真的回退路径）")
                           .arg(c->esc2TextBefore18).arg(c->esc2PageBefore18).arg(c->idxSearch)
                           .arg(c->esc2GuardBlocks ? QStringLiteral("true")
                                                   : QStringLiteral("false"))
                           .arg(pageNow).arg(c->idxSky));
        uiInteractFinish(app, c.get());
        return;
    }
    default:
        uiInteractFinish(app, c.get());
        return;
    }
    ++c->phase;
    const int d = c->nextDelayMs;
    c->nextDelayMs = kUiInteractTickMs;
    QTimer::singleShot(d, app, [app, c]() { uiInteractStep(app, c); });
}

int runUiInteractCheck(QGuiApplication *app, QQuickWindow *window,
                       stelapp::AppFacade *facade, stelapp::ActionRouter *router)
{
    auto c = std::make_shared<UiInteractCheck>();
    c->window = window;
    c->facade = facade;
    c->router = router;   // T29-W：只供诊断读 canDispatchToSky()
    // dispatched 信号 = 键盘链"routeKey 真的把动作发给引擎"的直接观测点。
    // 计数 + lastId 双记录：IT-05 断言"发过且发的是对的动作"，IT-06 断言"守卫时没发"。
    QObject::connect(router, &stelapp::ActionRouter::dispatched, app,
                     [c](const QString &actionId, bool executed) {
                         if (executed)
                         {
                             ++c->dispatchedCount;
                             c->lastDispatchedId = actionId;
                         }
                     });
    c->timer = new QTimer(app);
    // T31 **失活探针**：把窗口标成"不接受焦点" ⇒ `isActive()` 恒 false，用来**测量**
    // 每条判据的窗口激活依赖边界（配合 `STELQUICK_INTERACT_PROBE_ACTIVATION=1` 让门
    // 失败后照跑）。窗口仍可见、仍渲染 ⇒ 布局/polish 照常（不像 `hide()` 会停渲染）。
    // ⚠️ 这是**仪器**，只用于取边界数据；正题/负控都不设它。
    if (qEnvironmentVariableIsSet("STELQUICK_INTERACT_FORCE_INACTIVE") && window)
    {
        window->setFlags(window->flags() | Qt::WindowDoesNotAcceptFocus);
        std::printf("INTERACTCHECK: PROBE-note 已置 Qt::WindowDoesNotAcceptFocus"
                    "（失活探针；isActive 现在应为 false）\n");
        std::fflush(stdout);
    }
    // T31 **前台提升（app 自激活）** —— 让套件不必依赖外部 `open -a`。
    //
    // 起因（实测，别再走回头路）：本套件由脚本**直接 `exec`** 启动二进制，这种进程
    // **不被 LaunchServices 认作 `.app` 的实例** ⇒ `open -a <bundle>` 不会把已有的
    // 这一个带到前台，而是**另起一个幽灵实例**（实测 `pgrep` 出现两个 pid），要等的
    // 那一个依旧 `isActive()==false` ⇒ 16 条判据全落进"焦点前提不成立"那一支。
    // 时序上还撞墙：外部 sleep 再 `open`，最早也要 1.5s 才生效，而前导激活门窗口只有
    // `kUiActivationGateTries(3) × 400ms = 1.2s` ⇒ 门早已超时。
    //
    // ⇒ **唯一可靠办法 = app 自己抢前台**：分三次 `requestActivate()`，盖住"窗口刚
    // 上屏、系统还没把 app 提到前面"的窗口期，也盖住"启动瞬间被别的进程抢走前台"。
    //
    // ⚠️ 只在 `STELQUICK_INTERACT_REQUEST_ACTIVATE` 置位时启用 —— 正题是"真实前台
    // 环境"，负控/探针**刻意**不要它（负控造失活靠 `FORCE_FOCUSGATE_FAIL`，探针造
    // 失活靠 `FORCE_INACTIVE` + 别的 app 抢前台）。不置位时**零副作用**：不设定时器、
    // 不打任何新行 ⇒ 判据行与 T29/T30 **逐字不变**。
    if (qEnvironmentVariableIsSet("STELQUICK_INTERACT_REQUEST_ACTIVATE") && window)
    {
        std::printf("INTERACTCHECK: act-note 已排入 3 次 requestActivate"
                    "（0.15s/0.40s/0.90s，自激活前台提升）\n");
        std::fflush(stdout);
        for (int d : {150, 400, 900})
            QTimer::singleShot(d, app, [window]() { window->requestActivate(); });
    }
    std::printf("INTERACTCHECK: 开始（最外层注入：窗口真实滚轮/鼠标/键盘/捏合/输入法事件，"
                "共 16 条判据，每相位 %dms）\n", kUiInteractTickMs);
    std::fflush(stdout);
    uiInteractStep(app, c);
    return 0;
}

int runUiReturnCheck(QGuiApplication *app, QQuickWindow *window, stelapp::AppFacade *facade,
                     stelapp::ActionRouter *router)
{
    auto c = std::make_shared<UiReturnCheck>();
    c->window = window;
    c->facade = facade;
    c->router = router;   // T29-W：只供诊断读 canDispatchToSky()
    std::printf("RETURNUICHECK: 开始（最外层注入：objectName 定位控件 + 窗口真实鼠标/键盘"
                "事件，共 11 条判据；纯 UI 相位 %dms，写引擎的相位 %dms）\n",
                kUiReturnTickMs, kUiReturnWorldSettleMs);
    std::fflush(stdout);
    uiReturnStep(app, c);
    return 0;
}

// ══════════════════════════════════════════════════════════════════════════
// I-REP-02 全流程回放自检：STELQUICK_REPLAY_CHECK=1
//
// 软件测试文档 §5.3 的出口用例：「固定流程 开机→搜月球→定位→改时间→返回，
// 全流程自动回放通过（A-alpha 出口测试）」。本检查就是它的自动化实现 ——
// **全程只投递真实鼠标事件**，不绕过任何一层：
//
//   开机(停在天空页) → 点「搜索天体」→ 在搜索框输入 Moon → 点「搜索」
//   → 点第一条结果（选中月球）→ 点「定位并跟踪」→ 点「时间（T19）」
//   → 点「+1 时」（改时间）→ 点「返回天空」→ 断言末态
//
// 末态断言刻意覆盖四条互相独立的量（页面 / 时间 / 跟踪 / 星空），因为"流程跑完
// 了"这件事**不能**由"最后一屏看着对"来证明：
//   · 页面在天空页（返回环）
//   · JD 确实被改了且保留（改时间环 + 返回不是撤销）
//   · 仍在跟踪月球（定位环 + 返回不是清选中）
//   · 星空位置真的变了（I-DYN-02 的内核：改日期后星空位置变化**可验证**）
//
// 判据 11 条：RP-01..RP-09 含 RP-04a（首行即月球，T21 相关度排序的端到端见证）
// 与 RP-04b（结果无重复天体，T20 补的回归护栏）。
// 退出码同前：0=PASS / 10=FAIL / 6=UNAVAILABLE。
// ══════════════════════════════════════════════════════════════════════════

struct UiReplayCheck
{
    QQuickWindow *window = nullptr;
    stelapp::AppFacade *facade = nullptr;
    QQuickItem *pageStack = nullptr;
    QQuickItem *navSearch = nullptr;
    QQuickItem *navTime = nullptr;
    QQuickItem *queryField = nullptr;
    QQuickItem *goButton = nullptr;
    QQuickItem *resultList = nullptr;
    QQuickItem *locateButton = nullptr;
    QQuickItem *addHour = nullptr;
    QQuickItem *returnButton = nullptr;
    QStringList details;
    int passed = 0;
    int total = 0;
    int phase = 0;
    int nextDelayMs = kUiReturnTickMs;

    int idxSky = -1;
    int idxTime = -1;

    double jdAtBoot = 0.0;
    double jdBeforeTime = 0.0;
    QString sidExpected;
    Vec3d altAzBeforeTime = Vec3d(0.);
    int layoutWait = 0;   //!< T22 布局就绪门的重试计数（见 uiLayoutReady 注释）
};

void uiReplayMark(UiReplayCheck *c, bool ok, const QString &line)
{
    ++c->total;
    if (ok)
        ++c->passed;
    c->details.append(line + (ok ? QStringLiteral("：OK") : QStringLiteral("：FAIL")));
}

void uiReplayFinish(QGuiApplication *app, UiReplayCheck *c)
{
    const bool pass = c->total > 0 && c->passed == c->total;
    for (const QString &line : c->details)
        std::printf("REPLAYCHECK: %s\n", line.toUtf8().constData());
    std::printf("REPLAYCHECK: 判据 %d/%d\n", c->passed, c->total);
    std::printf("REPLAYCHECK: VERDICT=%s\n", pass ? "PASS" : "FAIL");
    std::fflush(stdout);
    app->exit(pass ? 0 : 10);
}

void uiReplayUnavailable(QGuiApplication *app, UiReplayCheck *c, const QString &why)
{
    for (const QString &line : c->details)
        std::printf("REPLAYCHECK: %s\n", line.toUtf8().constData());
    std::printf("REPLAYCHECK: %s\n", why.toUtf8().constData());
    std::printf("REPLAYCHECK: VERDICT=UNAVAILABLE\n");
    std::fflush(stdout);
    app->exit(6);
}

void uiReplayAdvance(QGuiApplication *app, std::shared_ptr<UiReplayCheck> c);

void uiReplayStep(QGuiApplication *app, std::shared_ptr<UiReplayCheck> c)
{
    switch (c->phase)
    {
    case 0: {
        c->pageStack    = c->window->findChild<QQuickItem *>(QStringLiteral("pageStack"));
        c->navSearch    = c->window->findChild<QQuickItem *>(QStringLiteral("navSearchButton"));
        c->navTime      = c->window->findChild<QQuickItem *>(QStringLiteral("navTimeButton"));
        c->queryField   = c->window->findChild<QQuickItem *>(QStringLiteral("searchQueryField"));
        c->goButton     = c->window->findChild<QQuickItem *>(QStringLiteral("searchGoButton"));
        c->resultList   = c->window->findChild<QQuickItem *>(QStringLiteral("searchResultList"));
        c->locateButton = c->window->findChild<QQuickItem *>(QStringLiteral("locateButton"));
        c->addHour      = c->window->findChild<QQuickItem *>(QStringLiteral("timeAddHourButton"));
        c->returnButton = c->window->findChild<QQuickItem *>(QStringLiteral("skyReturnButton"));
        c->idxSky       = uiPageIndexOf(c->window, "sky");
        c->idxTime      = uiPageIndexOf(c->window, "time");

        const int have = (c->pageStack ? 1 : 0) + (c->navSearch ? 1 : 0) + (c->navTime ? 1 : 0)
                       + (c->queryField ? 1 : 0) + (c->goButton ? 1 : 0) + (c->resultList ? 1 : 0)
                       + (c->locateButton ? 1 : 0) + (c->addHour ? 1 : 0)
                       + (c->returnButton ? 1 : 0);
        uiReplayMark(c.get(), have == 9,
                     QStringLiteral("RP-01 全流程锚点可寻（9 个控件找到 %1/9：pageStack/"
                                    "navSearch/navTime/searchQueryField/searchGoButton/"
                                    "searchResultList/locateButton/timeAddHourButton/"
                                    "skyReturnButton）").arg(have));
        if (have != 9)
        {
            uiReplayUnavailable(app, c.get(),
                                QStringLiteral("缺 QML 锚点：I-REP-02 回放要求全程走真实控件，"
                                               "缺一个就无法诚实回放"));
            return;
        }
        // ── RP-02：开机态停在天空页 ───────────────────────────────────────
        const int cur = uiCurrentPageIndex(c->pageStack);
        uiReplayMark(c.get(), cur == c->idxSky,
                     QStringLiteral("RP-02 开机态（startPage=sky）→ currentIndex=%1"
                                    "（期望 %2）").arg(cur).arg(c->idxSky));
        c->facade->setSimulationPaused(true);
        c->jdAtBoot = c->facade->julianDay();
        c->details.append(QStringLiteral("RP-note 时钟已暂停（scale=0），开机 JD=%1；"
                                         "全程只投递真实鼠标事件")
                              .arg(c->jdAtBoot, 0, 'f', 6));
        uiReturnClick(c->window, c->navSearch);
        c->nextDelayMs = kUiReturnTickMs;
        break;
    }
    case 1: {
        // 输入查询词：走 TextField 的真实 text 属性（等价用户键入），再真实点击「搜索」
        c->queryField->setProperty("text", QStringLiteral("Moon"));
        c->details.append(QStringLiteral("RP-note 已在搜索框写入 \"Moon\"（框内=%1）")
                              .arg(c->queryField->property("text").toString()));
        uiReturnClick(c->window, c->goButton);
        c->nextDelayMs = 600;   // 搜索经模型走一趟，给一拍
        break;
    }
    case 2: {
        // ── T22：本相位开头的**布局就绪门**（有界；理由见 uiLayoutReady 注释）──
        // ⚠️ 必须放在本相位**所有 `uiReplayMark` 之前**：门的做法是"停在本相位重试"，
        //    若放在判据之后，每次重试都会把前面的判据重跑一遍、计数虚高
        //    （负控实测出现过 `判据 80/81` 这种虚高读数）。
        if (!uiLayoutReady(c->resultList)) {
            if (c->layoutWait < kUiLayoutWaitTries) {
                if (c->layoutWait == 0)
                    c->details.append(QStringLiteral(
                        "RP-note 结果列表尚未完成布局（%1×%2）—— 有界等待就绪（≤%3×100ms）。"
                        "Qt Quick 的布局由渲染/polish 驱动，重压下读几何会早于 polish"
                        "（实测 207×0）；这是**仪器没接上**，按 DYN 先例只改判据协议，"
                        "判据本身不动。")
                        .arg(c->resultList->width(), 0, 'f', 0)
                        .arg(c->resultList->height(), 0, 'f', 0)
                        .arg(kUiLayoutWaitTries));
                ++c->layoutWait;
                c->nextDelayMs = 100;
                uiReplayAdvance(app, c);   // ⚠️ 停在本相位：**不** ++phase
                return;
            }
            c->details.append(QStringLiteral(
                "RP-note 等待 %1ms 后结果列表仍未完成布局（%2×%3）⇒ 判红，**不洗成 PASS**")
                .arg(kUiLayoutWaitTries * 100)
                .arg(c->resultList->width(), 0, 'f', 0)
                .arg(c->resultList->height(), 0, 'f', 0));
            uiReplayMark(c.get(), false,
                         QStringLiteral("RP-04 结果列表未完成布局（控件 %1×%2）—— "
                                        "仪器没接上：既不作退化证据，也**不作 PASS**")
                             .arg(c->resultList->width(), 0, 'f', 0)
                             .arg(c->resultList->height(), 0, 'f', 0));
            uiReplayFinish(app, c.get());
            return;
        }
        // ── RP-03：搜月球 —— 结果非空 ─────────────────────────────────────
        const int rows = c->resultList->property("count").toInt();
        uiReplayMark(c.get(), rows > 0,
                     QStringLiteral("RP-03 「搜月球」：真实输入 + 点「搜索」→ 结果 %1 条"
                                    "（要求 >0）").arg(rows));
        if (rows <= 0)
        {
            uiReplayUnavailable(app, c.get(),
                                QStringLiteral("搜 \"Moon\" 零结果 —— 环境条件（引擎数据/语言），"
                                               "非接线缺陷，无法继续回放"));
            return;
        }
        // 把前几行**照实打进证据**：RP-04 若判"第一条结果不是月球"，必须能从日志直接
        // 看出它到底是什么 —— 否则失败信息无法定性（是引擎排序？是我们的截断？）。
        for (int r = 0; r < qMin(rows, 5); ++r)
            c->details.append(QStringLiteral("RP-note 结果[%1] \"%2\" / english=\"%3\" / %4")
                                  .arg(r)
                                  .arg(c->facade->searchResults()->nameAt(r),
                                       c->facade->searchResults()->englishNameAt(r),
                                       c->facade->searchResults()->stableIdAt(r)));
        // ── RP-04b：结果列表**不许有重复天体** ─────────────────────────────
        // 这条是 T20 回放**首跑即抓到真实缺陷**后补上的回归护栏：引擎的
        // `listMatchingObjects` 会把翻译名表与英文名表各枚举一遍且不去重，同一个天体
        // 只要有两个名字含查询串就会各出一条（实测搜 "Moon" 5 条里 2 对是重复的）。
        // 修法在 SearchResultsModel（按 stableId 去重）。没有这条判据，那个缺陷
        // 只能靠人眼看列表才发现 —— T17 的 26 条 SRC 判据全绿也没抓到它。
        QSet<QString> uniq;
        int dupCount = 0;
        for (int r = 0; r < rows; ++r) {
            const QString sid = c->facade->searchResults()->stableIdAt(r);
            if (uniq.contains(sid))
                ++dupCount;
            uniq.insert(sid);
        }
        uiReplayMark(c.get(), dupCount == 0 && uniq.size() == rows,
                     QStringLiteral("RP-04b 结果列表无重复天体（%1 行 → %2 个不同 stableId，"
                                    "重复 %3 条）").arg(rows).arg(uniq.size()).arg(dupCount));

        // ── RP-04a：首行**必须**就是月球（T21 起）──────────────────────────
        // T20 时这里只能"按名字定位月球那一行"，并把"首行并非月球"照实打进证据——
        // 因为引擎聚合层把模块级的完全匹配优先序抹掉了（`StelObjectMgr::listMatchingObjects`
        // 在 StelObjectMgr.cpp:609 无条件按名称字典序 std::sort），实测首行是
        // "Ghost of the Moon Nebula"。T21 在模型层用 SearchRanker 重排之后，首行**必须**
        // 是月球。⇒ 这条判据从"绕过已知问题"升级成"断言问题已解决"，**判据强度是提高的**：
        // 把 T21 的排序改坏，它立刻红。
        const int moonRow = 0;
        const QString firstSid = c->facade->searchResults()->stableIdAt(0);
        c->details.append(QStringLiteral("RP-note 首行 = \"%1\"（%2）—— T21 相关度排序已生效，"
                                         "「首行 = 月球」现在是**断言**，不再是期望")
                              .arg(c->facade->searchResults()->nameAt(0), firstSid));
        uiReplayMark(c.get(), firstSid == QStringLiteral("Planet:Moon"),
                     QStringLiteral("RP-04a 首行即月球 Planet:Moon（相关度排序生效）—— 实得 %1")
                         .arg(firstSid));

        // 真实点击**首行**：按 ListView 的行高算场景坐标（行高取自 contentHeight，
        // 不去硬编码 delegate 的 46px —— delegate 一改高度，硬编码就会静默点偏）。
        const double rowH = rows > 0
                                ? c->resultList->property("contentHeight").toDouble() / rows
                                : 46.0;
        const QPointF rowCenter = c->resultList->mapToScene(
            QPointF(c->resultList->width() / 2.0, rowH * moonRow + rowH / 2.0));
        c->details.append(QStringLiteral("RP-note 向窗口投递真实点击 @(%1,%2) —— 结果列表第 %3 行"
                                         "（列表 %4×%5，行高 %6）")
                              .arg(rowCenter.x(), 0, 'f', 1).arg(rowCenter.y(), 0, 'f', 1)
                              .arg(moonRow)
                              .arg(c->resultList->width(), 0, 'f', 0)
                              .arg(c->resultList->height(), 0, 'f', 0)
                              .arg(rowH, 0, 'f', 1));
        uiLocateClick(c->window, rowCenter);
        c->nextDelayMs = kUiReturnTickMs;
        break;
    }
    case 3: {
        // ── RP-04：点结果行 → 引擎里真的选中了（且是月球）────────────────
        const QString en = c->facade->objectInfo()->englishName();
        const QString dn = c->facade->objectInfo()->displayName();
        const bool isMoon = c->facade->objectInfo()->hasSelection()
                         && (en.contains(QStringLiteral("Moon"), Qt::CaseInsensitive)
                             || dn.contains(QStringLiteral("Moon"), Qt::CaseInsensitive));
        uiReplayMark(c.get(), isMoon,
                     QStringLiteral("RP-04 真实点击「月球」那一行 → 选中=%1 displayName=\"%2\" "
                                    "englishName=\"%3\" stableId=\"%4\"（期望命中 Moon）")
                         .arg(c->facade->objectInfo()->hasSelection() ? QStringLiteral("true")
                                                                     : QStringLiteral("false"),
                              dn, en, c->facade->objectInfo()->stableId()));
        c->sidExpected = c->facade->objectInfo()->stableId();
        uiReturnClick(c->window, c->locateButton);
        // 定位是 1.5s 平滑移动（autoMoveDuration 默认）：等待挂在**本步**上，
        // 让"定位"真的走完，下一拍才读跟踪状态。
        c->nextDelayMs = 1800;
        break;
    }
    case 4: {
        // ── RP-05：定位 —— 视向锁定到月球 ─────────────────────────────────
        uiReplayMark(c.get(), c->facade->isTracking()
                                  && c->facade->objectInfo()->stableId() == c->sidExpected,
                     QStringLiteral("RP-05 「定位」：真实点击后 tracking=%1 stableId=\"%2\""
                                    "（期望 %3）")
                         .arg(c->facade->isTracking() ? QStringLiteral("true")
                                                      : QStringLiteral("false"),
                              c->facade->objectInfo()->stableId(), c->sidExpected));
        c->altAzBeforeTime = uiReturnAltAz();
        c->jdBeforeTime    = c->facade->julianDay();
        uiReturnClick(c->window, c->navTime);
        c->nextDelayMs = kUiReturnTickMs;
        break;
    }
    case 5: {
        // ── 改时间：真实点击「+1 时」；等待挂在**写入步**（T19 的教训）─────
        uiReturnClick(c->window, c->addHour);
        c->nextDelayMs = kUiReturnWorldSettleMs;
        break;
    }
    case 6: {
        // ── RP-06：时间确实被改了（这一步是"改时间"环的独立读数）──────────
        const double d = c->facade->julianDay() - c->jdBeforeTime;
        uiReplayMark(c.get(), d > 0.03 && d < 0.06,
                     QStringLiteral("RP-06 「改时间」：真实点击「+1 时」→ JD 位移 %1 天"
                                    "（期望 ≈1/24=%2 天，±30%）")
                         .arg(d, 0, 'f', 6).arg(1.0 / 24.0, 0, 'f', 6));
        uiReturnClick(c->window, c->returnButton);
        c->nextDelayMs = kUiReturnTickMs;
        break;
    }
    default: {
        // ── 末态四连断言 ─────────────────────────────────────────────────
        const int cur = uiCurrentPageIndex(c->pageStack);
        // RP-07 页面在天空页
        uiReplayMark(c.get(), cur == c->idxSky,
                     QStringLiteral("RP-07 末态·页面：真实点击「返回天空」→ currentIndex=%1"
                                    "（期望 %2）").arg(cur).arg(c->idxSky));
        // RP-08 时间改动保留 + 仍在跟踪月球
        const double dKeep = c->facade->julianDay() - c->jdBeforeTime;
        const bool keepTrack = c->facade->isTracking()
                            && c->facade->objectInfo()->stableId() == c->sidExpected;
        uiReplayMark(c.get(), dKeep > 0.03 && keepTrack,
                     QStringLiteral("RP-08 末态·状态保留：时间位移 %1 天（未被撤销）、"
                                    "tracking=%2 stableId=\"%3\"（期望 %4）")
                         .arg(dKeep, 0, 'f', 6)
                         .arg(c->facade->isTracking() ? QStringLiteral("true")
                                                      : QStringLiteral("false"),
                              c->facade->objectInfo()->stableId(), c->sidExpected));
        // RP-09 星空位置真的变了（I-DYN-02 的内核）
        const Vec3d now = uiReturnAltAz();
        const bool valid = uiReturnAltAzValid(c->altAzBeforeTime) && uiReturnAltAzValid(now);
        const double moved = valid ? uiReturnAngleDeg(c->altAzBeforeTime, now) : -1.0;
        uiReplayMark(c.get(), valid && moved > 5.0,
                     QStringLiteral("RP-09 末态·星空（I-DYN-02 内核）：改 1 小时后，月球 "
                                    "AltAz 相对改前变化 %1°（门槛 >5.0）").arg(moved, 0, 'f', 4));
        uiReplayFinish(app, c.get());
        return;
    }
    }
    ++c->phase;
    uiReplayAdvance(app, c);
}

void uiReplayAdvance(QGuiApplication *app, std::shared_ptr<UiReplayCheck> c)
{
    const int d = c->nextDelayMs;
    c->nextDelayMs = kUiReturnTickMs;
    QTimer::singleShot(d, app, [app, c]() { uiReplayStep(app, c); });
}

int runUiReplayCheck(QGuiApplication *app, QQuickWindow *window, stelapp::AppFacade *facade)
{
    auto c = std::make_shared<UiReplayCheck>();
    c->window = window;
    c->facade = facade;
    std::printf("REPLAYCHECK: 开始（I-REP-02 全流程回放：开机→搜月球→定位→改时间→返回，"
                "全程只投递真实鼠标事件，共 11 条判据）\n");
    std::fflush(stdout);
    uiReplayStep(app, c);
    return 0;
}
#endif

} // namespace

#if defined(STELQUICK_HAS_ENGINE) && defined(STELQUICK_WIDGETS_HOST)
// ── A3 前置探针：引擎无头引导 × QML 窗口同进程（STELQUICK_ENGINE_COEXIST=1）──
//
// 回答进度实况 §8.6 记录的未知数"QApplication × QQuickWindow 混用"。
// 与 render/legacy/LegacyAppCheck（A3-C01）的区别：那里的进程**只有引擎**；
// 本探针在**已经跑起来的 QQuickWindow 旁边**引导同一份引擎（同一进程、同一份
// stelMain 符号），验证两者共存。
//
// 为什么必须用 QApplication：引擎现成的无头引导路径依赖
//   new StelMainView(conf) + show() + WA_DontShowOnScreen → initializeGL → StelApp::init
// 而 StelMainView : public QGraphicsView 是 Widgets 类，QGuiApplication 下创建
// 任何 QWidget 会 abort。这正是本探针存在的理由。
//
// 退出码：0 = 全部判据通过；8 = 存在失败项。
int runEngineCoexistProbe(QGuiApplication *app, QQuickWindow *window)
{
    int painted = 0;
    QObject::connect(window, &QQuickWindow::frameSwapped, window, [&painted]() { ++painted; });

    const auto api = window->rendererInterface() ? window->rendererInterface()->graphicsApi()
                                                 : QSGRendererInterface::Unknown;
    std::printf("COEXIST: ── A3 前置探针：引擎无头引导 × QML 窗口同进程 ──\n");
    std::printf("COEXIST: 宿主形态 = QApplication（Widgets）、QML 后端 = %s\n", apiName(api));
    std::fflush(stdout);

    // 先让 QML 窗口真出一轮帧，作为"引擎未引导时窗口是活的"基线。
    // 注意：Qt Quick 是**按需渲染**——静态页面渲完就停，只数现成的帧会得到 0。
    // 所以基线本身也必须用"主动请求重绘"的方式取，两侧才可比。
    {
        QElapsedTimer warm;
        warm.start();
        while (warm.elapsed() < 1500) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
            window->update();
        }
    }
    const int framesBeforeBoot = painted;
    std::printf("COEXIST: 引导前 QML 帧数 = %d（sceneGraphInitialized=%d）\n",
                framesBeforeBoot, window->isSceneGraphInitialized() ? 1 : 0);
    std::fflush(stdout);

    // ── 引擎前置：照抄 src/main.cpp 的顺序（缺一不可）──────────────────────
    StelFileMgr::init();
    const QString userDir = StelFileMgr::getUserDir();
    StelLogger::init(userDir + QStringLiteral("/log.txt"));

    QString configFileFullPath = StelFileMgr::findFile(
        QStringLiteral("config.ini"),
        StelFileMgr::Flags(StelFileMgr::Writable | StelFileMgr::File));
    if (configFileFullPath.isEmpty())
        configFileFullPath = StelFileMgr::findFile(QStringLiteral("config.ini"), StelFileMgr::New);
    if (configFileFullPath.isEmpty()) {
        std::printf("COEXIST: C-00 FAIL 既找不到也建不出 config.ini\n");
        std::fflush(stdout);
        return 8;
    }
    auto *confSettings = new QSettings(configFileFullPath, StelIniFormat, nullptr);
    StelTranslator::init(StelFileMgr::getInstallationDir() + QStringLiteral("/data/languages.tab"));

    std::printf("COEXIST: 安装目录 = %s\n", qPrintable(StelFileMgr::getInstallationDir()));
    std::printf("COEXIST: 用户目录 = %s\n", qPrintable(userDir));
    std::printf("COEXIST: 配置文件 = %s\n", qPrintable(configFileFullPath));
    std::fflush(stdout);

    int failCount = 0;

    // ── C-01 引擎无头引导（与 A3-C01 同一路径，但此刻 QML 窗口已存在）──────────
    StelMainView *mainWin = new StelMainView(confSettings);
    mainWin->setAttribute(Qt::WA_DontShowOnScreen, true);
    mainWin->resize(1280, 720);
    mainWin->show();

    QElapsedTimer bootClock;
    bootClock.start();
    while (bootClock.elapsed() < 30000 && !StelApp::isInitialized()) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        if (!StelApp::isInitialized())
            QThread::msleep(10);
    }
    const bool booted = StelApp::isInitialized();
    if (!booted)
        ++failCount;
    std::printf("COEXIST: C-01 %s 引擎无头初始化%s（耗时 %lldms，WA_DontShowOnScreen，"
                "QML 窗口同时存活）\n",
                booted ? "PASS" : "FAIL",
                booted ? "成功" : "失败（30s 内 initializeGL 未触发）",
                static_cast<long long>(bootClock.elapsed()));

    // ── C-02 引擎 GL 形态（照抄 A3-C02：不采信请求值，只记拿到值）──────────────
    if (booted) {
        const StelMainView::GLInfo &gi = StelMainView::getInstance().getGLInformation();
        const bool glOk = (gi.mainContext != nullptr) && gi.majorVersion >= 3;
        if (!glOk)
            ++failCount;
        std::printf("COEXIST: C-02 %s 引擎主上下文 version=%d.%d core=%d renderer=\"%s\"\n",
                    glOk ? "PASS" : "FAIL", gi.majorVersion,
                    gi.mainContext ? gi.mainContext->format().minorVersion() : 0,
                    gi.isCoreProfile ? 1 : 0, qPrintable(gi.renderer));
    }

    // ── C-03 引导后 QML 窗口仍能出帧（共存的核心判据）────────────────────────
    // 同样必须**主动请求重绘**：Qt Quick 按需渲染，静态页面不会自发产帧，
    // 只数 frameSwapped 会把"没东西请求重绘"误判成"引擎把窗口拖死了"。
    const int framesAtBoot = painted;
    QElapsedTimer observe;
    observe.start();
    while (observe.elapsed() < 3000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        window->update();
    }
    const int delta = painted - framesAtBoot;
    const bool alive = delta >= 60; // 3s ≥60 帧（≥20fps）：同一请求节奏下与引导前同量级
    if (!alive)
        ++failCount;
    std::printf("COEXIST: C-03 %s 引导后 QML 窗口仍能出帧：+%d 帧 / 3s（主动请求重绘，"
                "引导前基线 %d 帧 / 1.5s）\n",
                alive ? "PASS" : "FAIL", delta, framesBeforeBoot);

    // ── C-04 引擎显式帧驱动可用（update/draw 不炸，T11 的前提）────────────────
    if (booted) {
        StelApp &stelApp = StelApp::getInstance();
        stelApp.getCore()->setTimeRate(0.0);
        bool driveOk = true;
        QElapsedTimer driveClock;
        driveClock.start();
        try {
            for (int i = 0; i < 4; ++i) {
                stelApp.update(0.0);
                stelApp.draw();
            }
        } catch (...) {
            driveOk = false;
        }
        if (!driveOk)
            ++failCount;
        std::printf("COEXIST: C-04 %s 引擎显式帧驱动 update/draw ×4 未抛异常（%lldms）\n",
                    driveOk ? "PASS" : "FAIL", static_cast<long long>(driveClock.elapsed()));
    }

    // ── C-05..C-07 真实引擎帧驱动 + 离屏读回（T11 全部设计的前提）──────────────
    //
    // 要回答的**唯一**问题：引擎的 update/draw 是否画到 **LegacySkyHost 自己绑定的
    // 离屏 FBO**，还是硬编码画到 QOpenGLWidget 的默认 FBO？
    //   - 若尊重外部绑定 → "引擎产帧 → 邮箱 → QML 上屏"整条链成立，T11 只剩机械工作；
    //   - 若忽略外部绑定 → 读回全黑，T11 必须改为读 QOpenGLWidget 自身的 FBO
    //     （grabFramebuffer 路径），设计要动。
    //
    // 机制：LegacySkyHost 的 GlContextMode::kBorrowed 专为"与旧宿主同进程"预留
    // （见 LegacySkyHost.hpp 第 52-54 行），内部直接取 QOpenGLContext::currentContext()。
    // 我们用 StelMainView 的公开接口 glContextMakeCurrent() 把引擎上下文借出来。
    if (booted) {
        // 刻意用 new 且不 delete：本探针只对判据负责，末尾 _exit 不走析构栈。
        // 让栈对象在这里析构反而会触发借来上下文的 makeCurrent/清理顺序问题。
        auto *host = new stelapp::LegacySkyHost();
        stelapp::LegacySkyHostConfig hostCfg;
        hostCfg.contextMode = stelapp::GlContextMode::kBorrowed;
        // 借用模式下上下文由我们（旧宿主接口）管理：doneCurrent() 会清空 surface()，
        // 所以不能让 LegacySkyHost 自己 makeCurrent（2026-09-23 首跑实测）。
        hostCfg.contextManagedExternally = true;
        hostCfg.physicalSize = QSize(1280, 720);
        hostCfg.devicePixelRatio = 1.0;
        hostCfg.maxDimension = 4096;

        mainWin->glContextMakeCurrent();
        QString hostError;
        const bool hostOk = host->initialize(hostCfg, &hostError);
        mainWin->glContextDoneCurrent();

        if (!hostOk)
            ++failCount;
        const stelapp::LegacyGlInfo &hostGl = host->glInfo();
        std::printf("COEXIST: C-05 %s 借用引擎上下文装配离屏宿主（mode=borrowed ownContext=%d "
                    "GL=%s renderer=\"%s\" maxRenderbuffer=%d maxTexture=%d）\n",
                    hostOk ? "PASS" : "FAIL", hostGl.ownContext ? 1 : 0,
                    qPrintable(hostGl.version), qPrintable(hostGl.renderer),
                    hostGl.maxRenderbufferSize, hostGl.maxTextureSize);
        if (!hostOk)
            std::printf("COEXIST: C-05 失败原因：%s\n", qPrintable(hostError));
        std::fflush(stdout);

        if (hostOk) {
            stelapp::FrameMailbox probeBox;
            host->attachMailbox(&probeBox);

            StelApp &stelApp2 = StelApp::getInstance();
            StelCore *core = stelApp2.getCore();
            // 关掉引擎自己的时间推进，改用**显式 setJD**，保证"帧内容变化"可复现且可对账。
            core->setTimeRate(0.0);
            const double jd0 = core->getJD();

            // sim 参数在此复用为"帧序号"：每 +1.0 推进 0.25 天（6 小时），
            // 星空/大气/日月光照必然显著不同——这正是"不是静态残留"的硬证据。
            host->setRenderCallback([&stelApp2, core, jd0](double dt, double sim, const QSize &) {
                core->setJD(jd0 + sim * 0.25);
                stelApp2.update(dt);
                stelApp2.draw();
            });

            struct ProbeFrame
            {
                quint64 frameNumber = 0;
                quint64 hash = 0;
                double nonBlackRatio = 0.0;
                QSize size;
            };
            auto grabOne = [&](double sim) -> ProbeFrame {
                ProbeFrame pf;
                QString frameError;
                // 外部管理上下文：由旧宿主接口 current 引擎上下文，
                // renderOneFrame 内部不再切上下文，只校验。
                mainWin->glContextMakeCurrent();
                const bool rendered = host->renderOneFrame(sim, &frameError);
                mainWin->glContextDoneCurrent();
                if (!rendered) {
                    std::printf("COEXIST: 帧请求失败 sim=%.2f：%s\n", sim,
                                qPrintable(frameError));
                    return pf;
                }
                stelapp::FrameLease lease = probeBox.takeLatestFrame();
                if (!lease.valid())
                    return pf;
                const stelapp::LegacyFrame &fr = lease.frame();
                pf.frameNumber = fr.frameNumber;
                pf.size = fr.physicalSize;

                // FNV-1a 逐像素哈希 + 非黑像素比例。
                // 非黑比例用来区分"引擎真画了"与"全黑 FBO"；哈希用来区分"两帧内容不同"。
                quint64 h = 1469598103934665603ull;
                const quint64 prime = 1099511628211ull;
                quint64 nonBlack = 0;
                for (int y = 0; y < fr.physicalSize.height(); ++y) {
                    const quint8 *row = fr.pixels + qsizetype(y) * fr.rowStride;
                    for (int x = 0; x < fr.physicalSize.width(); ++x) {
                        const quint8 *p = row + qsizetype(x) * 4;
                        h ^= p[0]; h *= prime;
                        h ^= p[1]; h *= prime;
                        h ^= p[2]; h *= prime;
                        h ^= p[3]; h *= prime;
                        if (p[0] || p[1] || p[2])
                            ++nonBlack;
                    }
                }
                pf.hash = h;
                const quint64 total = quint64(fr.physicalSize.height())
                                      * quint64(fr.rowStride / 4);
                pf.nonBlackRatio = total ? double(nonBlack) / double(total) : 0.0;

                // 可选：转存 PNG 供人眼核对（STELQUICK_PROBE_DUMP_DIR=<目录>）
                const QString dumpDir = qEnvironmentVariable("STELQUICK_PROBE_DUMP_DIR");
                if (!dumpDir.isEmpty()) {
                    const QImage view(fr.pixels, fr.physicalSize.width(),
                                      fr.physicalSize.height(), fr.rowStride,
                                      QImage::Format_RGBA8888);
                    const QString path = QStringLiteral("%1/probe-frame-sim%2.png")
                                             .arg(dumpDir)
                                             .arg(int(sim * 100));
                    std::printf("COEXIST: 帧转存 %s → %s\n",
                                view.copy().save(path) ? "成功" : "失败",
                                qPrintable(path));
                }
                return pf;
            };

            const ProbeFrame first = grabOne(0.0);
            const ProbeFrame second = grabOne(1.0);
            const stelapp::LegacySkyHost::Stats hs = host->stats();

            // C-06：两帧都读回成功、零失败
            const bool readbackOk = first.frameNumber > 0 && second.frameNumber > 0
                                    && hs.failed == 0 && hs.published >= 2;
            if (!readbackOk)
                ++failCount;
            std::printf("COEXIST: C-06 %s 真实引擎帧驱动 + 离屏读回：请求=%llu 发布=%llu "
                        "丢弃=%llu 失败=%llu（帧 %llu/%llu，%dx%d，读回 %lldms/帧）\n",
                        readbackOk ? "PASS" : "FAIL",
                        static_cast<unsigned long long>(hs.requested),
                        static_cast<unsigned long long>(hs.published),
                        static_cast<unsigned long long>(hs.droppedByMailbox),
                        static_cast<unsigned long long>(hs.failed),
                        static_cast<unsigned long long>(first.frameNumber),
                        static_cast<unsigned long long>(second.frameNumber),
                        first.size.width(), first.size.height(),
                        static_cast<long long>(hs.lastReadbackMs));

            // C-07：帧内容非空 **且** 随 JD 变化 —— 这一条才真正证明
            // "引擎画到了我们的 FBO"而不是"读到了一块没人写过的黑内存"。
            const bool nonEmpty = first.nonBlackRatio > 0.001 && second.nonBlackRatio > 0.001;
            const bool changed = first.hash != 0 && first.hash != second.hash;
            const bool contentOk = nonEmpty && changed;
            if (!contentOk)
                ++failCount;
            std::printf("COEXIST: C-07 %s 帧内容非空且随 JD 变化：非黑比例 %.4f / %.4f，"
                        "哈希 %016llx / %016llx（JD +0.25 天）\n",
                        contentOk ? "PASS" : "FAIL", first.nonBlackRatio,
                        second.nonBlackRatio,
                        static_cast<unsigned long long>(first.hash),
                        static_cast<unsigned long long>(second.hash));
            if (!nonEmpty)
                std::printf("COEXIST: C-07 诊断：帧疑似全黑 —— 引擎可能忽略了外部 FBO 绑定，"
                            "需改走 QOpenGLWidget 自身 FBO 读回路径。\n");
            else if (!changed)
                std::printf("COEXIST: C-07 诊断：两帧哈希相同 —— JD 未真正生效或引擎未重绘。\n");
            std::fflush(stdout);
        }
    }

    std::printf("COEXIST: VERDICT=%s 失败项=%d\n", failCount == 0 ? "PASS" : "FAIL", failCount);
    std::printf("COEXIST: 结论：引擎无头引导与 QML 窗口同进程共存%s\n",
                failCount == 0 ? "成立" : "不成立");
    Q_UNUSED(app);

    const int rc = failCount == 0 ? 0 : 8;
    std::printf("COEXIST: 退出码=%d\n", rc);
    std::fflush(nullptr);

    // 不走 return：本探针把引擎与 QML 两套完整栈拉进同一进程，正常析构路径会撞上
    // 静态对象析构顺序（StelApp/StelMainView 与 QQuickWindow 各自的 GL/Metal 上下文
    // 谁先销毁），实测 return 之后进程 SIGSEGV(139)，退出码被信号覆盖成 139。
    // 探针只对判据负责，用 _exit 直接交付退出码（前面已 fflush）。
    _exit(rc);
}
#endif

int main(int argc, char **argv)
{
#if defined(STELQUICK_HAS_ENGINE)
    // ── 静态库里的 qrc 必须显式初始化（2026-09-23 实测踩到）──────────────────
    // data/mainRes.qrc 与 data/gui/guiRes.qrc 由 QT_ADD_RESOURCES 编成
    // qrc_mainRes.cpp / qrc_guiRes.cpp 并加入 libstelMain.a。但**静态库中的 qrc
    // 对象文件没有任何被引用的符号**，链接器按需拉取的模型下不会把它装进可执行文件，
    // 于是 qInitResources_* 从不执行 → 资源未注册 → 引擎读
    // ":/shaders/preethamAtmosphere.vert" 失败 → qFatal → SIGABRT(134)。
    // 这两行强制引用该符号，把资源真正注册进 Qt 资源系统。
    // （stellarium 主目标自己编了 qrc 所以从没暴露；只有链接静态库的新目标才会踩。）
    Q_INIT_RESOURCE(mainRes);
    Q_INIT_RESOURCE(guiRes);
#endif

    // 0. macOS 渲染兼容开关必须在创建任何窗口之前生效
    applyMacOsVulkanWorkaround();

    // 1. 必须在创建任何窗口之前显式选择后端（计划一第 2 节）
    //
    // 默认强制 Vulkan，且失败不静默回退（A1 验收要求）。
    // STELQUICK_GRAPHICS_API 的两个用途（2026-09-23 T13 定案）：
    //   1. **诊断对照**：隔离"Vulkan 专用缺陷"与"通用渲染缺陷"。
    //   2. **Metal 后端 = T13 验收配置**：MoltenVK 与 Apple-OpenGL 同进程共存
    //      必丢设备（判别矩阵见 docs/evidence/2026-09-23-t13-live-longrun/），
    //      该缺陷已独立归档；合流形态验收跑使用 `=metal`。
    //   其余取值仍只作诊断对照，正常验收不得使用。
    //   metal  — macOS：T13 起为合流形态验收配置（亦是 MoltenVK 之外的对照组）
    //   opengl — 跨平台对照组
    //   d3d11 / d3d12 — Windows 对照组（D3D11 是 Qt 在 Windows 的默认后端，
    //                   比 OpenGL 更贴近"该平台的健康基线"，故 Windows 上首选它做对照）
    const QByteArray apiOverride = qgetenv("STELQUICK_GRAPHICS_API").trimmed().toLower();
    QSGRendererInterface::GraphicsApi wantedApi = QSGRendererInterface::Vulkan;
    if (apiOverride == "metal")
        wantedApi = QSGRendererInterface::Metal;
    else if (apiOverride == "opengl" || apiOverride == "gl")
        wantedApi = QSGRendererInterface::OpenGL;
    else if (apiOverride == "d3d11" || apiOverride == "direct3d11")
        wantedApi = QSGRendererInterface::Direct3D11;
    else if (apiOverride == "d3d12" || apiOverride == "direct3d12")
        wantedApi = QSGRendererInterface::Direct3D12;
    else if (!apiOverride.isEmpty() && apiOverride != "vulkan")
        std::printf("STELQUICK: 未识别的后端名 \"%s\"，按 Vulkan 处理\n", apiOverride.constData());
    // 只有真的切到了非 Vulkan 后端才算"对照模式"（未识别名回落到 Vulkan 时不算）
    if (wantedApi != QSGRendererInterface::Vulkan)
        std::printf("STELQUICK: 诊断对照模式——请求后端 %s（非验收配置）\n",
                    apiName(wantedApi));
    QQuickWindow::setGraphicsApi(wantedApi);

#if defined(STELQUICK_WIDGETS_HOST)
    QApplication app(argc, argv);
    std::printf("STELQUICK: 宿主形态 = QApplication（Widgets，A3 引擎共存前置）\n");
#else
    QGuiApplication app(argc, argv);
#endif

    // ── T16：单一仿真时钟的**纯逻辑**自检（STELQUICK_CLOCK_CHECK=1）──────────
    // 不建窗口、不引导引擎、不起事件循环，只验 StelClockController 的语义本身。
    // 独立成一支的理由：这是 T16 里唯一一处**能在无 GL / 无引擎环境下复跑**的证据，
    // 也是"时钟语义"与"引擎是否恰好跑通"解耦的地方——合流形态若整体启动失败，
    // 这一支仍能给出时钟语义的对错。
    if (qEnvironmentVariableIsSet("STELQUICK_CLOCK_CHECK")) {
        QStringList clockLog;
        const bool clockOk = StelClockController::selfTest(&clockLog);
        std::printf("CLOCKCHECK: 仿真时钟纯逻辑自检（%d 项，无 GL / 无引擎）\n", int(clockLog.size()));
        for (const QString &line : clockLog)
            std::printf("CLOCKCHECK: %s\n", line.toUtf8().constData());
        std::printf("CLOCKCHECK: VERDICT=%s\n", clockOk ? "PASS" : "FAIL");
        std::fflush(nullptr);
        return clockOk ? 0 : 7;
    }
    app.setApplicationName("stelQuickUI");
    app.setOrganizationName("stellarium-vulkan");

    // ── A2 主体 T6 自检：旧宿主显式帧驱动（计划一 A2 硬约束）──────────────────
    //
    // 刻意放在**创建任何窗口之前**、且**不进入事件循环**：
    //   若放在窗口之后或靠事件循环推进，"到底是显式驱动在产帧，还是 Qt 顺手
    //   发了 paint 事件帮我们画了一帧"就无法区分——那正是本任务要证伪的假设。
    // 整条路径不经过 QML / Vulkan 纹理，因此不被 §6.2 的外部缺陷阻塞。
    if (qEnvironmentVariableIsSet("STELQUICK_LEGACY_HOST_TEST")) {
        const int frames = qEnvironmentVariableIntValue("STELQUICK_LEGACY_HOST_FRAMES");
        const stelapp::LegacyHostCheckResult r = stelapp::LegacyHostCheck::run(
            QSize(960, 540),
            frames > 0 ? frames : stelapp::LegacyHostCheck::kDefaultFrameCount,
            stelapp::LegacyHostCheck::kDefaultSimStartSeconds,
            stelapp::LegacyHostCheck::kDefaultSimStepSeconds);

        std::printf("LEGACYHOST: ==== A2 主体 T6｜旧宿主显式帧驱动自检 ====\n");
        std::printf("LEGACYHOST: %s\n", r.summary.toUtf8().constData());
        if (!r.setupError.isEmpty())
            std::printf("LEGACYHOST: 装配失败：%s\n", r.setupError.toUtf8().constData());
        for (const QString &line : r.details)
            std::printf("LEGACYHOST: %s\n", line.toUtf8().constData());
        for (const QString &line : r.frames)
            std::printf("LEGACYHOST: %s\n", line.toUtf8().constData());
        std::printf("LEGACYHOST: VERDICT=%s\n",
                    (r.ran && r.pass) ? "PASS" : "FAIL");
        std::fflush(stdout);
        return (r.ran && r.pass) ? 0 : 8;
    }

    // 2. Vulkan 实例预检（负向测试 P-CFG 前置）：QVulkanInstance::create() 失败时
    //    必须明确报错并以退出码 3 结束。跳过此步时 Qt 6.11 会在场景图初始化阶段
    //    SIGSEGV（实测 2026-09-18），拿不到诊断页显示机会。
    ensureVulkanLoaderPath(); // 先自动定位加载库，避免"必须手动 export 才能跑"

    // QVulkanInstance 的来源（2026-09-21 重定）：
    // 必须使用 Qt 的**默认实例单例** QVulkanDefaultInstance，而不是自己再建一个。
    // 症状（Windows + 原生驱动 + 校验层实测）：
    //   应用自建实例 + Qt 内部默认实例并存 → swapchain surface 归属其中一个，
    //   退出时另一个去销毁它，报
    //     VUID-vkDestroySurfaceKHR-surface-parent（surface 属于 A、用 B 销毁）
    //     UNASSIGNED-non-acquired-swapchain-image-used
    //   并在进程退出阶段以 0xC0000005 访问违例收尾（RC 拿不到）。
    // 之前 A2 的逐像素结果不受影响（渲染走的是 Qt 自己挑的实例），
    // 所以这个冲突一直以"噪音日志"的形式潜伏。
    // 现在改为向 Qt 索取唯一实例：实例全进程只有一个，归属与析构顺序自然一致。
    QVulkanInstance *vulkanInstance = QVulkanDefaultInstance::instance();
    if (!vulkanInstance) {
        std::fprintf(stderr,
                     "A1 失败：无法获取 Qt 默认 Vulkan 实例（QVulkanDefaultInstance::instance() 返回空）。\n"
                     "检查 Vulkan 加载库是否可用（Windows：vulkan-1.dll；macOS：QT_VULKAN_LIB）。\n"
                     "禁止静默回退 Metal/GL，程序退出（码 3）。\n");
        return 3;
    }

    // MoltenVK portability 坑（第三处，2026-09-20 A2 定位）：
    // 不开 VK_KHR_get_physical_device_properties2 时，MoltenVK 打印
    //   "VK_KHR_portability_subset should be enabled ... Expect problems."
    // 实际后果：Image/QSGImageNode 这类 QSGTextureMaterial 纹理全部静默渲染为黑
    // （文字、纯色矩形正常）。MoltenVK 1.4.2 + Qt 6.11.2 + Apple M3 实测，
    // Qt RHI 自己不会加这个实例扩展，必须应用侧显式开。
    // 注意：默认实例由 Qt 延迟创建，这里在它 create() 之前补扩展仍然有效。
    QByteArrayList vkInstanceExtensions = vulkanInstance->extensions();
    if (!vkInstanceExtensions.contains("VK_KHR_get_physical_device_properties2"))
        vkInstanceExtensions << "VK_KHR_get_physical_device_properties2";
    vulkanInstance->setExtensions(vkInstanceExtensions);

    // 负向测试 P-CFG 前置：实例必须真的能创建出来（禁止静默回退）。
    if (!vulkanInstance->isValid() && !vulkanInstance->create()) {
        std::fprintf(stderr,
                     "A1 失败：QVulkanInstance::create() 失败——Vulkan 不可用"
                     "（检查 QT_VULKAN_LIB 是否指向 libvulkan.1.dylib、MoltenVK ICD 是否安装）。\n"
                     "禁止静默回退 Metal/GL，程序退出（码 3）。\n");
        return 3;
    }

    // 3. Vulkan 设备探针（诊断数据，不参与渲染；鸿蒙构建可关闭）
    auto *backendInfo = new stelapp::BackendInfo(&app);
#ifdef STELQUICK_VULKAN_PROBE
    const stelapp::VulkanProbeResult probeResult = stelapp::VkDeviceProbe::probe();
    backendInfo->applyProbe(probeResult);
    // portability_driver = 设备级 VK_KHR_portability_subset（**驱动形态**判据）
    // portability_enum_ext = 实例级 VK_KHR_portability_enumeration（loader 能力，
    //   新版 loader 恒定提供，不能当驱动形态用；2026-09-21 Windows 实测误判后拆分）
    std::printf("STELQUICK: probe ok=%d device=%s api=%s driver=%s"
                " portability_driver=%d portability_enum_ext=%d err=%s\n",
                probeResult.ok ? 1 : 0,
                probeResult.deviceName.toUtf8().constData(),
                probeResult.apiVersion.toUtf8().constData(),
                probeResult.driverVersion.toUtf8().constData(),
                probeResult.portabilityDriver ? 1 : 0,
                probeResult.portabilityEnumerationExt ? 1 : 0,
                probeResult.error.toUtf8().constData());
    std::fflush(stdout);
#else
    backendInfo->applyProbeUnavailable(
        QStringLiteral("本构建未启用 VkDeviceProbe（STELQUICK_VULKAN_PROBE=OFF，鸿蒙交叉编译）"));
#endif
    qmlRegisterSingletonInstance("StelQuickUI", 1, 0, "BackendInfo", backendInfo);

    // A2：SkyViewport 由 QML 实例化，帧邮箱由 C++ 装配点注入（A3 起改由 AppFacade 持有）。
    // 邮箱声明在 engine 之前，保证比所有 FrameLease 活得久（FrameLease 的生命周期契约）。
    qmlRegisterType<stelapp::SkyViewport>("StelQuickUI", 1, 0, "SkyViewport");
    stelapp::FrameMailbox frameMailbox;
    // 动态帧生产者（消费侧接线）：DYN 自检 / STELQUICK_LIVE 手动模式使用。
    // 生命周期必须短于邮箱（生产者析构时解绑回调），故声明在邮箱之后。
    stelapp::LiveFrameSource liveSource;
#if defined(STELQUICK_HAS_ENGINE) && defined(STELQUICK_WIDGETS_HOST)
    // T11：真实引擎帧驱动（STELQUICK_LIVE_ENGINE=1）。堆上持有，app.exec() 返回后
    // 显式 stop；析构顺序仍保证先于邮箱（声明在邮箱之后）。
    std::unique_ptr<stelapp::LiveSkyRuntime> liveSkyRuntime;
#endif

    const bool a2Check = qEnvironmentVariableIsSet("STELQUICK_A2_CHECK");
    const bool dynCheck = qEnvironmentVariableIsSet("STELQUICK_DYN_CHECK");
    const bool longRun = qEnvironmentVariableIsSet("STELQUICK_LONGRUN");
    const bool liveEngine = qEnvironmentVariableIsSet("STELQUICK_LIVE_ENGINE");
    // T15：命令通路自检（U-ACT-01..03 的自动化断言，判据见 AppFacadeCheck.hpp）
    const bool actionCheck = qEnvironmentVariableIsSet("STELQUICK_ACTION_CHECK");
    // T17：搜索/选择模型自检（U-SRC-01..04 + 纵向判据，见 SearchModelCheck.hpp）
    const bool searchCheck = qEnvironmentVariableIsSet("STELQUICK_SEARCH_CHECK");
    // T18：定位/跟踪自检（守卫/锁定角距/推进时间判别性对照，见 LocateCheck.hpp）
    const bool locateCheck = qEnvironmentVariableIsSet("STELQUICK_LOCATE_CHECK");
    // T18：UI 层端到端自检（objectName 锚点 + 窗口真实鼠标事件，见下方 uiLocate*）
    const bool uiCheck = qEnvironmentVariableIsSet("STELQUICK_UI_CHECK");
    // T19：改时间自检（往返恒等 / 与旧公式逐位一致 / UTC 方向 / 星空随动，见 TimeCheck.hpp）
    const bool timeCheck = qEnvironmentVariableIsSet("STELQUICK_TIME_CHECK");
    // T19：时间页的 **UI 层端到端**自检（见下方 uiTime*）。与 timeCheck 分开：
    //   timeCheck 走 C++ 公共 API，timeUiCheck 走"真实控件 + 真实鼠标事件"。
    //   两者缺一不可 —— 前者证逻辑，后者证接线（T15 的血泪教训）。
    const bool timeUiCheck = qEnvironmentVariableIsSet("STELQUICK_TIME_UI_CHECK");
    // T20：**返回环**的 UI 层端到端自检（见下方 uiReturn*）。语义=切回天空页+状态全保留，
    // 实现全在 MainWindow.returnToSky()，只有"真实控件 + 真实事件 + pageStack.currentIndex"
    // 这一个观测面 ⇒ 它**只能**做成 UI 层判据（没有可断言的 C++ 对象）。
    const bool returnUiCheck = qEnvironmentVariableIsSet("STELQUICK_RETURN_UI_CHECK");
    // T25：**交互级（键盘/滚轮）**UI 层端到端自检（见下方 uiInteract*）。滚轮链路
    // （旧宿主 StelMainView → handleWheel）在合流形态 T25 前从未接过；键盘活链此前
    // 只有 C++ 直调证据。两者都需要"窗口真实事件"这一个观测面。
    const bool interactUiCheck = qEnvironmentVariableIsSet("STELQUICK_INTERACT_UI_CHECK");
    // T33-A：地点数据面**探针**（见 app/LocationProbe.hpp）。**只报读数、不下结论** ——
    // 它是"查事实"（数据面在合流 bundle 布局下加载得起来吗），不是判据。
    // 断言的活儿在 T33-C 的 LocationCheck。分开的理由同"纯逻辑腿/活引擎腿"：
    // 环境事实不该污染判据的通过率。
    const bool locProbe = qEnvironmentVariableIsSet("STELQUICK_LOC_PROBE");
    // T33-C：地点写入面自检（见 app/LocationCheck.hpp）。起始页切到 "location"，
    // 故同时验证地点页 QML 可被实例化（页面有语法/引用错误时这一步就会暴露）。
    const bool locCheck = qEnvironmentVariableIsSet("STELQUICK_LOC_CHECK");
    // T34-A：工具栏**数据面探针**（见 app/ToolbarProbe.hpp）。引擎 StelAction
    // 注册表与透传路径在 T34 之前没有任何判据走过 ⇒ 先证可用再写 UI/判据。
    const bool toolProbe = qEnvironmentVariableIsSet("STELQUICK_TOOL_PROBE");
    // T34-C：工具栏自检（见 app/ToolbarCheck.hpp）。
    const bool toolCheck = qEnvironmentVariableIsSet("STELQUICK_TOOL_CHECK");
    // T35-A：**仿真时间链路数据面探针**（见 app/TimeLinkProbe.hpp）。T27 留档的
    // "帧泵推进与 getTimeRate() 脱钩"线索悬了六个任务，且决定所有时间类判据的
    // 口径是否可信 ⇒ 先探针把三个量（ΔJD / 实测墙钟 / rate）在同一时刻量齐。
    const bool timeLinkProbe = qEnvironmentVariableIsSet("STELQUICK_TIMELINK_PROBE");
    // T35-C：时间链路自检（见 app/TimeLinkCheck.hpp）。
    const bool timeLinkCheck = qEnvironmentVariableIsSet("STELQUICK_TIMELINK_CHECK");
    // T36-C：**配置目录隔离**自检（见 app/ConfigIsolationCheck.hpp）。它验的是
    // 引导序列的**结果**（用户目录/落盘位置），所以不需要帧泵；引导一完成就能判。
    const bool cfgCheck = qEnvironmentVariableIsSet("STELQUICK_CONFIG_CHECK");
    // T37-A：**夜视链路数据面探针**（见 app/NightModeProbe.hpp）。A-1.0 表里
    // "天空夜视仍由旧后处理负责、避免叠加两次"这句话在合流形态从未验证过 ——
    // 探针要先回答"引擎旧夜视（挂在 QGraphicsItem 上的纯 GL effect）在我们的
    // 读回帧上到底可不可见"，再决定夜视在 Qt Quick 侧怎么实现。只报读数。
    const bool nightProbe = qEnvironmentVariableIsSet("STELQUICK_NIGHT_PROBE");
    // T38-A：**显示参数命令面探针**（见 app/DisplayProbe.hpp）。A-1.0 把"亮度/星等、
    // 视场、投影"列为必须（基础级）—— 这三项在合流形态从未验证过；审计发现三个
    // 陷阱（setter 不夹取 / setFov 有夹取 / 投影非法 key 静默兜底），必须先实测坐实。
    const bool displayProbe = qEnvironmentVariableIsSet("STELQUICK_DISPLAY_PROBE");
    // T37-C：**夜视自检**（见 app/NightModeCheck.hpp）。判据 NC-01..NC-05。
    // 负控开关：`STELQUICK_NIGHT_EFFECT_OFF=1`（QML layer.enabled 强制 false）
    // ⇒ NC-03② 必红且只它红（证明"效果面"判据承重）。
    const bool nightCheck = qEnvironmentVariableIsSet("STELQUICK_NIGHT_CHECK");
    const bool displayCheck = qEnvironmentVariableIsSet("STELQUICK_DISPLAY_CHECK");
    // T38-C 显示参数自检（见 app/DisplayCheck.hpp）：判据 DP-00..DP-11。装配顺序同其他
    // 自检，运行块在下方紧邻 displayProbe。负控：STELQUICK_DISPLAY_GATE_OFF=1
    // （关范围闸/白名单闸）/ STELQUICK_DISPLAY_FWD_OFF=1（断引擎→façade 转发）。
    // T39-A：**高 DPI + 渲染诊断数据面探针**（见 app/HiDpiProbe.hpp）。A-1.0 范围表
    // 最后一格"必须"里的"高 DPI"；审计发现两条可疑（getGuiFontSize 读的是全局 app
    // font / setGuiFontSize 改全局字体），必须先实测坐实。只报读数。
    const bool hiDpiProbe = qEnvironmentVariableIsSet("STELQUICK_HIDPI_PROBE");
    // T39-C：**高 DPI + 渲染诊断自检**（见 app/HiDpiCheck.hpp）。判据 HP-00..HP-11。
    // 负控复用 T38 的两个开关：STELQUICK_DISPLAY_GATE_OFF（范围闸旁路）⇒ 预期恰红
    // [HP-02,HP-03,HP-04]；STELQUICK_DISPLAY_FWD_OFF（断引擎→façade 订阅）⇒ 预期恰红
    // [HP-07,HP-08]。
    const bool hiDpiCheck = qEnvironmentVariableIsSet("STELQUICK_HIDPI_CHECK");

    // T40-A：**快捷键编辑命令面探针**（见 app/ShortcutProbe.hpp）。A-1.0 范围表
    // 倒数第二格第一列「快捷键编辑」——第三列的约束（中文输入焦点不触发天空快捷键）
    // 已由 T29 兜住，本格要交付的是**页面本体**。引擎侧面（setShortcut/saveShortcuts/
    // restoreDefaultShortcut/findActionFromShortcut/routeKey）全现成，先探清落盘语义、
    // 不写穿、空串=移除、冲突两支语义相反等事实。只报读数。
    // ⚠️ 刻意不启帧泵：快捷键是纯命令面，与 GPU 无关（少一个 Metal 环节少一份风险）。
    const bool shortcutProbe = qEnvironmentVariableIsSet("STELQUICK_SHORTCUT_PROBE");
    // T40-C：**快捷键编辑自检**（见 app/ShortcutCheck.hpp）。判据 SC-01..SC-14。
    // 负控：STELQUICK_SHORTCUT_WRITE_OFF=1（改键不写引擎不落盘）⇒ 预期恰红
    // [SC-05a,SC-05b,SC-07]；STELQUICK_SHORTCUT_SAVE_OFF=1（写引擎不落盘）⇒ 预期恰红
    // [SC-05b]。B ⊂ A —— 只有 SC-05b 红 = 保存环节坏；SC-05a 也红 = 写入环节坏。
    const bool shortcutCheck = qEnvironmentVariableIsSet("STELQUICK_SHORTCUT_CHECK");

    // T41-A：**帮助/版本/许可证数据面探针**（见 app/HelpProbe.hpp）。A-1.0 范围表
    // 倒数第二格按 T40 拆分后余下的三项（帮助 / 版本 / 许可证）。老宿主那份是
    // `src/gui/HelpDialog`（QWidget 四标签对话框 + 硬编码 HTML 键位表 + 联网检查更新），
    // 而 `src/gui/` **完全不在本形态的构建里** ⇒ 要先探清"数据在不在、什么形态、
    // 运行时可不可达"，再决定 QML 侧怎么重建。**只读、零写入、不启帧泵**。
    const bool helpProbe = qEnvironmentVariableIsSet("STELQUICK_HELP_PROBE");
    // T41-C：帮助/版本/许可证自检（判据 HC-01..HC-17，见 app/HelpCheck.hpp）。
    // 负控 A `STELQUICK_HELP_TAKEOVER_OFF=1`（main.cpp 接管注册段读它）
    // 负控 B `STELQUICK_HELP_LICENSE_OFF=1`（HelpModel::loadLicense 读它）。
    const bool helpCheck = qEnvironmentVariableIsSet("STELQUICK_HELP_CHECK");

    // T20：**I-REP-02 全流程回放**自检（见下方 uiReplay*）——A-alpha 的出口测试。
    // 与 returnUiCheck 分开：那个验"返回这一环"，这个验"五环串起来能不能跑通"。
    const bool replayCheck = qEnvironmentVariableIsSet("STELQUICK_REPLAY_CHECK");
    // 起始页：A2/DYN/长跑校验必须停在天空页；搜索/定位/时间自检停在各自页面
    // （顺带验证该 QML 页面能真正被实例化——页面有语法/引用错误时这一步就会暴露，
    //  而不是等人工点击）；手动模式下可用 STELQUICK_PAGE 指定。
    // T20：返回环与 I-REP-02 回放都从**天空页**起步（前者要先从它切走再切回来，
    //   后者直接断言"开机态=天空页"）。
    // T39：`STELQUICK_PAGE` **显式指定优先**（此前它排在三元链最后，任何起引擎的
    // 模式都会把它吞掉 ⇒ `LIVE_ENGINE=1 STELQUICK_PAGE=diag` 实测停在天空页，
    // 诊断页的运行时数据没法人眼验证 —— 显式指定 > 自动选择，这是常规语义）。
    const QString pageEnv = qEnvironmentVariable("STELQUICK_PAGE");
    const QString startPage = !pageEnv.isEmpty()
                                  ? pageEnv
                                  : ((a2Check || dynCheck || longRun || returnUiCheck || replayCheck
                                      || interactUiCheck || locProbe || toolProbe || toolCheck
                                      || timeLinkProbe || timeLinkCheck || cfgCheck || nightProbe
                                      || nightCheck || displayProbe || displayCheck
                                      || hiDpiProbe || hiDpiCheck || shortcutProbe
                                      || shortcutCheck || helpProbe || helpCheck
                                      || qEnvironmentVariableIsSet("STELQUICK_LIVE")
                                      || liveEngine)
                                         ? QStringLiteral("sky")
                                         : ((searchCheck || locateCheck || uiCheck)
                                                ? QStringLiteral("search")
                                                : ((timeCheck || timeUiCheck)
                                                       ? QStringLiteral("time")
                                                       : (locCheck
                                                              ? QStringLiteral("location")
                                                              : QStringLiteral("diag")))));

    // 3. 加载 QML
    // T15 命令通路装配（加载前注入，QML 命令栏/Keys 直接绑定）：
    //   · 合流形态拆除 StelAction 的 QWidget 注册假设——QAction 不再挂到从不
    //     show 的 StelMainView（QML 窗口本就收不到分发，白留只会"双轨"），
    //     键盘唯一入口 = ActionRouter::routeKey（复用 StelAction::matches 单点）。
    //   · AppFacade 命令注册进 ActionRouter；帧泵启动后 attachSimControl 注入
    //     仿真推进控制（暂停/继续/速率）。
#if defined(STELQUICK_HAS_ENGINE) && defined(STELQUICK_WIDGETS_HOST)
    StelAction::setWidgetShortcutDispatchEnabled(false);
    std::printf("ACTIONCHECK: widget QAction 分发已拆除（合流形态，键盘经 ActionRouter 单点路由）\n");
#endif
    stelapp::AppFacade appFacade;
    stelapp::ActionRouter actionRouter;
    // T40-B：快捷键编辑的模型 + 命令面。**引擎是延迟引导的**（A3）⇒ 这里先创建空模型，
    // 引导完成后在下方调用 `refresh()` 拉注册表（否则 QML 在引导前实例化只会看到空表）。
    stelapp::ShortcutModel shortcutModel;
    // T41-B：帮助/版本/许可证的只读数据面。与 ShortcutModel 同理 —— 版本串依赖引擎，
    // 这里先建对象，引导完成后由 `refresh()` 填（QML 用 `HelpModel.ready` 显示空态）。
    // GPL 全文来自 qrc（构造时读一次），与引擎无关。
    stelapp::HelpModel helpModel;
    actionRouter.registerAction(QStringLiteral("app.togglePause"),
                                { [&appFacade]() { appFacade.togglePause(); },
                                  nullptr,
                                  QStringLiteral("暂停/继续仿真") });
    actionRouter.registerAction(QStringLiteral("app.zoomIn"),
                                { [&appFacade]() { appFacade.zoomIn(); },
                                  nullptr,
                                  QStringLiteral("放大视场") });
    actionRouter.registerAction(QStringLiteral("app.zoomOut"),
                                { [&appFacade]() { appFacade.zoomOut(); },
                                  nullptr,
                                  QStringLiteral("缩小视场") });
    // T18：定位与跟踪。**刻意不绑键位**——引擎自己的动作表里已经有
    // "对准选中天体"（`/`）与"开关跟踪"（空格）且经 ActionRouter 的引擎透传可用；
    // 这里再绑一次就是双轨（T15 定下的"单点键位路由铁律"）。这两条只服务 QML 按钮。
    actionRouter.registerAction(QStringLiteral("app.locateSelected"),
                                { [&appFacade]() { appFacade.locateSelected(true); },
                                  nullptr,
                                  QStringLiteral("定位到选中天体并跟踪") });
    actionRouter.registerAction(QStringLiteral("app.toggleTracking"),
                                { [&appFacade]() { appFacade.toggleTracking(); },
                                  nullptr,
                                  QStringLiteral("开关跟踪选中天体") });

    // ── T41：老 QWidget 窗口动作的**宿主接管** ────────────────────────────────
    // 动机与"到哪一页"的映射见 `MainWindow.qml::hostActionPages` 的注释。要点：
    // T41-A 探针 Q13/Q14 实测 —— `src/gui/` 的 QWidget GUI 子系统随 `stelMain`
    // 静态库链进了本形态，`StelApp::getGui()` 非空 ⇒ 老宿主那 8 个窗口动作全都
    // 活着，`trigger()` 会**真的弹出老式对话框**（QWidget 总数 12 → 58）。
    // 这里只**声明接管**（ActionRouter 负责"命中就不转发引擎"），去哪一页归 QML。
    // ⚠️ 只接管**有 QML 对应页**的动作：F2 设置 / F10 天文计算 / F12 脚本控制台
    //    没有对应页 ⇒ 刻意不接管（记入 T42「未支持项」清单）—— 接管到不存在的页
    //    只是把"弹老对话框"换成"按了没反应"，那是另一种骗人。
    // ⚠️ 负控 `STELQUICK_HELP_TAKEOVER_OFF=1` 跳过整段注册，用来证"接管相关的判据
    //    真的承重"（关掉后 F1 会恢复成"真的弹出老对话框"，判据必须红）。
    if (!qEnvironmentVariableIsSet("STELQUICK_HELP_TAKEOVER_OFF"))
    {
        actionRouter.setHostTakeover(QStringLiteral("actionShow_Help_Window_Global"), true);
        actionRouter.setHostTakeover(QStringLiteral("actionShow_Search_Window_Global"), true);
        actionRouter.setHostTakeover(QStringLiteral("actionShow_SkyView_Window_Global"), true);
        actionRouter.setHostTakeover(QStringLiteral("actionShow_DateTime_Window_Global"), true);
        actionRouter.setHostTakeover(QStringLiteral("actionShow_Location_Window_Global"), true);
        actionRouter.setHostTakeover(QStringLiteral("actionShow_Shortcuts_Window_Global"), true);
        std::printf("HELP: 已接管 %d 个老式窗口动作（F1/F3/F4/F5/F6/F7）\n",
                    int(actionRouter.hostTakeoverIds().size()));
    }
    else
    {
        std::printf("HELP: （负控）STELQUICK_HELP_TAKEOVER_OFF=1 —— 未注册任何接管\n");
    }

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("appFacade"), &appFacade);
    engine.rootContext()->setContextProperty(QStringLiteral("ActionRouter"), &actionRouter);
    // T17：两个模型作为**上下文属性**注入（与 ActionRouter 同一惯例）。
    // 刻意不给 AppFacade 加 Q_PROPERTY 指针：那样 QML 引擎需要额外注册这两个类型，
    // 而上下文属性的类型解析走 QObject 元对象，零注册成本、零"类型未注册"风险。
    engine.rootContext()->setContextProperty(QStringLiteral("searchResults"),
                                             appFacade.searchResults());
    engine.rootContext()->setContextProperty(QStringLiteral("objectInfo"),
                                             appFacade.objectInfo());
    // T40-B：快捷键编辑模型（同上惯例：上下文属性，不做类型注册）。
    engine.rootContext()->setContextProperty(QStringLiteral("ShortcutModel"), &shortcutModel);
    // T41-B：帮助/版本/许可证数据面（同上惯例）。
    engine.rootContext()->setContextProperty(QStringLiteral("HelpModel"), &helpModel);
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                     []() { std::exit(2); }, Qt::QueuedConnection);
    engine.load(QUrl(QStringLiteral("qrc:/StelQuickUI/qml/MainWindow.qml")));

    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().value(0, nullptr));
    if (!window) {
        std::fprintf(stderr, "A1 失败：主窗口创建失败\n");
        return 2;
    }

    // 注意：不能用 setContextProperty 注入起始页——QML 根对象自己声明了同名属性会遮蔽上下文属性
    // （2026-09-20 实测：视口尺寸 0x0，页面压根没切过去）。改为加载后显式赋值。
    window->setProperty("startPage", startPage);

    // 预检通过的实例挂到窗口，避免 "No QVulkanInstance set" 崩溃路径
    window->setVulkanInstance(vulkanInstance);

    // A2 装配点：找到 QML 实例化的天空视口，注入帧邮箱
    auto *skyViewport = window->findChild<stelapp::SkyViewport *>(QStringLiteral("skyViewport"));
    if (skyViewport) {
        skyViewport->setFrameMailbox(&frameMailbox);
    } else {
        std::fprintf(stderr, "提示：未找到 SkyViewport（skyViewport），A2 帧通路不可用\n");
    }

    // 调试对照图：把 STELQUICK_PATTERN_DUMP 的路径传给 SkyTestPage 的 Image。
    // 该 Image 用于区分"QSGImageNode 路径坏了"与"窗口渲染/抓帧坏了"。
    // 未设环境变量时传空串 → 对照 Image 不加载（也避免 Windows 上没有 /tmp 的假报错）。
    {
        const QString dumpPath = qEnvironmentVariable("STELQUICK_PATTERN_DUMP");
        if (!dumpPath.isEmpty()) {
            const QString url = QUrl::fromLocalFile(dumpPath).toString();
            if (!window->setProperty("debugPatternSource", url))
                std::fprintf(stderr, "提示：SkyTestPage 的 debugPatternSource 属性未生效\n");
            else
                std::printf("STELQUICK: 调试对照图 = %s\n", url.toUtf8().constData());
        }
    }

    const int baseW = window->width();
    const int baseH = window->height();

    // 场景图初始化后校验实际 API（后端必须在创建窗口前选定，此处只能确认）
    //
    // 2026-09-21 Windows 定位：`sceneGraphInitialized` 在 Windows + Vulkan(RHI) 下
    // **不一定送达**。实测（RTX 4060 / Qt 6.11.2 / VS2026 Release）：
    //   - A2 逐像素校验 12/12 PASS → 场景图确实起来了、确实在渲染；
    //   - 但本连接的 lambda 一次都没执行（日志里始终没有 runtimeApi= 那一行）；
    //   - 结果 backendOk 恒为 false → 明明渲染与像素全对，进程仍以 3 退出。
    // 根因是信号发送时机与连接/初始化顺序的竞态：设为 Vulkan 后 Qt 会在
    //   `setVulkanInstance()` 之前/之中就把场景图建好并发出该信号，连接建立时
    //   信号已经过去了（Qt 信号不重放）。macOS 上恰好落在信号之后，所以一直没暴露。
    //
    // 处置：不再只依赖信号。把判定抽成具名 lambda，两条路都调用它：
    //   ① 信号到达（正常路径，macOS 及部分 Windows 配置）；
    //   ② 延迟兜底轮询 rendererInterface()（signal 丢失时的兜底）。
    // 判定本身是幂等的（只是读 graphicsApi() 并赋值），重复调用无副作用。
    bool backendOk = false;
    bool backendChecked = false;
    auto checkBackendApi = [&window, backendInfo, &backendOk, &backendChecked,
                            skyViewport, wantedApi]() {
        if (backendChecked)
            return;
        QSGRendererInterface *rif = window->rendererInterface();
        if (!rif || rif->graphicsApi() == QSGRendererInterface::Unknown)
            return;   // 场景图还没就绪，等下一次
        backendChecked = true;

        const auto api = rif->graphicsApi();
        const QString name = QString::fromUtf8(apiName(api));
        // backendOk = 实际后端与请求后端一致（wantedApi 跟随用户请求）。
        // ⚠️ T41 修复：判定**只有这一份** —— BackendInfo::applyRuntimeApi 旧实现
        //   自判 `(apiName == "Vulkan")`，T39 转 Metal 后恒 false（状态色全错）。
        backendOk = (api == wantedApi);
        // 视口也要知道后端判定：非请求后端时进入 error 态而不是假装 ready（Q-001）
        if (skyViewport)
            skyViewport->applyBackendResult(name, backendOk);
        backendInfo->applyBackendResult(name, backendOk);

        std::printf("STELQUICK: runtimeApi=%s backendOk=%d device=%s（判定来源=%s）\n",
                    name.toUtf8().constData(), backendOk ? 1 : 0,
                    backendInfo->deviceName().toUtf8().constData(),
                    window->isSceneGraphInitialized() ? "信号/兜底" : "兜底");
        std::fflush(stdout);
        if (!backendOk) {
            std::fprintf(stderr,
                         "A1 失败：请求 %s，实际 %s。禁止静默回退，程序将退出。\n",
                         apiName(wantedApi), name.toUtf8().constData());
        }
    };

    QObject::connect(window, &QQuickWindow::sceneGraphInitialized, window, checkBackendApi);

    window->show();

#if defined(STELQUICK_HAS_ENGINE) && defined(STELQUICK_WIDGETS_HOST)
    // ── A3 前置探针：引擎无头引导 × QML 窗口同进程 ──────────────────────────
    // 必须在 window->show() 之后调用：要验证的正是"QML 窗口已存在时引导引擎"。
    if (qEnvironmentVariableIsSet("STELQUICK_ENGINE_COEXIST")) {
        const int coexistRc = runEngineCoexistProbe(&app, window);
        std::printf("COEXIST: 退出码=%d\n", coexistRc);
        std::fflush(stdout);
        return coexistRc;
    }
#endif

    // 兜底轮询：每 100ms 试一次，最多 5s。判定成功后立刻停表（幂等，不重复打印）。
    {
        auto *poll = new QTimer(window);
        poll->setInterval(100);
        QObject::connect(poll, &QTimer::timeout, window, [poll, checkBackendApi, window]() {
            checkBackendApi();
            if (window->isSceneGraphInitialized() || (poll->property("tries").toInt() > 50))
                poll->stop();
            poll->setProperty("tries", poll->property("tries").toInt() + 1);
        });
        poll->setProperty("tries", 0);
        poll->start();
    }

    // A2 静态图自动校验（用例 I-STC-01/02）：投递测试图案 → 等上传确认 → 抓帧逐像素比对
    if (a2Check) {
        if (!skyViewport) {
            std::fprintf(stderr, "A2 校验失败：未找到 SkyViewport\n");
            return 6;
        }
        stelapp::A2FrameCheck::runStartupSequence(
            &app, window, skyViewport, &frameMailbox,
            [&app](const stelapp::A2CheckResult &result) {
                std::printf("A2CHECK: %s\n", result.summary.toUtf8().constData());
                for (const QString &line : result.details)
                    std::printf("A2CHECK:%s\n", line.toUtf8().constData());
                if (!result.checkRan) {
                    std::printf("A2CHECK: VERDICT=UNAVAILABLE（校验手段不可用，不得据此声称通过）\n");
                    std::fflush(stdout);
                    app.exit(6);
                    return;
                }
                std::printf("A2CHECK: VERDICT=%s\n", result.pass ? "PASS" : "FAIL");
                std::fflush(stdout);
                app.exit(result.pass ? 0 : 5);
            });
        const int rc = app.exec();
        return (rc == 0 && !backendOk) ? 3 : rc;
    }

    // 动态帧通路自检（消费侧接线，I-DYN / P-BRG-01 前置 / P-BRG-04）
    //
    // T12：生产者可切（STELQUICK_DYN_PRODUCER）。
    //   · 默认/"test" —— LiveFrameSource（LegacyTestScene 替身，独立线程 + kOwn 上下文）
    //   · "engine"    —— LiveSkyRuntime（**真实引擎** StelApp::update/draw，GUI 线程 + 借上下文）
    // 两种生产者的装配方式差异巨大（后者须先 boot 引擎），故装配在此完成，
    // 自检只接收已启动的 IFrameProducer——同一套 7 项判据在两者上复跑。
    if (dynCheck) {
        if (!skyViewport) {
            std::fprintf(stderr, "DYN 校验失败：未找到 SkyViewport\n");
            return 6;
        }
        stelapp::DynFrameCheck::Options dynOptions = stelapp::DynFrameCheck::optionsFromEnv();
        const QByteArray producerKind = qgetenv("STELQUICK_DYN_PRODUCER");
        const bool engineProducer = (producerKind == "engine");
#if defined(STELQUICK_HAS_ENGINE) && defined(STELQUICK_WIDGETS_HOST)
        if (engineProducer)
        {
            // 真实引擎的测量窗与产能口径（依据见 kEngineNominalFps 注释）：
            // ① 默认 20 秒 —— 引擎前 ~10s 在从 warmup 爬升，窗口太短则尾窗仍在爬升段，
            //    判据会稳定卡在边界（T12 首测：15s 窗尾窗 34.5~36.4 fps vs 下限 36.0）；
            // ② 名义速率 50 —— 引擎受渲染开销限制达不到 UI 上限 60fps，
            //    用它当名义是定性错误。
            if (!qEnvironmentVariableIsSet("STELQUICK_DYN_SECONDS"))
                dynOptions.seconds = 20;
            if (!qEnvironmentVariableIsSet("STELQUICK_LIVE_FPS"))
                dynOptions.producerFps = kEngineNominalFps;
        }
#endif
        stelapp::IFrameProducer *producer = nullptr;
        QString producerError;

        if (!engineProducer) {
            // 替身生产者：LegacyTestScene，独立线程 + kOwn 离屏上下文。
            stelapp::LiveFrameSourceConfig cfg;
            cfg.physicalSize = dynOptions.physicalSize;
            cfg.devicePixelRatio = dynOptions.devicePixelRatio;
            cfg.fps = dynOptions.producerFps;
            cfg.simRate = 1.0;
            if (liveSource.start(&frameMailbox, cfg, &producerError))
                producer = &liveSource;
        }
#if defined(STELQUICK_HAS_ENGINE) && defined(STELQUICK_WIDGETS_HOST)
        else {
            // 真实引擎生产者：先暖机（顺序纪律见 warmUpSceneGraph），再 boot 引擎，
            // 最后起帧泵。三者缺一不可——不暖机会后端漂移，不 boot 则 start 拒绝。
            warmUpSceneGraph(window);
            liveSkyRuntime = std::make_unique<stelapp::LiveSkyRuntime>();
            if (!liveSkyRuntime->boot(&producerError)) {
                std::fprintf(stderr, "DYNCHECK: 引擎引导失败：%s\n",
                             producerError.toUtf8().constData());
                liveSkyRuntime.reset();
            } else if (!liveSkyRuntime->start(&frameMailbox, engineConfigFromEnv(0.1, kEngineNominalFps),
                                              &producerError)) {
                std::fprintf(stderr, "DYNCHECK: 引擎帧泵启动失败：%s\n",
                             producerError.toUtf8().constData());
                liveSkyRuntime.reset();
            } else {
                producer = liveSkyRuntime.get();
                appFacade.attachSimControl(liveSkyRuntime.get());   // T15：暂停/继续落点
            }
        }
#else
        else {
            producerError = QStringLiteral(
                "engine 生产者需要 Widgets 宿主形态构建（-DSTELQUICKUI_WIDGETS_HOST=ON 且 "
                "-DENABLE_STELQUICKUI=ON）");
        }
#endif

        if (!producer) {
            std::fprintf(stderr, "DYNCHECK: 生产者装配失败：%s\n",
                         producerError.toUtf8().constData());
            std::printf("DYNCHECK: VERDICT=UNAVAILABLE（生产者未装配，不得据此声称通过）\n");
            std::fflush(stdout);
            return 6;
        }

        std::printf("DYNCHECK: 生产者=%s\n", engineProducer ? "engine(真实引擎)" : "test(替身场景)");
        std::fflush(stdout);

        stelapp::DynFrameCheck::runStartupSequence(
            &app, window, skyViewport, &frameMailbox, producer, dynOptions,
            [&app](const stelapp::DynCheckResult &result) {
                std::printf("DYNCHECK: %s\n", result.summary.toUtf8().constData());
                for (const QString &line : result.details)
                    std::printf("DYNCHECK: %s\n", line.toUtf8().constData());
                if (!result.ran) {
                    std::printf("DYNCHECK: VERDICT=UNAVAILABLE（装配失败，不得据此声称通过）\n");
                    std::fflush(stdout);
                    app.exit(6);
                    return;
                }
                std::printf("DYNCHECK: VERDICT=%s\n", result.pass ? "PASS" : "FAIL");
                std::fflush(stdout);
                app.exit(result.pass ? 0 : 8);
            });
        const int rc = app.exec();
#if defined(STELQUICK_HAS_ENGINE) && defined(STELQUICK_WIDGETS_HOST)
        if (engineProducer) {
            // 引擎引导过的进程不走正常 return：双图形栈析构顺序未定义（探针实测
            // return 后 SIGSEGV(139) 吞掉判据码）。与探针/T11 同一纪律。
            if (liveSkyRuntime) {
                liveSkyRuntime->stop();
                liveSkyRuntime.reset();
            }
            std::fflush(nullptr);
            _exit(backendOk ? rc : 3);
        }
#endif
        return (rc == 0 && !backendOk) ? 3 : rc;
    }

    // T15 命令通路自检（STELQUICK_ACTION_CHECK=1）：需要真实引擎 + 帧泵运行
    // （暂停冻结/恢复判据要有 JD 在走）。装配与 DYN-engine 完全同一顺序纪律
    // （暖机 → boot → start → attach），判据本体见 AppFacadeCheck。
    if (actionCheck) {
#if !defined(STELQUICK_HAS_ENGINE) || !defined(STELQUICK_WIDGETS_HOST)
        std::printf("ACTIONCHECK: VERDICT=UNAVAILABLE（需要合流形态构建）\n");
        std::fflush(stdout);
        return 6;
#else
        warmUpSceneGraph(window);
        liveSkyRuntime = std::make_unique<stelapp::LiveSkyRuntime>();
        QString producerError;
        if (!liveSkyRuntime->boot(&producerError)) {
            std::fprintf(stderr, "ACTIONCHECK: 引擎引导失败：%s\n",
                         producerError.toUtf8().constData());
            std::printf("ACTIONCHECK: VERDICT=UNAVAILABLE\n");
            std::fflush(stdout);
            return 6;
        }
        stelapp::LiveSkyRuntime::Config cfg =
            engineConfigFromEnv(0.1, kEngineNominalFps);
        if (!liveSkyRuntime->start(&frameMailbox, cfg, &producerError)) {
            std::fprintf(stderr, "ACTIONCHECK: 帧泵启动失败：%s\n",
                         producerError.toUtf8().constData());
            std::printf("ACTIONCHECK: VERDICT=UNAVAILABLE\n");
            std::fflush(stdout);
            return 6;
        }
        appFacade.attachSimControl(liveSkyRuntime.get());

        stelapp::AppFacadeCheck::runStartupSequence(
            &app, window, &appFacade, &actionRouter,
            [&app](const stelapp::AppFacadeCheck::Result &result) {
                std::printf("ACTIONCHECK: %s\n", result.summary.toUtf8().constData());
                for (const QString &line : result.details)
                    std::printf("ACTIONCHECK: %s\n", line.toUtf8().constData());
                if (!result.ran) {
                    std::printf("ACTIONCHECK: VERDICT=UNAVAILABLE\n");
                    std::fflush(stdout);
                    app.exit(6);
                    return;
                }
                std::printf("ACTIONCHECK: VERDICT=%s\n", result.pass ? "PASS" : "FAIL");
                std::fflush(stdout);
                app.exit(result.pass ? 0 : 8);
            });
        const int rc = app.exec();
        // 引擎引导过的进程不走正常 return（双图形栈析构纪律，与 DYN-engine 相同）
        if (liveSkyRuntime) {
            liveSkyRuntime->stop();
            liveSkyRuntime.reset();
        }
        std::fflush(nullptr);
        _exit(backendOk ? rc : 3);
#endif
    }

    // T17 搜索/选择模型自检（STELQUICK_SEARCH_CHECK=1）：需要真实引擎
    // （阶段 B 要真的能搜到天体）。装配顺序与 ACTION_CHECK 完全一致
    // （暖机 → boot → start → attach → 判据），判据本体见 SearchModelCheck。
    // 起始页已在上面切到 "search"，故同时验证搜索页 QML 可被实例化。
    if (searchCheck) {
#if !defined(STELQUICK_HAS_ENGINE) || !defined(STELQUICK_WIDGETS_HOST)
        std::printf("SEARCHCHECK: VERDICT=UNAVAILABLE（需要合流形态构建）\n");
        std::fflush(stdout);
        return 6;
#else
        warmUpSceneGraph(window);
        liveSkyRuntime = std::make_unique<stelapp::LiveSkyRuntime>();
        QString producerError;
        if (!liveSkyRuntime->boot(&producerError)) {
            std::fprintf(stderr, "SEARCHCHECK: 引擎引导失败：%s\n",
                         producerError.toUtf8().constData());
            std::printf("SEARCHCHECK: VERDICT=UNAVAILABLE\n");
            std::fflush(stdout);
            return 6;
        }
        stelapp::LiveSkyRuntime::Config cfg =
            engineConfigFromEnv(0.1, kEngineNominalFps);
        if (!liveSkyRuntime->start(&frameMailbox, cfg, &producerError)) {
            std::fprintf(stderr, "SEARCHCHECK: 帧泵启动失败：%s\n",
                         producerError.toUtf8().constData());
            std::printf("SEARCHCHECK: VERDICT=UNAVAILABLE\n");
            std::fflush(stdout);
            return 6;
        }
        appFacade.attachSimControl(liveSkyRuntime.get());

        stelapp::SearchModelCheck::run(
            &app, &appFacade,
            [&app](const stelapp::SearchModelCheck::Result &result) {
                std::printf("SEARCHCHECK: %s\n", result.summary.toUtf8().constData());
                for (const QString &line : result.details)
                    std::printf("SEARCHCHECK: %s\n", line.toUtf8().constData());
                if (!result.ran) {
                    std::printf("SEARCHCHECK: VERDICT=UNAVAILABLE\n");
                    std::fflush(stdout);
                    app.exit(6);
                    return;
                }
                if (result.unavailable) {
                    // 阶段 A 已全绿、只是环境里没有可检索天体——照实报 UNAVAILABLE，
                    // 绝不伪装成 PASS（"不许静默通过"纪律）。
                    std::printf("SEARCHCHECK: VERDICT=UNAVAILABLE\n");
                    std::fflush(stdout);
                    app.exit(6);
                    return;
                }
                std::printf("SEARCHCHECK: VERDICT=%s\n", result.pass ? "PASS" : "FAIL");
                std::fflush(stdout);
                app.exit(result.pass ? 0 : 10);
            });
        const int rc = app.exec();
        if (liveSkyRuntime) {
            liveSkyRuntime->stop();
            liveSkyRuntime.reset();
        }
        std::fflush(nullptr);
        _exit(backendOk ? rc : 3);
#endif
    }

    // ══ T33-A：地点数据面**探针**（STELQUICK_LOC_PROBE=1）════════════════════════
    // 为什么它排在产品代码之前：T24 的血泪 —— 合流 bundle 布局下"翻译从未加载"
    // 潜伏了 **14 个任务**才被逼出来，因为在它之前没有任何判据依赖翻译名。
    // 地点库是**同款风险**：它依赖 `data/base_locations.bin.gz`，而 installDir 由
    // `STELLARIUM_DATA_ROOT`（默认 "."）决定 ⇒ **依赖启动时的 cwd**。
    // ⇒ **数据面不合格就不写 UI**（探针 = 第一相位）。
    // 输出 `LOCPROBE:` 前缀，**只报读数、不下 PASS/FAIL**（断言在 T33-C）。
    if (locProbe) {
#if !defined(STELQUICK_HAS_ENGINE) || !defined(STELQUICK_WIDGETS_HOST)
        std::printf("LOCPROBE: VERDICT=UNAVAILABLE（需要合流形态构建）\n");
        std::fflush(stdout);
        return 6;
#else
        warmUpSceneGraph(window);
        liveSkyRuntime = std::make_unique<stelapp::LiveSkyRuntime>();
        QString producerError;
        if (!liveSkyRuntime->boot(&producerError)) {
            std::fprintf(stderr, "LOCPROBE: 引擎引导失败：%s\n",
                         producerError.toUtf8().constData());
            std::printf("LOCPROBE: VERDICT=UNAVAILABLE\n");
            std::fflush(stdout);
            return 6;
        }
        stelapp::LiveSkyRuntime::Config cfg =
            engineConfigFromEnv(0.1, kEngineNominalFps);
        if (!liveSkyRuntime->start(&frameMailbox, cfg, &producerError)) {
            std::fprintf(stderr, "LOCPROBE: 帧泵启动失败：%s\n",
                         producerError.toUtf8().constData());
            std::printf("LOCPROBE: VERDICT=UNAVAILABLE\n");
            std::fflush(stdout);
            return 6;
        }
        appFacade.attachSimControl(liveSkyRuntime.get());

        stelapp::LocationProbe::run(
            &app, &appFacade,
            [&app](const stelapp::LocationProbe::Result &result) {
                std::printf("LOCPROBE: %s\n", result.summary.toUtf8().constData());
                for (const QString &line : result.details)
                    std::printf("LOCPROBE: %s\n", line.toUtf8().constData());
                // 探针没有"通过/不通过"，只有"跑到了"与"环境不支持"。
                // 刻意不打 PASS —— 免得有人把它当判据。
                std::printf("LOCPROBE: VERDICT=%s\n",
                            result.unavailable ? "UNAVAILABLE" : "DONE");
                std::fflush(stdout);
                app.exit(result.unavailable ? 6 : 0);
            });
        const int rc = app.exec();
        if (liveSkyRuntime) {
            liveSkyRuntime->stop();
            liveSkyRuntime.reset();
        }
        std::fflush(nullptr);
        _exit(backendOk ? rc : 3);
#endif
    }

    // ══ T34-A：工具栏**数据面探针**（STELQUICK_TOOL_PROBE=1）════════════════════
    // 排在产品代码之前（同 LOCPROBE 的理由）：引擎 StelAction 注册表与
    // ActionRouter 的引擎透传路径是工具栏的**依赖面**，先证可用再写判据。
    // 输出 `TOOLBARPROBE:` 前缀，只报读数、不下 PASS/FAIL。
    if (toolProbe) {
#if !defined(STELQUICK_HAS_ENGINE) || !defined(STELQUICK_WIDGETS_HOST)
        std::printf("TOOLBARPROBE: VERDICT=UNAVAILABLE（需要合流形态构建）\n");
        std::fflush(stdout);
        return 6;
#else
        warmUpSceneGraph(window);
        liveSkyRuntime = std::make_unique<stelapp::LiveSkyRuntime>();
        QString producerError;
        if (!liveSkyRuntime->boot(&producerError)) {
            std::fprintf(stderr, "TOOLBARPROBE: 引擎引导失败：%s\n",
                         producerError.toUtf8().constData());
            std::printf("TOOLBARPROBE: VERDICT=UNAVAILABLE\n");
            std::fflush(stdout);
            return 6;
        }
        stelapp::LiveSkyRuntime::Config cfg =
            engineConfigFromEnv(0.1, kEngineNominalFps);
        if (!liveSkyRuntime->start(&frameMailbox, cfg, &producerError)) {
            std::fprintf(stderr, "TOOLBARPROBE: 帧泵启动失败：%s\n",
                         producerError.toUtf8().constData());
            std::printf("TOOLBARPROBE: VERDICT=UNAVAILABLE\n");
            std::fflush(stdout);
            return 6;
        }
        appFacade.attachSimControl(liveSkyRuntime.get());

        stelapp::ToolbarProbe::run(
            &app, &appFacade, &actionRouter,
            [&app](const stelapp::ToolbarProbe::Result &result) {
                std::printf("TOOLBARPROBE: %s\n", result.summary.toUtf8().constData());
                for (const QString &line : result.details)
                    std::printf("TOOLBARPROBE: %s\n", line.toUtf8().constData());
                // 探针没有"通过/不通过"，只有"跑到了"与"环境不支持"。
                std::printf("TOOLBARPROBE: VERDICT=%s\n",
                            result.unavailable ? "UNAVAILABLE" : "DONE");
                std::fflush(stdout);
                app.exit(result.unavailable ? 6 : 0);
            });
        const int rc = app.exec();
        if (liveSkyRuntime) {
            liveSkyRuntime->stop();
            liveSkyRuntime.reset();
        }
        std::fflush(nullptr);
        _exit(backendOk ? rc : 3);
#endif
    }

    // T34-C 工具栏自检（STELQUICK_TOOL_CHECK=1）：需要真实引擎（StelAction 注册
    // 表、透传路径、模块 getter 回读）。装配顺序与 LOC_CHECK 一致
    // （暖机 → boot → start → attach → 判据）。起始页已切到 "sky"。
    if (toolCheck) {
#if !defined(STELQUICK_HAS_ENGINE) || !defined(STELQUICK_WIDGETS_HOST)
        std::printf("TOOLBARCHECK: VERDICT=UNAVAILABLE（需要合流形态构建）\n");
        std::fflush(stdout);
        return 6;
#else
        warmUpSceneGraph(window);
        liveSkyRuntime = std::make_unique<stelapp::LiveSkyRuntime>();
        QString producerError;
        if (!liveSkyRuntime->boot(&producerError)) {
            std::fprintf(stderr, "TOOLBARCHECK: 引擎引导失败：%s\n",
                         producerError.toUtf8().constData());
            std::printf("TOOLBARCHECK: VERDICT=UNAVAILABLE\n");
            std::fflush(stdout);
            return 6;
        }
        stelapp::LiveSkyRuntime::Config cfg =
            engineConfigFromEnv(0.1, kEngineNominalFps);
        if (!liveSkyRuntime->start(&frameMailbox, cfg, &producerError)) {
            std::fprintf(stderr, "TOOLBARCHECK: 帧泵启动失败：%s\n",
                         producerError.toUtf8().constData());
            std::printf("TOOLBARCHECK: VERDICT=UNAVAILABLE\n");
            std::fflush(stdout);
            return 6;
        }
        appFacade.attachSimControl(liveSkyRuntime.get());

        stelapp::ToolbarCheck::run(
            &app, &appFacade, &actionRouter, window,
            [&app](const stelapp::ToolbarCheck::Result &result) {
                std::printf("TOOLBARCHECK: %s\n", result.summary.toUtf8().constData());
                for (const QString &line : result.details)
                    std::printf("TOOLBARCHECK: %s\n", line.toUtf8().constData());
                if (result.unavailable) {
                    std::printf("TOOLBARCHECK: VERDICT=UNAVAILABLE\n");
                    std::fflush(stdout);
                    app.exit(6);
                    return;
                }
                std::printf("TOOLBARCHECK: VERDICT=%s\n", result.pass ? "PASS" : "FAIL");
                std::fflush(stdout);
                app.exit(result.pass ? 0 : 10);
            });
        const int rc = app.exec();
        if (liveSkyRuntime) {
            liveSkyRuntime->stop();
            liveSkyRuntime.reset();
        }
        std::fflush(nullptr);
        _exit(backendOk ? rc : 3);
#endif
    }

    // T36-C 配置目录隔离自检（STELQUICK_CONFIG_CHECK=1）：验的是**引导序列的结果**
    // ——用户目录切没切、config.ini/log.txt 落哪、写侧会不会穿到原版 Stellarium
    // 目录去。装配只需 暖机 → boot（**不启帧泵**：本套不读渲染面，没有
    // "等一帧"的需求，也就没有就绪门与竞态）。
    if (cfgCheck) {
#if !defined(STELQUICK_HAS_ENGINE) || !defined(STELQUICK_WIDGETS_HOST)
        std::printf("CONFIGCHECK: VERDICT=UNAVAILABLE（需要合流形态构建）\n");
        std::fflush(stdout);
        return 6;
#else
        warmUpSceneGraph(window);
        liveSkyRuntime = std::make_unique<stelapp::LiveSkyRuntime>();
        QString cfgError;
        if (!liveSkyRuntime->boot(&cfgError)) {
            std::fprintf(stderr, "CONFIGCHECK: 引擎引导失败：%s\n",
                         cfgError.toUtf8().constData());
            std::printf("CONFIGCHECK: VERDICT=UNAVAILABLE\n");
            std::fflush(stdout);
            return 6;
        }
        stelapp::ConfigIsolationCheck::run(
            &app, &actionRouter,
            [&app](const stelapp::ConfigIsolationCheck::Result &result) {
                std::printf("CONFIGCHECK: %s\n", result.summary.toUtf8().constData());
                for (const QString &line : result.details)
                    std::printf("CONFIGCHECK: %s\n", line.toUtf8().constData());
                if (result.unavailable) {
                    std::printf("CONFIGCHECK: VERDICT=UNAVAILABLE\n");
                    std::fflush(stdout);
                    app.exit(6);
                    return;
                }
                std::printf("CONFIGCHECK: VERDICT=%s\n", result.pass ? "PASS" : "FAIL");
                std::fflush(stdout);
                app.exit(result.pass ? 0 : 10);
            });
        const int rc = app.exec();
        if (liveSkyRuntime) {
            liveSkyRuntime->stop();
            liveSkyRuntime.reset();
        }
        std::fflush(nullptr);
        _exit(backendOk ? rc : 3);
#endif
    }

    // T35-A 仿真时间链路数据面探针（STELQUICK_TIMELINK_PROBE=1）：T27 留档的
    // "帧泵推进与 getTimeRate() 脱钩"线索悬了六个任务，且决定所有时间类判据的
    // 口径是否可信。装配顺序与 TOOL_PROBE 一致（暖机 → boot → start → attach）。
    // 输出 `TIMELINKPROBE:` 前缀，只报读数、不下 PASS/FAIL。
    if (timeLinkProbe) {
#if !defined(STELQUICK_HAS_ENGINE) || !defined(STELQUICK_WIDGETS_HOST)
        std::printf("TIMELINKPROBE: VERDICT=UNAVAILABLE（需要合流形态构建）\n");
        std::fflush(stdout);
        return 6;
#else
        warmUpSceneGraph(window);
        liveSkyRuntime = std::make_unique<stelapp::LiveSkyRuntime>();
        QString producerError;
        if (!liveSkyRuntime->boot(&producerError)) {
            std::fprintf(stderr, "TIMELINKPROBE: 引擎引导失败：%s\n",
                         producerError.toUtf8().constData());
            std::printf("TIMELINKPROBE: VERDICT=UNAVAILABLE\n");
            std::fflush(stdout);
            return 6;
        }
        stelapp::LiveSkyRuntime::Config cfg =
            engineConfigFromEnv(0.1, kEngineNominalFps);
        if (!liveSkyRuntime->start(&frameMailbox, cfg, &producerError)) {
            std::fprintf(stderr, "TIMELINKPROBE: 帧泵启动失败：%s\n",
                         producerError.toUtf8().constData());
            std::printf("TIMELINKPROBE: VERDICT=UNAVAILABLE\n");
            std::fflush(stdout);
            return 6;
        }
        appFacade.attachSimControl(liveSkyRuntime.get());

        stelapp::TimeLinkProbe::run(
            &app, &appFacade,
            [&app](const stelapp::TimeLinkProbe::Result &result) {
                std::printf("TIMELINKPROBE: %s\n", result.summary.toUtf8().constData());
                for (const QString &line : result.details)
                    std::printf("TIMELINKPROBE: %s\n", line.toUtf8().constData());
                if (result.unavailable) {
                    std::printf("TIMELINKPROBE: VERDICT=UNAVAILABLE\n");
                    std::fflush(stdout);
                    app.exit(6);
                    return;
                }
                std::printf("TIMELINKPROBE: VERDICT=DONE\n");
                std::fflush(stdout);
                app.exit(0);
            });
        const int rc = app.exec();
        if (liveSkyRuntime) {
            liveSkyRuntime->stop();
            liveSkyRuntime.reset();
        }
        std::fflush(nullptr);
        _exit(backendOk ? rc : 3);
#endif
    }

    // T37-A 夜视链路数据面探针（STELQUICK_NIGHT_PROBE=1）：A-1.0 表里"天空夜视
    // 仍由旧后处理负责、避免叠加两次"这句话在合流形态从未验证过；夜视又是"设置页"
    // 第一个要闭环的项。装配顺序与 TIMELINK_PROBE 一致（暖机 → boot → start → attach）。
    // 输出 `NIGHTPROBE:` 前缀，只报读数、不下 PASS/FAIL。
    if (nightProbe) {
#if !defined(STELQUICK_HAS_ENGINE) || !defined(STELQUICK_WIDGETS_HOST)
        std::printf("NIGHTPROBE: VERDICT=UNAVAILABLE（需要合流形态构建）\n");
        std::fflush(stdout);
        return 6;
#else
        warmUpSceneGraph(window);
        liveSkyRuntime = std::make_unique<stelapp::LiveSkyRuntime>();
        QString producerError;
        if (!liveSkyRuntime->boot(&producerError)) {
            std::fprintf(stderr, "NIGHTPROBE: 引擎引导失败：%s\n",
                         producerError.toUtf8().constData());
            std::printf("NIGHTPROBE: VERDICT=UNAVAILABLE\n");
            std::fflush(stdout);
            return 6;
        }
        stelapp::LiveSkyRuntime::Config cfg =
            engineConfigFromEnv(0.1, kEngineNominalFps);
        if (!liveSkyRuntime->start(&frameMailbox, cfg, &producerError)) {
            std::fprintf(stderr, "NIGHTPROBE: 帧泵启动失败：%s\n",
                         producerError.toUtf8().constData());
            std::printf("NIGHTPROBE: VERDICT=UNAVAILABLE\n");
            std::fflush(stdout);
            return 6;
        }
        appFacade.attachSimControl(liveSkyRuntime.get());

        stelapp::NightModeProbe::run(
            &app, &appFacade, &actionRouter, window, &frameMailbox,
            [&app](const stelapp::NightModeProbe::Result &result) {
                std::printf("NIGHTPROBE: %s\n", result.summary.toUtf8().constData());
                for (const QString &line : result.details)
                    std::printf("NIGHTPROBE: %s\n", line.toUtf8().constData());
                if (result.unavailable) {
                    std::printf("NIGHTPROBE: VERDICT=UNAVAILABLE\n");
                    std::fflush(stdout);
                    app.exit(6);
                    return;
                }
                std::printf("NIGHTPROBE: VERDICT=DONE\n");
                std::fflush(stdout);
                app.exit(0);
            });
        const int rc = app.exec();
        if (liveSkyRuntime) {
            liveSkyRuntime->stop();
            liveSkyRuntime.reset();
        }
        std::fflush(nullptr);
        _exit(backendOk ? rc : 3);
#endif
    }

    // T38-A 显示参数命令面探针（STELQUICK_DISPLAY_PROBE=1）：A-1.0 表里"亮度/星等、
    // 视场、投影"三项（T37 只做掉了同一格的"主题/夜视"）在合流形态从未验证过。
    // 装配顺序与 NIGHT_PROBE 一致（暖机 → boot → start → attach）。
    // 输出 `DISPLAYPROBE:` 前缀，只报读数、不下 PASS/FAIL。
    if (displayProbe) {
#if !defined(STELQUICK_HAS_ENGINE) || !defined(STELQUICK_WIDGETS_HOST)
        std::printf("DISPLAYPROBE: VERDICT=UNAVAILABLE（需要合流形态构建）\n");
        std::fflush(stdout);
        return 6;
#else
        warmUpSceneGraph(window);
        liveSkyRuntime = std::make_unique<stelapp::LiveSkyRuntime>();
        QString producerError;
        if (!liveSkyRuntime->boot(&producerError)) {
            std::fprintf(stderr, "DISPLAYPROBE: 引擎引导失败：%s\n",
                         producerError.toUtf8().constData());
            std::printf("DISPLAYPROBE: VERDICT=UNAVAILABLE\n");
            std::fflush(stdout);
            return 6;
        }
        stelapp::LiveSkyRuntime::Config cfg =
            engineConfigFromEnv(0.1, kEngineNominalFps);
        if (!liveSkyRuntime->start(&frameMailbox, cfg, &producerError)) {
            std::fprintf(stderr, "DISPLAYPROBE: 帧泵启动失败：%s\n",
                         producerError.toUtf8().constData());
            std::printf("DISPLAYPROBE: VERDICT=UNAVAILABLE\n");
            std::fflush(stdout);
            return 6;
        }
        appFacade.attachSimControl(liveSkyRuntime.get());

        stelapp::DisplayProbe::run(
            &app, &appFacade,
            [&app](const stelapp::DisplayProbe::Result &result) {
                std::printf("DISPLAYPROBE: %s\n", result.summary.toUtf8().constData());
                for (const QString &line : result.details)
                    std::printf("DISPLAYPROBE: %s\n", line.toUtf8().constData());
                if (result.unavailable) {
                    std::printf("DISPLAYPROBE: VERDICT=UNAVAILABLE\n");
                    std::fflush(stdout);
                    app.exit(6);
                    return;
                }
                std::printf("DISPLAYPROBE: VERDICT=DONE\n");
                std::fflush(stdout);
                app.exit(0);
            });
        const int rc = app.exec();
        if (liveSkyRuntime) {
            liveSkyRuntime->stop();
            liveSkyRuntime.reset();
        }
        std::fflush(nullptr);
        _exit(backendOk ? rc : 3);
#endif
    }

    // T38-C 显示参数自检（STELQUICK_DISPLAY_CHECK=1）：判据 DP-00..DP-11。装配顺序同上。
    // 输出 `DISPLAYCHECK:` 前缀。负控：STELQUICK_DISPLAY_GATE_OFF=1（范围闸/白名单闸旁路）
    // ⇒ 预期恰红 [DP-02,DP-04]；STELQUICK_DISPLAY_FWD_OFF=1（断引擎→façade 转发）
    // ⇒ 预期恰红 [DP-07]。
    if (displayCheck) {
#if !defined(STELQUICK_HAS_ENGINE) || !defined(STELQUICK_WIDGETS_HOST)
        std::printf("DISPLAYCHECK: VERDICT=UNAVAILABLE（需要合流形态构建）\n");
        std::fflush(stdout);
        return 6;
#else
        warmUpSceneGraph(window);
        liveSkyRuntime = std::make_unique<stelapp::LiveSkyRuntime>();
        QString producerError;
        if (!liveSkyRuntime->boot(&producerError)) {
            std::fprintf(stderr, "DISPLAYCHECK: 引擎引导失败：%s\n",
                         producerError.toUtf8().constData());
            std::printf("DISPLAYCHECK: VERDICT=UNAVAILABLE\n");
            std::fflush(stdout);
            return 6;
        }
        stelapp::LiveSkyRuntime::Config cfg =
            engineConfigFromEnv(0.1, kEngineNominalFps);
        if (!liveSkyRuntime->start(&frameMailbox, cfg, &producerError)) {
            std::fprintf(stderr, "DISPLAYCHECK: 帧泵启动失败：%s\n",
                         producerError.toUtf8().constData());
            std::printf("DISPLAYCHECK: VERDICT=UNAVAILABLE\n");
            std::fflush(stdout);
            return 6;
        }
        appFacade.attachSimControl(liveSkyRuntime.get());

        stelapp::DisplayCheck::run(
            &app, &appFacade, &actionRouter, window, &frameMailbox,
            [&app](const stelapp::DisplayCheck::Result &result) {
                std::printf("DISPLAYCHECK: %s\n", result.summary.toUtf8().constData());
                for (const QString &line : result.details)
                    std::printf("DISPLAYCHECK: %s\n", line.toUtf8().constData());
                if (result.unavailable) {
                    std::printf("DISPLAYCHECK: VERDICT=UNAVAILABLE\n");
                    std::fflush(stdout);
                    app.exit(6);
                    return;
                }
                // 同 T37 口径：INCONCLUSIVE（竞态型读数/异常帧）不硬判 FAIL，rc=6。
                if (result.inconclusive) {
                    std::printf("DISPLAYCHECK: VERDICT=INCONCLUSIVE\n");
                    std::fflush(stdout);
                    app.exit(6);
                    return;
                }
                std::printf("DISPLAYCHECK: VERDICT=%s\n", result.pass ? "PASS" : "FAIL");
                std::fflush(stdout);
                app.exit(result.pass ? 0 : 10);
            });
        const int rc = app.exec();
        if (liveSkyRuntime) {
            liveSkyRuntime->stop();
            liveSkyRuntime.reset();
        }
        std::fflush(nullptr);
        _exit(backendOk ? rc : 3);
#endif
    }

    // T39-A 高 DPI + 渲染诊断数据面探针（STELQUICK_HIDPI_PROBE=1）：A-1.0 范围表
    // 最后一格"必须"里的"高 DPI"。装配顺序与 T38-A 一致（暖机 → boot → start → attach）。
    // 输出 `HIDPIPROBE:` 前缀，只报读数、不下 PASS/FAIL（断言在 T39-C 的 HiDpiCheck）。
    if (hiDpiProbe) {
#if !defined(STELQUICK_HAS_ENGINE) || !defined(STELQUICK_WIDGETS_HOST)
        std::printf("HIDPIPROBE: VERDICT=UNAVAILABLE（需要合流形态构建）\n");
        std::fflush(stdout);
        return 6;
#else
        warmUpSceneGraph(window);
        liveSkyRuntime = std::make_unique<stelapp::LiveSkyRuntime>();
        QString producerError;
        if (!liveSkyRuntime->boot(&producerError)) {
            std::fprintf(stderr, "HIDPIPROBE: 引擎引导失败：%s\n",
                         producerError.toUtf8().constData());
            std::printf("HIDPIPROBE: VERDICT=UNAVAILABLE\n");
            std::fflush(stdout);
            return 6;
        }
        stelapp::LiveSkyRuntime::Config cfg =
            engineConfigFromEnv(0.1, kEngineNominalFps);
        if (!liveSkyRuntime->start(&frameMailbox, cfg, &producerError)) {
            std::fprintf(stderr, "HIDPIPROBE: 帧泵启动失败：%s\n",
                         producerError.toUtf8().constData());
            std::printf("HIDPIPROBE: VERDICT=UNAVAILABLE\n");
            std::fflush(stdout);
            return 6;
        }
        appFacade.attachSimControl(liveSkyRuntime.get());

        stelapp::HiDpiProbe::run(
            &app, &appFacade, window, &frameMailbox,
            [&app](const stelapp::HiDpiProbe::Result &result) {
                std::printf("HIDPIPROBE: %s\n", result.summary.toUtf8().constData());
                for (const QString &line : result.details)
                    std::printf("HIDPIPROBE: %s\n", line.toUtf8().constData());
                if (result.unavailable) {
                    std::printf("HIDPIPROBE: VERDICT=UNAVAILABLE\n");
                    std::fflush(stdout);
                    app.exit(6);
                    return;
                }
                std::printf("HIDPIPROBE: VERDICT=DONE\n");
                std::fflush(stdout);
                app.exit(0);
            });
        const int rc = app.exec();
        if (liveSkyRuntime) {
            liveSkyRuntime->stop();
            liveSkyRuntime.reset();
        }
        std::fflush(nullptr);
        _exit(backendOk ? rc : 3);
#endif
    }

    // T39-C 高 DPI + 渲染诊断自检（STELQUICK_HIDPI_CHECK=1）：判据 HP-00..HP-11。
    // 装配顺序与 T38-C 一致（暖机 → boot → start → attach）。
    // 输出 `HIDPICHECK:` 前缀。负控复用 T38 的两个开关（GATE_OFF / FWD_OFF）。
    if (hiDpiCheck) {
#if !defined(STELQUICK_HAS_ENGINE) || !defined(STELQUICK_WIDGETS_HOST)
        std::printf("HIDPICHECK: VERDICT=UNAVAILABLE（需要合流形态构建）\n");
        std::fflush(stdout);
        return 6;
#else
        warmUpSceneGraph(window);
        liveSkyRuntime = std::make_unique<stelapp::LiveSkyRuntime>();
        QString producerError;
        if (!liveSkyRuntime->boot(&producerError)) {
            std::fprintf(stderr, "HIDPICHECK: 引擎引导失败：%s\n",
                         producerError.toUtf8().constData());
            std::printf("HIDPICHECK: VERDICT=UNAVAILABLE\n");
            std::fflush(stdout);
            return 6;
        }
        stelapp::LiveSkyRuntime::Config cfg =
            engineConfigFromEnv(0.1, kEngineNominalFps);
        if (!liveSkyRuntime->start(&frameMailbox, cfg, &producerError)) {
            std::fprintf(stderr, "HIDPICHECK: 帧泵启动失败：%s\n",
                         producerError.toUtf8().constData());
            std::printf("HIDPICHECK: VERDICT=UNAVAILABLE\n");
            std::fflush(stdout);
            return 6;
        }
        appFacade.attachSimControl(liveSkyRuntime.get());

        stelapp::HiDpiCheck::run(
            &app, &appFacade, window, &frameMailbox,
            [&app](const stelapp::HiDpiCheck::Result &result) {
                std::printf("HIDPICHECK: %s\n", result.summary.toUtf8().constData());
                for (const QString &line : result.details)
                    std::printf("HIDPICHECK: %s\n", line.toUtf8().constData());
                if (result.unavailable) {
                    std::printf("HIDPICHECK: VERDICT=UNAVAILABLE\n");
                    std::fflush(stdout);
                    app.exit(6);
                    return;
                }
                // 同 T37/T38 口径：INCONCLUSIVE（竞态型读数/异常帧）不硬判 FAIL，rc=6。
                if (result.inconclusive) {
                    std::printf("HIDPICHECK: VERDICT=INCONCLUSIVE\n");
                    std::fflush(stdout);
                    app.exit(6);
                    return;
                }
                std::printf("HIDPICHECK: VERDICT=%s\n", result.pass ? "PASS" : "FAIL");
                std::fflush(stdout);
                app.exit(result.pass ? 0 : 10);
            });
        const int rc = app.exec();
        if (liveSkyRuntime) {
            liveSkyRuntime->stop();
            liveSkyRuntime.reset();
        }
        std::fflush(nullptr);
        _exit(backendOk ? rc : 3);
#endif
    }

    // T40-A 快捷键编辑命令面探针（STELQUICK_SHORTCUT_PROBE=1）：A-1.0 范围表
    // 倒数第二格第一列「快捷键编辑」（第三列的约束已由 T29 兜住 ⇒ 本格交付页面本体）。
    // ⚠️ **刻意不启帧泵**：快捷键是纯命令面（QObject 注册表），与 GPU 无关 ——
    // 少一个 Metal 环节少一份掉设备的风险。boot 之后直接查注册表规模自证引导完整。
    // 输出 `SHORTCUTPROBE:` 前缀，只报读数、不下 PASS/FAIL（断言在 T40-C）。
    if (shortcutProbe) {
#if !defined(STELQUICK_HAS_ENGINE) || !defined(STELQUICK_WIDGETS_HOST)
        std::printf("SHORTCUTPROBE: VERDICT=UNAVAILABLE（需要合流形态构建）\n");
        std::fflush(stdout);
        return 6;
#else
        warmUpSceneGraph(window);
        liveSkyRuntime = std::make_unique<stelapp::LiveSkyRuntime>();
        QString producerError;
        if (!liveSkyRuntime->boot(&producerError)) {
            std::fprintf(stderr, "SHORTCUTPROBE: 引擎引导失败：%s\n",
                         producerError.toUtf8().constData());
            std::printf("SHORTCUTPROBE: VERDICT=UNAVAILABLE\n");
            std::fflush(stdout);
            return 6;
        }
        // 引导完整性自证：动作注册表非空才说明 StelApp::init() 跑到位。
        if (StelApp::getInstance().getStelActionManager()->getActionList().isEmpty()) {
            std::fprintf(stderr, "SHORTCUTPROBE: 动作注册表为空（boot 未完整引导）\n");
            std::printf("SHORTCUTPROBE: VERDICT=UNAVAILABLE\n");
            std::fflush(stdout);
            return 6;
        }

        stelapp::ShortcutProbe::run(
            &app, &actionRouter,
            [&app](const stelapp::ShortcutProbe::Result &result) {
                std::printf("SHORTCUTPROBE: %s\n", result.summary.toUtf8().constData());
                for (const QString &line : result.details)
                    std::printf("SHORTCUTPROBE: %s\n", line.toUtf8().constData());
                if (result.unavailable) {
                    std::printf("SHORTCUTPROBE: VERDICT=UNAVAILABLE\n");
                    std::fflush(stdout);
                    app.exit(6);
                    return;
                }
                std::printf("SHORTCUTPROBE: VERDICT=DONE\n");
                std::fflush(stdout);
                app.exit(0);
            });
        const int rc = app.exec();
        liveSkyRuntime.reset();
        std::fflush(nullptr);
        _exit(backendOk ? rc : 3);
#endif
    }

    // T41-A 帮助/版本/许可证数据面探针（STELQUICK_HELP_PROBE=1）：A-1.0 范围表
    // 倒数第二格按 T40 拆分后余下的三项。老宿主那份 `src/gui/HelpDialog` 是
    // QWidget 四标签对话框（Help=硬编码 HTML 键位表 / About=版本+版权+贡献者 /
    // Log / Config + 联网检查更新），**`src/gui/` 不在本形态构建里** ⇒ 本轮先探
    // 数据面：版本四件套与编译期宏、运行环境串、贡献者名单、🔴 COPYING 在运行时的
    // 可达性（源码树 / StelFileMgr 各目录 / app bundle / qrc）、🔴 帮助动作悬空裁决、
    // "Windows" 组之谜、帮助页两半数据源规模、翻译串可达性。
    // ⚠️ **只读、零写入、不启帧泵**（版本/许可证是纯数据面，与 GPU 无关）。
    // 输出 `HELPPROBE:` 前缀，只报读数、不下 PASS/FAIL（断言在 T41-C）。
    if (helpProbe) {
#if !defined(STELQUICK_HAS_ENGINE) || !defined(STELQUICK_WIDGETS_HOST)
        std::printf("HELPPROBE: VERDICT=UNAVAILABLE（需要合流形态构建）\n");
        std::fflush(stdout);
        return 6;
#else
        warmUpSceneGraph(window);
        liveSkyRuntime = std::make_unique<stelapp::LiveSkyRuntime>();
        QString producerError;
        if (!liveSkyRuntime->boot(&producerError)) {
            std::fprintf(stderr, "HELPPROBE: 引擎引导失败：%s\n",
                         producerError.toUtf8().constData());
            std::printf("HELPPROBE: VERDICT=UNAVAILABLE\n");
            std::fflush(stdout);
            return 6;
        }
        // 引导完整性自证：动作注册表非空才说明 StelApp::init() 跑到位（S6/S7/S9 依赖它）。
        if (StelApp::getInstance().getStelActionManager()->getActionList().isEmpty()) {
            std::fprintf(stderr, "HELPPROBE: 动作注册表为空（boot 未完整引导）\n");
            std::printf("HELPPROBE: VERDICT=UNAVAILABLE\n");
            std::fflush(stdout);
            return 6;
        }

        stelapp::HelpProbe::run(
            &app,
            [&app](const stelapp::HelpProbe::Result &result) {
                std::printf("HELPPROBE: %s\n", result.summary.toUtf8().constData());
                for (const QString &line : result.details)
                    std::printf("HELPPROBE: %s\n", line.toUtf8().constData());
                if (result.unavailable) {
                    std::printf("HELPPROBE: VERDICT=UNAVAILABLE\n");
                    std::fflush(stdout);
                    app.exit(6);
                    return;
                }
                std::printf("HELPPROBE: VERDICT=DONE\n");
                std::fflush(stdout);
                app.exit(0);
            });
        const int rc = app.exec();
        liveSkyRuntime.reset();
        std::fflush(nullptr);
        _exit(backendOk ? rc : 3);
#endif
    }

    // T40-C 快捷键编辑自检（STELQUICK_SHORTCUT_CHECK=1）：判据 SC-01..SC-14。
    // 装配与 T39-C 一致（暖机 → boot → start）—— SC-12/13 要真实点击，窗口必须
    // 完整渲染。输出 `SHORTCUTCHECK:` 前缀；负控 WRITE_OFF / SAVE_OFF（见上方注释）。
    if (shortcutCheck) {
#if !defined(STELQUICK_HAS_ENGINE) || !defined(STELQUICK_WIDGETS_HOST)
        std::printf("SHORTCUTCHECK: VERDICT=UNAVAILABLE（需要合流形态构建）\n");
        std::fflush(stdout);
        return 6;
#else
        warmUpSceneGraph(window);
        liveSkyRuntime = std::make_unique<stelapp::LiveSkyRuntime>();
        QString producerError;
        if (!liveSkyRuntime->boot(&producerError)) {
            std::fprintf(stderr, "SHORTCUTCHECK: 引擎引导失败：%s\n",
                         producerError.toUtf8().constData());
            std::printf("SHORTCUTCHECK: VERDICT=UNAVAILABLE\n");
            std::fflush(stdout);
            return 6;
        }
        stelapp::LiveSkyRuntime::Config cfg =
            engineConfigFromEnv(0.1, kEngineNominalFps);
        if (!liveSkyRuntime->start(&frameMailbox, cfg, &producerError)) {
            std::fprintf(stderr, "SHORTCUTCHECK: 帧泵启动失败：%s\n",
                         producerError.toUtf8().constData());
            std::printf("SHORTCUTCHECK: VERDICT=UNAVAILABLE\n");
            std::fflush(stdout);
            return 6;
        }
        // 🔴 必须给 **QML 绑定的那个** model 喂数据（正常路径末尾那行 `refresh()` 在
        // 自检块里走不到）。漏了的形态很隐蔽：判据自建的实例有 505 行（SC-01 照样绿），
        // 但 ListView 的 model 是空表 ⇒ **一个 delegate 都不创建** ⇒ SC-12/13 报
        // "按钮缺失"。首轮就栽在这（T40-C 第一轮 SC-12 红）。
        shortcutModel.refresh();

        stelapp::ShortcutCheck::run(
            &app, &appFacade, window, &frameMailbox,
            [&app](const stelapp::ShortcutCheck::Result &result) {
                std::printf("SHORTCUTCHECK: %s\n", result.summary.toUtf8().constData());
                for (const QString &line : result.details)
                    std::printf("SHORTCUTCHECK: %s\n", line.toUtf8().constData());
                if (result.unavailable) {
                    std::printf("SHORTCUTCHECK: VERDICT=UNAVAILABLE\n");
                    std::fflush(stdout);
                    app.exit(6);
                    return;
                }
                if (result.inconclusive) {
                    std::printf("SHORTCUTCHECK: VERDICT=INCONCLUSIVE\n");
                    std::fflush(stdout);
                    app.exit(6);
                    return;
                }
                std::printf("SHORTCUTCHECK: VERDICT=%s\n", result.pass ? "PASS" : "FAIL");
                std::fflush(stdout);
                app.exit(result.pass ? 0 : 10);
            });
        const int rc = app.exec();
        if (liveSkyRuntime) {
            liveSkyRuntime->stop();
            liveSkyRuntime.reset();
        }
        std::fflush(nullptr);
        _exit(backendOk ? rc : 3);
#endif
    }

    // T41-C 帮助/版本/许可证自检（STELQUICK_HELP_CHECK=1）：判据 HC-01..HC-17。
    // 装配与 T40-C 一致（暖机 → boot → start）—— HC-09..13 要真实点击，窗口必须
    // 完整渲染。输出 `HELPCHECK:` 前缀；负控 TAKEOVER_OFF（main.cpp 接管注册段）/
    // LICENSE_OFF（HelpModel::loadLicense）。
    if (helpCheck) {
#if !defined(STELQUICK_HAS_ENGINE) || !defined(STELQUICK_WIDGETS_HOST)
        std::printf("HELPCHECK: VERDICT=UNAVAILABLE（需要合流形态构建）\n");
        std::fflush(stdout);
        return 6;
#else
        warmUpSceneGraph(window);
        liveSkyRuntime = std::make_unique<stelapp::LiveSkyRuntime>();
        QString producerError;
        if (!liveSkyRuntime->boot(&producerError)) {
            std::fprintf(stderr, "HELPCHECK: 引擎引导失败：%s\n",
                         producerError.toUtf8().constData());
            std::printf("HELPCHECK: VERDICT=UNAVAILABLE\n");
            std::fflush(stdout);
            return 6;
        }
        stelapp::LiveSkyRuntime::Config cfg =
            engineConfigFromEnv(0.1, kEngineNominalFps);
        if (!liveSkyRuntime->start(&frameMailbox, cfg, &producerError)) {
            std::fprintf(stderr, "HELPCHECK: 帧泵启动失败：%s\n",
                         producerError.toUtf8().constData());
            std::printf("HELPCHECK: VERDICT=UNAVAILABLE\n");
            std::fflush(stdout);
            return 6;
        }
        // 🔴 给 **QML 绑定的那些实例**喂数据（陷阱 84；正常路径末尾的 refresh() 在
        // 自检块里走不到）。HelpModel：版本/系统行；ShortcutModel：HelpPage 的
        // engineActionCount 绑着它的 totalCount，不喂数摘要就显示 0。
        helpModel.refresh();
        shortcutModel.refresh();

        stelapp::HelpCheck::run(
            &app, window, &actionRouter, &helpModel,
            [&app](const stelapp::HelpCheck::Result &result) {
                std::printf("HELPCHECK: %s\n", result.summary.toUtf8().constData());
                for (const QString &line : result.details)
                    std::printf("HELPCHECK: %s\n", line.toUtf8().constData());
                if (result.unavailable) {
                    std::printf("HELPCHECK: VERDICT=UNAVAILABLE\n");
                    std::fflush(stdout);
                    app.exit(6);
                    return;
                }
                if (result.inconclusive) {
                    std::printf("HELPCHECK: VERDICT=INCONCLUSIVE\n");
                    std::fflush(stdout);
                    app.exit(6);
                    return;
                }
                std::printf("HELPCHECK: VERDICT=%s\n", result.pass ? "PASS" : "FAIL");
                std::fflush(stdout);
                app.exit(result.pass ? 0 : 10);
            });
        const int rc = app.exec();
        if (liveSkyRuntime) {
            liveSkyRuntime->stop();
            liveSkyRuntime.reset();
        }
        std::fflush(nullptr);
        _exit(backendOk ? rc : 3);
#endif
    }

    // T37-C 夜视自检（STELQUICK_NIGHT_CHECK=1）：判据 NC-01..NC-05。装配顺序同上。
    // 输出 `NIGHTCHECK:` 前缀。负控 STELQUICK_NIGHT_EFFECT_OFF=1 ⇒ NC-03② 必红。
    if (nightCheck) {
#if !defined(STELQUICK_HAS_ENGINE) || !defined(STELQUICK_WIDGETS_HOST)
        std::printf("NIGHTCHECK: VERDICT=UNAVAILABLE（需要合流形态构建）\n");
        std::fflush(stdout);
        return 6;
#else
        warmUpSceneGraph(window);
        liveSkyRuntime = std::make_unique<stelapp::LiveSkyRuntime>();
        QString producerError;
        if (!liveSkyRuntime->boot(&producerError)) {
            std::fprintf(stderr, "NIGHTCHECK: 引擎引导失败：%s\n",
                         producerError.toUtf8().constData());
            std::printf("NIGHTCHECK: VERDICT=UNAVAILABLE\n");
            std::fflush(stdout);
            return 6;
        }
        stelapp::LiveSkyRuntime::Config cfg =
            engineConfigFromEnv(0.1, kEngineNominalFps);
        if (!liveSkyRuntime->start(&frameMailbox, cfg, &producerError)) {
            std::fprintf(stderr, "NIGHTCHECK: 帧泵启动失败：%s\n",
                         producerError.toUtf8().constData());
            std::printf("NIGHTCHECK: VERDICT=UNAVAILABLE\n");
            std::fflush(stdout);
            return 6;
        }
        appFacade.attachSimControl(liveSkyRuntime.get());

        stelapp::NightModeCheck::run(
            &app, &appFacade, &actionRouter, window, &frameMailbox,
            [&app](const stelapp::NightModeCheck::Result &result) {
                std::printf("NIGHTCHECK: %s\n", result.summary.toUtf8().constData());
                for (const QString &line : result.details)
                    std::printf("NIGHTCHECK: %s\n", line.toUtf8().constData());
                if (result.unavailable) {
                    std::printf("NIGHTCHECK: VERDICT=UNAVAILABLE\n");
                    std::fflush(stdout);
                    app.exit(6);
                    return;
                }
                // T37 口径：INCONCLUSIVE（竞态型读数，如 NC-05 抓到撕裂帧）
                // 不许硬判 FAIL——与 INTERACTCHECK 的环境门同款，rc=6 让
                // 跑批脚本走 ENV-SKIP（不洗成 PASS，也不计失败）。
                if (result.inconclusive) {
                    std::printf("NIGHTCHECK: VERDICT=INCONCLUSIVE\n");
                    std::fflush(stdout);
                    app.exit(6);
                    return;
                }
                std::printf("NIGHTCHECK: VERDICT=%s\n", result.pass ? "PASS" : "FAIL");
                std::fflush(stdout);
                app.exit(result.pass ? 0 : 10);
            });
        const int rc = app.exec();
        if (liveSkyRuntime) {
            liveSkyRuntime->stop();
            liveSkyRuntime.reset();
        }
        std::fflush(nullptr);
        _exit(backendOk ? rc : 3);
#endif
    }

    // T35-C 仿真时间链路自检（STELQUICK_TIMELINK_CHECK=1）：判据 TL-01..TL-07。
    // 装配顺序同上。起始页已切到 "sky"。
    if (timeLinkCheck) {
#if !defined(STELQUICK_HAS_ENGINE) || !defined(STELQUICK_WIDGETS_HOST)
        std::printf("TIMELINKCHECK: VERDICT=UNAVAILABLE（需要合流形态构建）\n");
        std::fflush(stdout);
        return 6;
#else
        warmUpSceneGraph(window);
        liveSkyRuntime = std::make_unique<stelapp::LiveSkyRuntime>();
        QString producerError;
        if (!liveSkyRuntime->boot(&producerError)) {
            std::fprintf(stderr, "TIMELINKCHECK: 引擎引导失败：%s\n",
                         producerError.toUtf8().constData());
            std::printf("TIMELINKCHECK: VERDICT=UNAVAILABLE\n");
            std::fflush(stdout);
            return 6;
        }
        stelapp::LiveSkyRuntime::Config cfg =
            engineConfigFromEnv(0.1, kEngineNominalFps);
        if (!liveSkyRuntime->start(&frameMailbox, cfg, &producerError)) {
            std::fprintf(stderr, "TIMELINKCHECK: 帧泵启动失败：%s\n",
                         producerError.toUtf8().constData());
            std::printf("TIMELINKCHECK: VERDICT=UNAVAILABLE\n");
            std::fflush(stdout);
            return 6;
        }
        appFacade.attachSimControl(liveSkyRuntime.get());

        stelapp::TimeLinkCheck::run(
            &app, &appFacade,
            [&app](const stelapp::TimeLinkCheck::Result &result) {
                std::printf("TIMELINKCHECK: %s\n", result.summary.toUtf8().constData());
                for (const QString &line : result.details)
                    std::printf("TIMELINKCHECK: %s\n", line.toUtf8().constData());
                if (result.unavailable) {
                    std::printf("TIMELINKCHECK: VERDICT=UNAVAILABLE\n");
                    std::fflush(stdout);
                    app.exit(6);
                    return;
                }
                std::printf("TIMELINKCHECK: VERDICT=%s\n", result.pass ? "PASS" : "FAIL");
                std::fflush(stdout);
                app.exit(result.pass ? 0 : 10);
            });
        const int rc = app.exec();
        if (liveSkyRuntime) {
            liveSkyRuntime->stop();
            liveSkyRuntime.reset();
        }
        std::fflush(nullptr);
        _exit(backendOk ? rc : 3);
#endif
    }

    // T33-C 地点写入面自检（STELQUICK_LOC_CHECK=1）：需要真实引擎（地点库、UTCOffset、
    // Polaris 的 J2000 方向）。装配顺序与 LOCATE_CHECK 一致
    // （暖机 → boot → start → attach → 判据）。起始页已切到 "location"。
    if (locCheck) {
#if !defined(STELQUICK_HAS_ENGINE) || !defined(STELQUICK_WIDGETS_HOST)
        std::printf("LOCATIONCHECK: VERDICT=UNAVAILABLE（需要合流形态构建）\n");
        std::fflush(stdout);
        return 6;
#else
        warmUpSceneGraph(window);
        liveSkyRuntime = std::make_unique<stelapp::LiveSkyRuntime>();
        QString producerError;
        if (!liveSkyRuntime->boot(&producerError)) {
            std::fprintf(stderr, "LOCATIONCHECK: 引擎引导失败：%s\n",
                         producerError.toUtf8().constData());
            std::printf("LOCATIONCHECK: VERDICT=UNAVAILABLE\n");
            std::fflush(stdout);
            return 6;
        }
        stelapp::LiveSkyRuntime::Config cfg =
            engineConfigFromEnv(0.1, kEngineNominalFps);
        if (!liveSkyRuntime->start(&frameMailbox, cfg, &producerError)) {
            std::fprintf(stderr, "LOCATIONCHECK: 帧泵启动失败：%s\n",
                         producerError.toUtf8().constData());
            std::printf("LOCATIONCHECK: VERDICT=UNAVAILABLE\n");
            std::fflush(stdout);
            return 6;
        }
        appFacade.attachSimControl(liveSkyRuntime.get());

        stelapp::LocationCheck::run(
            &app, &appFacade,
            [&app](const stelapp::LocationCheck::Result &result) {
                std::printf("LOCATIONCHECK: %s\n", result.summary.toUtf8().constData());
                for (const QString &line : result.details)
                    std::printf("LOCATIONCHECK: %s\n", line.toUtf8().constData());
                std::printf("LOCATIONCHECK: 判据 %d/%d\n", result.passed, result.total);
                if (!result.ran || result.unavailable) {
                    // 环境条件，不是逻辑缺陷——照实报 UNAVAILABLE，绝不伪装成 PASS。
                    std::printf("LOCATIONCHECK: VERDICT=UNAVAILABLE\n");
                    std::fflush(stdout);
                    app.exit(6);
                    return;
                }
                std::printf("LOCATIONCHECK: VERDICT=%s\n", result.pass ? "PASS" : "FAIL");
                std::fflush(stdout);
                app.exit(result.pass ? 0 : 10);
            });
        const int rc = app.exec();
        if (liveSkyRuntime) {
            liveSkyRuntime->stop();
            liveSkyRuntime.reset();
        }
        std::fflush(nullptr);
        _exit(backendOk ? rc : 3);
#endif
    }

    // T18 定位/跟踪自检（STELQUICK_LOCATE_CHECK=1）：需要真实引擎（要真能搜到天体、
    // 真跑帧泵、真推进时钟）。装配顺序与 SEARCH_CHECK 完全一致
    // （暖机 → boot → start → attach → 判据），判据本体见 LocateCheck。
    // 起始页已切到 "search"，故同时验证搜索页 QML 可被实例化。
    if (locateCheck) {
#if !defined(STELQUICK_HAS_ENGINE) || !defined(STELQUICK_WIDGETS_HOST)
        std::printf("LOCATECHECK: VERDICT=UNAVAILABLE（需要合流形态构建）\n");
        std::fflush(stdout);
        return 6;
#else
        warmUpSceneGraph(window);
        liveSkyRuntime = std::make_unique<stelapp::LiveSkyRuntime>();
        QString producerError;
        if (!liveSkyRuntime->boot(&producerError)) {
            std::fprintf(stderr, "LOCATECHECK: 引擎引导失败：%s\n",
                         producerError.toUtf8().constData());
            std::printf("LOCATECHECK: VERDICT=UNAVAILABLE\n");
            std::fflush(stdout);
            return 6;
        }
        stelapp::LiveSkyRuntime::Config cfg =
            engineConfigFromEnv(0.1, kEngineNominalFps);
        if (!liveSkyRuntime->start(&frameMailbox, cfg, &producerError)) {
            std::fprintf(stderr, "LOCATECHECK: 帧泵启动失败：%s\n",
                         producerError.toUtf8().constData());
            std::printf("LOCATECHECK: VERDICT=UNAVAILABLE\n");
            std::fflush(stdout);
            return 6;
        }
        appFacade.attachSimControl(liveSkyRuntime.get());

        stelapp::LocateCheck::run(
            &app, &appFacade,
            [&app](const stelapp::LocateCheck::Result &result) {
                std::printf("LOCATECHECK: %s\n", result.summary.toUtf8().constData());
                for (const QString &line : result.details)
                    std::printf("LOCATECHECK: %s\n", line.toUtf8().constData());
                std::printf("LOCATECHECK: 判据 %d/%d\n", result.passed, result.total);
                if (!result.ran || result.unavailable) {
                    // 环境条件，不是逻辑缺陷——照实报 UNAVAILABLE，绝不伪装成 PASS。
                    std::printf("LOCATECHECK: VERDICT=UNAVAILABLE\n");
                    std::fflush(stdout);
                    app.exit(6);
                    return;
                }
                std::printf("LOCATECHECK: VERDICT=%s\n", result.pass ? "PASS" : "FAIL");
                std::fflush(stdout);
                app.exit(result.pass ? 0 : 10);
            });
        const int rc = app.exec();
        if (liveSkyRuntime) {
            liveSkyRuntime->stop();
            liveSkyRuntime.reset();
        }
        std::fflush(nullptr);
        _exit(backendOk ? rc : 3);
#endif
    }

    // T19 改时间自检（STELQUICK_TIME_CHECK=1）：需要真实引擎（要真能写时钟、
    // 真跑帧泵、真量"星空随时刻变化"）。装配顺序与 LOCATE_CHECK 完全一致，
    // 判据本体见 TimeCheck。起始页切到 "time"，故同时验证时间页 QML 可被实例化。
    if (timeCheck) {
#if !defined(STELQUICK_HAS_ENGINE) || !defined(STELQUICK_WIDGETS_HOST)
        std::printf("TIMECHECK: VERDICT=UNAVAILABLE（需要合流形态构建）\n");
        std::fflush(stdout);
        return 6;
#else
        warmUpSceneGraph(window);
        liveSkyRuntime = std::make_unique<stelapp::LiveSkyRuntime>();
        QString producerError;
        if (!liveSkyRuntime->boot(&producerError)) {
            std::fprintf(stderr, "TIMECHECK: 引擎引导失败：%s\n",
                         producerError.toUtf8().constData());
            std::printf("TIMECHECK: VERDICT=UNAVAILABLE\n");
            std::fflush(stdout);
            return 6;
        }
        stelapp::LiveSkyRuntime::Config cfg =
            engineConfigFromEnv(0.1, kEngineNominalFps);
        if (!liveSkyRuntime->start(&frameMailbox, cfg, &producerError)) {
            std::fprintf(stderr, "TIMECHECK: 帧泵启动失败：%s\n",
                         producerError.toUtf8().constData());
            std::printf("TIMECHECK: VERDICT=UNAVAILABLE\n");
            std::fflush(stdout);
            return 6;
        }
        appFacade.attachSimControl(liveSkyRuntime.get());

        stelapp::TimeCheck::run(
            &app, &appFacade,
            [&app](const stelapp::TimeCheck::Result &result) {
                std::printf("TIMECHECK: %s\n", result.summary.toUtf8().constData());
                for (const QString &line : result.details)
                    std::printf("TIMECHECK: %s\n", line.toUtf8().constData());
                std::printf("TIMECHECK: 判据 %d/%d\n", result.passed, result.total);
                if (!result.ran || result.unavailable) {
                    // 环境条件，不是逻辑缺陷——照实报 UNAVAILABLE，绝不伪装成 PASS。
                    std::printf("TIMECHECK: VERDICT=UNAVAILABLE\n");
                    std::fflush(stdout);
                    app.exit(6);
                    return;
                }
                std::printf("TIMECHECK: VERDICT=%s\n", result.pass ? "PASS" : "FAIL");
                std::fflush(stdout);
                app.exit(result.pass ? 0 : 10);
            });
        const int rc = app.exec();
        if (liveSkyRuntime) {
            liveSkyRuntime->stop();
            liveSkyRuntime.reset();
        }
        std::fflush(nullptr);
        _exit(backendOk ? rc : 3);
#endif
    }

    // T19 时间页 UI 层端到端自检（STELQUICK_TIME_UI_CHECK=1）：**从最外层注入**。
    // 与 TIMECHECK 的关键区别：TIMECHECK 走 AppFacade 的 C++ 公共 API，证明不了
    // TimePage.qml 的 onClicked 接线与状态行绑定是活的；本检查按 objectName 找
    // 真实控件、向窗口投递真实鼠标事件，并反向读 QML 控件的真实属性
    // （SpinBox.value / Label.text）。判据见下方 uiTime*，共 27 条
    // （T22 追加 6 条数据面；T30 追加 8 条**视觉层**）。
    // 负控：`STELQUICK_TIME_VISUAL_NEGCTL=1` 会故意破坏两个视觉事实 ⇒ 期望
    // UI-17/UI-20/UI-23 判红（扰动观测面，不改产品代码）。
    if (timeUiCheck) {
#if !defined(STELQUICK_HAS_ENGINE) || !defined(STELQUICK_WIDGETS_HOST)
        std::printf("TIMEUICHECK: VERDICT=UNAVAILABLE（需要合流形态构建）\n");
        std::fflush(stdout);
        return 6;
#else
        warmUpSceneGraph(window);
        liveSkyRuntime = std::make_unique<stelapp::LiveSkyRuntime>();
        QString producerError;
        if (!liveSkyRuntime->boot(&producerError)) {
            std::fprintf(stderr, "TIMEUICHECK: 引擎引导失败：%s\n",
                         producerError.toUtf8().constData());
            std::printf("TIMEUICHECK: VERDICT=UNAVAILABLE\n");
            std::fflush(stdout);
            return 6;
        }
        stelapp::LiveSkyRuntime::Config cfg =
            engineConfigFromEnv(0.1, kEngineNominalFps);
        if (!liveSkyRuntime->start(&frameMailbox, cfg, &producerError)) {
            std::fprintf(stderr, "TIMEUICHECK: 帧泵启动失败：%s\n",
                         producerError.toUtf8().constData());
            std::printf("TIMEUICHECK: VERDICT=UNAVAILABLE\n");
            std::fflush(stdout);
            return 6;
        }
        appFacade.attachSimControl(liveSkyRuntime.get());
        runUiTimeCheck(&app, window, &appFacade);
        const int rc = app.exec();
        if (liveSkyRuntime) {
            liveSkyRuntime->stop();
            liveSkyRuntime.reset();
        }
        std::fflush(nullptr);
        _exit(backendOk ? rc : 3);
#endif
    }

    // T20 返回环 UI 层端到端自检（STELQUICK_RETURN_UI_CHECK=1）：**从最外层注入**。
    // 装配与 T18/T19 的 UI 检查完全一致（暖机 → boot → start → attach → 判据），
    // 因为"返回"要保住的那三样东西（时间、选中、跟踪）都得先在真实引擎里成立。
    if (returnUiCheck) {
#if !defined(STELQUICK_HAS_ENGINE) || !defined(STELQUICK_WIDGETS_HOST)
        std::printf("RETURNUICHECK: VERDICT=UNAVAILABLE（需要合流形态构建）\n");
        std::fflush(stdout);
        return 6;
#else
        warmUpSceneGraph(window);
        liveSkyRuntime = std::make_unique<stelapp::LiveSkyRuntime>();
        QString producerError;
        if (!liveSkyRuntime->boot(&producerError)) {
            std::fprintf(stderr, "RETURNUICHECK: 引擎引导失败：%s\n",
                         producerError.toUtf8().constData());
            std::printf("RETURNUICHECK: VERDICT=UNAVAILABLE\n");
            std::fflush(stdout);
            return 6;
        }
        stelapp::LiveSkyRuntime::Config cfg =
            engineConfigFromEnv(0.1, kEngineNominalFps);
        if (!liveSkyRuntime->start(&frameMailbox, cfg, &producerError)) {
            std::fprintf(stderr, "RETURNUICHECK: 帧泵启动失败：%s\n",
                         producerError.toUtf8().constData());
            std::printf("RETURNUICHECK: VERDICT=UNAVAILABLE\n");
            std::fflush(stdout);
            return 6;
        }
        appFacade.attachSimControl(liveSkyRuntime.get());
        runUiReturnCheck(&app, window, &appFacade, &actionRouter);
        const int rc = app.exec();
        if (liveSkyRuntime) {
            liveSkyRuntime->stop();
            liveSkyRuntime.reset();
        }
        std::fflush(nullptr);
        _exit(backendOk ? rc : 3);
#endif
    }

    // T25 交互级自检（STELQUICK_INTERACT_UI_CHECK=1）：滚轮/键盘的最外层注入。
    // 装配与 returnUiCheck 同构；额外传 &actionRouter —— INTERACTCHECK 要订阅
    // dispatched 信号当"routeKey 真的把动作发给引擎"的观测点。
    if (interactUiCheck) {
#if !defined(STELQUICK_HAS_ENGINE) || !defined(STELQUICK_WIDGETS_HOST)
        std::printf("INTERACTCHECK: VERDICT=UNAVAILABLE（需要合流形态构建）\n");
        std::fflush(stdout);
        return 6;
#else
        warmUpSceneGraph(window);
        liveSkyRuntime = std::make_unique<stelapp::LiveSkyRuntime>();
        QString producerError;
        if (!liveSkyRuntime->boot(&producerError)) {
            std::fprintf(stderr, "INTERACTCHECK: 引擎引导失败：%s\n",
                         producerError.toUtf8().constData());
            std::printf("INTERACTCHECK: VERDICT=UNAVAILABLE\n");
            std::fflush(stdout);
            return 6;
        }
        stelapp::LiveSkyRuntime::Config cfg =
            engineConfigFromEnv(0.1, kEngineNominalFps);
        if (!liveSkyRuntime->start(&frameMailbox, cfg, &producerError)) {
            std::fprintf(stderr, "INTERACTCHECK: 帧泵启动失败：%s\n",
                         producerError.toUtf8().constData());
            std::printf("INTERACTCHECK: VERDICT=UNAVAILABLE\n");
            std::fflush(stdout);
            return 6;
        }
        appFacade.attachSimControl(liveSkyRuntime.get());
        runUiInteractCheck(&app, window, &appFacade, &actionRouter);
        const int rc = app.exec();
        if (liveSkyRuntime) {
            liveSkyRuntime->stop();
            liveSkyRuntime.reset();
        }
        std::fflush(nullptr);
        _exit(backendOk ? rc : 3);
#endif
    }

    // T20 I-REP-02 全流程回放（STELQUICK_REPLAY_CHECK=1）：A-alpha 的出口测试。
    if (replayCheck) {
#if !defined(STELQUICK_HAS_ENGINE) || !defined(STELQUICK_WIDGETS_HOST)
        std::printf("REPLAYCHECK: VERDICT=UNAVAILABLE（需要合流形态构建）\n");
        std::fflush(stdout);
        return 6;
#else
        warmUpSceneGraph(window);
        liveSkyRuntime = std::make_unique<stelapp::LiveSkyRuntime>();
        QString producerError;
        if (!liveSkyRuntime->boot(&producerError)) {
            std::fprintf(stderr, "REPLAYCHECK: 引擎引导失败：%s\n",
                         producerError.toUtf8().constData());
            std::printf("REPLAYCHECK: VERDICT=UNAVAILABLE\n");
            std::fflush(stdout);
            return 6;
        }
        stelapp::LiveSkyRuntime::Config cfg =
            engineConfigFromEnv(0.1, kEngineNominalFps);
        if (!liveSkyRuntime->start(&frameMailbox, cfg, &producerError)) {
            std::fprintf(stderr, "REPLAYCHECK: 帧泵启动失败：%s\n",
                         producerError.toUtf8().constData());
            std::printf("REPLAYCHECK: VERDICT=UNAVAILABLE\n");
            std::fflush(stdout);
            return 6;
        }
        appFacade.attachSimControl(liveSkyRuntime.get());
        runUiReplayCheck(&app, window, &appFacade);
        const int rc = app.exec();
        if (liveSkyRuntime) {
            liveSkyRuntime->stop();
            liveSkyRuntime.reset();
        }
        std::fflush(nullptr);
        _exit(backendOk ? rc : 3);
#endif
    }

    // T18 UI 层端到端自检（STELQUICK_UI_CHECK=1）：**从最外层注入**。
    // 与 LOCATE_CHECK 的关键区别：LOCATE_CHECK 全程走 AppFacade 的 C++ 公共 API，
    // 证明不了 QML 的 onClicked 接线是活的；本检查按 objectName 找真实控件、
    // 向窗口投递真实鼠标事件，断言引擎状态真的因此改变（判据见下方 uiLocate*）。
    // 装配顺序与 LOCATE_CHECK 一致（暖机 → boot → start → attach → 判据），
    // 因为定位要真能搜到天体、真改引擎状态。
    if (uiCheck) {
#if !defined(STELQUICK_HAS_ENGINE) || !defined(STELQUICK_WIDGETS_HOST)
        std::printf("UICHECK: VERDICT=UNAVAILABLE（需要合流形态构建）\n");
        std::fflush(stdout);
        return 6;
#else
        warmUpSceneGraph(window);
        liveSkyRuntime = std::make_unique<stelapp::LiveSkyRuntime>();
        QString producerError;
        if (!liveSkyRuntime->boot(&producerError)) {
            std::fprintf(stderr, "UICHECK: 引擎引导失败：%s\n",
                         producerError.toUtf8().constData());
            std::printf("UICHECK: VERDICT=UNAVAILABLE\n");
            std::fflush(stdout);
            return 6;
        }
        stelapp::LiveSkyRuntime::Config cfg =
            engineConfigFromEnv(0.1, kEngineNominalFps);
        if (!liveSkyRuntime->start(&frameMailbox, cfg, &producerError)) {
            std::fprintf(stderr, "UICHECK: 帧泵启动失败：%s\n",
                         producerError.toUtf8().constData());
            std::printf("UICHECK: VERDICT=UNAVAILABLE\n");
            std::fflush(stdout);
            return 6;
        }
        appFacade.attachSimControl(liveSkyRuntime.get());

        runUiLocateCheck(&app, window, &appFacade);
        const int rc = app.exec();
        if (liveSkyRuntime) {
            liveSkyRuntime->stop();
            liveSkyRuntime.reset();
        }
        std::fflush(nullptr);
        _exit(backendOk ? rc : 3);
#endif
    }

    // 消费侧全量计量长跑（P-BRG-01 验收）：warmup + measure 两段，逐秒/逐帧 CSV。
    // 环境前置不合规时以退出码 9 拒绝测量（不产生任何测量数据）。
    //
    // T13：生产者可切（STELQUICK_LONGRUN_PRODUCER）。
    //   · 默认/"test" —— LiveFrameSource（替身场景）
    //   · "engine"    —— LiveSkyRuntime（**真实引擎**）→ 这才是"合流形态长跑"
    // 与 DYN 分支同构：装配在此完成（engine 须先暖机再 boot），SkyLongRun 只吃
    // IFrameProducer*。尺寸与名义速率**一律以 STELQUICK_LONGRUN_* 为准**，
    // 不读 STELQUICK_LIVE_*（避免两套口径互相污染）。
    if (longRun) {
        if (!skyViewport) {
            std::fprintf(stderr, "长跑失败：未找到 SkyViewport\n");
            return 6;
        }
        stelapp::SkyLongRunOptions longOptions = stelapp::SkyLongRun::optionsFromEnv();
        const QByteArray longProducerKind = qgetenv("STELQUICK_LONGRUN_PRODUCER");
        const bool engineLongProducer = (longProducerKind == "engine");
        stelapp::IFrameProducer *producer = nullptr;
        QString producerError;

        if (!engineLongProducer) {
            stelapp::LiveFrameSourceConfig cfg;
            cfg.physicalSize = longOptions.physicalSize;
            cfg.devicePixelRatio = longOptions.devicePixelRatio;
            cfg.fps = longOptions.producerFps;
            cfg.simRate = 1.0;
            if (liveSource.start(&frameMailbox, cfg, &producerError))
                producer = &liveSource;
        }
#if defined(STELQUICK_HAS_ENGINE) && defined(STELQUICK_WIDGETS_HOST)
        else {
            // engine 形态：与 DYN-engine 完全同一顺序纪律（暖机 → boot → start）。
            // 名义速率默认 50——引擎产能档（不是 UI 上限 60，见 kEngineNominalFps）；
            // simRate 默认 0.02（1 天/50s）→ 30 分钟走约 54 天，内容持续变化但
            // 不引入负载阶跃（比 0.1 更贴近长期运行的真实观感）。
            if (!qEnvironmentVariableIsSet("STELQUICK_LONGRUN_FPS"))
                longOptions.producerFps = kEngineNominalFps;
            // SL-C03 口径：engine 形态的生产者与消费侧**共占 GUI 线程**（形态属性，
            // 不是从数据倒推）——引擎 tick 会牵引 QML 上屏栅格，绝对毫秒门槛不适用。
            longOptions.producerSharesGuiThread = true;
            warmUpSceneGraph(window);
            // 引擎重 GL 初始化（StelMainView::initializeGL）之前先让 QML 静默——
            // 根因与判别证据见 quiesceSceneGraph 头注释（T13）。
            quiesceSceneGraph(quiesceMs());
            liveSkyRuntime = std::make_unique<stelapp::LiveSkyRuntime>();
            stelapp::LiveSkyRuntime::Config engineCfg;
            engineCfg.renderSize = longOptions.physicalSize;
            engineCfg.fps = longOptions.producerFps;
            const QByteArray longSimRate = qgetenv("STELQUICK_LIVE_SIMRATE");
            engineCfg.simRate = longSimRate.isEmpty() ? 0.02 : longSimRate.toDouble();
            if (!liveSkyRuntime->boot(&producerError)) {
                std::fprintf(stderr, "长跑：引擎引导失败：%s\n",
                             producerError.toUtf8().constData());
                liveSkyRuntime.reset();
            } else {
                // 引导已完成（引擎的 GL 资源已建）；再静默一次，使帧泵首帧
                // `stelApp.draw()`（planets GL shaders 初始化）也在 QML 空闲时进行。
                quiesceSceneGraph(quiesceMs());
                if (!liveSkyRuntime->start(&frameMailbox, engineCfg, &producerError)) {
                    std::fprintf(stderr, "长跑：引擎帧泵启动失败：%s\n",
                                 producerError.toUtf8().constData());
                    liveSkyRuntime.reset();
                } else {
                    producer = liveSkyRuntime.get();
                    appFacade.attachSimControl(liveSkyRuntime.get());   // T15：暂停/继续落点
                }
            }
        }
#else
        else {
            producerError = QStringLiteral(
                "engine 生产者需要 Widgets 宿主形态构建（-DSTELQUICKUI_WIDGETS_HOST=ON 且 "
                "-DENABLE_STELQUICKUI=ON）");
        }
#endif

        if (!producer) {
            std::fprintf(stderr, "长跑失败：生产者装配失败：%s\n",
                         producerError.toUtf8().constData());
            std::printf("STELLRUN: VERDICT=UNAVAILABLE（生产者未装配，不得据此声称通过）\n");
            std::fflush(stdout);
            return 6;
        }
        std::printf("STELLRUN: 生产者=%s\n",
                    engineLongProducer ? "engine(真实引擎)" : "test(替身场景)");
        std::fflush(stdout);

        stelapp::SkyLongRun::runStartupSequence(
            &app, window, skyViewport, &frameMailbox, producer, longOptions,
            [&app](const stelapp::SkyLongRunResult &result) {
                std::printf("STELLRUN: %s\n", result.summary.toUtf8().constData());
                for (const QString &line : result.details)
                    std::printf("STELLRUN: %s\n", line.toUtf8().constData());
                if (!result.ran) {
                    std::printf("STELLRUN: VERDICT=%s\n",
                                result.envBlocked ? "ENV_FAIL" : "UNAVAILABLE");
                    std::fflush(stdout);
                    app.exit(result.envBlocked ? 9 : 6);
                    return;
                }
                std::printf("STELLRUN: VERDICT=%s\n",
                            result.dataInvalid ? "INVALID"
                                               : (result.pass ? "PASS" : "FAIL"));
                std::fflush(stdout);
                app.exit(result.pass ? 0 : (result.dataInvalid ? 9 : 8));
            });
        const int rc = app.exec();
#if defined(STELQUICK_HAS_ENGINE) && defined(STELQUICK_WIDGETS_HOST)
        if (engineLongProducer) {
            // 引擎引导过的进程不走正常 return（双图形栈析构顺序未定义，探针实测
            // return 后 SIGSEGV(139) 会吞掉判据码）。与 DYN-engine 同一纪律。
            // 注意：帧泵已在 SkyLongRun 收尾由 producer->stop() 停掉，这里只做
            // 引擎侧离屏资源的释放（stop 幂等）。
            if (liveSkyRuntime) {
                liveSkyRuntime->stop();
                liveSkyRuntime.reset();
            }
            std::fflush(nullptr);
            _exit(backendOk ? rc : 3);
        }
#endif
        return (rc == 0 && !backendOk) ? 3 : rc;
    }

#if defined(STELQUICK_HAS_ENGINE) && defined(STELQUICK_WIDGETS_HOST)
    // ── T11：真实引擎帧驱动模式（STELQUICK_LIVE_ENGINE=1）────────────────────
    // 引擎引导 + 帧泵全在 GUI 线程；与 LiveFrameSource 互斥（邮箱单生产者契约）。
    if (liveEngine) {
        if (!skyViewport) {
            std::fprintf(stderr, "LIVE_ENGINE 失败：未找到 SkyViewport\n");
            return 6;
        }
        liveSkyRuntime = std::make_unique<stelapp::LiveSkyRuntime>();
        // 引擎操作前的顺序纪律：先让 QML 场景图完成首次渲染（见 warmUpSceneGraph）。
        warmUpSceneGraph(window);
        QString bootError;
        if (!liveSkyRuntime->boot(&bootError)) {
            std::fprintf(stderr, "LIVESKY: 引擎引导失败：%s\n", bootError.toUtf8().constData());
            std::fflush(stderr);
            return 8;
        }
        // 手动查看模式：simRate 默认 0.02（1 天 / 50s，接近真实观感）
        const stelapp::LiveSkyRuntime::Config engineCfg = engineConfigFromEnv(0.02);
        skyViewport->setDegradeThreshold(qEnvironmentVariable("STELQUICK_DEGRADE_FPS", "15").toDouble());
        QString startError;
        if (!liveSkyRuntime->start(&frameMailbox, engineCfg, &startError)) {
            std::fprintf(stderr, "LIVESKY: 帧泵启动失败：%s\n", startError.toUtf8().constData());
            std::fflush(stderr);
            return 8;
        }
    }
#endif

    // 手动查看模式（动态）：STELQUICK_LIVE=1 起动态帧生产者。
    // 与静态图案投递互斥（邮箱契约：同一时刻只允许一个生产者）。
    const bool liveMode = !liveEngine && qEnvironmentVariableIsSet("STELQUICK_LIVE");
    if (liveMode && skyViewport) {
        stelapp::LiveFrameSourceConfig liveCfg;
        const QByteArray fpsEnv = qgetenv("STELQUICK_LIVE_FPS");
        if (!fpsEnv.isEmpty())
            liveCfg.fps = fpsEnv.toDouble();
        const QByteArray sizeEnv = qgetenv("STELQUICK_LIVE_SIZE");
        if (!sizeEnv.isEmpty()) {
            const QList<QByteArray> wh = sizeEnv.split('x');
            if (wh.size() == 2)
                liveCfg.physicalSize = QSize(wh.at(0).toInt(), wh.at(1).toInt());
        }
        skyViewport->setDegradeThreshold(qEnvironmentVariable("STELQUICK_DEGRADE_FPS", "15").toDouble());
        QString liveError;
        if (liveSource.start(&frameMailbox, liveCfg, &liveError))
            std::printf("STELQUICK: 动态帧生产者已启动（%dx%d，%.1f fps）\n",
                        liveCfg.physicalSize.width(), liveCfg.physicalSize.height(), liveCfg.fps);
        else
            std::fprintf(stderr, "STELQUICK: 动态帧生产者启动失败：%s\n",
                         liveError.toUtf8().constData());
        std::fflush(stdout);
    }

    // 手动查看模式：投递一次静态测试图案，便于人眼确认通路已通。
    // 说明：静态帧源只投一次，窗口后续缩放不会重新生成图案（图案会被缩放显示）；
    // A2 主体接入真实产帧后，此段整体删除。
    //
    // 2026-09-21 Windows 定位：原先固定 300ms 就投递，但此时 QML 布局尚未跑完
    // （StackLayout 还没给视口分配尺寸）→ 逻辑 0x0 → 报"视口物理尺寸过小…1x1"。
    // 这个抖动一直以"假失败日志"形式存在（不影响 A2 模式，那边自己带等待）。
    // 处置：改成轮询等视口拿到有效尺寸再投递，最多等 5s；超时才报真失败。
    if (skyViewport && !liveMode) {
        auto *pending = new QTimer(&app);
        pending->setInterval(50);
        QObject::connect(pending, &QTimer::timeout, &app,
                         [pending, &frameMailbox, skyViewport, window]() {
            const QSize logical(qRound(skyViewport->width()), qRound(skyViewport->height()));
            const int waited = pending->property("waitedMs").toInt() + 50;
            pending->setProperty("waitedMs", waited);
            if (logical.width() < 16 || logical.height() < 16) {
                if (waited < 5000)
                    return;   // 布局还没好，继续等
                pending->stop();
                std::printf("A2: 静态测试图案投递失败：等待 %dms 后视口仍无效（%dx%d）\n",
                            waited, logical.width(), logical.height());
                std::fflush(stdout);
                return;
            }
            pending->stop();
            QString error;
            const bool ok = stelapp::StaticFrameSource::publishTestPattern(
                frameMailbox, logical, window->effectiveDevicePixelRatio(), 1, 1, &error);
            std::printf("A2: 静态测试图案%s（逻辑 %dx%d，等待 %dms）%s%s\n",
                        ok ? "已投递" : "投递失败",
                        logical.width(), logical.height(), waited,
                        error.isEmpty() ? "" : "原因：", error.toUtf8().constData());
            std::fflush(stdout);
        });
        pending->setProperty("waitedMs", 0);
        pending->start();
    }

    // 交互回归自测模式（自动化 A1 手动项）
    if (qEnvironmentVariableIsSet("STELQUICK_WINDOW_TEST")) {
        runInteractionTest(&app, window, baseW, baseH);
        const int rc = app.exec();
        return backendOk ? rc : 3;
    }

    // 自动验收模式：N 秒后自动退出，返回码反映后端判定。
    // 附加：STELQUICK_GRAB_AT_SECONDS=N + STELQUICK_GRAB_PATH=<png>
    //   在第 N 秒抓一帧窗口内容存盘（固定场景图像比对的通用手段，不依赖 A2 校验）。
    const int autoSeconds = qEnvironmentVariableIntValue("STELQUICK_AUTOTEST_SECONDS");
    if (autoSeconds > 0) {
        QTimer::singleShot(autoSeconds * 1000, &app, &QCoreApplication::quit);
    }
    const int grabAt = qEnvironmentVariableIntValue("STELQUICK_GRAB_AT_SECONDS");
    const QString grabPath = qEnvironmentVariable("STELQUICK_GRAB_PATH");
    if (grabAt > 0 && !grabPath.isEmpty()) {
        QTimer::singleShot(grabAt * 1000, &app, [window, grabPath]() {
            const QImage grabbed = window->grabWindow();
            const bool ok = !grabbed.isNull() && grabbed.save(grabPath);
            std::printf("STELQUICK: 抓帧 %s → %s（%dx%d）\n",
                        ok ? "成功" : "失败", grabPath.toUtf8().constData(),
                        grabbed.width(), grabbed.height());
            std::fflush(stdout);
        });
    }

    // T39：把帧泵接上诊断页（**运行时诊断**的数据源）。
    // 放在这里 = 正常运行路径的最后一个共同点，且 frameMailbox 与 backendInfo 都还活着。
    // 自检路径各自 exit，不需要诊断页；`refresh()` 在没接时只发信号、不假装 0 是读数。
    if (liveSkyRuntime)
        backendInfo->setFrameMailbox(&frameMailbox);

    // T40-B：快捷键表同理 —— 引擎延迟引导（A3）⇒ 引导完成后才能拉到动作注册表。
    // 放在这里 = 同一个共同点；未引导时 `refresh()` 是空操作（表保持空、UI 显示空态）。
    if (liveSkyRuntime)
        shortcutModel.refresh();

    // T41-B：版本/系统信息同理（`StelUtils` 要走 `StelFileMgr`，引导后才拿得到）。
    // GPL 全文与贡献者名单不依赖引擎，构造时就已经就绪。
    if (liveSkyRuntime)
        helpModel.refresh();

    const int rc = app.exec();
    liveSource.stop();   // 生产者停机先于邮箱析构（析构顺序：liveSource 在 frameMailbox 之后声明）
#if defined(STELQUICK_HAS_ENGINE) && defined(STELQUICK_WIDGETS_HOST)
    if (liveSkyRuntime)
        liveSkyRuntime->stop();
    // 引擎引导过的进程不走正常 return：双图形栈（引擎 GL + QML RHI）的析构顺序
    // 未定义，A3 前置探针实测 return 后 SIGSEGV(139) 吞掉判据码。
    // 与探针同一纪律：_exit 直接交付退出码。
    if (liveEngine) {
        std::fflush(nullptr);
        _exit(backendOk ? rc : 3);
    }
#endif
    return backendOk ? rc : 3;
}
