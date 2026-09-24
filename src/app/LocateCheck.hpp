/*
 * LocateCheck — T18 定位/跟踪自检（STELQUICK_LOCATE_CHECK=1）。
 *
 * 与 A2FrameCheck / DynFrameCheck / AppFacadeCheck / SearchModelCheck 同风格：
 * 装配在 main.cpp 完成（引擎已 boot、帧泵已 start、AppFacade 已建），
 * 本类只消费就绪对象，跑完经回调回传。
 *
 * ── 判据设计：为什么必须成对出现 ──────────────────────────────────────────────
 * 本自检要回答的不是"有没有调过 setFlagTracking"，而是"**视向真的跟着目标走吗**"。
 * 孤立地断言"目标到视向的夹角 ≈ 0"是**可以被假绿**的：
 *   · 如果推进的时间不够、目标在 AltAz 里几乎没动（或者根本没推进），
 *     那夹角当然接近 0 —— 但这什么都没证明；
 *   · 如果视向被锁在**赤道坐标**里不动，对一颗恒星来说夹角也接近 0 ——
 *     同样什么都没证明。
 * 所以每一条"跟上了"的断言都配一条"**世界确实动了**"的观测：
 *
 *   LOC-04（跟踪开）→ 同时断言
 *     (a) 目标 AltAz 方向在 Δt 内的变化量 > 5°（信号存在：地球自转把它带走了）
 *     (b) 目标到视向的夹角仍 < 0.5°（视向跟着走）
 *   LOC-05（跟踪关，同样的 Δt、同样的目标）→ 断言
 *     (c) 夹角 > 5°（放开后视向不再跟随）
 *   (b) ∧ (a) 排除了"世界没动"；(b) ∧ (c) 排除了"跟踪本来就会这样"。
 *   三者合起来才是判别性的。**只跑 LOC-04 不断言 (a) 是本自检最危险的写法。**
 *
 * ── 夹角口径 ─────────────────────────────────────────────────────────────────
 * 一律在 **J2000 赤道系**里算：`obj->getJ2000EquatorialPos(core)` 与
 * `mv->getViewDirectionJ2000()` 同系直接求夹角（用 Vec3d::angle，弧度→度）。
 * 刻意不复刻引擎 mountMode 那一串换算 —— 那是被**测**的对象，不是判据的输入。
 * 唯一的已知偏移是引擎的"视口中心偏移"（默认 0）：
 *   updateVisionVector 的跟踪分支会给目标纬度加
 *   `viewportCenterOffset[1] * currentFov`，故期望夹角 = |该偏移|。
 *   判据用 `|夹角 - |offset|| < 0.5°`，并把 offset 打到日志里
 *   （若哪天有人改了配置导致它非 0，这条日志能立刻说清原因）。
 *
 * ── 判据清单 ─────────────────────────────────────────────────────────────────
 *   LOC-01 家园行星守卫（**纯谓词**）：isHomePlanet 的 4 组边界输入。
 *          抽成纯谓词的理由：真去检索"当前观察地点的行星"不一定拿得到对象
 *          （首页地点是 Earth，而 Earth 是否在 SolarSystem 里可检索取决于配置），
 *          但守卫逻辑本身必须**恒可验**，不能因环境而漏测。
 *   LOC-02 无选中时不谎报：clearSelection 后 setTracking(true) 必须返回 false、
 *          理由 "no-selection"、且 isTracking() 为 false。
 *          （引擎侧 setFlagTracking(true) 在无选中时会 emit flagTrackingChanged(true)
 *           而内部仍是 false —— 任何缓存标志的 UI 都会在这里谎报，本层读合取真值免疫。）
 *   LOC-03 定位落地：选中 fixture 后 locateSelected(true) 返回 true；
 *          等自动移动（默认 1.5s）结束后夹角达标（不达标自动重采样一次，
 *          两次都不过才判 FAIL —— 防"帧泵刚好卡了一下"的假红）。
 *   LOC-04 跟踪真的在跟：见上，(a) ∧ (b)。
 *   LOC-05 判别性对照：见上，(c)。
 *   LOC-06 关跟踪：setTracking(false) → isTracking() 为 false 且 trackedName() 为空。
 *   LOC-07 幂等重选不打断跟踪：跟踪中再 selectByStableId(同一 id) 后
 *          isTracking() 必须仍为 true。
 *          （引擎的 selectedObjectChange 槽会**无条件**关跟踪；T18 在
 *           ObjectInfoModel::selectByStableId 加了幂等闸，这条是它的回归判据。）
 *   LOC-08 家园行星端到端：能检索到"当前观察地点行星"时选它并定位 →
 *          必须被拒（理由 home-planet）且未进入跟踪；检索不到则 **SKIP**（照实写明，
 *          因为 LOC-01 已经证明守卫谓词本身正确，此处只是补一条端到端）。
 *
 * ── 退出码 ───────────────────────────────────────────────────────────────────
 *   0=PASS，10=FAIL，6=UNAVAILABLE（环境里找不到任何可用 fixture；
 *     此时阶段 LOC-01/02 仍需全绿，且结果**照实打印**，不伪装成全绿）。
 *   注：不占 9 —— 9 已被长跑的环境前置守卫占用（"拒绝测量"），语义不同不可复用。
 */
#pragma once

#include <QString>
#include <QStringList>
#include <functional>

class QCoreApplication;

namespace stelapp {

class AppFacade;

class LocateCheck
{
public:
    struct Result
    {
        bool ran = false;          //!< 真正跑过（前置满足）
        bool pass = false;
        bool unavailable = false;  //!< 环境无 fixture（环境条件，非逻辑缺陷）
        int passed = 0;
        int total = 0;
        QString summary;
        QStringList details;
    };

    //! 全部在 GUI 线程。@p delayMs 后开跑（等引擎数据落定）。结果经 onDone 回传。
    static void run(QCoreApplication *app,
                    AppFacade *facade,
                    const std::function<void(const Result &)> &onDone,
                    int delayMs = 2500);
};

} // namespace stelapp
