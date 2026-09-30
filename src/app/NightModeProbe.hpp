/*
 * NightModeProbe — T37-A **夜视链路数据面探针**（STELQUICK_NIGHT_PROBE=1）。
 *
 * ── 为什么第一相位是探针（T24/T33/T34/T35 同款纪律）──────────────────────────
 * A-1.0 范围表把"主题/夜视"列为**必须**，并在备注里写了一句**前提性假设**：
 *   「天空夜视效果仍由旧后处理负责，避免叠加两次」。
 * 这句话在**合流形态**（QQuickWindow 承载 + 引擎自建离屏 FBO 产帧）里
 * **从未被验证过**。开工前的代码审计给出的链条是：
 *   ① 夜视 = `StelMainView.cpp:166` 的 `NightModeGraphicsEffect`（**纯 OpenGL
 *      shader + QOpenGLFramebufferObject**），挂在 `rootItem`（**QGraphicsItem**）
 *      上（`StelMainView.cpp:991-994` `setGraphicsEffect`）。
 *   ② 触发链 = `StelApp::setVisionModeNight()`（`StelApp.cpp:1284`）只 `emit
 *      visionNightModeChanged` ⇒ `StelMainView::updateNightModeProperty`
 *      （`StelMainView.cpp:1091`）⇒ `nightModeEffect->setEnabled(b)` + 给宿主
 *      `setProperty("nightMode", b)`。
 *   ③ 那条 connect 在 `#ifndef NO_GUI` 块内（`StelMainView.cpp:1023`）；
 *      本机构建 `STELLARIUM_GUI_MODE=Standard`（`build-release/CMakeCache.txt`）
 *      ⇒ **未定义 NO_GUI** ⇒ **connect 在**。
 *   ④ **但** `QGraphicsEffect::draw()` 只在 **QGraphicsView 的 scene 渲染循环**
 *      里被调用；合流形态的宿主是 `WA_DontShowOnScreen`（`LiveSkyRuntime.cpp:79`），
 *      而且帧根本不经它（帧由 `LegacySkyHost` 自建 FBO + `StelApp::draw()` 产出，
 *      `StelApp::draw()` 路径里**没有任何夜视分支** —— 已逐行核过）。
 *   ⑤ 且 `NightModeGraphicsEffect` 是 GL 对象，与合流形态 Qt Quick 的 **Metal RHI**
 *      是两条独立图形栈。
 * ⇒ 推断候选：**夜视对读回帧零影响**（A-1.0 那句"仍由旧后处理负责"在合流形态下
 *   站不住 ⇒ "叠加两次"的顾虑不存在 ⇒ 夜视须在 Qt Quick 侧自己实现）。
 *   **但推断不是证据**，必须实测。探针不合格就不写 UI（T34 立的规矩）。
 *
 * ── ⚠️ T37-C 修正（2026-09-30 三轮反转后的最终口径）─────────────────────────
 *   ① "GL effect 对读回帧零影响"**仍然成立**（实验：禁用该 effect 行为不变）。
 *   ② 但"夜视对上游帧零影响"**不成立**——引擎自己有夜视反应：三处大气类
 *      `if (getVisionModeNight()) return;` ⇒ 夜视时大气整层退场（原版语义，
 *      旧宿主靠 GL effect 滤红、合流形态靠 Qt Quick 滤镜承担红移）。
 *   ③ 首轮探针的"上游 OFF↔ON 逐位相同"是**假绿**：S5 的 900ms 曾挂在
 *      `delayAfter`（读步之后），S5 抓帧紧跟 S4 翻转 ⇒ 读到旧帧（帧号相同
 *      实锤）。已修：等待移到写入步（S4/S6）。**陷阱 69 的第一个实例。**
 *
 * ── 探针不是判据（刻意）────────────────────────────────────────────────────
 * 只报读数、不下 PASS/FAIL（断言在 T37-C 的 NightModeCheck）。
 *
 * ── 探针要回答的问题 ─────────────────────────────────────────────────────
 *   Q1 **引擎状态面**：`StelApp::getVisionModeNight()` 初值 / 翻转后 / 还原后。
 *   Q2 **宿主链面**：`StelMainView::getInstance()` 的 `isVisible` /
 *      `WA_DontShowOnScreen` / `isHighGraphicsMode` / `property("nightMode")`
 *      —— 直接回答"环节①②那条 connect 链到底活不活"（属性跟随 = 链活）。
 *   Q3 **帧静定基线（噪声底）**：**同一状态**连取两帧的差异。判据解释的前提：
 *      非零噪声底会抬高后面所有"差异显著"的门槛；噪声底为零时"零差异"才是硬证据。
 *   Q4 **上游帧面**：夜视 OFF/ON 两帧在 **`FrameMailbox` 原始 RGBA** 上的差异
 *      （= 我们真正交给 Qt Quick 的东西）。
 *   Q5 **下游帧面**：夜视 OFF/ON 两帧在 **`QQuickWindow::grabWindow()`** 上的差异
 *      （= 用户眼睛看到的东西）。两条**独立读回路径**（陷阱 43）。
 *   Q6 **判别性对照（承重）**：用**同一套取帧 + 比较方法**，对
 *      `actionShow_Constellation_Lines` OFF/ON 两帧比较 —— **必须**测出显著差异，
 *      否则 Q4/Q5 的"零差异"是"比较方法根本测不出任何东西"的假绿（T34/T35 血泪）。
 *
 * ── 为什么必须冻结仿真 ─────────────────────────────────────────────────────
 * 帧泵在跑 ⇒ JD 每帧推进 ⇒ 星空/大气/日月光照每帧都不同 ⇒ 逐像素差异必然非零，
 * Q4/Q5 会被时间噪声淹没。故探针先 `facade->setSimulationPaused(true)`
 * （`ISimPacing::setSimScale(0)`：**帧泵照常出帧，只有 JD 推进被冻结**），
 * 再用 Q3 量出残留噪声底。收尾还原。
 *
 * ── 退出码（由 main.cpp 决定）──────────────────────────────────────────────
 *   0=DONE（正常报完读数），6=UNAVAILABLE（非合流形态/引擎未引导）。
 */
#pragma once

#include <QString>
#include <QStringList>
#include <functional>

class QCoreApplication;
class QQuickWindow;

namespace stelapp {

class AppFacade;
class ActionRouter;
class FrameMailbox;

class NightModeProbe
{
public:
    struct Result
    {
        bool ran = false;          //!< 真正跑过（前置满足）
        bool unavailable = false;  //!< 环境里跑不起来（非合流形态/引擎未引导）
        QString summary;
        QStringList details;       //!< 逐行读数（前缀已在 main.cpp 侧统一加）
    };

    //! 全部在 GUI 线程。@p delayMs 后开跑（等引擎动作注册落定）。结果经 onDone 回传。
    static void run(QCoreApplication *app,
                    AppFacade *facade,
                    ActionRouter *router,
                    QQuickWindow *window,
                    FrameMailbox *mailbox,
                    const std::function<void(const Result &)> &onDone,
                    int delayMs = 2500);
};

} // namespace stelapp
