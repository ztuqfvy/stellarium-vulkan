/*
 * VkDeviceProbe — Vulkan 物理设备探针（A1 诊断页数据源）。
 *
 * 职责：创建临时 VkInstance，枚举物理设备，取第一个设备上报
 *       设备名、Vulkan API 版本、驱动版本（MoltenVK 场景下驱动版本为 MoltenVK 版本语义）。
 * 禁止：不创建窗口表面、不参与渲染；探针完成即销毁全部 Vulkan 句柄。
 *
 * 线程：THREAD: gui（仅应用启动时在主线程调用一次）。
 *
 * 平台差异（2026-09-20 增补，Windows 移植）：
 *   macOS 经 MoltenVK（portability 驱动）：需要
 *     VK_DRIVER_FILES=/opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json
 *     QT_VULKAN_LIB=/opt/homebrew/opt/vulkan-loader/lib/libvulkan.1.dylib
 *   Windows/Linux 原生驱动（NVIDIA/AMD/Intel）：系统自带 vulkan-1.dll / libvulkan.so，
 *     两个变量都不需要；portability 扩展也不存在，探针会自动跳过（见 .cpp）。
 */
#pragma once

#include <QString>

namespace stelapp {

struct VulkanProbeResult
{
    bool ok = false;
    QString deviceName;      // 物理设备名（如 "Apple M3" / "NVIDIA GeForce RTX 4060"）
    QString apiVersion;      // 设备支持的 Vulkan API 版本（major.minor.patch）
    QString driverVersion;   // 驱动版本（MoltenVK 转译路径的版本语义 / 原生驱动号）
    QString error;           // 失败原因（ok=false 时有值）
    // 是否走 portability 转译层（MoltenVK=true，Windows 原生 NVIDIA 驱动=false）。
    // 用途：跨平台对照时必须先确认"驱动形态"这个自变量，否则结论无法归因。
    // 判据（2026-09-21 修正）：**设备级**扩展 VK_KHR_portability_subset 是否存在。
    // 不能用实例级 VK_KHR_portability_enumeration 判——Windows 的 Vulkan 1.4.357
    // loader 恒定暴露该实例扩展（它是 loader 的能力，不代表驱动形态），会把原生
    // NVIDIA 驱动误标成转译层。误判实测见 docs/BUILD_RECORD.zh_CN.md。
    bool portabilityDriver = false;
    // 实例级 VK_KHR_portability_enumeration 是否可用（loader 能力）。
    // 仅用于决定 vkCreateInstance 要不要声明枚举 portability 位；
    // 严禁拿它当"驱动形态"判据（见上）。
    bool portabilityEnumerationExt = false;
};

class VkDeviceProbe
{
public:
    static VulkanProbeResult probe();
};

} // namespace stelapp
