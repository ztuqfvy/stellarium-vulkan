/*
 * TimeLinkProbe — T35-A **仿真时间链路数据面探针**（STELQUICK_TIMELINK_PROBE=1）。
 *
 * ── 为什么这一轮要探针（T33/T34 同款纪律）────────────────────────────────────
 * T27 留档了一条"待查线索"：**HostDriven 帧泵 400ms 推 0.61 天（≈1.5 天/秒），
 * 与 `getTimeRate()` 读数 10 脱钩，差 4 个数量级**，并建议"若属设计需文档说明，
 * 否则是独立的时间链路缺陷"。这条线索悬了 T28..T34 六个任务，**它决定所有时间类
 * 判据的口径是否可信**（时间类判据一律以"帧泵推进量"为基线）。
 *
 * 但复查代码后发现，那句结论的**结论链**本身可疑（见 TimeLinkCheck.hpp 头注）：
 *   ① `getTimeRate()` 的单位是 **JDay/sec**（`StelCore.hpp:595` 原文），
 *      而 T27 的算术 `10 × JD_SECOND × 0.4s = 4.6e-5 天` 把它当成了"× 实时倍率"；
 *   ② `engineRate` 读数在 INTERACTCHECK 里是**移动靶** —— IT-05 自己就注入 L 键
 *      （`increaseTimeSpeed()`，阶梯 ×10），同一个进程里 rate 会被套件自己改掉；
 *   ③ "400ms" 是**相位名义延迟**，从未与 ΔJD **同一时刻**量过真实墙钟窗口。
 * ⇒ 三处任一处错都能独立造成"看起来差 4 个数量级"。**先探针，把三个量在同一时刻
 *   量齐再下结论。**
 *
 * ── 探针不是判据（刻意）────────────────────────────────────────────────────
 * 只报读数、不下 PASS/FAIL（断言在 T35-C 的 TimeLinkCheck）。
 *
 * ── 探针要回答的六个问题 ─────────────────────────────────────────────────
 *   Q1 时钟面：时钟模式 / 四个读面（simClockJD、getJD、facade.julianDay、
 *      getTimeRate）各自的值 —— 先证"只有一个时间源"。
 *   Q2 链路原始读数（**核心**）：在自测墙钟窗口 W 内量 ΔJD，与
 *      `W × rate × scale` 并列打印，给出**比值**与**反推窗口** `ΔJD/rate`。
 *      （比值 ≈ 1 ⇒ 无脱钩；反推窗口 ≈ 实测 W ⇒ T27 的"1.5 天/秒"只是窗口长度问题。）
 *   Q3 窗口线性：同一 rate 下两个不同长度的窗口，ΔJD 之比应等于 W 之比
 *      —— 直接反驳"帧泵按固定步长推进、rate 只管别的语义"这一（README 里
 *      自己列出的）备选假设。
 *   Q4 速率阶梯台账：`increaseTimeSpeed()` 连按三级，打印每级 rate
 *      —— 解释 INTERACTCHECK 里 `engineRate` 读数为何在 0.1 / 1 / 10 之间跳。
 *   Q5 冻结面：scale=0 窗口内 ΔJD（应 ≈ 0）+ 恢复后一窗口（不应补冻结期）。
 *   Q6 天文腿原始读数：**把链路验到画面/坐标系上** ——
 *      Q6b 恒星时恒等式 ΔLST vs ΔJD × 360.9856°（当地恒星日）；
 *      Q6c 固定 J2000 方向经引擎 `j2000ToAltAz` 的落点转角（证明下游真的重算了）；
 *      Q6d 视线的 ΔRA（核验 T27「视线锁地平 ⇒ 随 JD 漂移」这句话的**机理**成不成立）。
 */
#pragma once

#include <QString>
#include <QStringList>
#include <functional>

class QCoreApplication;

namespace stelapp {

class AppFacade;

class TimeLinkProbe
{
public:
    struct Result
    {
        bool ran = false;          //!< 真正跑过（前置满足）
        bool unavailable = false;  //!< 环境里跑不起来（合流形态缺失/引擎引导失败）
        QString summary;
        QStringList details;       //!< 逐行读数（前缀已在 main.cpp 侧统一加）
    };

    //! 全部在 GUI 线程。@p delayMs 后开跑（等引擎引导与帧泵稳态）。结果经 onDone 回传。
    static void run(QCoreApplication *app,
                    AppFacade *facade,
                    const std::function<void(const Result &)> &onDone,
                    int delayMs = 2500);
};

} // namespace stelapp
