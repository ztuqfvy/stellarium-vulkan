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
    // ⚠️ T41 修复：这里**不再自判** `m_backendOk = (apiName == "Vulkan")`。
    //   旧实现把判定写死成 "Vulkan" 字符串（A1 时代的语义复制）—— T39 转 Metal
    //   RHI 后 `applyRuntimeApi("Metal")` ⇒ m_backendOk 恒 false ⇒ 工具栏标签、
    //   诊断页状态色、关于页渲染后端行**全错**（T41-B 冒烟实抓：C++ 日志明明
    //   `backendOk=1`，界面却红）。判定真源 = main.cpp `checkBackendApi` 的
    //   `api == wantedApi`（wantedApi 跟随用户请求），经 applyBackendResult 传入。
    emit runtimeApiNameChanged();
}

void BackendInfo::applyBackendResult(const QString &apiName, bool ok)
{
    applyRuntimeApi(apiName);
    // 值变化才 emit（避免噪音重算 —— 绑定只登记"绑定里实际读过的属性"，
    // 重复发信号只会引起无谓的重算）。
    if (m_backendOk != ok)
    {
        m_backendOk = ok;
        emit backendOkChanged();
    }
}

} // namespace stelapp
