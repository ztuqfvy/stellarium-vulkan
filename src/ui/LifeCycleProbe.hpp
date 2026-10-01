/*
 * LifeCycleProbe — T44-A **生命周期数据面探针**（STELQUICK_LIFECYCLE_PROBE=1）。
 *
 * ── 这个探针存在的理由 ───────────────────────────────────────────────────────
 * 测试文档 §6.2 有 P-LIF-01..04 四条用例，且 §6.4 的窗口自测里写着：
 *   `test->window->hide();   // 关闭代理：隐藏窗口（真·关闭重开见 P-LIF ×100 回归）`
 *   —— 即 **A1 起就把"真·关闭重开 ×100"当欠账挂在 P-LIF 名下，一直没还**。
 * A-1.0 出口第 3 条要求 P-LIF-01…04 全部通过 ⇒ T44 就是来还这笔账的。
 *
 * 但写判据之前有一批**事实**没人验过（凭想象写必然出假绿/假红）：
 *   · `showMinimized()` 在 macOS 上到底让 `isExposed()` 变不变？场景图停不停？
 *   · 停的时候**生产者还在产帧吗**？邮箱会攒/丢吗？—— 这决定"帧队列有界"怎么判。
 *   · 恢复后帧号多久重新推进？(决定等待量)  恢复是否回到原几何？
 *   · `viewportGeneration` 是不是**每次尺寸变化恰好 +1**？同尺寸重设会不会**假递增**？
 *   · 窗口隐藏时 `displayedFrameNumber` 这个**观测面本身**还动不动？
 *     （若它也不动，就分不清"窗口没推进"与"数据面停更"—— 观测面必须先自证）
 *   · 进程级"关闭重开"单次墙钟多少？（判据 ×100 的预算依据）
 * 本探针**只报读数、不打 PASS**（与 ErrorProbe / 各 Probe 同一纪律）。
 *
 * ── 为什么用替身生产者（LiveFrameSource）而不是真实引擎 ─────────────────────
 * 引擎形态的"真实预热是 ~900 秒"（T9 实测，见 BUILD_RECORD）。P-LIF-01 要 ×100，
 * 引擎形态 100×(900s 预热) 在物理上不可行。生命周期验的是**帧通路与窗口/场景图
 * 资源**的寿命，与"帧从哪来"无关 ⇒ 用替身生产者（独立线程 + 自己的离屏上下文）
 * 是正确取舍。**产品代码路径零改动**：邮箱、SkyViewport、上传、上屏全是真的。
 *   ⚠️ 诚实性声明：本组判据**不覆盖**引擎 GL 资源在窗口生命周期上的释放；
 *      那一面由 T9/T13 的进程级长跑（进程退出即拆引擎）间接覆盖，此处如实标注。
 *
 * ── 观测面（全部走公开面，不碰私有）────────────────────────────────────────
 *  · `skyViewport`（objectName）的 Q_PROPERTY：`displayedFrameNumber` /
 *    `viewportGeneration` / `uploadCount` / `ready` —— 用 `QObject::property()`
 *    泛型读取（T33/T41 教训：`src/app` 与 `src/ui` 的检查一律通过 objectName
 *    找 `QQuickItem*`，不依赖具体类型，避免把层依赖倒过来）。
 *  · `FrameMailbox::Stats`：published / dropped / completeSlots（= 队列长度）/
 *    latestFrameAgeMs / sizeGeneration。
 *  · `QWindow`：isVisible / isExposed / geometry / windowState。
 *
 * ── 问题表 ─────────────────────────────────────────────────────────────────
 *   Q1  最小化前后的三元组：(isVisible, isExposed, windowState)，以及帧号增量。
 *   Q2  最小化期间**生产者**是否照常投递（published 增量）、邮箱是否攒/丢
 *      （completeSlots / dropped 增量）。
 *   Q3  恢复：`showNormal()` 是否回到原几何；**真·首帧延迟**（30ms 粒度轮询，
 *      1.5s 上限；⚠️ 不是"恢复后某时刻的总墙钟"—— 那样是名不符实，陷阱 65）。
 *   Q4  尺寸切换：`setGeometry` 每次是否让 `viewportGeneration` 恰好 +1；
 *      **同尺寸**重设是否假递增。⚠️ 假递增检查**两条都测**（否则是平凡真，陷阱 67）：
 *        Q4b-1 `setGeometry(当前几何)`（可能被 Qt 短路）
 *        Q4b-2 `resize(当前尺寸)`（绕过几何比较，走 resize 通路 —— 对应"窗口管理器
 *               重复发同尺寸 resize"这个真实场景）。
 *        若假递增，判据就不能拿世代当"重建发生"的证据。
 *   Q5  尺寸切换的代价：切换后**真·首帧延迟**（同 Q3 的 30ms 粒度轮询口径）。
 *   Q6  ×20 快速最小化/恢复：帧号是否每轮都重新推进（有无"恢复后再不推进"）。
 *      同时报**每轮增量序列**（只有 min/max/均值看不出"从第几轮开始坏"）。
 *   Q7  观测面自证：窗口隐藏期间 `displayedFrameNumber` 的读数行为。
 *   Q8  进程级：单次"启动 → 首帧上屏 → 退出"的墙钟秒数（×100 预算依据）。
 *
 * ── 调度机制（值得一看，改之前先读）────────────────────────────────────────
 *   步骤是**一次性**的（append 时就知道延迟），但"首帧延迟"必须细粒度反复采样。
 *   故调度器支持**重入**：step 体内设 `Ctx::retryCurrent = true` ⇒ 调度器**重入
 *   当前步**（延迟用 `Ctx::retryDelay`）而不是前进。Q3/Q5 就靠它做 30ms 轮询。
 */
#pragma once

#include <QString>
#include <QStringList>

#include <functional>

class QCoreApplication;
class QQuickWindow;

namespace stelapp {

class FrameMailbox;
class IFrameProducer;

class LifeCycleProbe
{
public:
    struct Result
    {
        bool ran = false;           //!< 探针是否跑完（false = 装配失败/被跳过）
        bool unavailable = false;   //!< 环境不满足（如窗口/视口缺失）⇒ UNAVAILABLE，非 FAIL
        QString summary;            //!< 一行摘要
        QStringList details;        //!< 逐条读数（`Q1`、`Q2`… 前缀，便于 grep）
        bool firstFrameReached = false;  //!< Q8 用：进程内是否观察到首帧上屏
        qint64 firstFrameMs = -1;        //!< Q8 用：启动到首帧墙钟毫秒
    };

    //! 全部在 GUI 线程。@p delayMs 后开跑（等 QML/场景图落定）。结果经 onDone 回传。
    //! @p producer 可为 nullptr（此时只跑不依赖帧流的项）。
    static void run(QCoreApplication *app,
                    QQuickWindow *window,
                    FrameMailbox *mailbox,
                    IFrameProducer *producer,
                    const std::function<void(const Result &)> &onDone,
                    int delayMs = 1500);

    //! **P-LIF-01 的单次腿**：进程级「启动 → 首帧上屏 → 退出」计时。
    //! 由外层脚本 `tools/a6-lifecycle-100.sh` 重复 N 次驱动（×100 的可行性依此预算）。
    //!
    //! @param processStartMs 进程起点墙钟毫秒（由 `main()` 开头取，
    //!        ⚠️ **不能用本函数进入时刻** —— 那样量到的是"引擎装配之后到首帧"，
    //!        不是"启动到首帧"，名字与实际所测不同义）。
    //! @param timeoutMs 首帧超时；超时按**真实失败**报（`firstFrameReached=false`），
    //!        不是 UNAVAILABLE —— 前提齐备而结果没出现，那就是失败。
    //! @return 经 onDone 回传；`Result::firstFrameMs` 即启动到首帧的墙钟毫秒。
    static void runStartupOnce(QCoreApplication *app,
                               QQuickWindow *window,
                               qint64 processStartMs,
                               const std::function<void(const Result &)> &onDone,
                               int timeoutMs = 6000);
};

} // namespace stelapp
