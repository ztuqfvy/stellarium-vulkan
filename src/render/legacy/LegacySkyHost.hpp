/*
 * LegacySkyHost — 旧 OpenGL 天空宿主的帧驱动（骨架，未实现）。
 *
 * 职责：把 StelMainView 中"GL 上下文内 update/draw 再交还 QPainter"的现有链路
 *   （StelMainView.cpp:398-431）隔离为可离屏驱动的天空专用宿主：
 *   1. 显式帧驱动：隐藏旧窗口不保证继续绘制，必须主动驱动 update/draw（禁止靠 paint 事件侥幸）。
 *   2. 取帧位置：StelRootItem::paint() 中天空及必要后处理结束后、旧工具栏绘制前。
 *   3. 读回 RGBA8 内存帧（禁止逐帧 PNG/JPEG 编码、禁止写图片文件、禁止变更图片 URL 的缓存技巧）。
 *   4. 把帧投递到 FrameMailbox。
 * 禁止：传递层接口不暴露 GLuint；不把这些调用搬到 QML 渲染线程。
 *
 * 线程：init/update/draw 全部在旧 GL 上下文所属线程（THREAD: gl-ctx）执行。
 * 备选：同进程原型失败时隔离为辅助进程（版本化协议 + 有界共享内存帧环，+1–3 工程周）。
 * 实现顺序：A2。验收：I-DYN-01..03、P-BRG 系列。
 */
#pragma once

#include <QObject>
#include <QSize>

namespace stelapp {

class FrameMailbox;

class LegacySkyHost : public QObject
{
    Q_OBJECT
public:
    explicit LegacySkyHost(QObject *parent = nullptr);

    // 绑定帧邮箱（生命周期：宿主先于邮箱析构；关闭顺序见开发指导文档第 4 节第 5 条）。
    // void attachMailbox(FrameMailbox *mailbox);

    // THREAD: gl-ctx
    // 请求一帧：驱动旧宿主 update/draw，取天空帧并读回到指定槽位。
    // 帧率调度由外部时钟控制（目标 720p@30fps，降级到 15fps 必须在 UI 明示）。
    // void renderOneFrame(const QSize &targetSize, quint32 sizeGeneration);

signals:
    // 一帧完整投递（frameNumber, sizeGeneration）。UI 可据此显示降级/健康状态。
    void framePublished(quint64 frameNumber, quint32 sizeGeneration);

private:
    // m_glContext、m_offscreenTarget、m_mailbox 等实现时补充。
};

} // namespace stelapp
