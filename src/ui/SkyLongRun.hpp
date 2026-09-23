/*
 * SkyLongRun — P-BRG-01 消费侧全量计量长跑（30 分钟稳定性与吞吐验收）。
 *
 * 与 T9（LegacyLongRun，产帧侧）的定位差异：
 *   - T9 量的是**引擎产帧管道**：renderOneFrame → readback → 邮箱，逐帧 CSV；
 *   - 本类量的是**消费侧通路**：邮箱 → 场景图线程上传纹理 → 上屏，
 *     逐秒 CSV + 逐帧上屏间隔 CSV，回答"帧流长时间喂进真实窗口是否稳定"。
 *   两者共用同一套冻结门槛口径（测试文档 §6.1.1 的协议与量级），但指标语义不同，
 *   故判据编号单列（SL-Cxx），门槛在本次数据上按消费侧口径落定。
 *
 * 跑法：STELQUICK_LONGRUN=1 ./stelQuickUI
 *   （截图/像素类判据不在本跑范围内；本跑只看计量与稳定性）
 *
 * 判据（退出码 0 全过 / 8 有 FAIL / 9 环境不合规或数据无效——前置拒绝、或测量中
 * 窗口暴露不全判 INVALID）：
 *   SL-C01 生产者健康：LiveFrameSource 零失败，稳态生产速率 ≥40 fps
 *   SL-C02 显示吞吐：稳态显示帧率（displayedFrameNumber 增量）≥40 fps
 *   SL-C03 上屏节奏：稳态 frameSwapped 间隔 p95 ≤30ms、p99 ≤45ms
 *   SL-C04 邮箱完整：测量段邮箱丢弃 = 0
 *   SL-C05 上传健康：测量段上传次数 >0、均值 <5ms、单次最大 <100ms
 *   SL-C06 帧龄有界：测量段邮箱帧龄最大值 <500ms
 *   SL-C07 内存稳定：phys_footprint 稳态线性斜率 ≤1.0 MiB/min
 *   SL-C08 环境未漂移：测量期间始终接电且低电量模式关闭（每 60s 复核）
 *   SL-C09 窗口暴露：测量段窗口始终 exposed（被遮挡/最小化会让显示数据不可信）
 *   SL-C10 降级未误报：测量段 degraded=true 的采样点占比 ≤1%（P-BRG-04 不误报）
 *
 * 环境前置（SL-C00，不入判据列表，是硬门）：
 *   门槛冻结协议要求"接通电源 + 关闭低电量模式"。不合规直接以退出码 9 拒绝测量，
 *   不产生任何测量数据（2026-09-22 T9 被降频污染的教训：污染会静默通过计量判据）。
 *
 * 环境变量：
 *   STELQUICK_LONGRUN=1                    触发（src/ui/main.cpp 分流）
 *   STELQUICK_LONGRUN_WARMUP_SECONDS=N     预热秒（默认 900）
 *   STELQUICK_LONGRUN_SECONDS=N            测量秒（默认 1800 = 30 分钟）
 *   STELQUICK_LONGRUN_CSV=<path>           逐秒 CSV（默认 /tmp/stelsky-longrun.csv）
 *   STELQUICK_LONGRUN_FRAMES_CSV=<path>    逐帧上屏间隔 CSV（默认 <逐秒 CSV>.frames.csv）
 *   STELQUICK_LONGRUN_FPS=<f>              生产者名义速率（默认 60）
 *   STELQUICK_LONGRUN_SIZE=WxH             离屏尺寸（默认 1280x720）
 *   STELQUICK_LONGRUN_ALLOW_THROTTLED=1    跳过环境前置与漂移判定（仅调试；正式验收禁止）
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
class SkyViewport;

struct SkyLongRunOptions
{
    int warmupSeconds = 900;
    int measureSeconds = 1800;
    double producerFps = 60.0;
    QSize physicalSize{1280, 720};
    qreal devicePixelRatio = 1.0;
    QString csvPath;
    QString framesCsvPath;
    bool allowThrottled = false;
};

struct SkyLongRunResult
{
    bool ran = false;         //!< 是否真的完成了整跑（装配失败/环境拒绝为 false）
    bool envBlocked = false;  //!< 环境前置拒绝 → 退出码 9
    bool dataInvalid = false; //!< 测量中窗口暴露不全 → 数据被遮挡污染，判 INVALID（退出码 9）
    bool pass = false;
    int checkCount = 0;
    int failCount = 0;
    QString summary;
    QStringList details;
};

class SkyLongRun
{
public:
    //! 从环境变量读运行参数（见头注释的变量表）。
    static SkyLongRunOptions optionsFromEnv();

    //! 启动长跑；完成后经 done 回调返回结果（回调内负责 app.exit）。
    //! mailbox 必须已注入 viewport；生产者由本函数启动与停止。
    static void runStartupSequence(QGuiApplication *app,
                                   QQuickWindow *window,
                                   SkyViewport *viewport,
                                   FrameMailbox *mailbox,
                                   const SkyLongRunOptions &options,
                                   const std::function<void(const SkyLongRunResult &)> &done);
};

} // namespace stelapp
