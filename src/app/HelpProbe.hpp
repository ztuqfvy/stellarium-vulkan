/*
 * HelpProbe — T41-A **帮助 / 版本 / 许可证数据面探针**（STELQUICK_HELP_PROBE=1）。
 *
 * ── 为什么第一相位是探针（T24/T33/T34/T35/T37/T38/T39/T40 同款纪律）──────────
 * A-1.0 范围表倒数第二格（`2026-09-17-01-qml-ui-vulkan-development.zh_CN.md:76`）：
 *   `|快捷键编辑、**帮助、版本与许可证页面** | 可延后 | 必须 | 中文输入焦点不能触发天空快捷键|`
 * T40 已交付**快捷键编辑**；本轮交付**同格的余下三项**（帮助 / 版本 / 许可证）。
 *
 * ── 老宿主那一份长什么样（`src/gui/HelpDialog.{hpp,cpp}`，只读参考）────────────
 * `HelpDialog : StelDialog` = **QWidget 多标签对话框**，四个 tab：
 *   ① **Help**（`updateHelpText()`，`:278-501`）—— 一大段**硬编码 HTML 表格**：
 *      · 「Keys」表：约 60 行 `<tr><td>中文说明</td><td><b>键位</b></td></tr>`，
 *        其中**一半的键位来自引擎注册表**（`hotkeyTextWrapper(k)` 就是把字符串丢给
 *        `QKeySequence(k).toString(NativeText)`），**另一半是"没有 action 的手势"**
 *        （鼠标拖拽 / 滚轮 + Ctrl / Shift / Alt 组合 / 双击 …… 直接硬编码在 htmlText 里）。
 *      · 「Further Reading」表：若干外链（wiki / 官网 / 论坛）。
 *      ⚠️ **没有外部 help 文件** —— `StelFileMgr::findFile()` 在这次探针里要顺带证伪。
 *   ② **About**（`updateAboutText()`，`:507-616`）—— 版本四件套 + Qt 版本 + OS +
 *      架构 + `STELLARIUM_COPYRIGHT` + GPL 声明段 + **开发者/前开发者/贡献者/资助者**名单。
 *   ③ Log（读 `StelLogger::getLogFileName()`）④ Config（读 `QSettings::fileName()`）。
 *   还带一个**联网检查更新**（`checkUpdates()` → `QNetworkAccessManager`）。
 *   ⚠️ ①②③④ 全部是 **QWidget + QTextBrowser**，QML 形态**一个都用不了**。
 *
 * ── 🔴 探针实测裁决（定稿轮；**首轮推断被推翻三处**，见下）──────────────────
 * 开工前的推断是「`src/ui/CMakeLists.txt` 源表里没有 `gui/` ⇒ 老 `HelpDialog` 不存在
 * ⇒ F1 是没主的键 ⇒ 帮助入口只能自建」。**实测三项全错**：
 *
 *   ① **`src/gui/` 是被编译的** —— 只是不在 `src/ui/CMakeLists.txt` 的源表里，
 *      而在 `src/CMakeLists.txt:330-348`（`stelMain` 静态库）里；`stelQuickUI` 链接
 *      `stelMain` ⇒ **整个 QWidget GUI 子系统随静态库进来**。
 *      ⇒ 教训：判"某子系统在不在运行时"，**要查最终链接目标的源表**，
 *        不能只看当前目录的 CMakeLists（详见 `TRAPS.md` 88）。
 *   ② **8 个窗口动作全部在册**，`getText()` 还是**中文**（T24 翻译链路的红利）：
 *      `actionShow_Help_Window_Global`（组=**Windows**｜键=**F1**｜文本=「说明」）等。
 *      ⇒ T40 探针看到 "Windows" 组是**真读数**，不是误抄。
 *   ③ **F1 触发会真的弹出老 QWidget 对话框**（Q14 实测）：
 *      `trigger()` 前 QWidget 总数 **12** → 后 **58**（**Δ46**）且出现可见 `QDialog`；
 *      `StelDialog::setVisible(true)` 是**惰性建 UI**（`StelDialog.cpp:112` new QDialog +
 *      `:116` createDialogContent）并挂到 `StelMainView` 的 QGraphicsScene 上。
 *      ⚠️ **还原后 Δ46 不归零** —— `setVisible(false)` 只隐藏不销毁 ⇒ 46 个 widget 常驻。
 *      ⇒ **T41 必须「接管」F1**，不能「并存」（否则 QML 界面下按 F1 蹦老式对话框，
 *        直接违背"QML 重写"的目标）。
 *
 *   其余实测要点：
 *   · 版本面全现成：`Stellarium 26.1+` / `26.1.273-4132aeb [main]` / `26.1` / `26.0`；
 *     编译期宏 `PACKAGE_VERSION`/`STELLARIUM_*` 都是 `ADD_DEFINITIONS` 全局可用；
 *     `STELLARIUM_COPYRIGHT` = `Copyright (C) 2000-2026 Stellarium Developers`。
 *   · 🔴 `QCoreApplication::applicationVersion()` 是 **`1.0.0`**（未设成产品版本）
 *     ⇒ 版本页**必须**走 `StelUtils::getApplicationVersion()`，不能读它。
 *   · 🔴 **`COPYING` 在 bundle 里不存在**（`Contents/` 只有 Info.plist/MacOS/translations）；
 *     `StelFileMgr::findFile("COPYING")` 命中「./COPYING」**只是因为跑的 cwd 恰好是源码树根**
 *     （`getInstallationDir()` = `"."`）⇒ **分发时必然失效** ⇒ 必须编进 qrc。
 *   · 贡献者名单 **246 条、去重后 245**（原表自带 1 条重复；老 GUI 在显示前
 *     `removeDuplicates()`）⇒ 产品侧必须去重。
 *   · 帮助页规模：老 `HelpDialog.cpp` 里 `<tr>` **40** 处、`hotkeyTextWrapper(` **14** 处
 *     ⇒ 约 **26 条**是"没有 action 的手势"（鼠标/滚轮组合），需产品侧硬编码。
 *   · 翻译链路健康：`Stellarium Help`→「Stellarium 帮助」、`Keys`→「快捷键」、
 *     `Windows` 组动作名全中文；`Copyright` **保持英文**（老代码注释明说
 *     "this legal notice is not suitable for translation" ⇒ 这是对的，别去翻）。
 *
 * ── 探针要回答的问题 ─────────────────────────────────────────────────────
 *   版本面（全部现成，低风险）
 *   Q1  版本四件套的**实际字符串**：`getApplicationName()` / `getApplicationVersion()` /
 *       `getApplicationPublicVersion()` / `getApplicationSeries()`；
 *       `GIT_REVISION` 在不在（决定版本串是 `25.2-abc1234 [main]` 还是纯 `25.2.0`）。
 *   Q2  编译期宏直接可用性：`PACKAGE_VERSION` / `STELLARIUM_BUIDING_VERSION` /
 *       `STELLARIUM_PUBLIC_VERSION` / `STELLARIUM_SERIES` / `STELLARIUM_COPYRIGHT`
 *       （`CMakeLists.txt:101-112` 是 `ADD_DEFINITIONS` 全局宏 ⇒ 产品侧直接能用，
 *       不必绕 `StelUtils`）。
 *   Q3  运行环境串：`getOperatingSystemInfo()` / `getAddressingMode()` /
 *       `QSysInfo::prettyProductName()` / `QSysInfo::currentCpuArchitecture()` /
 *       `QSysInfo::kernelVersion()` —— "版本"页要显示且要**中文化**的那几条。
 *   Q4  **编译期 vs 运行期 Qt 版本**是否一致（`QT_VERSION_STR` vs `qVersion()`）——
 *       不一致说明运行时装了别的 Qt，这是"版本"页必须显示的事实。
 *
 *   许可证面（🔴 本轮最大风险点）
 *   Q5  🔴 **`COPYING` 在运行时可达吗**：源码树里它在 `./COPYING`（GPL-2.0 全文），
 *       `CMakeLists.txt:1041` 只把它给 `CPACK_RESOURCE_FILE_LICENSE`（打包器用），
 *       **没有任何 install / qrc 规则** ⇒ 立刻要证的假设是：**bundle 里没有它**。
 *       探针要枚举：源码树路径 / md5 / 字节数 / 行数 · `StelFileMgr::findFile("COPYING")`
 *       是否命中 · `findFileInAllPaths` 的全部候选 · `getUserDir()` / `getInstallationDir()`
 *       下有没有 · `QFile(":/COPYING")` 是否命中（qrc 现状）。
 *   Q6  ⇒ 若不可达，**分发合规**（GPL 要求随二进制给许可证副本）要求产品侧自己带一份。
 *       探针顺带测：把文本塞进 **qrc** 后 `QFile(":/...")` 读得到吗（`qrc:/` 与 `:/` 两种前缀）。
 *   Q7  GPL 声明的**权威文案**：`updateAboutText()` 里那段"本程序是自由软件…"是
 *       **手写字面量**（`:558-568`，`STELLARIUM_COPYRIGHT` 宏 + 5 段固定英文）
 *       ⇒ 产品侧照抄即可，但要确认 `STELLARIUM_COPYRIGHT` 展开成什么。
 *
 *   帮助面
 *   Q8  🔴 **帮助动作悬空裁决**：`findAction("actionShow_Help_Window_Global")` 是否 null；
 *       `findActionFromShortcut("F1")` 命中谁（`F1` 有没有主）；
 *       注册表里 text 含 `Help`/`帮助` 的动作有几个。
 *   Q9  🔴 **"Windows" 组之谜**：`getGroupList()` 里有没有 `Windows`；
 *       若有，`getActionList("Windows")` 的成员是**谁注册的**（老 `StelGui` 没编进来
 *       ⇒ 只能是插件/别的模块）⇒ 决定 T41 的帮助入口要不要与之共存。
 *   Q10 帮助页的**两半数据源**边界：
 *       (a) 引擎注册表那一半（505 动作 / 15 组）—— T40 的 `ShortcutModel` 已能用；
 *       (b) "没有 action 的手势"那一半（老 HTML 里硬编码的鼠标/滚轮组合）——
 *           探针数一数老 HTML 里到底有几条、分别是什么（决定 T41 是照抄常量表
 *           还是干脆砍掉不做）。
 *   Q11 翻译串可达性：`q_("Stellarium Help")` / `q_("Help window")` / `q_("Keys")` /
 *       `q_("Further Reading")` 在 QML 形态下**译成中文了吗**（T24 翻译链路的红利）。
 *       拿不到中文 ⇒ 帮助页文案要么走产品侧字面量、要么补翻译条目。
 *
 *   收尾
 *   Q12 探针**零写入**（只读）；收尾核对整本配置指纹与基线**逐项相同**
 *       （本轮不碰 `QSettings`，这条是回归护栏）。
 *
 * ── 探针不是判据（刻意）────────────────────────────────────────────────────
 * 只报读数、不下 PASS/FAIL（断言在 T41-C 的 HelpCheck）。
 *
 * ── 退出码（由 main.cpp 决定）──────────────────────────────────────────────
 *   0=DONE（正常报完读数），6=UNAVAILABLE（非合流形态/引擎未引导）。
 */
#pragma once

#include <QString>
#include <QStringList>
#include <functional>

class QCoreApplication;

namespace stelapp {

class HelpProbe
{
public:
    struct Result
    {
        bool ran = false;          //!< 真正跑过（前置满足）
        bool unavailable = false;  //!< 环境里跑不起来（非合流形态/引擎未引导）
        QString summary;
        QStringList details;       //!< 逐行读数（前缀已在 main.cpp 侧统一加）
    };

    //! 全部在 GUI 线程。@p delayMs 后开跑（等引擎引导落定）。结果经 onDone 回传。
    //! ⚠️ 刻意**不启帧泵、不抓帧**：版本/许可证/帮助面是**纯数据面**（编译期宏 +
    //! QFile + QObject 注册表），与 GPU 无关 —— 少一个 Metal 环节少一份掉设备风险。
    //! ⚠️ 刻意**只读不写**：不碰 `QSettings`，不注册动作，不改任何引擎状态。
    static void run(QCoreApplication *app,
                    const std::function<void(const Result &)> &onDone,
                    int delayMs = 1200);
};

} // namespace stelapp
