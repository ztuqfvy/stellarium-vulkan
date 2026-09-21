/*
 * LegacyHostCheck — T6 自动自检：旧宿主显式帧驱动（计划一 A2 硬约束的可执行判据）。
 *
 * 为什么是**程序化判据**而不是"人看一下窗口"：
 *   A2 的硬约束是"隐藏旧窗口不保证继续绘制，必须实现显式帧驱动"。这件事在肉眼上
 *   **完全看不出来**——靠 paint 事件侥幸跑起来的实现，和真正显式驱动的实现，
 *   画面上一样。只有把"请求 N 帧"与"得到 N 帧、且逐帧内容按仿真时间变化"对上账，
 *   才能证伪"其实是靠事件循环偶然在画"。
 *
 * 跑法：STELQUICK_LEGACY_HOST_TEST=1 ./stelQuickUI
 *   - 在**创建任何窗口之前**执行（因此"无窗口"是可验证的事实，不是声明）。
 *   - 不进入事件循环，纯同步：没有主循环可以"顺手帮我们画一帧"。
 *   - 全程不经过 QML / Vulkan 纹理上传，因此不受 Qt6.11.2 Vulkan RHI × MoltenVK
 *     静态纹理黑屏缺陷影响（见 docs/BUILD_RECORD.zh_CN.md）。
 *
 * 退出码：0 全部通过；8 有检查项不通过（见 docs/WINDOWS_BUILD.zh_CN.md 退出码表）。
 */
#pragma once

#include <QSize>
#include <QString>
#include <QStringList>

namespace stelapp {

struct LegacyHostCheckResult
{
    bool ran = false;        //!< 自检是否真的执行了（装配失败时为 false）
    bool pass = false;
    int checkCount = 0;
    int failCount = 0;
    QString setupError;      //!< 装配/初始化失败原因（ran=false 时有值）
    QString summary;         //!< 一行结论
    QStringList details;     //!< 逐检查项一行（"T6-Cxx PASS/FAIL 说明"）
    QStringList frames;      //!< 逐帧明细
};

class LegacyHostCheck
{
public:
    //! 默认参数：12 帧、仿真步长 1s、起始仿真时间 4s。
    //! 取值理由：仿真时间 4→15s 对应本测试场景 t=1/6→0.625 的昼弧段，
    //! 太阳完整落在画面内且水平位置单调推进（不会被方向标记带遮住）。
    static constexpr int kDefaultFrameCount = 12;
    static constexpr double kDefaultSimStartSeconds = 4.0;
    static constexpr double kDefaultSimStepSeconds = 1.0;
    //! 帧元数据里的 DPR 换算用值（刻意取 2.0 以走通换算分支）。
    static constexpr double kDevicePixelRatio = 2.0;

    static LegacyHostCheckResult run(const QSize &physicalSize = QSize(960, 540),
                                     int frameCount = kDefaultFrameCount,
                                     double simStartSeconds = kDefaultSimStartSeconds,
                                     double simStepSeconds = kDefaultSimStepSeconds);
};

} // namespace stelapp
