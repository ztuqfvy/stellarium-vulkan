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
//! T39 运行时诊断的数据源（只前置声明 —— 实现细节留在 .cpp，本头保持轻量）。
class FrameMailbox;

class BackendInfo : public QObject
{
    Q_OBJECT
    // 探针数据（启动时 VkDeviceProbe::probe() 填充）
    Q_PROPERTY(QString deviceName READ deviceName CONSTANT)
    Q_PROPERTY(QString vulkanVersion READ vulkanVersion CONSTANT)
    Q_PROPERTY(QString driverVersion READ driverVersion CONSTANT)
    Q_PROPERTY(QString probeError READ probeError CONSTANT)
    // 驱动形态：true=portability 转译层（MoltenVK），false=平台原生驱动（NVIDIA/AMD/Intel）
    Q_PROPERTY(bool portabilityDriver READ portabilityDriver CONSTANT)
    // Qt Quick 运行时实际 API（场景图初始化后由 main 写入，如 "Vulkan"/"Metal"）
    Q_PROPERTY(QString runtimeApiName READ runtimeApiName NOTIFY runtimeApiNameChanged)
    // 后端校验判定：请求 Vulkan 且实际 Vulkan 才为 true
    Q_PROPERTY(bool backendOk READ backendOk NOTIFY backendOkChanged)
    // T34-C 负控开关（同 STELQUICK_TOOL_REV_OFF 先例，只用于证明判据承重）：
    // true ⇒ Toolbar.qml 的 engineOn 绑定**不读** revision token（QML 绑定铁律的
    // 缺陷形态）⇒ 引擎翻转后按钮态停在首帧 ⇒ TOOLBARCHECK 的 TB-12 必红。
    Q_PROPERTY(bool tbTokenBindingOff READ tbTokenBindingOff CONSTANT)
    // T34-C 负控开关②：true ⇒ Toolbar.qml 的开关按钮 onClicked **不派发**
    // ActionRouter.trigger ⇒ 真实点击不翻动引擎 ⇒ TB-10 必红（且只 TB-10 红，
    // TB-12 不受影响——隔离良好的判别负控）。证明"点击→引擎"这条腿承重：
    // 没有它，"点击没接上"这种缺陷只能靠 TB-09 的存在性判据碰运气。
    Q_PROPERTY(bool tbClickOff READ tbClickOff CONSTANT)
    // T34-C 负控开关③：true ⇒ Toolbar 根 Rectangle 的 implicitHeight 打回 **0**
    // ⇒ 复现"工具栏高度 0、布局全乱但**不裁剪**"的**假绿掩护**缺陷形态。
    // 期望：只有 TB-10 红（真实点击落在根内容控件上），其余 11 条照绿
    // ——证明"落点覆盖"自证是这条假绿的唯一哨兵。
    Q_PROPERTY(bool tbLayoutBreak READ tbLayoutBreak CONSTANT)
    // T37-C 负控开关：true ⇒ MainWindow.qml 的夜视 layer.enabled 绑定强制 false
    // ⇒ 夜视效果物理不生效 ⇒ NIGHTCHECK 的 NC-03② 必红（且只它红）。
    // 证明"效果面"判据承重：没有它，"QML 滤镜被悄悄删掉/shader 路径写错"这种
    // 缺陷只能靠 NC-03① 的上游零差异碰运气（那是"引擎侧没画"，测不出"UI 侧没画"）。
    Q_PROPERTY(bool nightEffectOff READ nightEffectOff CONSTANT)

    // ---- T39 运行时诊断（诊断页的"活"的那一半）----
    //!
    //! 出处：A-1.0 范围表 `|资源路径、错误提示、**渲染诊断**、配置保存 | 最小 | 必须 |`
    //! —— T36 只做掉了"配置保存"（目录隔离）；"渲染诊断"此前只有**静态后端信息**
    //! （API/设备/驱动/DPR），**没有任何运行时量**。数据源 = `FrameMailbox::Stats`
    //! （`src/render/legacy/FrameMailbox.hpp:117`，任意线程可读）。
    //!
    //! ⚠️ 这些量**每帧都在变**（帧号 / 帧年龄）⇒ 按项目纪律**不建绑定**，由 QML 侧
    //! `Timer` 周期调 `refresh()`（T15：每帧变的连续量一律轮询；T39 探针已确认
    //! 字号那一组才是"离散状态"，可以绑定）。
    //! ⚠️ `refresh()` 里**不做任何计算**，只把引擎读数搬过来 —— 诊断页显示的数字
    //! 必须与 `FrameMailbox::stats()` 逐字对应（否则又是一把"会撒谎的仪器"）。
    Q_PROPERTY(quint64 frameNumber READ frameNumber NOTIFY refreshed)
    Q_PROPERTY(quint64 framesPublished READ framesPublished NOTIFY refreshed)
    Q_PROPERTY(quint64 framesDropped READ framesDropped NOTIFY refreshed)
    Q_PROPERTY(quint64 framesLeased READ framesLeased NOTIFY refreshed)
    Q_PROPERTY(int completeSlots READ completeSlots NOTIFY refreshed)
    Q_PROPERTY(int slotCapacity READ slotCapacity NOTIFY refreshed)
    Q_PROPERTY(int readersHeld READ readersHeld NOTIFY refreshed)
    Q_PROPERTY(qint64 latestFrameAgeMs READ latestFrameAgeMs NOTIFY refreshed)
    Q_PROPERTY(qint64 bytesPerFrame READ bytesPerFrame NOTIFY refreshed)
    Q_PROPERTY(quint32 sizeGeneration READ sizeGeneration NOTIFY refreshed)
    //! 诊断是否已接上帧泵（未接 ⇒ 全 0，界面显示"未接引擎帧泵"而非假装 0 是读数）。
    Q_PROPERTY(bool runtimeDiagAvailable READ runtimeDiagAvailable NOTIFY refreshed)
    //! ⚠️ **刻意不报帧宽高**：`FrameMailbox` 没有只读的尺寸访问，唯一途径是
    //! `takeLatestFrame()` —— 而它会让 `leased` 计数 **+1**，也就是**诊断会污染
    //! 被诊断的量**（刷新 500ms 一次 ⇒ leased 长得比 published 快，读数是假的）。
    //! 尺寸的独立验算留在 T39-A 探针与 T39-C 判据里做（那里本来就持有完整接口）。

    // 说明：手动 qmlRegisterSingletonInstance 注册（main.cpp），不用 QML_ELEMENT 宏，
    // 避免与 qt_add_qml_module 的类型注册重复冲突。

public:
    explicit BackendInfo(QObject *parent = nullptr);

    QString deviceName() const;
    QString vulkanVersion() const;
    QString driverVersion() const;
    QString probeError() const;
    bool portabilityDriver() const;
    QString runtimeApiName() const;
    bool backendOk() const;
    bool tbTokenBindingOff() const;
    bool tbClickOff() const;
    bool tbLayoutBreak() const;
    bool nightEffectOff() const;

    //! T39：拉一次运行时读数并 emit refreshed()（QML 侧 Timer 周期调；**不建绑定**）。
    Q_INVOKABLE void refresh();
    //! T39：main.cpp 在帧泵 `start()` **之后**调用（之前 refresh() 拿不到数据）。
    void setFrameMailbox(FrameMailbox *mailbox);

    quint64 frameNumber() const { return m_frameNumber; }
    quint64 framesPublished() const { return m_published; }
    quint64 framesDropped() const { return m_dropped; }
    quint64 framesLeased() const { return m_leased; }
    int completeSlots() const { return m_completeSlots; }
    int slotCapacity() const { return m_slotCapacity; }
    int readersHeld() const { return m_readersHeld; }
    qint64 latestFrameAgeMs() const { return m_latestFrameAgeMs; }
    qint64 bytesPerFrame() const { return m_bytesPerFrame; }
    quint32 sizeGeneration() const { return m_sizeGeneration; }
    bool runtimeDiagAvailable() const { return m_mailbox != nullptr; }

    // 填充探针结果（应用启动时调用一次）
    void applyProbe(const VulkanProbeResult &probe);
    // 探针未编译进本构建时调用（如鸿蒙交叉编译，OHOS NDK 不保证提供 libvulkan）
    void applyProbeUnavailable(const QString &reason);
    // 写入运行时实际 API 名并做判定（sceneGraphInitialized 回调）
    void applyRuntimeApi(const QString &apiName);

signals:
    void runtimeApiNameChanged();
    void backendOkChanged();
    //! T39：`refresh()` 拉完一次运行时读数后发（QML 侧那些数字靠它重算）。
    void refreshed();

private:
    QString m_deviceName;
    QString m_vulkanVersion;
    QString m_driverVersion;
    QString m_probeError;
    bool m_portabilityDriver = false;
    QString m_runtimeApiName;
    bool m_runtimeApiKnown = false;
    bool m_backendOk = false;

    // T39 运行时诊断（每次 refresh() 从 FrameMailbox::Stats 原样搬过来）
    FrameMailbox *m_mailbox = nullptr;
    quint64 m_frameNumber = 0;
    quint64 m_published = 0;
    quint64 m_dropped = 0;
    quint64 m_leased = 0;
    int m_completeSlots = 0;
    int m_slotCapacity = 0;
    int m_readersHeld = 0;
    qint64 m_latestFrameAgeMs = -1;
    qint64 m_bytesPerFrame = 0;
    quint32 m_sizeGeneration = 0;
};

} // namespace stelapp
