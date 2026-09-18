/*
 * VkDeviceProbe — Vulkan 物理设备探针（A1 诊断页数据源）。
 *
 * 职责：创建临时 VkInstance，枚举物理设备，取第一个设备上报
 *       设备名、Vulkan API 版本、驱动版本（MoltenVK 场景下驱动版本为 MoltenVK 版本语义）。
 * 禁止：不创建窗口表面、不参与渲染；探针完成即销毁全部 Vulkan 句柄。
 *
 * 线程：THREAD: gui（仅应用启动时在主线程调用一次）。
 * 运行前置环境变量（见 docs/BUILD_RECORD.zh_CN.md）：
 *   VK_DRIVER_FILES=/opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json
 *   QT_VULKAN_LIB=/opt/homebrew/opt/vulkan-loader/lib/libvulkan.1.dylib
 */
#pragma once

#include <QString>

namespace stelapp {

struct VulkanProbeResult
{
    bool ok = false;
    QString deviceName;      // 物理设备名（如 "Apple M3"）
    QString apiVersion;      // 设备支持的 Vulkan API 版本（major.minor.patch）
    QString driverVersion;   // 驱动版本（MoltenVK 转译路径的版本语义）
    QString error;           // 失败原因（ok=false 时有值）
};

class VkDeviceProbe
{
public:
    static VulkanProbeResult probe();
};

} // namespace stelapp
