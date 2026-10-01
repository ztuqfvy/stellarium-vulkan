/*
 * ConfigHealthProbe — T45-A **配置健康 / 资源链接数据面探针**
 *                      （STELQUICK_CONFIG_HEALTH_PROBE=1）。
 *
 * ── 这个探针要还的账 ─────────────────────────────────────────────────────────
 * 测试文档 §6.3「配置与数据安全」四条里，T36 只做掉了 P-CFG-02（路径隔离，判据
 * 落在 `ConfigIsolationCheck` CFG-03/04/06/07）。余下三条**一直没有判据**：
 *   P-CFG-01 写入**损坏**的个人版配置文件 ⇒ 启动可恢复（回退默认或引导修复）+ **明确提示**
 *   P-CFG-03 资源目录保护：只读资源链接不被生成工具改写；materialize 前禁止写链接目录
 *   P-CFG-04 渲染诊断页显示的 API/GPU/交换链信息与**启动日志**一致
 * 而 T36 的产品侧只处理了「个人版目录里**没有** config.ini」（兜底拷贝
 * `data/default_cfg.ini`，见 `ConfigIsolation.hpp` 的"第二条腿"）——
 * **"有但坏了"这一支从来没被想过**，所以 P-CFG-01 的后半句（明确提示）大概率是缺口。
 *
 * ── 开工前必须实测的事实（凭想象写判据必然假绿/假红）────────────────────────
 *   Q1  现状读数：个人版 config.ini 的路径/大小/md5/键数；原目录那一份同样读数。
 *   Q2  ★ **QSettings 对"损坏"到底是什么语义**（在 /tmp 的**副本**上做，绝不动用户配置）：
 *       (a) 截断在键值中间   (b) 整文件二进制垃圾   (c) 坏行 + 后面接合法键
 *       (d) 非法 UTF-8 字节   (e) 空文件
 *       逐个报 `QSettings::status()`（NoError/FormatError/AccessError）+
 *       `allKeys().size()` + "已知键读得到吗"。
 *       ⇒ 这决定 P-CFG-01 怎么判：若 QSettings 坏行跳过、好行照读，
 *         "回退默认"就不是产品行为而是 Qt 行为，判据不能把 Qt 的功劳记在产品头上
 *         （陷阱 75：只许验"被声称的命题"）。
 *   Q3  只读 config.ini：`QSettings::sync()` 会怎样（AccessError？静默？）；
 *       产品有没有任何出口让人看出来。
 *   Q4  **"明确提示"的现有出口盘点**：`ErrorModel` 的状态行里有没有"配置健康"这一项；
 *       引导期 stdout/日志有没有任何配置异常提示。⇒ 决定 T45 要不要写产品。
 *   Q5  资源链接现状（P-CFG-03 关心的九个）：逐个报 `lstat` 是否符号链接、
 *       `readlink` 的 target、target 是否存在、target 的 mtime（判"有没有被改写"）。
 *   Q6  `docs/vulkan/source-manifest.json` 的资源清单规模（脚本 `source-snapshot.mjs
 *       verify` 的结论由外部脚本给，探针只报自己读到的清单条数，供交叉核对）。
 *
 * ── 写入面（受控，且全部落在 /tmp）──────────────────────────────────────────
 *   Q2/Q3 会在 `QDir::tempPath()` 下建独立临时文件（每个形态一个），逐个 rm。
 *   **绝不触碰**个人版或原版的真实 config.ini —— 这是本探针的第一条纪律。
 */
#pragma once

#include <QString>
#include <QStringList>

#include <functional>

class QCoreApplication;

namespace stelapp {

class ConfigHealthProbe
{
public:
    struct Result
    {
        bool ran = false;
        bool unavailable = false;
        QString summary;
        QStringList details;
    };

    static void run(QCoreApplication *app,
                    const std::function<void(const Result &)> &onDone,
                    int delayMs = 800);
};

} // namespace stelapp
