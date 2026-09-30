/*
 * ConfigIsolationCheck — T36-C 配置目录隔离自检（STELQUICK_CONFIG_CHECK=1）。
 *
 * ── 判据清单（CFG-01..CFG-08，8 条；前提不齐 ⇒ 整套 UNAVAILABLE）───────────────
 *   CFG-01 **隔离读数自洽**：引导期记录 isolated=1 ∧ **note 为空** ∧ 独立重读
 *         `StelFileMgr::getUserDir()` == 记录里的 personalDir ∧ ≠ originalDir。
 *         `note` 是**降级/异常**通道（`setUserDir` 抛异常、兜底拷贝失败），不是
 *         "有没有走播种"的通道 —— 后者走 `info`，**不参与任何判据**。
 *         ⚠️ 这两者曾混用一个字段，导致"非首次启动/负控"把正常路径写进 note、
 *         让本条假红（实测：负控 B 曾多红一条 CFG-01）。判据验的是"隔离到底
 *         生没生效"，与"这次跳不跳播种"无关 ⇒ 必须分开。
 *   CFG-02 **目录独立**：personalDir 存在、是目录、可写（真写一个临时文件再删），
 *         且**不是** originalDir 的子树，也**不是**同一个路径。
 *         （前缀判断是结构性防线：防止有人把"子目录"当成"独立目录"。）
 *   CFG-03 **配置写侧落点**：`StelApp::getSettings()->fileName()` 规范化后必须位于
 *         personalDir 之下，**且 personalDir 与原目录不重合**。★ 这是最强的一条 ——
 *         QSettings::fileName() 是**引擎此刻真正会写的那个文件**，不是我们复述自己的
 *         意图（陷阱 43：换独立回读路径）。★★ 后半句不能省：不写它，隔离关掉时这条
 *         会**自洽假绿**（落点当然"还在那个目录里"，因为两个目录本来就是同一个）。
 *   CFG-04 **日志落点**：personalDir/log.txt 存在 ∧ 非空 ∧ 比 originalDir/log.txt 新。
 *         （原目录那份是原版自己的历史日志；"比它新"才说明我们的日志没写错地方。）
 *   CFG-05 **配置种子完整**：按"原目录有没有 config.ini"分两叉 ——
 *         ① 有 ⇒ 个人版必须**继承齐**它的每个键（键集 ⊇）。只比键、不比值，
 *            用户改过的值必须被尊重；键集用**独立解析**（对原目录另开一个
 *            QSettings 实例枚举 allKeys），不复述引导期的任何计数。
 *            ⚠️ 本机实测：原目录 768 键 vs `data/default_cfg.ini` 239 键 ⇒ 事先量得
 *            **至少 529 键不在默认配置里**，所以"关掉播种、只剩兜底"时这条**必红**。
 *            负控 B 实测缺失 348 键（首个 = DialogPositions/Location）—— 差额那部分是
 *            引擎引导期自己补写的默认值，不影响结论。事先量过，不是事后调判据。
 *         ② 没有（全新机器）⇒ 只要求个人版自己有一份**非空**的（内容由兜底腿提供）。
 *   CFG-06 **写侧不触原目录（成对）**：写一个唯一哨兵键并 `sync()` 之后，
 *         ① 个人版 config.ini 的**原始字节**里出现哨兵值 ∧
 *         ② 原目录 config.ini 的原始字节里**不出现**哨兵值 ∧
 *         ③ 原目录**原本没有** config.ini 时，跑完仍**不许出现**它（写穿会创建）。
 *         三半都必须成立 —— 只查②在"哪儿都没写"时也会绿（负控撞车型假绿，T34 血泪）；
 *         只查①②在**全新机器**上会退化成平凡真（原目录连文件都没有，"不含"自然成立）
 *         ⇒ ③ 是专门补的那个洞。
 *   CFG-07 **产品路径落点（成对）**：把 `flagImmediateSave` 置真（记旧值待还原）⇒
 *         `ActionRouter.trigger("actionShow_Constellation_Lines")`（**用户点按钮走的就是
 *         这条路**）⇒ `sync()` 之后：
 *         ① 个人版 config.ini 里 `flag_constellation_drawing` **等于翻转后的值** ∧
 *         ② 原目录 config.ini 的 md5 **与触发前逐位相同**，且原目录原本没有
 *            config.ini 时跑完仍不存在（与 CFG-06 ③ 同款的非平凡化）。
 *         ①用**内容**而不是时间戳：翻转+复原的值相同 ⇒ mtime 型判据会因文件系统
 *         时间粒度而随机（本机 APFS 够细，但判据不该依赖这个）。
 *   CFG-08 **播种清单完整**：原目录里**除瞬态外**（log.txt / output.txt / config.old）
 *         的每一个文件，在个人版目录里都存在（存在性，不比内容）。
 *         这条守的是"数据没丢"：合流形态切换用户目录后，原目录里的
 *         `modules/Satellites/tle*.txt`（20 MB 下载数据）、`modules/Exoplanets/…`、
 *         `data/….json` 等**都不在安装目录里**，不播种就找不到了。
 *
 * ── 两组负控（红项集**两两不同**，各自承重）────────────────────────────────────
 *   A `STELQUICK_CFG_ISOLATE_OFF=1`（隔离关掉，行为回到修复前）
 *       期望红项 = [CFG-01, CFG-02, CFG-03, CFG-04, CFG-06, CFG-07]
 *       （CFG-05 反而绿：personal == original，那个文件自己当然包含自己的键；
 *         CFG-08 也绿：同一目录，文件当然"都在"）
 *   B `STELQUICK_CFG_MIGRATE_OFF=1`（隔离在、但不播种；个人版目录须先清空）
 *       期望红项 = [CFG-05, CFG-08]
 *       即"配置种子"与"数据种子"两条腿必须靠播种才成立。
 *
 * ── 为什么不需要就绪门（与 T33/T35 的差别）──────────────────────────────────
 *   本套全是**同步事实**：目录路径、文件字节、QSettings::sync() 后的落盘内容。
 *   没有变换栈、没有帧泵、没有跨线程信号 ⇒ 不需要有界窗口就绪门，也不会出现
 *   "等待落点"型竞态。唯一的时间依赖是**隐含前提** `immediateSave` 开关，
 *   而它被显式读取、故意置真、并在收尾行里还原（陷阱 44：判据的隐含前提
 *   可能只在用户 config 里 —— 这里改成"判据自己读+强制+恢复"）。
 *
 * ── 退出码 ─────────────────────────────────────────────────────────────────
 *   0=PASS，10=FAIL，6=UNAVAILABLE（引擎未引导 / 拿不到 settings / 目录不可用）。
 */
#pragma once

#include <QString>
#include <QStringList>
#include <functional>

class QCoreApplication;

namespace stelapp
{

class ActionRouter;

class ConfigIsolationCheck
{
public:
    struct Result
    {
        bool ran = false;
        bool pass = false;
        bool unavailable = false;
        int passed = 0;
        int total = 0;
        QString summary;
        QStringList details;
    };

    //! 全部在 GUI 线程。@p delayMs 后开跑（等引擎动作注册落定）。
    //! 结果经 onDone 回传。**不需要**帧泵（本套不读渲染面）。
    static void run(QCoreApplication *app,
                    ActionRouter *router,
                    const std::function<void(const Result &)> &onDone,
                    int delayMs = 2000);
};

} // namespace stelapp
