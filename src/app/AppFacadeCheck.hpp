/*
 * AppFacadeCheck — T15/T16 命令通路与仿真时钟自检（STELQUICK_ACTION_CHECK=1）。
 *
 * 与 A2FrameCheck/DynFrameCheck 同风格：装配在 main.cpp 完成（引擎已 boot、
 * 帧泵已 start、AppFacade 已 attach），本类只消费就绪对象，跑完经回调回传。
 *
 * 判据（对应软件测试文档 3.2 U-ACT-01..03 + T15/T16 任务书）：
 *   AC-1 拆除判定：合流形态 widget QAction 分发已关闭（双轨的前提不存在）。
 *   AC-2 zoom 单次：每次 zoomIn/zoomOut 恰好改变一步（aimFov 精确断言），
 *        连续调用不叠加、不丢失。
 *   AC-3 暂停语义：暂停期间 JD 冻结（≤1e-12 漂移）；恢复后继续推进；
 *        重复 setPaused 同值幂等（信号恰一次）。
 *   AC-4 注册表禁用门（U-ACT-02）：enabled=false 的命令 trigger 返回 false，
 *        不执行、计数不动。
 *   AC-5 引擎透传单次（U-ACT-01）：routeKey/trigger 到引擎 StelAction
 *        恰好触发一次（toggled 计数断言；用 actionShow_Cardinal_Points，
 *        离屏无副作用）。
 *   AC-6 焦点守卫基线（U-ACT-03）：无输入焦点时 canDispatchToSky()=true。
 *
 * ── T16 新增（单一仿真时钟）───────────────────────────────────────────────
 *   AC-7 驱动源单点：simClock 处于 HostDriven，且旧宿主帧节拍器（fpsTimer）
 *        未激活——"同一时刻只有一个 update 驱动源"的可观测判据。
 *   AC-8 墙钟路径未参与推进：暂停态下把墙钟锚点人为拉远 1 小时，JD 仍严格不变
 *        （EngineWallClock 路径若还在跑，1 小时 × 速率会是巨大的正跳变）。
 *        ——这是"updateTime 不读墙钟"的决定性断言。
 *   AC-9 外部跳转重锚（插件零改造的关键）：暂停态下经 StelCore::setJD 跳转
 *        1000 天，静置后 JD 仍**严格等于**跳转值；恢复后从新值继续推进。
 *        T15 的宿主闭式公式会在下一帧把跳转值覆盖掉，此处必须不成立。
 *   AC-10 速率线性贯通：速率 R 与 2R 两窗口的推进量之比 ∈ [1.6, 2.4]
 *        （比值口径对定时抖动不敏感），证明 setTimeRate 真的作用到推进上。
 *   AC-11 速率 0 真冻结：**直接调 StelCore::setTimeRate(0)**（不经 AppFacade，
 *        模拟插件行为）后 JD 严格不变——证明"暂停"不再依赖宿主侧字段，
 *        插件无需改造即合规。
 *
 * 另在 t0 跑一遍 StelClockController::selfTest（纯逻辑，12 项），
 * 使单次运行同时给出"逻辑语义"与"引擎集成"两层证据。
 *
 * 退出码：0=PASS，8=FAIL，6=UNAVAILABLE（前置不满足）。
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

class AppFacadeCheck
{
public:
    struct Result
    {
        bool ran = false;       //!< 前置满足、真正跑过
        bool pass = false;
        QString summary;
        QStringList details;
    };

    //! 全部在 GUI 线程（内部用 QTimer::singleShot 链）。结果经 onDone 异步回传。
    static void runStartupSequence(QCoreApplication *app,
                                   QQuickWindow *window,
                                   AppFacade *facade,
                                   ActionRouter *router,
                                   const std::function<void(const Result &)> &onDone);
};

} // namespace stelapp
