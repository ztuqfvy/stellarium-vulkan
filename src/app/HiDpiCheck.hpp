/*
 * HiDpiCheck — T39-C **高 DPI + 渲染诊断自检**（STELQUICK_HIDPI_CHECK=1）。
 *
 * ── 设计依据（全部来自 T39-A 探针实测，见 app/HiDpiProbe.hpp）────────────────
 *   ① 三个高 DPI setter **一个都不夹取** ⇒ 范围闸在 AppFacade（负控 A 证明承重）。
 *   ② `getGuiFontSize()` 读的就是全局字体 ⇒ **判据只读写 façade 值 + 引擎 getter
 *      独立回读**（陷阱 43：不复述自己的读法），不拿它当"用户设定值"真源。
 *   ③ `screenFontSize` 有强帧效应（噪声底**逐位相同** → 13→40 差异 **4.054%**，
 *      还原后**逐位回到原哈希**）⇒ 帧级判据（成对 + 判别对照）有充足信噪比。
 *   ④ 静置 1.5s 三条 NOTIFY 增量 0 ⇒ 可以安全绑定（不是每帧连续量）。
 *
 * ── 环境前提 ────────────────────────────────────────────────────────────────
 *   · **冻结仿真**（帧静定 —— 像素比较的前提；探针首轮漏了这步，帧差 99.99% 全是
 *     时间流逝的污染，零判别力）。
 *   · **不动大气**（探针已证：默认布场里天空文本可见，字号效应 4.054% —— 比
 *     T38 那样专门关大气更省事，且更接近用户真实看到的布场）。
 *
 * ── 🔴 噪声门限必须同布场实测（探针的新血泪）────────────────────────────────
 *   T38 布场（冻结+大气关，暗天空）噪声 0.079%/Δ1-2；本布场（默认布场）噪声
 *   **逐位相同（哈希一致）** ⇒ **门限不能跨布场搬** —— HP-00 先在同布场量噪声底，
 *   后面的帧级判据全部以它为参照。
 *
 * ── 判据清单（12 条）────────────────────────────────────────────────────────
 *   【状态面】
 *   HP-00 **噪声底门**：冻结后同状态两帧低于噪声容差（幅度口径）—— 环境门。
 *   HP-01 **引擎面往返**：façade 写 33 ⇒ façade 读回 33 ∧ **引擎 getter 独立回读** 33。
 *   HP-02 **范围闸（screenFontSize）**：写 99 → 50、写 0 → 5；引擎独立回读一致；
 *         refusal 记 out-of-range。（原版 SpinBox [5,50]）
 *   HP-03 **范围闸（guiFontSize）**：写 99 → 50、写 0 → 7。（原版 SpinBox [7,50]）
 *   HP-04 **范围闸（screenButtonScale）**：写 999 → 200、写 0 → 50。
 *         ⚠️ [50,200] 是**产品自定**（原版只有配置键没有控件），文档已标注。
 *   HP-05 **派生量算术验算**：引擎 `getScreenScale()` == dpp × (字号/13)，
 *         `getGuiScale()` 同理 —— 独立算一遍对比（陷阱 43）。
 *   【UI 腿（视觉树递归 —— 陷阱 45）】
 *   HP-06 **控件齐备**：displayScreenFontSlider / displayScreenFontValue /
 *         displayScaleStatusLabel 存在 ∧ 滑块 from/to == façade 范围常量 ∧
 *         value == façade.screenFontSize。
 *   HP-07 **绑定腿**：**引擎侧**改字号 ⇒ UI 滑块/标签跟随（守 T15 铁律）。
 *   HP-08 **交互后绑定仍活**（比 T38 DP-07 更强的一条）：先用 `increase()` 模拟
 *         用户交互（验证 onMoved 通路），**再**引擎侧改 ⇒ UI 仍要跟随。
 *         —— Qt 6 的 Slider 若交互打破绑定，只有"交互后再看"才测得出
 *         （T38 DP-07 没交互过，测不到这个形态）。
 *   【帧级（成对 + 判别对照）】
 *   HP-09 **字号帧效应**：噪声底 → façade 写 40 ⇒ 上游帧差异 ≥ 效应门；
 *         还原初值 ⇒ 帧回噪声级（判别对照：效应可逆，别的东西没在动）。
 *   【数据面与复原】
 *   HP-10 **诊断数据面健康**：published>0 ∧ dropped≤published ∧ completeSlots∈
 *         [0,槽位] ∧ **bytesPerFrame == 宽×高×4**（用抓帧的 FrameSample 独立验算，
 *         刻意不走 takeLatestFrame —— 那会给 leased +1，诊断污染被诊断的量）。
 *   HP-11 **复原**：全部 getter 回初值 ∧ 复原帧与参考帧低于噪声容差。
 *
 * ── 负控开关（红项集合两两不同）────────────────────────────────────────────
 *   A `STELQUICK_DISPLAY_GATE_OFF=1`（复用 T38 的"关范围闸"）⇒ 恰红
 *     [HP-02, HP-03, HP-04]
 *   B `STELQUICK_DISPLAY_FWD_OFF=1`（复用 T38 的"断引擎→façade 订阅"）⇒ 恰红
 *     [HP-07, HP-08]（引擎侧改动不刷 QML）
 *
 * ── 退出码（由 main.cpp 决定）──────────────────────────────────────────────
 *   0=PASS，10=FAIL，6=UNAVAILABLE / INCONCLUSIVE。
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

class HiDpiCheck
{
public:
    struct Result
    {
        bool ran = false;
        bool unavailable = false;   //!< 环境里跑不起来（非合流形态/引擎未引导）
        bool inconclusive = false;  //!< 竞态型读数/异常帧/交互模拟无效 —— 不硬判 FAIL
        bool pass = false;
        int passed = 0;
        int total = 0;
        QString summary;
        QStringList details;
    };

    static void run(QCoreApplication *app,
                    AppFacade *facade,
                    QQuickWindow *window,
                    FrameMailbox *mailbox,
                    const std::function<void(const Result &)> &onDone,
                    int delayMs = 2500);
};

} // namespace stelapp
