/*
 * ConfigHealthCheck — T45-C：**配置健康/数据安全自检**（STELQUICK_CFGHEALTH_CHECK=1），
 *                     还的是测试文档 §6.3 的 P-CFG-01 与 P-CFG-04。
 *
 * ── 为什么需要一整套新判据，而不是给 `CONFIGCHECK`（T36）加两条 ────────────────
 * T36 只覆盖 P-CFG-**02**（个人版路径隔离）。P-CFG-01（**损坏**配置可恢复 + 明确
 * 提示）与 P-CFG-04（诊断页与启动日志一致）此前**没有任何判据**：
 *   · T36 的产品侧只处理了「个人版目录里**没有** config.ini」（兜底拷贝
 *     `data/default_cfg.ini`）—— "**有但坏了**"这一支从来没被想过（T45-A Q4b 代码事实）。
 *   · P-CFG-04 的诊断页字段与启动日志从没有过对照。
 *
 * ── 判据设计的第一原则：**注入的是真文件，不是桩**────────────────────────────
 * P-CFG-01 要求"写入损坏的个人版配置文件 ⇒ 启动可恢复"。要判它，必须真把损坏字节
 * 放到**个人版 config.ini** 的位置上，再跑一遍看产品怎么办。
 *
 * 做法（**零产品测试代码**）：外层脚本设 `STEL_USERDIR=<临时目录>/Stellarium`
 * （`StelFileMgr` 认这个变量，见 `StelFileMgr.cpp:69`），于是
 * `bootstrapPersonalConfigDir()` 推出的个人版目录 = `<临时目录>/Stellarium-quick`
 * —— **一个临时目录**。损坏字节由脚本写进那个目录里的 `config.ini`。
 * ⇒ 真实路径、真实文件、真实引导路径；**真实用户的配置一字节都不碰**。
 *
 * ⚠️ 刻意**不**在产品里实现 `STELQUICK_CFG_INJECT=<形态>`（`T45_CONFIG_SAFETY.zh_CN.md`
 * §3.3 当初设想的方案）：那等于让**出货二进制**带一段"往用户配置里写垃圾"的代码 ——
 * 为了测试给产品加一把能伤到用户的刀，不划算。注入归测试装置（脚本）管。
 * 铁律仍然成立：**不许造假的 QSettings / 假配置对象**来"验"产品行为
 * （陷阱 86：判据自建副本 ≠ 被测接线实例）。本 check **只读**文件，从不写。
 *
 * ── 形态表（脚本写什么 × 产品该报什么）────────────────────────────────────────
 *  `STELQUICK_CFGHEALTH_FORM` ∈ 下表。每个形态一份期望严重度，**理由写在表里**
 *  （没有理由的期望值就是拍脑袋 —— 陷阱 87：期望值必须实跑出来再写死）。
 *
 *  ⚠️ 期望值的**证据来源**是 T45-A 探针 Q2（`docs/evidence/2026-10-01-a6-configsafety/`）：
 *     QSettings 对五种损坏形态**全部静默 `NoError`** ⇒ 「回退默认」是 **Qt 行为**，
 *     不是产品行为。**因此本组判据不主张"回退默认"**，只主张两件产品该负责的事：
 *       ① 坏成这样**还起得来**（可判：引导完成 + 窗口在）；
 *       ② **该说的要说出来**（可判：严重度 + 状态行）。
 *     并把"产品**测不出**的形态"如实钉成 **必须报正常**（不许谎报）—— 这才是诚实。
 *
 *  🔴 2026-10-01 实测推翻了一条前提：**"配置不完整"会让引擎 SIGSEGV（rc=139）**
 *     —— 0 键、99 键（截断）、1 键**都崩**，崩点恒为
 *     `LandscapeMgr: initialized Cache for 100 MB.` 之后（与"空用户目录"同一崩点）。
 *     所以"损坏"不是显示问题：**不修就起不来**。产品因此补了**引导修复**腿
 *     （备份 `.corrupt` + 重建为随包默认），判据 CH-09 单独记账这条腿。
 *
 *  | form      | 脚本写什么                          | 期望严重度 | 为什么 |
 *  |-----------|-------------------------------------|-----------|--------|
 *  | none      | 不写（目录留空，产品自己兜底播种）    | 1 正常    | 基线：播种出的 `default_cfg.ini` 键数 == 随包默认 ⇒ **不许修** |
 *  | truncate  | 取默认配置前 40%（99 键）             | 3 错误    | < 252 键 ⇒ 不完整 ⇒ 引导修复 |
 *  | badline   | 一行垃圾 + 一个合法键（1 键）         | 3 错误    | 同上 |
 *  | badutf8   | 合法行 + 值里混非法 UTF-8（1 键）     | 3 错误    | 同上 |
 *  | binary    | 256 字节 0x00–0xFF（0 键）            | 3 错误    | 同上 |
 *  | empty     | 0 字节                               | 3 错误    | 同上 |
 *  | readonly  | 拷默认配置后 chmod 444（252 键，完整） | 3 错误    | **不可写** ⇒ 改动会丢 ⇒ 报错；内容完整 ⇒ **不需要也不许修** |
 *
 *  —— 「静默形态必须报正常」不是"放它一马"，而是一条**实质性断言**：
 *     若产品把 AccessError 之类判成正常（假绿），或反过来在 NoError 时喊狼来了（假红），
 *     本组立刻红。**判别性**由此成立（陷阱 4：孤立断言不算证据）。
 *
 * ── 判据逐条（台账 id 用显式列表；台账 id 必须逐字一致 —— 陷阱 85）───────────
 *  CH-01 注入自证（**前提判据**）：注入的字节（sha256 + 字节数）能在**原处
 *        `config.ini`** 或**`.corrupt` 备份**里找到 ⇒ 判的确实是脚本造的那份。
 *        两处都没有 ⇒ 整个 run **UNAVAILABLE**（陷阱 3：仪器没接在实况上）。
 *  CH-02 **启动可恢复**：`engineBooted` ∧ 引导记录 `isolated` ∧ personalDir 以
 *        `-quick` 结尾 ∧ 窗口非空且可见。★ 这是 P-CFG-01 前半句的**产品**可判面。
 *        不主张"回退默认"（那是 Qt 的语义，见上）；产品主张的是"**不修就起不来**"。
 *  CH-03 **提示出口已接**：`configStatus != -1`（产品**真的读了**配置健康 —— T45-A Q4b
 *        证实 T45 之前引导路径上从未读过）∧ `configSeverity ∈ {1,2,3}` ∧ 文案非空。
 *  CH-04 **产品读数与磁盘事实一致**：引导记录里的 `configFileBytes` == 判据自己读
 *        **产品当时读的那份文件**的字节数（修过 ⇒ 比 `.corrupt` 备份；只对照**原始量**，
 *        **不重算严重度** —— 重算就是自建副本，陷阱 86）。
 *  CH-05 **严重度符合形态期望**（上表）。⭐ 这条同时管两件事：可检出形态必须报出来；
 *        完好的基线**必须**报正常。
 *  CH-06 **状态行渲染一致**：`ErrorModel` 的「配置文件」行存在，其 `state` 与严重度
 *        的映射一致（0→info 1→ok 2→warn 3→error），且 `value` == 引导期文案。
 *  CH-07 **诊断页 ↔ 启动日志一致（P-CFG-04）**：读 `STELQUICK_BOOTLOG` 指向的日志
 *        文本，**按文本解析**出 `probe ok= device= api= driver= portability_driver=`
 *        与 `runtimeApi= backendOk=`，逐项与 `BackendInfo` 的 getter 相等。
 *        ⚠️ 它证明的是"**诊断页读的字段与启动日志打的文本同值**"（两处渲染一致），
 *        **不**证明底层探针结论正确 —— 那是 `BackendInfo` 自己的事（T39 `HIDPICHECK`）。
 *        未提供 `STELQUICK_BOOTLOG` 或日志里没有这两行 ⇒ **本项 UNAVAILABLE**（单列，
 *        不许静默跳过、也不许记 FAIL）。
 *  CH-08 **交换链信息如实缺席**：`BackendInfo` 的 Q_PROPERTY 名里**不得**出现
 *        `swap`（大小写无关）。测试文档 P-CFG-04 的原话含"交换链"，但本形态诊断页
 *        **没有**它，且这是**刻意的**（交换链属 QML/Vulkan 后端，产品侧拿不到；
 *        契约第 1 条禁止 QML 侧接触 Vulkan 记号）⇒ 如实记 N/A 并把它钉成断言，
 *        免得日后有人"补"一个近似量进来充数（陷阱 67/75）。
 *  CH-09 **引导修复的账目**（两个方向都判）：
 *        ① 该修的形态 ⇒ `configRepaired` ∧ 备份存在 ∧ **注入字节完整躺在备份里**
 *           （"修复没丢用户数据"的可判面）；
 *        ② `none` / `readonly` ⇒ **不许修**（防"顺手把好配置也换掉"，陷阱 90）。
 *
 * ── 负控（差异只在环境；期望值实跑出来再写死，陷阱 87）──────────────────────
 *  · `STELQUICK_CFG_ISOLATE_OFF=1`（+ form=none）关 T36 隔离 ⇒ `personalDir` 退成原目录。
 *  · `STELQUICK_CFG_STATUS_OFF=1`（+ truncate）关掉**健康报告**（修复照常）。
 *  · `STELQUICK_CFG_REPAIR_OFF=1`（+ truncate）关掉**引导修复** ⇒ **引擎应崩**（rc≠0、
 *    无 VERDICT）—— 这正是"修复腿承重"的铁证；期望值按"rc≠0 且无判据输出"写死。
 *  · `STELQUICK_CFGHEALTH_DRIFT=1`（+ form=none）main.cpp 在打印探针行**之后**改掉
 *    `BackendInfo` 的 deviceName ⇒ 制造"日志与诊断面漂移"（CH-07 的判别对照）。
 *
 * ── 纪律（T40/T41/T42/T44 血泪）──────────────────────────────────────────────
 *  · 收割回调必须是 `Ctx` 的成员（陷阱 84：漏赋值 ⇒ 零判据输出）。
 *  · 判据只许验**被声称的命题**（陷阱 75）。
 *  · 步进结构一律 `waitBefore` 语义（陷阱 102：把"等待"写在采样之后 ⇒ 读数的时间窗
 *    根本不是你以为的那个 ⇒ 一整套互相"矛盾"的假读数）。
 *  · 本 check **不写任何文件**（除了读），不启帧泵（配置面与帧无关，少一个 Metal
 *    环节少一份掉设备风险）。
 */
#pragma once

#include <QString>
#include <QStringList>

#include <functional>

class QCoreApplication;
class QQuickWindow;

namespace stelapp
{

class BackendInfo;
class ErrorModel;

class ConfigHealthCheck
{
public:
    struct Result
    {
        bool ran = false;
        //! 前提不齐（形态名未给 / 注入自证失败 / 无引擎）。**不记 FAIL**。
        bool unavailable = false;
        bool pass = false;
        int passed = 0;
        int total = 0;
        //! 单项 UNAVAILABLE 的条数（如 CH-07 没拿到启动日志）—— 与整体
        //! unavailable 区分：整体可用但个别项前提不齐时，这项**既不算过也不算错**。
        int skipped = 0;
        QString summary;
        QStringList details;
    };

    //! @p engineBooted 由装配方传入（调用方知道 boot 到底成没成）。
    static void run(QCoreApplication *app,
                    QQuickWindow *window,
                    BackendInfo *backend,
                    ErrorModel *model,
                    bool engineBooted,
                    const std::function<void(const Result &)> &onDone,
                    int delayMs = 1200);
};

} // namespace stelapp
