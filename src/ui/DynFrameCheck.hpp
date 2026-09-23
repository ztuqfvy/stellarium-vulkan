/*
 * DynFrameCheck — 动态帧通路自检（消费侧接线，I-DYN / P-BRG-01 / P-BRG-04 前置）。
 *
 * 回答的问题：动态生产者 → FrameMailbox → SkyViewport（场景图线程上传纹理）→
 *   上屏 这条**持续运行**的链路是否健康。
 * 与 A2FrameCheck 的区别：A2 校验的是"一帧静态图的像素正确性"；本校验的是
 *   "帧流持续推进 + 消费侧计量 + 降级状态切换"。
 *
 * 生产者无关（T12，2026-09-23）：自检只依赖 IFrameProducer 的运行期契约，因此
 *   同一套判据可在两种生产者上复跑——
 *     · LiveFrameSource（LegacyTestScene 替身，独立线程 + kOwn 上下文）→ 默认；
 *     · LiveSkyRuntime（**真实引擎** StelApp::update/draw，GUI 线程 + 借上下文）。
 *   装配方式差异巨大（后者须先 boot 引擎），故**装配由调用方完成**，本函数接收
 *   已启动的生产者；装配失败由调用方报 UNAVAILABLE（rc=6）。
 *
 * 跑法：STELQUICK_DYN_CHECK=1 ./stelQuickUI（默认请求 Vulkan，Metal 用
 *   STELQUICK_GRAPHICS_API=metal 对照；Vulkan 上 D1-C06 逐像素判据因 §6.2
 *   已知缺陷标记 SKIP，不作为失败项——管线计量判据仍然全部执行）。
 *   生产者切换：STELQUICK_DYN_PRODUCER=engine（须 Widgets 宿主形态构建）。
 *
 * 判据（退出码 0 全过 / 8 有 FAIL）：
 *   D1-C01 生产者健康：零失败 + **尾窗稳态**产出速率 ≥ 0.6×名义速率
 *   D1-C02 显示推进：**尾窗稳态**显示速率 ≥ 0.4×名义速率（显示可被 vsync 封顶）
 *         —— 两条都取"尾窗稳态"而非全程平均：warmup（引擎首帧编译/加载）与
 *            降速探针都不是生产者缺陷，用全程平均会稳定假红。
 *            2026-09-23 T12 实测：8 秒窗平均 31.5 fps vs 稳态 53.6 fps，差 40%。
 *            尾窗起点 = max(降速段结束, 总时长 − max(3s, 时长×35%))。
 *   D1-C03 邮箱有界：丢弃 ≤ 5% 投递
 *   D1-C04 帧龄有界：采样期内 latestFrameAgeMs 最大值 < 500ms
 *   D1-C05 上传健康：上传次数>0 且单次上传最大 < 100ms
 *   D1-C06 内容动态：相隔 ~700ms 的两次抓帧不同（Vulkan 已知黑屏缺陷 → SKIP）
 *   D1-C07 降级切换：P-BRG-04——降速窗口内 degraded=true，恢复后 false
 *         （STELQUICK_DYN_DEGRADE_PROBE=0 可关）
 *
 * 诊断/留证：STELQUICK_DYN_CSV=<path> 输出逐 250ms 采样 CSV（含生产者累计帧、
 *   邮箱计数、帧龄、降级标记），用于区分"生产者病态"与"窗口被 warmup 主导"。
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
class IFrameProducer;
class SkyViewport;

struct DynCheckResult
{
    bool ran = false;       // 自检是否真的执行（传入空生产者时 false）
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
        double producerFps = 60.0;    // 名义生产速率（仅用于判据下限口径）
        QSize physicalSize{1280, 720};
        qreal devicePixelRatio = 1.0;
        bool degradeProbe = true;     // D1-C07 降速探针
        double degradeFps = 15.0;     // 降级阈值（SkyViewport 判定用）
    };

    // 从环境变量读 Options（STELQUICK_DYN_SECONDS / STELQUICK_LIVE_FPS /
    // STELQUICK_DYN_DEGRADE_PROBE / STELQUICK_DEGRADE_FPS）。
    static Options optionsFromEnv();

    // 全程 GUI 线程定时器推进：采样 → 降速探针 → 汇总回调。
    //   · mailbox 必须已注入 SkyViewport；producer 必须**已启动**且生命周期长于本函数
    //     （自检收尾会调 producer->stop()，但不销毁它）；
    //   · producer 为空时立即回 ran=false（判据不成立，调用方应按 UNAVAILABLE 处理）。
    static void runStartupSequence(QGuiApplication *app,
                                   QQuickWindow *window,
                                   SkyViewport *viewport,
                                   FrameMailbox *mailbox,
                                   IFrameProducer *producer,
                                   const Options &options,
                                   const std::function<void(const DynCheckResult &)> &done);
};

} // namespace stelapp
