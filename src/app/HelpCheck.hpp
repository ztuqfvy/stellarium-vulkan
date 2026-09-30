/*
 * HelpCheck — T41-C **帮助 / 版本 / 许可证自检**（STELQUICK_HELP_CHECK=1）。
 *
 * ── 设计依据（来自 T41-A 探针读数，见 app/HelpProbe.hpp 与
 *    docs/evidence/2026-09-30-t41-help/mac/probe-help-mac.txt）──────────────
 * 判据共 **17 条（HC-01..HC-17）**，三条纪律与 T37..T40 一致：
 *   · **独立回读**（陷阱 43）：版本行与 `StelUtils` 逐项比、GPL 与**源码树磁盘
 *     原文**比 md5（用 `__FILE__` 推根，HelpProbe Q5 同款）、UI 文本与数据面比。
 *   · **判别对照必须有**：HC-01（applicationVersion="1.0.0" 的反例）、HC-07
 *     （白名单外 URL 必拒）、**HC-15（撤销接管 ⇒ 老 QWidget 对话框真的弹出来**
 *     —— 证明"接管在起作用"，绿不是白捡的）。
 *   · **等待挂写入步**：切页/点按钮后**单独一步 + 400ms**（陷阱 45/84：StackLayout
 *     隐藏页 delegate 不创建、要等一帧）。
 *
 * ── 判据清单 ───────────────────────────────────────────────────────────────
 *   HC-01 版本行       versionRows 非空；「完整版本」== `StelUtils::getApplicationVersion()`；
 *                      Git 修订 == GIT_REVISION（宏可用时）；**判别对照**：
 *                      `QCoreApplication::applicationVersion()` 是「1.0.0」且 ≠ 完整版本
 *                      ⇒ 证明没走错源（探针 Q4 警告的产品级复验）
 *   HC-02 系统行       操作系统 == `getOperatingSystemInfo()`；Qt 编译期 == 运行期
 *   HC-03 贡献者       245 条（246 去重后）；无重复；**大小写不敏感**排序（首项 adalava）；
 *                      与引擎静态表去重集合相同；非 ASCII 名 UTF-8 安全
 *   HC-04 许可证内容   loaded + 无 error + 行数>300 + 内含 "GNU GENERAL PUBLIC LICENSE"
 *                      与 "Version 2"
 *   HC-05 许可证资源   `licenseResource()` == ":/StelQuickUI/COPYING" 且该资源存在，
 *                      且**逐字节 == 源码树磁盘 COPYING**（探针 Q5：bundle 里没有 ⇒
 *                      必须编进 qrc，这里证"编进去了且没编错文件"）
 *   HC-06 手势表       每条 scope ∈ {sky,legacy}；sky+legacy==总数；legacy==10
 *                      （脚本 3 + 天文计算 7 —— 常量表变了就该醒）
 *   HC-07 外链+白名单  7 条、全 https；表内 URL 放行、**表外必拒**（判别对照）
 *   HC-08 键名平台化   至少一条 keys 以 `QKeySequence(Qt::CTRL).toString(NativeText)`
 *                      开头（macOS=⌘）—— 键名由 C++ 生成的物证（T40 铁律同源）
 *   HC-09 导航按钮     navHelpButton/navAboutButton 在视觉树；**原有 8 个导航按钮**
 *                      一个不少（T34/T38/T40 的"只加不改"回归）
 *   HC-10 帮助页控件   真实点击切页后：手势行数 == 数据面行数、外链行数 == 7、
 *                      **可见** legacy 标记数 == legacyGestureCount（陷阱 45：视觉树递归）
 *   HC-11 跳转快捷键页 真实点击 helpGotoShortcutsButton ⇒ pageStack.currentIndex == 6
 *   HC-12 关于页控件   版本行/系统行 Repeater delegate 数 == 数据面行数；贡献者
 *                      ListView delegate 在场且首条含 "adalava"
 *   HC-13 许可证端到端 真实点击按钮 ⇒ Dialog visible；**UI 文本 == HelpModel.licenseText**
 *                      （判据读 UI 自己的 TextArea，不拿孤立副本当参照 —— T40 SC-13 教训）
 *   HC-14 接管表       `hostTakeoverIds()` == 预期 6 个（排序逐字比）；**F2 设置不在表里**
 *                      （"没对应页就刻意不接管"这条设计决定被执行的物证）
 *   HC-15 接管生效     trigger 路径：F1 ⇒ hostActionRequested +1、QWidget 总数**不变**、
 *                      pageStack 切到 help；**判别对照**：`setHostTakeover(id,false)` 再
 *                      trigger ⇒ QWidget 总数**上涨**（老对话框真的弹出来 —— 探针 Q14 的
 *                      产品级复验；不接管就弹 = 接管在起作用）
 *   HC-16 键盘路径     `routeKey(F1,0)` ⇒ 同样被接管（探针 Q8：F1 经
 *                      findActionFromShortcut 能命中 ⇒ 只接管 trigger 是假修复）
 *   HC-17 复原         整本 config.ini 指纹 == S1 基线（本自检**全程只读**）；
 *                      可见顶层窗口快照 == 基线（判别对照弹出的 QDialog 已关）
 *
 * ── 负控（红项集合**按实跑读数写死** —— T40 教训：期望值是实验读数不是推论）────
 *   A `STELQUICK_HELP_TAKEOVER_OFF=1` ⇒ main.cpp 不注册任何接管
 *      ⇒ 实测红 **[HC-14, HC-15, HC-16]**（14/17，rc=10）
 *      · HC-14 接管表空；· HC-15 F1 透传引擎 ⇒ 老对话框弹出（QWidget Δ46）；
 *      · HC-16 键盘路径同理。**HC-17 曾连带红**（HC-16 透传留下的可见 QDialog
 *        污染了"可见顶层回基线"）⇒ 已在 HC-16 末尾补"每步自己收尾"的清理。
 *   B `STELQUICK_HELP_LICENSE_OFF=1` ⇒ HelpModel 不读 GPL 全文
 *      ⇒ 实测红 **[HC-04, HC-13]**（15/17，rc=10）
 *      · ⚠️ **HC-05 不红是对的**：它验的是"GPL **编进 qrc** 了没有"（资源存在性 +
 *        与磁盘逐字节一致），负控 B 打的是"**加载**这条腿" —— 两件事。首版预期
 *        （推理填的）写了 [HC-04, HC-05, HC-13]，实测只有两条，按实跑改写。
 *   **两腿正交**（红项集合两两不同，且语义可分）：
 *      A\B = {HC-14,15,16}（**接管域**）、B\A = {HC-04,13}（**许可证域**）、无交集。
 *      ⇒ 看红项落在哪半边就能定位坏在哪个环节。
 *
 * ── 环境前提 ───────────────────────────────────────────────────────────────
 *   · 合流形态构建 + 引擎引导（非合流 ⇒ UNAVAILABLE rc=6）。
 *   · **全程只读**用户配置（S1 记指纹，HC-17 核对）。
 *   · HC-15 判别对照会真的弹一次老 HelpDialog（46 个 widget 常驻进程）⇒ 判完立即
 *     close + 恢复接管；HC-17 以**可见顶层**快照核对（隐藏 widget 不影响用户）。
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

class ActionRouter;
class HelpModel;

class HelpCheck
{
public:
    struct Result
    {
        bool ran = false;
        bool unavailable = false;
        bool pass = false;
        bool inconclusive = false;
        int passed = 0;
        int total = 0;
        QString summary;
        QStringList details;
    };

    static void run(QCoreApplication *app,
                    QQuickWindow *window,
                    ActionRouter *router,
                    HelpModel *model,
                    const std::function<void(const Result &)> &onDone,
                    int delayMs = 2500);
};

} // namespace stelapp
