/*
 * AppFacade — 新 QML 界面访问引擎的单一稳定接口。
 *
 * 职责：暴露时间/地点/选择/显示选项的稳定接口，供 QML 与 ActionRouter 使用。
 * 禁止：不暴露 GL/Vulkan 句柄；不让 QML 直接遍历模块单例。
 *
 * 依据：开发计划一第 5 节 / 开发指导文档第 3 节。
 * 实现轨迹：
 *   - A0 骨架（2026-09-17）定义属性面。
 *   - T15（2026-09-24）最小切片落地：simulationPaused + fieldOfView（含
 *     zoomIn/zoomOut 便捷命令）。时间速率经 ISimPacing 走帧泵真源。
 *
 * 时间语义（T14 审计 docs/T14_UPDATE_CLOCK_AUDIT.zh_CN.md §2.4 的落地）：
 *   合流形态的时间真源在帧泵（LiveSkyRuntime 每帧累计推进 JD），
 *   引擎 StelCore::updateTime 的墙钟路径已被 setTimeRate(0) 架空。
 *   因此本类**不直接写引擎时钟**：暂停/继续/速率全部经 ISimPacing
 *   接口交给帧泵实现。ISimPacing 就是 T16 ClockController 的前身。
 *
 * 纪律：
 *   1. 所有方法只允许 GUI 线程调用（引擎对象全为 GUI 线程亲和，本类不设锁）。
 *   2. 引擎未编译（独立工程形态）或未引导时，全部方法安全 no-op——
 *      QML 命令栏在无引擎形态下可点，但不产生引擎效果（currentFov 返回 -1 可辨）。
 *   3. 幂等：setSimulationPaused 对相同值直接返回（防双触发的第一道闸）。
 */
#pragma once

#include <QObject>

namespace stelapp {

//! 仿真推进控制（最小接口）。LiveSkyRuntime 实现；无引擎形态下为空。
//! 命名刻意不带 Clock：T16 落地 ClockController 时在此接口上扩展
//! （JD 单点写入、时间跳转、日历换算），接口形状已按"宿主单点写时钟"设计。
class ISimPacing
{
public:
    virtual ~ISimPacing() = default;
    //! 仿真推进比例：1=正常，0=暂停。帧泵照常出帧，只有 JD 推进被冻结。
    virtual void setSimScale(double scale) = 0;
    virtual double simScale() const = 0;
    //! 仿真推进速率（Julian day / 墙钟秒）。与 AppFacade::timeRate 同单位。
    virtual void setSimRate(double rate) = 0;
    virtual double simRate() const = 0;
};

class AppFacade : public QObject
{
    Q_OBJECT
    // QML 端只读属性经 NOTIFY 通知；白名单制，未列入的引擎属性不开放。
    Q_PROPERTY(bool simulationPaused READ simulationPaused WRITE setSimulationPaused NOTIFY simulationPausedChanged)
    Q_PROPERTY(double timeRate READ timeRate WRITE setTimeRate NOTIFY timeRateChanged)
    Q_PROPERTY(double fieldOfView READ fieldOfView WRITE setFieldOfView NOTIFY fieldOfViewChanged)

public:
    explicit AppFacade(QObject *parent = nullptr);

    //! 装配点注入（main.cpp 在引擎帧泵启动成功后调用）。可传 nullptr（无引擎形态）。
    void attachSimControl(ISimPacing *sim);

    // ---- 时间（语义保留：JD 为 UT；速率单位 Julian day/second）----
    // THREAD: gui
    // 当前 JD 直接读引擎（只读不写，写侧归帧泵/T16 ClockController）。
    // 引擎不可用时返回 0.0 并置无效标记（QML 侧不应依赖此值）。
    Q_INVOKABLE double julianDay() const;
    // "60 倍速"必须换算为 60.0 Julian day/second 后传入，禁止把倍数直接当速率。
    double timeRate() const;
    void setTimeRate(double ratePerJulianDaySecond);

    // ---- 显示 ----
    // THREAD: gui
    double fieldOfView() const;
    //! 经引擎 zoomTo 平滑过渡（0.4s）到目标视场。越界值由引擎侧 min/max 钳制。
    void setFieldOfView(double degrees);

    bool simulationPaused() const;
    //! 幂等：值不变直接返回（不重复通知，不重复改帧泵状态）。
    void setSimulationPaused(bool paused);

    // ---- T15 命令便捷入口（ActionRouter 注册表的目标）----
    Q_INVOKABLE void zoomIn();    //!< 视场 ×0.8
    Q_INVOKABLE void zoomOut();   //!< 视场 ×1.25
    Q_INVOKABLE void togglePause() { setSimulationPaused(!m_simulationPaused); }

signals:
    void simulationPausedChanged(bool paused);
    void timeRateChanged(double ratePerJulianDaySecond);
    void fieldOfViewChanged(double degrees);

private:
    //! 引擎 StelMovementMgr 是否可用（已引导且 zoom 接口可达）。
    bool movementReady() const;

    ISimPacing *m_sim = nullptr;
    bool m_simulationPaused = false;  // 与 LiveSkyRuntime 的 scale=1 默认一致（运行态）
    double m_timeRate = 1.0;          // Julian day / second（镜像值，真源在 ISimPacing）
};

} // namespace stelapp
