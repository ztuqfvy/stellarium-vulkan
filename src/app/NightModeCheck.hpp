/*
 * NightModeCheck — T37-C 夜视自检（STELQUICK_NIGHT_CHECK=1）。
 *
 * ── 定性结论（T37-A 探针 + T37-X2 三轮反转后的最终口径）──────────────────────
 *   · 引擎旧夜视后处理（NightModeGraphicsEffect，挂 QGraphicsItem 的纯 GL effect）
 *     在合流形态**物理不可达**：帧由 LegacySkyHost 自建 FBO 产出、不经 QGraphicsView，
 *     对读回帧零影响（实验：禁用该 effect 行为不变）。⇒ 夜视由 Qt Quick 侧单一实现
 *     （MainWindow.qml keySink.layer.effect，qsb 预编译，公式逐字复刻引擎
 *     lum=max(r,g,b)→(lum,lum*0.3,0)）—— 这正是 A-1.0"夜视效果只做一次、
 *     避免叠加两次"的正确落地。
 *   · **引擎自己对夜视也有既有反应**（原版语义，不是缺陷）：三处大气类
 *     `if (getVisionModeNight()) return;` ⇒ 夜视开启时大气整层退场；旧宿主靠
 *     GL effect 把整个场景滤红，合流形态改由 Qt Quick 滤镜承担红移。
 *     ⚠️ T37-A 探针头条"上游 OFF↔ON 逐位相同"是假绿（等待挂读步，读到旧帧，
 *     TRAPS 69）——真实情况是上游随大气退场而变化。
 *
 * ── 环境前提（S1 布场，收尾全部还原）────────────────────────────────────────
 *   冻结仿真（setSimScale(0)）+ 对照量置关 + **大气置关**。
 *   大气关掉的原因：引擎夜视反应里唯一动帧内容的就是大气退场——关掉它，
 *   NC-03①（上游不变）的前提才成立；同时星点可见性不再依赖白昼亮度模型。
 *
 * ── 判据清单（NC-01..NC-05；前提不齐 ⇒ 整套 UNAVAILABLE，不记 FAIL）──────────
 *   NC-01 **状态面（活引擎腿）**：trigger(actionShow_Night_Mode) ⇒
 *         **引擎 getter** `getVisionModeNight()` 翻转（⚠️ 回读走引擎 getter、
 *         不经 StelAction/actionChecked —— 陷阱 43：复述它 = 自洽假绿）。
 *   NC-02 **噪声底门（有效性门）**：冻结后同状态连取两帧，上游/下游都须
 *         **低于噪声容差**（占比<0.1% ∧ 通道差≤2）。⚠️ 不用"逐位相同"——冻结下
 *         引擎渲染仍有亚 LSB 抖动（实测上游 0.029%/Δ1、下游 0.019%/Δ1）。
 *         超限 ⇒ 判据口径不可信 ⇒ 整套 UNAVAILABLE（环境门第三态，不记 FAIL）。
 *   NC-03 **效果面（核心，成对）**：夜视 OFF↔ON：
 *         ① **上游低于噪声容差**（大气已关 ⇒ 引擎夜视反应成 no-op ⇒ 上游不变；
 *            若将来有人把滤镜挪进引擎或引擎夜视反应扩大，这里立刻红）；
 *         ② **下游视口内差异像素 ≥ 90% ∧ 平均通道差 > 1.0**（滤镜真的生效）。
 *            ⚠️ 平均通道差不能用 50：暗星空背景下滤镜只显著改写星点/线条，
 *            全帧均值掉到个位数（实测 ~14.5）。
 *            伪证守门 = ①：上游不变时，下游任何变化只能来自滤镜——
 *            "黑帧伪装成效果"的通道被堵死（TRAPS 69：黑帧差异量级 ~157 ≈ 滤镜）。
 *         ② 看的是**视口内**不是全图（全图差异会被工具栏稀释/混淆）。
 *   NC-04 **判别对照（承重）**：同一套取帧+比较方法，对判别对照动作
 *         （默认 actionShow_Constellation_Lines，可 STELQUICK_NIGHT_CONTROL_ACTION
 *         覆盖）OFF↔ON ⇒ **上游差异超出噪声容差**。没有这条，NC-03① 的
 *         "上游=0"可能只是"比较方法测不出东西"（假绿）。
 *         ⚠️ 等待挂**写入步**（S6 delayAfter=900）——挂读步 = 读到旧帧假绿。
 *   NC-05 **往返复原**：还原对照量+关夜视 ⇒ 引擎 getter 回初值 ∧ 复原帧与参考帧B
 *         的差异**不含全帧级红移**（占比<0.5 ∧ 均值<20 —— 滤镜没关的签名是
 *         ~100%/~150）。已知有界容忍：星线 fader 在仿真冻结期间停在中间态
 *         （~1.5%/Δ≤23 余辉，T37-X4 另案定性）。
 *         ⚠️ 抓到异常帧（非黑 < 参考 90%）⇒ INCONCLUSIVE，不记 FAIL。
 *
 * ── 负控开关（证明判据承重）────────────────────────────────────────────────
 *   `STELQUICK_NIGHT_EFFECT_OFF=1` ⇒ BackendInfo.nightEffectOff ⇒ QML
 *   layer.enabled 强制 false ⇒ 效果物理不生效 ⇒ NC-03② 必红（且只它红）。
 *   实证方式同 T34/T33：注掉机制 → 必须看到对应判据 FAIL，否则判据是摆设。
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

class NightModeCheck
{
public:
    struct Result
    {
        bool ran = false;
        bool pass = false;
        bool unavailable = false;
        bool inconclusive = false;  //!< NC-05 遇到异常帧等竞态读数（不记 FAIL）
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
