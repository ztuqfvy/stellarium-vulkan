// LiveSkyRuntime 实现。线程约束见头文件注释。
#include "ui/LiveSkyRuntime.hpp"

#if defined(STELQUICK_HAS_ENGINE) && defined(STELQUICK_WIDGETS_HOST)

#include "render/legacy/FrameMailbox.hpp"

// T36：个人版配置目录隔离 + 首次播种（唯一调用点，见下方 boot()）
#include "app/ConfigIsolation.hpp"

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
#include <cmath>
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
    // T36：**位置是判据的一部分** —— 必须紧跟 init()、且在 StelLogger 与
    // `findFile("config.ini")` **之前**。它把用户目录切到个人版目录
    // （`<引擎默认目录>-quick`）并做首次播种。晚一步，日志与配置就落到
    // **原版 Stellarium 的目录**里（实测写穿，见 app/ConfigIsolation.hpp 头注）。
    bootstrapPersonalConfigDir();
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
    // T36：把**实际解析到**的配置文件路径也报出来 —— 这是"隔离真的生效了"的
    // 直接读数（自检另有 `QSettings::fileName()` 的独立回读路径，不复述这一行）。
    std::printf("CONFIGISO: config=%s\n", qPrintable(configFileFullPath));
    std::fflush(stdout);
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
    // ── T16：接管仿真时钟 ────────────────────────────────────────────────────
    // 顺序要紧：
    //   ① setSimClockHostDriven(true) —— 以引擎当前 JD 锚定起点并切到宿主驱动。
    //      此后 updateTime 不再读墙钟（取代 T15 的 setTimeRate(0) 架空方案：
    //      不再是"用速率 0 把墙钟乘没"，而是"墙钟路径根本不执行"）。
    //   ② setTimeRate(simRate) —— 速率复用引擎既有字段，插件零改造：
    //      插件调 setTimeRate 在宿主驱动下自然变成"宿主按该速率推进"。
    //   ③ setSimClockScale(1.0) —— 新实例起步必为运行态。
    //   ④ 最后才起帧泵（帧泵是唯一推进源，起之前推进量为 0）。
    core->setSimClockHostDriven(true);
    // T16：同一时刻只能有一个 update 驱动源。旧宿主帧节拍器必须在**接管那一刻**停掉。
    // 实测（AC-7 首跑）：boot() 的 show() 会点燃旧 QWidget 绘制链，fpsTimer 确实在跑
    // （且它没有日志，光看日志会误判成"从未执行"）——即合流形态此前一直是双驱动源。
    // 只靠 drawEnded() 内的"不自启"守卫拦不住已经活着的实例，故这里显式停表。
    m_mainView->stopLegacyFrameTimer();
    core->setTimeRate(m_cfg.simRate);
    core->setSimClockScale(1.0);
    m_core = core;
    m_jd0 = core->getSimClockJD();

    m_host->setRenderCallback([&stelApp, core](double dt, double sim, const QSize &) {
        Q_UNUSED(sim);
        // T16：宿主只负责**推进**，不再直接写时钟值（T15 是 core->setJD(m_jdAccum)）。
        // JD 真源在 core 的仿真时钟里；updateTime 会从那里取，且与插件/脚本的
        // core->setJD 写的是同一个值——外部跳转不再被下一帧覆盖。
        //
        // ── T35 三组负控（**只用于证明 TimeLinkCheck 的判据承重**；正题恒 false）──
        //   A `STELQUICK_TIMELINK_BREAK=1`：帧泵按**半速**推进 ⇒ 推进量与墙钟脱钩
        //     ⇒ 只有 TL-01（链路自洽）该红；比值型判据 TL-02/03/05 整体缩放不变、
        //     TL-07 是恒等式（ΔJD 与 ΔLST 一起缩放）。TL-04 也**刻意保持绿**：
        //     它的职责是"冻结必须真零 + 不许补"，不是"比值必须=1"（那是 TL-01 的活）。
        //     ⚠️ 因此 TL-04 的带宽下界必须**远离 0.5**：A/B 两组负控在该窗口的
        //     比值都恰好是 0.5，下界写 0.5 会让红/绿随计时噪声漂（实测 0.4990 ↔ 0.5002）。
        //   B `STELQUICK_TIMELINK_RATE_IGNORED=1`：链路用**钉死的 0.1 天/秒**推进，
        //     而 `core->getTimeRate()` 仍返回用户设的值 —— 这正是 T27 证据 README
        //     里假设过的那个缺陷形态（"帧推进走固定步长，rate 只管别的语义"），
        //     本轮把它**实现出来**当负控，好让"rate 真的进了链路"这条腿可被证伪。
        //   C `STELQUICK_TIMELINK_FREEZE_LEAK=1`：scale=0（冻结）期间**照旧推进**
        //     ——做法是"只在推进的那一瞬间把 scale 解成 1，推完立刻写回 0"，所以
        //     `getSimClockScale()` 的**读数**仍然是 0（冻结看起来还在），链路却漏了。
        //     这正是 TL-04 冻结腿要守的东西："scale 真的进链路"。
        //     ⇒ 只有 TL-04 该红（冻结窗 ΔJD = 0.5·W·rate ≠ 0），其余窗口都在冻结之前。
        //     ⚠️ 为什么不用"解冻后把冻结期一次性补回来"当负控？实测不可靠：解冻后
        //     检查还要等 300ms 才 `arm()`，补的那一脚恰好落在开窗**之前**（比值
        //     仍是 0.9947，判据照绿 —— 负控形状对、落点不对）。冻结漏推进没有
        //     落点问题，且同样是真实缺陷形态。
        static const bool tlBreak = qEnvironmentVariableIsSet("STELQUICK_TIMELINK_BREAK");
        static const bool tlRateIgnored =
            qEnvironmentVariableIsSet("STELQUICK_TIMELINK_RATE_IGNORED");
        static const bool tlFreezeLeak =
            qEnvironmentVariableIsSet("STELQUICK_TIMELINK_FREEZE_LEAK");
        const double advanceDt = tlBreak ? dt * 0.5 : dt;
        if (tlRateIgnored)
        {
            const double keep = core->getTimeRate();
            if (std::fabs(keep - 0.1) > 1e-12)
            {
                core->setTimeRate(0.1);          // 注入：链路 rate 钉死
                core->advanceSimClock(advanceDt);
                core->setTimeRate(keep);         // 还原读数，制造"读数与链路脱钩"
            }
            else
                core->advanceSimClock(advanceDt);
        }
        else if (tlFreezeLeak && core->getSimClockScale() == 0.0)
        {
            core->setSimClockScale(1.0);         // 注入：推进的瞬间解开 scale
            core->advanceSimClock(advanceDt * 0.5);
            core->setSimClockScale(0.0);         // 读数写回 0 ⇒ "冻结"看着还在
        }
        else
            core->advanceSimClock(advanceDt);
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

    std::printf("LIVESKY: 帧泵已启动（%dx%d，名义 %.1f fps，JD 速率 %.4f 天/秒，起点 JD=%.5f，时钟=%s）\n",
                m_cfg.renderSize.width(), m_cfg.renderSize.height(), fps,
                m_cfg.simRate, m_jd0, qPrintable(core->getSimClockModeName()));
    std::fflush(stdout);
    return true;
}

void LiveSkyRuntime::pumpTick()
{
    if (!m_host || !m_mainView)
        return;

    // T16：本函数不再推进 JD（推进在渲染回调内 core->advanceSimClock(dt)，
    // 用 LegacySkyHost 算好的墙钟帧差）。此处只负责节拍与计量。
    const double sim = m_simClock.elapsed() / 1000.0;
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

// ── ISimPacing（T15/T16：AppFacade 暂停/继续 + 速率的落点）────────────────────
// T16：以下四个方法都是对**引擎仿真时钟**的投影——本类不持有时钟状态。
// 好处：无论经 AppFacade 还是经插件/脚本写时钟，落点都是同一个 StelClockController，
// 不会出现"宿主以为暂停了、插件把速率改回去了"这类双份状态互相打架。
void LiveSkyRuntime::setSimScale(double scale)
{
    if (!m_core)
        return;
    m_core->setSimClockScale(scale);   // 幂等闸在 StelCore 内（与 AppFacade 的门双重保险）
    std::printf("LIVESKY: simScale -> %.3f（引擎仿真时钟）\n", m_core->getSimClockScale());
    std::fflush(stdout);
}

double LiveSkyRuntime::simScale() const
{
    return m_core ? m_core->getSimClockScale() : 1.0;
}

void LiveSkyRuntime::setSimRate(double rate)
{
    if (!m_core)
        return;
    // 速率复用引擎既有 timeSpeed：负速率 = 倒放，引擎支持（时间倒退时
    // LegacySkyHost 的 dt 钳制保护上层积分）；暂停请用 scale，不要用速率 0。
    m_core->setTimeRate(rate);
    std::printf("LIVESKY: simRate -> %.6f 天/秒（引擎仿真时钟）\n", m_core->getTimeRate());
    std::fflush(stdout);
}

double LiveSkyRuntime::simRate() const
{
    // 真源在引擎。仅在 start() 之前（m_core 未接线）回落到启动配置值。
    return m_core ? m_core->getTimeRate() : m_cfg.simRate;
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
