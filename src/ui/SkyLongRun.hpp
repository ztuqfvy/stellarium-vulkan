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
 *   SL-C01 生产者健康：生产者零失败，稳态生产速率 ≥40 fps
 *   SL-C02 显示吞吐：稳态显示帧率（displayedFrameNumber 增量）≥40 fps
 *   SL-C03 上屏节奏：稳态 frameSwapped 间隔 p99 / max（**形态相关口径**，见下）
 *   SL-C04 邮箱完整：测量段邮箱丢弃 = 0
 *   SL-C05 上传健康：测量段上传次数 >0、均值 <5ms、单次最大 <100ms
 *   SL-C06 帧龄有界：测量段邮箱帧龄最大值 <500ms（逐秒口径）
 *   SL-C07 内存稳定：phys_footprint 稳态线性斜率 ≤1.0 MiB/min（**只判增长**；
 *                     负斜率 = 引擎释放 warmup 期缓存，属正常，不判违规）
 *   SL-C08 环境未漂移：测量期间始终接电且低电量模式关闭（每 60s 复核）
 *   SL-C09 窗口暴露：测量段窗口始终 exposed（被遮挡/最小化会让显示数据不可信）
 *   SL-C10 降级未误报：测量段 degraded=true 的采样点占比 ≤1%（P-BRG-04 不误报）
 *   SL-C11 GUI 线程卡顿：稳态邮箱帧龄 p95 ≤ 门槛（高频采样，T13 新增）
 *         —— 这答的是 §7.4「GUI 与 GL 同线程卡顿」风险：合流形态下引擎 draw 与
 *            QML 场景图渲染共占 GUI 线程，若引擎单帧开销大到挤占事件循环，帧会
 *            在邮箱里排队变"老"。SL-C06 的 max 只抓单次异常，p95 才反映常态。
 *
 * 生产者可切（T13，与 DynFrameCheck 同构）：
 *   本类**不再自建生产者**，由调用方（main.cpp）按类型装配后传入 IFrameProducer*：
 *     · LiveFrameSource —— 替身场景（独立线程 + kOwn 上下文）
 *     · LiveSkyRuntime   —— 真实引擎（GUI 线程 QTimer + 借引擎上下文）
 *   原因：两者装配差异巨大（后者必须先 boot 引擎），装配属调用方职责；装配差异
 *   不进自检，自检只吃"装配成功之后"的 IFrameProducer 契约。
 *
 * SL-C03 的形态相关口径（T13，2026-09-23）：
 *   上屏间隔的**正常栅格**由 max(vsync 周期, 生产者帧间隔) 决定，不是恒为 vsync 周期：
 *     · 生产者独立线程、顶到 vsync（test 形态）→ 栅格 16.7/33.4ms，
 *       "漏一帧"=33.4ms → 绝对门槛 p99≤50 / max≤100（2026-09-23 冻结值，**未改动**）
 *     · 生产者与消费侧共占 GUI 线程（engine 形态）→ 引擎 tick 牵引 QML 出帧，
 *       实测 p50 由 16.71ms 变 21.35ms、帧间隔 18.2→23.3ms，"漏一帧"=46.7ms
 *       → 改用相对口径 p99 ≤ min(3×帧间隔, 100ms)、max ≤ min(6×帧间隔, 200ms)
 *   绝对天花板（100/200ms）是必要的：只给相对口径会让"生产者极慢"的负控失效
 *   （5fps 时 3×帧间隔 = 600ms，病态反而通过）。
 *   形态由 SkyLongRunOptions::producerSharesGuiThread 显式声明，**不从数据倒推**。
 *
 * 环境前置（SL-C00，不入判据列表，是硬门）：
 *   门槛冻结协议要求"接通电源 + 关闭低电量模式"。不合规直接以退出码 9 拒绝测量，
 *   不产生任何测量数据（2026-09-22 T9 被降频污染的教训：污染会静默通过计量判据）。
 *
 * 环境变量：
 *   STELQUICK_LONGRUN=1                    触发（src/ui/main.cpp 分流）
 *   STELQUICK_LONGRUN_PRODUCER=engine      生产者切真实引擎（默认替身场景；T13 新增）
 *   STELQUICK_LONGRUN_WARMUP_SECONDS=N     预热秒（默认 900）
 *   STELQUICK_LONGRUN_SECONDS=N            测量秒（默认 1800 = 30 分钟）
 *   STELQUICK_LONGRUN_CSV=<path>           逐秒 CSV（默认 /tmp/stelsky-longrun.csv）
 *   STELQUICK_LONGRUN_FRAMES_CSV=<path>    逐帧上屏间隔 CSV（默认 <逐秒 CSV>.frames.csv）
 *   STELQUICK_LONGRUN_FPS=<f>              生产者名义速率（默认 60；engine 下 50）
 *   STELQUICK_LONGRUN_SIZE=WxH             离屏尺寸（默认 1280x720）
 *   STELQUICK_LONGRUN_AGE_MS=<ms>          帧龄高频采样间隔（默认 100；SL-C11 口径）
 *   STELQUICK_LONGRUN_STALL_MS=<ms>        **负控**：每秒在 GUI 线程忙等该毫秒数，
 *                                          人为制造上屏停顿——SL-C03/SL-C11 的可红性
 *                                          证据（正式验收禁用）
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
class IFrameProducer;
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
    //! 生产者是否与消费侧**共占 GUI 线程**（engine 形态为 true）。只影响 SL-C03
    //! 的口径选择：共线程时引擎 tick 会牵引 QML 上屏栅格（实测 p50 由 vsync 的
    //! 16.7ms 变成引擎帧间隔 23ms），绝对毫秒门槛不再适用，改用相对口径。
    //! 这是**形态属性**（客观事实），不是从数据反推——不从数据倒推门槛。
    bool producerSharesGuiThread = false;
    //! SL-C11 帧龄高频采样间隔（ms）。逐秒采样算 p95 只有 1800 个点、且是"每秒瞬时
    //! 值"，抓不住秒级以下的排队长尾；100ms 粒度在 30 分钟窗内给 1.8 万个样本。
    int frameAgeSampleMs = 100;
};

struct SkyLongRunResult
{
    bool ran = false;         //!< 是否真的完成了整跑（装配失败/环境拒绝为 false）
    bool envBlocked = false;  //!< 环境前置拒绝 → 退出码 9
    bool dataInvalid = false; //!< 测量中窗口暴露不全 → 数据被遮挡污染，判 INVALID（退出码 9）
    bool pass = false;
    int checkCount = 0;
    int failCount = 0;
    //! SL-C11 稳态帧龄分布（高频采样口径；诊断与门槛判定共用）
    double steadyAgeMeanMs = 0.0;
    double steadyAgeP95Ms = 0.0;
    double steadyAgeP99Ms = 0.0;
    double steadyAgeMaxMs = 0.0;
    quint64 steadyAgeSamples = 0;
    QString summary;
    QStringList details;
};

class SkyLongRun
{
public:
    //! 从环境变量读运行参数（见头注释的变量表）。
    static SkyLongRunOptions optionsFromEnv();

    //! 启动长跑；完成后经 done 回调返回结果（回调内负责 app.exit）。
    //! mailbox 必须已注入 viewport。**producer 必须已装配并运行**（本函数不负责
    //! 装配，也不负责其生命周期；仅在收尾调用一次 producer->stop() 停产帧）。
    static void runStartupSequence(QGuiApplication *app,
                                   QQuickWindow *window,
                                   SkyViewport *viewport,
                                   FrameMailbox *mailbox,
                                   IFrameProducer *producer,
                                   const SkyLongRunOptions &options,
                                   const std::function<void(const SkyLongRunResult &)> &done);
};

} // namespace stelapp
