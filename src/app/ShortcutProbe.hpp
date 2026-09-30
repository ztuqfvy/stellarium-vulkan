/*
 * ShortcutProbe — T40-A **快捷键编辑命令面探针**（STELQUICK_SHORTCUT_PROBE=1）。
 *
 * ── 为什么第一相位是探针（T24/T33/T34/T35/T37/T38/T39 同款纪律）────────────
 * A-1.0 范围表倒数第二格：
 *   `|**快捷键编辑**、帮助、版本与许可证页面 | 可延后 | 必须 | 中文输入焦点不能触发天空快捷键|`
 * 第三列的**约束**已由 T29 兜住（IME 组合态 + Esc 守卫；那是"不许出错"的一条），
 * 本任务要交付的是**第一列那个页面本体**：用户能在 QML 里看 + 改快捷键并落盘。
 *
 * ── 开工前的代码审计（推断，不是证据）──────────────────────────────────────
 * 引擎侧快捷键面（`src/core/StelActionMgr.hpp` / `.cpp`，全部已存在，**不需要改引擎**）：
 *   · `StelAction::getShortcut() / getAltShortcut()`            → `QKeySequence`
 *   · `StelAction::setShortcut(QString) / setAltShortcut(QString)`
 *       实现是 `keySequence = QKeySequence(key); emit changed();`
 *       ⚠️ **不落盘、不 emit `StelActionMgr::shortcutsChanged()`**（见 `:89-99`）
 *   · `StelAction::matches(QKeySequence)` → 主/备取 `qMax`（`:190-196`）
 *   · `StelActionMgr::findAction(id)` / `findActionFromShortcut(str)`
 *       ⚠️ 后者是**字符串精确比较、遍历全部、返回最后一个匹配**
 *   · `getGroupList()` / `getActionList(group)` / `getActionList()` / `getShortcutsList()`
 *   · `saveShortcuts()`（`:320-344`）—— **`conf->beginGroup("shortcuts"); conf->remove("");`
 *       整组清空后重建**，只写"与出厂默认不同"的项；空串写成**字面量 `""`**；
 *       末尾 `emit shortcutsChanged()`
 *   · `restoreDefaultShortcuts()` / `restoreDefaultShortcut(action)`（`:346-364`）
 *       ⚠️ 单个恢复也要走一遍 **`saveShortcuts()`（整组重写）**
 *   · `pushKey(int,bool)`（`:246-272`）—— **受 `actionsEnabled` 门**，且有
 *       **跨按键累积的 `keySequence` 成员**（多键序列的中间态）
 *   · `setAllActionsEnabled(bool)`（原版注释：editing shortcuts without triggering any actions）
 * 读取点（`StelAction` 构造，`:55-67`）：`conf->value("shortcuts/"+actionId).toString()
 *   .split(spaceExp)` → `[0]` 主键、`[1]` 备键；**`conf->contains()` 才读**（不存在则用出厂默认）。
 * 启动期冲突检查（`src/StelMainView.cpp:1041-1070`）：读 `shortcuts` 组 → `QMultiMap<键,动作>`
 *   → 找重复键告警。⚠️ 这段在**旧宿主**里；合流形态下走不走得通未知。
 * 路由面（`src/app/ActionRouter.cpp:141-160`）：`routeKey` → 遍历 `getActionList()`
 *   取**首个 `ExactMatch`** → `trigger()`。**不受 `actionsEnabled` 门**、**不受 `canDispatchToSky`
 *   之外的门**；⚠️「首个 ExactMatch」与 `findActionFromShortcut` 的「最后一个」**语义相反**。
 *
 * ── 探针要回答的问题 ─────────────────────────────────────────────────────
 *   Q1 **`QKeySequence` 往返恒等**：`getShortcut().toString()` → `QKeySequence(str)` → `toString()`
 *      是否逐位相同；单键 / 多修饰 / 功能键 / 箭头 / 多键序列（`Ctrl+E, Ctrl+2`）各一类样本。
 *      这是整个编辑页面的地基 —— 往返不恒等 ⇒ 用户改完键回来就变样。
 *   Q2 **`setShortcut()` 的**即时性**：改完内存里立刻生效吗（`getShortcut` 回读 +
 *      `findActionFromShortcut` 能找到 + `ActionRouter::routeKey` 能命中）？
 *      以及它**是否 emit `changed()` / `shortcutsChanged()`**（决定下游要不要自己转发）。
 *   Q3 **`saveShortcuts()` 的落盘语义**：写哪些项（只写非默认？）、写到哪（T36 个人版目录？）、
 *      格式长什么样（多键序列怎么拼、空串写成什么）、**是否立刻进文件**（QSettings 延迟写）。
 *   Q4 🔴 **不写穿**（T36 同族）：`saveShortcuts()` 前后，**`shortcuts` 组以外**的全部键值
 *      指纹是否变化。这是"编辑快捷键会不会毁掉用户其它设置"的唯一证据。
 *   Q5 🔴 **空串 = 移除**的语义：`setShortcut("")` 之后落盘是什么（字面量 `""`？空？）、
 *      重启时 `split()` 出来的东西喂给 `QKeySequence()` 得到什么。原版靠这个"允许删除快捷键"。
 *   Q6 **重启等价性**：另开一个 `QSettings` 实例直接从**同一个文件**读，值是否与内存一致
 *      （= 重启后 `StelAction` 构造能读到什么）。**分 sync 前 / sync 后两次读**，
 *      暴露 QSettings 的延迟写窗口。
 *   Q7 **`restoreDefaultShortcut(action)` 的波及面**：它内部走整组重写 ⇒
 *      **别的动作的自定义键会不会被顺手弄丢**。（单个恢复不该有全局副作用。）
 *   Q8 🔴 **冲突数据面**：注册表里现存多少**重复键**（原版出厂就可能带）；
 *      `findActionFromShortcut` 与 `routeKey` 对同一个冲突键**各选中谁**；
 *      「全部冲突项」能不能只靠现成 API 拿到（拿不到 ⇒ 产品侧自己扫）。
 *   Q9 **`setAllActionsEnabled(false)` 的门**：`pushKey` 是否真的被挡住；
 *      该状态下 `routeKey` 是否**照旧触发**（若是 ⇒ 编辑期间"屏蔽动作"不能只靠它）。
 *   Q10 **注册表规模与文本**：动作数 / 分组数 / 分组名 / 代表动作的中文 `getText()`
 *      （T24 翻译链路的红利，T34 已见 505 动作 / 15 分组，这里复核是否随版本漂移）。
 *   Q11 **平台键位格式**：macOS 上 `toString()` 给的是 `Ctrl`（PortableText）还是 `⌘`（NativeText）；
 *      两者互转会不会丢信息（决定 QML 里给用户看什么、存什么）。
 *   Q12 **试验品可还原**：全程动过的动作在收尾能否**逐项还原**，且
 *      `shortcuts` 组 + 其它组指纹都回到 S1 基线。（探针不许污染用户配置。）
 *
 * ── 探针不是判据（刻意）────────────────────────────────────────────────────
 * 只报读数、不下 PASS/FAIL（断言在 T40-C 的 ShortcutCheck）。
 *
 * ── 退出码（由 main.cpp 决定）──────────────────────────────────────────────
 *   0=DONE（正常报完读数），6=UNAVAILABLE（非合流形态/引擎未引导）。
 */
#pragma once

#include <QString>
#include <QStringList>
#include <functional>

class QCoreApplication;

namespace stelapp {

class ActionRouter;

class ShortcutProbe
{
public:
    struct Result
    {
        bool ran = false;          //!< 真正跑过（前置满足）
        bool unavailable = false;  //!< 环境里跑不起来（非合流形态/引擎未引导）
        QString summary;
        QStringList details;       //!< 逐行读数（前缀已在 main.cpp 侧统一加）
    };

    //! 全部在 GUI 线程。@p delayMs 后开跑（等引擎动作注册落定）。结果经 onDone 回传。
    //! ⚠️ 刻意**不启帧泵、不抓帧**：快捷键面是纯命令面（QObject 注册表），
    //! 与 GPU 无关 —— 少一个 Metal 环节少一份掉设备的风险。
    static void run(QCoreApplication *app,
                    ActionRouter *router,
                    const std::function<void(const Result &)> &onDone,
                    int delayMs = 2500);
};

} // namespace stelapp
