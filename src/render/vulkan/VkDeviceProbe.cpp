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
    //
    // 跨平台修正（2026-09-20）：Windows/Linux 原生驱动（NVIDIA/AMD/Intel）**不提供**
    // VK_KHR_portability_enumeration。无条件把它列进 enabledExtensionNames 会让
    // vkCreateInstance 返回 VK_ERROR_EXTENSION_NOT_PRESENT(-7)，探针直接判失败，
    // 而 Qt 自己的 Vulkan 路径却正常——产生"探针坏、渲染好"的假故障。
    // 因此先枚举实例扩展，存在才开。
    const char *portabilityExtension = "VK_KHR_portability_enumeration";
    bool hasPortabilityEnumExt = false;
    {
        quint32 extCount = 0;
        if (vkEnumerateInstanceExtensionProperties(nullptr, &extCount, nullptr) == VK_SUCCESS
            && extCount > 0) {
            QList<VkExtensionProperties> exts(static_cast<int>(extCount));
            if (vkEnumerateInstanceExtensionProperties(nullptr, &extCount, exts.data()) == VK_SUCCESS) {
                for (const VkExtensionProperties &ext : exts) {
                    if (qstrcmp(ext.extensionName, portabilityExtension) == 0) {
                        hasPortabilityEnumExt = true;
                        break;
                    }
                }
            }
        }
    }
    // 只记录"loader 提供该实例扩展"这一事实；驱动形态判据在下面按设备级扩展给出。
    result.portabilityEnumerationExt = hasPortabilityEnumExt;
    if (hasPortabilityEnumExt) {
        createInfo.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
        createInfo.enabledExtensionCount = 1;
        createInfo.ppEnabledExtensionNames = &portabilityExtension;
    }

    VkInstance instance = VK_NULL_HANDLE;
    VkResult vr = vkCreateInstance(&createInfo, nullptr, &instance);
    if (vr != VK_SUCCESS || instance == VK_NULL_HANDLE) {
        result.error = QString("vkCreateInstance 失败（VkResult=%1）。"
                               "macOS：检查 VK_DRIVER_FILES / QT_VULKAN_LIB 环境变量。"
                               "Windows/Linux：确认显卡驱动已安装且 vulkan-1.dll / libvulkan.so 可加载。")
                           .arg(int(vr));
        return result;
    }

    quint32 deviceCount = 0;
    vr = vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
    if (vr != VK_SUCCESS || deviceCount == 0) {
        result.error = QString("vkEnumeratePhysicalDevices 无设备（VkResult=%1，count=%2）。"
                               "ICD 可能未被加载（macOS 检查 MoltenVK ICD，Windows 检查显卡驱动）。")
                           .arg(int(vr)).arg(deviceCount);
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

    // 驱动形态判据（2026-09-21 修正为设备级）：
    //   portability 驱动必须在其物理设备上暴露 VK_KHR_portability_subset
    //   （MoltenVK 有，Windows 原生 NVIDIA 驱动没有）。
    //   实例级 VK_KHR_portability_enumeration 不能作判据：新版 loader 恒定提供它。
    {
        quint32 devExtCount = 0;
        if (vkEnumerateDeviceExtensionProperties(devices.first(), nullptr, &devExtCount, nullptr)
                == VK_SUCCESS
            && devExtCount > 0) {
            QList<VkExtensionProperties> devExts(static_cast<int>(devExtCount));
            if (vkEnumerateDeviceExtensionProperties(devices.first(), nullptr, &devExtCount,
                                                     devExts.data())
                == VK_SUCCESS) {
                for (const VkExtensionProperties &ext : devExts) {
                    if (qstrcmp(ext.extensionName, "VK_KHR_portability_subset") == 0) {
                        result.portabilityDriver = true;
                        break;
                    }
                }
            }
        }
    }
    result.ok = true;

    vkDestroyInstance(instance, nullptr);
    return result;
}

} // namespace stelapp
