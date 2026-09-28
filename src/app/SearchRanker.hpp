/*
 * SearchRanker — 搜索结果的**相关度排序**（T21 落地，A4 正题）。
 *
 * 职责：给一行搜索结果算一个可比较的"排序键"，让列表能把**用户真正想要的那个**
 *       排在最前。纯逻辑：只依赖 QString，不碰引擎、不碰 QObject、不碰 Qt GUI。
 *       因此**两种形态都能编**（链接引擎 / 独立工程），也**可以被自检直接调用**
 *       （不需要拉起引擎就能覆盖全部分级规则）。
 *
 * ── 为什么排序必须由本层做（不是"本层想加个喜好"）────────────────────────
 * 引擎在这件事上是**自相矛盾**的，两处源码各干一半：
 *
 *   ① 模块级 `StelObjectModule::listMatchingObjects`（StelObjectModule.cpp:45-75）
 *      **确实**按相关度排过：完全匹配被单独拎出来 `result.prepend(fullMatch)`
 *      （第 72-73 行），其余按名称 `std::sort`。
 *      它的头注也承诺 "by order of relevance"（StelObjectModule.hpp:70）。
 *
 *   ② 聚合级 `StelObjectMgr::listMatchingObjects`（StelObjectMgr.cpp:595-611）
 *      把各模块结果 `result += matchingObj` 简单拼接后，**无条件**按名称字典序
 *      `std::sort`（第 609 行）——**把 ① 精心 prepend 的完全匹配整个抹掉**。
 *
 *   ⇒ 调用方拿到的是"纯字典序"，与头注承诺的"按相关度"不符。
 *
 * 实测证据（2026-09-28，T20 回放顺带抓到）：
 *   搜 "Moon" → `[0] "Ghost of the Moon Nebula"（Nebula:NGC 6781）`、
 *                `[1] "Moon"（Planet:Moon）`、
 *                `[2] "Pirate Moon Cluster"（Nebula:NGC 1647）`
 *   用户输的是**完整名称** "Moon"，第一条却是名字里带 Moon 的星云。
 *
 * 为什么不改引擎：`StelObjectMgr::listMatchingObjects` 是**共用原语**，
 * 旧 GUI 搜索框也在用（SearchDialog.cpp:985）。在引擎里改排序会牵连旧界面；
 * 而且"结果该怎么排"本来就该由**列表模型**决定（视图想要什么序，模型给什么序）。
 *
 * ── ⚠️ 本层的**能力边界**（不写清就会被误当成"搜索变好了"）──────────────
 * 引擎的候选集截断发生在**排序之前、且按遍历顺序**：
 * `StelObjectModule.cpp:67-68` 是"累计到 maxNbItem 就 break"，遍历的是
 * `listAllObjects(false) << listAllObjects(true)` 的原始顺序。
 * ⇒ **高相关度的候选可能压根没进候选集**。本层只能在**已返回的候选内**重排，
 *   无法凭空召回。所以本层解决的是"**结果里有的东西排对**"（precision@k），
 *   不是"**该有的东西都在**"（recall）。想动召回就得改引擎候选生成，不在本层。
 *   判据因此只断言"已知候选的相对顺序"，不断言"召回完整"。
 *
 * ── 另一个必须在本层处理的连带点 ─────────────────────────────────────────
 * 引擎会把同一个天体给**多行**（翻译名表 + 英文名表各枚举一遍，见
 * StelObjectModule.cpp:52），且 `StelObjectMgr` **不去重**。本层按 stableId 去重时，
 * "保留哪一行"会决定用户**看到的名字**（两行的 name 可能不同：翻译名 vs 英文名）。
 * ⇒ 去重必须保留**相关度最高**的那一行，而不是"遍历顺序最先"的那一行。
 *
 * @see SearchResultsModel::collect() 调用点（去重 + 排序 + 截断全在那里）
 * @see StelObjectModule.cpp:45-75 / StelObjectMgr.cpp:595-611（上述两处源码）
 */

#pragma once

#include <QString>

namespace stelapp {

//! 匹配质量分级。**数值越小越靠前**。
//! 分级依据：用户输得越"完整"，命中的就应该越靠前。
enum class MatchQuality : int
{
    Exact     = 0,  //!< 名称与查询串**完全相同**（忽略大小写）
    Prefix    = 1,  //!< 名称**以**查询串开头（用户输入的是名称的开头）
    WordStart = 2,  //!< 命中点落在名称中**某个词的开头**（"Moon" 之于 "Pirate Moon Cluster"）
    Substring = 3,  //!< 名称**任意位置**含查询串（最弱，通常是"名字里碰巧带这个词"）
    None      = 4,  //!< 不匹配。引擎只返回匹配项，理论上不出现；留作哨兵与防御。
};

//! 排序键。字段顺序即**比较优先级**（见 less()）。
struct RankKey
{
    MatchQuality quality = MatchQuality::None;  //!< 主序：匹配质量
    int          typeWeight = 0;                //!< 次序：天体类型权重（太阳系优先）
    int          nameLength = 0;                //!< 次序：名称长度（越短越"精确"）
    QString      name;                          //!< 尾键：名称字典序（保证**全序**、结果可复现）
};

//! 纯函数集合。无状态，可直接在自检里逐条覆盖。
class SearchRanker
{
public:
    //! 匹配质量分级。
    //! 只做**字符串关系**判定，不含任何天体语义。
    //! 大小写不敏感（与引擎 matchObjectName 的口径一致：它用 Qt::CaseInsensitive）。
    static MatchQuality quality(const QString &name, const QString &query);

    //! 天体类型权重。**数值越小越靠前**。
    //! 口径：太阳系天体（太阳/月球/行星）优先于恒星，恒星优先于深空天体。
    //! 依据是"用户搜一个名字时，最可能是想找那个'有名的实体'"——而普通人能叫出
    //! 名字的天体绝大多数是太阳系天体。此权重只是 tie-break 的**次要**因子：
    //! 它永远排在 quality 之后，所以分级判错也不会破坏主序。
    static int typeWeight(const QString &typeName);

    //! 组装排序键。query 需为**已 trim** 的字符串（调用方统一处理）。
    static RankKey key(const QString &name, const QString &typeName, const QString &query);

    //! 严格弱序（可用于 std::stable_sort / std::sort）。
    //! 尾键用 QString::operator<（UTF-16 码点序，**确定性**），因此同输入必得同输出。
    static bool less(const RankKey &a, const RankKey &b);

    //! 分级的中文说明（判据行里打印用，避免证据里出现裸数字）。
    static QString qualityName(MatchQuality q);
};

} // namespace stelapp
