/*
 * PinyinIndex 实现 — 见头注（T24）。
 *
 * 实现要点：
 *  - 表懒加载：首次调用任何查询接口时从 qrc 读入；失败只置 s_loadFailed，
 *    **不抛不崩**——available() 返回 false 让调用方整体跳过拼音分支。
 *  - memo：名字 → (主读音, 首字母)。会话内同名只转一次。
 *    用 QHash 而非 static 局部初始化：首次调用发生在 UI 线程（collect）
 *    与自检（SearchModelCheck）两处，语义上都是单线程入口。
 */

#include "PinyinIndex.hpp"

#include <QFile>
#include <QHash>
#include <QTextStream>

namespace stelapp {

namespace {

//! 基本区 U+4E00–U+9FFF + 扩展A U+3400–U+4DBF。天体译名不会超出这两个区。
bool isHanChar(uint cp)
{
    return (cp >= 0x4E00 && cp <= 0x9FFF) || (cp >= 0x3400 && cp <= 0x4DBF);
}

struct Table
{
    bool loaded = false;
    bool failed = false;
    QHash<uint, QStringList> readings;   //!< 码点 → 全部读音（第一读音 = 主读音）
};

Table &table()
{
    static Table t;
    if (t.loaded || t.failed)
        return t;

    QFile f(QStringLiteral(":/StelQuickUI/pinyin-lite.txt"));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        t.failed = true;
        return t;
    }
    QTextStream in(&f);   // Qt6 默认 UTF-8（setCodec 已随 Qt5 移除）
    while (!in.atEnd()) {
        // 行格式：U+XXXX py1 py2 ...（构建期已去声调、去注释）
        const QString line = in.readLine().trimmed();
        if (line.isEmpty() || !line.startsWith(QStringLiteral("U+")))
            continue;
        const int sp = line.indexOf(QLatin1Char(' '));
        if (sp <= 2)
            continue;
        bool ok = false;
        const uint cp = line.mid(2, sp - 2).toUInt(&ok, 16);
        if (!ok || !isHanChar(cp))
            continue;
        t.readings.insert(cp, line.mid(sp + 1).split(QLatin1Char(' '), Qt::SkipEmptyParts));
    }
    t.failed = t.readings.isEmpty();
    t.loaded = true;
    return t;
}

//! 名字 → (主读音串, 首字母串)，带 memo。
QPair<QString, QString> pinyinOf(const QString &name)
{
    static QHash<QString, QPair<QString, QString>> memo;
    const auto it = memo.constFind(name);
    if (it != memo.constEnd())
        return it.value();

    QString full, initials;
    int i = 0;
    const int n = name.size();
    while (i < n) {
        const QChar c = name.at(i);
        uint cp = c.unicode();
        if (c.isHighSurrogate() && i + 1 < n && name.at(i + 1).isLowSurrogate())
            cp = QChar::surrogateToUcs4(c, name.at(i + 1));

        if (isHanChar(cp)) {
            const QStringList &rs = table().readings.value(cp);
            if (!rs.isEmpty()) {
                full += rs.first();
                initials += rs.first().left(1);
            }
        }
        i += (cp > 0xFFFF) ? 2 : 1;
    }
    const QPair<QString, QString> r{full, initials};
    memo.insert(name, r);
    return r;
}

//! 查询串是否全为 ASCII 字母（小写比较已由调用方 lower() 完成，这里两种都认）。
bool asciiLettersOnly(const QString &s)
{
    if (s.isEmpty())
        return false;
    for (const QChar c : s) {
        const char16_t u = c.unicode();
        if (!((u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z')))
            return false;
    }
    return true;
}

} // namespace

bool PinyinIndex::available()
{
    return !table().failed;
}

QString PinyinIndex::toPinyin(const QString &name)
{
    return pinyinOf(name).first;
}

QString PinyinIndex::initials(const QString &name)
{
    return pinyinOf(name).second;
}

MatchQuality PinyinIndex::matchQuality(const QString &name, const QString &query)
{
    if (!available() || !asciiLettersOnly(query))
        return MatchQuality::None;
    const QString q = query.toLower();
    const auto py = pinyinOf(name);
    if (py.first.isEmpty())
        return MatchQuality::None;          // 名字里没有汉字
    if (py.first.contains(q))
        return MatchQuality::PinyinFull;
    if (py.second.startsWith(q))
        return MatchQuality::PinyinInitial;
    return MatchQuality::None;
}

bool PinyinIndex::isPinyinQuery(const QString &query)
{
    return asciiLettersOnly(query);
}

bool PinyinIndex::hasCjk(const QString &name)
{
    int i = 0;
    const int n = name.size();
    while (i < n) {
        const QChar c = name.at(i);
        uint cp = c.unicode();
        if (c.isHighSurrogate() && i + 1 < n && name.at(i + 1).isLowSurrogate())
            cp = QChar::surrogateToUcs4(c, name.at(i + 1));
        if (isHanChar(cp))
            return true;
        i += (cp > 0xFFFF) ? 2 : 1;
    }
    return false;
}

QString PinyinIndex::qualityName(MatchQuality q)
{
    // 直接复用 SearchRanker 的档位文案（T24 两个新档已并入其 switch）。
    return SearchRanker::qualityName(q);
}

} // namespace stelapp
