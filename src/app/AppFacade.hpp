/*
 * AppFacade — 新 QML 界面访问引擎的单一稳定接口（骨架，未实现）。
 *
 * 职责：暴露时间/地点/选择/显示选项的稳定接口，供 QML 与 ActionRouter 使用。
 * 禁止：不暴露 GL/Vulkan 句柄；不让 QML 直接遍历模块单例。
 *
 * 依据：开发计划一第 5 节 / 开发指导文档第 3 节。
 * 实现顺序：A3。时间入口见 StelCore.hpp:493,499,592；地点见 StelCore.hpp:322,400。
 */
#pragma once

#include <QObject>
#include <QDateTime>

namespace stelapp {

/*
 * 应用门面（骨架）。所有方法只能在 GUI/仿真线程调用（QML 绑定属性除外，
 * Qt 属性系统会处理跨线程通知；可变业务状态一律归 GUI 线程）。
 */
class AppFacade : public QObject
{
    Q_OBJECT
    // QML 端只读属性经 NOTIFY 通知；白名单制，未列入的引擎属性不开放。
    Q_PROPERTY(bool simulationPaused READ simulationPaused WRITE setSimulationPaused NOTIFY simulationPausedChanged)
    Q_PROPERTY(double timeRate READ timeRate WRITE setTimeRate NOTIFY timeRateChanged)
    Q_PROPERTY(double fieldOfView READ fieldOfView WRITE setFieldOfView NOTIFY fieldOfViewChanged)

public:
    explicit AppFacade(QObject *parent = nullptr);

    // ---- 时间（语义保留：JD 为 UT；速率单位 Julian day/second）----
    // THREAD: gui
    // @see StelCore::setJD / StelCore::getJD / StelCore::setTimeRate
    double julianDay() const;
    void setJulianDay(double jd);
    // "60 倍速"必须换算为 60.0 Julian day/second 后传入，禁止把倍数直接当速率。
    double timeRate() const;
    void setTimeRate(double ratePerJulianDaySecond);

    // ---- 地点（使用位置值对象，复用旧观察者切换逻辑）----
    // THREAD: gui
    // @see StelCore::getCurrentLocation / StelCore::moveObserverTo
    // 返回值对象的具体类型在实现时定稿（可复制结构，不含引擎指针）。
    // LocationValue currentLocation() const;
    // void moveObserverTo(const LocationValue &location);

    // ---- 显示 ----
    // THREAD: gui
    double fieldOfView() const;
    void setFieldOfView(double degrees);

    bool simulationPaused() const;
    void setSimulationPaused(bool paused);

signals:
    void simulationPausedChanged(bool paused);
    void timeRateChanged(double ratePerJulianDaySecond);
    void fieldOfViewChanged(double degrees);

private:
    bool m_simulationPaused = true;
    double m_timeRate = 1.0;       // Julian day / second
    double m_fieldOfView = 60.0;   // 度
};

} // namespace stelapp
