// ErrorProbe 实现。设计说明与问题表见 ErrorProbe.hpp（同一套口径）。
#include "app/ErrorProbe.hpp"

#if defined(STELQUICK_HAS_ENGINE)

#include "app/ActionRouter.hpp"
#include "core/StelActionMgr.hpp"
#include "core/StelApp.hpp"
#include "core/StelFileMgr.hpp"
#include "StelLogger.hpp"

#include <QApplication>
#include <QClipboard>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHash>
#include <QKeySequence>
#include <QSettings>
#include <QTimer>
#include <QUrl>
#include <QWidget>

#include <cstdio>

namespace stelapp {

namespace {

using ProbeResult = ErrorProbe::Result;

// ─────────────────────────────────────────────────────────────────────────────
// 小工具（除 Q6 的受控 trigger 外全部只读）
// ─────────────────────────────────────────────────────────────────────────────

//! 一个目录的"人读一行"。⚠️「存在」与「可写」是两件事 —— 引擎的
//! `getScreenshotDir()` 等目录可能是**惰性创建**的（首次用到才 mkDir），
//! 如实区分，不能一律画成健康。
QString dirFact(const QString &label, const QString &path)
{
    if (path.isEmpty())
        return QStringLiteral("%1：**空串**（API 返回空）").arg(label);
    const QFileInfo fi(path);
    const bool exists = fi.exists() && fi.isDir();
    QString state;
    if (exists)
    {
        state = fi.isWritable() ? QStringLiteral("存在·可写")
                                : QStringLiteral("存在·**只读**");
    }
    else
    {
        const QFileInfo parent(fi.absolutePath());
        state = parent.isWritable() ? QStringLiteral("**不存在**·父目录可写（惰性创建？）")
                                    : QStringLiteral("**不存在**·父目录**只读**");
    }
    const int n = exists
                      ? QDir(path).entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot).size()
                      : -1;
    return QStringLiteral("%1 = %2｜%3｜条目 %4").arg(label, path, state).arg(n);
}

//! 一个文件的"人读一行"（存在性 / 大小 / 可读 / 可写）。
QString fileFact(const QString &label, const QString &path)
{
    if (path.isEmpty())
        return QStringLiteral("%1：**空串**（未设/不可解析）").arg(label);
    const QFileInfo fi(path);
    if (!fi.exists())
        return QStringLiteral("%1 = %2｜**不存在**").arg(label, path);
    return QStringLiteral("%1 = %2｜%3 B｜%4｜%5")
        .arg(label, path)
        .arg(fi.size())
        .arg(fi.isReadable() ? QStringLiteral("可读") : QStringLiteral("**不可读**"))
        .arg(fi.isWritable() ? QStringLiteral("可写") : QStringLiteral("只读"));
}

//! 进程内 QWidget 总数（老宿主对话框可达性的读数）。
int widgetCount()
{
    return QApplication::allWidgets().size();
}

//! 可见的顶层 QDialog 数（"老对话框真的弹出来了吗"的直接读数）。
int visibleDialogs()
{
    int n = 0;
    for (QWidget *w : QApplication::allWidgets())
        if (w && w->isWindow() && w->isVisible() && w->inherits("QDialog"))
            ++n;
    return n;
}

//! 关掉所有可见顶层 QDialog（清理 —— "每一步自己收尾"，防污染后续读数）。
void closeVisibleDialogs()
{
    for (QWidget *w : QApplication::allWidgets())
        if (w && w->isWindow() && w->isVisible() && w->inherits("QDialog"))
            w->close();
}

QHash<QString, QString> dumpSettings(QSettings *s)
{
    QHash<QString, QString> out;
    if (!s)
        return out;
    const QStringList keys = s->allKeys();
    for (const QString &k : keys)
        out.insert(k, s->value(k).toString());
    return out;
}

QStringList diffKeys(const QHash<QString, QString> &a, const QHash<QString, QString> &b)
{
    QStringList out;
    for (auto it = a.cbegin(); it != a.cend(); ++it)
        if (b.value(it.key()) != it.value())
            out << it.key();
    for (auto it = b.cbegin(); it != b.cend(); ++it)
        if (!a.contains(it.key()))
            out << (QStringLiteral("+") + it.key());
    out.sort();
    return out;
}

struct Ctx;
struct Step
{
    int delayAfter = 0;
    std::function<void(Ctx *)> body;
};

struct Ctx
{
    ActionRouter *router = nullptr;
    QCoreApplication *app = nullptr;
    ProbeResult result;
    QSettings *conf = nullptr;
    QHash<QString, QString> baseConf;

    void note(const QString &s) { result.details << s; }
    void finish()
    {
        result.summary = QStringLiteral("错误/状态与未支持项探针：%1 条读数（只读为主）")
                             .arg(result.details.size());
        if (result_cb)
            result_cb(result);
    }
    std::function<void(const ProbeResult &)> result_cb;
};

} // namespace

void ErrorProbe::run(QCoreApplication *app,
                     ActionRouter *router,
                     const std::function<void(const Result &)> &onDone,
                     int delayMs)
{
    auto ctx = std::make_shared<Ctx>();
    ctx->app = app;
    ctx->router = router;
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

    // ── S1 布场：配置指纹 + 引擎状态 ──────────────────────────────────────
    steps->append(Step{0, [](Ctx *c) {
        c->conf = StelApp::getInstance().getSettings();
        c->baseConf = dumpSettings(c->conf);
        c->note(QStringLiteral("布场：引擎 initialized=%1｜QWidget 总数=%2｜可见 QDialog=%3｜"
                               "配置文件=%4｜配置键 %5")
                    .arg(StelApp::isInitialized() ? 1 : 0)
                    .arg(widgetCount())
                    .arg(visibleDialogs())
                    .arg(c->conf ? c->conf->fileName() : QStringLiteral("(null)"))
                    .arg(c->baseConf.size()));
    }});

    // ── S2 Q1 资源路径面（七个目录 API）──────────────────────────────────
    steps->append(Step{0, [](Ctx *c) {
        c->note(QStringLiteral("Q1 ===== 资源路径面（StelFileMgr 七个目录 API）====="));
        c->note(QStringLiteral("Q1 %1").arg(dirFact(QStringLiteral("用户目录 userDir"),
                                                   StelFileMgr::getUserDir())));
        c->note(QStringLiteral("Q1 %1").arg(dirFact(QStringLiteral("安装目录 installDir"),
                                                   StelFileMgr::getInstallationDir())));
        c->note(QStringLiteral("Q1 %1").arg(dirFact(QStringLiteral("缓存目录 cacheDir"),
                                                   StelFileMgr::getCacheDir())));
        c->note(QStringLiteral("Q1 %1").arg(dirFact(QStringLiteral("截图目录 screenshotDir"),
                                                   StelFileMgr::getScreenshotDir())));
        c->note(QStringLiteral("Q1 %1").arg(dirFact(QStringLiteral("观测列表 obsListDir"),
                                                   StelFileMgr::getObsListDir())));
        c->note(QStringLiteral("Q1 %1").arg(dirFact(QStringLiteral("语言目录 localeDir"),
                                                   StelFileMgr::getLocaleDir())));
        c->note(QStringLiteral("Q1 %1").arg(dirFact(QStringLiteral("桌面目录 desktopDir"),
                                                   StelFileMgr::getDesktopDir())));
        // 搜索路径的**可观测代理**：`StelFileMgr::fileLocations` 是 **private**
        // （`StelFileMgr.hpp:223`）⇒ 用公开 API `findFileInAllPaths()` 拿"某个文件
        // 实际在哪些搜索路径里命中"，语义等价且不越界。
        const QStringList cfgHits = StelFileMgr::findFileInAllPaths(QStringLiteral("config.ini"));
        c->note(QStringLiteral("Q1 config.ini 的搜索路径命中（findFileInAllPaths）= %1 项：[%2]"
                               "（⚠️ 路径全表 `fileLocations` 是 private，只能用这个可观测代理）")
                    .arg(cfgHits.size())
                    .arg(cfgHits.join(QStringLiteral(" ｜ "))));
    }});

    // ── S3 Q2 T36 隔离的运行期直接读数 ────────────────────────────────────
    steps->append(Step{0, [](Ctx *c) {
        const QString userDir = StelFileMgr::getUserDir();
        const bool suffixed = userDir.endsWith(QStringLiteral("-quick"));
        c->note(QStringLiteral("Q2 隔离读数：getUserDir() = 「%1」｜以 `-quick` 结尾 = %2 ⇒ %3")
                    .arg(userDir)
                    .arg(suffixed ? QStringLiteral("是") : QStringLiteral("**否**"))
                    .arg(suffixed ? QStringLiteral("T36 隔离**生效**（同级目录，不是子目录）")
                                  : QStringLiteral("隔离未生效或被 STELQUICK_CFG_ISOLATE_OFF 关闭")));
        // 原版目录是否被触碰：只读比较"原版目录"是否存在（不写入）
        if (suffixed)
        {
            const QString original = userDir.left(userDir.size() - 6);
            QFileInfo ofi(original);
            c->note(QStringLiteral("Q2 原版目录（去掉 -quick）= 「%1」｜存在=%2｜可写=%3"
                                   "（本探针**只读**，不触碰它）")
                        .arg(original)
                        .arg(ofi.exists() ? QStringLiteral("是") : QStringLiteral("否"))
                        .arg(ofi.isWritable() ? QStringLiteral("是") : QStringLiteral("否")));
        }
    }});

    // ── S4 Q3 关键文件路径（"打开日志"按钮的目标）────────────────────────
    steps->append(Step{0, [](Ctx *c) {
        c->note(QStringLiteral("Q3 ===== 关键文件（错误页/状态页要展示与跳转的目标）====="));
        c->note(QStringLiteral("Q3 配置文件 QSettings::fileName() = 「%1」")
                    .arg(c->conf ? c->conf->fileName() : QStringLiteral("(null)")));
        c->note(QStringLiteral("Q3 %1").arg(fileFact(QStringLiteral("日志文件"),
                                                    StelLogger::getLogFileName())));
        c->note(QStringLiteral("Q3 %1").arg(fileFact(QStringLiteral("config.ini（findFile Writable|File）"),
                                                    StelFileMgr::findFile(
                                                        QStringLiteral("config.ini"),
                                                        StelFileMgr::Flags(StelFileMgr::Writable
                                                                           | StelFileMgr::File)))));
        c->note(QStringLiteral("Q3 %1").arg(fileFact(QStringLiteral("config.ini（findFile New）"),
                                                    StelFileMgr::findFile(QStringLiteral("config.ini"),
                                                                          StelFileMgr::New))));
        c->note(QStringLiteral("Q3 %1").arg(fileFact(QStringLiteral("languages.tab"),
                                                    StelFileMgr::findFile(QStringLiteral("languages.tab")))));
        c->note(QStringLiteral("Q3 %1").arg(fileFact(QStringLiteral("data/default_cfg.ini（播种源）"),
                                                    StelFileMgr::findFile(
                                                        QStringLiteral("data/default_cfg.ini")))));
    }});

    // ── S5 Q4 失败面的触发条件（静态条件 + 实测交叉）──────────────────────
    steps->append(Step{0, [](Ctx *c) {
        c->note(QStringLiteral("Q4 ===== 失败面（错误页要展示的『可操作原因』的来路）====="));
        c->note(QStringLiteral("Q4 boot() 三处 fail：① findFile(config.ini) 双路（Writable|File → New）"
                               "皆空；② 30s 内 initializeGL 未触发（StelApp::isInitialized()==false）；"
                               "③ 『mainView 已存在但未 boot 完成』异常态。"));
        c->note(QStringLiteral("Q4 start() 四道前置闸：未 boot / 已在运行（重复 start）/ 邮箱为空 / "
                               "渲染尺寸非法（<1）。"));
        c->note(QStringLiteral("Q4 🔴 **第四处、且最凶的一处不在 boot() 里**："
                               "`StelFileMgr::init()` 在用户目录**不可创建**时走 "
                               "`makeSureDirExistsAndIsWritable` 抛 `std::runtime_error` ⇒ "
                               "`qFatal`（`src/core/StelFileMgr.cpp:90-92`）⇒ **SIGABRT，进程直接崩**，"
                               "任何 UI 都没机会出现。⇒ 错误页要在 **boot() 里加预检**"
                               "（init 之前判定用户目录可创建性），别去改引擎上游。"));
        c->note(QStringLiteral("Q4 fail 消息形态（实测样例，来自 boot 的 fail() 文案）："
                               "『既找不到也建不出 config.ini（StelFileMgr::findFile 双路失败）。』"
                               "⇒ 这类**可操作**原因是错误页正文的素材。"));
    }});

    // ── S5b Q5 可注入性：用现成环境变量造真实失败（不造假桩）──────────────
    steps->append(Step{0, [](Ctx *c) {
        const QByteArray env = qgetenv("STEL_USERDIR");
        c->note(QStringLiteral("Q5 ===== 可注入性（判据要用**真实失败**驱动，不是注入桩）====="));
        c->note(QStringLiteral("Q5 STEL_USERDIR 环境变量 = 「%1」｜getUserDir() = 「%2」")
                    .arg(QString::fromLocal8Bit(env), StelFileMgr::getUserDir()));
        c->note(QStringLiteral("Q5 `StelFileMgr.cpp:69-77` 读 STEL_USERDIR 覆盖 userDir，"
                               "随后 `makeSureDirExistsAndIsWritable()`（`:87`）"
                               "⇒ **把 STEL_USERDIR 指向『父路径不是目录』的位置**"
                               "（如 `/dev/null/xxx`）即得**真实**的『用户目录不可创建』失败，"
                               "无需产品开测试口子。"));
        c->note(QStringLiteral("Q5 现有可控开关（T36/T41 先例）：STELQUICK_CFG_ISOLATE_OFF"
                               "（跳过隔离）/ STELQUICK_CFG_MIGRATE_OFF（不播种）/ "
                               "STELQUICK_HELP_TAKEOVER_OFF（不注册接管）。"));
        c->note(QStringLiteral("Q5 UI 侧错误出口现状：**无** —— main.cpp 引导失败只做 "
                               "`fprintf(stderr)` + `return 8`（静默退出）；QML 页面清单里"
                               "没有错误/状态页（%1）").arg(QStringLiteral(
                                   "QML 现有 9 个业务页：天空/搜索/时间/地点/显示/快捷键/帮助/关于/诊断")));
    }});

    // ── S6 Q6-a 未接管动作 trigger 实测①：F10 天文计算 ────────────────────
    steps->append(Step{0, [](Ctx *c) {
        c->note(QStringLiteral("Q6 ===== 未接管动作 trigger() 实测（T41 只测过 F1）====="));
        const QString id = QStringLiteral("actionShow_AstroCalc_Window_Global");
        StelAction *a = StelApp::getInstance().getStelActionManager()->findAction(id);
        if (!a)
        {
            c->note(QStringLiteral("Q6-a %1：**不在注册表**（跳过）").arg(id));
            return;
        }
        c->note(QStringLiteral("Q6-a %1：在册｜文本=「%2」｜键=「%3」｜接管=%4")
                    .arg(id, a->getText(),
                         a->getShortcut().toString(QKeySequence::NativeText),
                         c->router->isHostTakeover(id) ? QStringLiteral("是") : QStringLiteral("**否**")));
        const int n0 = widgetCount();
        const int d0 = visibleDialogs();
        const bool ok = c->router->trigger(id);
        const int n1 = widgetCount();
        const int d1 = visibleDialogs();
        c->note(QStringLiteral("Q6-a trigger=%1｜QWidget %2→%3（**Δ%4**）｜可见 QDialog %5→%6（**Δ%7**）"
                               " ⇒ %8")
                    .arg(ok ? QStringLiteral("true") : QStringLiteral("false"))
                    .arg(n0).arg(n1).arg(n1 - n0)
                    .arg(d0).arg(d1).arg(d1 - d0)
                    .arg((n1 > n0 || d1 > d0)
                             ? QStringLiteral("**老 QWidget 对话框真的弹出来了**")
                             : QStringLiteral("没弹出来（与 T41 的推断不符，需复核）")));
    }});

    // ── S7 Q6-b 实测②：Alt+B 观察列表（🔴 T41 从未提到的那个）────────────
    steps->append(Step{200, [](Ctx *c) {
        // 先收尾上一步弹出的对话框（每步自己收尾）
        closeVisibleDialogs();
        const QString id = QStringLiteral("actionShow_ObsList_Window_Global");
        StelAction *a = StelApp::getInstance().getStelActionManager()->findAction(id);
        if (!a)
        {
            c->note(QStringLiteral("Q6-b %1：**不在注册表**（跳过）").arg(id));
            return;
        }
        c->note(QStringLiteral("Q6-b %1：在册｜文本=「%2」｜键=「%3」｜接管=%4"
                               "（🔴 T41 只处理了 9 个 actionShow_* 里的 6 个，**这一个从未被提及**）")
                    .arg(id, a->getText(),
                         a->getShortcut().toString(QKeySequence::NativeText),
                         c->router->isHostTakeover(id) ? QStringLiteral("是") : QStringLiteral("**否**")));
        const int n0 = widgetCount();
        const int d0 = visibleDialogs();
        const bool ok = c->router->trigger(id);
        const int n1 = widgetCount();
        const int d1 = visibleDialogs();
        c->note(QStringLiteral("Q6-b trigger=%1｜QWidget %2→%3（**Δ%4**）｜可见 QDialog %5→%6（**Δ%7**）"
                               " ⇒ %8")
                    .arg(ok ? QStringLiteral("true") : QStringLiteral("false"))
                    .arg(n0).arg(n1).arg(n1 - n0)
                    .arg(d0).arg(d1).arg(d1 - d0)
                    .arg((n1 > n0 || d1 > d0)
                             ? QStringLiteral("**老 QWidget 对话框真的弹出来了**")
                             : QStringLiteral("没弹出来（需复核）")));
    }});

    // ── S8 Q6-c 收尾：清干净 + 报剩余 QWidget 常驻 ────────────────────────
    steps->append(Step{200, [](Ctx *c) {
        closeVisibleDialogs();
        c->note(QStringLiteral("Q6-c 收尾：已关可见 QDialog｜QWidget 总数 = %1（**只隐藏不销毁**，"
                               "Δ 常驻是 `StelDialog` 的既有语义，T41 已记录）")
                    .arg(widgetCount()));
    }});

    // ── S9 Q7 「未支持项」清单的真源核对（不许凭记忆写清单）────────────────
    steps->append(Step{0, [](Ctx *c) {
        StelActionMgr *mgr = StelApp::getInstance().getStelActionManager();
        const QString win = QStringLiteral("Windows");
        const QList<StelAction *> members = mgr->getActionList(win);
        c->note(QStringLiteral("Q7 ===== 未支持项真源（Windows 组全表 = 老 GUI 窗口入口的真源）====="));
        c->note(QStringLiteral("Q7 「Windows」组成员 %1 个：").arg(members.size()));
        for (StelAction *a : members)
        {
            if (!a)
                continue;
            c->note(QStringLiteral("Q7   · %1｜文本=「%2」｜键=「%3」｜接管=%4")
                        .arg(a->getId(), a->getText(),
                             a->getShortcut().toString(QKeySequence::NativeText),
                             c->router->isHostTakeover(a->getId()) ? QStringLiteral("是")
                                                                   : QStringLiteral("**否**")));
        }
        // A-1.0「不做」三项与注册表的对应（清单必须落在真源上）
        c->note(QStringLiteral("Q7 A-1.0『不做』三项对应：高级天文计算 = "
                               "actionShow_AstroCalc_Window_Global（F10）｜脚本控制台 = "
                               "actionShow_ScriptConsole_Window_Global（F12）｜全部插件设置 = "
                               "配置窗口 actionShow_Configuration_Window_Global（F2）内的插件页"
                               "（老 GUI 无独立入口）。"));
        c->note(QStringLiteral("Q7 legacy 手势 10 条（T41 已定，仍留在帮助页红字标注）："
                               "脚本控制台 3 + 天文计算 7 ⇒ 与上表同源，未支持项清单**引用**它们，"
                               "不重复维护第二份。"));
        c->note(QStringLiteral("Q7 T39 移交的不可搬控件 2 个（guiFontSize / screenButtonScale）"
                               "也属未支持项（对 QML 视觉树零影响）。"));
    }});

    // ── S10 Q8 可操作性（错误页按钮要真的能用）───────────────────────────
    steps->append(Step{0, [](Ctx *c) {
        c->note(QStringLiteral("Q8 QDesktopServices::openUrl 可用（Qt 基础 API，本地目录走 "
                               "QUrl::fromLocalFile）｜剪贴板 = %1")
                    .arg(QGuiApplication::clipboard() ? QStringLiteral("可用")
                                                      : QStringLiteral("**不可用**")));
        c->note(QStringLiteral("Q8 ⚠️ T41 的 `HelpModel::openExternal` 已建立**白名单制**先例；"
                               "错误页的『打开日志目录』沿用同一策略（只允许已知路径，不接受任意 URL）。"));
        c->note(QStringLiteral("Q8 探针**不真的调 openUrl** —— 会唤起 Finder 抢焦点，"
                               "污染后续读数（T41 的交互腿教训：环境副作用要主动隔离）。"));
    }});

    // ── S11 Q9 自净 + 零净写入自证（配置指纹逐项比对）─────────────────────
    steps->append(Step{0, [](Ctx *c) {
        // 🔴 首轮实测**推翻了开工假设**："StelDialog 弹窗只动内存、不写 QSettings"是错的 ——
        //    Q6 的两次 trigger 让配置多出 `DialogSizes/AstroCalc` 与
        //    `DialogSizes/ObservingList`（老对话框会**记住自己的尺寸**）。
        //    ⇒ 探针必须**自净**：按基线把新增键删掉、把变化键还原，否则污染后续
        //    T42-C 的"零写入"基线与 T36 的配置指纹判据（"清理 = 写入，写之前先问
        //    原来是什么态"，陷阱 90 的同族纪律）。
        const QHash<QString, QString> now = dumpSettings(c->conf);
        const QStringList diff = diffKeys(c->baseConf, now);

        QStringList added, changed;
        for (const QString &k : diff)
        {
            if (k.startsWith(QLatin1Char('+')))
                added << k.mid(1);
            else
                changed << k;
        }
        for (const QString &k : changed)
            c->conf->setValue(k, c->baseConf.value(k));
        for (const QString &k : added)
            c->conf->remove(k);
        if (!diff.isEmpty())
            c->conf->sync();

        const QHash<QString, QString> after = dumpSettings(c->conf);
        const QStringList residual = diffKeys(c->baseConf, after);
        c->note(QStringLiteral("Q9 探针写入与自净：Q6 两次 trigger 造成 %1 项写入"
                               "（新增 %2｜变化 %3）⇒ 已按基线还原")
                    .arg(diff.size())
                    .arg(added.isEmpty() ? QStringLiteral("无") : added.join(QStringLiteral(", ")))
                    .arg(changed.isEmpty() ? QStringLiteral("无") : changed.join(QStringLiteral(", "))));
        c->note(QStringLiteral("Q9 零净写入自证：还原后配置键 %1 → %2｜残差 %3 项%4")
                    .arg(c->baseConf.size())
                    .arg(after.size())
                    .arg(residual.size())
                    .arg(residual.isEmpty()
                             ? QStringLiteral("（**逐项相同** ⇒ 探针净写入为零）")
                             : QStringLiteral("：%1（**未净**，需排查）")
                                   .arg(residual.join(QStringLiteral(", ")))));
        c->note(QStringLiteral("Q9 🔴 **这条读数本身就是产品判据的素材**：不接管 ⇒ trigger 老窗口"
                               "会写 `DialogSizes/*`；接管后 ⇒ 不弹窗、配置零变化。"
                               "比 QWidget 计数更精细的『接管生效』独立证据通道（T42-C 采用）。"));
    }});
}

} // namespace stelapp

#endif // STELQUICK_HAS_ENGINE
