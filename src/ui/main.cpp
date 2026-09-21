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
 * 退出码：0 正常；2 窗口创建失败；3 后端校验失败（实际 API 非 Vulkan）；4 交互自测失败；
 *         5 A2 静态图校验失败；6 A2 校验手段不可用（不得据此声称通过）。
 *
 * 运行：直接双击 stelQuickUI.app 即可（main.cpp 自动定位 Vulkan 加载库）。
 */
#include <QGuiApplication>
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
#include "ui/quick/SkyViewport.hpp"

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

int main(int argc, char **argv)
{
    // 0. macOS 渲染兼容开关必须在创建任何窗口之前生效
    applyMacOsVulkanWorkaround();

    // 1. 必须在创建任何窗口之前显式选择后端（计划一第 2 节）
    //
    // 默认强制 Vulkan，且失败不静默回退（A1 验收要求）。
    // STELQUICK_GRAPHICS_API 只用于**诊断对照**：把同一份场景换成别的后端跑，
    // 用来隔离"Vulkan 专用缺陷"与"通用渲染缺陷"。正常验收不得使用。
    //   metal  — macOS 对照组（MoltenVK 之外的原生 Metal RHI）
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

    QGuiApplication app(argc, argv);
    app.setApplicationName("stelQuickUI");
    app.setOrganizationName("stellarium-vulkan");

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
    std::printf("STELQUICK: probe ok=%d device=%s api=%s driver=%s portability=%d err=%s\n",
                probeResult.ok ? 1 : 0,
                probeResult.deviceName.toUtf8().constData(),
                probeResult.apiVersion.toUtf8().constData(),
                probeResult.driverVersion.toUtf8().constData(),
                probeResult.portabilityDriver ? 1 : 0,
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

    const bool a2Check = qEnvironmentVariableIsSet("STELQUICK_A2_CHECK");
    // 起始页：A2 校验必须停在天空页；手动模式下可用 STELQUICK_PAGE 指定
    const QString startPage = a2Check ? QStringLiteral("sky")
                                      : qEnvironmentVariable("STELQUICK_PAGE", QStringLiteral("diag"));

    // 3. 加载 QML
    QQmlApplicationEngine engine;
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

    // 手动查看模式：投递一次静态测试图案，便于人眼确认通路已通。
    // 说明：静态帧源只投一次，窗口后续缩放不会重新生成图案（图案会被缩放显示）；
    // A2 主体接入真实产帧后，此段整体删除。
    //
    // 2026-09-21 Windows 定位：原先固定 300ms 就投递，但此时 QML 布局尚未跑完
    // （StackLayout 还没给视口分配尺寸）→ 逻辑 0x0 → 报"视口物理尺寸过小…1x1"。
    // 这个抖动一直以"假失败日志"形式存在（不影响 A2 模式，那边自己带等待）。
    // 处置：改成轮询等视口拿到有效尺寸再投递，最多等 5s；超时才报真失败。
    if (skyViewport) {
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
    return backendOk ? rc : 3;
}
