/*
 * FrameMailbox — 有界 CPU 帧中转邮箱（骨架，未实现）。
 *
 * 职责：旧 GL 宿主（生产者）与 Qt Quick 渲染线程（消费者）之间传递不可变天空帧快照。
 *   - 2–3 个槽位，只保留最新完整帧；忙时丢旧帧，不积压。
 *   - 帧元数据：帧编号、状态编号、尺寸、格式、行步长、色彩、方向、世代。
 * 禁止：生产者不得覆盖消费者正在读取的数据。
 *
 * 线程模型：
 *   生产者 THREAD: gl-ctx    （旧 GL 上下文所属线程读回帧后投递）
 *   消费者 THREAD: scenegraph（场景图线程 takeLatestFrame 后上传纹理）
 * 实现顺序：A2（先静态图，后动态天空）。测试用例：U-FRM-01..05、P-BRG-01..04。
 */
#pragma once

#include <QtGlobal>
#include <QSize>

namespace stelapp {

enum class FrameState : quint8
{
    kEmpty = 0,     // 槽位无有效帧
    kWriting,       // 生产者写入中（消费者跳过）
    kComplete       // 完整可消费
};

// 单帧不可变快照。数据归邮箱所有，消费者使用期间邮箱保证不回收该槽位。
struct LegacyFrame
{
    quint64 frameNumber = 0;      // 帧编号（生产者单调递增）
    quint64 stateNumber = 0;      // 产生该帧时的引擎状态编号
    quint32 sizeGeneration = 0;   // 尺寸世代：宽高变化时递增，旧世代帧拒收
    QSize logicalSize;            // 天空内容逻辑尺寸
    QSize physicalSize;           // 像素尺寸（行步长与字节宽度的依据）
    quint32 rowStride = 0;        // 行步长（字节）
    // 格式契约（交接给计划二前冻结，见开发指导文档第 7 节）：
    //   RGBA8（字节序 R,G,B,A）、原点左上、行优先、非预乘 alpha、sRGB 色彩空间。
    // 指向槽位内存；不单独分配，由邮箱的槽位池管理。
    quint8 *pixels = nullptr;
};

class FrameMailbox
{
public:
    // 槽位数：kFrameSlotCount（2–3，实现时定稿）。
    // 生产者 THREAD: gl-ctx：投递最新帧；忙时覆盖最旧完整帧之外仍被读取者跳过。
    // void publishFrame(LegacyFrame frame);

    // 消费者 THREAD: scenegraph：取最新完整帧；使用期间邮箱不得回收该槽位。
    // 返回空 LegacyFrame（frameNumber==0）表示无新帧。
    // LegacyFrame takeLatestFrame();

    // 诊断统计（帧桥长跑 P-BRG-01..04 用）：丢帧数、帧年龄、队列长度、内存占用。
    // struct Stats { quint64 droppedFrames; double avgFrameAgeMs; double p95FrameAgeMs; ... };
    // Stats stats() const;

private:
    // 槽位池 + 世代锁/原子序号；实现要点：
    //   1. 生产者写 kWriting 槽位，写完置 kComplete 并更新最新序号。
    //   2. 消费者按最新序号取帧，使用期持有槽位引用计数，生产者跳过在用槽位。
    //   3. 尺寸世代变化时废弃全部旧槽位（拒收旧世代帧）。
};

} // namespace stelapp
