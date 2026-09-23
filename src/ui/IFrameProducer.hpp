/*
 * IFrameProducer — 动态帧生产者的运行期契约（T12，2026-09-23）。
 *
 * 定位：DynFrameCheck（I-DYN / P-BRG-01 / P-BRG-04 自检）需要一套"生产者无关"的
 *   运行期能力，而两种生产者的装配方式根本不同：
 *     · LiveFrameSource —— 独立 std::thread + kOwn 离屏上下文，装配带自己的 Config；
 *     · LiveSkyRuntime   —— GUI 线程 QTimer + 借引擎上下文，装配前必须先 boot 引擎。
 *   装配（start/boot）差异巨大，故**不纳入本接口**：装配由调用方（main.cpp）按
 *   生产者类型完成并把实例交给自检；本接口只覆盖"装配成功之后"两边语义相同的能力。
 *
 * 三能力（DynFrameCheck 的全部实际需求）：
 *   1. setFps    —— P-BRG-04 降级探针的动态调速；
 *   2. counters  —— D1-C01 生产者健康判据（rendered / failed）+ 诊断用实测速率；
 *   3. stop      —— 自检收尾停机。
 *
 * 线程约束：两个实现都不相同（LiveFrameSource 的 setFps 跨线程原子；
 *   LiveSkyRuntime 只在 GUI 线程调用合法），**契约层面统一为"调用方保证线程合法"**——
 *   DynFrameCheck 在 GUI 线程定时器里调用，对两个实现都合法。
 */
#pragma once

#include <QtGlobal>

namespace stelapp {

//! 生产者运行期计数快照（"成功产出的帧"口径统一：两种实现都指**已进邮箱**的帧）。
struct ProducerCounters
{
    quint64 rendered = 0;   //!< 成功产出并投递的帧数
    quint64 failed = 0;     //!< 渲染失败次数（健康判据要求恒为 0）
    double fps = 0.0;       //!< 生产者自报的实测速率（仅诊断，不参与判据）
};

class IFrameProducer
{
public:
    virtual ~IFrameProducer() = default;

    //! 动态调速（P-BRG-04 降级探针）。<=0 的实现语义由各生产者定义。
    virtual void setFps(double fps) = 0;

    //! 计数快照（任意时刻可读，不得阻塞）。
    virtual ProducerCounters counters() const = 0;

    //! 停机（幂等）。注意：只停"产帧"，不负责销毁引擎/邮箱。
    virtual void stop() = 0;
};

} // namespace stelapp
