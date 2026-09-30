/*
 * ShortcutCheck — T40-C **快捷键编辑自检**（STELQUICK_SHORTCUT_CHECK=1）。
 *
 * ── 设计依据（全部来自 T40-A 探针的十二条口径，见 app/ShortcutProbe.hpp 头注）──
 * 判据共 **14 条（SC-01..SC-14）**，成对 + 判别对照 + 复原，三条纪律与 T37/T38/T39 一致：
 *   · **独立回读**（陷阱 43）：模型说改了不算，要引擎 `getShortcut()` 或
 *     **另开 QSettings 实例**从磁盘读 —— 不复述自己刚写进去的值。
 *   · **判别对照必须有**：SC-04（生成器 vs 手拼 `PageUp`）、SC-07（冲突产生 ⇄ 消解）、
 *     SC-10（合法输入 vs 非法键名被拒）。
 *   · **等待挂写入步**（陷阱 41/69）：`setKey` 里模型会同步 `refresh()`，但
 *     `saveShortcuts()` 的落盘要等一步再读（QSettings 虽然探针实测无需显式 sync，
 *     仍按纪律隔一个事件循环）。
 *
 * ── 判据清单 ───────────────────────────────────────────────────────────────
 *   SC-01 表完整性        `totalCount` == 引擎 `getActionList().size()`；分组数一致
 *   SC-02 行数据抽样一致   前 10 行的 title/主键/备键 == 引擎逐项（不复述模型）
 *   SC-03 customized 口径  与 `conf->contains("shortcuts/"+id)` 全表核对（引擎同判据）
 *   SC-04 键名生成单点     `keySequenceFromEvent` 成对恒等；**判别对照**：手拼
 *                         `PageUp` 直构为空序列（⇒ 键名必须由 C++ 生成的物证）；
 *                         修饰键单独按下 ⇒ 空串（中间态）
 *   SC-05a 改键写引擎      `setKey` 后引擎 getter 独立回读 == 新键
 *   SC-05b 改键落盘        **新 QSettings 实例**从磁盘读，按构造解析路径取首段、
 *                         比**序列语义**（口径⑥：落盘会删分隔空格，字符串比会假红）
 *   SC-06 不写穿           改键前后非 `shortcuts` 组指纹零差异（探针 Q4 的产品级复验）
 *   SC-07 冲突检测         制造冲突 ⇒ `conflictCount>0` 且两行 conflict；**判别对照**：
 *                         消解 ⇒ 归零
 *   SC-08 恢复默认         `restoreDefault` ⇒ 引擎回默认（conf 项被剔除）+ **别人的
 *                         自定义键还在**（探针⑦的产品级复验 —— 探针首轮曾因靶子
 *                         撞车得出相反结论，本轮用**两个不同动作**重钉）
 *   SC-09 空串移除         `setKey(row,0,"")` ⇒ 引擎键空 + 磁盘 `"" ""` 形态 +
 *                         复刻构造解析回解为空（口径⑤：这是合法操作）
 *   SC-10 非法键名闸       `setKey(row,0,"PageUp")` ⇒ **拒绝**（返回 false + 引擎键
 *                         不变 + 状态文本非空）—— 防止"手拼键名静默变删除"
 *   SC-11 全部恢复         `restoreAll` ⇒ `customizedCount==0` + shortcuts 组空
 *   SC-12 UI 控件齐备      视觉树递归（陷阱 45）：页面/搜索框/列表/主键按钮/
 *                         恢复按钮/捕获器全部在场
 *   SC-13 UI 交互端到端    **真实点击**导航按钮切页 → **真实点击**主键按钮（进捕获态）
 *                         → **注入真实 QKeyEvent**（Ctrl+Alt+Shift+F7）→ 引擎与模型
 *                         双确认（T39 血泪：invokeMethod 不发 moved ⇒ 交互腿必须
 *                         真实事件；isVisible 不看视口裁剪 ⇒ 用第 0 行，天然在视口）
 *   SC-14 复原             `restoreAll` + 整本指纹 == S1 基线（探针不许污染用户配置）
 *
 * ── 负控（红项集合必须两两不同）────────────────────────────────────────────
 * 两条负控打的都是 `setKey` 的腿（产品侧**同一个命令面**，见 ShortcutModel.cpp）：
 *   A `STELQUICK_SHORTCUT_WRITE_OFF=1` ⇒ `setKey` **不写引擎**（落盘腿仍在，但没内容可存）
 *   B `STELQUICK_SHORTCUT_SAVE_OFF=1`  ⇒ 写引擎但**不落盘**
 *
 * **实测红项集合（不许美化，这就是真读数）**：
 *   A ⇒ **[SC-05a, SC-05b, SC-07, SC-08, SC-09, SC-11, SC-13]**（8/15，rc=10）
 *   B ⇒ **[SC-05b, SC-09, SC-11]**（12/15，rc=10）
 *   正题 ⇒ **∅**（15/15，rc=0）
 *
 * 🔴 **红项比"靶心"大是**设计的事实**，不是判据缺陷** —— 本项目这套自检是**线性步骤链**
 *   （S1 布场 → … → S14 复原），后步的**前提态由前步的写入产生**。写引擎腿一断，
 *   "前提态"根本不存在 ⇒ 凡**经由 `setKey` 承重**或**依赖前序写入态**的判据必然红。
 *   反过来说，这正是我们要的：**判据真的挂在产品路径上**（假绿才是缺陷，T39 血泪同源）。
 *
 * **两条腿可正交定位**（这才是负控的用处）：
 *   A \ B = {SC-05a, SC-07, SC-08, SC-13} —— 恰是"**引擎写入必须真生效**"的集合；
 *   B \ A = ∅、B = {SC-05b, SC-09, SC-11} —— 恰是"**断言磁盘态**"的集合。
 *   ⇒ 看红项落在哪半边，就能判断坏在写入腿还是落盘腿（而"全绿"只有在两条腿都活着时
 *     才可能出现 —— 正题 15/15 就是这个断言）。
 *   ⚠️ 别把 A 的红项理解成"SC-07/08 也坏了"：它们红是因为**它们的输入没了**。
 *     要看某个判据"本身"是否承重，读 **A\B** 那一半。
 *
 * ── 环境前提 ───────────────────────────────────────────────────────────────
 *   · 合流形态构建 + 引擎引导（非合流 ⇒ UNAVAILABLE rc=6）
 *   · **会写用户配置**（个人版目录，T36 隔离）：改键/恢复全程动 `shortcuts` 组，
 *     S14 复原并核对整本指纹。**中途崩溃可能留下 `Ctrl+Alt+Shift+F9` 等试验键位**
 *     —— 重跑一次本自检即可复原（S11/S14 会清）。
 *   · **不动大气/仿真**（本自检不碰帧与天空，无需冻结仿真）。
 *
 * ── 退出码（由 main.cpp 决定）──────────────────────────────────────────────
 *   0=PASS，10=FAIL，6=UNAVAILABLE / INCONCLUSIVE。
 */
#pragma once

#include <QString>
#include <QStringList>
#include <functional>

class QCoreApplication;
class QQuickWindow;

namespace stelapp {

class AppFacade;
class FrameMailbox;

class ShortcutCheck
{
public:
    struct Result
    {
        bool ran = false;
        bool unavailable = false;
        bool pass = false;
        bool inconclusive = false;
        int passed = 0;
        int total = 0;
        QString summary;
        QStringList details;
    };

    static void run(QCoreApplication *app,
                    AppFacade *facade,
                    QQuickWindow *window,
                    FrameMailbox *mailbox,
                    const std::function<void(const Result &)> &onDone,
                    int delayMs = 2500);
};

} // namespace stelapp
