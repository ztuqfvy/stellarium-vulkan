/*
 * StelClockController — T16 单一仿真时钟（时钟所有权收编）。
 *
 * ── 为什么需要它 ────────────────────────────────────────────────────────────
 * T14 审计（docs/T14_UPDATE_CLOCK_AUDIT.zh_CN.md）盘出合流形态的时间推进是
 * **事实上的单点**，但只是"约定"而非"机制"：
 *
 *     setTimeRate(0)                    ← 架空引擎墙钟推进（墙钟 ×0 仍在每帧算）
 *     宿主每帧 core->setJD(jd0 + sim×rate)   ← 唯一的时钟写入点
 *
 * 这个约定有三个缺口：
 *   ① 宿主把 JD 当作**闭式公式的产物**（jd0 + 累计秒数 × 速率）。任何外部跳转
 *      （插件/脚本/GUI 调 core->setJD）都会被宿主下一帧**直接覆盖**——
 *      跳转"看得见一瞬间，然后被拽回去"。
 *   ② `setTimeRate(0)` 是"用速率 0 把墙钟路径乘没"，语义上是 hack：
 *      updateTime 仍在每 tick 读墙钟、算一次无用的乘加，且速率字段被占用，
 *      插件调 setTimeRate 会与架空方案互相打架。
 *   ③ "只有一个 update 驱动源"依赖 StelMainView 恰好不 show 这个**巧合**
 *      （实测坐实：30 分钟长跑日志中 paintGL/drawEnded 各 0 次）。一旦有任何
 *      意外 paint，旧宿主 fpsTimer 会静默自启，变成两个驱动源——不报错、不崩，
 *      只让 CPU 翻倍、帧率抖动。
 *
 * ── 本类做什么 ──────────────────────────────────────────────────────────────
 * 把 JD 的**所有权**从"宿主的闭式公式"搬到引擎内部的一个显式对象上：
 *
 *   · 单一真源   m_jd —— 两模式下唯一的当前仿真时间。
 *   · 单一写入   jumpTo() 是**唯一**的外部跳转入口（StelCore::setJD/setJDE 调它）。
 *                插件/脚本/GUI 因此零改造地"跳转生效且不被拽回"。
 *   · 两种推进   HostDriven（宿主帧泵 advanceHostDriven）/ EngineWallClock（墙钟）。
 *                同一时刻只有一个模式生效 → 只有一个推进源。
 *   · 暂停语义   scale=0 冻结 JD 而速率保留（恢复不掉速、不补时间）。
 *
 * ── 与速率的关系（刻意不搬 timeSpeed）──────────────────────────────────────
 * 推进速率仍由 StelCore 的 `timeSpeed` 持有：它是引擎既有公共状态
 * （700+ 处读取 + timeRateChanged 信号 + RemoteSync/插件/脚本依赖），
 * 搬进本类只会制造第二份真相。本类只管"**推进与重锚**"，速率由调用方每 tick 传入。
 * 好处：插件调 setTimeRate(60) 在 HostDriven 下自然变成"宿主按 60 倍推进"，
 * 插件无需知道宿主是谁。
 *
 * ── 纪律 ────────────────────────────────────────────────────────────────────
 * 1. 纯逻辑，无 GL / 无 QObject / 无定时器——可脱离引擎独立自检（selfTest）。
 * 2. 非线程安全（与 StelCore 同：GUI 线程亲和）。
 * 3. `advanceHostDriven` 只接受**非负**墙钟差：时间倒退只能由 jumpTo（绝对写入）
 *    表达，不能由推进表达——否则同一帧内会有两种"倒退"语义。
 */
#pragma once

#include <QString>
#include <QStringList>

class StelClockController
{
public:
	//! 仿真时钟的推进模式。同一时刻只有一个模式生效。
	enum class Mode
	{
		//! 旧形态（stellarium.exe，QWidget 宿主）：updateTime 每 tick 读墙钟，
		//! 由锚点（jdOfLastJDUpdate / milliSecondsOfLastJDUpdate）+ 墙钟差 × 速率得到 JD。
		//! 这是 T16 之前**唯一**存在的形态，行为必须逐位保持。
		EngineWallClock,
		//! 合流形态（stelQuickUI.exe，QML+Vulkan 宿主）：JD 由宿主帧泵经
		//! advanceHostDriven 单点推进；updateTime **不读墙钟**。
		HostDriven
	};

	StelClockController() = default;

	Mode mode() const { return m_mode; }
	//! 切线：调用方负责在新模式下重新锚定（见 StelCore::setSimClockHostDriven）。
	void setMode(Mode m) { m_mode = m; }

	//! 当前仿真 JD（两模式下的唯一真源）。
	double jd() const { return m_jd; }
	//! 是否已锚定过（未锚定时 jd() 无意义，恒为 0）。
	bool valid() const { return m_valid; }

	//! 起步锚定。幂等：重复调用以最后一次为准。
	void reset(double jd0);

	//! **唯一**的外部跳转入口（用户/插件/脚本/GUI 写时钟）。绝对写入，立即生效。
	void jumpTo(double jd);

	//! 宿主帧泵推进（HostDriven）。dtWallSeconds 为墙钟秒差，非负（<=0 忽略）。
	//! 实际推进量 = dtWall × ratePerSecond × scale()。返回推进后的 JD。
	double advanceHostDriven(double dtWallSeconds, double ratePerSecond);

	//! 旧形态推进（EngineWallClock）。由锚点 + 已流逝墙钟秒 × 速率算出 JD。
	//! anchorJd/elapsedSeconds 由 StelCore 传入（那里才是锚点的持有者）。
	double advanceEngineWallClock(double anchorJd, double elapsedSeconds, double ratePerSecond);

	//! 推进比例：1=正常，0=暂停（帧泵照跑、JD 冻结）。负值钳为 0（倒放请用负**速率**）。
	void setScale(double scale) { m_scale = scale > 0.0 ? scale : 0.0; }
	double scale() const { return m_scale; }

	static QString modeName(Mode m);

	//! 纯逻辑自检（无 GL / 无引擎 / 无事件循环）。全部通过返回 true，明细写入 log。
	static bool selfTest(QStringList *log);

private:
	//! 合计 4 个字长状态——刻意保持"小到可以被审计"。
	Mode m_mode = Mode::EngineWallClock;
	double m_jd = 0.0;
	bool m_valid = false;
	double m_scale = 1.0;
};
