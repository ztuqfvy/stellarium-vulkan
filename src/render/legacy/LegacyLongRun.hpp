/*
 * LegacyLongRun — T9 长跑：真实天空产帧管道的 30 分钟稳定性与吞吐基线。
 *
 * 回答的问题（测试文档 P-BRG-01…04）：
 *   A3 已证明"旧宿主引擎能被显式帧驱动出真实天空帧"（短时 8 帧量级），
 *   T9 回答长时间维度的问题：
 *     - 吞吐：720p 连续 30 分钟，平均 / p95 的 render / readback / 总延迟；
 *     - 管道完整性：请求=投递、零失败、邮箱丢弃为零（有界队列不积压）；
 *     - 内存：常驻内存与 phys_footprint 不随时间长增（基线数据，门槛在
 *       本次运行之后冻结——测试文档 §6.1：冻结后不得降门槛）；
 *     - 内容哨兵：长跑中周期性抽查帧内容非平凡（防"静默黑屏跑了 30 分钟"）。
 *
 * 驱动方式（与生产路径对齐）：
 *   - 引导与 A3 相同：WA_DontShowOnScreen + show() + 有界 processEvents；
 *   - 回调体 = setJD(纪元 + 已流逝秒 × JD速率) → update(dt) → draw()；
 *     JD 默认按真实时间流速推进（1 墙钟秒 = 1/86400 JD 天），dt 用真实墙钟差；
 *   - 全速显式驱动（不按 vsync 节流）：量的是管道**最大**吞吐；
 *   - 每帧即取即弃（takeLatestFrame）：模拟最理想消费者，吞吐上限不受
 *     消费速度拖累；邮箱丢弃计数恒为预期。
 *   - 引擎开关保持"接近生产"：timeRate=0（JD 由外部注入是生产契约）、
 *     星闪与人眼自适应保持默认开（真实成本）。
 *
 * 环境变量：
 *   STELT9_RUN=1            触发（src/main.cpp 分流，在正常主窗口创建前）
 *   STELT9_SECONDS=N        测量时长秒（默认 1800 = 30 分钟）
 *   STELT9_WARMUP_SECONDS=N 预热时长秒（默认 120）
 *   STELT9_SIZE=WxH         离屏尺寸（默认 1280x720）；0x0 负控 → 装配失败/退出码 8
 *   STELT9_CSV=<path>       逐帧 CSV（默认 /tmp/stelt9-frames.csv）
 *   STELT9_JD_RATE=<f>      JD 推进速率（JD 天 / 墙钟秒，默认 1/86400 真实时间）
 *   STELT9_ALLOW_THROTTLED=1 跳过 T9-C00 环境前置（仅调试用；被测环境被系统降频时
 *                            数据无效，正式验收禁止设置）
 *
 * 退出码：0 = 管道完整（零失败零丢弃）；8 = 管道失败；
 *         9 = 环境不合规（未接电源 / 低电量模式开启，未开始测量）。
 *
 * 环境前置（T9-C00 / T9-C08，2026-09-22 教训新增）：
 *   门槛冻结协议要求"接通电源 + 关闭低电量模式"。实测同一二进制在电池 + 低电量模式下
 *   稳态吞吐从 52.66 fps 掉到 7.32 fps，而 T9-C01~C07 全部 PASS（不含吞吐判定）——
 *   环境污染会静默通过。故：测前 T9-C00 硬拦（不合规直接退出 9），测中每 60 秒复核，
 *   一旦发现降到电池/低电量模式即 T9-C08 FAIL。
 * 注意：吞吐/内存门槛**不在本次判定内**——T9 是基线，门槛由本次数据冻结。
 */
#pragma once

#include "render/legacy/LegacyAppCheck.hpp"

class QSettings;

namespace stelapp {

class LegacyLongRun
{
public:
    //! 前置条件与 LegacyAppCheck::run 相同（见其头注释）。
    static LegacyAppCheckResult run(QSettings *confSettings);
};

} // namespace stelapp
