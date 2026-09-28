/*
 * SearchModelCheck — T17 搜索/选择模型自检（STELQUICK_SEARCH_CHECK=1）。
 *
 * 与 A2FrameCheck / DynFrameCheck / AppFacadeCheck 同风格：装配在 main.cpp 完成
 * （引擎已 boot、帧泵已 start、AppFacade 已建），本类只消费就绪对象，跑完经回调回传。
 *
 * 判据（对应软件测试文档 3.3 U-SRC-01..04 + T17 任务书的纵向判据）：
 *
 *   阶段 A —— **不依赖任何真实天体**，只验模型语义（环境无关，必须恒可跑）：
 *   SRC-01 过期结果被丢弃（U-SRC-01）：先发 A 再发 B，然后把 A 的结果投进来——
 *          必须被拒收、列表仍是 B 的、丢弃计数 +1；requestId=0 同样被拒。
 *          直接构造"旧请求后到达"，不依赖并发。
 *   SRC-02 空结果安全（U-SRC-02）：查一个必然无匹配的串 → 0 行 + 非空空态理由；
 *          再查空串 → 同样安全（有明确理由，不是静默）。
 *   SRC-03 失效访问返回安全空值（U-SRC-03）：非法 stableId（无分隔符 / 空类型 /
 *          空 ID / 类型不存在）逐一验：返回 false 且信息模型**全部归零**，
 *          不留陈旧值（陈旧值比空值更危险——QML 会照常渲染）。
 *
 *   SRC-06..09 —— **相关度排序（T21）**，同样是环境无关的纯逻辑判据：
 *   SRC-06 匹配分级逐条覆盖：完全 / 前缀 / 词首 / 子串 / 不匹配 共 7 例，
 *          含大小写无关（"Moon" vs "moon"）与"词中命中不算词首"（Honeymoon）。
 *   SRC-07 实测场景复现：用 T20 回放证据里的三个真实名字（Ghost of the Moon Nebula /
 *          Moon / Pirate Moon Cluster）排序，首行必须是 Moon；07b 连带验"同分之后"
 *          的第二、三级比较（类型权重 → 名字长度）也生效。
 *   SRC-08 稳定可复现：打乱输入顺序，排序结果不变（全序）；08b 比较器自洽
 *          （非自反 + 不对称——传非法比较器给 std::sort 是 UB，不只是排错）。
 *   SRC-09 **内建判别性对照**：把同一批候选按纯字典序排（= 修复前引擎的排法），
 *          首行必须**不是** Moon。若这条 FAIL，说明该场景两种排法给同样结果、
 *          SRC-07 测不出东西——那时该换数据，而不是改判据。
 *
 *   阶段 B —— 需要环境中**至少有一个可检索天体**（星表已载入即可满足）：
 *   SRC-04 稳定标识（U-SRC-04）：同一查询跨次得到的 stableId 完全一致且形如
 *          "type:id"；选中后按 stableId **跨查询回查**能重新选中同一天体
 *          （以英文名作身份比对——显示名随界面语言变化，不能当身份）。
 *   SRC-05 总量上限：引擎的 listMatchingObjects 把 maxNbItem 当**每模块**上限
 *          （各模块各取 N 条后拼接、再按名称字典序重排），故调用方可能收到
 *          模块数×N 条。本层统一为**总量**上限，此处验证真的截断到 N 条以内，
 *          并用 lastRawMatchCount() 暴露原始匹配数（证明上限确实生效过）。
 *   纵向判据（T17 通过条件）：搜索 → 选择 → 信息页三段的产物**同时非空**
 *          （displayName / typeName / infoText），即"纵向可用"。
 *   SRC-11 活引擎端到端（T21）：搜 "Moon" 首行必须是 Planet:Moon。**顺序上放在阶段 B
 *          最后**——它会换掉模型的结果集，而前面的 V1/V2/SRC-04c 依赖 fixture。
 *          环境无 SolarSystem 数据时不判 FAIL，只记 INFO 说明跳过。
 *
 * 环境缺fixture 的处置（**不许静默通过**）：
 *   阶段 B 找不到任何可检索天体时，**不判 FAIL**（那是环境不是模型缺陷），
 *   而是整体判 UNAVAILABLE（rc=6）并**照实打印阶段 A 的结果**——
 *   让"模型逻辑已验、只是环境没有天体"这一事实可见，而不是伪装成全绿。
 *   首次探测失败会**自动重试一次**（引擎数据可能尚在载入，星表载入是渐进的）。
 *
 * 退出码：0=PASS，10=FAIL，6=UNAVAILABLE（阶段 B 无 fixture；阶段 A 仍需全绿）。
 *   注：不占 9 —— 9 已被长跑的环境前置守卫占用（"拒绝测量"），语义不同不可复用。
 */
#pragma once

#include <QString>
#include <QStringList>
#include <functional>

class QCoreApplication;

namespace stelapp {

class AppFacade;

class SearchModelCheck
{
public:
    struct Result
    {
        bool ran = false;          //!< 真正跑过（前置满足）
        bool pass = false;
        bool unavailable = false;  //!< 阶段 B 无 fixture（环境条件，非模型缺陷）
        int passed = 0;
        int total = 0;
        QString summary;
        QStringList details;
    };

    //! 全部在 GUI 线程。@p delayMs 后开跑（等引擎数据落定），
    //! 首次找不到 fixture 时再等 @p retryMs 重试一次。结果经 onDone 回传。
    static void run(QCoreApplication *app,
                    AppFacade *facade,
                    const std::function<void(const Result &)> &onDone,
                    int delayMs = 2500,
                    int retryMs = 1500);
};

} // namespace stelapp
