# T29 证据 — 输入法组合键与 Esc 守卫（2026-09-29）

> 交付文档：`docs/T29_IME_KEY_GUARD.zh_CN.md` ｜ 一键复跑：`tools/t29-verify.sh`
> 环境口径与 t17..t28 **完全一致**（macOS + Metal RHI，`STELQUICK_GRAPHICS_API=metal`）——
> 刻意不换口径。

## 结论

**正题 `INTERACTCHECK 16/16 rc=0 ×5`**（判据 12 → 16，新增 IT-13..IT-16），
5 跑 IT-13..16 读数 `md5` **逐位一致**。
**回归 11 项全 `rc=0`**（含 A2 逐像素 12/12 探针、DYN 引擎 **5/5** + 替身 **5/5**）。
**三轮代码级负控各命中一条判据、互不串扰。**

## 缺陷（一句话）

`MainWindow.qml` 的 **Esc 返回分支写在焦点守卫之前** ⇒ 焦点在搜索框时按 Esc **直接跳页**，
把用户半途的输入丢在框里 —— 与 U-ACT-03「焦点在可编辑控件时不触发天空快捷键」
自相矛盾：**守卫被绕过**。

## 真链（Qt 6.11.2 源码实证，原文见 `probe-qt-source-evidence.txt`）

1. `QQuickDeliveryAgentPrivate::deliverKeyEvent`（`qquickdeliveryagent.cpp:971-1001`）
   先 `accept()` 再投递，**只有被 `ignore()` 才沿父链冒泡**：
   `while (!e->isAccepted() && (item = item->parentItem()))`。
2. `QQuickTextInputPrivate::processKeyEvent`（`qquicktextinput.cpp:4747-4798`）对
   "未命中编辑键 + `isAcceptableInput()` 为假"的键走 `event->ignore()`；
   **Esc 不在它处理的 QKeySequence 列表里**（全文件无 `QKeySequence::Cancel`）。
   `QInputControl::isAcceptableInput`（`qinputcontrol.cpp:23-55`）对空文本/控制字符返回 false。
3. ⇒ 冒泡到 `keySink` 的 `Keys.onPressed` ⇒ 修复前**绕过守卫**直接 `returnToSky()`。

## 一个**不存在**的缺陷（单列，防后来人凭想象修 bug）

`qnsview_keys.mm:137`：`if (m_sendKeyEvent && m_composingText.isEmpty())` 才
`sendWindowSystemEvent`。**组合期间 `m_composingText` 非空 ⇒ 不产生 `QKeyEvent`。**
空格/数字/回车/Esc 取消组合在 macOS 上**到不了应用层**。
（Esc 取消走 `cancelOperation:`（`:185-204`），最终仍被 `:137` 挡住。）

⇒ IT-14 属**"比真实更严苛"**的测试，但它的判别力是**实测**的（负控①下真红），
且它保护的是"即使到达也必须被拦"这条**不变式**（换平台 / 将来 QML 层加 pre-edit 处理时会用上）。

## 三轮负控

| # | 手手术 | 结果 | 命中 | 日志 |
|---|---|---|---|---|
| ① | 撤掉守卫（回 T29 修复前写法） | 15/16 rc=10 | **只有 IT-14 红**（页 2→1） | `negctrl/negctrl1-*.log` |
| ② | **过宽修复**：无条件吞掉 Esc | 15/16 rc=10 | **只有 IT-16 红**（页没切）；⚠️ IT-14 **假绿** | `negctrl/negctrl2-*.log` |
| ③ | 判据取值敏感性：preedit 注入 `yueqiu-bogus` | 15/16 rc=10 | **只有 IT-13 红** | `negctrl/negctrl3-*.log` |

- **负控②是"否定式判据单独绿没有意义"的实证**（血泪第 20 条第二次实例）：
  同一个"Esc 不跳页"的断言在过宽修复下绿得毫无意义，只有 IT-16 能拆穿。
- 负控①②改 `.qml`、③改 `main.cpp`；三份文件先 `cp` 到 `/tmp/t29-bak/`，
  每轮后 `md5`/`cmp` 校验还原。**绝不用 `git checkout --`**。
- ⚠️ **备份快照必须在所有编辑完成之后取**：本轮踩过一次——备份早取一步，
  一次还原把"判据解耦"改动一起回滚，跑出的 5 次"全绿"其实是旧版判据的读数
  （靠日志文案差异才发现）。详见交付文档 §7.2。

## 仪器不可用留档（新环境门）

`instrument-unavailable-first-attempt.txt`（另见 `negctrl/` 同名文件）：
本机 `pmset -g custom` 电池档 **`displaysleep=2`**（2 分钟息屏）。构建几分钟后屏幕已睡 ⇒
`requestActivate()` 拿不到焦点 ⇒ `focusObject()` 恒 `nullptr` ⇒ **IT-06 明确判红
「仪器不可用」`exit(10)`**，且因 IT-06 在相位 7，**后面 9 条判据全不跑**（连续 3 跑 `判据 5/6`）。

处置：`tools/t29-verify.sh` 内置 `wake_display()`（`caffeinate -u -t 2` 唤醒 +
`caffeinate -dimsu -t 7200` 常驻）⇒ 之后 5/5 稳定。
⚠️ 与既有记忆的区别：那条"`caffeinate -dimsu` 无效"说的是**锁屏**；这里是**显示器休眠**，
`caffeinate` **有效**。

## 为什么回归里没有 S3 旧宿主

本轮改动面 = `src/ui/qml/MainWindow.qml` + `src/ui/main.cpp`，**二者都不在旧宿主
（stellarium）的构建目标里**，且未动 `src/core/`、未动两形态共用的引擎路径 ⇒
S3 对本轮改动不敏感。这是**主动论证后的取舍**，不是漏跑。
（T28 跑 S3 是因为捏合链终点 `StelMovementMgr::handlePinch` 与旧宿主共用；T29 的
Esc 守卫是纯 QML 层，没有对应的共用路径。）

## 文件清单

| 文件 | 内容 |
|---|---|
| `probe-qt-source-evidence.txt` | Qt 6.11.2 源码实证原文摘录（328 行，行号即 tag 内实际行号） |
| `interactcheck-mac-run{1..5}.txt` / `-n5.txt` | 正题 5 跑原始日志 + 汇总 |
| `negctrl/` | 三轮负控 + 仪器不可用留档（4 份） |
| `regression-*.txt` | 10 项相邻回归（含 `a2-metal` 逐像素） |
| `regression-dyn-{engine,stub}-metal*` | DYN 双跑（真实引擎 5/5、替身 5/5，判别性对照） |
| `rc-summary.txt` | 退出码汇总 |
