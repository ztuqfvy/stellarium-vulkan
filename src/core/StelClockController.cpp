// StelClockController 实现（T16）。设计动机与纪律见头文件。
#include "core/StelClockController.hpp"

#include <QtGlobal>

void StelClockController::reset(double jd0)
{
	m_jd = jd0;
	m_valid = true;
}

void StelClockController::jumpTo(double jd)
{
	// 绝对写入：不叠加、不看模式、不看 scale。暂停状态下跳转同样生效
	// （"暂停"约束的是**推进**，不是"用户直接指定时刻"）。
	m_jd = jd;
	m_valid = true;
}

double StelClockController::advanceHostDriven(double dtWallSeconds, double ratePerSecond)
{
	if (!m_valid)
		return m_jd;
	// 非负墙钟差：本方法只表达"向前推进"。倒退（时间倒放/用户往回拨）走 jumpTo，
	// 或由负速率（ratePerSecond<0）表达——两条路径互斥，不会在一帧里叠加。
	if (dtWallSeconds <= 0.0)
		return m_jd;
	m_jd += dtWallSeconds * ratePerSecond * m_scale;
	return m_jd;
}

double StelClockController::advanceEngineWallClock(double anchorJd,
                                                   double elapsedSeconds,
                                                   double ratePerSecond)
{
	// 旧形态语义原样搬运：JD = 锚点 + 墙钟流逝 × 速率。**不乘 scale**——
	// 旧形态没有"暂停比例"概念（暂停在旧形态由 timeSpeed==0 表达），
	// 保持这一层完全等价是 A2/DYN/S3 回归不动的关键。
	m_jd = anchorJd + elapsedSeconds * ratePerSecond;
	m_valid = true;
	return m_jd;
}

QString StelClockController::modeName(Mode m)
{
	return m == Mode::HostDriven ? QStringLiteral("HostDriven") : QStringLiteral("EngineWallClock");
}

// ── 纯逻辑自检 ───────────────────────────────────────────────────────────────
// 无 GL / 无引擎 / 无事件循环：可在窗口创建前、甚至在独立工程形态下运行。
// 判据覆盖 T16 的每一条语义承诺，全部是"能失败"的断言（不是打印式检查）。
bool StelClockController::selfTest(QStringList *log)
{
	const auto put = [log](const QString &line) {
		if (log)
			log->append(line);
	};
	bool pass = true;
	const auto check = [&pass, &put](const char *id, const QString &desc, bool ok) {
		if (!ok)
			pass = false;
		put(QStringLiteral("%1 %2：%3")
		        .arg(QString::fromLatin1(id), desc,
		             ok ? QStringLiteral("OK") : QStringLiteral("FAIL")));
	};
	const auto eq = [](double a, double b) { return qAbs(a - b) < 1e-12; };

	// ST-01 默认态
	{
		StelClockController c;
		check("ST-01", "默认态（EngineWallClock / 未锚定 / scale=1）",
		      c.mode() == Mode::EngineWallClock && !c.valid() && eq(c.scale(), 1.0));
	}

	// ST-02 起步锚定
	{
		StelClockController c;
		c.reset(2451545.0);
		check("ST-02", "reset 锚定起点", c.valid() && eq(c.jd(), 2451545.0));
	}

	// ST-03 宿主推进
	{
		StelClockController c;
		c.reset(1000.0);
		const double jd = c.advanceHostDriven(1.0, 0.5);
		check("ST-03", "宿主推进 dt=1s × rate=0.5 → +0.5", eq(jd, 1000.5) && eq(c.jd(), 1000.5));
	}

	// ST-04 推进量只由传入 dt 决定（无隐含墙钟/次数耦合）
	{
		StelClockController c;
		c.reset(1000.0);
		c.advanceHostDriven(0.25, 1.0);
		c.advanceHostDriven(0.25, 1.0);
		c.advanceHostDriven(0.25, 1.0);
		check("ST-04", "三次 0.25s × rate=1 → 恰好 +0.75（只与 dt 有关）",
		      eq(c.jd(), 1000.75));
	}

	// ST-05 暂停冻结
	{
		StelClockController c;
		c.reset(1000.0);
		c.advanceHostDriven(0.1, 1.0);
		c.setScale(0.0);
		const double before = c.jd();
		for (int i = 0; i < 10; ++i)
			c.advanceHostDriven(0.1, 1.0);
		check("ST-05", "scale=0 连续推进 1.0s 墙钟 → JD 零漂移", eq(c.jd(), before));
	}

	// ST-06 恢复不补时间、不掉速
	{
		StelClockController c;
		c.reset(1000.0);
		c.setScale(0.0);
		c.advanceHostDriven(5.0, 0.5);   // 暂停期间 5s 墙钟（应完全丢弃）
		c.setScale(1.0);
		const double jd = c.advanceHostDriven(0.2, 0.5);
		check("ST-06", "暂停 5s 后恢复：只推进 0.2×0.5=0.1（不补暂停期）",
		      eq(c.jd(), 1000.1) && eq(jd, 1000.1));
	}

	// ST-07 外部跳转重锚（插件/脚本/GUI 写时钟不被拽回的关键）
	{
		StelClockController c;
		c.reset(1000.0);
		c.advanceHostDriven(1.0, 1.0);    // → 1001
		c.jumpTo(2000.0);                 // 外部跳转
		const bool jumped = eq(c.jd(), 2000.0);
		c.advanceHostDriven(1.0, 1.0);    // 从跳转值继续
		check("ST-07", "jumpTo 绝对写入后推进从新值起算（不被拽回）",
		      jumped && eq(c.jd(), 2001.0));
	}

	// ST-08 负 dt 不由推进表达
	{
		StelClockController c;
		c.reset(1000.0);
		const double jd = c.advanceHostDriven(-3.0, 1.0);
		check("ST-08", "负墙钟差被忽略（倒退只能由 jumpTo/负速率表达）",
		      eq(jd, 1000.0));
	}

	// ST-09 旧形态墙钟推进等价性
	{
		StelClockController c;
		c.setMode(Mode::EngineWallClock);
		const double jd = c.advanceEngineWallClock(2451545.0, 2.0, 0.5);
		check("ST-09", "EngineWallClock：锚点+2s×0.5 = 锚点+1",
		      eq(jd, 2451546.0));
	}

	// ST-10 负速率倒放支持（dt 仍须为正）
	{
		StelClockController c;
		c.reset(1000.0);
		const double jd = c.advanceHostDriven(1.0, -0.5);
		check("ST-10", "负速率倒放：dt=1s × rate=-0.5 → -0.5", eq(jd, 999.5));
	}

	// ST-11 scale 负值钳为 0
	{
		StelClockController c;
		c.setScale(-2.0);
		check("ST-11", "负 scale 钳为 0（不允许经由 scale 表达倒放）", eq(c.scale(), 0.0));
	}

	// ST-12 切线不改真源
	{
		StelClockController c;
		c.reset(1000.0);
		c.advanceHostDriven(1.0, 1.0);
		c.setMode(Mode::HostDriven);
		c.setMode(Mode::EngineWallClock);
		check("ST-12", "模式切换不动 JD（真源不被切线重置）", eq(c.jd(), 1001.0));
	}

	return pass;
}
