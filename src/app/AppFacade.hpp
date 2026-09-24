/*
 * AppFacade — 新 QML 界面访问引擎的单一稳定接口。
 *
 * 职责：暴露时间/地点/选择/显示选项的稳定接口，供 QML 与 ActionRouter 使用。
 * 禁止：不暴露 GL/Vulkan 句柄；不让 QML 直接遍历模块单例。
 *
 * 依据：开发计划一第 5 节 / 开发指导文档第 3 节。
 * 实现轨迹：
 *   - A0 骨架（2026-09-17）定义属性面。
 *   - T15（2026-09-24）最小切片落地：simulationPaused + fieldOfView（含
 *     zoomIn/zoomOut 便捷命令）。当时时间速率经 ISimPacing 走"宿主自己算"的帧泵。
 *   - T16（2026-09-24）时钟所有权收编为引擎内 StelClockController。
 *   - T17（2026-09-24）新增搜索/选择协调：持有 SearchResultsModel +
 *     ObjectInfoModel，提供 searchObjects / selectSearchResult / clearSelection。
 *   - T18（2026-09-24）新增定位与跟踪：locateSelected / setTracking /
 *     toggleTracking / isTracking。这是 A4 固定流程
 *     "开机→搜月球→定位→改时间→返回" 里的**定位环**。
 *
 * 时间语义（**T16 后已更新，勿再引用 T15 的旧说法**）：
 *   时间真源是**引擎内的 StelClockController**（docs/T16_SINGLE_SIM_CLOCK.zh_CN.md）：
 *   合流形态下引擎时钟处于 HostDriven，由宿主帧泵每帧 advanceSimClock(dt) 单点推进；
 *   StelCore::updateTime 在 HostDriven 下**不读墙钟**。
 *   故本类仍然**不直接写引擎时钟**——但它现在只是"把命令转给 ISimPacing"，
 *   而 ISimPacing 已退化为**对引擎时钟的纯投影**（暂停 = 引擎时钟 scale=0，
 *   速率 = 引擎 timeSpeed）。ISimPacing 不再是"另一个时间源"。
 *
 * 搜索/选择的分工（T17）：
 *   本类是**命令入口**，模型是**数据**。QML 侧约定：
 *     列表用 `model: searchResults`（模型直供），命令走 appFacade。
 *   理由：选择需要"引擎语义"（把 stableId 解析成对象并真的选中），
 *   那是本类的职责；模型只负责把数据摆好，不碰引擎状态。
 *
 * 定位/跟踪的语义（T18，**先读这段再改代码**）：
 *   1. "定位" = 把视向对准当前选中天体；"跟踪" = 之后持续把视向锁在它身上。
 *      引擎侧两者是分开的：`moveToObject()` 只做一次平滑移动（1.5s 自动移动），
 *      `setFlagTracking(true)` 兼做移动 + 锁定（等价于旧 GUI 的空格键）。
 *      ⇒ 本类的 `locateSelected(track=false)` 只移动，`locateSelected(true)` 移动+锁定。
 *   2. **家园行星守卫**：选中天体的英文名 == 当前观察地点所在行星名时，
 *      不能把视线对准"自己脚下"——旧 GUI 在多处都做了这条判断
 *      （`SearchDialog.cpp:1468-1484` 等）。本类复用同一条判据，拒绝理由
 *      经 `lastLocateRefusal()` 报 `"home-planet"`。
 *   3. `isTracking()` 读的是**合取真值**：引擎 `getFlagTracking()`
 *      **∧** 当前确有选中。原因见 .cpp 里的长注释（引擎在 unSelect 后不清标志）。
 *
 * 纪律：
 *   1. 所有方法只允许 GUI 线程调用（引擎对象全为 GUI 线程亲和，本类不设锁）。
 *   2. 引擎未编译（独立工程形态）或未引导时，全部方法安全 no-op——
 *      QML 命令栏在无引擎形态下可点，但不产生引擎效果（currentFov 返回 -1 可辨；
 *      搜索返回 0 行 + 明确空态理由；定位返回 false + 理由 "engine-unavailable"）。
 *   3. 幂等：setSimulationPaused 对相同值直接返回（防双触发的第一道闸）。
 */
#pragma once

#include <QObject>

#include "app/ObjectInfoModel.hpp"
#include "app/SearchResultsModel.hpp"

namespace stelapp {

//! 仿真推进控制（最小接口）。LiveSkyRuntime 实现；无引擎形态下为空。
//! 命名刻意不带 Clock：T16 落地 ClockController 时在此接口上扩展
//! （JD 单点写入、时间跳转、日历换算），接口形状已按"宿主单点写时钟"设计。
class ISimPacing
{
public:
    virtual ~ISimPacing() = default;
    //! 仿真推进比例：1=正常，0=暂停。帧泵照常出帧，只有 JD 推进被冻结。
    virtual void setSimScale(double scale) = 0;
    virtual double simScale() const = 0;
    //! 仿真推进速率（Julian day / 墙钟秒）。与 AppFacade::timeRate 同单位。
    virtual void setSimRate(double rate) = 0;
    virtual double simRate() const = 0;
};

class AppFacade : public QObject
{
    Q_OBJECT
    // QML 端只读属性经 NOTIFY 通知；白名单制，未列入的引擎属性不开放。
    Q_PROPERTY(bool simulationPaused READ simulationPaused WRITE setSimulationPaused NOTIFY simulationPausedChanged)
    Q_PROPERTY(double timeRate READ timeRate WRITE setTimeRate NOTIFY timeRateChanged)
    Q_PROPERTY(double fieldOfView READ fieldOfView WRITE setFieldOfView NOTIFY fieldOfViewChanged)
    // T18：跟踪状态（只读投影。**不设 QML 可写**——写侧必须走命令，见纪律 3 的同类理由）。
    Q_PROPERTY(bool tracking READ isTracking NOTIFY trackingChanged)
    Q_PROPERTY(QString trackedName READ trackedName NOTIFY trackingChanged)
    //! 上一次定位被拒的原因（稳定 ASCII 串）。QML 侧用 `locateRefusalText()` 取文案。
    Q_PROPERTY(QString lastLocateRefusal READ lastLocateRefusal NOTIFY lastLocateRefusalChanged)

public:
    explicit AppFacade(QObject *parent = nullptr);

    //! 装配点注入（main.cpp 在引擎帧泵启动成功后调用）。可传 nullptr（无引擎形态）。
    void attachSimControl(ISimPacing *sim);

    // ---- 时间（语义保留：JD 为 UT；速率单位 Julian day/second）----
    // THREAD: gui
    // 当前 JD 直接读引擎（只读不写，写侧归帧泵/T16 ClockController）。
    // 引擎不可用时返回 0.0 并置无效标记（QML 侧不应依赖此值）。
    Q_INVOKABLE double julianDay() const;
    // "60 倍速"必须换算为 60.0 Julian day/second 后传入，禁止把倍数直接当速率。
    double timeRate() const;
    void setTimeRate(double ratePerJulianDaySecond);

    // ---- 显示 ----
    // THREAD: gui
    double fieldOfView() const;
    //! 经引擎 zoomTo 平滑过渡（0.4s）到目标视场。越界值由引擎侧 min/max 钳制。
    void setFieldOfView(double degrees);

    bool simulationPaused() const;
    //! 幂等：值不变直接返回（不重复通知，不重复改帧泵状态）。
    void setSimulationPaused(bool paused);

    // ---- T15 命令便捷入口（ActionRouter 注册表的目标）----
    Q_INVOKABLE void zoomIn();    //!< 视场 ×0.8
    Q_INVOKABLE void zoomOut();   //!< 视场 ×1.25
    Q_INVOKABLE void togglePause() { setSimulationPaused(!m_simulationPaused); }

    // ---- T17 搜索 / 选择（模型持有 + 命令协调）----
    //! 本类持有的两个模型（main.cpp 以 context property 注入 QML，见头注"分工"）。
    //! 刻意不写成 Q_PROPERTY：QML 侧用 `model: searchResults` 直接消费模型本体，
    //! 走 context property 与 T15 的 ActionRouter 同一惯例，避免多注册一个 QML 类型。
    SearchResultsModel *searchResults() { return &m_search; }
    ObjectInfoModel *objectInfo() { return &m_info; }

    //! THREAD: gui
    //! 发起搜索（转交模型；模型内部过请求编号门）。
    //! @return 本次结果行数（无引擎/无匹配时为 0，空态理由见 searchResults().emptyReason）
    Q_INVOKABLE int searchObjects(const QString &query, int maxItems = 20);

    //! THREAD: gui
    //! 选中搜索结果第 row 行：取该行 stableId → 引擎回查并选中 → 回填信息模型。
    //! @return 是否选中成功（行越界/对象已失效/引擎不可用 → false 且信息模型归零）
    Q_INVOKABLE bool selectSearchResult(int row);

    //! THREAD: gui
    //! 按稳定标识选择（QML 侧若有缓存/外链可用该方法绕开列表）。
    Q_INVOKABLE bool selectByStableId(const QString &stableId);

    //! THREAD: gui
    //! 取消选中（引擎 unSelect + 信息模型归零）。
    Q_INVOKABLE void clearSelection();

    // ---- T18 定位 / 跟踪 ----
    //! THREAD: gui
    //! 把视向对准当前选中天体。
    //! @param track true = 移动并持续锁定（等价旧 GUI 空格键）；false = 只移动一次。
    //! @return 是否落地。失败时 `lastLocateRefusal()` 给出原因（稳定 ASCII 串）。
    Q_INVOKABLE bool locateSelected(bool track = true);

    //! THREAD: gui
    //! 开/关跟踪。开 = 同 locateSelected(true)（同一守卫）；关 = 仅解锁定，**不动选中**。
    Q_INVOKABLE bool setTracking(bool on);
    Q_INVOKABLE void toggleTracking() { setTracking(!isTracking()); }

    //! THREAD: gui
    //! "跟踪中"的**合取真值**：引擎标志 ∧ 当前确有选中。语义见头注第 3 条。
    bool isTracking() const;
    //! 正在跟踪的天体显示名；未跟踪时为空串。
    QString trackedName() const;

    //! 上一次定位被拒的原因（稳定 ASCII 串，QML 侧自行映射文案）：
    //! `"ok"` / `"engine-unavailable"` / `"no-selection"` / `"home-planet"`。
    //! 定位成功时复位为 `"ok"`。
    QString lastLocateRefusal() const { return m_refusal; }

    //! QML 侧文案（把稳定 token 翻成人话；判定逻辑永远看 token，不看文案）。
    Q_INVOKABLE QString locateRefusalText() const;

    //! 纯谓词：能否把视线对准该天体。抽出来是为了让自检能在**不需要家园行星
    //! 真的可检索**的情况下验证守卫逻辑（见 LocateCheck LOC-01）。
    //! 空串一律判 false —— 宁可漏判，也不要把"名字取不到"当成"是家园行星"而误拒。
    static bool isHomePlanet(const QString &objectEnglishName, const QString &locationPlanetName);

    // 观测量（自检用；不可作 UI 逻辑依据）
    quint64 locateCount() const { return m_locateCount; }
    quint64 locateRefusedCount() const { return m_locateRefusedCount; }

signals:
    void simulationPausedChanged(bool paused);
    void timeRateChanged(double ratePerJulianDaySecond);
    void fieldOfViewChanged(double degrees);
    void trackingChanged();
    void lastLocateRefusalChanged();

private:
    //! 引擎 StelMovementMgr 是否可用（已引导且 zoom 接口可达）。
    bool movementReady() const;
    //! 记录拒绝理由（同时累加拒绝计数）。nullptr/空串 → "ok"。
    void setRefusal(const char *reason);

    ISimPacing *m_sim = nullptr;
    bool m_simulationPaused = false;  // 与 LiveSkyRuntime 的 scale=1 默认一致（运行态）
    double m_timeRate = 1.0;          // Julian day / second（镜像值，真源在引擎时钟）

    // T17：值成员（QObject 子对象随本类生命周期；父指针保证 QML 侧不会被提前回收）。
    SearchResultsModel m_search{this};
    ObjectInfoModel m_info{this};

    // T18
    QString m_refusal = QStringLiteral("ok");
    quint64 m_locateCount = 0;
    quint64 m_locateRefusedCount = 0;
};

} // namespace stelapp
