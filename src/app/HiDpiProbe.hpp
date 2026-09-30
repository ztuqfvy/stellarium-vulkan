/*
 * HiDpiProbe — T39-A **高 DPI + 渲染诊断数据面探针**（STELQUICK_HIDPI_PROBE=1）。
 *
 * ── 为什么第一相位是探针（T24/T33/T34/T35/T37/T38 同款纪律）──────────────────
 * A-1.0 范围表最后一个"必须"格：
 *   `|亮度/星等、视场、投影、主题/夜视、**高 DPI** | 基础 | 必须 | ... |`
 * T37 做掉"主题/夜视"，T38 做掉前三项，**高 DPI 是本格的收尾**。
 * 同一张表还有一格：
 *   `|资源路径、错误提示、**渲染诊断**、配置保存 | 最小 | 必须 | ... |`
 * T36 只做掉了"配置保存"（目录隔离）；"渲染诊断"目前只有 `DiagnosticPage.qml` 的
 * **静态后端信息**（API/设备/驱动/DPR），**没有任何运行时量**。
 *
 * ── 开工前的代码审计（推断，不是证据）──────────────────────────────────────
 * 引擎侧的高 DPI 面（全部已是 Q_PROPERTY，**带 NOTIFY**）：
 *   · `StelApp.screenFontSize`     int    —— 天空文本字号（星名/地景/星座名…）
 *     默认 `DEFAULT_FONT_SIZE` = **13**（`StelUtils.hpp:102`）；原版控件范围 **[5, 50]**。
 *   · `StelApp.guiFontSize`        int    —— GUI 面板字号；原版范围 **[7, 50]**。
 *   · `StelApp.screenButtonScale`  double —— 屏幕按钮尺寸**百分比**，默认 **100**。
 * 派生量（**可算术验算**，是判据的理想靶子）：
 *   `getScreenScale() = getDevicePixelsPerPixel() × screenFontSizeRatio()`
 *   `getGuiScale()    = getDevicePixelsPerPixel() × guiFontSizeRatio()`
 *   `...Ratio()       = 当前值 / getDefaultGuiFontSize()`（=13）
 * ⚠️ 审计发现两条**可疑**（探针要坐实或否掉）：
 *   ① **`getGuiFontSize()` 读的不是内部缓存，而是 `QGuiApplication::font().pixelSize()`**
 *      （`StelApp.cpp:1572-1575`）⇒ 任何**绕过 setter** 改全局字体的代码都会把"用户设定值"
 *      污染掉。这是 T38 那条血泪（`getLimitMagnitude()` 不是用户设定值）的**同族**：
 *      **读错了量的名字**。若成立 ⇒ 产品侧与判据都**不能**拿它当"用户设定"的真值。
 *   ② **`setGuiFontSize()` 直接 `QGuiApplication::setFont()`**（`StelApp.cpp:1561-1571`）
 *      ⇒ **全局**副作用。而本项目 QML 侧的控件**普遍显式设了 `font.pixelSize`**
 *      ⇒ 大概率**完全不跟随**（引擎的 guiFontSize 只管老 QWidget 对话框）。
 *      若成立 ⇒ "高 DPI"在 QML 侧必须**自己的缩放系数**，不能指望引一引引擎属性就完事。
 * 渲染诊断的数据面（`FrameMailbox::Stats`，任意线程可读）：
 *   `published / dropped / leased / sizeGeneration / completeSlots / readersHeld /
 *    latestFrameAgeMs / bytesPerFrame` + `latestCompletedFrameNumber()`
 *   —— 这是"渲染诊断"从"静态后端信息"升级成"运行时健康度"的**现成素材**，
 *   但**从未在任何 UI 上暴露过**。探针先量清楚读数的量级与稳定性。
 *
 * ── 探针要回答的问题 ─────────────────────────────────────────────────────
 *   Q1 **三个高 DPI 属性的写读往返**：逐项（名 / 类型 / 初值 / 写入值 / 回读值 /
 *      是否一致 / **setter 有没有夹取**）。写入值刻意取"远"值（99/99/999）暴露夹取。
 *   Q2 **默认值与派生量算术验算**：`dpp` / 两个 ratio / 两个 scale 读数 +
 *      用 `dpp × ratio` **独立算一遍**与引擎读数对比（陷阱 43：不复述自己的读法）。
 *   Q3 🔴 **`getGuiFontSize()` 的来源**：绕过 setter 直接 `QGuiApplication::setFont()`
 *      改 pixelSize ⇒ `getGuiFontSize()` 是否跟着变（⇒ 证明它读全局字体，不是缓存）。
 *   Q4 🔴 **`setGuiFontSize()` 的波及面**：改字号后**递归扫 QML 视觉树**里所有带
 *      `font` 属性的项，逐项对比 `pixelSize` —— 有多少跟随、多少不跟随。
 *      （T34 陷阱 45：Repeater delegate 的 QObject 父链是空的 ⇒ 一律走视觉树。）
 *   Q5 **`screenFontSize` 是否真的影响引擎渲染**：抓上游帧做前后对比（帧级）。
 *      只报读数（有没有超出噪声量级），判不判在 T39-C。
 *   Q6 **NOTIFY 静置频率**：三个信号在静置期的增量（决定 QML 能否安全绑定）。
 *   Q7 **渲染诊断数据面**：`FrameMailbox::Stats` 全部字段 + 帧号 —— 报量级与稳定性。
 *   Q8 **初值的来源**：`getSettings()` 里 `gui/screen_font_size` / `gui/gui_font_size` /
 *      `gui/screen_button_scale` 的实际落盘值 —— 陷阱 44 同族：判据的隐含前提
 *      （"初值 = 默认 13"）可能只在**用户自己的 config** 里成立/不成立。
 *   收尾**全部还原**并核对。
 *
 * ── 探针不是判据（刻意）────────────────────────────────────────────────────
 * 只报读数、不下 PASS/FAIL（断言在 T39-C 的 HiDpiCheck）。
 *
 * ── 退出码（由 main.cpp 决定）──────────────────────────────────────────────
 *   0=DONE（正常报完读数），6=UNAVAILABLE（非合流形态/引擎未引导）。
 */
#pragma once

#include <QString>
#include <QStringList>
#include <functional>

class QCoreApplication;
class QQuickWindow;

namespace stelapp {

class AppFacade;
class FrameMailbox;

class HiDpiProbe
{
public:
    struct Result
    {
        bool ran = false;          //!< 真正跑过（前置满足）
        bool unavailable = false;  //!< 环境里跑不起来（非合流形态/引擎未引导）
        QString summary;
        QStringList details;       //!< 逐行读数（前缀已在 main.cpp 侧统一加）
    };

    //! 全部在 GUI 线程。@p delayMs 后开跑（等引擎动作注册落定）。结果经 onDone 回传。
    static void run(QCoreApplication *app,
                    AppFacade *facade,
                    QQuickWindow *window,
                    FrameMailbox *mailbox,
                    const std::function<void(const Result &)> &onDone,
                    int delayMs = 2500);
};

} // namespace stelapp
