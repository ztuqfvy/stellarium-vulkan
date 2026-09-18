#include "ui/quick/BackendInfo.hpp"
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
QString BackendInfo::runtimeApiName() const { return m_runtimeApiName; }
bool BackendInfo::backendOk() const { return m_backendOk; }

void BackendInfo::applyProbe(const VulkanProbeResult &probe)
{
    m_deviceName = probe.deviceName;
    m_vulkanVersion = probe.apiVersion;
    m_driverVersion = probe.driverVersion;
    m_probeError = probe.error;
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
