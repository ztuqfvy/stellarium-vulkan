/*
 * LegacyAppCheck — A3 主体自检：旧宿主引擎的**进程内无头集成**。
 *
 * 回答的问题（A3 计划的核心未知数）：
 *   能否在"窗口不上屏、不进入事件循环 exec"的前提下，把旧宿主引擎（StelApp 全栈）
 *   初始化到可渲染状态，并用 T6 已验证的显式帧驱动路径（离屏 FBO → RGBA8 读回 →
 *   FrameMailbox）让它出真实天空帧？
 *
 * 与 T6 的分工：
 *   T6 证明"显式帧驱动机制"成立（用 LegacyTestScene 合成场景，不依赖引擎）；
 *   A3 证明"旧宿主引擎"能被塞进这条机制（回调体换成 StelApp::update()/draw()，
 *   不再是合成场景）。两份证据拼起来才是 A2 主体产帧侧的完整证明。
 *
 * 引导方式（A3 第一未知数）：
 *   StelApp::init() 只能由 StelGLWidget::initializeGL() 触发（需要 current GL 上下文），
 *   而 QOpenGLWidget 的上下文在首次渲染时才创建。本自检用
 *     mainWin->setAttribute(Qt::WA_DontShowOnScreen) + show() + 有界 processEvents
 *   引导：窗口对象与平台窗口存在（引擎以为自己在正常跑），但绝不合成上屏，
 *   事件循环只用 processEvents 有界泵送，绝不进入 app.exec()。
 *   若这条路走不通（initializeGL 不触发），A3-C01 会红——这正是自检要暴露的。
 *
 * 确定性保障（否则"同一状态重渲染指纹一致"会误报）：
 *   - core->setTimeRate(0)：引擎自走时钟停掉，儒略日只由本自检逐帧注入
 *     （setJD(kJdEpoch + simSeconds)），帧内容对 simSeconds 的映射完全确定；
 *   - skyDrawer->setFlagTwinkle(false)：星闪是逐帧随机数，必须关；
 *   - skyDrawer->setFlagLuminanceAdaptation(false)：人眼自适应随 update(dt)
 *     累积调整全图亮度（首跑实测定性），必须关；
 *   - 预热 30 帧并泵送事件：星表/纹理异步加载完成后才开始测量，
 *     避免把"加载进度"误判成"渲染不确定"。
 *   以上都只改运行时对象，不写用户 config.ini。
 *
 * 环境变量：
 *   STELA3_CHECK=1       触发本自检（在 src/main.cpp 分流，不创建正常主窗口）
 *   STELA3_FRAMES=N      测量帧数（默认 8）
 *   STELA3_DUMP=<path>   首测量帧存 PNG（辅助留档，不参与判定）
 *   STELA3_SIZE=WxH      注入离屏读回尺寸；0x0 为**负控**——装配必败，
 *                        期望 VERDICT=FAIL、退出码 8（回应"负控能红"教训）
 *
 * 退出码：0 = 全部判据通过；8 = 存在失败项。
 */
#pragma once

#include <QString>
#include <QStringList>

class QSettings;

namespace stelapp {

struct LegacyAppCheckResult
{
    bool ran = false;          //!< 自检确实跑完（区别于"前置条件不满足提前返回"）
    bool pass = false;         //!< failCount == 0
    int checkCount = 0;
    int failCount = 0;
    QStringList details;       //!< 判据明细（A3-C01…C08），机器可 grep
    QStringList frames;        //!< 逐帧指纹行 + 可选 dump 行
    QString summary;
    QString setupError;        //!< 装配失败的原因（可操作、不静默降级）
};

class LegacyAppCheck
{
public:
    //! 前置条件：QApplication 已创建；StelFileMgr/config 已就绪；StelTranslator::init 已做；
    //! QSurfaceFormat 默认格式已设（main.cpp 的正常流程在分流点之前全部完成）；
    //! StelMainView 单例尚未创建（正常流程尚未走到 `StelMainView mainWin(...)`）。
    static LegacyAppCheckResult run(QSettings *confSettings);
};

} // namespace stelapp
