// ConfigHealthProbe 实现。问题表与纪律见 ConfigHealthProbe.hpp（同一套口径）。
#include "app/ConfigHealthProbe.hpp"

#if defined(STELQUICK_HAS_ENGINE)

#include "core/StelFileMgr.hpp"

#include <QByteArray>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QTimer>
#include <QVariant>
#include <QVector>

#include <cstdio>
#include <memory>

namespace stelapp {

namespace {

using ProbeResult = ConfigHealthProbe::Result;

struct Ctx;
struct Step
{
    int delayAfter = 0;
    std::function<void(Ctx *)> body;
};

struct Ctx
{
    QCoreApplication *app = nullptr;
    ProbeResult result;
    QStringList tempFiles;   //!< 收尾要删的临时文件（绝不碰真实配置）

    void note(const QString &s) { result.details << s; }
    void finish()
    {
        result.summary = QStringLiteral("配置健康/资源链接探针：%1 条读数（只读；写入面全在 /tmp）")
                             .arg(result.details.size());
        if (result_cb)
            result_cb(result);
    }
    std::function<void(const ProbeResult &)> result_cb;
};

//! 人类可读的字节数（⚠️ 首版只返回 "%1 B" —— 名字叫 humanBytes 却不做换算，
//! 属"名不符实"同族，改掉）
QString humanBytes(qint64 n)
{
    if (n < 1024)
        return QStringLiteral("%1 B").arg(n);
    if (n < 1024 * 1024)
        return QStringLiteral("%1 KiB").arg(double(n) / 1024.0, 0, 'f', 1);
    return QStringLiteral("%1 MiB").arg(double(n) / (1024.0 * 1024.0), 0, 'f', 2);
}

//! 一行"这个配置文件长什么样"：路径 / 大小 / md5 / QSettings 能读到的键数
QString fileFact(const QString &path)
{
    QFileInfo fi(path);
    if (!fi.exists())
        return QStringLiteral("不存在（%1）").arg(path);
    return QStringLiteral("%1｜%2｜mtime=%3｜键数=%4")
        .arg(path)
        .arg(humanBytes(fi.size()))
        .arg(fi.lastModified().toString(QStringLiteral("MM-dd HH:mm:ss")))
        .arg(QSettings(path, QSettings::IniFormat).allKeys().size());
}

//! 在 /tmp 造一个"损坏形态"的配置文件，返回路径（调用方负责登记以删除）
QString writeTemp(Ctx *c, const QString &tag, const QByteArray &bytes)
{
    const QString path = QDir::tempPath() + QStringLiteral("/a6-cfghealth-%1.ini").arg(tag);
    // 先清掉上次残留（尤其 S3 会把文件改成只读 ⇒ open(Truncate) 会失败，
    // 于是"写进去的字节"根本不是本次造的 —— 静默假绿）。
    QFile::remove(path);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        c->note(QStringLiteral("⚠️ %1 写入失败（%2）—— 本形态读数不可信").arg(tag, f.errorString()));
    else
    {
        f.write(bytes);
        f.close();
    }
    c->tempFiles << path;
    return path;
}

//! 对某个"损坏文件"报 QSettings 的三项读数
QString probeQSettings(const QString &path, const QString &probeKey)
{
    QSettings s(path, QSettings::IniFormat);
    const QSettings::Status st = s.status();
    const char *stName = st == QSettings::NoError
                             ? "NoError"
                             : (st == QSettings::FormatError ? "FormatError" : "AccessError");
    const QStringList keys = s.allKeys();
    const QVariant v = s.value(probeKey);
    return QStringLiteral("status=%1｜键数=%2｜\"%3\"=%4")
        .arg(QLatin1String(stName))
        .arg(keys.size())
        .arg(probeKey)
        .arg(v.isValid() ? QStringLiteral("有值(%1)").arg(v.toString())
                         : QStringLiteral("无（QSettings 回退到调用方默认）"));
}

} // namespace

void ConfigHealthProbe::run(QCoreApplication *app,
                            const std::function<void(const Result &)> &onDone,
                            int delayMs)
{
    auto ctx = std::make_shared<Ctx>();
    ctx->app = app;
    ctx->result.ran = true;
    ctx->result_cb = onDone;

    auto steps = std::make_shared<QVector<Step>>();
    auto tick = std::make_shared<std::function<void(int)>>();
    auto runStep = [steps, ctx, tick](int i) {
        if (i >= steps->size())
        {
            ctx->finish();
            return;
        }
        const Step &st = steps->at(i);
        st.body(ctx.get());
        if (st.delayAfter > 0)
            QTimer::singleShot(st.delayAfter, ctx->app, [tick, i]() { (*tick)(i + 1); });
        else
            (*tick)(i + 1);
    };
    *tick = [runStep](int i) { runStep(i); };
    QTimer::singleShot(delayMs, app, [runStep]() { runStep(0); });

    // ── S1 Q1：现状读数（个人版 vs 原目录）─────────────────────────────────
    steps->append(Step{0, [](Ctx *c) {
        const QString personal = StelFileMgr::getUserDir();
        QString original = personal;
        if (original.endsWith(QStringLiteral("-quick")))
            original.chop(QStringLiteral("-quick").size());
        c->note(QStringLiteral("Q1 个人版目录：%1").arg(personal));
        c->note(QStringLiteral("Q1 个人版 config.ini：%1").arg(fileFact(personal + QStringLiteral("/config.ini"))));
        c->note(QStringLiteral("Q1 原版目录（推得）：%1").arg(original));
        c->note(QStringLiteral("Q1 原版 config.ini：%1").arg(fileFact(original + QStringLiteral("/config.ini"))));
        const QString def = StelFileMgr::findFile(QStringLiteral("data/default_cfg.ini"));
        c->note(QStringLiteral("Q1 兜底源 default_cfg.ini：%1").arg(fileFact(def)));
        c->note(QStringLiteral("Q1 安装目录：%1｜当前工作目录：%2")
                    .arg(StelFileMgr::getInstallationDir())
                    .arg(QDir::currentPath()));
    }});

    // ── S2 Q2：QSettings 对五种"损坏"的语义（全在 /tmp，副本）─────────────
    steps->append(Step{0, [](Ctx *c) {
        const QString probeKey = QStringLiteral("flag_constellation_drawing");

        // 拿真配置的前若干字节做"截断"，比凭空造更贴近现实
        const QString personal = StelFileMgr::getUserDir();
        QByteArray seed;
        {
            QFile f(personal + QStringLiteral("/config.ini"));
            if (f.open(QIODevice::ReadOnly))
                seed = f.read(4096);
        }
        if (seed.isEmpty())
            seed = QByteArray("[General]\nflag_constellation_drawing = true\n");
        const int cut = seed.size() / 2;
        const int lineStart = seed.lastIndexOf('\n', cut) + 1;

        struct Case { const char *tag; const char *desc; QByteArray bytes; };
        const QVector<Case> cases = {
            {"trunc", "① 截断在键值行中间（取真配置前半 + 半行）",
             seed.left(lineStart) + seed.mid(lineStart, 20)},
            {"bin", "② 整文件二进制垃圾（0x00–0xFF 循环 256 字节）",
             [] { QByteArray b; for (int i = 0; i < 256; ++i) b.append(char(i)); return b; }()},
            {"badline", "③ 坏行 + 后面接合法键",
             QByteArray("!!!garbage!!!not-ini\n[Valid]\nflag_constellation_drawing=true\n")},
            {"utf8", "④ 非法 UTF-8 字节混在值里",
             QByteArray("[Valid]\nflag_constellation_drawing=") + QByteArray::fromHex("fffe80") + "\n"},
            {"empty", "⑤ 空文件（0 字节）", QByteArray()},
        };
        for (const Case &cs : cases)
        {
            const QString p = writeTemp(c, QLatin1String(cs.tag), cs.bytes);
            c->note(QStringLiteral("Q2 %1 ⇒ %2")
                        .arg(QString::fromUtf8(cs.desc))   // desc 是 UTF-8 源码字面量；QLatin1String 会 mojibake
                        .arg(probeQSettings(p, probeKey)));
        }
        c->note(QStringLiteral("Q2 判读：若坏行被跳过而好行照读（键数>0 且 NoError），"
                               "则「回退默认」是 **Qt 行为**，不是产品行为 —— "
                               "P-CFG-01 不能把 Qt 的功劳记在产品头上（陷阱 75）。"
                               "产品该负责的是后半句「**给出明确提示**」。"));
    }});

    // ── S3 Q3：只读配置文件的写入语义 ──────────────────────────────────────
    steps->append(Step{0, [](Ctx *c) {
        const QString p = writeTemp(c, QStringLiteral("ro"),
                                    QByteArray("[General]\nflag_constellation_drawing=true\n"));
        QFile::setPermissions(p, QFile::ReadOwner | QFile::ReadGroup | QFile::ReadOther);
        QSettings s(p, QSettings::IniFormat);
        s.setValue(QStringLiteral("a6_probe_sentinel"), QStringLiteral("1"));
        s.sync();
        const QSettings::Status st = s.status();
        c->note(QStringLiteral("Q3 只读文件 setValue+sync ⇒ status=%1（AccessError 才说明 Qt 明确报错）"
                               "｜文件大小 %2（读回）")
                    .arg(st == QSettings::NoError ? QStringLiteral("NoError")
                                                  : (st == QSettings::FormatError
                                                         ? QStringLiteral("FormatError")
                                                         : QStringLiteral("AccessError")))
                    .arg(QFileInfo(p).size()));
        // 顺带：文件本身可不可写（**不是** `QSettings::isWritable()` —— 那是静态方法，
        // 答的是"QSettings 全局能不能写"，与 p 无关；这里要的是"p 可不可写"）
        c->note(QStringLiteral("Q3b 参考：`QFileInfo(p).isWritable()`=%1"
                               "（注：`QSettings::isWritable()` 是**静态**方法，"
                               "答的是全局状态，不能拿来判单个文件）")
                    .arg(QFileInfo(p).isWritable() ? 1 : 0));
    }});

    // ── S4 Q4：「明确提示」的现有出口盘点 ─────────────────────────────────
    steps->append(Step{0, [](Ctx *c) {
        // 引导期是否已经过去了 —— 配置异常若在引导期只打日志，产品侧就无从提示。
        // 本探针由装配方在引擎 boot 成功**之后**启动（main.cpp 装配块保证，
        // boot 失败直接走 UNAVAILABLE 分支）⇒ 引导期已过，无需运行时再判。
        c->note(QStringLiteral("Q4 引擎已引导=是（装配方保证：boot 失败不会到达本探针）"
                               "⇒ 引导期是否做过配置健康检查，"
                               "看 `ConfigIsolation` 的记录通道（note/info）"));
        c->note(QStringLiteral("Q4b 现有提示出口（代码事实，逐条核对）："
                               "① `ConfigIsolation::Record::note`（降级/异常通道，只在"
                               "setUserDir 抛异常或兜底拷贝失败时才有值）；"
                               "② `ErrorModel::statusRows`（4 行：引擎引导/图形后端/配置目录/帧通路）"
                               "—— **没有「配置健康」这一行**；"
                               "③ `QSettings::status()` 在**正常引导路径上从未被读取**。"
                               "⇒ 结论（待 T45-B 复核）：**「有但坏了」这一支当前无任何提示出口**。"));
    }});

    // ── S5 Q5：资源链接现状（P-CFG-03）────────────────────────────────────
    steps->append(Step{0, [](Ctx *c) {
        // 九个资源目录（见 tools/materialize-resources.mjs 的清单）
        const QStringList res = {"models", "textures", "landscapes", "skycultures",
                                 "nebulae", "stars", "atmosphere", "scenery3d", "po"};
        const QString root = QDir::currentPath();
        int links = 0, dirs = 0, missing = 0;
        QStringList weird;
        for (const QString &r : res)
        {
            const QString p = root + QLatin1Char('/') + r;
            const QFileInfo fi(p);
            if (!fi.exists())
            {
                ++missing;
                weird << QStringLiteral("%1=缺失").arg(r);
            }
            else if (fi.isSymLink())
            {
                ++links;
                const QString tgt = fi.symLinkTarget();
                weird << QStringLiteral("%1→%2%3").arg(r, tgt,
                             QFileInfo(tgt).exists() ? QString() : QStringLiteral("(**target 不存在!**)"));
            }
            else
            {
                ++dirs;
                weird << QStringLiteral("%1=实体目录").arg(r);
            }
        }
        c->note(QStringLiteral("Q5 资源占位：符号链接 %1 / 实体目录 %2 / 缺失 %3（根=%4）")
                    .arg(links).arg(dirs).arg(missing).arg(root));
        c->note(QStringLiteral("Q5a ⚠️ 上表的根 = **进程工作目录**（%1），不是安装目录"
                               "（%2）⇒ 跑本探针必须 `cd` 到仓库根，否则读数恒为「缺失」。"
                               "另：清单外的 3 个链接（plugins/scripts/util）不在此表，"
                               "由 tools/a6-resource-links-check.mjs §1b 兜。")
                    .arg(root, StelFileMgr::getInstallationDir()));
        c->note(QStringLiteral("Q5b 逐项：%1").arg(weird.join(QStringLiteral("｜"))));
        c->note(QStringLiteral("Q5c 判读：P-CFG-03 的「链接未被生成工具改写」= 上表恒为"
                               "「符号链接」且 target 存在；materialize 前写链接目录属流程禁止项，"
                               "由 tools/ 脚本层把关（本探针只报现状）。"));
    }});

    // ── S6 Q6：资源清单规模 ────────────────────────────────────────────────
    steps->append(Step{0, [](Ctx *c) {
        const QString mf = QDir::currentPath() + QStringLiteral("/docs/vulkan/source-manifest.json");
        const QFileInfo fi(mf);
        c->note(QStringLiteral("Q6 source-manifest.json：%1｜%2")
                    .arg(fi.exists() ? humanBytes(fi.size()) : QStringLiteral("不存在"))
                    .arg(mf));
        c->note(QStringLiteral("Q6b 校验结论由 `node tools/source-snapshot.mjs verify` 给"
                               "（探针不复述别人的结论 —— 陷阱 86：判据自建副本 ≠ 被测接线实例）"));
    }});

    // ── 收尾：删掉全部临时文件（探针零残留 —— 陷阱 90/94）─────────────────
    steps->append(Step{0, [](Ctx *c) {
        int removed = 0;
        for (const QString &p : c->tempFiles)
            if (QFile::remove(p))
                ++removed;
        c->note(QStringLiteral("收尾：/tmp 临时文件 %1/%2 已删除（真实配置全程零触碰）")
                    .arg(removed).arg(c->tempFiles.size()));
    }});

    std::fprintf(stderr, "CFGHEALTHPROBE: 配置健康探针开跑（delayMs=%d）\n", delayMs);
    std::fflush(stderr);
}

} // namespace stelapp

#endif // STELQUICK_HAS_ENGINE
