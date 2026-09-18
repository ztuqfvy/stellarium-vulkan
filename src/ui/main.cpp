/*
 * stelQuickUI — A1 阶段主程序：Vulkan QML 宿主 + 诊断页。
 *
 * 启动顺序（计划一 A1 硬性要求）：
 *   1. 创建任何窗口之前显式 QQuickWindow::setGraphicsApi(Vulkan)。
 *   2. VkDeviceProbe 探针枚举物理设备（诊断数据）。
 *   3. 加载 QML，场景图初始化后读实际 API；非 Vulkan 则 backendOk=false，
 *      延迟数秒让诊断页显示错误后以退出码 3 结束——禁止静默回退 Metal/GL。
 *
 * 自动化验收（测试文档 P-LIF 系列前置）：
 *   STELQUICK_AUTOTEST_SECONDS=N 环境变量 → N 秒后自动退出（返回码见下）。
 * 退出码：0 正常；2 窗口创建失败；3 后端校验失败（实际 API 非 Vulkan）。
 *
 * 运行前置（docs/BUILD_RECORD.zh_CN.md）：
 *   export VK_DRIVER_FILES=/opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json
 *   export QT_VULKAN_LIB=/opt/homebrew/opt/vulkan-loader/lib/libvulkan.1.dylib
 */
#include <QGuiApplication>
#include <QFileInfo>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QTimer>
#include <QUrl>
#include <QVulkanInstance>
#include <cstdio>

namespace {

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

} // namespace

int main(int argc, char **argv)
{
    // 1. 必须在创建任何窗口之前显式选择后端（计划一第 2 节）
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Vulkan);

    QGuiApplication app(argc, argv);
    app.setApplicationName("stelQuickUI");
    app.setOrganizationName("stellarium-vulkan");

    // 2. Vulkan 实例预检（负向测试 P-CFG 前置）：QVulkanInstance::create() 失败时
    //    必须明确报错并以退出码 3 结束。跳过此步时 Qt 6.11 会在场景图初始化阶段
    //    SIGSEGV（实测 2026-09-18），拿不到诊断页显示机会。
    ensureVulkanLoaderPath(); // 先自动定位加载库，避免"必须手动 export 才能跑"

    QVulkanInstance vulkanInstance;
    if (!vulkanInstance.create()) {
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
    std::printf("STELQUICK: probe ok=%d device=%s api=%s driver=%s err=%s\n",
                probeResult.ok ? 1 : 0,
                probeResult.deviceName.toUtf8().constData(),
                probeResult.apiVersion.toUtf8().constData(),
                probeResult.driverVersion.toUtf8().constData(),
                probeResult.error.toUtf8().constData());
    std::fflush(stdout);
#else
    backendInfo->applyProbeUnavailable(
        QStringLiteral("本构建未启用 VkDeviceProbe（STELQUICK_VULKAN_PROBE=OFF，鸿蒙交叉编译）"));
#endif
    qmlRegisterSingletonInstance("StelQuickUI", 1, 0, "BackendInfo", backendInfo);

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

    // 预检通过的实例挂到窗口，避免 "No QVulkanInstance set" 崩溃路径
    window->setVulkanInstance(&vulkanInstance);

    // 场景图初始化后校验实际 API（后端必须在创建窗口前选定，此处只能确认）
    bool backendOk = false;
    QObject::connect(window, &QQuickWindow::sceneGraphInitialized, window,
                     [&window, backendInfo, &backendOk]() {
                         QSGRendererInterface *rif = window->rendererInterface();
                         const auto api = rif ? rif->graphicsApi()
                                              : QSGRendererInterface::Unknown;
                         const QString name = QString::fromUtf8(apiName(api));
                         backendInfo->applyRuntimeApi(name);
                         backendOk = backendInfo->backendOk();

                         std::printf("STELQUICK: runtimeApi=%s backendOk=%d device=%s\n",
                                     name.toUtf8().constData(), backendOk ? 1 : 0,
                                     backendInfo->deviceName().toUtf8().constData());
                         std::fflush(stdout);
                         if (!backendOk) {
                             std::fprintf(stderr,
                                          "A1 失败：请求 Vulkan，实际 %s。禁止静默回退，程序将退出。\n",
                                          name.toUtf8().constData());
                         }
                     });

    window->show();

    // 自动验收模式：N 秒后自动退出，返回码反映后端判定
    const int autoSeconds = qEnvironmentVariableIntValue("STELQUICK_AUTOTEST_SECONDS");
    if (autoSeconds > 0) {
        QTimer::singleShot(autoSeconds * 1000, &app, &QCoreApplication::quit);
    }

    const int rc = app.exec();
    return backendOk ? rc : 3;
}
