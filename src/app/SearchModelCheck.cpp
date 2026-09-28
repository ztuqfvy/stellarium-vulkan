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
#include "app/SearchRanker.hpp"     // T21：排序分级规则（纯逻辑，可直接穷尽覆盖）
#include "app/SearchResultsModel.hpp"
#include "app/PinyinIndex.hpp"      // T24：拼音检索（纯逻辑 + qrc 数据表）

#include "StelApp.hpp"              // T24：活引擎腿切语言（拼音判据需要 zh 名字）
#include "StelLocaleMgr.hpp"
#include "StelModuleMgr.hpp"        // T24：getAllModules() 枚举天体模块（诊断段）
#include "StelObjectMgr.hpp"        // T26：无截断原语的引擎侧读数（SRC-12 判别对照）

#include <QCoreApplication>
#include <QDebug>
#include <QTimer>
#include <QVector>

#include <algorithm>
#include <memory>
#include <QSet>     // T20：SRC-05d 的结果去重检查

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

    // ── SRC-06..09：相关度排序（T21）────────────────────────────────────────
    // 这一组是**纯逻辑**判据：只用 SearchRanker 与**手造的名字**，不触引擎、不用 fixture，
    // 因此与阶段 A 同一纪律——恒可跑、结果与星表数据无关。
    // 它覆盖"分级规则本身对不对"；"collect() 真的调了它"由 SRC-11（阶段 B 的真实数据
    // 端到端）与一次性的"注掉排序 + 重建"反向对照共同证明——**两者不可互相替代**：
    // 纯逻辑判据证明了规则，但规则再对、没人调用也是白搭。
    {
        // ── SRC-06 匹配分级逐条覆盖 ──────────────────────────────────────
        struct GradeCase
        {
            const char *name;
            const char *query;
            MatchQuality want;
        };
        const GradeCase kCases[] = {
            { "Moon",                     "Moon", MatchQuality::Exact     },
            { "Moon",                     "moon", MatchQuality::Exact     },  // 大小写无关
            { "Moonlight",                "Moon", MatchQuality::Prefix    },
            { "Pirate Moon Cluster",      "Moon", MatchQuality::WordStart },
            { "Ghost of the Moon Nebula", "Moon", MatchQuality::WordStart },
            { "Honeymoon",                "Moon", MatchQuality::Substring },  // 前邻是字母 y ⇒ 不是词首
            { "Sirius",                   "Moon", MatchQuality::None      },
        };
        const int kCaseCount = int(sizeof(kCases) / sizeof(kCases[0]));
        bool allGrades = true;
        QString gradeDetail;
        for (const GradeCase &c : kCases) {
            const MatchQuality got = SearchRanker::quality(QString::fromLatin1(c.name),
                                                          QString::fromLatin1(c.query));
            if (got != c.want) {
                allGrades = false;
                gradeDetail = QStringLiteral("「%1」查「%2」得 %3，期望 %4")
                                  .arg(QString::fromLatin1(c.name),
                                       QString::fromLatin1(c.query),
                                       SearchRanker::qualityName(got),
                                       SearchRanker::qualityName(c.want));
                break;
            }
        }
        ctx->check(allGrades,
                   allGrades
                       ? QStringLiteral("SRC-06 匹配分级逐条覆盖 %1 例全对"
                                        "（完全/前缀/词首/子串/不匹配，且大小写无关）").arg(kCaseCount)
                       : QStringLiteral("SRC-06 匹配分级错了一项：%1").arg(gradeDetail));

        // ── SRC-07 T20 实测场景复现 ──────────────────────────────────────
        // 名字与类型**原样照抄** T20 回放证据 `replaycheck-mac.txt` 的三条 RP-note，
        // 这样"排序到底修好了什么"能直接拿 T20 的原始观测对照，不需要二次解释。
        //   [0] "Ghost of the Moon Nebula" / Nebula:NGC 6781
        //   [1] "Moon"                     / Planet:Moon
        //   [2] "Pirate Moon Cluster"      / Nebula:NGC 1647
        QVector<SearchResultsModel::Row> demoRaw;
        auto addRow = [&demoRaw](const char *name, const char *typeName) {
            SearchResultsModel::Row r;
            r.name     = QString::fromLatin1(name);
            r.typeName = QString::fromLatin1(typeName);
            demoRaw.append(r);
        };
        addRow("Ghost of the Moon Nebula", "Nebula");
        addRow("Moon",                     "Planet");
        addRow("Pirate Moon Cluster",      "Nebula");

        const QString demoQuery = QStringLiteral("Moon");
        auto sortByRank = [&demoQuery](QVector<SearchResultsModel::Row> v) {
            std::stable_sort(v.begin(), v.end(), [&demoQuery](const auto &a, const auto &b) {
                return SearchRanker::less(SearchRanker::key(a.name, a.typeName, demoQuery),
                                          SearchRanker::key(b.name, b.typeName, demoQuery));
            });
            return v;
        };
        auto joinNames = [](const QVector<SearchResultsModel::Row> &v) {
            QString s;
            for (const auto &r : v)
                s += (s.isEmpty() ? QString() : QStringLiteral(" > ")) + r.name;
            return s;
        };

        const QVector<SearchResultsModel::Row> demo = sortByRank(demoRaw);
        const QString wantOrder = QStringLiteral("Moon > Pirate Moon Cluster > Ghost of the Moon Nebula");
        const QString gotOrder  = joinNames(demo);

        ctx->check(!demo.isEmpty() && demo.at(0).name == QStringLiteral("Moon"),
                   QStringLiteral("SRC-07 实测场景复现：搜「Moon」首行 = Moon"
                                  "（修复前是 Ghost of the Moon Nebula）；完整顺序 [%1]").arg(gotOrder));
        // 07b 考的是"同分之后"的第二、三级比较（类型权重 → 名字长度）也真的生效：
        // 两条 Nebula 同为词首匹配，短的 "Pirate Moon Cluster"(18) 应排在 (25) 之前。
        ctx->check(gotOrder == wantOrder,
                   QStringLiteral("SRC-07b 同分次序 = %1（两条 Nebula 都是词首匹配，"
                                  "再按名字长度定序）").arg(wantOrder));

        // ── SRC-08 稳定可复现（全序）────────────────────────────────────
        QVector<SearchResultsModel::Row> shuffled = demoRaw;
        std::reverse(shuffled.begin(), shuffled.end());     // 打乱输入顺序
        const QVector<SearchResultsModel::Row> resorted = sortByRank(shuffled);
        ctx->check(joinNames(resorted) == gotOrder,
                   QStringLiteral("SRC-08 打乱输入顺序后排序结果不变（全序、可复现）——"
                                  "否则「同一查询搜两次顺序不一样」会成为神出鬼没的 bug"));

        // 08b 比较器自洽：std::sort 拿到非严格弱序是 UB（可能越界写，不只是排错）。
        bool comparatorOk = true;
        for (const auto &a : demoRaw)
            for (const auto &b : demoRaw) {
                const RankKey ka = SearchRanker::key(a.name, a.typeName, demoQuery);
                const RankKey kb = SearchRanker::key(b.name, b.typeName, demoQuery);
                if (SearchRanker::less(ka, ka))
                    comparatorOk = false;                                    // 非自反
                if (SearchRanker::less(ka, kb) && SearchRanker::less(kb, ka))
                    comparatorOk = false;                                    // 不对称
            }
        ctx->check(comparatorOk,
                   QStringLiteral("SRC-08b 比较器自洽（非自反 + 不对称）—— 传非法比较器给 std::sort 是 UB"));

        // ── SRC-09 内建判别性对照：证明这组判据**有检验力** ─────────────
        // 把同一批候选按**纯字典序**排一遍——那正是修复前 `StelObjectMgr::listMatchingObjects`
        // 的排法（StelObjectMgr.cpp:609）。若这样排首行也恰好是 Moon，说明这个场景
        // 两种排法给同样结果，SRC-07 就**测不出任何东西**（假绿）。
        QVector<SearchResultsModel::Row> lex = demoRaw;
        std::stable_sort(lex.begin(), lex.end(),
                         [](const auto &a, const auto &b) { return a.name < b.name; });
        const QString lexFirst = lex.isEmpty() ? QStringLiteral("(空)") : lex.at(0).name;
        ctx->check(lexFirst != QStringLiteral("Moon"),
                   QStringLiteral("SRC-09 内建判别性对照：同批候选按纯字典序排（= 修复前引擎的排法）"
                                  "首行是「%1」而不是 Moon ⇒ 本组判据确实能区分两种排序，SRC-07 有检验力。"
                                  "若这条 FAIL，说明场景选错了，得换数据而不是改判据").arg(lexFirst));
    }

    // ── T24 拼音索引：纯逻辑腿（PhaseA，不需要引擎）────────────────────
    // 规则细节全部可独立穷尽：表加载、读音转换、分档判定、查询形态判定。
    // "collect() 真的会拿拼音候选"由 SRC-11 后面的 PINY-05..07（活引擎腿）证明。
    {
        // PINY-01 表加载（判据自足的前提；失败则后面全部无意义，逐条 FAIL 是对的）
        ctx->check(PinyinIndex::available(),
                   QStringLiteral("PINY-01 拼音表加载成功（%1）")
                       .arg(PinyinIndex::available() ? QStringLiteral(":/StelQuickUI/pinyin-lite.txt")
                                                     : QStringLiteral("资源缺失！")));

        // PINY-02 读音转换：主读音、多汉字拼接、非汉字名空串
        struct PinyinCase { const char *name; const char *full; const char *ini; };
        const PinyinCase convCases[] = {
            { "月球",         "yueqiu",           "yq"     },
            { "太阳",         "taiyang",          "ty"     },
            { "猎户座大星云", "liehuzuodaxingyun","lhzdxy" },
            { "Moon",         "",                 ""       },  // 无汉字 → 空串（调用方短路）
        };
        for (const auto &c : convCases) {
            const QString full = PinyinIndex::toPinyin(QString::fromUtf8(c.name));
            const QString ini  = PinyinIndex::initials(QString::fromUtf8(c.name));
            ctx->check(full == QString::fromUtf8(c.full) && ini == QString::fromUtf8(c.ini),
                       QStringLiteral("PINY-02 读音转换「%1」→ 全拼 %2 / 首字母 %3"
                                      "（期望 %4 / %5）")
                           .arg(QString::fromUtf8(c.name), full, ini,
                                QString::fromUtf8(c.full), QString::fromUtf8(c.ini)));
        }

        // PINY-03 分档判定：全拼连续子串 → PinyinFull；首字母词首 → PinyinInitial；
        // 无关查询 / 非字母查询 / 空查询 → None
        using QM = MatchQuality;
        struct MatchCase { const char *name; const char *query; QM expect; };
        const MatchCase mqCases[] = {
            { "月球", "yueqiu", QM::PinyinFull    },
            { "月球", "yue",    QM::PinyinFull    },   // 前缀也算连续子串
            { "月球", "qiu",    QM::PinyinFull    },   // 中间片段也合法
            { "月球", "yq",     QM::PinyinInitial },
            { "月球", "moon",   QM::None          },   // 读音里没有
            { "月球", "月球",   QM::None          },   // 非 ASCII 查询不走拼音
            { "Moon", "yq",     QM::None          },   // 无汉字名
        };
        for (const auto &c : mqCases) {
            const QM got = PinyinIndex::matchQuality(QString::fromUtf8(c.name),
                                                     QString::fromUtf8(c.query));
            ctx->check(got == c.expect,
                       QStringLiteral("PINY-03 分档「%1」×「%2」→ %3（期望 %4）")
                           .arg(QString::fromUtf8(c.name), QString::fromUtf8(c.query),
                                PinyinIndex::qualityName(got),
                                PinyinIndex::qualityName(c.expect)));
        }

        // PINY-04 查询形态与 CJK 判定（拼音分支的开关条件）
        ctx->check(PinyinIndex::isPinyinQuery(QStringLiteral("yueqiu"))
                       && PinyinIndex::isPinyinQuery(QStringLiteral("YQ"))
                       && !PinyinIndex::isPinyinQuery(QStringLiteral("月球"))
                       && !PinyinIndex::isPinyinQuery(QStringLiteral(""))
                       && !PinyinIndex::isPinyinQuery(QStringLiteral("mo on"))
                       && !PinyinIndex::isPinyinQuery(QStringLiteral("yue-qiu")),
                   QStringLiteral("PINY-04 查询形态判定：纯字母=true（含大写）、"
                                  "中文/空/含空格或连字符=false"));
        ctx->check(PinyinIndex::hasCjk(QStringLiteral("月球"))
                       && !PinyinIndex::hasCjk(QStringLiteral("Moon"))
                       && !PinyinIndex::hasCjk(QString()),
                   QStringLiteral("PINY-05 CJK 判定：汉字名 true、英文名/空串 false"));
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
        // T24 口径修正：查询是纯字母（宽前缀如 "S" 必然是）时，拼音检索会补充
        // "中文名拼音命中"的候选（lastPinyinMatchCount）。截断前候选池 = raw + pinyin，
        // SRC-05b 的"候选 ≥ 行数"不变式因此按新口径比较——拼音候选也是
        // "截断前的候选"，漏掉它这条判据会在拼音功能生效后报假红。
        ctx->check(probeRaw + m->lastPinyinMatchCount() >= probeRows,
                   QStringLiteral("SRC-05b 原始匹配数 + 拼音候选 ≥ 截断后行数"
                                  "（raw=%1 + pinyin=%2 ≥ %3）")
                       .arg(probeRaw).arg(m->lastPinyinMatchCount()).arg(probeRows));

        // ② 未超限时行数应恰等于**去重后**的原始匹配数（即上限不是"顺手砍了一刀"）。
        //
        // ⚠️ T20 修正（2026-09-28）：原判据写的是 `exactRows == min(exactRaw, 10)`，
        //   把引擎的**原始条数**当成了唯一行的条数。而 `StelObjectModule::
        //   listMatchingObjects` 会把翻译名表与英文名表各枚举一遍且**不去重**
        //   —— 同一个天体有几个名字含查询串就出几条（实测 Sirius → 原始 4 条，
        //   全是同一个 Star:HIP 32349 A）。于是这条判据实际上在**为缺陷背书**：
        //   行数=1（去重后的正确值）反而被判红。T20 在模型层按 stableId 去重后
        //   它立刻暴露。现在的口径 = "cap 不截断时与不限量查询给同样行数"，
        //   既不放弃原意（上限不许顺手砍），也不再要求行数等于原始条数。
        const int exactRows = ctx->facade->searchObjects(ctx->fixture, 10);
        const int exactRaw = m->lastRawMatchCount();
        int dupCount = 0;
        {
            QSet<QString> seen;
            for (int r = 0; r < exactRows; ++r) {
                const QString sid = m->stableIdAt(r);
                if (seen.contains(sid))
                    ++dupCount;
                seen.insert(sid);
            }
        }
        // 同一查询、上限放到"不截断"处，行数必须与 cap=10 时一致。
        // T24：候选池 = 引擎原始 + 拼音候选（拼音分支见 SRC-05b 注）。
        const int totalPool = exactRaw + m->lastPinyinMatchCount();
        const int fullRows = ctx->facade->searchObjects(ctx->fixture, std::max(1, totalPool));
        const bool notTruncated = totalPool <= 10;
        ctx->check((!notTruncated || fullRows == exactRows)
                       && exactRows <= std::min(totalPool, 10) && exactRows > 0,
                   QStringLiteral("SRC-05c 未超限时行数 = 去重后条数，上限不再额外截断"
                                  "（cap=10 → %1 行；cap=%2（不截断）→ %3 行；"
                                  "候选池 raw=%4 + pinyin=%5 ⇒ 去重掉 %6 条）")
                       .arg(exactRows).arg(totalPool).arg(fullRows)
                       .arg(exactRaw).arg(m->lastPinyinMatchCount())
                       .arg(totalPool - exactRows));
        ctx->check(dupCount == 0,
                   QStringLiteral("SRC-05d 结果内无重复天体（%1 行 → %2 个不同 stableId）"
                                  "—— T20 补的回归护栏，原 26 条判据全绿也没抓到重复行")
                       .arg(exactRows).arg(exactRows - dupCount));
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

    // ── SRC-11：活引擎端到端（T21）──────────────────────────────────────
    // SRC-06..09 是纯逻辑判据，证明"规则本身对"；但**规则再对，没人调用也是白搭**。
    // 这条走真实引擎，验证 `collect()` 里那次 stable_sort 真的被执行了。
    // 放在阶段 B 的最后：它会把模型的结果集换成"Moon 的结果"，
    // 而前面 V1/V2/SRC-04c 都依赖 fixture（别的天体），顺序不能颠倒。
    {
        const int n = ctx->facade->searchObjects(QStringLiteral("Moon"), 10);
        if (n <= 0) {
            // 环境条件（SolarSystem 数据未载入）不是模型缺陷——照实说明，不判 FAIL。
            ctx->details << QStringLiteral("INFO SRC-11 跳过：本环境搜「Moon」无结果"
                                           "（SolarSystem 数据未载入）—— 环境条件，不判 FAIL");
        } else {
            const QString firstSid = m->stableIdAt(0);
            const int q0 = m->data(m->index(0, 0), SearchResultsModel::QualityRole).toInt();
            QString allNames;
            for (int r = 0; r < n; ++r)
                allNames += (allNames.isEmpty() ? QString() : QStringLiteral(" > ")) + m->nameAt(r);
            ctx->check(firstSid == QStringLiteral("Planet:Moon"),
                       QStringLiteral("SRC-11 活引擎端到端：搜「Moon」首行 = Planet:Moon"
                                      "（实得 %1，匹配质量 %2）；完整顺序 [%3]")
                           .arg(firstSid,
                                SearchRanker::qualityName(static_cast<MatchQuality>(q0)),
                                allNames));
        }
    }

    // ── T24 拼音检索：活引擎腿（真实引擎 + 真实模型 + 真实数据表）────────
    // ⚠️ 本判据会改引擎语言环境（zh_CN），必须**在本段内还原**（T22 TC-18 血泪：
    // 判据改了环境不在同一步恢复，后面的判据就在错误语境里测出假红/假绿）。
    // 还原路径无早退（check() 不抛、不 return），顺序执行到底即可。
    {
        const QString langBefore = StelApp::getInstance().getLocaleMgr().getAppLanguage();
        StelApp::getInstance().getLocaleMgr().setAppLanguage(QStringLiteral("zh_CN"));

        // 诊断段（T24 排查"拼音候选 0"）：打印模块枚举 / cast 成功数 / 名字样本
        {
            const auto allModules = StelApp::getInstance().getModuleMgr().getAllModules();
            int castOk = 0;
            QString samples;
            for (StelModule *m : allModules) {
                auto *mod = dynamic_cast<StelObjectModule *>(m);
                if (!mod)
                    continue;
                ++castOk;
                const auto all = mod->listAllObjects(false);
                if (samples.size() < 200 && !all.isEmpty()) {
                    for (int i = 0; i < std::min<int>(3, all.size()); ++i)
                        samples += QStringLiteral("[%1 cjk=%2] ")
                                       .arg(all.at(i).first,
                                            PinyinIndex::hasCjk(all.at(i).first)
                                                ? QStringLiteral("1")
                                                : QStringLiteral("0"));
                    samples += QStringLiteral("| ");
                }
            }
            ctx->details << QStringLiteral("DIAG 模块总数=%1 cast成功=%2 样本：%3")
                                .arg(allModules.size()).arg(castOk).arg(samples);
        }

        // PINY-06 全拼端到端：搜 "yueqiu" → 首行必须是"月球"（Planet:Moon）。
        // 引擎字面检索对 "yueqiu" 必然空手而归 ⇒ 这行只能来自拼音分支 ⇒
        // 同时证明"拼音分支真的被 collect() 调用"（SRC-11 的拼音版）。
        const int nFull = ctx->facade->searchObjects(QStringLiteral("yueqiu"), 10);
        const QString sidFull = nFull > 0 ? m->stableIdAt(0) : QString();
        const QString nameFull = nFull > 0 ? m->nameAt(0) : QString();
        const int pinyinCount = m->lastPinyinMatchCount();
        ctx->check(nFull > 0 && sidFull == QStringLiteral("Planet:Moon")
                       && nameFull == QStringLiteral("月球"),
                   QStringLiteral("PINY-06 活引擎全拼：搜「yueqiu」首行 = 月球(Planet:Moon)"
                                  "（实得 %1 行，首行 %2/%3，匹配质量 %4）")
                       .arg(nFull).arg(nameFull, sidFull,
                                        nFull > 0 ? SearchRanker::qualityName(
                                            static_cast<MatchQuality>(
                                                m->data(m->index(0, 0),
                                                        SearchResultsModel::QualityRole)
                                                    .toInt()))
                                                  : QString(QStringLiteral("(无)"))));

        // PINY-07 拼音候选计数：本查询的候选**只能**来自拼音分支（引擎字面检索
        // 对 "yueqiu" 恒为 0 条）⇒ lastPinyinMatchCount 必须 > 0。孤立断言
        // "首行对了"可以假绿（万一引擎哪天改了字面匹配规则，PINY-06 的来源
        // 就不再是拼音分支）——计数与首行**成对**，缺一不可。
        ctx->check(pinyinCount > 0,
                   QStringLiteral("PINY-07 拼音候选计数 > 0（实得 %1）—— 证明"
                                  "PINY-06 的首行确实来自拼音分支而非字面命中")
                       .arg(pinyinCount));

        // PINY-08 首字母端到端：搜 "yq" → 首行也是"月球"（首字母档垫底，
        // 但"月球"是该查询下唯一命中，垫底也是第一）。
        const int nIni = ctx->facade->searchObjects(QStringLiteral("yq"), 10);
        const QString sidIni = nIni > 0 ? m->stableIdAt(0) : QString();
        ctx->check(nIni > 0 && sidIni == QStringLiteral("Planet:Moon"),
                   QStringLiteral("PINY-08 活引擎首字母：搜「yq」首行 = Planet:Moon"
                                  "（实得 %1 行，首行 %2，拼音候选 %3）")
                       .arg(nIni).arg(sidIni).arg(m->lastPinyinMatchCount()));

        StelApp::getInstance().getLocaleMgr().setAppLanguage(langBefore);
        ctx->details << QStringLiteral("INFO 语言环境已还原为 %1（拼音判据只在自己段内改环境）")
                            .arg(langBefore);
    }

    // ── T26 召回闭环：候选池全量化（活引擎腿）────────────────────────────
    // T21/T24 时代本层只能重排"引擎已返回的候选"；引擎候选集截断发生在枚举序
    // 里（每模块 maxNbItem 先到先得，StelObjectModule::listMatchingObjects 的
    // break），高相关度候选可能压根没进池。T26 引擎新增**无截断原语**
    // listAllMatchingObjects（默认实现以不可能触顶的预算复用各模块自有匹配
    // 逻辑，语义零复刻），collect() 改调它、截断移到排序之后。
    // 本组判据证明三件事：① 新原语真的比旧截断调用拿到更多（判别对照）；
    // ② 模型真的换了原语（raw == 引擎全量数）；③ 排序层在全量池上把词首
    // 候选顶到最前（recall 的收益落到 precision@1）。
    // 查询用 "al"：星名以 Al 开头的恒星很多（Alcor/Albireo/Alcyone/Altair…），
    // 星名表是 core 自带（fixture 已保证载入），不依赖 SolarSystem 插件；
    // 且此段在 PINY 语言还原之后执行，名字处于默认应用语言下。
    {
        StelObjectMgr &objMgr = StelApp::getInstance().getStelObjectMgr();
        const QString satQ = QStringLiteral("al");

        // SRC-12 判别对照：无截断原语 > 每模块 cap=3 的旧调用，且词首候选在池内。
        const auto fullPool = objMgr.listAllMatchingObjects(satQ, false);
        const auto cappedPool = objMgr.listMatchingObjects(satQ, 3, false);
        bool hasWordStart = false;
        for (const auto &pr : fullPool) {
            if (pr.first.startsWith(satQ, Qt::CaseInsensitive)) {
                hasWordStart = true;
                break;
            }
        }
        ctx->check(int(fullPool.size()) > int(cappedPool.size()) && hasWordStart,
                   QStringLiteral("SRC-12 无截断原语全量 %1 条 > 旧每模块cap=3 调用 %2 条"
                                  "（证明截断真的丢过候选），词首候选在池内=%3")
                       .arg(fullPool.size()).arg(cappedPool.size()).arg(hasWordStart));

        // SRC-13 模型接线：collect() 走无截断原语 ⇒ lastRawMatchCount == 引擎全量数。
        // 孤立断言"行数=cap"证明不了来源（旧调用也能凑出 3 行）——raw 计数与
        // 引擎侧读数**成对**才判别（负控：回退模型调用 → raw 掉回截断数 → 红）。
        const int rows3 = ctx->facade->searchObjects(satQ, 3);
        const int raw3 = m->lastRawMatchCount();
        ctx->check(rows3 == 3 && raw3 == int(fullPool.size()),
                   QStringLiteral("SRC-13 模型走无截断原语：cap=3 → %1 行，"
                                  "raw=%2 == 引擎全量 %3")
                       .arg(rows3).arg(raw3).arg(int(fullPool.size())));

        // SRC-14 端到端：全量池上排序层必须把高相关档（完全/前缀/词首）顶到
        // 最前——recall 的收益落到 precision@1。全序已由 SRC-08 保证 ⇒ 首行可
        // 确定断言。（"al" 对 "Albireo" 等是前缀档，比词首档更优，一并接受。）
        const int rows5 = ctx->facade->searchObjects(satQ, 5);
        const int q0 = rows5 > 0
                           ? m->data(m->index(0, 0), SearchResultsModel::QualityRole).toInt()
                           : -1;
        ctx->check(rows5 == 5 && q0 >= 0 && q0 <= int(MatchQuality::WordStart),
                   QStringLiteral("SRC-14 全量池上首行 = 高相关档（%1，行数 %2）"
                                  "—— 候选池全量化后，排序最优档确实可达")
                       .arg(rows5 > 0 ? SearchRanker::qualityName(
                                            static_cast<MatchQuality>(q0))
                                      : QString(QStringLiteral("(无)")))
                       .arg(rows5));
    }
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
