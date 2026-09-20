// FrameMailbox 实现。线程模型与不变式见 FrameMailbox.hpp 顶部注释。
#include "render/legacy/FrameMailbox.hpp"

#include <QDateTime>
#include <QMutexLocker>
#include <cstring>
#include <limits>

namespace stelapp {

namespace {
LegacyFrame makeInvalidFrame()
{
    return LegacyFrame{};
}
} // namespace

// ── FrameLease ───────────────────────────────────────────────────────────

FrameLease::FrameLease(FrameLease &&other) noexcept
    : m_mailbox(other.m_mailbox), m_slot(other.m_slot)
{
    other.m_mailbox = nullptr;
    other.m_slot = -1;
}

FrameLease &FrameLease::operator=(FrameLease &&other) noexcept
{
    if (this != &other) {
        reset();
        m_mailbox = other.m_mailbox;
        m_slot = other.m_slot;
        other.m_mailbox = nullptr;
        other.m_slot = -1;
    }
    return *this;
}

const LegacyFrame &FrameLease::frame() const
{
    if (!valid())
        return makeInvalidFrame();
    // 租约持有期间槽位不会被生产者复用，元数据读取无需加锁
    // （生产者改动槽位前必先确认 readers == 0）。
    return m_mailbox->m_slots[m_slot].frame;
}

void FrameLease::reset()
{
    if (m_mailbox && m_slot >= 0)
        m_mailbox->releaseSlot(m_slot);
    m_mailbox = nullptr;
    m_slot = -1;
}

// ── FrameMailbox ─────────────────────────────────────────────────────────

FrameMailbox::~FrameMailbox()
{
    QMutexLocker locker(&m_mutex);
    for (Slot &slot : m_slots) {
        slot.readers = 0;
        slot.state = FrameState::kEmpty;
    }
}

int FrameMailbox::pickWritableSlotLocked() const
{
    // 1) 优先空槽位（避免覆盖还有价值的新帧）
    for (int i = 0; i < kFrameSlotCount; ++i) {
        if (m_slots[i].state == FrameState::kEmpty && m_slots[i].readers == 0)
            return i;
    }
    // 2) 退而求其次：最旧的完整帧，且当前无人读取
    int oldest = -1;
    qint64 oldestMs = std::numeric_limits<qint64>::max();
    for (int i = 0; i < kFrameSlotCount; ++i) {
        const Slot &slot = m_slots[i];
        if (slot.state == FrameState::kComplete && slot.readers == 0 && slot.publishMs < oldestMs) {
            oldestMs = slot.publishMs;
            oldest = i;
        }
    }
    return oldest; // -1：全部槽位都在写或在被读，本帧只能丢弃（U-FRM-01）
}

bool FrameMailbox::publishFrame(const LegacyFrame &source, const quint8 *pixels, qsizetype byteCount)
{
    if (!pixels || byteCount <= 0)
        return false;

    int slotIndex = -1;
    {
        QMutexLocker locker(&m_mutex);

        // 世代校验（U-FRM-04）：旧世代帧一律拒收，尺寸切换期间不产生撕裂帧
        if (source.sizeGeneration != m_sizeGeneration) {
            ++m_dropped;
            return false;
        }
        // 自洽性校验：字节数必须与尺寸/行步长一致，否则宁可丢帧也不上传错位数据
        if (source.rowStride == 0
            || byteCount != qsizetype(source.physicalSize.height()) * qsizetype(source.rowStride)) {
            ++m_dropped;
            return false;
        }

        slotIndex = pickWritableSlotLocked();
        if (slotIndex < 0) {
            ++m_dropped; // 忙时丢帧，不积压
            return false;
        }

        Slot &slot = m_slots[slotIndex];
        if (slot.buffer.size() != byteCount)
            slot.buffer.resize(byteCount);
        slot.frame = source;                       // 元数据（U-FRM-03）
        slot.state = FrameState::kWriting;         // 拷贝期间跳过本槽位
    }

    // 锁外拷贝：互斥锁不承担 memcpy 的耗时
    {
        Slot &slot = m_slots[slotIndex];
        std::memcpy(slot.buffer.data(), pixels, size_t(byteCount));
    }

    {
        QMutexLocker locker(&m_mutex);
        Slot &slot = m_slots[slotIndex];
        slot.frame.pixels = slot.buffer.data();
        slot.publishMs = QDateTime::currentMSecsSinceEpoch();
        slot.state = FrameState::kComplete;
        ++m_published;
    }

    // 唤醒消费者重绘。在生产者线程调用，锁外执行（回调可能投递队列事件）。
    // 没有这一步，消费者不会知道有新帧——邮箱是被动数据，不会自己触发场景图重绘
    // （2026-09-20 实测踩过：帧投递成功但视口永远不更新）。
    if (m_onFrameAvailable)
        m_onFrameAvailable();
    return true;
}

void FrameMailbox::setFrameAvailableCallback(FrameAvailableCallback callback)
{
    m_onFrameAvailable = std::move(callback);
}

quint32 FrameMailbox::bumpSizeGeneration()
{
    QMutexLocker locker(&m_mutex);
    ++m_sizeGeneration;
    // 废弃所有旧世代槽位；仍被读取的槽位保留数据（生产者已跳过），世代由调用方写入新帧
    for (Slot &slot : m_slots) {
        if (slot.readers == 0) {
            slot.state = FrameState::kEmpty;
            slot.frame = LegacyFrame{};
        }
    }
    return m_sizeGeneration;
}

FrameLease FrameMailbox::takeLatestFrame()
{
    QMutexLocker locker(&m_mutex);

    int best = -1;
    quint64 bestFrameNumber = 0;
    for (int i = 0; i < kFrameSlotCount; ++i) {
        const Slot &slot = m_slots[i];
        if (slot.state != FrameState::kComplete)
            continue;
        if (best < 0 || slot.frame.frameNumber > bestFrameNumber) {
            best = i;
            bestFrameNumber = slot.frame.frameNumber;
        }
    }
    if (best < 0)
        return FrameLease();

    ++m_slots[best].readers; // 生产者从现在起不会覆盖该槽位（U-FRM-02）
    ++m_leased;
    return FrameLease(this, best);
}

quint64 FrameMailbox::latestCompletedFrameNumber() const
{
    QMutexLocker locker(&m_mutex);
    quint64 latest = 0;
    for (const Slot &slot : m_slots) {
        if (slot.state == FrameState::kComplete)
            latest = qMax(latest, slot.frame.frameNumber);
    }
    return latest;
}

FrameMailbox::Stats FrameMailbox::stats() const
{
    QMutexLocker locker(&m_mutex);
    Stats s;
    s.published = m_published;
    s.dropped = m_dropped;
    s.leased = m_leased;
    s.sizeGeneration = m_sizeGeneration;

    qint64 newestMs = -1;
    for (const Slot &slot : m_slots) {
        if (slot.state == FrameState::kComplete) {
            ++s.completeSlots;
            newestMs = qMax(newestMs, slot.publishMs);
            s.bytesPerFrame = qMax(s.bytesPerFrame, slot.buffer.size());
        }
        if (slot.readers > 0)
            ++s.readersHeld;
    }
    if (newestMs >= 0)
        s.latestFrameAgeMs = QDateTime::currentMSecsSinceEpoch() - newestMs;
    return s;
}

void FrameMailbox::releaseSlot(int slot)
{
    QMutexLocker locker(&m_mutex);
    if (slot >= 0 && slot < kFrameSlotCount && m_slots[slot].readers > 0)
        --m_slots[slot].readers;
}

} // namespace stelapp
