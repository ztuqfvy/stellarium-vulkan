/*
 * DisplayCheck — T38-C 显示参数自检（STELQUICK_DISPLAY_CHECK=1）。
 *
 * ── 设计依据（全部来自 T38-A 探针的"实测结论"，见 app/DisplayProbe.hpp）──────
 *   · 引擎四个亮度/星等 setter **不夹取** ⇒ 范围闸在 AppFacade ⇒ DP-02 判它；
 *   · `maxFov` **随投影变**（120/235/360）⇒ DP-03 判它是活属性；
 *   · 投影非法 key **静默兜底 Stereographic** ⇒ 白名单闸 ⇒ DP-04 判它；
 *   · `getLimitMagnitude()` ≠ 用户设定 ⇒ 判据只读写 `customStarMagLimit`
 *     （读错量 = 结论必错，这是 T38 要躲的假绿形态）；
 *   · 引擎 NOTIFY 静置期 0 发射 ⇒ 可以绑定 ⇒ DP-07 判"引擎改 ⇒ UI 跟"。
 *
 * ── 环境前提（S1 布场，收尾全部还原）────────────────────────────────────────
 *   冻结仿真（帧静定，像素比较才有意义）+ **大气置关**。
 *   大气为什么必须关：① 星等/亮度的**视觉效应只有星星可见时才存在**，白昼基帧里
 *   本来就没有星星（T37 血泪：把"本来就没有"当成"消失了"）；② 关掉大气后天空背景
 *   是暗星空，星点改动的像素占比才不会被白昼亮度淹没。
 *
 * ── 判据清单（DP-01..DP-11）────────────────────────────────────────────────
 *   【数据面】
 *   DP-01 **状态面往返**：5 项（相对亮度/星点尺寸/夜空亮度/极限星等/星等开关）
 *         façade 写 → **引擎 getter 独立回读**（陷阱 43：不复述 façade 自己的读法）。
 *   DP-02 **范围闸**：写越界（99）⇒ 引擎收到的是**上界** ∧ refusal=="out-of-range"；
 *         写合法值 ⇒ refusal=="ok"。
 *   DP-03 **投影 12 key 往返 + maxFov 活属性**：逐 key 写 ⇒ 引擎 key 一致；
 *         且 maxFov 随投影取到**≥3 个不同值**（证明它不是常量）。
 *   DP-04 **投影白名单闸**：写乱码 ⇒ 返回 false ∧ 引擎 key **不变** ∧ refusal 正确。
 *   DP-05 **拒绝理由必须是 Q_PROPERTY**（T19 血泪：Q_INVOKABLE 在 QML 绑定里
 *         读到函数对象、比较恒 false）⇒ 元对象层断言"是属性 ∧ 有 NOTIFY"。
 *   【UI 腿（视觉树递归 —— T34 陷阱 45：Repeater delegate 的 QObject 父链是空的）】
 *   DP-06 **控件齐备**：navDisplayButton + 5 个滑块/开关 + 12 个 proj_* 按钮
 *         （后者由 Repeater 动态生成 ⇒ 只能走视觉树）。
 *         🔴 **首轮实测 0/12** —— 抓到一个**真产品缺陷**（详见下方"首轮反哺"）。
 *   DP-07 **绑定腿**：引擎侧改 relativeStarScale ⇒ UI 滑块 value 跟随
 *         （守 T15 铁律：绑定必须真的读属性）。
 *   【帧级（成对 + 判别对照）】
 *   DP-08 **视场生效**：改视场 ⇒ 上游帧差异**超出噪声容差**（成对：同一方法对
 *         判别对照——大气开关——必须测出显著差异）。
 *   DP-09 **投影生效**：切 Fisheye ⇒ 上游帧显著变化。
 *   DP-10 **星等截断生效**（两条）：
 *         DP-10a **静置对照**：状态不变时连抓两帧，亮像素计数漂移 ≤ 门
 *         （证明这个**新引入的方向量**在噪声下是稳的，不是抖动凑数）；
 *         DP-10b **LOW↔HIGH 成对方向量**：极限星等 LOW（下界 0，星表近乎清空）↔ HIGH
 *         （15，全开）两状态各抓一帧，亮像素数之差 ≥ 门 ⇒ 星表截断跟着设定值走。
 *         为什么不用"全图差异占比"：星点像素占比天然小，会被地面/背景稀释；用
 *         LOW↔HIGH 成对则**不依赖"初值长什么样"**，方向单一、承重明确。
 *   DP-11 **复原**：全部还原 ⇒ 引擎 getter 回初值 ∧ 复原帧与参考帧低于噪声容差。
 *
 * ── 首轮实测反哺（首跑 10/13，三条 FAIL 一条都不是"调参"能修的）────────────
 *   🔴 DP-06 0/12 —— **产品缺陷**：`AppFacade::projectionTypeKeys()` 原本是
 *      `Q_INVOKABLE`，而合流形态**先 engine.load()（QML 起来）、后 boot()（引擎起来）**
 *      ⇒ QML 里 `model: appFacade.projectionTypeKeys()` 这种**函数式绑定不读任何属性**
 *      ⇒ 没有依赖 ⇒ 只求值一次、那一次拿到空表 ⇒ **12 个投影按钮一个都不出现且永不
 *      自愈**（真机同路径同样空）。已改成 `Q_PROPERTY + NOTIFY`，并由
 *      `ensureDisplayParamsForwarding()` 末尾补一次"开机唤醒" emit。
 *   🔴 DP-11 红 —— **基准被污染**：数据面 DP-01..DP-03 改过 `absoluteStarScale` /
 *      `lightPollutionLuminance` / `flag+customMag` 且**没还原**就去抓"参考帧"
 *      （实测参考帧 `nonBlack` 被光污染顶到 1.0000）⇒ DP-09/10/11 全比在假基准上。
 *      已加 `Ctx::restoreAllDisplayParams()`，参考帧前强制全量还原。
 *   🔴 DP-08 红（且**逐位相同**）—— **延迟挂错步**：`delayAfter` 的语义是"跑完本步
 *      之后等"，首轮挂到了**读步** ⇒ 写后 0ms 抓帧 ⇒ 拿到上一次的帧缓冲（哈希都相同）。
 *      已挪到写步（陷阱 41/69 的原样复现）。
 *
 * ── 第二轮实测反哺（12/13 → 判据口径两处修正）────────────────────────────
 *   🔴 **噪声容差口径**：冻结下的亚 LSB 抖动**面积会随场景状态变、幅度不会** ——
 *      DP-00 实测 0.079%/Δ1，而"对照量还原核对"（大气开关来回后同一状态）实测
 *      **0.557%/Δ2**（面积涨 7 倍，幅度只从 1 到 2）⇒「低于噪声容差」**只能拿幅度
 *      当判据**（`maxDelta ≤ kNoiseMaxDelta`）；面积当必要条件会造出假红。面积只留给
 *      DP-00 当"帧到底静没静下来"的粗门（`kNoiseGateRatio`）。
 *   ⚠️ DP-10 撤掉一条**自造的伪命题**：原本额外要求"HIGH 帧 vs 参考帧A' 差异 ≥1%"，
 *      实测 HIGH 与参考帧A' **逐位相同**（哈希相同）—— 初值（`flag` 关 ⇒ 走引擎自算的
 *      有效限制）本来就是"全开"。那条实在验证"15 与初值不同"，**从未被声称过** ⇒
 *      必然红。判据只能验被声称的命题，多验一条就是自造红项。
 *      ⇒ DP-10 拆成 **10a 静置对照**（新引入的"亮像素方向量"必须先证明在噪声下稳定）
 *      + **10b LOW↔HIGH 成对方向量**（才是被声称的命题）。
 *
 * ── 负控开关（证明判据承重；红项集合**两两不同**）──────────────────────────
 *   A `STELQUICK_DISPLAY_GATE_OFF=1`  ⇒ 关范围闸+白名单闸 ⇒ 恰好红 [DP-02, DP-04]
 *   B `STELQUICK_DISPLAY_FWD_OFF=1`   ⇒ 断引擎→façade **订阅** ⇒ 恰好红 [DP-07]
 *   （B 刻意**不拦**开机唤醒那一次 emit —— 拦截的话会连 DP-06 一起打红，
 *     "恰好红 [DP-07]"这个口径就没了。两个负控各管一件事。）
 *   （B 同时不影响 DP-11 的 getter 读回 —— 订阅只管信号，不管 getter。）
 *
 * ── 退出码（由 main.cpp 决定）──────────────────────────────────────────────
 *   0=PASS，10=FAIL，6=UNAVAILABLE / INCONCLUSIVE（竞态型读数不硬判 FAIL）。
 */
#pragma once

#include <QString>
#include <QStringList>
#include <functional>

class QCoreApplication;
class QQuickWindow;

namespace stelapp {

class AppFacade;
class ActionRouter;
class FrameMailbox;

class DisplayCheck
{
public:
    struct Result
    {
        bool ran = false;
        bool pass = false;
        bool unavailable = false;
        bool inconclusive = false;
        int passed = 0;
        int total = 0;
        QString summary;
        QStringList details;
    };

    //! 全部在 GUI 线程。@p delayMs 后开跑（等引擎动作注册落定）。结果经 onDone 回传。
    static void run(QCoreApplication *app,
                    AppFacade *facade,
                    ActionRouter *router,
                    QQuickWindow *window,
                    FrameMailbox *mailbox,
                    const std::function<void(const Result &)> &onDone,
                    int delayMs = 2500);
};

} // namespace stelapp
