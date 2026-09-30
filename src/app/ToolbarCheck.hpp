/*
 * ToolbarCheck — T34-C 真实工具栏自检（STELQUICK_TOOL_CHECK=1）。
 *
 * ── 判据清单（TB-01..TB-12，12 条；前提不齐 ⇒ 整套 UNAVAILABLE）────────────────
 *   TB-01..04 **写入生效（活引擎腿，成对）**：对 4 个代表性开关
 *         （Constellation_Lines / Ground / Equatorial_Grid / Night_Mode）
 *         `ActionRouter.trigger(id)`（注册表未命中 ⇒ **引擎透传**）⇒
 *         **模块 getter 翻转**（世界确实动了）。
 *         ⚠️ 回读走**模块 getter**、不经 StelAction/StelProperty —— 陷阱 43：
 *         action 的 checked 就是它连的那个属性，复述它=自洽假绿。
 *   TB-05 **往返复原**：再 trigger 一次 ⇒ 四个 getter 回到写前值。
 *         （工具栏是常驻 UI：判据跑完必须把状态还原，否则污染后续判据/读数。）
 *   TB-06 **信号腿**：写入腿期间 `StelActionMgr::actionToggled` 对这 4 个 id
 *         各发射 ≥1 次（AppFacade revision 订阅的信号源）。
 *   TB-07 **revision 腿**：`displayTogglesRevision` 净增量 ≥ 8（4 开关 × 2 次翻转）。
 *   TB-08 **负控（不存在 ID）**：trigger 不存在的动作 ⇒ 返回 false ∧ revision
 *         **纹丝不动** ∧ toggled 不发射（拒绝必须无副作用）。
 *   TB-09 **UI 腿（存在 ∧ 态一致）**：QML 里 12 个 `toolToggle_*` 按钮按
 *         objectName 齐全 ∧ 每个按钮的 `engineOn` == 引擎 getter（初始一致，
 *         证明 checked 绑定没有"第一次求值后不再重算"的死绑定缺陷）。
 *   TB-10 **UI 点击腿（最外层注入）**：对 `toolToggle_actionShow_Ground` 的
 *         中心投递**真实鼠标 press/release**（AC-12 先例）⇒ 引擎翻转 ∧
 *         revision +1 ⇒ 再点一次复原。
 *   TB-11 **判别负控（注册表路径 ≠ 引擎路径）**：trigger 注册表命令
 *         `app.togglePause` ⇒ executed=true ∧ revision **不变**
 *         （注册表命令不碰显示开关；没有这条，"revision 变了"可能是
 *         "任何 trigger 都会加 revision"——读不出是引擎翻转在驱动）。
 *   TB-12 **绑定重算腿**：点击后按钮 `engineOn` 跟随引擎。TB-09 只证**初始**
 *         一致；绑定若不读 revision token，引擎翻转后属性永远停在首帧而
 *         TB-09 照绿 —— 只有这条能红（QML 绑定铁律的直接判据）。
 *
 * ── 与 T33 判据的两点不同（照抄时别倒退）────────────────────────────────────
 *   · 开关状态是**同步 bool 属性**（不经过变换栈/渲染循环）⇒ trigger 返回即
 *     可读，**不需要** T33 那种有界就绪门；但仍给每步 300ms 事件循环，让
 *     actionToggled → revision 的 queued 连接跑完（跨线程信号是 queued 的）。
 *   · 负控不是概率性的：被测行为没有竞态 ⇒ "每次都必须红"这里成立。
 *
 * ── 负控开关（证明判据承重）────────────────────────────────────────────────
 *   `STELQUICK_TOOL_REV_OFF=1` ⇒ AppFacade **不订阅** actionToggled
 *   （ensureDisplayForwarding 直接返回）⇒ TB-07 必红、TB-09/10 的按钮态将
 *   **停在首帧**（这正是它要抓的死绑定缺陷），其余判据照绿。
 *   实证方式同 T33：注掉机制 → 必须看到对应判据 FAIL，否则判据是摆设。
 *
 * ── 退出码 ─────────────────────────────────────────────────────────────────
 *   0=PASS，10=FAIL，6=UNAVAILABLE（引擎未引导/动作未注册齐）。
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

class ToolbarCheck
{
public:
    struct Result
    {
        bool ran = false;
        bool pass = false;
        bool unavailable = false;
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
                    const std::function<void(const Result &)> &onDone,
                    int delayMs = 2500);
};

} // namespace stelapp
