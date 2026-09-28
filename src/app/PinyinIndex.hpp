/*
 * PinyinIndex — 中文名的**拼音检索索引**（T24 落地，A4 加固）。
 *
 * ── 为什么必须有这一层 ────────────────────────────────────────────────────
 * 中文界面下天体名是翻译名（"月球""猎户座大星云"），而用户用键盘只能敲
 * ASCII —— 引擎的 matchObjectName（StelObjectModule.cpp:32-38）做的是
 * **字面** contains/startsWith，"yueqiu" 与 "月球" 永远不匹配
 * ⇒ 拼音查询在引擎检索这路**一条候选都拿不到**。这不是排序问题（T21
 * 管不了"根本没进候选集的东西"），是**召回**问题：必须有一层能
 * 把 ASCII 拼音串映射到中文名，在检索阶段就放行这些候选。
 *
 * ── 为什么不改引擎 ───────────────────────────────────────────────────────
 * listMatchingObjects 是共用原语（旧 SearchDialog.cpp:985 也在用）；
 * 且拼音表是**应用层资产**（几百 KB 数据文件），塞进 src/core 会让引擎
 * 层背上 UI 语义。T21 的先例同样适用：视图想要什么行为，模型层自己长。
 *
 * ── 数据来源与口径 ───────────────────────────────────────────────────────
 * 拼音表来自 mozillazg/pinyin-data v0.15.0（MIT License），构建期已转成
 * 无声调精简格式（`U+XXXX py1 py2 ...`，多音字保留全部读音，第一读音 =
 * 该字最高频读音）。资源路径 `:/StelQuickUI/pinyin-lite.txt`，懒加载一次。
 *
 * ── 匹配语义（两层，对应 MatchQuality 两个新档位）──────────────────────
 *   PinyinFull    ：名字汉字**主读音串**含查询串（"yueqiu" ⊂ "yueqiu"；
 *                    "huzu" ⊂ "liehuzudaxingyun" 也算——中间片段是合法意图）
 *   PinyinInitial ：名字汉字**首字母串**以查询串开头（"yq" → "月球"）。
 *                    只认词首，避免误命中泛滥。
 *   多音字按**主读音**（第一读音）拼——"银行"会被拼成 yinxing 而非 yinhang，
 *   这是可接受的精度损失（天体译名里多音字很少，全组合展开是多音字数的
 *   指数爆炸，不做）。
 *
 * ── 性能 ────────────────────────────────────────────────────────────────
 * 表加载一次（~44k 条，毫秒级）；名字→拼音结果做 memo（同名在会话内只转
 * 一次，语言切换后名字串本身不同、不冲突）。一次拼音检索 = 全量列表遍历 +
 * hash 查找 + contains，万级候选亚毫秒。
 */

#pragma once

#include <QString>

#include "SearchRanker.hpp"

namespace stelapp {

class PinyinIndex
{
public:
    //! 拼音表是否已成功加载（表缺失/损坏时 false ⇒ 调用方应整体跳过拼音分支）。
    static bool available();

    //! 名字里全部汉字的主读音串（无声调小写拼接）；非汉字字符跳过。
    //! 空串 = 名字里没有汉字（调用方可用它短路）。
    static QString toPinyin(const QString &name);

    //! 名字里全部汉字的首字母串（主读音首字母拼接）。
    static QString initials(const QString &name);

    //! 拼音匹配判定：返回命中的拼音档位（PinyinFull / PinyinInitial / None）。
    //! query 期望是已 lower() 的纯字母串；其它形态一律 None。
    static MatchQuality matchQuality(const QString &name, const QString &query);

    //! 查询串是否是"拼音形态"（非空且全为 ASCII 字母）。
    //! 中文名查询（含 CJK）不走拼音分支——引擎检索那路本来就认。
    static bool isPinyinQuery(const QString &query);

    //! 名字是否含 CJK 汉字（拼音匹配只对汉字名做，英文名引擎那路已覆盖）。
    static bool hasCjk(const QString &name);

    //! 档位说明（判据行里打印用）。
    static QString qualityName(MatchQuality q);
};

} // namespace stelapp
