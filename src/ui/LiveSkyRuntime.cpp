// LiveSkyRuntime 实现。线程约束见头文件注释。
#include "ui/LiveSkyRuntime.hpp"

#if defined(STELQUICK_HAS_ENGINE) && defined(STELQUICK_WIDGETS_HOST)

#include "render/legacy/FrameMailbox.hpp"

// 引擎头（使用面照抄 src/main.cpp 与 A3 前置探针）
#include "StelMainView.hpp"
#include "core/StelApp.hpp"
#include "core/StelCore.hpp"
#include "core/StelFileMgr.hpp"
#include "core/StelTranslator.hpp"
#include "core/StelIniParser.hpp"
#include "StelLogger.hpp"

#include <QCoreApplication>
#include <QEventLoop>
#include <QObject>
#include <QSettings>
#include <QThread>
#include <QTimer>
#include <cstdio>

namespace stelapp {

LiveSkyRuntime::~LiveSkyRuntime()
{
    stop();
    // 注意：引擎本体（StelMainView/StelApp/QSettings）不在这里销毁——
    // 双图形栈（引擎 GL + QML Metal/Vulkan）的正常析构顺序未定义（探针实测
    // return 后 SIGSEGV），进程退出路径用 _exit 或接受 OS 回收。本类只管自己的离屏资源。
}

bool LiveSkyRuntime::boot(QString *errorOut)
{
    const auto fail = [errorOut](const QString &msg) {
        if (errorOut)
            *errorOut = msg;
        return false;
    };

    if (m_booted)
        return true;
    if (m_mainView)
        return fail(QStringLiteral("LiveSkyRuntime 处于异常状态（mainView 已存在但未 boot 完成）。"));

    // ── 引擎前置：照抄 src/main.cpp 的顺序（缺一不可）──────────────────────
    StelFileMgr::init();
    const QString userDir = StelFileMgr::getUserDir();
    StelLogger::init(userDir + QStringLiteral("/log.txt"));

    QString configFileFullPath = StelFileMgr::findFile(
        QStringLiteral("config.ini"),
        StelFileMgr::Flags(StelFileMgr::Writable | StelFileMgr::File));
    if (configFileFullPath.isEmpty())
        configFileFullPath = StelFileMgr::findFile(QStringLiteral("config.ini"), StelFileMgr::New);
    if (configFileFullPath.isEmpty())
        return fail(QStringLiteral("既找不到也建不出 config.ini（StelFileMgr::findFile 双路失败）。"));

    m_confSettings = new QSettings(configFileFullPath, StelIniFormat, nullptr);
    StelTranslator::init(StelFileMgr::getInstallationDir() + QStringLiteral("/data/languages.tab"));

    // ── 引擎无头引导（A3-C01 同一路径；QML 窗口此时可以同时存在——探针已验证）──
    m_mainView = new StelMainView(m_confSettings);
    m_mainView->setAttribute(Qt::WA_DontShowOnScreen, true);
    m_mainView->resize(m_cfg.renderSize);
    m_mainView->show();

    QElapsedTimer bootClock;
    bootClock.start();
    while (bootClock.elapsed() < 30000 && !StelApp::isInitialized())
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        if (!StelApp::isInitialized())
            QThread::msleep(10);
    }
    if (!StelApp::isInitialized())
        return fail(QStringLiteral("引擎无头初始化失败：30s 内 initializeGL 未触发。"));

    const StelMainView::GLInfo &gi = StelMainView::getInstance().getGLInformation();
    std::printf("LIVESKY: 引擎引导完成（%lldms，GL %d.%d core=%d renderer=\"%s\"）\n",
                static_cast<long long>(bootClock.elapsed()),
                gi.majorVersion, gi.mainContext ? gi.mainContext->format().minorVersion() : 0,
                gi.isCoreProfile ? 1 : 0, qPrintable(gi.renderer));
    std::fflush(stdout);

    m_booted = true;
    return true;
}

bool LiveSkyRuntime::start(FrameMailbox *mailbox, const Config &config, QString *errorOut)
{
    const auto fail = [errorOut](const QString &msg) {
        if (errorOut)
            *errorOut = msg;
        return false;
    };

    if (!m_booted || !m_mainView)
        return fail(QStringLiteral("LiveSkyRuntime::start 在 boot() 成功之前调用。"));
    if (m_timer)
        return fail(QStringLiteral("LiveSkyRuntime 已在运行，重复 start。"));
    if (!mailbox)
        return fail(QStringLiteral("LiveSkyRuntime::start：邮箱为空。"));
    if (config.renderSize.width() < 1 || config.renderSize.height() < 1)
        return fail(QStringLiteral("LiveSkyRuntime::start：渲染尺寸非法 %1x%2。")
                        .arg(config.renderSize.width())
                        .arg(config.renderSize.height()));

    m_cfg = config;

    // ── 离屏产帧宿主：借引擎上下文，绑定自己的 FBO ──────────────────────────
    // 为什么必须 contextManagedExternally：QOpenGLContext::doneCurrent() 会把
    // surface() 清空，借用模式下宿主自己 makeCurrent 拿不到表面（2026-09-23 首跑实测）。
    // 上下文的 current/done 由我们用 StelMainView 公开接口管理。
    m_host = std::make_unique<LegacySkyHost>();
    LegacySkyHostConfig hostCfg;
    hostCfg.contextMode = GlContextMode::kBorrowed;
    hostCfg.contextManagedExternally = true;
    hostCfg.physicalSize = m_cfg.renderSize;
    hostCfg.devicePixelRatio = 1.0;
    hostCfg.maxDimension = 4096;
    // glMajor/glMinor/coreProfile 仅对 kOwn 生效；引擎格式由 StelMainView 决定。

    m_mainView->glContextMakeCurrent();
    const bool hostOk = m_host->initialize(hostCfg, errorOut);
    m_mainView->glContextDoneCurrent();
    if (!hostOk)
    {
        m_host.reset();
        return false;   // errorOut 已由 initialize 填好
    }

    // ── 渲染回调：真实引擎（T11 的核心换装点）────────────────────────────────
    StelApp &stelApp = StelApp::getInstance();
    StelCore *core = stelApp.getCore();
    // 关掉引擎自己的时间推进（timeRate=0），改用显式 setJD：
    // "帧内容随仿真时间变化"由我们完全控制，可复现、可对账（探针 C-07 同法）。
    core->setTimeRate(0.0);
    m_jd0 = core->getJD();
    m_jdAccum = m_jd0;      // T15：累计推进起点
    m_lastSim = 0.0;

    m_host->setRenderCallback([&stelApp, core, this](double dt, double sim, const QSize &) {
        Q_UNUSED(sim);
        // T15：JD 由 pumpTick 逐 tick 累计（m_jdAccum），不再用闭式公式——
        // 暂停（scale=0）冻结、恢复无跳变。dt 仍是墙钟帧差（动画惯性照常）。
        core->setJD(m_jdAccum);
        stelApp.update(dt);
        stelApp.draw();
    });
    m_host->attachMailbox(mailbox);

    // ── 帧泵：QTimer 分片，全部在 GUI 线程 ─────────────────────────────────
    m_simClock.start();
    m_windowStart = 0.0;
    m_windowFrames = 0;
    const double fps = m_cfg.fps > 0.0 ? m_cfg.fps : 60.0;
    m_timer = new QTimer();
    m_timer->setInterval(qMax(1, int(1000.0 / fps)));
    QObject::connect(m_timer, &QTimer::timeout, m_timer, [this]() { pumpTick(); });
    m_timer->start();

    std::printf("LIVESKY: 帧泵已启动（%dx%d，名义 %.1f fps，JD 速率 %.4f 天/秒，起点 JD=%.5f）\n",
                m_cfg.renderSize.width(), m_cfg.renderSize.height(), fps,
                m_cfg.simRate, m_jd0);
    std::fflush(stdout);
    return true;
}

void LiveSkyRuntime::pumpTick()
{
    if (!m_host || !m_mainView)
        return;

    const double sim = m_simClock.elapsed() / 1000.0;
    // T15：仿真时间累计推进。dtWall 每 tick 必然推进（无论暂停与否），
    // 所以暂停后恢复不会把暂停期间的墙钟一次性补进 JD（scale 门挡住）。
    const double dtWall = sim - m_lastSim;
    m_lastSim = sim;
    if (m_simScale > 0.0)
        m_jdAccum += dtWall * m_cfg.simRate * m_simScale;
    QString error;
    m_mainView->glContextMakeCurrent();
    const bool ok = m_host->renderOneFrame(sim, &error);
    m_mainView->glContextDoneCurrent();

    ++m_windowFrames;
    if (!ok)
    {
        // 失败限流打印：前 5 次 + 此后每 300 次，避免日志被刷爆
        static quint64 logged = 0;
        if (++logged <= 5 || logged % 300 == 0)
        {
            std::fprintf(stderr, "LIVESKY: 帧失败 #%llu：%s\n",
                         static_cast<unsigned long long>(logged),
                         error.toUtf8().constData());
            std::fflush(stderr);
        }
    }

    // 1s 滑窗实测速率
    const double now = m_simClock.elapsed() / 1000.0;
    if (now - m_windowStart >= 1.0)
    {
        m_lastWindowFps = double(m_windowFrames) / (now - m_windowStart);
        m_windowStart = now;
        m_windowFrames = 0;
    }
}

void LiveSkyRuntime::stop()
{
    if (m_timer)
    {
        m_timer->stop();
        delete m_timer;
        m_timer = nullptr;
    }
    if (m_host)
    {
        // GL 资源必须在创建它们的上下文里销毁；且必须 current（见
        // LegacySkyHost::shutdown 对借用模式的处理——surface 在 doneCurrent 后为空，
        // 不先 makeCurrent 会跳过 destroyTargets 造成 GL 资源泄漏）。
        if (m_mainView && m_booted)
            m_mainView->glContextMakeCurrent();
        m_host->attachMailbox(nullptr);
        m_host->shutdown();
        if (m_mainView && m_booted)
            m_mainView->glContextDoneCurrent();
        m_host.reset();
    }
}

LiveSkyRuntime::Stats LiveSkyRuntime::stats() const
{
    Stats s;
    s.fps = m_lastWindowFps;
    if (m_host)
    {
        const LegacySkyHost::Stats &hs = m_host->stats();
        s.requested = hs.requested;
        s.published = hs.published;
        s.dropped = hs.droppedByMailbox;
        s.failed = hs.failed;
    }
    return s;
}

// ── IFrameProducer ────────────────────────────────────────────────────────────
// 调速：改 QTimer 间隔即改名义产帧节奏。**仅 GUI 线程**（QTimer 亲和性），
// 与 LiveFrameSource 的跨线程原子调速不同——调用方（DynFrameCheck）在 GUI 线程
// 定时器内调用，对本实现合法。名义值只决定 QTimer 间隔；实际速率恒以实测为准。
void LiveSkyRuntime::setFps(double fps)
{
    const double eff = fps > 0.0 ? fps : 60.0;
    m_cfg.fps = eff;
    if (m_timer)
        m_timer->setInterval(qMax(1, int(1000.0 / eff)));
}

// ── ISimPacing（T15：AppFacade 暂停/继续 + 速率的落点）────────────────────────
void LiveSkyRuntime::setSimScale(double scale)
{
    if (scale == m_simScale)
        return;   // 幂等闸（与 AppFacade::setSimulationPaused 的门双重保险）
    m_simScale = qMax(0.0, scale);   // 负值视为暂停（0），不允许倒放经由本接口
    std::printf("LIVESKY: simScale -> %.3f\n", m_simScale);
    std::fflush(stdout);
}

void LiveSkyRuntime::setSimRate(double rate)
{
    if (rate == m_cfg.simRate)
        return;
    // 负速率 = 倒放，引擎支持（时间倒退时 LegacySkyHost 的 dt 钳制保护上层积分）；
    // 这里不做符号限制，只防离谱值。
    m_cfg.simRate = rate;
    std::printf("LIVESKY: simRate -> %.6f 天/秒\n", m_cfg.simRate);
    std::fflush(stdout);
}

ProducerCounters LiveSkyRuntime::counters() const
{
    const Stats s = stats();
    ProducerCounters c;
    // "已进邮箱的帧"口径：published（不是 requested——requested 含被邮箱拒收的 tick）。
    // 这与 LiveFrameSource 的 rendered 语义一致（那边渲染成功即投递成功）。
    c.rendered = s.published;
    c.failed = s.failed;
    c.fps = s.fps;
    return c;
}

} // namespace stelapp

#endif // STELQUICK_HAS_ENGINE && STELQUICK_WIDGETS_HOST
