/*
 * SkyViewport — QML 天空视口项（骨架，未实现）。
 *
 * 职责：在 QML 场景中显示天空帧、向引擎转发手势、提供 backend/ready/error 状态。
 * 禁止：QML 页面不关心天空来自 CPU 帧还是 VkImage（计划二只换数据来源，本项接口不变）。
 *
 * 渲染路径（A 阶段）：场景图线程（THREAD: scenegraph）从 FrameMailbox 取最新帧，
 * 经 QQuickItem/QSGImageNode + createTextureFromImage() 验证，再按实测优化纹理更新。
 * 后端要求：窗口创建前显式 QQuickWindow::setGraphicsApi(Vulkan)；场景图初始化后
 * 检查实际 API，非 Vulkan 报错退出（禁止静默回退 Metal/GL）。
 *
 * 实现顺序：A2（静态图）→ A2（动态帧）。测试用例：Q-001..003、I-STC-01..02、I-DYN-01..04。
 */
#pragma once

#include <QQuickItem>

namespace stelapp {

class SkyViewport : public QQuickItem
{
    Q_OBJECT
    // 后端状态：vulkan / error。非 Vulkan 后端一律 error，不得假装成功。
    Q_PROPERTY(QString backend READ backend NOTIFY backendChanged)
    Q_PROPERTY(bool ready READ ready NOTIFY readyChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY backendFailed)
    // 视口世代：resize/缩放时递增；手势事件携带世代，过期事件被拒绝。
    Q_PROPERTY(quint32 viewportGeneration READ viewportGeneration NOTIFY viewportGenerationChanged)

public:
    explicit SkyViewport(QQuickItem *parent = nullptr);

    QString backend() const;
    bool ready() const;
    QString errorMessage() const;
    quint32 viewportGeneration() const;

    // QQuickItem::geometryChange / itemChange 覆写中更新 ViewportState 并递增世代。
    // 覆写 updatePaintNode（场景图线程）实现帧上传，仅访问邮箱快照，禁止触碰引擎模块。

signals:
    void backendChanged(QString backend);
    void readyChanged(bool ready);
    void backendFailed(QString message);
    void viewportGenerationChanged(quint32 generation);

protected:
    // QSGNode *updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *data) override;

private:
    QString m_backend;
    bool m_ready = false;
    QString m_errorMessage;
    quint32 m_viewportGeneration = 0;
};

} // namespace stelapp
