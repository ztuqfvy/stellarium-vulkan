/*
 * LegacyTestScene — T6 用的"旧路径"GL 场景（**开发期替代物**，2026-09-21）。
 *
 * 定位：与 StaticFrameSource 同性质——把两个风险拆开。
 *   T6 要回答的是"旧宿主能否被**显式、离屏**驱动"，不是"Stellarium 的天空画得好不好"。
 *   因此这里用一条**不依赖引擎数据**的 GL 路径产出动态画面，先把驱动机制钉死；
 *   真实天空（StelApp::update()/draw()）在 A3 进程内集成时替换本类，见 04 号文档 §7。
 *
 * 它检验的 GL 面与旧宿主一致（不是"随便画个东西"）：
 *   - Core Profile 3.3 + VAO + VBO + 着色器程序 —— 与 StelOpenGL/StelPainter 同一套机制。
 *   - 逐帧重设 viewport、绑定自建 FBO、glReadPixels 读回 RGBA8。
 *   - 帧内容 = f(simSeconds) 的确定性函数（同一 simSeconds 必得同一像素）。
 *
 * 场景内容（三样都是刻意的，各承担一条判据）：
 *   1. 天空竖直渐变 + 昼夜色温：随 simSeconds 变化 → 判据"帧内容随仿真时间变化"。
 *   2. 太阳圆盘：水平位置随 simSeconds 单调推进 → 判据"不是静态残留"，
 *      且给出可量化的运动方向（探针质心）。
 *   3. **顶部绿色标记带 + 左侧红色标记带**：位置固定 → 用于**独立验证行序翻转与水平方向**。
 *      若读回后忘记翻转（或翻反），顶部带会跑到图像底部；本类自身不参与判断，
 *      判断由 LegacyHostCheck 对**邮箱投递后的帧**做——即验证的是最终交付数据，不是中间态。
 *
 * 禁止：本类不得被 QML 或 src/ui/qml/ 引用；不得作为生产渲染路径。
 */
#pragma once

#include <QSize>
#include <QString>
#include <memory>

namespace stelapp {

class LegacyTestScene
{
public:
    LegacyTestScene();
    ~LegacyTestScene();
    LegacyTestScene(const LegacyTestScene &) = delete;
    LegacyTestScene &operator=(const LegacyTestScene &) = delete;

    //! THREAD: gl-ctx。渲染一帧到"当前已绑定的 FBO / 当前 viewport"。
    //! 首次调用时惰性编译着色器与 VAO（此上下文已 current，资源归属正确）。
    //! 说明：本函数**不**清屏、**不**切换 FBO、**不**碰 glReadPixels——那些属于
    //! LegacySkyHost 的职责，场景只负责"往当前 target 上画"。
    void render(double simSeconds, const QSize &physicalSize);

    //! THREAD: gl-ctx（且上下文必须 current）。显式释放 GL 资源。
    void releaseResources();

    bool ok() const;
    QString lastError() const;

    struct Private;

private:
    std::unique_ptr<Private> d;
};

} // namespace stelapp
