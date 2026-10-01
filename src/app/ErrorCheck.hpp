/*
 * ErrorCheck — T42-C：**状态与错误页自检**（STELQUICK_ERROR_CHECK=1）。
 *
 * ── 判据设计的第一原则：**两条腿，同一二进制**────────────────────────────
 * 本页要证明的东西分两半，而且这半与那半的前提**互斥**：
 *   · A 组（引擎可用）：资源路径、状态表、未支持项清单、接管行为、UI 控件、
 *     诊断文本/剪贴板 —— **正题**。
 *   · B 组（引擎不可用）：错误页真的会出来、错误文本非空、状态表标红 ——
 *     用 `STEL_USERDIR` 指向**不可创建**的位置造**真实失败**（T42-A Q5 的入口），
 *     不是注入桩。
 * ⇒ `run()` 收一个 `engineBooted` 开关，按前提装不同的步骤链；两条腿跑的是**同一个
 *   二进制、同一套判据代码**（与 T38/T39 的负控腿同一思路：差异只在环境）。
 *
 * ── 判据逐条（A 组：引擎可用）──────────────────────────────────────────────
 *  EC-01 资源路径表 8 行，且**无 problem 行**（引导成功后关键路径都应就位）。
 *  EC-02 用户目录以 `-quick` 结尾（T36 隔离的**运行期**读数，不是读配置文件）。
 *  EC-03 配置文件与日志文件两行 path 非空且 state=ok（"打开日志"按钮的前提）。
 *  EC-04 状态表 4 行、**无 error 行**（引擎已引导 + 后端判定已注入）。
 *  EC-05 有引擎 + 后端 ok ⇒ `hasError == false`（无错时不该喊狼来了）。
 *  EC-06 未支持项清单 **7 条**（4 老窗口 + 插件设置 + legacy 手势 + T39 两控件）。
 *  EC-07 4 个老窗口项的 `actionId` **逐个在注册表里找得到**且 `feature` 名与
 *        `getText()` 逐字相同（**防清单与真源漂移** —— 清单不许凭记忆写）。
 *  EC-08 接管表 == 10 个（T41 的 6 个 + T42 的 4 个）。
 *  EC-09 4 个未支持动作逐个 `trigger()` ⇒ **QWidget Δ=0**（不弹老窗口）
 *        ∧ 未支持对话框可见 ∧ **配置零变化**（老对话框会写 `DialogSizes/*`，
 *        不写 = 真的没弹 —— 比 QWidget 计数更精细的独立证据通道）。
 *  EC-10 **判别对照**：撤销接管后 `trigger()` ⇒ QWidget Δ>0（老窗口**真的会弹**）
 *        ⇒ EC-09 的 Δ=0 是接管的功劳，而不是"根本弹不出来"。
 *        ⚠️ 恢复只恢复到**布场态**（陷阱 90：无条件恢复会自己修好被负控打断的路径）。
 *  EC-11 错误页控件（**递归视觉树**，陷阱 45）：状态行 4 / 路径行 8 / 未支持行 7
 *        且每个 delegate 的三个字段都非空。
 *  EC-12 操作行 4 个按钮存在且**几何上落在视口内**（陷阱 47：`childAt` 会撒谎，
 *        用"落点被自身及全部祖先矩形覆盖"判 —— T41 的跳转按钮沉底就是这么抓到的）。
 *  EC-13 「复制诊断信息」写进剪贴板：文本非空、含版本行、含"未支持项"小节。
 *  EC-15 `openPath` **白名单闸**：未知 kind 拒收（返回 false）—— 真模式下也安全，
 *        拒收不弹任何东西。⚠️ "真的唤起系统程序"那一半批跑环境验不了（会弹
 *        Finder），由 `STELQUICK_ERROR_OPEN_OFF`（受理但不唤起）留给交互式人工腿：
 *        该开关置位时本条**加验**已知 kind 的受理半边（openPath("log")==true 且不弹）。
 *  EC-14 复原：配置指纹与布场**逐项相同** + 接管集合回到布场态。
 *
 * ── 负控（差异只在环境；期望值实跑出来再写死，陷阱 87）──────────────────────
 *  · 负控 A `STELQUICK_ERROR_TAKEOVER_OFF=1`：4 个 T42 接管不注册 ⇒ 老窗口照弹。
 *  · 负控 B `STELQUICK_ERROR_PATHS_OFF=1`：资源路径表留空。
 *  · 负控 C `STELQUICK_ERROR_UNSUPPORTED_OFF=1`：未支持项清单留空。
 *  （`STELQUICK_ERROR_OPEN_OFF` **不是负控** —— 没有判据会因它变红，红不了的负控
 *   不承重（陷阱 67）；它是 EC-15 受理半边的**无副作用布场开关**。）
 *
 * ── 判据逐条（B 组：引擎不可用 —— 失败腿）──────────────────────────────────
 *  EC-01 `hasError == true` 且 `errorHeadline` 非空。
 *  EC-02 `errorDetail` 非空，且与 main.cpp 注入的失败原因**一致**（不许改写/吞掉）。
 *  EC-03 错误块 UI 可见：`errorPanel.visible == true` 且 headline/ detail 文本非空。
 *  EC-04 状态表里「引擎引导」一行为 `error` 态。
 *  EC-05 资源路径面**不崩不空**：`pathRows` 非空（引导失败也要能看到路径，否则
 *        用户没有任何排查线索 —— 这条正是"错误提示"的意义）。
 *
 * ── 纪律（T40/T41 血泪）────────────────────────────────────────────────────
 *  · 台账 id 用**显式列表**（陷阱 85：`arg(i)` 生成 "SC-1" ≠ "SC-01" 会静默丢 mark）。
 *  · 收割回调必须是 `Ctx` 的成员（陷阱 84：漏赋值 ⇒ `bad_function_call`、零输出）。
 *  · 判据只许验**被声称的命题**（陷阱 75）：额外加戏 = 自造红项。
 *  · 每步自己收尾（关掉弹出的对话框、还原临时状态），防污染后续判据。
 *  · 负控的期望值**必须实跑出来再写死**（陷阱 87）。
 */
#pragma once

#include <QString>
#include <QStringList>
#include <functional>

class QCoreApplication;
class QQuickWindow;

namespace stelapp {

class ActionRouter;
class ErrorModel;

class ErrorCheck
{
public:
    struct Result
    {
        bool ran = false;
        bool unavailable = false;
        bool pass = false;
        int passed = 0;
        int total = 0;
        QString summary;
        QStringList details;
    };

    //! @p engineBooted 决定装哪条腿的步骤链（见文件头注）。
    static void run(QCoreApplication *app,
                    QQuickWindow *window,
                    ActionRouter *router,
                    ErrorModel *model,
                    bool engineBooted,
                    const std::function<void(const Result &)> &onDone,
                    int delayMs = 2500);
};

} // namespace stelapp
