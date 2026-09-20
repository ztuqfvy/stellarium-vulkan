/*
 * StaticFrameSource — A2 静态图阶段的帧源（**开发期替代物**，2026-09-20）。
 *
 * 定位：A2 主体的生产者是"旧 GL 宿主读回帧"。本类只负责生成一张已知内容的测试图案并投递到
 * FrameMailbox，用来把两个风险拆开：
 *   ① 帧桥接口本身是否正确（元数据、世代、行步长、纹理上传、方向/通道/DPR）—— 现在就能证伪；
 *   ② 旧 GL 宿主能否被可靠离屏驱动 —— A2 主体才验证，是真正的未知数。
 * A2 主体完成后，本类应被 LegacySkyHost 的读回路径替换，或仅保留在测试构建中。
 *
 * 禁止：本类不得被 QML 或 src/ui/qml/ 引用；它只出现在 C++ 装配点与测试里。
 */
#pragma once

#include <QSize>
#include <QString>

namespace stelapp {

class FrameMailbox;

class StaticFrameSource
{
public:
    // 逻辑尺寸 × DPR = 物理尺寸。全工程唯一的换算入口，避免 DPR 少乘/多乘
    // （少乘一次正是"图能显示但模糊/错位"这类难查问题的来源）。
    static QSize physicalSizeFor(const QSize &logicalSize, qreal devicePixelRatio);

    // 生成测试图案并投递一帧。失败原因写入 errorOut（如有）。
    static bool publishTestPattern(FrameMailbox &mailbox,
                                  const QSize &logicalSize,
                                  qreal devicePixelRatio,
                                  quint64 frameNumber,
                                  quint64 stateNumber,
                                  QString *errorOut = nullptr);
};

} // namespace stelapp
