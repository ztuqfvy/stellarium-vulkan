/*
 * FrameMailbox — 有界 CPU 帧中转邮箱（A2 实现）。
 *
 * 职责：旧 GL 宿主（生产者）与 Qt Quick 渲染线程（消费者）之间传递不可变天空帧快照。
 *   - 3 个槽位（kFrameSlotCount），只保留最新完整帧；忙时丢旧帧，不积压。
 *   - 帧元数据：帧编号、状态编号、尺寸、格式、行步长、色彩、方向、世代。
 * 禁止：生产者不得覆盖消费者正在读取的数据。
 *
 * 线程模型：
 *   生产者 THREAD: gl-ctx    （旧 GL 上下文所属线程读回帧后投递）
 *   消费者 THREAD: scenegraph（场景图线程 takeLatestFrame 后上传纹理）
 * 实现顺序：A2（先静态图，后动态天空）。测试用例：U-FRM-01..05、P-BRG-01..04。
 *
 * 实现说明（2026-09-20）：
 *   - 互斥锁只保护**簿记**（槽位状态、序号、引用计数）；像素拷贝在锁外进行，
 *     期间槽位处于 kWriting，生产者与消费者都会跳过它，因此不需要持锁做 memcpy。
 *   - 同一时刻**只允许一个生产者线程**（契约如此：旧 GL 上下文只有一个）。
 *     若将来出现多生产者，需在 kWriting 的选取上加独占标记。
 *   - 尺寸世代（sizeGeneration）变化时，旧世代帧一律拒收；消费者已持有的租约不受影响。
 */
#pragma once

#include <QtGlobal>
#include <QMutex>
#include <QSize>
#include <QVector>
#include <atomic>
#include <functional>

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

class FrameMailbox;

// ── 租约（RAII）──────────────────────────────────────────────────────────
// 消费者持有租约期间，该槽位不会被生产者复用；析构即归还。
// 生命周期约束：FrameMailbox 必须比所有租约活得更久。
class FrameLease
{
public:
    FrameLease() = default;
    ~FrameLease() { reset(); }
    FrameLease(const FrameLease &) = delete;
    FrameLease &operator=(const FrameLease &) = delete;
    FrameLease(FrameLease &&other) noexcept;
    FrameLease &operator=(FrameLease &&other) noexcept;

    bool valid() const { return m_mailbox != nullptr && m_slot >= 0; }
    // valid() 为 false 时返回零值帧（frameNumber == 0），调用方无需特判。
    const LegacyFrame &frame() const;
    void reset();

private:
    friend class FrameMailbox;
    FrameLease(FrameMailbox *mailbox, int slot) : m_mailbox(mailbox), m_slot(slot) {}
    FrameMailbox *m_mailbox = nullptr;
    int m_slot = -1;
};

// ── 邮箱 ─────────────────────────────────────────────────────────────────
class FrameMailbox
{
public:
    static constexpr int kFrameSlotCount = 3;

    FrameMailbox() = default;
    ~FrameMailbox();
    FrameMailbox(const FrameMailbox &) = delete;
    FrameMailbox &operator=(const FrameMailbox &) = delete;

    // 帧到达回调（唤醒消费者重绘）。
    // 契约：**装配期设置一次，运行期不再变更**（因此读取无需加锁）；
    // 回调在**生产者线程**上被调用，实现必须自己不碰 QML 场景图对象（通常做法是投递队列事件）。
    // 消费者（SkyViewport）在析构时必须清空，避免回调打到已销毁对象上。
    using FrameAvailableCallback = std::function<void()>;
    void setFrameAvailableCallback(FrameAvailableCallback callback);

    // 生产者 THREAD: gl-ctx。投递完整帧（元数据 + 像素）。
    // 返回值的语义：
    //   true  — 已投递（可能覆盖了更旧的完整帧，属设计内行为）
    //   false — 帧被丢弃：无可用槽位（全部在用/写入中）、世代不符、或帧序号不新
    // 像素格式必须与 LegacyFrame 注释中的契约一致；byteCount 必须等于
    // physicalSize.height() * rowStride。
    bool publishFrame(const LegacyFrame &source, const quint8 *pixels, qsizetype byteCount);

    // 生产者 THREAD: gl-ctx。尺寸变化时调用：递增世代并废弃所有旧世代槽位。
    // 返回新的 sizeGeneration，生产者应把该值写入后续帧。
    quint32 bumpSizeGeneration();

    // 消费者 THREAD: scenegraph。取最新完整帧；无完整帧时返回无效租约。
    FrameLease takeLatestFrame();

    // 诊断（任意线程）：最新完整帧的帧编号。
    quint64 latestCompletedFrameNumber() const;

    struct Stats
    {
        quint64 published = 0;        // 成功投递次数
        quint64 dropped = 0;          // 丢弃次数（U-FRM-01/05）
        quint64 leased = 0;           // 被消费者取走的次数
        quint32 sizeGeneration = 0;   // 当前尺寸世代
        int completeSlots = 0;        // 当前完整槽位数（≤ kFrameSlotCount，即"队列长度有界"）
        int readersHeld = 0;          // 正被读取的槽位数
        qint64 latestFrameAgeMs = -1; // 最新完整帧的年龄（毫秒），无完整帧为 -1
        qsizetype bytesPerFrame = 0;  // 每帧字节数（内存占用依据）
    };
    Stats stats() const;

private:
    friend class FrameLease;
    void releaseSlot(int slot);

    struct Slot
    {
        LegacyFrame frame;
        QVector<quint8> buffer;
        FrameState state = FrameState::kEmpty;
        int readers = 0;
        qint64 publishMs = 0;
    };

    // 必须在持锁状态下调用；返回 -1 表示无可用槽位。
    // 选取顺序：空槽位 > 最旧的完整且无人读取的槽位；跳过 kWriting 与 readers > 0。
    int pickWritableSlotLocked() const;

    mutable QMutex m_mutex;
    Slot m_slots[kFrameSlotCount];
    quint64 m_published = 0;
    quint64 m_dropped = 0;
    quint64 m_leased = 0;
    quint32 m_sizeGeneration = 1;
    FrameAvailableCallback m_onFrameAvailable;   // 装配期设置一次，运行期只读
};

} // namespace stelapp
