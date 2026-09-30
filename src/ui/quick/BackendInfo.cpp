#include "ui/quick/BackendInfo.hpp"
#include "render/legacy/FrameMailbox.hpp"  // T39 运行时诊断的数据源
#include "render/vulkan/VkDeviceProbe.hpp" // 包含路径以 src/ 为根（CMake include 目录）

namespace stelapp {

BackendInfo::BackendInfo(QObject *parent)
    : QObject(parent)
{
}

QString BackendInfo::deviceName() const { return m_deviceName; }
QString BackendInfo::vulkanVersion() const { return m_vulkanVersion; }
QString BackendInfo::driverVersion() const { return m_driverVersion; }
QString BackendInfo::probeError() const { return m_probeError; }
bool BackendInfo::portabilityDriver() const { return m_portabilityDriver; }
QString BackendInfo::runtimeApiName() const { return m_runtimeApiName; }
bool BackendInfo::backendOk() const { return m_backendOk; }

bool BackendInfo::tbTokenBindingOff() const
{
    return qEnvironmentVariableIsSet("STELQUICK_TOOL_TOKEN_OFF");
}

bool BackendInfo::tbClickOff() const
{
    return qEnvironmentVariableIsSet("STELQUICK_TOOL_CLICK_OFF");
}

bool BackendInfo::tbLayoutBreak() const
{
    return qEnvironmentVariableIsSet("STELQUICK_TOOL_LAYOUT_BREAK");
}

bool BackendInfo::nightEffectOff() const
{
    return qEnvironmentVariableIsSet("STELQUICK_NIGHT_EFFECT_OFF");
}

void BackendInfo::setFrameMailbox(FrameMailbox *mailbox)
{
    m_mailbox = mailbox;
    refresh();   // 立刻拉一次，免得界面先显示一排 0
}

void BackendInfo::refresh()
{
    if (!m_mailbox)
    {
        emit refreshed();
        return;
    }
    // 只读搬运：**不做任何计算、不做任何加计数的取帧**（见头注那句"诊断污染"）。
    const FrameMailbox::Stats st = m_mailbox->stats();
    m_frameNumber = m_mailbox->latestCompletedFrameNumber();
    m_published = st.published;
    m_dropped = st.dropped;
    m_leased = st.leased;
    m_completeSlots = st.completeSlots;
    m_slotCapacity = FrameMailbox::kFrameSlotCount;
    m_readersHeld = st.readersHeld;
    m_latestFrameAgeMs = st.latestFrameAgeMs;
    m_bytesPerFrame = st.bytesPerFrame;
    m_sizeGeneration = st.sizeGeneration;
    emit refreshed();
}

void BackendInfo::applyProbe(const VulkanProbeResult &probe)
{
    m_deviceName = probe.deviceName;
    m_vulkanVersion = probe.apiVersion;
    m_driverVersion = probe.driverVersion;
    m_probeError = probe.error;
    m_portabilityDriver = probe.portabilityDriver;
    // 探针失败不立即判死：Qt 可能仍有其他渲染路径；最终判定以运行时 API 为准。
    // 但探针失败要显示给诊断页（probeError 非空）。
}

void BackendInfo::applyProbeUnavailable(const QString &reason)
{
    m_probeError = reason;
}

void BackendInfo::applyRuntimeApi(const QString &apiName)
{
    m_runtimeApiName = apiName;
    m_runtimeApiKnown = true;
    // A1 判定：请求 Vulkan，实际必须 Vulkan
    m_backendOk = (apiName == QLatin1String("Vulkan"));
    emit runtimeApiNameChanged();
    emit backendOkChanged();
}

} // namespace stelapp
