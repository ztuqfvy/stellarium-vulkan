/*
 * BackendInfo — A1 诊断页数据单例（QML 只读）。
 *
 * 职责：聚合三路后端信息——VkDeviceProbe 探针结果、Qt Quick 运行时实际 API、
 *       后端校验判定（backendOk）。诊断页据此显示 API/GPU/驱动/状态色。
 * 禁止：不暴露任何 GL/Vulkan 句柄给 QML；只暴露字符串与布尔值。
 *
 * 线程：THREAD: gui。sceneGraphInitialized 在 GUI 线程发射，直接写属性安全。
 * 判定规则（A1 通过条件）：请求 Vulkan 就必须实际 Vulkan，否则 backendOk=false，
 * 应用以非零码退出（禁止静默回退 Metal/GL）。
 */
#pragma once

#include <QObject>

namespace stelapp {

struct VulkanProbeResult;

class BackendInfo : public QObject
{
    Q_OBJECT
    // 探针数据（启动时 VkDeviceProbe::probe() 填充）
    Q_PROPERTY(QString deviceName READ deviceName CONSTANT)
    Q_PROPERTY(QString vulkanVersion READ vulkanVersion CONSTANT)
    Q_PROPERTY(QString driverVersion READ driverVersion CONSTANT)
    Q_PROPERTY(QString probeError READ probeError CONSTANT)
    // Qt Quick 运行时实际 API（场景图初始化后由 main 写入，如 "Vulkan"/"Metal"）
    Q_PROPERTY(QString runtimeApiName READ runtimeApiName NOTIFY runtimeApiNameChanged)
    // 后端校验判定：请求 Vulkan 且实际 Vulkan 才为 true
    Q_PROPERTY(bool backendOk READ backendOk NOTIFY backendOkChanged)

    // 说明：手动 qmlRegisterSingletonInstance 注册（main.cpp），不用 QML_ELEMENT 宏，
    // 避免与 qt_add_qml_module 的类型注册重复冲突。

public:
    explicit BackendInfo(QObject *parent = nullptr);

    QString deviceName() const;
    QString vulkanVersion() const;
    QString driverVersion() const;
    QString probeError() const;
    QString runtimeApiName() const;
    bool backendOk() const;

    // 填充探针结果（应用启动时调用一次）
    void applyProbe(const VulkanProbeResult &probe);
    // 写入运行时实际 API 名并做判定（sceneGraphInitialized 回调）
    void applyRuntimeApi(const QString &apiName);

signals:
    void runtimeApiNameChanged();
    void backendOkChanged();

private:
    QString m_deviceName;
    QString m_vulkanVersion;
    QString m_driverVersion;
    QString m_probeError;
    QString m_runtimeApiName;
    bool m_runtimeApiKnown = false;
    bool m_backendOk = false;
};

} // namespace stelapp
