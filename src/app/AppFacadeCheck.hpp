/*
 * AppFacadeCheck — T15 命令通路自检（STELQUICK_ACTION_CHECK=1）。
 *
 * 与 A2FrameCheck/DynFrameCheck 同风格：装配在 main.cpp 完成（引擎已 boot、
 * 帧泵已 start、AppFacade 已 attach），本类只消费就绪对象，跑完经回调回传。
 *
 * 判据（对应软件测试文档 3.2 U-ACT-01..03 + T15 任务书）：
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
