/*
 * A1 预验证：Qt Quick Vulkan 后端诊断（手动编译运行，不进主构建）。
 * 通过条件（计划一 A1）：实际 API == Vulkan；禁用 Vulkan 时明确报错退出（不静默回退）。
 * 构建：
 *   clang++ -std=c++17 -fPIC a1_backend_diag.cpp \
 *     -I/opt/homebrew/opt/qt/lib/QtQuick.framework/Headers \
 *     -I/opt/homebrew/opt/qt/lib/QtGui.framework/Headers \
 *     -I/opt/homebrew/opt/qt/lib/QtCore.framework/Headers \
 *     -I/opt/homebrew/opt/qt/share/qt/mkspecs/macx-clang \
 *     -F/opt/homebrew/opt/qt/lib -framework QtQuick -framework QtGui -framework QtCore \
 *     -o a1_backend_diag
 * 运行前导出 ICD：
 *   export VK_DRIVER_FILES=/opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json
 *   ./a1_backend_diag vulkan    # 期望打印 API=Vulkan
 *   ./a1_backend_diag opengl    # 对照：打印 API=OpenGL 并以非零退出
 */
#include <QGuiApplication>
#include <QQuickWindow>
#include <QQuickView>
#include <QTimer>
#include <QSGRendererInterface>
#include <cstdio>

int main(int argc, char **argv)
{
    const QByteArray want = argc > 1 ? QByteArray(argv[1]) : QByteArray("vulkan");

    // 必须在创建窗口前显式选择后端（计划一 2 节：后端要在创建窗口前选定）
    if (want == "vulkan")
        QQuickWindow::setGraphicsApi(QSGRendererInterface::Vulkan);
    else if (want == "opengl")
        QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
    else if (want == "metal")
        QQuickWindow::setGraphicsApi(QSGRendererInterface::Metal);

    QGuiApplication app(argc, argv);

    QQuickView view;
    view.setSource(QUrl("qrc:/empty.qml")); // 无实际场景也行；用最小内联场景
    // 若 qml 文件缺失，继续用空窗口即可——graphicsApi 在 scene graph 初始化后可读

    // 场景图初始化后读取实际 API
    QObject::connect(&view, &QQuickWindow::sceneGraphInitialized, [&view, &want]() {
        QSGRendererInterface *rif = view.rendererInterface();
        const QSGRendererInterface::GraphicsApi api = rif
            ? rif->graphicsApi()
            : QSGRendererInterface::Unknown;
        const char *name = "Unknown";
        switch (api) {
        case QSGRendererInterface::Vulkan: name = "Vulkan"; break;
        case QSGRendererInterface::OpenGL: name = "OpenGL"; break;
        case QSGRendererInterface::Metal:  name = "Metal"; break;
        case QSGRendererInterface::Software: name = "Software"; break;
        default: name = "Unknown"; break;
        }
        std::printf("REQUESTED=%s  ACTUAL=%s\n", want.constData(), name);
        // A1 通过条件：请求 Vulkan 就必须实际 Vulkan，禁止静默回退
        const bool ok = (want == "vulkan" && api == QSGRendererInterface::Vulkan)
                     || (want != "vulkan");
        std::printf("VERDICT=%s\n", ok ? "PASS" : "FAIL");
        QTimer::singleShot(200, qGuiApp, &QCoreApplication::quit);
    });

    view.resize(320, 200);
    view.show();
    QTimer::singleShot(8000, qGuiApp, &QCoreApplication::quit); // 兜底超时
    return app.exec();
}
