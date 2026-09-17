/*
 * ViewportState — 视口状态值对象（骨架，未实现）。
 *
 * 职责：携带逻辑尺寸、物理尺寸、DPR、可绘区域、相机状态，以及
 *       视口世代（viewportGeneration）与帧/状态世代，供输入校验。
 * 禁止：不把屏幕像素与 QML 逻辑像素混用（所有字段必须写明单位）。
 *
 * 约束：必须是可复制的值结构（可在线程间安全传递），不含引擎指针、不含 GL/Vulkan 类型。
 * 输入事件携带 frame/state ID；世代变化后拒绝旧坐标（测试 Q-006/Q-007、U-FRM-04）。
 */
#pragma once

#include <QtGlobal>
#include <QSizeF>

namespace stelapp {

struct ViewportState
{
    // 世代：缩放或尺寸变化时递增；输入携带的世代不匹配即丢弃。
    quint32 viewportGeneration = 0;

    QSizeF logicalSize;   // QML 逻辑像素
    QSizeF physicalSize;  // 设备像素；约束 logicalSize * devicePixelRatio == physicalSize
    qreal devicePixelRatio = 1.0;
    QRectF paintArea;     // 可绘区域（逻辑像素），天空纹理映射目标

    // 相机/视场快照：由 AppFacade 填充，只读传递。
    double fieldOfViewDegrees = 60.0;

    bool isValid() const
    {
        return !logicalSize.isEmpty() && devicePixelRatio > 0.0
            && qFuzzyCompare(logicalSize.width() * devicePixelRatio, physicalSize.width())
            && qFuzzyCompare(logicalSize.height() * devicePixelRatio, physicalSize.height());
    }
};

} // namespace stelapp
