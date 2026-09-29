/*
 * LocationProbe — 实现（T33-A 地点数据面探针）。设计与四个问题见头注。
 *
 * 驱动方式沿用 LocateCheck 的**线性步骤表**：每步 = (跑完后等多少 ms, 步骤体)。
 * 刻意不做成"步骤体自己再挂定时器"—— 那会与驱动器并存、变成两条路都在推进。
 */
#include "app/LocationProbe.hpp"

#include "app/AppFacade.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QTimer>
#include <QVector>

#if defined(STELQUICK_HAS_ENGINE)
#include "StelApp.hpp"
#include "StelCore.hpp"
#include "StelFileMgr.hpp"
#include "StelLocation.hpp"
#include "StelLocationMgr.hpp"
#endif

namespace stelapp {

namespace {

using ProbeResult = LocationProbe::Result;

#if defined(STELQUICK_HAS_ENGINE)
//! 一行描述一个地点。写前/写后各打一行，便于脚本与肉眼逐项对照。
//! `getLatitude(true)` 的 `true` = **抑制观察者伪行星特例**（在 "o" 型
//! 观察者地点上，getter 会返回北极点）—— 探针要的是真实数字。
QString describeLoc(const StelLocation &l)
{
    return QStringLiteral("name=\"%1\" planet=\"%2\" lat=%3 lon=%4 alt=%5 iana=\"%6\" role='%7'")
        .arg(l.name)
        .arg(l.planetName)
        .arg(QString::number(double(l.getLatitude(true)), 'f', 4))
        .arg(QString::number(double(l.getLongitude(true)), 'f', 4))
        .arg(l.altitude)
        .arg(l.ianaTimeZone)
        .arg(QString(l.role));
}

//! 造一个 ad-hoc 地球地点（探针用来问"引擎认不认这个边界值"）。
StelLocation adHocEarth(const QString &name, float lon, float lat)
{
    return StelLocation(name, QString(), QString(), QStringLiteral("Earth"),
                        lon, lat, 0, 0, QStringLiteral("UTC"), 1, QChar('X'));
}

struct Ctx
{
    QCoreApplication *app = nullptr;
    AppFacade *facade = nullptr;
    std::function<void(const ProbeResult &)> onDone;
    ProbeResult result;
    int nextDelay = 0;

    QStringList lines;

    StelLocation before, target, after;
    QString tzBefore, tzAfter;
    double offBefore = 0.0, offAfter = 0.0;
    int dbCount = 0;
    QString targetId;          //!< 目标地点的**完整 ID**（getID() = "name, region"）
    bool targetResolved = false;
    bool targetCrossTz = false; //!< 目标时区是否与写前**不同**（时区腿的判别性前提）

    void note(const QString &line) { lines.append(line); }

    StelCore *core() const
    {
        return StelApp::isInitialized() ? StelApp::getInstance().getCore() : nullptr;
    }
    StelLocationMgr &locMgr() const { return StelApp::getInstance().getLocationMgr(); }

    void finish()
    {
        result.ran = true;
        result.summary = QStringLiteral("地点数据面探针：只报读数、不下结论（断言见 T33-C 的 LocationCheck）");
        result.details = lines;
        onDone(result);
    }
};

//! 驱动步骤：跑完本步后等 delayAfter 毫秒进下一步。
struct Step
{
    int delayAfter = 0;
    std::function<void(Ctx *)> body;
};

#endif  // STELQUICK_HAS_ENGINE

} // namespace

void LocationProbe::run(QCoreApplication *app,
                        AppFacade *facade,
                        const std::function<void(const Result &)> &onDone,
                        int delayMs)
{
#if !defined(STELQUICK_HAS_ENGINE)
    Q_UNUSED(facade)
    Q_UNUSED(delayMs)
    Result r;
    r.unavailable = true;
    r.summary = QStringLiteral("地点探针需要合流形态构建（STELQUICK_HAS_ENGINE 未定义）");
    QTimer::singleShot(0, app, [onDone, r]() { onDone(r); });
#else
    auto *ctx = new Ctx;
    ctx->app = app;
    ctx->facade = facade;
    ctx->onDone = onDone;

    auto steps = std::make_shared<QVector<Step>>();
    auto idx = std::make_shared<int>(0);

    auto tick = std::make_shared<std::function<void()>>();
    *tick = [ctx, steps, idx, tick]() {
        if (*idx >= steps->size())
        {
            ctx->finish();
            return;
        }
        const Step s = steps->at((*idx)++);
        ctx->nextDelay = s.delayAfter;
        s.body(ctx);
        // 延时在**步骤体跑完之后**才读：步骤体改写 nextDelay 就是为了改这一次的等待。
        QTimer::singleShot(ctx->nextDelay, ctx->app, [tick]() { (*tick)(); });
    };

    // ── 步骤 1：P1 环境 / P2 规模 / P3 查询 / P4 写前快照 ──────────────────────
    steps->append({delayMs, [](Ctx *c) {
        StelCore *core = c->core();
        if (!core)
        {
            c->note(QStringLiteral("引擎不可用（core == nullptr）⇒ 探针无读数"));
            return;
        }
        StelLocationMgr &mgr = c->locMgr();

        // ── P1 环境：数据面**到底依赖什么**是全探针最要紧的一条 ─────────────
        c->note(QStringLiteral("P1 cwd = \"%1\"").arg(QDir::currentPath()));
        c->note(QStringLiteral("P1 STELLARIUM_DATA_ROOT = \"%1\"（未设时引擎用 \".\"）")
                    .arg(qEnvironmentVariable("STELLARIUM_DATA_ROOT")));
        c->note(QStringLiteral("P1 installDir = \"%1\"").arg(StelFileMgr::getInstallationDir()));
#if defined(STELLARIUM_SOURCE_DIR)
        // 实测：cwd=仓库根 ⇒ installDir="."（走 STELLARIUM_DATA_ROOT 默认值）；
        //       cwd=别处 ⇒ installDir=**源码目录**（Resources 与 ../share 都落空，
        //       最后落到这个**编译期宏**）⇒ **数据面不依赖 cwd**，两条路都能加载。
        c->note(QStringLiteral("P1 编译期兜底 STELLARIUM_SOURCE_DIR = \"%1\"")
                    .arg(QStringLiteral(STELLARIUM_SOURCE_DIR)));
#endif
        const QString checkFile = StelFileMgr::findFile(QStringLiteral("data/ssystem_major.ini"));
        c->note(QStringLiteral("P1 findFile(data/ssystem_major.ini) [installDir 的判据文件] = %1")
                    .arg(checkFile.isEmpty() ? QStringLiteral("<未命中>")
                                             : QStringLiteral("\"%1\"").arg(checkFile)));
        const QString binFile = StelFileMgr::findFile(QStringLiteral("data/base_locations.bin.gz"));
        c->note(QStringLiteral("P1 findFile(data/base_locations.bin.gz) = %1")
                    .arg(binFile.isEmpty() ? QStringLiteral("<未命中 —— 地点库加载必为空>")
                                           : QStringLiteral("\"%1\"").arg(binFile)));
        const QString locDir = StelFileMgr::getLocaleDir();
        c->note(QStringLiteral("P1 getLocaleDir() = \"%1\" [T24 血泪面：翻译曾经三候选全不命中]")
                    .arg(locDir));

        // ── P2 规模 ──────────────────────────────────────────────────────────
        const LocationList all = mgr.getAll();
        c->dbCount = int(all.size());
        const QStringList regions = mgr.getRegionNames();
        const QStringList tzNames = mgr.getAllTimezoneNames();
        c->note(QStringLiteral("P2 地点库规模：地点 %1 条 / 区域 %2 个 / 时区名 %3 个")
                    .arg(c->dbCount)
                    .arg(regions.size())
                    .arg(tzNames.size()));
        const int sample = qMin(3, c->dbCount);
        for (int i = 0; i < sample; ++i)
            c->note(QStringLiteral("P2 抽样[%1] %2").arg(i).arg(describeLoc(all.at(i))));

        // 时区名单里有没有 IANA 名（决定"换地点时区能不能落"）
        int ianaCount = 0;
        for (const QString &z : tzNames)
            if (z.contains(QLatin1Char('/')))
                ++ianaCount;
        c->note(QStringLiteral("P2 时区名中含 \"/\" 的 IANA 形态：%1 个").arg(ianaCount));

        // ── P3 查询 ──────────────────────────────────────────────────────────
        // ⚠️ 口径警告：`locationForString` 对**认不出**的输入**不会报错**，而是走
        //    坐标解析兜底、最后返回一个 `role='!'` 的无效地点（StelLocationMgr.cpp:784）。
        //    所以"查到了"要看 `role`，不能只看有没有返回。
        const char *const queries[] = {"Beijing", "Paris, Western Europe", "北京"};
        for (const char *q : queries)
        {
            const QString qs = QString::fromUtf8(q);
            const StelLocation l = mgr.locationForString(qs);
            c->note(QStringLiteral("P3 locationForString(\"%1\") ⇒ %2  [role='%3' ⇒ %4]")
                        .arg(qs)
                        .arg(describeLoc(l))
                        .arg(QString(l.role))
                        .arg(l.role == QChar('!') ? QStringLiteral("**无效（认不出）**")
                                                  : QStringLiteral("有效")));
        }

        // ── P3b：反查"真正的 key 形态" ───────────────────────────────────────
        // `StelLocation::getID()` = `"name, region"`（region 非空时）⇒ 只用城市单名
        // **多半不命中**。这条把库里的真实 key 打出来，判据阶段要用它。
        for (const StelLocation &l : all)
        {
            if (l.name.compare(QStringLiteral("Beijing"), Qt::CaseInsensitive) == 0)
            {
                c->targetId = l.getID();
                c->note(QStringLiteral("P3b 库内 Beijing 条目 %1").arg(describeLoc(l)));
                c->note(QStringLiteral("P3b 其 getID()=\"%1\" ⇒ 用完整 ID 再查一次").arg(c->targetId));
                const StelLocation byId = mgr.locationForString(c->targetId);
                c->note(QStringLiteral("P3b locationForString(完整 ID) ⇒ %1  [role='%2']")
                            .arg(describeLoc(byId))
                            .arg(QString(byId.role)));
                break;
            }
        }
        if (c->targetId.isEmpty())
            c->note(QStringLiteral("P3b 库内没有 name==\"Beijing\" 的条目（地点库可能是空集）"));

        // ── P4 写前快照 + 挑目标 ────────────────────────────────────────────
        c->before = core->getCurrentLocation();
        c->tzBefore = core->getCurrentTimeZone();
        c->offBefore = core->getUTCOffset(core->getJD());
        c->note(QStringLiteral("P4 写前 %1").arg(describeLoc(c->before)));
        c->note(QStringLiteral("P4 写前 timeZone=\"%1\" UTCOffset=%2 h")
                    .arg(c->tzBefore)
                    .arg(QString::number(c->offBefore, 'f', 4)));

        // 目标：**必须跨时区**，否则"时区联动"那条腿是**假绿**的 ——
        // 写前地点（默认是引擎配的 "Mianyang (Sichuan)"，iana=Asia/Shanghai）与
        // Beijing 同属 Asia/Shanghai ⇒ 即使联动整条断掉，写后时区也照样是
        // Asia/Shanghai，判据读不出来。Paris（Europe/Paris，+2h vs +8h）才是判别性的。
        // 「孤立断言可假绿」在选址上的具体形态 —— 见 TRAPS 第 4 条。
        StelLocation t = mgr.locationForString(QStringLiteral("Paris, Western Europe"));
        c->targetCrossTz = true;
        if (!t.isValid())
        {
            // 兜底：从库里挑第一个与写前时区**不同**的地点（保持判别性，不退回同时区）。
            for (const StelLocation &l : all)
            {
                if (!l.ianaTimeZone.isEmpty() && l.ianaTimeZone != c->before.ianaTimeZone)
                {
                    t = l;
                    break;
                }
            }
            c->note(QStringLiteral("P4 Paris 未取到 ⇒ 改从库里挑第一个**跨时区**地点"));
        }
        if (!t.isValid())
        {
            t = mgr.getLastResortLocation();
            c->targetCrossTz = false;
            c->note(QStringLiteral("P4 跨时区目标也没取到 ⇒ 回落到 lastResort（**时区腿将失去判别性**）"));
        }
        c->target = t;
        c->targetResolved = true;
        c->note(QStringLiteral("P4 目标 %1").arg(describeLoc(c->target)));
        c->note(QStringLiteral("P4 判别性前提：目标 iana=\"%1\" vs 写前 iana=\"%2\" ⇒ %3")
                    .arg(c->target.ianaTimeZone, c->before.ianaTimeZone,
                         c->targetCrossTz ? QStringLiteral("**不同**（判据有判别力）")
                                          : QStringLiteral("**相同**（时区腿无判别力，假绿风险）")));
    }});

    // ── 步骤 2：P5 瞬时写入（duration=0 走 StelObserver，不走飞行动画）────────
    steps->append({0, [](Ctx *c) {
        StelCore *core = c->core();
        if (!core)
            return;
        core->moveObserverTo(c->target, 0.0);
        c->note(QStringLiteral("P5 moveObserverTo(target, 0.0) 已调用（瞬时分支）"));
    }});

    // ── 步骤 3：P6 回读 + 时区联动（等 800ms 让帧跑一拍）─────────────────────
    steps->append({800, [](Ctx *c) {
        StelCore *core = c->core();
        if (!core)
            return;
        c->after = core->getCurrentLocation();
        c->tzAfter = core->getCurrentTimeZone();
        c->offAfter = core->getUTCOffset(core->getJD());
        c->note(QStringLiteral("P6 写后 %1").arg(describeLoc(c->after)));
        c->note(QStringLiteral("P6 写后 timeZone=\"%1\" UTCOffset=%2 h")
                    .arg(c->tzAfter)
                    .arg(QString::number(c->offAfter, 'f', 4)));

        const double dLat = double(c->after.getLatitude(true)) - double(c->before.getLatitude(true));
        const double dLon = double(c->after.getLongitude(true)) - double(c->before.getLongitude(true));
        c->note(QStringLiteral("P6 坐标位移：Δlat=%1° Δlon=%2°")
                    .arg(QString::number(dLat, 'f', 4))
                    .arg(QString::number(dLon, 'f', 4)));
        c->note(QStringLiteral("P6 UTCOffset 位移：Δ=%1 h")
                    .arg(QString::number(c->offAfter - c->offBefore, 'f', 4)));

        // 时区**联动**：`setObserver` 里 setCurrentTimeZone(iana)（StelCore.cpp:1546）。
        // 目标 iana 为空时引擎**刻意跳过**（!isEmpty() 条件）——这也是读数的一部分。
        const QString wantTz = c->target.ianaTimeZone;
        QString verdict;
        if (wantTz.isEmpty())
            verdict = QStringLiteral("目标 iana 为空 ⇒ 引擎条件里就跳过（不是缺陷）");
        else if (c->tzAfter == wantTz)
            verdict = QStringLiteral("已落");
        else
            verdict = QStringLiteral("**未落** ⇒ 撞上 setCurrentTimeZone 的静默失败"
                                     "（名单外只 qWarning、不设置，StelCore.cpp:1715）");
        c->note(QStringLiteral("P6 时区联动：目标 iana=\"%1\" ⇒ 引擎当前=\"%2\"（%3）")
                    .arg(wantTz, c->tzAfter, verdict));
        c->note(QStringLiteral("P6 时区名单里是否含目标 iana：%1")
                    .arg(c->locMgr().getAllTimezoneNames().contains(wantTz)
                             ? QStringLiteral("是（能落）")
                             : QStringLiteral("**否**（落不进去）")));
        // 前提腿（血泪第 4 条）：没有它，"已落"可能是"写前写后本来就一样"。
        if (!c->targetCrossTz)
            c->note(QStringLiteral("P6 ⚠️ 判别性前提不成立（目标与写前同时区）⇒ "
                                   "本行的\"已落\"**不构成证据**"));
        else if (c->tzAfter == c->before.ianaTimeZone)
            c->note(QStringLiteral("P6 判别腿：写后时区 **仍等于写前的 \"%1\"** ⇒ "
                                   "联动整条断掉")
                        .arg(c->before.ianaTimeZone));
        else
            c->note(QStringLiteral("P6 判别腿：写后时区已从 \"%1\" 变为 \"%2\" ⇒ 联动确实发生")
                        .arg(c->before.ianaTimeZone, c->tzAfter));
    }});

    // ── 步骤 4：P7 引擎侧边界（isValid 到底校不校纬度范围）──────────────────
    steps->append({0, [](Ctx *c) {
        auto flag = [](bool b) { return b ? QStringLiteral("true") : QStringLiteral("false"); };
        const StelLocation lat91 = adHocEarth(QStringLiteral("Lat91"), 0.f, 91.f);
        const StelLocation lon200 = adHocEarth(QStringLiteral("Lon200"), 200.f, 0.f);
        const StelLocation good = adHocEarth(QStringLiteral("Good"), 116.4f, 39.9f);
        const StelLocation atZero = adHocEarth(QStringLiteral("AtZero"), 0.f, 0.f);
        const StelLocation invalid = StelLocation(QStringLiteral("Inv"), QString(), QString(),
                                                  QStringLiteral("Earth"), 10.f, 10.f, 0, 0,
                                                  QStringLiteral("UTC"), 1, QChar('!'));
        c->note(QStringLiteral("P7 引擎边界 isValid()：lat=91 ⇒ %1 / lon=200 ⇒ %2 / "
                               "正常(39.9,116.4) ⇒ %3 / (0,0) ⇒ %4 / role='!' ⇒ %5")
                    .arg(flag(lat91.isValid()), flag(lon200.isValid()), flag(good.isValid()),
                         flag(atZero.isValid()), flag(invalid.isValid())));
        c->note(QStringLiteral("P7 判读：引擎**不校验经纬度范围**（StelLocation.cpp:296 只查 "
                               "role=='!' 与\"经纬不同时为 0\"）⇒ \"非法纬度必须被拒\"由应用层负责"));
        c->note(QStringLiteral("P7 注：(0,0) 的返回依赖 planetName 非空 ⇒ %1")
                    .arg(atZero.isValid() ? QStringLiteral("本引擎判为合法") : QStringLiteral("被判非法")));
    }});

    // ── 步骤 5：P8 **AppFacade 地点面**（走公共 API —— 证接线不是死代码）────
    // 血泪第 3 条："仪器没接在实况上"。前面 P4-P6 直接调 `core->moveObserverTo`，
    // 那是引擎原语；**产品实际走的是 AppFacade**。这一段把两条路分开记：
    // 若 P6 绿而 P8 红 ⇒ 断在接线层；若两条都绿 ⇒ 链路通。
    steps->append({0, [](Ctx *c) {
        AppFacade *f = c->facade;
        if (!f)
        {
            c->note(QStringLiteral("P8 facade == nullptr ⇒ 无法验证公共 API"));
            return;
        }
        c->note(QStringLiteral("P8 读：name=\"%1\" id=\"%2\" lat=%3 lon=%4 alt=%5 planet=\"%6\" 地点时区=\"%7\"")
                    .arg(f->locationName(), f->locationId())
                    .arg(QString::number(f->locationLatitude(), 'f', 4),
                         QString::number(f->locationLongitude(), 'f', 4),
                         QString::number(f->locationAltitudeMeters(), 'f', 1))
                    .arg(f->locationPlanet(), f->locationTimeZone()));

        // ── 非法值：必须被拒 ∧ **无副作用**（拒绝不能悄悄改掉当前地点）──
        const QString idBefore = f->locationId();
        const bool badLat = f->setLocationByCoordinates(91.0, 0.0, 0.0);
        const QString tokLat = f->lastLocationRefusal();
        const bool badLon = f->setLocationByCoordinates(0.0, 200.0, 0.0);
        const QString tokLon = f->lastLocationRefusal();
        const bool badAlt = f->setLocationByCoordinates(0.0, 0.0, 1.0e6);
        const QString tokAlt = f->lastLocationRefusal();
        const QString idAfterBad = f->locationId();
        c->note(QStringLiteral("P8 非法值：lat=91 ⇒ %1（%2）/ lon=200 ⇒ %3（%4）/ alt=1e6 ⇒ %5（%6）")
                    .arg(badLat ? QStringLiteral("**被接受（缺陷）**") : QStringLiteral("被拒"),
                         tokLat,
                         badLon ? QStringLiteral("**被接受（缺陷）**") : QStringLiteral("被拒"),
                         tokLon,
                         badAlt ? QStringLiteral("**被接受（缺陷）**") : QStringLiteral("被拒"),
                         tokAlt));
        c->note(QStringLiteral("P8 无副作用：三次拒绝后 id \"%1\" ⇒ \"%2\"（%3）")
                    .arg(idBefore, idAfterBad,
                         idBefore == idAfterBad ? QStringLiteral("纹丝不动")
                                                : QStringLiteral("**被改掉了（缺陷）**")));

        // ── 合法 ad-hoc 坐标写（跨时区：东京 vs 巴黎，UTC+9 vs +2）──
        const bool okAdHoc = f->setLocationByCoordinates(35.68, 139.77, 40.0);
        c->note(QStringLiteral("P8 ad-hoc 写东京(35.68,139.77,40m) ⇒ %1 token=%2 ⇒ 回读 "
                               "name=\"%3\" lat=%4 lon=%5 alt=%6 地点时区=\"%7\" 引擎时区=\"%8\"")
                    .arg(okAdHoc ? QStringLiteral("成功") : QStringLiteral("**失败**"),
                         f->lastLocationRefusal(),
                         f->locationName(),
                         QString::number(f->locationLatitude(), 'f', 4),
                         QString::number(f->locationLongitude(), 'f', 4),
                         QString::number(f->locationAltitudeMeters(), 'f', 1),
                         f->locationTimeZone(), f->timeZoneId()));
        c->note(QStringLiteral("P8 判读：ad-hoc 地点 iana 留空 ⇒ 引擎 setObserver 的 "
                               "!isEmpty() 条件跳过 ⇒ **时区不动**（上面两个时区应相同）"));

        // ── 查库 + 用 ID 写（对外正路）──
        const QStringList hits = f->findLocations(QStringLiteral("Beijing"), 5);
        c->note(QStringLiteral("P8 findLocations(\"Beijing\",5) ⇒ %1 条：%2")
                    .arg(hits.size())
                    .arg(hits.isEmpty() ? QStringLiteral("<空>") : hits.join(QStringLiteral(" | "))));
        if (!hits.isEmpty())
        {
            const bool okById = f->setLocationById(hits.first());
            c->note(QStringLiteral("P8 setLocationById(\"%1\") ⇒ %2 token=%3 ⇒ 回读 name=\"%4\" "
                                   "lat=%5 lon=%6 地点时区=\"%7\" 引擎时区=\"%8\"")
                        .arg(hits.first(),
                             okById ? QStringLiteral("成功") : QStringLiteral("**失败**"),
                             f->lastLocationRefusal(),
                             f->locationName(),
                             QString::number(f->locationLatitude(), 'f', 4),
                             QString::number(f->locationLongitude(), 'f', 4),
                             f->locationTimeZone(), f->timeZoneId()));
        }
        // 不存在的 ID 必须报 not-found（而不是静默成功）
        const bool badId = f->setLocationById(QStringLiteral("这个地点不存在, Nowhere"));
        c->note(QStringLiteral("P8 setLocationById(不存在的 ID) ⇒ %1 token=%2")
                    .arg(badId ? QStringLiteral("**被接受（缺陷）**") : QStringLiteral("被拒"),
                         f->lastLocationRefusal()));

        c->note(QStringLiteral("P8 计数：写入 %1 次 / 拒绝 %2 次（读数：写入应=2、拒绝应=4）")
                    .arg(f->locationWriteCount())
                    .arg(f->locationRefusedCount()));
    }});

    (*tick)();
#endif
}

} // namespace stelapp
