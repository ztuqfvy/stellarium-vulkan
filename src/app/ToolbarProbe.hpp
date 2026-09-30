/*
 * ToolbarProbe — T34-A **工具栏数据面探针**（STELQUICK_TOOL_PROBE=1）。
 *
 * ── 为什么第一相位是探针（与 T33 的 LOCPROBE 同款纪律）─────────────────────────
 * T24/T33 的血泪：依赖面（翻译、地点库）都曾"看起来该在、实际没加载"，潜伏多任务
 * 才被逼出来。工具栏的依赖面是**引擎 StelAction 注册表**：
 *   · 12 个候选开关（照桌面底栏选）必须真的被注册、且是 **checkable**（连着
 *     bool Q_PROPERTY）——桌面 GUI（StelGui）里它们挂在底栏，但**合流形态根本
 *     不创建 StelGui**，动作注册与否只取决于各 StelModule::init 是否跑过；
 *   · `ActionRouter::trigger` 的**引擎透传路径**（注册表未命中 → findAction →
 *     trigger()）是 T15 写的，T34 之前**没有任何判据走过这条路**；
 *   · `StelActionMgr::actionToggled(id, value)` 是否发射，决定 AppFacade 能不能
 *     用一个 revision 通知 QML 刷新按钮态（T34-B 的前提）。
 * ⇒ 先证"命令面可用"，再写工具栏。探针不合格就不写 UI。
 *
 * ── 探针不是判据（刻意）────────────────────────────────────────────────────
 * 只报读数、不下 PASS/FAIL（断言在 T34-C 的 ToolbarCheck）。
 *
 * ── 探针要回答的四个问题 ─────────────────────────────────────────────────
 *   Q1 注册表：动作总数 / 分组数（12 个候选必须落在其中）
 *   Q2 候选清单：findAction 非空？isCheckable？isChecked 当前值？text？快捷键？
 *   Q3 透传：ActionRouter.trigger(id)（注册表未命中 → 引擎）后模块 getter
 *      **真的翻转**吗？（回读走**模块 getter**，不经 StelAction —— 复刻被测
 *      逻辑的对照是自洽假绿，T33 陷阱 43。）
 *   Q4 信号：这期间 actionToggled 对哪些 id 发射了？
 */
#pragma once

#include <QString>
#include <QStringList>
#include <functional>

class QCoreApplication;

namespace stelapp {

class AppFacade;
class ActionRouter;

class ToolbarProbe
{
public:
    struct Result
    {
        bool ran = false;          //!< 真正跑过（前置满足）
        bool unavailable = false;  //!< 环境里跑不起来（合流形态缺失/引擎引导失败）
        QString summary;
        QStringList details;       //!< 逐行读数（前缀已在 main.cpp 侧统一加）
    };

    //! 全部在 GUI 线程。@p delayMs 后开跑（等引擎动作注册落定）。结果经 onDone 回传。
    static void run(QCoreApplication *app,
                    AppFacade *facade,
                    ActionRouter *router,
                    const std::function<void(const Result &)> &onDone,
                    int delayMs = 2500);
};

} // namespace stelapp
