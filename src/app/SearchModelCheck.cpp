/*
 * SearchModelCheck — 实现（T17）。
 *
 * 判据定义、阶段划分与环境缺 fixture 的处置纪律见 SearchModelCheck.hpp 头注。
 * 本文件不含任何引擎头——它只经 AppFacade 的两个模型间接触达引擎，
 * 这样"模型逻辑是否正确"不会因为引擎细节变化而失效。
 */
#include "app/SearchModelCheck.hpp"

#include "app/AppFacade.hpp"
#include "app/ObjectInfoModel.hpp"
#include "app/SearchResultsModel.hpp"

#include <QCoreApplication>
#include <QDebug>
#include <QTimer>
#include <QVector>

#include <algorithm>
#include <memory>

namespace stelapp {
namespace {

//! 阶段 B 的候选 fixture：只要星表载入就能命中（**不假设** SolarSystem 插件已加载）。
const char *const kFixtureCandidates[] = {
    "Sirius", "Polaris", "Vega", "Betelgeuse", "Rigel", "Mars", "Jupiter", "Moon", "Sun"
};

//! 必然无匹配的查询串（U-SRC-02 用）。
const char *const kNonsenseQuery = "zzz_no_such_object_zzz";

struct Ctx
{
    QCoreApplication *app = nullptr;
    AppFacade *facade = nullptr;
    std::function<void(const SearchModelCheck::Result &)> onDone;
    int retryMs = 1500;
    bool phaseADone = false;
    bool retried = false;
    int passed = 0;
    int total = 0;
    int phaseAPassed = 0;
    int phaseATotal = 0;
    QStringList details;
    QString fixture;
    QString fixtureEnglish;

    void check(bool ok, const QString &label)
    {
        ++total;
        if (ok)
            ++passed;
        details << QStringLiteral("%1 %2")
                       .arg(ok ? QStringLiteral("PASS") : QStringLiteral("FAIL"), label);
    }
};

//! 过期请求用的"陈旧行"：名字刻意醒目——一旦它出现在模型里，判据必然抓到。
SearchResultsModel::Row staleRow()
{
    SearchResultsModel::Row r;
    r.name        = QStringLiteral("__STALE_ROW__");
    r.englishName = QStringLiteral("__STALE_ROW__");
    r.objectType  = QStringLiteral("__stale__");
    r.typeName    = QStringLiteral("__Stale__");
    r.stableId    = QStringLiteral("__Stale__:__stale__");
    return r;
}

void runPhaseA(const std::shared_ptr<Ctx> &ctx)
{
    SearchResultsModel *m = ctx->facade->searchResults();
    ObjectInfoModel *info = ctx->facade->objectInfo();

    // ── SRC-01：过期结果被丢弃（U-SRC-01）────────────────────────────────
    {
        const quint64 d0 = m->discardedRequests();
        const quint32 reqA = m->search(QStringLiteral("Sirius"), 10);
        const quint32 reqB = m->search(QStringLiteral("Polaris"), 10);
        const int rowsB = m->count();

        ctx->check(reqB == reqA + 1,
                   QStringLiteral("SRC-01a 请求编号单调递增（%1 → %2）").arg(reqA).arg(reqB));
        ctx->check(m->currentRequestId() == reqB,
                   QStringLiteral("SRC-01b currentRequestId = 最后一次搜索（%1）").arg(m->currentRequestId()));

        const QVector<SearchResultsModel::Row> staleRows{ staleRow() };
        const bool staleAccepted = m->applyResults(reqA, staleRows, QString());   // 旧请求"后到达"
        const bool zeroAccepted  = m->applyResults(0, staleRows, QString());      // 无效编号

        bool noStaleRow = true;
        for (int i = 0; i < m->rowCount(); ++i) {
            if (m->nameAt(i) == QStringLiteral("__STALE_ROW__")) {
                noStaleRow = false;
                break;
            }
        }
        const quint64 d1 = m->discardedRequests();

        ctx->check(!staleAccepted,
                   QStringLiteral("SRC-01c 过期请求结果被拒收（applyResults 返回 false）"));
        ctx->check(m->count() == rowsB,
                   QStringLiteral("SRC-01d 过期结果未覆盖新查询（行数仍 %1）").arg(m->count()));
        ctx->check(noStaleRow,
                   QStringLiteral("SRC-01e 陈旧行未进入模型（__STALE_ROW__ 不可见）"));
        ctx->check(d1 == d0 + 2,
                   QStringLiteral("SRC-01f 丢弃计数 +2（过期 1 + 无效编号 1），实得 %1").arg(d1 - d0));
        ctx->check(!zeroAccepted,
                   QStringLiteral("SRC-01g 编号 0 视为无效请求，被拒收"));
    }

    // ── SRC-02：空结果安全（U-SRC-02）────────────────────────────────────
    {
        const int n1 = ctx->facade->searchObjects(QString::fromLatin1(kNonsenseQuery));
        ctx->check(n1 == 0 && m->count() == 0 && !m->emptyReason().isEmpty(),
                   QStringLiteral("SRC-02a 无匹配查询 → 0 行 + 非空空态理由（「%1」）")
                       .arg(m->emptyReason()));

        const int n2 = ctx->facade->searchObjects(QString());
        ctx->check(n2 == 0 && m->count() == 0 && !m->emptyReason().isEmpty(),
                   QStringLiteral("SRC-02b 空查询 → 0 行 + 明确理由（「%1」），不崩不挂")
                       .arg(m->emptyReason()));
    }

    // ── SRC-03：失效访问返回安全空值（U-SRC-03）──────────────────────────
    {
        info->clear();
        ctx->check(!info->hasSelection() && info->displayName().isEmpty()
                       && info->stableId().isEmpty() && info->infoText().isEmpty()
                       && info->infoMap().isEmpty(),
                   QStringLiteral("SRC-03a 清空后各字段归零（hasSelection=false + 全空）"));

        const QString bad[] = {
            QString(),                                    // 空串
            QStringLiteral("bad-no-separator"),           // 无分隔符
            QStringLiteral(":noType"),                    // 类型段为空
            QStringLiteral("Star:"),                      // ID 段为空
            QStringLiteral("NoSuchType:no-such-id"),      // 类型不存在（对象已卸载的等价情形）
        };
        bool allSafe = true;
        QString firstBad;
        for (const QString &b : bad) {
            const bool ok = (info->selectByStableId(b) == false)
                            && !info->hasSelection()
                            && info->displayName().isEmpty()
                            && info->infoText().isEmpty();
            if (!ok) {
                allSafe = false;
                firstBad = b;
                break;
            }
        }
        ctx->check(allSafe,
                   allSafe
                       ? QStringLiteral("SRC-03b 5 种非法/失效标识均返回 false 且字段全空（未留陈旧值）")
                       : QStringLiteral("SRC-03b 非法标识「%1」未安全归零").arg(firstBad));
    }

    ctx->phaseAPassed = ctx->passed;
    ctx->phaseATotal = ctx->total;
}

//! 探测一个可检索天体；成功则记入 ctx。
bool findFixture(const std::shared_ptr<Ctx> &ctx)
{
    SearchResultsModel *m = ctx->facade->searchResults();
    for (const char *candidate : kFixtureCandidates) {
        const QString q = QString::fromLatin1(candidate);
        if (ctx->facade->searchObjects(q, 10) <= 0)
            continue;
        const QString english = m->englishNameAt(0);
        const QString sid = m->stableIdAt(0);
        if (!english.isEmpty() && !sid.isEmpty() && sid.contains(QLatin1Char(':'))) {
            ctx->fixture = q;
            ctx->fixtureEnglish = english;
            return true;
        }
    }
    return false;
}

void runPhaseB(const std::shared_ptr<Ctx> &ctx)
{
    SearchResultsModel *m = ctx->facade->searchResults();
    ObjectInfoModel *info = ctx->facade->objectInfo();

    ctx->details << QStringLiteral("INFO fixture=%1 | englishName=%2 | stableId=%3")
                        .arg(ctx->fixture, ctx->fixtureEnglish, m->stableIdAt(0));

    // ── SRC-04a/b：标识的稳定性与形状（U-SRC-04）─────────────────────────
    const QString sid1 = m->stableIdAt(0);
    ctx->facade->searchObjects(QString::fromLatin1(kNonsenseQuery));   // 中间插一次别的查询
    ctx->facade->searchObjects(ctx->fixture, 10);
    const QString sid2 = m->stableIdAt(0);

    const int sep = sid1.indexOf(QLatin1Char(':'));
    ctx->check(!sid1.isEmpty() && sid1 == sid2,
               QStringLiteral("SRC-04a 同一查询跨次得到的标识完全一致（%1）").arg(sid1));
    ctx->check(sep > 0 && sep < sid1.size() - 1 && m->typeNameAt(0) == sid1.left(sep),
               QStringLiteral("SRC-04b 标识形如 type:id 且类型段 = getType()（%1 | %2）")
                   .arg(sid1, m->typeNameAt(0)));

    // ── SRC-05：总量上限（引擎的 maxNbItem 是"每模块"上限，本层统一为总量）──
    {
        // ① 主动构造超限：用 fixture 的**首字符**作前缀（如 "Sirius" → "S"），
        //    宽前缀会命中多个模块的多条记录，尽量让"原始匹配 > 请求上限"真的发生。
        const QString broad = ctx->fixture.left(1);
        const int probeCap = 3;
        const int probeRows = ctx->facade->searchObjects(broad, probeCap);
        const int probeRaw = m->lastRawMatchCount();
        const bool truncated = probeRaw > probeCap;

        ctx->check(probeRows <= probeCap,
                   QStringLiteral("SRC-05a 总量上限生效：查询「%1」请求 %2 条 → 实得 %3 条"
                                  "（引擎原始匹配 %4 条%5）")
                       .arg(broad)
                       .arg(probeCap)
                       .arg(probeRows)
                       .arg(probeRaw)
                       .arg(truncated ? QStringLiteral("，**已触发截断**")
                                      : QStringLiteral("，本环境未构造出超限场景")));
        ctx->check(probeRaw >= probeRows,
                   QStringLiteral("SRC-05b 原始匹配数 ≥ 截断后行数（raw=%1 ≥ %2）")
                       .arg(probeRaw).arg(probeRows));

        // ② 未超限时行数应恰等于原始匹配数（即上限不是"顺手砍了一刀"）。
        const int exactRows = ctx->facade->searchObjects(ctx->fixture, 10);
        const int exactRaw = m->lastRawMatchCount();
        ctx->check(exactRows == std::min(exactRaw, 10),
                   QStringLiteral("SRC-05c 未超限时行数 = min(原始, 上限)（%1 = min(%2, 10)）")
                       .arg(exactRows).arg(exactRaw));
    }

    // ── 纵向 V1/V2：搜索 → 选择 ─────────────────────────────────────────
    const bool selected = ctx->facade->selectSearchResult(0);
    ctx->check(selected && info->hasSelection() && info->englishName() == ctx->fixtureEnglish,
               QStringLiteral("T17-V1 选中第 0 行 → 信息模型英语名一致（%1）").arg(info->englishName()));

    ctx->facade->searchObjects(QString::fromLatin1(kNonsenseQuery));   // 搜索不该动选中
    ctx->check(info->hasSelection() && info->englishName() == ctx->fixtureEnglish,
               QStringLiteral("T17-V2 再次搜索不影响既有选中（选中与搜索解耦）"));

    // ── V3 + 行越界 ─────────────────────────────────────────────────────
    ctx->facade->clearSelection();
    ctx->check(!info->hasSelection() && info->displayName().isEmpty(),
               QStringLiteral("T17-V3 取消选中 → 信息页归零（安全空值）"));
    ctx->check(ctx->facade->selectSearchResult(9999) == false && !info->hasSelection(),
               QStringLiteral("T17-V9 行越界 → 返回 false 且信息页保持归零，不崩"));

    // ── SRC-04c：跨查询回查（U-SRC-04 的"定位回查"）──────────────────────
    const bool resolved = ctx->facade->selectByStableId(sid1);
    ctx->check(resolved && info->hasSelection() && info->englishName() == ctx->fixtureEnglish,
               QStringLiteral("SRC-04c 按 stableId 跨查询回查，重新选中同一天体（%1）")
                   .arg(info->englishName()));

    // ── 纵向 V4..V8：信息页可用（T17 通过条件）──────────────────────────
    ctx->check(!info->displayName().isEmpty(),
               QStringLiteral("T17-V4 displayName 非空（%1）").arg(info->displayName()));
    ctx->check(!info->typeName().isEmpty(),
               QStringLiteral("T17-V5 typeName 非空（%1）").arg(info->typeName()));
    ctx->check(!info->infoText().isEmpty(),
               QStringLiteral("T17-V6 infoText 非空（%1 字符）").arg(int(info->infoText().size())));
    ctx->check(!info->infoMap().isEmpty(),
               QStringLiteral("T17-V7 infoMap 非空（%1 项）").arg(int(info->infoMap().size())));
    ctx->check(info->stableId().contains(QLatin1Char(':')),
               QStringLiteral("T17-V8 stableId 形如 type:id（%1）").arg(info->stableId()));
}

void finish(const std::shared_ptr<Ctx> &ctx, bool fixtureMissing)
{
    const int phaseAFailed = ctx->phaseATotal - ctx->phaseAPassed;

    SearchModelCheck::Result r;
    r.ran = true;
    r.passed = ctx->passed;
    r.total = ctx->total;
    r.details = ctx->details;
    // 阶段 A 有任何失败 → 就是 FAIL（不许被"环境缺 fixture"掩盖）。
    // 阶段 A 全绿但阶段 B 无 fixture → UNAVAILABLE（环境条件，照实说明）。
    r.unavailable = fixtureMissing && phaseAFailed == 0;
    r.pass = phaseAFailed == 0 && !fixtureMissing && ctx->passed == ctx->total;

    if (phaseAFailed > 0) {
        r.summary = QStringLiteral("阶段 A 有 %1 项失败 → FAIL（%2/%3 PASS）")
                        .arg(phaseAFailed).arg(ctx->passed).arg(ctx->total);
    } else if (fixtureMissing) {
        r.summary = QStringLiteral("阶段 A %1/%1 PASS；阶段 B 无 fixture"
                                   "（环境中检出 0 个可检索天体）→ UNAVAILABLE")
                        .arg(ctx->phaseATotal);
    } else {
        r.summary = QStringLiteral("%1/%2 PASS（含纵向判据 V1–V9）")
                        .arg(ctx->passed).arg(ctx->total);
    }
    if (ctx->onDone)
        ctx->onDone(r);
}

void execute(const std::shared_ptr<Ctx> &ctx)
{
    if (!ctx->phaseADone) {
        runPhaseA(ctx);
        ctx->phaseADone = true;
    }

    if (findFixture(ctx)) {
        runPhaseB(ctx);
        finish(ctx, false);
        return;
    }

    if (!ctx->retried) {
        // 引擎数据是渐进载入的（星表可能还没进内存）→ 再等一轮，
        // 避免把"载入中"误报成"环境没有天体"。
        ctx->retried = true;
        ctx->details << QStringLiteral("INFO 首次探测无 fixture，%1ms 后重试一次").arg(ctx->retryMs);
        QTimer::singleShot(ctx->retryMs, ctx->app, [ctx]() { execute(ctx); });
        return;
    }
    finish(ctx, true);
}

} // namespace

void SearchModelCheck::run(QCoreApplication *app,
                           AppFacade *facade,
                           const std::function<void(const Result &)> &onDone,
                           int delayMs,
                           int retryMs)
{
    auto ctx = std::make_shared<Ctx>();
    ctx->app = app;
    ctx->facade = facade;
    ctx->onDone = onDone;
    ctx->retryMs = retryMs;

    QTimer::singleShot(delayMs, app, [ctx]() { execute(ctx); });
}

} // namespace stelapp
