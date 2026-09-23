// LiveFrameSource 实现。线程约束见头文件注释。
#include "ui/LiveFrameSource.hpp"

#include "render/legacy/FrameMailbox.hpp"
#include "render/legacy/LegacySkyHost.hpp"
#include "render/legacy/LegacyTestScene.hpp"

#include <QThread>
#include <cstdio>
#include <future>
#include <memory>

namespace stelapp {

LiveFrameSource::LiveFrameSource() = default;

LiveFrameSource::~LiveFrameSource()
{
    stop();
}

// 帧循环：节流驱动 renderOneFrame，直到 stop()。
// THREAD: gl-ctx（工作线程）。回调在"上下文 current + 离屏 FBO 已绑定"前提下执行；
// 场景内容 = f(simSeconds)（确定性动态），与 T6 判据同源。
void LiveFrameSource::frameLoop(const LiveFrameSourceConfig &config)
{
    QElapsedTimer clock;
    clock.start();
    double lastWall = 0.0;
    double fpsWindowStart = 0.0;
    quint64 fpsWindowFrames = 0;

    while (!m_stopRequested.load())
    {
        const double fps = m_fps.load();

        // 节流：目标帧间隔 sleep（粗粒度即可；计量看实测值，不看名义值）
        if (fps > 0.0)
        {
            const double targetInterval = 1.0 / fps;
            const double due = lastWall + targetInterval;
            const double sleepMs = (due - clock.elapsed() / 1000.0) * 1000.0;
            if (sleepMs > 0.5)
                QThread::msleep(quint64(sleepMs));
        }

        const double now = clock.elapsed() / 1000.0;
        const double sim = now * config.simRate;

        QString frameError;
        if (m_host->renderOneFrame(sim, &frameError))
        {
            ++m_rendered;
            ++fpsWindowFrames;
            // 首帧后自检场景是否真的装配成功（着色器/VAO 惰性创建，失败只在这里暴露）
            if (!m_scene->ok())
            {
                ++m_failed;
                if (!m_sceneFailureLogged)
                {
                    m_sceneFailureLogged = true;
                    std::fprintf(stderr, "LIVEFRAME: 场景装配失败：%s\n",
                                 m_scene->lastError().toUtf8().constData());
                    std::fflush(stderr);
                }
            }
        }
        else
        {
            ++m_failed;
            static quint64 logged = 0;
            if (++logged <= 5)
            {
                std::fprintf(stderr, "LIVEFRAME: 帧失败：%s\n", frameError.toUtf8().constData());
                std::fflush(stderr);
            }
        }
        lastWall = now;

        // 实测生产速率：1s 滑窗（供 GUI 侧诊断读取）
        if (now - fpsWindowStart >= 1.0)
        {
            m_measuredFps.store(double(fpsWindowFrames) / (now - fpsWindowStart));
            fpsWindowStart = now;
            fpsWindowFrames = 0;
        }
    }
}

bool LiveFrameSource::start(FrameMailbox *mailbox, const LiveFrameSourceConfig &config,
                            QString *errorOut)
{
    if (m_running.load())
    {
        if (errorOut)
            *errorOut = QStringLiteral("LiveFrameSource 已在运行");
        return false;
    }
    if (!mailbox)
    {
        if (errorOut)
            *errorOut = QStringLiteral("LiveFrameSource::start：邮箱为空");
        return false;
    }

    m_stopRequested.store(false);
    m_rendered.store(0);
    m_failed.store(0);
    m_measuredFps.store(0.0);
    m_fps.store(config.fps);

    // 装配结果经 promise 带回：装配完成（成功或失败）即置值，帧循环在其后继续。
    auto okPromise = std::make_shared<std::promise<bool>>();
    auto errorBox = std::make_shared<QString>();
    m_thread = std::thread([this, mailbox, config, okPromise, errorBox]() {
        m_running.store(true);

        // THREAD: gl-ctx。装配必须在本线程完成（GL 上下文亲和性）。
        m_host = std::make_unique<LegacySkyHost>();
        LegacySkyHostConfig hostCfg;
        hostCfg.contextMode = GlContextMode::kOwn;   // 自建离屏上下文，无窗口
        hostCfg.physicalSize = config.physicalSize;
        hostCfg.devicePixelRatio = config.devicePixelRatio;
        hostCfg.maxDimension = 4096;
        hostCfg.glMajor = config.glMajor;
        hostCfg.glMinor = config.glMinor;
        hostCfg.coreProfile = config.coreProfile;

        QString error;
        if (!m_host->initialize(hostCfg, &error))
        {
            *errorBox = QStringLiteral("LegacySkyHost::initialize（kOwn）失败：%1").arg(error);
            // GL 对象必须在装配它的线程上销毁（否则 Qt 报
            // "Cannot make QOpenGLContext current in a different thread" 并 abort）
            m_host.reset();
            okPromise->set_value(false);
            m_running.store(false);
            return;
        }
    m_scene = std::make_unique<LegacyTestScene>();
    // 注意：LegacyTestScene 是**惰性**的——ok() 只在首次 render()（着色器/VAO 在
    // 已 current 的上下文里编译成功）之后才为真。构造后立即查 ok() 必然为假，
    // 属误判（2026-09-23 首跑踩到）。这里不检查，交给首帧后的自检。
        m_host->setRenderCallback([this](double, double simSeconds, const QSize &physicalSize) {
            m_scene->render(simSeconds, physicalSize);
        });
        m_host->attachMailbox(mailbox);
        okPromise->set_value(true);

        frameLoop(config);   // 阻塞直到 stop()

        // 清理（必须在本线程：GL 资源亲和）。unique_ptr 也在这里重置——
        // 主线程析构 GL 对象会触发跨线程 makeCurrent 断言并 abort。
        m_host->withContextCurrent([this]() {
            if (m_scene)
                m_scene->releaseResources();
        });
        m_host->attachMailbox(nullptr);
        m_host->shutdown();
        m_scene.reset();
        m_host.reset();
        m_running.store(false);
    });

    const bool ok = okPromise->get_future().get();
    if (!ok)
    {
        if (m_thread.joinable())
            m_thread.join();
        if (errorOut && !errorBox->isEmpty())
            *errorOut = *errorBox;
        return false;
    }
    return true;
}

void LiveFrameSource::stop()
{
    if (!m_thread.joinable())
        return;
    m_stopRequested.store(true);
    m_thread.join();
}

LiveFrameSource::RuntimeStats LiveFrameSource::runtimeStats() const
{
    RuntimeStats s;
    s.rendered = m_rendered.load();
    s.failed = m_failed.load();
    s.producerFps = m_measuredFps.load();
    return s;
}

} // namespace stelapp
