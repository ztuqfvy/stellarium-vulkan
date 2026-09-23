/*
 * DynFrameCheck — 动态帧通路自检（消费侧接线，I-DYN / P-BRG-01 / P-BRG-04 前置）。
 *
 * 回答的问题：LiveFrameSource（动态生产者）→ FrameMailbox → SkyViewport（场景图
 *   线程上传纹理）→ 上屏 这条**持续运行**的链路是否健康。
 * 与 A2FrameCheck 的区别：A2 校验的是"一帧静态图的像素正确性"；本校验的是
 *   "帧流持续推进 + 消费侧计量 + 降级状态切换"。
 *
 * 跑法：STELQUICK_DYN_CHECK=1 ./stelQuickUI（默认请求 Vulkan，Metal 用
 *   STELQUICK_GRAPHICS_API=metal 对照；Vulkan 上 D1-C06 逐像素判据因 §6.2
 *   已知缺陷标记 SKIP，不作为失败项——管线计量判据仍然全部执行）。
 *
 * 判据（退出码 0 全过 / 8 有 FAIL）：
 *   D1-C01 生产者健康：renderOneFrame 零失败，产出 ≥ 0.6×名义速率×时长
 *   D1-C02 显示推进：displayedFrameNumber 增量 ≥ 0.5×名义速率×时长（显示可被 vsync 封顶）
 *   D1-C03 邮箱有界：丢弃 ≤ 5% 投递
 *   D1-C04 帧龄有界：采样期内 latestFrameAgeMs 最大值 < 500ms
 *   D1-C05 上传健康：上传次数>0 且单次上传最大 < 100ms
 *   D1-C06 内容动态：相隔 ~1s 的两次抓帧不同（Vulkan 已知黑屏缺陷 → SKIP）
 *   D1-C07 降级切换：P-BRG-04——降速窗口内 degraded=true，恢复后 false
 *         （STELQUICK_DYN_DEGRADE_PROBE=0 可关）
 */
#pragma once

#include <QSize>
#include <QString>
#include <QStringList>
#include <functional>

class QGuiApplication;
class QQuickWindow;

namespace stelapp {

class FrameMailbox;
class LiveFrameSource;
class SkyViewport;

struct DynCheckResult
{
    bool ran = false;       // 自检是否真的执行（装配失败时 false）
    bool pass = false;
    int checkCount = 0;
    int failCount = 0;
    QString summary;
    QStringList details;    // 逐判据一行（"D1-Cxx PASS/FAIL/SKIP 说明"）
};

class DynFrameCheck
{
public:
    struct Options
    {
        int seconds = 8;              // 测量时长
        double producerFps = 60.0;    // 名义生产速率
        QSize physicalSize{1280, 720};
        qreal devicePixelRatio = 1.0;
        bool degradeProbe = true;     // D1-C07 降速探针
        double degradeFps = 15.0;     // 降级阈值（SkyViewport 判定用）
    };

    // 从环境变量读 Options（STELQUICK_DYN_SECONDS / STELQUICK_LIVE_FPS /
    // STELQUICK_DYN_DEGRADE_PROBE / STELQUICK_DEGRADE_FPS）。
    static Options optionsFromEnv();

    // 全程 GUI 线程定时器推进：启动生产者 → 采样 → 降速探针 → 汇总回调。
    // mailbox 必须已注入 SkyViewport；生产者由本函数启动与停止。
    static void runStartupSequence(QGuiApplication *app,
                                   QQuickWindow *window,
                                   SkyViewport *viewport,
                                   FrameMailbox *mailbox,
                                   const Options &options,
                                   const std::function<void(const DynCheckResult &)> &done);
};

} // namespace stelapp
