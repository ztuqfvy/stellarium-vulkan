// ConfigHealthCheck 实现。判据表、形态表与纪律见 ConfigHealthCheck.hpp（同一套口径）。

#include "ui/ConfigHealthCheck.hpp"

#include "app/ConfigIsolation.hpp"
#include "app/ErrorModel.hpp"
#include "ui/quick/BackendInfo.hpp"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMetaObject>
#include <QMetaProperty>
#include <QPair>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QSet>
#include <QTimer>
#include <QVariantMap>
#include <QVector>

#include <cstdio>
#include <memory>

namespace stelapp
{

namespace
{

// ── 形态表：脚本写什么 × 产品该报什么 ────────────────────────────────────────
//    期望严重度**逐条带理由**（没理由的期望值就是拍脑袋）；证据来源 = T45-A 探针
//    Q2 的五形态实测（全 NoError）+ Q3b（只读文件要显式查 isWritable）。
struct FormSpec
{
    const char *id;      //!< STELQUICK_CFGHEALTH_FORM
    int expectSev;       //!< 期望严重度（0=未探测 1=正常 2=警告 3=错误）
    const char *why;     //!< 为什么是这个值
};

const FormSpec kForms[] = {
    {"none", 1,
     "基线：脚本不写（个人版目录空 ⇒ 产品兜底播种 data/default_cfg.ini ⇒ 键数=随包默认）"},
    {"full", 1,
     "只用于 ISOLATE_OFF 负控：把**完整**随包默认配置放进**原目录**（隔离关掉后产品用的"
     "就是那份）⇒ 与 none 同级：正常、**不许修**"},
    {"truncate", 3, "默认配置前 40% ⇒ 99 键 < 252 ⇒ **不完整** ⇒ 引导修复（实测不修则 139）"},
    {"badline", 3, "一行垃圾 + 一个合法键 ⇒ 1 键 < 252 ⇒ 不完整 ⇒ 同上"},
    {"badutf8", 3, "合法行 + 值里混非法 UTF-8 ⇒ 1 键 < 252 ⇒ 不完整 ⇒ 同上"},
    {"binary", 3, "256 字节 0x00–0xFF ⇒ 0 键 < 252 ⇒ 不完整 ⇒ 同上"},
    {"empty", 3, "0 字节 ⇒ 0 键 < 252 ⇒ 不完整 ⇒ 同上"},
    {"readonly", 3, "chmod 444 ⇒ `QFileInfo::isWritable()` 假（252 键本身完整，修不了也不需要修）"},
};

const FormSpec *findForm(const QString &id)
{
    for (const FormSpec &f : kForms)
        if (id == QLatin1String(f.id))
            return &f;
    return nullptr;
}

//! 严重度 → 状态行的 state 字符串（**唯一映射**，判据与产品两侧都照它比）。
QString sevToState(int sev)
{
    switch (sev)
    {
    case 1: return QStringLiteral("ok");
    case 2: return QStringLiteral("warn");
    case 3: return QStringLiteral("error");
    default: return QStringLiteral("info");
    }
}

// ── 启动日志解析（P-CFG-04 的"另一侧"）──────────────────────────────────────
//    ⚠️ 刻意**读日志文本再解析**，而不是去读 BackendInfo 的内部字段：
//       本判据要证的是"**诊断页与启动日志同值**"，两侧必须是**两条渲染路径**。
struct LogFacts
{
    bool haveProbe = false;
    bool haveRuntime = false;
    bool probeOk = false;
    QString device, api, driver, err;
    bool portabilityDriver = false;
    QString runtimeApi;
    bool backendOk = false;
};

LogFacts parseBootLog(const QString &text)
{
    LogFacts f;
    QRegularExpression probeRe(QStringLiteral(
        "STELQUICK: probe ok=(\\d) device=(.*?) api=(\\S+) driver=(\\S+)"
        " portability_driver=(\\d) portability_enum_ext=(\\d) err=(.*)"));
    const QRegularExpressionMatch pm = probeRe.match(text);
    if (pm.hasMatch())
    {
        f.haveProbe = true;
        f.probeOk = pm.captured(1) == QLatin1String("1");
        f.device = pm.captured(2);
        f.api = pm.captured(3);
        f.driver = pm.captured(4);
        f.portabilityDriver = pm.captured(5) == QLatin1String("1");
        f.err = pm.captured(7);
    }
    QRegularExpression rtRe(QStringLiteral("STELQUICK: runtimeApi=(\\S+) backendOk=(\\d) device="));
    const QRegularExpressionMatch rm = rtRe.match(text);
    if (rm.hasMatch())
    {
        f.haveRuntime = true;
        f.runtimeApi = rm.captured(1);
        f.backendOk = rm.captured(2) == QLatin1String("1");
    }
    return f;
}

struct Ctx
{
    QCoreApplication *app = nullptr;
    QQuickWindow *window = nullptr;
    BackendInfo *backend = nullptr;
    ErrorModel *model = nullptr;
    bool engineBooted = false;
    std::function<void(const ConfigHealthCheck::Result &)> onDone;   //!< 第一句赋值（陷阱 84）

    ConfigHealthCheck::Result result;
    QVector<QPair<QString, bool>> items;   //!< 台账：显式 id 列表
    QSet<QString> marked;
    QStringList idOrder;
    int skipped = 0;

    void mark(const QString &id, bool ok, const QString &detail)
    {
        marked.insert(id);
        for (auto &it : items)
            if (it.first == id)
                it.second = ok;
        result.details << QStringLiteral("  [%1] %2")
                              .arg(ok ? QStringLiteral("PASS") : QStringLiteral("FAIL"), detail);
    }
    void skip(const QString &id, const QString &detail)
    {
        marked.insert(id);
        ++skipped;
        result.details << QStringLiteral("  [SKIP] %1").arg(detail);
    }
    void note(const QString &detail) { result.details << QStringLiteral("  · ") + detail; }

    QString personalConfig() const;
    void finish();
};

QString Ctx::personalConfig() const
{
    return configIsolationReport().personalDir + QStringLiteral("/config.ini");
}

void Ctx::finish()
{
    int pass = 0;
    for (const auto &it : items)
        if (it.second)
            ++pass;
    result.ran = true;
    result.passed = pass;
    result.total = items.size();
    result.skipped = skipped;
    // ⚠️ 前提不齐的项**既不算过也不算错**（项目纪律：环境门第三态 UNAVAILABLE 不记 FAIL）
    //   ⇒ 判"通过"要求 `passed + skipped >= total`，且未记账的 id 另有自证。
    result.pass = (pass + skipped >= items.size());
    QStringList never;
    for (const auto &it : items)
        if (!marked.contains(it.first))
            never << it.first;
    const QString formId = qEnvironmentVariable("STELQUICK_CFGHEALTH_FORM");
    result.summary = QStringLiteral("配置健康自检（form=%1）：判据 %2/%3")
                         .arg(formId.isEmpty() ? QStringLiteral("(未给)") : formId)
                         .arg(pass)
                         .arg(items.size());
    if (skipped > 0)
        result.summary += QStringLiteral("｜%1 项前提不齐（UNAVAILABLE，不记 FAIL）").arg(skipped);
    if (!never.isEmpty())
        result.summary += QStringLiteral("｜⚠️ 未被记账的 id：%1")
                              .arg(never.join(QStringLiteral(",")));
    if (onDone)
        onDone(result);
}

// ── 注入自证：判的确实是脚本造的那份文件 ────────────────────────────────────
//    ⚠️ 产品**引导修复**会把不完整的 config.ini 改名成 `config.ini.corrupt` 再重建，
//      所以"注入的字节还在不在"要**两处都找**：
//        · 还在 `config.ini` ⇒ 未修复（只读形态就是这样）
//        · 搬到了 `config.ini.corrupt` ⇒ **修复发生了，且原始字节一个没丢**
//      两处都没有 ⇒ 仪器没接在实况上 ⇒ UNAVAILABLE（陷阱 3）。
//      这一条同时是 CH-09 的一半证据：备份里的 sha 与注入记录逐位相同 =
//      "修复没丢用户数据"的可判面。
enum class InjWhere { Missing, InPlace, InBackup };

struct InjVerdict
{
    InjWhere where = InjWhere::Missing;
    //! 注入字节**实际落在**的那个文件（原处 或 `.corrupt` 备份）。
    //! CH-04 用它做"产品读数 vs 磁盘事实"的对照 —— 产品读的是**修复之前**的那份，
    //! 修复之后 `config.ini` 已经换成默认配置，拿它比就是拿错了文件。
    QString matchedPath;
    QString why;
};

QString sha256Of(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return QString();
    return QString::fromLatin1(
        QCryptographicHash::hash(f.readAll(), QCryptographicHash::Sha256).toHex());
}

InjVerdict injectedWhere(const QString &cfgPath, bool bytesMustSurvive)
{
    InjVerdict v;
    const QFileInfo fi(cfgPath);
    if (!fi.exists())
    {
        v.why = QStringLiteral("个人版 config.ini 不存在：") + cfgPath;
        return v;
    }
    // form=full（ISOLATE_OFF 负控专用）：注入的是**完整**配置 ⇒ 引擎起得来就必然
    // 把它整体重写（771 键 > 随包 252 键），且修复腿不触发 ⇒ 没有 .corrupt 备份。
    // ⇒ 注入字节**本来就不该存活**，自证降为"注入目标处文件存在且非空"。
    // form=none 走上面的空期望分支（同样只要求存在），与此处语义一致。
    if (!bytesMustSurvive)
    {
        if (fi.size() <= 0)
        {
            v.why = QStringLiteral("注入目标处文件存在但为 0 字节（form 不要求字节存活，"
                                   "但空文件说明注入没成）：") +
                    cfgPath;
            return v;
        }
        v.where = InjWhere::InPlace;
        v.matchedPath = cfgPath;
        v.why = QStringLiteral("form 不要求字节存活（引擎整体重写完整配置是预期）⇒ "
                               "自证=注入目标处存在非空文件（%1 字节）")
                    .arg(fi.size());
        return v;
    }
    const QString expBytes = qEnvironmentVariable("STELQUICK_CFGHEALTH_EXPECT_BYTES");
    const QString expSha = qEnvironmentVariable("STELQUICK_CFGHEALTH_EXPECT_SHA256");
    if (expBytes.isEmpty() && expSha.isEmpty())
    {
        v.where = InjWhere::InPlace;
        v.matchedPath = cfgPath;
        v.why = QStringLiteral("未提供期望字节数/sha256（form=none 基线）⇒ 只要求存在");
        return v;
    }
    const qint64 wantBytes = expBytes.isEmpty() ? -1 : expBytes.toLongLong();
    const QString want = expSha.toLower();
    const QString nowSha = sha256Of(cfgPath);

    // ① 原处（未修复）
    if ((wantBytes < 0 || fi.size() == wantBytes) && (want.isEmpty() || nowSha == want))
    {
        v.where = InjWhere::InPlace;
        v.matchedPath = cfgPath;
        v.why = QStringLiteral("原处匹配：字节 %1 + sha256 %2")
                    .arg(fi.size())
                    .arg(want.isEmpty() ? QStringLiteral("(未给)") : want.left(16));
        return v;
    }
    // ② 备份处（引导修复搬走了）
    const QString base = cfgPath + QStringLiteral(".corrupt");
    for (int k = 0; k < 10; ++k)
    {
        const QString bak = k == 0 ? base : base + QStringLiteral(".%1").arg(k);
        const QFileInfo bfi(bak);
        if (!bfi.exists())
            continue;
        const QString bsha = sha256Of(bak);
        if ((wantBytes < 0 || bfi.size() == wantBytes) && (want.isEmpty() || bsha == want))
        {
            v.where = InjWhere::InBackup;
            v.matchedPath = bak;
            v.why = QStringLiteral("**引导修复**：注入字节完整搬到了 %1（字节 %2 + sha256 %3）")
                        .arg(bfi.fileName())
                        .arg(bfi.size())
                        .arg(bsha.left(16));
            return v;
        }
    }
    v.why = QStringLiteral("原处与 .corrupt 备份都不匹配（原处 %1 字节 / sha %2；期望 %3 字节 / %4）")
                .arg(fi.size())
                .arg(nowSha.left(16))
                .arg(expBytes.isEmpty() ? QStringLiteral("?") : expBytes)
                .arg(want.isEmpty() ? QStringLiteral("?") : want.left(16));
    return v;
}

} // namespace

void ConfigHealthCheck::run(QCoreApplication *app,
                            QQuickWindow *window,
                            BackendInfo *backend,
                            ErrorModel *model,
                            bool engineBooted,
                            const std::function<void(const Result &)> &onDone,
                            int delayMs)
{
    auto ctx = std::make_shared<Ctx>();
    ctx->app = app;
    ctx->window = window;
    ctx->backend = backend;
    ctx->model = model;
    ctx->engineBooted = engineBooted;
    ctx->onDone = onDone;

    // 台账：**显式 id 列表**（陷阱 85：拼出来的身份一旦差一个字符就静默丢 mark）。
    ctx->idOrder = {QStringLiteral("CH-01"), QStringLiteral("CH-02"), QStringLiteral("CH-03"),
                    QStringLiteral("CH-04"), QStringLiteral("CH-05"), QStringLiteral("CH-06"),
                    QStringLiteral("CH-07"), QStringLiteral("CH-08"), QStringLiteral("CH-09")};
    for (const QString &id : ctx->idOrder)
        ctx->items.append(qMakePair(id, false));

    const QString formId = qEnvironmentVariable("STELQUICK_CFGHEALTH_FORM");
    const FormSpec *form = findForm(formId);

    QTimer::singleShot(delayMs, app, [ctx, formId, form]() {
        // ── S1 前提：形态名必须认识，且注入自证通过 ──────────────────────────
        //    ⚠️ delayMs 在**动作之前**等（T44 陷阱 102：把 wait 写在采样之后
        //      ⇒ 读数的时间窗根本不是你以为的那个）。本 check 只有一步，
        //      但语义仍写成"先等、后读"，与 LifeCycleCheck 同款。
        if (form == nullptr)
        {
            if (ctx->onDone)
                ctx->onDone(ConfigHealthCheck::Result{true, true, false, 0, 0, 0,
                                                      QStringLiteral("CFGHEALTHCHECK 未给/不认识的 "
                                                                     "STELQUICK_CFGHEALTH_FORM=%1")
                                                          .arg(formId),
                                                      {}});
            return;
        }
        if (ctx->window == nullptr)
        {
            ctx->onDone(ConfigHealthCheck::Result{
                true, true, false, 0, 0, 0,
                QStringLiteral("前提不齐：无 QQuickWindow（本 check 需要在合流形态下跑）"), {}});
            return;
        }

        const QString cfg = ctx->personalConfig();
        // 注入字节是否必须存活：损坏形态（truncate/badline/badutf8/binary/empty）
        // ⇒ 修复腿把原始字节冻结进 .corrupt 备份 ⇒ 必须找到；none/full ⇒ 引擎
        // 播种/整体重写都是预期 ⇒ 只要求注入目标处文件存在（见 injectedWhere 头注）。
        const bool bytesMustSurvive = (formId != QStringLiteral("none") &&
                                       formId != QStringLiteral("full"));
        const InjVerdict inj = injectedWhere(cfg, bytesMustSurvive);
        if (inj.where == InjWhere::Missing)
        {
            ConfigHealthCheck::Result r;
            r.ran = true;
            r.unavailable = true;
            r.summary = QStringLiteral("前提不齐：注入自证失败 —— %1").arg(inj.why);
            r.details << QStringLiteral("  · form=%1 个人版 config=%2").arg(formId, cfg);
            if (ctx->onDone)
                ctx->onDone(r);
            return;
        }

        const ConfigIsolationReport &rep = configIsolationReport();
        const QFileInfo fi(cfg);

        ctx->note(QStringLiteral("布场：form=%1｜个人版目录 %2｜config %3（%4 字节）｜"
                                 "引擎已引导=%5｜窗口可见=%6")
                      .arg(formId, rep.personalDir, cfg)
                      .arg(fi.size())
                      .arg(ctx->engineBooted ? 1 : 0)
                      .arg(ctx->window->isVisible() ? 1 : 0));
        ctx->note(QStringLiteral("注入自证：%1").arg(inj.why));

        // ── CH-01 注入自证 ────────────────────────────────────────────────
        ctx->mark(QStringLiteral("CH-01"), true,
                  QStringLiteral("CH-01 注入自证：%1｜form=%2（期望严重度 %3 —— %4）")
                      .arg(inj.why)
                      .arg(formId)
                      .arg(form->expectSev)
                      .arg(QString::fromUtf8(form->why)));

        // ── CH-02 启动可恢复（P-CFG-01 前半句的产品可判面）────────────────
        const bool personalOk =
            rep.isolated && rep.personalDir.endsWith(QStringLiteral("-quick"));
        ctx->mark(QStringLiteral("CH-02"),
                  ctx->engineBooted && personalOk && ctx->window->isVisible(),
                  QStringLiteral("CH-02 启动可恢复：引擎已引导=%1｜隔离生效=%2｜"
                                 "personalDir=%3（应以 -quick 结尾）｜窗口可见=%4"
                                 "｜引导修复=%5（备份 %6）"
                                 "｜⚠️ 不主张「回退默认」—— 那是 Qt 的语义（探针 Q2）；"
                                 "产品主张的是「**不修就起不来**，修了才起得来」")
                      .arg(ctx->engineBooted ? 1 : 0)
                      .arg(rep.isolated ? 1 : 0)
                      .arg(rep.personalDir)
                      .arg(ctx->window->isVisible() ? 1 : 0)
                      .arg(rep.configRepaired ? 1 : 0)
                      .arg(rep.configCorruptBackup.isEmpty()
                               ? QStringLiteral("(none)")
                               : QFileInfo(rep.configCorruptBackup).fileName()));

        // ── CH-03 提示出口已接 ────────────────────────────────────────────
        const bool sevValid = rep.configSeverity >= 1 && rep.configSeverity <= 3;
        ctx->mark(QStringLiteral("CH-03"),
                  rep.configStatus != -1 && sevValid && !rep.configHealthText.isEmpty(),
                  QStringLiteral("CH-03 提示出口已接：configStatus=%1（-1=未探测 ⇒ 应为实测值）"
                                 "｜severity=%2｜文案「%3」")
                      .arg(rep.configStatus)
                      .arg(rep.configSeverity)
                      .arg(rep.configHealthText));

        // ── CH-04 产品读数与磁盘事实一致（只比**原始量**）──────────────────
        //    ⚠️ 前提：**产品读的那份文件还躺在磁盘上**。
        //      · 引导修复 ⇒ 原始字节被冻结在 `.corrupt` 备份里 ⇒ 可以比 ✅
        //      · 未修复 ⇒ 引擎引导期会**依法重写** config.ini（实测 none 形态
        //        12721 → 39005 字节）⇒ 磁盘上已不是产品读的那份 ⇒ **前提不齐**，
        //        记 SKIP 而不是 FAIL（陷阱 3：这是仪器够不着，不是被测物错了；
        //        陷阱 4：差一个变量就断言 = 假红）。
        //      · 只对照**原始量**，**不重算严重度**（重算就是自建副本，陷阱 86）。
        {
            const QFileInfo mfi(inj.matchedPath);
            if (inj.where != InjWhere::InBackup)
            {
                ctx->skip(QStringLiteral("CH-04"),
                          QStringLiteral("CH-04 产品读数 vs 磁盘事实：跳过 —— 未修复形态下 "
                                         "config.ini 在引导期被引擎重写（磁盘已变），"
                                         "磁盘上不是产品读的那份 ⇒ 前提不齐（不记 FAIL）"));
            }
            else
            {
                ctx->mark(QStringLiteral("CH-04"), rep.configFileBytes == mfi.size(),
                          QStringLiteral("CH-04 产品读数 vs 磁盘事实：引导期 bytes=%1｜"
                                         "判据现读 %2（%3）｜引导期 keys=%4"
                                         "（不重算严重度 —— 那是自建副本，陷阱 86）")
                              .arg(rep.configFileBytes)
                              .arg(mfi.size())
                              .arg(inj.matchedPath)
                              .arg(rep.configParsedKeys));
            }
        }

        // ── CH-05 严重度符合形态期望 ──────────────────────────────────────
        ctx->mark(QStringLiteral("CH-05"), rep.configSeverity == form->expectSev,
                  QStringLiteral("CH-05 严重度 %1（期望 %2）—— %3")
                      .arg(rep.configSeverity)
                      .arg(form->expectSev)
                      .arg(QString::fromUtf8(form->why)));

        // ── CH-06 状态行渲染一致 ──────────────────────────────────────────
        {
            QString rowState, rowText;
            bool found = false;
            const QVariantList rows = ctx->model ? ctx->model->statusRows() : QVariantList{};
            for (const QVariant &v : rows)
            {
                const QVariantMap m = v.toMap();
                // ⚠️ 必须 QStringLiteral：QLatin1String 按 Latin-1 解读 UTF-8 字面量
                //   ⇒ 永不相等（2026-10-01 EC-16 实测踩过）。
                if (m.value(QStringLiteral("label")).toString() == QStringLiteral("配置文件"))
                {
                    found = true;
                    rowState = m.value(QStringLiteral("state")).toString();
                    rowText = m.value(QStringLiteral("value")).toString();
                    break;
                }
            }
            ctx->mark(QStringLiteral("CH-06"),
                      found && rowState == sevToState(rep.configSeverity) &&
                          rowText == rep.configHealthText,
                      QStringLiteral("CH-06 状态行渲染：行存在=%1｜state=%2（应由 severity=%3 "
                                     "映射为 %4）｜value「%5」（应 == 引导期文案）")
                          .arg(found ? 1 : 0)
                          .arg(rowState.isEmpty() ? QStringLiteral("(无)") : rowState)
                          .arg(rep.configSeverity)
                          .arg(sevToState(rep.configSeverity))
                          .arg(rowText));
        }

        // ── CH-07 诊断页 ↔ 启动日志一致（P-CFG-04）────────────────────────
        {
            const QString logPath = qEnvironmentVariable("STELQUICK_BOOTLOG");
            QString text;
            bool got = false;
            if (!logPath.isEmpty())
            {
                QFile f(logPath);
                if (f.open(QIODevice::ReadOnly))
                {
                    text = QString::fromUtf8(f.readAll());
                    got = true;
                }
            }
            if (!got)
            {
                ctx->skip(QStringLiteral("CH-07"),
                          QStringLiteral("CH-07 诊断页 ↔ 启动日志：未提供可读的 "
                                         "STELQUICK_BOOTLOG=%1（UNAVAILABLE，不记 FAIL）")
                              .arg(logPath.isEmpty() ? QStringLiteral("(未设)") : logPath));
            }
            else
            {
                const LogFacts lf = parseBootLog(text);
                if (!lf.haveProbe || !lf.haveRuntime)
                {
                    ctx->skip(QStringLiteral("CH-07"),
                              QStringLiteral("CH-07 诊断页 ↔ 启动日志：日志里没找到探针行/"
                                             "后端行（probe=%1 runtime=%2）—— 前提不齐")
                                  .arg(lf.haveProbe ? 1 : 0)
                                  .arg(lf.haveRuntime ? 1 : 0));
                }
                else
                {
                    QStringList diffs;
                    auto cmp = [&diffs](const QString &what, const QString &a, const QString &b) {
                        if (a != b)
                            diffs << QStringLiteral("%1: 诊断面「%2」≠ 日志「%3」").arg(what, a, b);
                    };
                    cmp(QStringLiteral("deviceName"), ctx->backend->deviceName(), lf.device);
                    cmp(QStringLiteral("vulkanVersion"), ctx->backend->vulkanVersion(), lf.api);
                    cmp(QStringLiteral("driverVersion"), ctx->backend->driverVersion(), lf.driver);
                    cmp(QStringLiteral("runtimeApiName"), ctx->backend->runtimeApiName(),
                        lf.runtimeApi);
                    if (ctx->backend->portabilityDriver() != lf.portabilityDriver)
                        diffs << QStringLiteral("portabilityDriver: 诊断面 %1 ≠ 日志 %2")
                                     .arg(ctx->backend->portabilityDriver() ? 1 : 0)
                                     .arg(lf.portabilityDriver ? 1 : 0);
                    if (ctx->backend->backendOk() != lf.backendOk)
                        diffs << QStringLiteral("backendOk: 诊断面 %1 ≠ 日志 %2")
                                     .arg(ctx->backend->backendOk() ? 1 : 0)
                                     .arg(lf.backendOk ? 1 : 0);
                    ctx->mark(QStringLiteral("CH-07"), diffs.isEmpty(),
                              QStringLiteral("CH-07 诊断页 ↔ 启动日志（P-CFG-04，6 项）："
                                             "device「%1」api「%2」driver「%3」"
                                             "runtimeApi「%4」portability=%5 backendOk=%6｜%7"
                                             "｜⚠️ 只证「两处渲染一致」，不证探针结论正确")
                                  .arg(lf.device, lf.api, lf.driver, lf.runtimeApi)
                                  .arg(lf.portabilityDriver ? 1 : 0)
                                  .arg(lf.backendOk ? 1 : 0)
                                  .arg(diffs.isEmpty()
                                           ? QStringLiteral("逐项相同")
                                           : diffs.join(QStringLiteral("；"))));
                }
            }
        }

        // ── CH-08 交换链信息如实缺席（把 §4.1 的 N/A 决定钉成断言）──────────
        {
            QStringList hits;
            const QMetaObject *mo = ctx->backend->metaObject();
            for (int i = 0; i < mo->propertyCount(); ++i)
            {
                const QString name = QString::fromLatin1(mo->property(i).name());
                if (name.contains(QStringLiteral("swap"), Qt::CaseInsensitive))
                    hits << name;
            }
            ctx->mark(QStringLiteral("CH-08"), hits.isEmpty(),
                      QStringLiteral("CH-08 诊断面**不含**交换链信息（样本 = BackendInfo 的 %1 个 "
                                     "Q_PROPERTY）：命中 %2 —— 交换链属 QML/Vulkan 后端，产品侧"
                                     "拿不到（契约第 1 条禁止 QML 侧接触 Vulkan 记号）⇒ "
                                     "P-CFG-04 的「交换链」一项如实记 **N/A**，不得编近似量充数"
                                     "（陷阱 67/75）")
                          .arg(mo->propertyCount())
                          .arg(hits.isEmpty() ? QStringLiteral("0 项")
                                              : hits.join(QStringLiteral(","))));
        }

        // ── CH-09 引导修复的账目（"启动可恢复"背后的机制单独记账）──────────────
        //    断言：**该修的形态必须真修了，且原始字节一个没丢**；**不该修的必须没修**。
        //      · 不完整形态（除 readonly 外的注入形态）⇒ `configRepaired` ∧ 备份存在
        //        ∧ 注入字节确实在备份里（`inj.where == InBackup`）；
        //      · `none`（键数=随包默认）与 `readonly`（不可写，修不了）⇒ **不许修**
        //        —— 这条是防"顺手把好配置也换掉"的反向闸（陷阱 90：清理=写入，
        //        先问"我原来是什么态"）。
        {
            const bool expectsRepair =
                (formId != QLatin1String("readonly") && form->expectSev == 3);
            const bool bakOk = !rep.configCorruptBackup.isEmpty() &&
                               QFileInfo(rep.configCorruptBackup).exists();
            const bool uninjured = (inj.where == InjWhere::InBackup);
            const bool ok = expectsRepair ? (rep.configRepaired && bakOk && uninjured)
                                          : !rep.configRepaired;
            ctx->mark(QStringLiteral("CH-09"), ok,
                      expectsRepair
                          ? QStringLiteral("CH-09 引导修复（该修必须修）：repaired=%1｜"
                                           "备份存在=%2（%3）｜注入字节完整搬入备份=%4")
                                .arg(rep.configRepaired ? 1 : 0)
                                .arg(bakOk ? 1 : 0)
                                .arg(rep.configCorruptBackup.isEmpty()
                                         ? QStringLiteral("(none)")
                                         : QFileInfo(rep.configCorruptBackup).fileName())
                                .arg(uninjured ? 1 : 0)
                          : QStringLiteral("CH-09 引导修复（**不该修不许修**）："
                                           "form=%1 配置本身可用 ⇒ repaired=%2（应 0；"
                                           "repaired=1 意味着把好配置也换掉了）")
                                .arg(formId)
                                .arg(rep.configRepaired ? 1 : 0));
        }

        ctx->finish();
    });
}

} // namespace stelapp
