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
#include "app/AppFacade.hpp"
#include "ui/IFrameProducer.hpp"

class QTimer;
class QSettings;
class StelMainView;
class StelCore;

namespace stelapp {

class FrameMailbox;

//! T12：实现 IFrameProducer，使本类可被 DynFrameCheck 以"生产者无关"的方式驱动——
//! 即 DYN 的 7 项判据能在**真实引擎**上复跑，而不只是 LiveFrameSource 替身。
//! 差异口径：setFps 在本类是 GUI 线程内改 QTimer 间隔（不是原子量），调用线程受限。
//!
//! T15：另实现 ISimPacing（AppFacade 暂停/继续 + 速率的落点）。时间推进从
//! "jd0 + sim×simRate 的闭式公式"改为**逐 tick 累计**（m_jdAccum += dtWall×simRate×scale）：
//!   - 暂停（scale=0）冻结 JD 但帧泵照跑，恢复无跳变（闭式公式做不到）；
//!   - 默认 scale=1 时与旧公式数值等价（同起点同速率，仅累计浮点路径不同），
//!     DYN/长跑判据不受影响。
//!
//! T16：**本类不再持有时钟状态**。T15 的 m_jdAccum/m_lastSim/m_simScale 全部上移到
//! 引擎内的单一真源（StelCore 的 StelClockController）：
//!   - 帧泵只做两件事：`core->advanceSimClock(dt)` 与 `renderOneFrame`；
//!   - ISimPacing 的四个方法变成对引擎时钟的**纯投影**（读/写都落在引擎上）。
//! 这样插件/脚本/GUI 的 core->setJD 与宿主推进共享同一个 JD，前者不再被后者拽回。
class LiveSkyRuntime : public IFrameProducer, public ISimPacing
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
    void stop() override;

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

    // ── IFrameProducer ────────────────────────────────────────────────────────
    //! 动态调速（P-BRG-04 降级探针）：改 QTimer 间隔。**仅 GUI 线程可调**
    //! （LiveFrameSource 的同名方法是跨线程原子量，两者差异在此，调用方需按实现遵守）。
    void setFps(double fps) override;
    //! 口径统一：rendered 取 "已进邮箱的帧数"（published），与 LiveFrameSource 一致。
    ProducerCounters counters() const override;

    // ── ISimPacing（AppFacade 暂停/继续 + 速率；仅 GUI 线程）──────────────────
    // T16：以下四个方法都是对**引擎仿真时钟**的投影，本类不再持有对应状态。
    // 于是"暂停"只有一个真源：无论经 AppFacade 还是经插件/脚本调 setTimeRate，
    // 落点都是同一个 StelClockController。
    void setSimScale(double scale) override;
    double simScale() const override;
    void setSimRate(double rate) override;
    double simRate() const override;

private:
    void pumpTick();

    bool m_booted = false;
    Config m_cfg;
    QTimer *m_timer = nullptr;
    QElapsedTimer m_simClock;
    double m_jd0 = 0.0;        //!< 仿真起点 JD（仅用于启动日志对账）
    quint64 m_windowFrames = 0;
    double m_windowStart = 0.0;
    double m_lastWindowFps = 0.0;

    // 引擎侧对象（boot 创建，进程退出路径负责；本类不销毁引擎）
    QSettings *m_confSettings = nullptr;
    StelMainView *m_mainView = nullptr;
    //! T16：仿真时钟真源所在（不持有所有权）。start() 后非空。
    StelCore *m_core = nullptr;
    std::unique_ptr<LegacySkyHost> m_host;
};

} // namespace stelapp

#endif // STELQUICK_HAS_ENGINE && STELQUICK_WIDGETS_HOST
