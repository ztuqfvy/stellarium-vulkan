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
#include <QFileInfo>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickWindow>
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
    // 起始页：A2/DYN/长跑校验必须停在天空页；手动模式下可用 STELQUICK_PAGE 指定
    const QString startPage = (a2Check || dynCheck || longRun
                               || qEnvironmentVariableIsSet("STELQUICK_LIVE")
                               || liveEngine)
                                  ? QStringLiteral("sky")
                                  : qEnvironmentVariable("STELQUICK_PAGE", QStringLiteral("diag"));

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
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("appFacade"), &appFacade);
    engine.rootContext()->setContextProperty(QStringLiteral("ActionRouter"), &actionRouter);
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
        backendInfo->applyRuntimeApi(name);
        // backendOk = 实际后端与请求后端一致（默认请求即 Vulkan）
        backendOk = (api == wantedApi);
        // 视口也要知道后端判定：非 Vulkan 时进入 error 态而不是假装 ready（Q-001）
        if (skyViewport)
            skyViewport->applyBackendResult(name, backendOk);

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
