#include "VkDeviceProbe.hpp"

#include <QList>
#include <vulkan/vulkan.h>

namespace stelapp {

namespace {

QString versionToString(quint32 version)
{
    // Vulkan 版本宏：major/minor/patch 三段拆解（vulkan-headers 1.4 提供）
    return QString("%1.%2.%3")
        .arg(VK_API_VERSION_MAJOR(version))
        .arg(VK_API_VERSION_MINOR(version))
        .arg(VK_API_VERSION_PATCH(version));
}

} // namespace

VulkanProbeResult VkDeviceProbe::probe()
{
    VulkanProbeResult result;

    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "stelQuickUI-probe";
    appInfo.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    appInfo.pEngineName = "stelQuickUI";
    appInfo.engineVersion = VK_MAKE_VERSION(0, 1, 0);
    appInfo.apiVersion = VK_API_VERSION_1_1; // 探针请求 1.1 足够枚举设备

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    // MoltenVK 是 portability 驱动（is_portability_driver: true）：
    // 必须声明枚举 portability 位 + 启用对应扩展，否则 vkCreateInstance 返回
    // VK_ERROR_INCOMPATIBLE_DRIVER(-9)。Qt 的 QVulkanInstance 内部已自动处理，
    // 手写探针必须显式处理——这是与 Qt 行为对齐的关键一步。
    const char *portabilityExtension = "VK_KHR_portability_enumeration";
    createInfo.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    createInfo.enabledExtensionCount = 1;
    createInfo.ppEnabledExtensionNames = &portabilityExtension;

    VkInstance instance = VK_NULL_HANDLE;
    VkResult vr = vkCreateInstance(&createInfo, nullptr, &instance);
    if (vr != VK_SUCCESS || instance == VK_NULL_HANDLE) {
        result.error = QString("vkCreateInstance 失败（VkResult=%1）。"
                               "检查 VK_DRIVER_FILES / QT_VULKAN_LIB 环境变量。").arg(int(vr));
        return result;
    }

    quint32 deviceCount = 0;
    vr = vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
    if (vr != VK_SUCCESS || deviceCount == 0) {
        result.error = QString("vkEnumeratePhysicalDevices 无设备（VkResult=%1，count=%2）。"
                               "MoltenVK ICD 可能未被加载。").arg(int(vr)).arg(deviceCount);
        vkDestroyInstance(instance, nullptr);
        return result;
    }

    QList<VkPhysicalDevice> devices(deviceCount);
    vr = vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());
    if (vr != VK_SUCCESS) {
        result.error = QString("vkEnumeratePhysicalDevices 第二次调用失败（VkResult=%1）").arg(int(vr));
        vkDestroyInstance(instance, nullptr);
        return result;
    }

    // 取第一个设备（macOS + MoltenVK 场景下即当前 Metal 设备）
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(devices.first(), &props);
    result.deviceName = QString::fromUtf8(props.deviceName);
    result.apiVersion = versionToString(props.apiVersion);
    result.driverVersion = versionToString(props.driverVersion);
    result.ok = true;

    vkDestroyInstance(instance, nullptr);
    return result;
}

} // namespace stelapp
