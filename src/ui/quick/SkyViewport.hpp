/*
 * SkyViewport — QML 天空视口项（A2 实现：静态帧通路）。
 *
 * 职责：在 QML 场景中显示天空帧、向引擎转发手势、提供 backend/ready/error 状态。
 * 禁止：QML 页面不关心天空来自 CPU 帧还是 VkImage（计划二只换数据来源，本项接口不变）。
 *
 * 渲染路径（A 阶段）：场景图线程（THREAD: scenegraph）从 FrameMailbox 取最新帧，
 * 经 createTextureFromImage() 上传为 QSGTexture，由 QSGImageNode 绘制到本项矩形。
 * 后端要求：窗口创建前显式 QQuickWindow::setGraphicsApi(Vulkan)；场景图初始化后
 * 检查实际 API，非 Vulkan 报错退出（禁止静默回退 Metal/GL）。
 *
 * 线程约束（开发指导文档第 4 节第 3 条）：
 *   - updatePaintNode 运行在场景图线程，只能读邮箱快照，**禁止**访问任何 Stellarium 模块。
 *   - m_texture / m_uploadedFrameNumber 只在场景图线程读写，GUI 线程不得触碰。
 *   - frameNumber 通过原子量暴露给 GUI 线程显示，属只读诊断信息。
 *
 * 实现顺序：A2（静态图）→ A2（动态帧）。测试用例：Q-001..003、I-STC-01..02、I-DYN-01..04。
 */
#pragma once

#include <QQuickItem>
#include <QString>
#include <atomic>

class QSGImageNode;
class QSGTexture;

namespace stelapp {

class FrameMailbox;

class SkyViewport : public QQuickItem
{
    Q_OBJECT
    // 后端状态：vulkan / error。非 Vulkan 后端一律 error，不得假装成功。
    Q_PROPERTY(QString backend READ backend NOTIFY backendChanged)
    Q_PROPERTY(bool ready READ ready NOTIFY readyChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY backendFailed)
    // 视口世代：resize/缩放时递增；手势事件携带世代，过期事件被拒绝。
    Q_PROPERTY(quint32 viewportGeneration READ viewportGeneration NOTIFY viewportGenerationChanged)
    // 已上传的帧编号（诊断用，只读；由场景图线程更新）
    Q_PROPERTY(quint64 displayedFrameNumber READ displayedFrameNumber NOTIFY displayedFrameNumberChanged)

public:
    explicit SkyViewport(QQuickItem *parent = nullptr);
    ~SkyViewport() override;

    QString backend() const { return m_backend; }
    bool ready() const { return m_ready; }
    QString errorMessage() const { return m_errorMessage; }
    quint32 viewportGeneration() const { return m_viewportGeneration; }
    quint64 displayedFrameNumber() const { return m_displayedFrameNumber.load(); }

    // 装配点注入（A2 权限：由 main.cpp 在窗口构造后调用一次；A3 起改由 AppFacade 持有）。
    // GUI 线程调用。传 nullptr 表示解绑。
    void setFrameMailbox(FrameMailbox *mailbox);
    FrameMailbox *frameMailbox() const { return m_mailbox; }

    // 后端判定（GUI 线程调用一次，来自 main.cpp 的场景图 API 校验结果）。
    void applyBackendResult(const QString &runtimeApiName, bool isVulkan);

signals:
    void backendChanged(QString backend);
    void readyChanged(bool ready);
    void backendFailed(QString message);
    void viewportGenerationChanged(quint32 generation);
    void displayedFrameNumberChanged(quint64 frameNumber);

protected:
    QSGNode *updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *data) override;
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;

private:
    QString m_backend;
    bool m_ready = false;
    QString m_errorMessage;
    quint32 m_viewportGeneration = 0;

    FrameMailbox *m_mailbox = nullptr;

    // ↓ 以下三项仅场景图线程访问
    QSGTexture *m_texture = nullptr;
    QSGTexture *m_dummyTexture = nullptr;   // 邮箱未投帧时的 1x1 占位，防止空纹理材质
    quint64 m_uploadedFrameNumber = 0;

    std::atomic<quint64> m_displayedFrameNumber{0};
};

} // namespace stelapp
