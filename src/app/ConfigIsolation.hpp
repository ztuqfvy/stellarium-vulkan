/*
 * ConfigIsolation — T36：合流形态的**个人版配置目录隔离 + 首次迁移**。
 *
 * ── 为什么要有这个东西（探针实测，不是推测）────────────────────────────────
 * 合流形态（stelQuickUI）此前直接用引擎的**默认**用户目录，也就是**原版
 * Stellarium 的目录**。macOS 上实测（`docs/evidence/2026-09-30-t36-config/probe/`）：
 * 跑一次 `STELQUICK_TOOL_CHECK=1` 之后，
 *     config.ini                       mtime 变（被 QSettings 整体重写）
 *     log.txt                          10308 B → 10387 B，md5 变（原版日志被顶掉）
 *     modules/Oculars/ocular.ini       mtime 变
 * 原因不是"某个模块写错了"，而是**路径本来就选错了**：
 *   · `StelApp::immediateSave()` 在 `flagImmediateSave` 为真时直写 `getSettings()`；
 *     本机 config.ini 里就是 `immediate_save_details = true` ⇒ 工具栏翻一个开关
 *     就落到原版配置里；
 *   · 日志 `StelLogger::init(userDir + "/log.txt")` 同样落在原版目录，**截断覆盖**。
 * A-1.0 范围表写得很明确：「资源路径 …… 配置保存 | 最小 | **必须**」，且
 * 「配置使用**独立个人版目录**，避免修改原程序设置」。
 *
 * ── 为什么必须"拷贝迁移"，不能靠"把原目录当只读回退"────────────────────────
 * 直觉方案是 fileLocations = [个人版, …, 原版]，让个人版里没有的数据文件回退到
 * 原版去**读**。**这条路是漏的**：`StelFileMgr::findFile()`（StelFileMgr.cpp:218）
 * 是"按 fileLocations 顺序返回**第一个满足 flags** 的路径"，而 `Writable` 只是
 * "**那个文件**可写"。于是模块用 `findFile("modules/Satellites/tle0.txt",
 * Writable|File)` 更新 TLE 时会命中**原版**里的副本并原地改写原版 —— 正是我们要
 * 消除的行为。⇒ 只能**先把原目录播种到个人版目录**，之后个人版目录里
 * **什么都有**，原目录彻底退出搜索路径（`setUserDir` 是 `replace(0, …)`，不是
 * append，见 StelFileMgr.cpp:422-428）。
 *
 * ── 落点 ────────────────────────────────────────────────────────────────────
 * `personalDir = <引擎默认用户目录> + "-quick"`（macOS：
 * `~/Library/Application Support/Stellarium-quick`）。**同级目录**，不是原目录的
 * 子目录 —— 这样"原目录一字节不动"是**结构性**成立的（连目录 mtime 都不变），
 * 判据才敢按字节比对。★ 刻意**不**复刻各平台 `QDir::homePath()` / `CSIDL_APPDATA`
 * 的推导（那是引擎的语义），一律以 `StelFileMgr::getUserDir()` 为基准加后缀推导。
 *
 * ── 调用契约（硬性）─────────────────────────────────────────────────────────
 *   ① 必须在 `StelFileMgr::init()` **之后**调用（依赖 init 建好的 fileLocations[0]）；
 *   ② 必须在 `StelLogger::init()` 与**任何** `findFile("config.ini")` **之前**调用
 *      （否则日志/配置已经落到原目录）；
 *   ③ 幂等：重复调用返回首次的记录，不会推出 `-quick-quick`。
 *
 * ── 第二条腿：个人版目录里**必须**有一份 config.ini（否则引擎会段错误）────────
 * 实测（本轮探针）：把用户目录清空后跑，引擎 **rc=139（SIGSEGV）**，崩在
 * `LandscapeMgr: initialized Cache for 100 MB.` 之后、`StarMgr` 载入天区文化之前。
 * 分块播种二分：只放 `stars` / `modules` / `data` 都照崩，**只放 `config.ini` 就正常**
 * ⇒ 崩因是"**用户目录里没有 config.ini**"，不是缺数据。
 * 上游 `src/main.cpp:398-403` 本来就是这么处理的：
 *     「Config file … does not exist. Copying the default file.」⇒ `copyDefaultConfigFile()`
 *     （`src/main.cpp:130`，把 `data/default_cfg.ini` 拷成 `config.ini` 并补写权限）。
 * **合流形态此前根本没有这一步**，只是被"原目录里早就有 config.ini"一直掩盖着；
 * 一旦用户目录变成我们自己的个人版目录，这个洞就会在**全新机器**上露出来。
 * ⇒ 本函数无条件补上这条腿（不随 `STELQUICK_CFG_MIGRATE_OFF` 关闭）：播种是
 *    "搬用户的旧东西"，兜底是"没有旧东西也要能起来"，两件事不能混。
 *
 * ── 负控开关（只用于证明 `ConfigIsolationCheck` 的判据承重；正题恒不设）─────
 *   `STELQUICK_CFG_ISOLATE_OFF=1` ⇒ 跳过 `setUserDir`（行为回到修复前）
 *   `STELQUICK_CFG_MIGRATE_OFF=1` ⇒ 隔离生效但**不做**播种（个人版目录空着）
 */
#pragma once

#include <QString>
#include <QtGlobal>

namespace stelapp
{

//! 引导期一次性的配置隔离读数（供自检与证据读取）。
struct ConfigIsolationReport
{
    //! 引擎默认（= 原版 Stellarium）用户目录。
    QString originalDir;
    //! 实际使用的用户目录。隔离失败/被关时为 == originalDir。
    QString personalDir;
    //! 是否成功切到个人版目录（`setUserDir` 未抛异常）。
    bool isolated = false;
    //! 本次是否**进入**了播种流程（判据：目标 config.ini 此前不存在 ∧ 未被 env 关掉）。
    bool migrationRan = false;
    //! 播种拷贝的文件数与字节数（migrationRan 为假时恒 0）。
    int migratedFiles = 0;
    qint64 migratedBytes = 0;
    //! 播种时因目标已存在而跳过的文件数（幂等性读数）。
    int skippedExisting = 0;
    //! 个人版目录里**原本没有** config.ini，本次从 `data/default_cfg.ini` 兜底拷了一份。
    //! 这条腿不是可选项 —— 见下面对"空 config.ini 会崩"的说明。
    bool defaultConfigSeeded = false;
    //! **降级/异常**说明（空串 = 一切正常）。非空时表示隔离过程真出了问题
    //! （如 `setUserDir` 抛异常、兜底拷贝失败），判据 CFG-01 据此报红。
    QString note;
    //! **正常**流程说明（"个人版里已有 config.ini ⇒ 跳过播种"、"负控生效"、
    //! "原目录不存在 ⇒ 无可播种"）。与 note 的差别是**语义**，不是内容：
    //! info 非空**不影响任何判据**。
    //! ⚠️ 两者必须分开：把正常路径塞进 note 会让 CFG-01 在**非首次启动**与
    //! 负控下假红（实测踩过 —— 负控 B 因此多红一条 CFG-01），而 CFG-01 验的是
    //! "隔离到底生没生效"，跟"这次有没有跳过播种"根本不是一回事。
    QString info;
};

//! 见头注调用契约。返回本次（或首次）引导的记录。
ConfigIsolationReport bootstrapPersonalConfigDir();

//! 最近一次 `bootstrapPersonalConfigDir()` 的记录；从未调用 → 全默认值（isolated=false）。
const ConfigIsolationReport &configIsolationReport();

} // namespace stelapp
