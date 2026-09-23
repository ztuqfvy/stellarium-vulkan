/*
 * LiveSkyRuntime — 真实引擎的进程内帧驱动（T11，里程碑 A3 第一步落地）。
 *
 * 定位：替代 LiveFrameSource（消费侧联调替身）的位置——同样是"往 FrameMailbox
 *   投递动态帧的生产者"，但渲染回调画的是**真实引擎**（StelApp::update/draw），
 *   而不是 LegacyTestScene。消费侧（SkyViewport）零改动。
 *
 * 与 LiveFrameSource 的根本差异（线程模型，2026-09-23 探针定案）：
 *   LiveFrameSource：独立 std::thread + kOwn 离屏上下文（引擎不参与）。
 *   LiveSkyRuntime：**GUI 线程内驱动**。理由：
 *     1. 引擎上下文归 StelGLWidget（QOpenGLWidget），owner = QApplication 主线程；
 *        GL 上下文不可跨线程使用，跨线程 makeCurrent 直接 abort。
 *     2. StelApp/StelModule 是 QObject 树 + 内部定时器，全为 GUI 线程亲和。
 *     3. 探针（STELQUICK_ENGINE_COEXIST）已证明：GUI 线程上 update/draw 安全，
 *        且 QML 窗口同时存活（引导后 +179 帧/3s，零退化）。
 *   节奏用 QTimer 分片（名义值；计量口径以实测为准）。
 *
 * 引擎引导（boot）：照抄 render/legacy/LegacyAppCheck A3-C01 的前置链
 *   （StelFileMgr → StelLogger → config.ini → StelTranslator）+ 探针验证过的
 *   new StelMainView + WA_DontShowOnScreen → initializeGL → StelApp::init。
 *
 * 帧驱动（start 后）：每 tick
 *   StelMainView::glContextMakeCurrent()  ← 引擎上下文 current（外部管理）
 *   → LegacySkyHost::renderOneFrame(sim)  ← kContextMode::kBorrowed 借引擎上下文，
 *                                            绑**自己的离屏 FBO**，回调里
 *                                            setJD + update + draw，读回 RGBA8
 *   → StelMainView::glContextDoneCurrent()
 * 探针 C-05..C-07 已证明引擎 draw 尊重外部 FBO 绑定（读回非黑、随 JD 变化）。
 *
 * 禁止：
 *   1. 任何方法在非 GUI 线程调用（本类不设锁——加锁只会掩盖误用）。
 *   2. 帧泵运行期间触碰 StelMainView 的可见性（WA_DontShowOnScreen 是前提）。
 *   3. 多个生产者投递同一邮箱（邮箱契约：单生产者）。
 *
 * 编译形态：本文件**无条件**进源列表，内容用双宏守卫
 *   （STELQUICK_HAS_ENGINE + STELQUICK_WIDGETS_HOST，见 src/ui/CMakeLists.txt）。
 *   独立工程 / 默认形态下编译为空——引擎头（StelMainView.hpp 等）不进依赖面。
 */
#pragma once

#include <QElapsedTimer>
#include <QSize>
#include <QString>
#include <memory>

#if defined(STELQUICK_HAS_ENGINE) && defined(STELQUICK_WIDGETS_HOST)

#include "render/legacy/LegacySkyHost.hpp"

class QTimer;
class QSettings;
class StelMainView;

namespace stelapp {

class FrameMailbox;

class LiveSkyRuntime
{
public:
    struct Config
    {
        //! 离屏 FBO 物理尺寸（与 T9 基线同尺寸，计量口径一致）。
        //! 注意：这也是 StelMainView::resize 的逻辑尺寸（DPR=1 假设）。
        QSize renderSize{1280, 720};
        //! 帧泵名义节奏（QTimer 间隔 = 1000/fps）。<=0 视为 60。
        double fps = 60.0;
        //! 仿真时间推进速率：JD 变化（天）/ 墙钟秒。默认 1 天/50s
        //! （探针 C-07 用 0.25 天/帧序号验证过画面变化；长跑用更慢的真实速率）。
        double simRate = 0.02;
    };

    LiveSkyRuntime() = default;
    ~LiveSkyRuntime();
    LiveSkyRuntime(const LiveSkyRuntime &) = delete;
    LiveSkyRuntime &operator=(const LiveSkyRuntime &) = delete;

    //! 引擎无头引导（GUI 线程）。失败返回 false 并写入可操作的原因。
    //! 幂等：已 boot 成功时重复调用返回 true。
    bool boot(QString *errorOut);

    //! 起帧泵（GUI 线程）。boot 必须已成功；邮箱必须比本对象活得久。
    bool start(FrameMailbox *mailbox, const Config &config, QString *errorOut);

    //! 停帧泵并释放离屏资源（GL 资源在本线程、引擎上下文 current 时清理）。
    //! 引擎本体（StelApp/StelMainView）**不关**——进程退出路径由应用层决定。
    void stop();

    bool isBooted() const { return m_booted; }
    bool isRunning() const { return m_timer != nullptr; }

    struct Stats
    {
        quint64 requested = 0;   //!< 帧泵 tick 次数
        quint64 published = 0;   //!< 成功进邮箱的帧数
        quint64 dropped = 0;     //!< 邮箱拒收（忙时丢旧，设计内）
        quint64 failed = 0;      //!< renderOneFrame 失败（必须为 0）
        double fps = 0.0;        //!< 最近 1s 窗口实测投递速率
    };
    Stats stats() const;

private:
    void pumpTick();

    bool m_booted = false;
    Config m_cfg;
    QTimer *m_timer = nullptr;
    QElapsedTimer m_simClock;
    double m_jd0 = 0.0;
    quint64 m_windowFrames = 0;
    double m_windowStart = 0.0;
    double m_lastWindowFps = 0.0;

    // 引擎侧对象（boot 创建，进程退出路径负责；本类不销毁引擎）
    QSettings *m_confSettings = nullptr;
    StelMainView *m_mainView = nullptr;
    std::unique_ptr<LegacySkyHost> m_host;
};

} // namespace stelapp

#endif // STELQUICK_HAS_ENGINE && STELQUICK_WIDGETS_HOST
