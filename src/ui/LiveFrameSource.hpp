/*
 * LiveFrameSource — 消费侧接线的动态帧生产者（A2 主体 → 消费侧联调用）。
 *
 * 定位：把 LegacySkyHost（产帧）+ LegacyTestScene（动态合成场景，f(simSeconds)）
 *   装进一个独立线程，以可配速率向 FrameMailbox 投递动态帧。
 *   消费侧（SkyViewport 场景图线程）由此获得"持续更新的真实帧流"，
 *   用于：I-DYN 动态帧通路自检、P-BRG-01 上传/队列计量、P-BRG-04 降级 UI。
 *
 * 与 A3 真实引擎的关系：本类是**消费侧联调替身**——线程模型、邮箱投递、
 *   速率控制与将来接入 StelApp 的 LiveSkyRuntime 完全同构；唯一差异是渲染回调
 *   画的是 LegacyTestScene（无引擎依赖）而非真实天空。引擎接入（AppFacade，A4）
 *   时只换回调体，本类的线程与生命周期骨架不变。
 *
 * 线程模型（与 FrameMailbox 契约对齐）：
 *   - 生产者 THREAD: gl-ctx（本类的工作线程，上下文 kOwn，自建离屏）。
 *     LegacySkyHost 与 LegacyTestScene 的**全部调用**都发生在该线程。
 *   - 停止：stop() 置位 + join；所有清理在工作线程内完成（GL 上下文亲和性）。
 *   - setFps() 可跨线程调用（原子量），用于 P-BRG-04 降级探针的动态调速。
 *
 * 禁止：
 *   1. 在 GUI 线程调用 initialize/shutdown（GL 上下文线程亲和）。
 *   2. 在工作线程触碰任何 QML / QQuickWindow 对象。
 *   3. 多个 LiveFrameSource 投递同一邮箱（邮箱契约：单生产者）。
 */
#pragma once

#include <QSize>
#include <QString>
#include <QElapsedTimer>
#include <atomic>
#include <thread>
#include <memory>

namespace stelapp {

class FrameMailbox;
class LegacySkyHost;
class LegacyTestScene;

struct LiveFrameSourceConfig
{
    //! 离屏目标物理尺寸。默认 1280x720 与 T9 基线同尺寸（计量口径一致）。
    QSize physicalSize{1280, 720};
    //! 帧元数据里的 DPR（窗口显示时按此换算）。离屏渲染无缩放，默认 1.0。
    qreal devicePixelRatio = 1.0;
    //! 生产速率（fps）。<=0 表示全速不节流。默认 60（与显示刷新同量级，
    //! 让消费侧自然出现"同帧跳过"，邮箱年龄口径与真实场景一致）。
    double fps = 60.0;
    //! 仿真时间推进速率（simSeconds / 墙钟秒）。默认 1.0。
    double simRate = 1.0;
    //! 与旧宿主一致的 GL 版本请求。
    int glMajor = 3;
    int glMinor = 3;
    bool coreProfile = true;
};

class LiveFrameSource
{
public:
    LiveFrameSource();
    ~LiveFrameSource();
    LiveFrameSource(const LiveFrameSource &) = delete;
    LiveFrameSource &operator=(const LiveFrameSource &) = delete;

    //! 启动工作线程并在其上装配 GL 上下文与场景。失败返回 false（线程已回收）。
    bool start(FrameMailbox *mailbox, const LiveFrameSourceConfig &config, QString *errorOut);

    //! 停止并 join（幂等；未启动时为空操作）。清理全部在工作线程内完成。
    void stop();

    bool isRunning() const { return m_running.load(); }

    //! 跨线程调速（P-BRG-04 降级探针用）。<=0 表示全速。
    void setFps(double fps) { m_fps.store(fps); }
    double fps() const { return m_fps.load(); }

    //! 运行期统计快照（原子计数，任意线程可读）。
    struct RuntimeStats
    {
        quint64 rendered = 0;     //!< 工作线程完成的 renderOneFrame 次数
        quint64 failed = 0;       //!< renderOneFrame 返回 false 次数（必须为 0）
        double producerFps = 0.0; //!< 最近 1s 窗口的实测生产速率
    };
    RuntimeStats runtimeStats() const;

private:
    void frameLoop(const LiveFrameSourceConfig &config);

    std::thread m_thread;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_stopRequested{false};
    std::atomic<double> m_fps{60.0};
    std::atomic<quint64> m_rendered{0};
    std::atomic<quint64> m_failed{0};
    std::atomic<double> m_measuredFps{0.0};
    bool m_sceneFailureLogged = false;   //!< 场景装配失败只报一次（工作线程独占）
    // 工作线程独占（join 后才可能被析构清理；所有调用都在工作线程内）
    std::unique_ptr<LegacySkyHost> m_host;
    std::unique_ptr<LegacyTestScene> m_scene;
};

} // namespace stelapp
