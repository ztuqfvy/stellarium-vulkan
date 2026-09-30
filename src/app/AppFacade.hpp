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
 *   - T19（2026-09-24）新增时间写入：setJulianDay / setLocalDateTime /
 *     setTimeNow + 本地/UT 双日历投影。这是同一流程里的**"改时间"环**。
 *
 * 时间的读/写口径（T19，**先读这段再改**）：
 *   写：全部落到 `StelCore::setJD()` —— 它是 T16 确立的**唯一**跳转入口
 *       （内部 `simClock.jumpTo()` 重锚，所以不会被宿主帧泵拽回）。
 *  读：`julianDay()` 读 `getSimClockJD()`（T16 的单一真源）。**不读 `getJD()`** ——
 *      那个（`JD.first`）只在 `StelCore::updateTime()` 里从仿真时钟同步，
 *      读它等于读"上一帧的快照"。
 *  时区：引擎里所有 JD 都是 **UT 尺度**；"本地日历"= UT + `getUTCOffset(jd)`（小时）。
 *      写本地字段时 `-offset`、读本地字段时 `+offset` ——**方向搞反的后果是
 *      "输入 12:00 回显 20:00"，且只有往返比对才抓得到**。本类的往返恒等
 *      判据（TimeCheck TC-01）就是钉这件事的。
 *      两个方向一律复用 `StelUtils::getJDFromDate` / `getDateTimeFromJulianDay`
 *      与 `StelCore::getUTCOffset` —— **不重新发明天文/日历算法**（A-alpha 硬要求）。
 *  显示历法：1582-10-15 之前引擎按**儒略历**解释（`StelUtils::getJDFromDate` 内注释），
 *      之后按格里历。本类**照抄引擎判定**，不另立一套。
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
 *   3. `isTracking()` 直接读引擎 `getFlagTracking()`。T23 之前这里读**合取
 *      真值**（引擎标志 ∧ 确有选中）——引擎 unSelect 后不清标志，单读会谎报。
 *      T23 在引擎侧根治（selectedObjectChange 处理 RemoveFromSelection）后，
 *      flagTracking==true 蕴含有选中，合取退役。不变量由 LOC-08b/LOC-09 守着。
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
    // T19：上一次时间写入的结果 token（"ok" / "invalid-date" / "out-of-range" …）。
    // **必须是 Q_PROPERTY**：QML 绑定里写 `appFacade.lastTimeRefusal === "ok"` 时，
    //   只有它是属性才会拿到**字符串**并建立依赖；若只声明成 Q_INVOKABLE 方法，
    //   那个表达式读到的是**函数对象**，恒为 false —— TimePage 的"已生效"分支
    //   就变成了永远走不到的死代码，而 `timeRefusalText()` 在 ok 时返回空串，
    //   于是"写入成功"反而显示成**空白 + 红字**（T19 用最外层注入的 UI 判据抓到）。
    //   与 T18 的 `lastLocateRefusal` 同形态。
    Q_PROPERTY(QString lastTimeRefusal READ lastTimeRefusal NOTIFY lastTimeRefusalChanged)

public:
    explicit AppFacade(QObject *parent = nullptr);

    //! 装配点注入（main.cpp 在引擎帧泵启动成功后调用）。可传 nullptr（无引擎形态）。
    void attachSimControl(ISimPacing *sim);

    // ---- 时间（语义保留：JD 为 UT；速率单位 Julian day/second）----
    // THREAD: gui
    //! 当前仿真 JD（UT 尺度）。T19 起读 `getSimClockJD()`（T16 的单一真源），
    //! 不再读 `getJD()`（= JD.first，只在 updateTime 里被同步，等于上一帧快照）。
    //! 引擎不可用时返回 0.0（QML 侧用 `utcOffsetHours() < -900` 之类不可靠，
    //! 请改用 `timeRefusalText()`/`lastTimeRefusal()` 判可用性）。
    Q_INVOKABLE double julianDay() const;
    // "60 倍速"必须换算为 60.0 Julian day/second 后传入，禁止把倍数直接当速率。
    double timeRate() const;
    void setTimeRate(double ratePerJulianDaySecond);

    // ---- T19 时间只读投影（"改时间"环的**读侧**）----
    //!
    //! **刻意不做 Q_PROPERTY**：JD 每帧都在变（HostDriven 帧泵），挂 NOTIFY
    //! 会让 QML 每帧重建绑定 → 刷屏。与 MainWindow.qml 里 fovLabel 的轮询读
    //! 同惯例：**离散状态**才用属性通知（如 T18 的 tracking），**连续量**一律轮询。
    //!
    //! 本地/UT 双日历并排暴露，是为了让"区分 UTC、时区"可被**看见**（A-alpha 要求）。
    //@{
    //! 本地时区相对 UT 的偏移（**小时**，含 DST）。口径即 `StelCore::getUTCOffset(jd)`
    //! （内部 shiftInSeconds/3600）。引擎不可用返回 0.0。
    Q_INVOKABLE double utcOffsetHours() const;
    //! 当前时刻的**本地**日历文本，形如 `2026-09-24 14:26:55`（引擎口径）。
    Q_INVOKABLE QString localDateTimeText() const;
    //! 同一时刻的 **UT** 日历文本。与上一条并排显示即"区分 UTC/时区"。
    Q_INVOKABLE QString utcDateTimeText() const;
    //! 本地日历的单个字段（给 QML 的 6 个自旋框回填）：0=年 1=月 2=日 3=时 4=分 5=秒。
    //! 越界或引擎不可用 → 0（QML 侧不要用 0 当合法值判断，用 `year` 更可靠的是 >= 1）。
    Q_INVOKABLE int localDateTimeField(int which) const;
    //@}

    // ---- T19 时间写命令（"改时间"环的**写侧**：全部是命令，无 QML 可写属性）----
    // THREAD: gui
    //! 绝对写入（**唯一入口**）：落到 `StelCore::setJD(jd)` → `simClock.jumpTo()`。
    //! @param jd UT 尺度的 JD。超出引擎可表示范围（`StelCore::updateTime` 的
    //!        钳制区间）时**拒绝**而不是"写进去然后被悄悄改掉"。
    //! @return 是否落地；失败理由见 `lastTimeRefusal()`。
    Q_INVOKABLE bool setJulianDay(double jd);

    //! 按**本地日历**的 6 个字段写入（"改时间"对话框的 6 条写入路径）。
    //! 内部把本地 → UT（`-getUTCOffset(cjd)/24`），再走 `setJulianDay`。
    //! 字段范围非法（如 13 月 / 32 日 / 25 时）→ 拒绝，理由 `"invalid-date"`，
    //! 且**不改动时钟**（拒绝必须无副作用）。
    //! @note 范围校验用 `QDate::isValid`（与引擎 `getJDFromDate` 内部的判定同一口径），
    //!       因为引擎那个函数在 1582 年前的分支**不做校验**，会静默给出垃圾 JD。
    Q_INVOKABLE bool setLocalDateTime(int y, int m, int d, int h, int min, int s);

    //! 跳回"现在"（`StelCore::setTimeNow`，即 `setJD(getJDFromSystem())`）。
    //! 不加时区偏移——"现在"是绝对时刻。
    Q_INVOKABLE void setTimeNow();

    //! 上一次时间写入被拒的原因（稳定 ASCII 串）：
    //! `"ok"` / `"engine-unavailable"` / `"invalid-date"` / `"out-of-range"`。
    Q_INVOKABLE QString lastTimeRefusal() const { return m_timeRefusal; }
    //! QML 侧文案（把稳定 token 翻成人话；判定逻辑永远看 token，不看文案）。
    Q_INVOKABLE QString timeRefusalText() const;

    // 观测量（自检用；不可作 UI 逻辑依据）
    quint64 timeWriteCount() const { return m_timeWriteCount; }
    quint64 timeRefusedCount() const { return m_timeRefusedCount; }

    // ---- T22 时间页收尾（MJD / 显示历法 / 时区 / 速率真源）----
    //!
    //! 这一节把计划文档里挂着的那四项补齐。**全部是转发或照抄**，没有一行新算法：
    //!   MJD   → 加减 2400000.5（引擎 `StelCore::setMJDay` 的定义，`StelCore.cpp:1275`）
    //!   历法   → 照抄旧界面判定（`DateTimeDialog.cpp:251`）
    //!   时区   → 照抄旧 `LocationDialog` 的口径（`QTimeZone` + `setCurrentTimeZone`）
    //!   速率   → 照抄旧状态栏的换算（`StelGuiItems.cpp:885-907`）
    //!
    //! @name MJD（Modified Julian Day，**UT 尺度**）
    //@{
    //! 当前 MJD = `julianDay() - 2400000.5`。
    //!
    //! ⚠️ **刻意不用 `StelCore::getMJDay()`**：它返回 `JD.first - 2400000.5`
    //! （`StelCore.cpp:1280`），而 `JD.first` 只在 `updateFrame`/`updateTime` 里
    //! 从仿真时钟同步 = **上一帧快照**。T19 已经因为同一理由把读侧从 `getJD()`
    //! 改成 `getSimClockJD()`；这里若图省事调 `getMJDay()`，就把那个坑从 JD 挪到
    //! MJD 上，而且**更隐蔽**（数值只差几毫秒，肉眼与单次断言都抓不到）。
    //! 本方法从**同一个真源**导出 ⇒ "MJD ≡ JD − 2400000.5" 是一条可断言的恒等式。
    Q_INVOKABLE double modifiedJulianDay() const;

    //! 以 MJD 写入（UT 尺度）：内部 `+2400000.5` 后走**同一个** `setJulianDay()`。
    //! 复用它的范围校验与 token 语义（越界 → `"out-of-range"` 且**无副作用**）。
    //! 旧界面 `DateTimeDialog::mjdChanged` 是增量写法（`applyJD(jd + delta)`），
    //! 与绝对写法等价（MJD↔JD 只差常数）；这里取绝对写法，好让判据断言"位移精确"。
    Q_INVOKABLE bool setModifiedJulianDay(double mjd);
    //@}

    //! @name 显示历法
    //@{
    //! 当前日期所属历法的**稳定 token**：`"julian"` / `"gregorian"` / `"unknown"`。
    //!
    //! 判定**照抄**旧界面（`DateTimeDialog.cpp:251`：`if (jd < 2299161) → 儒略历`），
    //! 该字面量即 `StelUtils.cpp:848` 的 `JD_GREG_CAL`（1582-10-15 换历）。
    //! 这是"照抄引擎判定"，不是"自己发明规则"——引擎的日期换算内部就用它：
    //! `getDateFromJulianDay` 在 `julian >= JD_GREG_CAL` 走格里历分支，否则走儒略历。
    //! ⚠️ 判定用 **`julianDay()`（真源）**，不用 `getJD()`（快照）—— 否则换历那一刻
    //! 会晚一帧才显示出来。
    Q_INVOKABLE QString dateCalendarToken() const;
    //! QML 文案（token → 人话；判定永远看 token，不看文案）。
    Q_INVOKABLE QString dateCalendarText() const;
    //@}

    //! @name 时区（照抄旧 `LocationDialog` 的口径）
    //@{
    //! 引擎当前的 IANA 时区 id（如 `"Asia/Shanghai"`）。引擎不可用 → 空串。
    Q_INVOKABLE QString timeZoneId() const;
    //! 是否**自定义**时区（关 = 跟随观察地点自带的时区）。对应引擎 `flagUseCTZ`。
    Q_INVOKABLE bool useCustomTimeZone() const;
    //! 是否启用夏令时修正（对应引擎 `flagUseDST`）。
    Q_INVOKABLE bool useDST() const;
    //! 可用 IANA 时区 id 列表（照抄 `LocationDialog::populateTimeZonesList` 的
    //! `QTimeZone::availableTimeZoneIds()`，排序后返回）。**只构建一次并缓存** ——
    //! 这个列表有 600+ 项，每次调用都重排会拖慢 QML 的组合框。
    Q_INVOKABLE QStringList availableTimeZoneIds() const;

    //! 写入时区 id。**只写时区，不动 `flagUseCTZ`** —— 旧 `LocationDialog` 里
    //! 两者也是分开的（CheckBox 单独控制）。QML 侧要让"选了就生效"，就显式再调
    //! 一次 `setUseCustomTimeZone(true)`（两条路径分开 ⇒ 判据可分别验）。
    //! @return 是否落地（引擎不可用 / id 不能被 `QTimeZone` 解析 → false）。
    Q_INVOKABLE bool setTimeZoneId(const QString &tz);
    Q_INVOKABLE void setUseCustomTimeZone(bool on);
    Q_INVOKABLE void setUseDST(bool on);

    // 观测量（自检用）
    quint64 timeZoneWriteCount() const { return m_timeZoneWriteCount; }
    //@}

    //! @name 速率（**引擎 timeSpeed 的投影**，不是本地缓存）
    //@{
    //! 当前仿真速率（**Julian day / second**）。
    //!
    //! 🔴 T22 修正的真实缺陷：原实现直接返回本地缓存 `m_timeRate`，而它只在
    //! `setTimeRate()` 里被更新。引擎的六个速率动作
    //! （`actionIncrease_Time_Speed` / `...Decrease` / `..._Less` /
    //! `actionSet_Real_Time_Speed` / `actionSet_Time_Rate_Zero` /
    //! `actionToggle_Time_Rate_Zero` / `actionSet_Time_Reverse`）
    //! 走的是 `StelCore::increaseTimeSpeed()` 之类，**完全不经过本类**
    //! ⇒ 用户按一次 `L` 键，引擎真的加速了，而 QML 读到的数纹丝不动。
    //! 这正是本仓库反复复发的"仪表没接在实况上"（T14/T15 血泪第 3 条）。
    //!
    //! 现在读 `ISimPacing::simRate()`（其实现即 `core->getTimeRate()`，见
    //! `LiveSkyRuntime.cpp:302-305`）；无引擎/未挂 ISimPacing 时才回退缓存。
    //!
    //! ⚠️ QML 侧**必须轮询**这个值（与 JD 同惯例），不要指望 NOTIFY：
    //! 引擎动作改速率时本类并不知道，`timeRateChanged` 不会发。
    //! （声明见上方的 `Q_PROPERTY(double timeRate ...)`；此处只补语义。）

    //! 人类可读速率文本，形如 `x3600 (1.00 hr/s)`。换算口径**照抄**
    //! `StelGuiItems.cpp:885-907`：`|rate| / JD_SECOND` → 秒/秒，
    //! 再 ≥60 → 分/秒，≥60 → 时/秒，≥24 → 天/秒，≥365.25 → 年/秒。
    //! （照抄不"改进"：旧界面的单位跳档阈值就是这四档。）
    Q_INVOKABLE QString timeRateText() const;
    //! 时间方向 token：`"forward"` / `"backward"` / `"stopped"`。
    //! 引擎 `actionSet_Time_Reverse` 会让速率变号，零速率时显示"停"。
    Q_INVOKABLE QString timeDirection() const;
    //@}

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

    //! T25：滚轮 → 引擎的**保真转发**入口（QML WheelHandler 调用）。
    //! 旧宿主链路是 StelMainView::wheelEvent → StelApp::handleWheel（引擎自己把
    //! 事件分发给各模块：缩放走 StelMovementMgr，Ctrl+滚轮改时间等 modifiers 语义
    //! 全在引擎侧）。本方法只负责用窗口真实 angleDelta 合成 QWheelEvent 并原样
    //! 交给 handleWheel —— **不在此复刻任何缩放语义**（复刻=双轨）。
    //! @param dx dy 窗口真实 angleDelta（QML wheel.angleDelta）；
    //!        modifiers 修饰键掩码（Qt::KeyboardModifiers 的 int 值）。
    //! 无引擎形态：安全 no-op。
    //! THREAD: gui
    Q_INVOKABLE void wheelZoom(int dx, int dy, int modifiers);

    //! T28：触控板捏合 → 引擎的**保真转发**入口（QML PinchHandler 调用）。
    //! 旧宿主链路是 StelMainView::grabGesture(Qt::PinchGesture) → gestureEvent →
    //! pinchTriggered → StelApp::handlePinch(QPinchGesture::scaleFactor(), true)，
    //! 引擎侧 StelMovementMgr::handlePinch 做 `previousFov=getAimFov()` +
    //! `zoomTo(previousFov/scale, 0)`（0ms ⇒ 立即生效 ⇒ 每次调用天然以**当前真值**
    //! 为基准，即增量语义）。合流形态此前没接 —— QML 天空页没有任何 PinchHandler
    //! （T25 滚轮、T27 鼠标之外，同一条"引擎输入面"的第三块）。
    //! @param scale 捏合的**乘法变化量**（QML `PinchHandler::scaleChanged(delta)` 的
    //!        delta，官方语义 = activeScale 的相对变化，如 2→2.5 时给 1.25）——
    //!        与旧宿主读的 scaleFactor() 逐字对应。**不要传 activeScale**（手势内
    //!        累积量）：引擎每次都以当前 aimFov 为基准再除一次，传累积量会双重累积。
    //! 本方法只做透传前的健全性过滤（丢弃 0.5..2 之外的荒诞比值，与旧宿主
    //! `zoom < 2 && zoom > 0.5` 的同一道闸），**不复刻任何缩放语义**（复刻=双轨）；
    //! 视场 min/max 钳制仍在引擎侧。
    //! 无引擎形态：安全 no-op。
    //! THREAD: gui
    Q_INVOKABLE void pinchZoom(double scale);

    //! T27：天空页鼠标（点击选中 / 拖拽平移 / 右键取消）→ 引擎的**保真转发**入口
    //! （QML MouseArea 调用）。旧宿主链路是 StelMainView 的 mousePress/Release →
    //! StelApp::handleClick、mouseMove → StelApp::handleMove（press 置 isDragging、
    //! release 无拖拽时 findAndSelect、右键 release 反选、Ctrl+拖拽改时间等语义
    //! 全在引擎侧）。本组方法只负责**坐标空间适配**（view 事实，非引擎语义）：
    //! 合流形态引擎渲染固定 renderSize 帧再缩放显示到窗口 ⇒ QML 逻辑坐标与引擎
    //! 投影像素**不同构**，须按 (x/wq·wp, (1-y/hq)·hp) 比例映射并并入 y 翻转
    //! （旧宿主 convertMouseEvent 的 `h-1-y` 是它在等尺寸下的特例）；再除以
    //! dppp，因 handleClick/handleMove 内部会乘回。**不在此复刻任何导航/选择
    //! 语义**（复刻=双轨）。
    //! @param x y MouseArea 本地坐标（QML mouse.x/y，逻辑像素）；
    //!        viewportWidth/Height MouseArea 尺寸（映射基准）；button/buttons/
    //!        modifiers 为 Qt::MouseButton / Qt::MouseButtons /
    //!        Qt::KeyboardModifiers 的 int 值。**button 必须透传真实按键**：引擎
    //!        release 分支按 button 区分（右键 release = 反选，左键 = 选中对象）
    //!        ——硬编码左键会把右键反选变成"点哪选哪"。
    //! 无引擎形态：安全 no-op。
    //! THREAD: gui
    Q_INVOKABLE void skyMousePress(double x, double y, double viewportWidth,
                                   double viewportHeight, int button = 1, int modifiers = 0);
    Q_INVOKABLE void skyMouseRelease(double x, double y, double viewportWidth,
                                     double viewportHeight, int button = 1, int modifiers = 0);
    Q_INVOKABLE void skyMouseMove(double x, double y, double viewportWidth,
                                  double viewportHeight, int buttons = 1);

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

    // ══ T33：观察地点（观察者位置）写入面 ═══════════════════════════════════════
    // 全部口径来自 T33-A 的地点数据面**探针**（证据 docs/evidence/2026-09-29-t33-location/）：
    //   ① 地点库在合流形态下**加载正常**：33501 条 / 193 区域 / 496 时区名；
    //      installDir 有编译期兜底（STELLARIUM_SOURCE_DIR）⇒ **不依赖 cwd**
    //      （cwd=仓库根走 "."、cwd=别处走源码目录，两条都能加载）。
    //   ② `locationForString(单名)` **既不报错也不命中**，而是返回一个 `role='!'`、
    //      坐标全 0 的**无效地点**（StelLocationMgr.cpp:784 的兜底）
    //      ⇒ 对外一律走 **ID**（`getID()` = `"name, region"`），不拿用户输入直接喂它。
    //   ③ 引擎 `StelLocation::isValid()` **不校验经纬度范围**（StelLocation.cpp:296
    //      只查 `role=='!'` 与"经纬不同时为 0"）⇒ 范围校验是**应用层的责任**。
    //   ④ `moveObserverTo(loc, **0.0**)` 才走瞬时 `StelObserver` 分支（>0 是
    //      `SpaceShipObserver` 飞行动画）；写后时区**联动**（`setObserver` 里
    //      `setCurrentTimeZone(iana)`，StelCore.cpp:1543-1547）—— 探针实测
    //      绵阳(Asia/Shanghai) → 巴黎(Europe/Paris)，UTCOffset +8h → +2h。

    //! 当前观察地点名（引擎不可用 → 空串）。
    Q_INVOKABLE QString locationName() const;
    //! 当前观察地点的**完整 ID**（`"name, region"`）—— 与 setLocationById 配对。
    Q_INVOKABLE QString locationId() const;
    Q_INVOKABLE double locationLatitude() const;
    Q_INVOKABLE double locationLongitude() const;
    Q_INVOKABLE double locationAltitudeMeters() const;
    Q_INVOKABLE QString locationPlanet() const;
    //! 当前地点**自带**的时区（iana）。刻意与 `timeZoneId()` 区分：后者是 core 的
    //! "当前时区"，可能被用户显式改过；这里是"地点给的默认值"。
    Q_INVOKABLE QString locationTimeZone() const;

    //! T33：坐标范围**纯谓词**。抽出来是为了让自检能在**不需要引擎**的情况下验证
    //! 范围闸（照 isHomePlanet 的先例：守卫逻辑必须**恒可验**，不能因环境漏测）。
    //! ⚠️ 写入路径**直接调**这三个谓词 —— 不是"判据另写一遍"（那会让判据与被测
    //! 代码双轨，"规则正确"与"规则被调用"就又分家了）。
    static bool isAcceptableLatitude(double deg);
    static bool isAcceptableLongitude(double deg);
    static bool isAcceptableAltitude(double meters);

    //! T33：**唯一**的地点写入入口 —— 转发 `StelCore::moveObserverTo(loc, 0.0)`。
    //! 刻意不暴露"动画时长"：A-alpha 要的是"写下去立刻能读到"，飞行动画会把"写完"
    //! 变成跨帧过程、判据得跟着挂等待 —— 那是另一件事（T35 的时间链路脱钩线索同源）。
    //! @p id 必须是 `locationId()` 那种完整 ID（探针 ②）。
    Q_INVOKABLE bool setLocationById(const QString &id);
    //! T33：按经纬度写。范围校验在**本层**做（引擎不校，探针 ③）：
    //! lat ∈ [-90, 90]、lon ∈ [-180, 180]、alt ∈ [-1000, 100000] 米。
    //! @p name 为空则由本层生成 `"观察点 39.90N 116.40E"`。
    //! 时区**不动**（ad-hoc 地点的 iana 留空 ⇒ 引擎 `setObserver` 的
    //! `!ianaTimeZone.isEmpty()` 条件直接跳过）—— 保守且可逆，文档写明。
    Q_INVOKABLE bool setLocationByCoordinates(double latitudeDeg, double longitudeDeg,
                                              double altitudeMeters,
                                              const QString &name = QString());

    //! T33：地点写入结果 token：`ok` / `invalid-latitude` / `invalid-longitude` /
    //! `invalid-altitude` / `not-found` / `engine-unavailable` / `readback-mismatch`。
    //! **必须是 Q_PROPERTY**（同 lastTimeRefusal 的理由）：QML 绑定里写
    //! `appFacade.lastLocationRefusal === "ok"` 时，只有属性才拿得到字符串并建立依赖；
    //! 声明成 Q_INVOKABLE 方法会读到**函数对象**，比较恒 false。
    Q_PROPERTY(QString lastLocationRefusal READ lastLocationRefusal NOTIFY lastLocationRefusalChanged)
    Q_INVOKABLE QString lastLocationRefusal() const { return m_locationRefusal; }
    Q_INVOKABLE QString locationRefusalText() const;

    //! T33：按关键词查地点（大小写不敏感的**子串**匹配 name/region/planet），
    //! 返回**完整 ID** 列表（直接喂 `setLocationById`）。刻意不走 `locationForString`：
    //! 它对单名返回无效地点（探针 ②）。
    Q_INVOKABLE QStringList findLocations(const QString &query, int maxItems = 20) const;

    // 观测量（自检用）
    quint64 locationWriteCount() const { return m_locationWriteCount; }
    quint64 locationRefusedCount() const { return m_locationRefusedCount; }

    // 观测量（自检用；不可作 UI 逻辑依据）
    quint64 locateCount() const { return m_locateCount; }
    quint64 locateRefusedCount() const { return m_locateRefusedCount; }

    // ---- T34 工具栏：显示开关状态面（读侧；写侧走 ActionRouter.trigger 透传）----
    //!
    //! 背景：桌面版这些开关挂在 StelGui 底栏，**合流形态根本不创建 StelGui**。
    //! 开关本体是引擎 StelAction（连着各模块的 bool Q_PROPERTY），命令路径已经
    //! 由 `ActionRouter::trigger(id)` 的**引擎透传**覆盖（T15 写的，T34 之前
    //! 没有任何判据走过这条路）。这里只补**读侧**：QML 按钮的 checked 态。
    //!
    //! 为什么是"revision token"而不是每个开关一个 Q_PROPERTY：12 个开关是
    //! **固定清单**，若逐个 Q_PROPERTY 会把通知矩阵翻 12 倍；而 QML 绑定真正
    //! 需要的只是"引擎某处翻转了，去重读一次"这件事。于是：
    //!   `checked: { appFacade.displayTogglesRevision;   // ← 绑定必须**真的读** token
    //!               return appFacade.actionChecked(id) }`
    //! （T15 铁律：**绑定只登记"绑定里实际读过"的属性**——只在绑定里调
    //!   `actionChecked()` 而不读 token，token 变化时绑定不会重算。）
    //@{
    //! 引擎显示开关的**修订号**：`StelActionMgr::actionToggled` 每发射一次 +1。
    //! 引擎未引导时恒 0。订阅在 `attachSimControl`（此刻引擎必已 boot、动作已注册）。
    Q_PROPERTY(int displayTogglesRevision READ displayTogglesRevision NOTIFY
                   displayTogglesRevisionChanged)
    int displayTogglesRevision() const { return m_displayTogglesRevision; }

    //! 引擎动作当前 checked 值（不存在 / 非 checkable / 引擎未引导 → false）。
    Q_INVOKABLE bool actionChecked(const QString &actionId) const;
    //! 引擎动作是否 checkable（连着 bool 属性；不存在 / 引擎未引导 → false）。
    //! 工具栏用它把"动作存在但不是开关"挡在 UI 外（面板里只放 checkable 项）。
    Q_INVOKABLE bool actionIsCheckable(const QString &actionId) const;
    //! 引擎动作的英文短描述（StelAction::getText；不存在 → 空串）。
    Q_INVOKABLE QString actionText(const QString &actionId) const;
    //@}

signals:
    void simulationPausedChanged(bool paused);
    void timeRateChanged(double ratePerJulianDaySecond);
    void fieldOfViewChanged(double degrees);
    void trackingChanged();
    void lastLocateRefusalChanged();
    //! T19：时间写入结果 token 变化时通知（QML 的状态行绑定靠它重算）。
    void lastTimeRefusalChanged();
    //! T33：地点写入结果 token 变化时通知。
    void lastLocationRefusalChanged();
    //! T34：任一引擎显示开关翻转时通知（QML checked 绑定经 revision token 重算）。
    void displayTogglesRevisionChanged();

private:
    //! 引擎 StelMovementMgr 是否可用（已引导且 zoom 接口可达）。
    bool movementReady() const;
    //! 记录拒绝理由（同时累加拒绝计数）。nullptr/空串 → "ok"。
    void setRefusal(const char *reason);
    //! T19：记录时间写入拒绝理由（同时累加拒绝计数）。nullptr/空串 → "ok"。
    void setTimeRefusal(const char *reason);
    //! T33：记录地点写入拒绝理由（同时累加拒绝计数）。nullptr/空串 → "ok"。
    void setLocationRefusal(const char *reason);
    //! T19：把 JD 格式化为日历文本（`local` 为 true 时套 UTC 偏移）。引擎不可用 → 空串。
    QString formatJd(double jd, bool local) const;
    //! T23：懒连接引擎 flagTrackingChanged → 本类 trackingChanged（只连一次）。
    //! 引擎侧自行改变跟踪状态（unSelect、Esc、旧 GUI 键位）时 QML 绑定才能收到通知。
    void ensureTrackingForwarding();
    //! T34：懒连接引擎 actionToggled → displayTogglesRevision 自增（只连一次）。
    void ensureDisplayForwarding();

    ISimPacing *m_sim = nullptr;
    bool m_simulationPaused = false;  // 与 LiveSkyRuntime 的 scale=1 默认一致（运行态）
    double m_timeRate = 1.0;          // Julian day / second（镜像值，真源在引擎时钟）
    bool m_trackingForwarded = false; // T23：引擎跟踪信号转发是否已连接

    // T17：值成员（QObject 子对象随本类生命周期；父指针保证 QML 侧不会被提前回收）。
    SearchResultsModel m_search{this};
    ObjectInfoModel m_info{this};

    // T18
    QString m_refusal = QStringLiteral("ok");
    quint64 m_locateCount = 0;
    quint64 m_locateRefusedCount = 0;

    // T19
    QString m_timeRefusal = QStringLiteral("ok");
    quint64 m_timeWriteCount = 0;
    quint64 m_timeRefusedCount = 0;

    // T22
    quint64 m_timeZoneWriteCount = 0;
    //! 时区 id 列表缓存（600+ 项，构建一次）。
    mutable QStringList m_tzCache;

    // T33
    QString m_locationRefusal = QStringLiteral("ok");
    quint64 m_locationWriteCount = 0;
    quint64 m_locationRefusedCount = 0;

    // T34
    int m_displayTogglesRevision = 0;
    //! T34：引擎 actionToggled → revision 的转发是否已连接（只连一次）。
    bool m_displayForwarded = false;
};

} // namespace stelapp
