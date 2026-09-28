/*
 * SearchRanker 实现。设计说明与能力边界见 SearchRanker.hpp 头注。
 */

#include "SearchRanker.hpp"

namespace stelapp {

// ── 匹配质量分级 ─────────────────────────────────────────────────────────────

MatchQuality SearchRanker::quality(const QString &name, const QString &query)
{
    const QString n = name.trimmed();
    const QString q = query.trimmed();
    if (q.isEmpty() || n.isEmpty())
        return MatchQuality::None;

    // ① 完全匹配
    if (n.compare(q, Qt::CaseInsensitive) == 0)
        return MatchQuality::Exact;

    // ② 前缀匹配
    if (n.startsWith(q, Qt::CaseInsensitive))
        return MatchQuality::Prefix;

    // ③ 词首匹配：找出**所有**命中位置，只要有一处落在词边界就算 WordStart。
    //    词边界判定用 "前一字符不是字母/数字"（空格、连字符、括号、撇号等都算）。
    //    注意：中文没有词边界，中文字符互相之间都算字母 ⇒ 中文的多字查询不会被
    //    误判成 WordStart，会自然落到 ④，这是刻意的（中文按子串匹配更符合直觉）。
    bool hitAnywhere = false;
    int from = 0;
    while (from <= n.size())
    {
        const int pos = n.indexOf(q, from, Qt::CaseInsensitive);
        if (pos < 0)
            break;
        hitAnywhere = true;
        if (pos == 0)
            return MatchQuality::Prefix;   // 理论上 ② 已拦；这里兜底，语义等同
        const QChar prev = n.at(pos - 1);
        if (!prev.isLetterOrNumber())
            return MatchQuality::WordStart;
        from = pos + 1;
    }

    // ④ 子串匹配（命中点全都落在某个词的中间）
    if (hitAnywhere)
        return MatchQuality::Substring;

    return MatchQuality::None;
}

// ── 天体类型权重 ─────────────────────────────────────────────────────────────

int SearchRanker::typeWeight(const QString &typeName)
{
    // 取值来源是实测的 stableId 前缀（`obj->getType()`），不是猜测：
    //   Planet:Moon（SolarSystem 模块）、Nebula:NGC 6781（NebulaMgr）。
    // 太阳系天体给 0：用户能叫出名字的天体绝大多数是太阳系天体，
    // 且它们数量少、名字短，最容易被同名深空天体挤下去。
    if (typeName == QLatin1String("Planet"))
        return 0;   // 太阳系天体（太阳 / 月球 / 行星 / 矮行星 / 小行星…）
    if (typeName == QLatin1String("SolarSystem"))
        return 0;   // 防御：不同引擎版本可能直接用模块名当 getType()
    if (typeName == QLatin1String("Star"))
        return 1;   // 恒星（有专名的恒星用户也在搜）
    if (typeName == QLatin1String("Nebula"))
        return 2;   // 深空天体
    if (typeName == QLatin1String("Galaxy"))
        return 2;
    return 3;       // 其余（星团 / 人造天体 / 文化天体…）：不猜，给末位
}

// ── 排序键与比较 ─────────────────────────────────────────────────────────────

RankKey SearchRanker::key(const QString &name, const QString &typeName, const QString &query)
{
    RankKey k;
    k.quality    = quality(name, query);
    k.typeWeight = typeWeight(typeName);
    k.nameLength = name.trimmed().size();
    k.name       = name;
    return k;
}

bool SearchRanker::less(const RankKey &a, const RankKey &b)
{
    // 优先级：匹配质量 > 类型权重 > 名称长度 > 名称字典序。
    // 前三条是"相关度"，最后一条只为了让**全序成立**（否则同键元素顺序未定义，
    // 结果不可复现，"搜两次顺序不一样"会变成神出鬼没的 bug）。
    if (a.quality != b.quality)
        return static_cast<int>(a.quality) < static_cast<int>(b.quality);
    if (a.typeWeight != b.typeWeight)
        return a.typeWeight < b.typeWeight;
    if (a.nameLength != b.nameLength)
        return a.nameLength < b.nameLength;
    return a.name < b.name;
}

// ── 证据可读性 ───────────────────────────────────────────────────────────────

QString SearchRanker::qualityName(MatchQuality q)
{
    switch (q)
    {
    case MatchQuality::Exact:     return QStringLiteral("完全匹配");
    case MatchQuality::Prefix:    return QStringLiteral("前缀匹配");
    case MatchQuality::WordStart: return QStringLiteral("词首匹配");
    case MatchQuality::PinyinFull:    return QStringLiteral("拼音全拼");
    case MatchQuality::Substring: return QStringLiteral("子串匹配");
    case MatchQuality::PinyinInitial: return QStringLiteral("拼音首字母");
    case MatchQuality::None:      return QStringLiteral("不匹配");
    }
    return QStringLiteral("未知");
}

} // namespace stelapp
