/*
 * ErrorProbe — T42-A **错误/状态与未支持项探针**（STELQUICK_ERROR_PROBE=1）。
 *
 * ── T42 的范围从哪里来（三处文档口径合起来才是完整的）──────────────────────
 *  ① A-1.0 范围表（`2026-09-17-01-qml-ui-vulkan-development.zh_CN.md:75`）：
 *     `|资源路径、错误提示、渲染诊断、配置保存 | 最小 | **必须** | 配置使用独立个人版目录…|`
 *     T36 已做「配置保存」（个人版目录隔离 + 播种）、T39 已做「渲染诊断」
 *     ⇒ **本轮做掉余下两项：资源路径 + 错误提示**。
 *  ② A5 行（同文档 §1）：`A-1.0 全部页面、配置迁移、夜视、**错误页**`。
 *  ③ 开发指导文档（`2026-09-17-02-…:230`）：
 *     「**旧 UI 禁止作为隐式回退**：纳入范围的功能必须全部从 QML 操作；
 *       **未支持项有清单和明确提示**。」
 *  另：A-1.0 表最后一行 `|高级天文计算、脚本控制台、全部插件设置 | 不做 | 默认不纳入 |
 *     列入后续清单；需要时逐模块增加，**不静默打开旧对话框**|`
 *     ⇒ 「不静默打开旧对话框」是**明文要求**，不是我的发挥。
 *
 * ── 开工前的已知事实（T41-A 探针的读数，本轮要复核/补齐）────────────────────
 *  · `src/gui/` **是被编译的**（随 `stelMain` 静态库进 `stelQuickUI`）⇒ 老 QWidget
 *    对话框在这个进程里**真的可达**（T41-A Q14：F1 ⇒ QWidget 12→58，Δ46 常驻）。
 *  · `src/gui/StelGui.cpp:262-272` 注册了 Windows 组，**共 10 个成员**，其中
 *    **9 个是 `actionShow_*_Window_Global`**（F1/F2/F3/F4/F5/F6/F7/F10/F12）
 *    + **1 个 `actionShow_ObsList_Window_Global`（键 = `Alt+B`）**。
 *    ⇒ 🔴 **T41 只处理了 9 个里的 6 个**（接管 F1/F3/F4/F5/F6/F7）；
 *      F2 / F10 / F12 **刻意未接管**（当时理由：无对应 QML 页，接管 = "按了没反应"）；
 *      **`Alt+B` 观察列表 T41 从未提到**。
 *    ⇒ 这 4 个动作现在按下去会**静默弹出老 QWidget 对话框** —— 违反上面 ③ 与
 *      A-1.0 最后一行的明文。**这正是 T42 要收编的东西。**
 *
 * ── 本轮探针要回答的问题（只读，除 Q5/Q6 的受控 trigger 外零写入）──────────
 *
 *  资源路径面（A-1.0「资源路径 | 最小 | 必须」）
 *   Q1  `StelFileMgr` 七个目录 API 的实测值：`getUserDir` / `getInstallationDir` /
 *       `getCacheDir` / `getScreenshotDir` / `getObsListDir` / `getLocaleDir` /
 *       `getDesktopDir` —— 逐项报「路径 + 存在性 + 可写性」。
 *       ⚠️ 「存在」与「可写」是两件事：`getScreenshotDir()` 可能**目录不存在**
 *          （引擎靠首次截图时 `mkDir`），错误页要如实区分，不能一律画绿。
 *   Q2  T36 隔离的**运行期直接读数**：`getUserDir()` 是否 = 引擎默认用户目录 + `-quick`
 *       （不是子目录！）；`getInstallationDir()` 是什么（源码树跑时 = `"."` ⇒ **分发形态
 *       与源码树形态不同**，这一条决定"资源路径"页在分发后是否还有意义）。
 *   Q3  关键文件实际路径：`config.ini`（`QSettings::fileName()` 独立回读）/ `log.txt`
 *       （`StelLogger::getLogFileName()`）/ `languages.tab` / 星表数据目录 —— 各自
 *       存在性 + 可读性 + 大小。**"打开日志"按钮的目标就是 Q3 的那一条。**
 *
 *  错误提示面（A-1.0「错误提示 | 最小 | 必须」+ A5「错误页」）
 *   Q4  失败面**静态条件**核对（读代码 + 实测交叉验算）：`LiveSkyRuntime::boot()`
 *       的三处 `fail(...)` 分别由什么触发（findFile 双路失败 / 30s 未初始化 /
 *       异常状态）；`start()` 的四道前置闸。⇒ 决定错误页要展示哪些"可操作原因"。
 *   Q5  🔴 **UI 侧现在有没有任何错误出口**：引导失败时 `main.cpp` 只做
 *       `fprintf(stderr)` + `return 8`（**静默退出**，用户看到的是窗口一闪/无画面）；
 *       图形后端探测失败时 `BackendInfo::probeError` 非空但**只有诊断页在用**。
 *       ⇒ 本项实测：合流形态下 `probeError` / `backendOk` / `runtimeApiName` 的**当前值**、
 *         以及"引导失败"能不能被环境**可注入地**制造出来（可测性 ⇒ 产品要开的小口子）。
 *   Q6  🔴 **未接管动作 trigger() 实测裁决**（T41 只实测过 F1；"F2/F10/F12 也会弹"
 *       至今是**推断** —— T41-A 的教训就是推断三处全错）：
 *       对 `actionShow_AstroCalc_Window_Global`（F10）与
 *       `actionShow_ObsList_Window_Global`（**Alt+B**）各测一次
 *       「`trigger()` 前后 `QApplication::allWidgets()` 计数 + 可见 QDialog 数」。
 *       ⚠️ 只采两个样本：Δ 常驻**不可逆**（`StelDialog::setVisible(false)` 只隐藏不销毁），
 *          同类机制不必逐个踩；F2/F12 由 T42-C 用"接管后不再弹"覆盖。
 *   Q7  「未支持项」清单的**真源核对**：A-1.0「不做」三项（高级天文计算 / 脚本控制台 /
 *       全部插件设置）与注册表的对应关系（F10 / F12 / 插件动作组）+ T41 移交的
 *       legacy 手势 10 条 + T39 移交的两个不搬控件 —— 清单条目数必须与真源一致
 *       （**不能凭记忆写清单**，那是"判据在缺前提的机器上退化成平凡真"的同族）。
 *
 *  可操作性（错误页的按钮要真的能用）
 *   Q8  `QDesktopServices::openUrl` 对 `QUrl::fromLocalFile(目录)` 的可用性 +
 *       `QGuiApplication::clipboard()` 是否可用（"复制诊断信息"按钮）。
 *       ⚠️ T41 的 `HelpModel::openExternal` 已有**白名单制**先例，错误页沿用。
 *
 *  收尾
 *   Q9  探针**净写入自证**：首轮实测发现"老对话框弹窗只动内存"是**错的** ——
 *       `StelDialog` 会把尺寸写进 `QSettings`（`DialogSizes/*`）。⇒ 探针按基线
 *       **自净**（新增键删除、变化键还原），再核对与基线**逐项相同**。
 *       ⚠️ 这条副产品本身是 T42-C 的判据素材：**接管 ⇒ 不弹窗 ⇒ 配置零变化**，
 *          比"QWidget 计数"更精细的独立证据通道。
 *
 * ── 探针不是判据（刻意）────────────────────────────────────────────────────
 * 只报读数、不下 PASS/FAIL（断言在 T42-C 的 ErrorCheck）。
 *
 * ── 退出码（由 main.cpp 决定）──────────────────────────────────────────────
 *   0=DONE（正常报完读数），6=UNAVAILABLE（非合流形态 / 引擎未引导）。
 */
#pragma once

#include <QString>
#include <QStringList>
#include <functional>

class QCoreApplication;

namespace stelapp {

class ActionRouter;

class ErrorProbe
{
public:
    struct Result
    {
        bool ran = false;          //!< 真正跑过（前置满足）
        bool unavailable = false;  //!< 环境里跑不起来（非合流形态 / 引擎未引导）
        QString summary;
        QStringList details;       //!< 逐行读数（前缀已在 main.cpp 侧统一加）
    };

    //! 全部在 GUI 线程。@p delayMs 后开跑（等引擎引导落定）。结果经 onDone 回传。
    //! ⚠️ 刻意**不启帧泵、不抓帧**：资源路径 / 配置 / 注册表都是**纯数据面**，
    //!    与 GPU 无关 —— 少一个 Metal 环节少一份掉设备风险（T41-A 同款理由）。
    //! ⚠️ 写入面仅 Q6 的两次 `trigger()`（受控、且是"老对话框到底弹不弹"的唯一
    //!    取证手段）；**收尾自净**（Q9）—— 首轮实测发现老对话框会写
    //!    `DialogSizes/*` 到 `QSettings`，探针按基线还原，保证**净写入为零**。
    static void run(QCoreApplication *app,
                    ActionRouter *router,
                    const std::function<void(const Result &)> &onDone,
                    int delayMs = 1200);
};

} // namespace stelapp
