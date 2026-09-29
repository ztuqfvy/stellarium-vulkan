#!/bin/zsh
# T29 验证脚本（2026-09-29）。判据 + 回归，证据落 docs/evidence/2026-09-29-t29-ime/
#
# 用法：tools/t29-verify.sh          （跑全部）
#       tools/t29-verify.sh core 5   （T29 正题 INTERACTCHECK，跑 N 次；默认 5）
#       tools/t29-verify.sh regress  （A2/DYN + 相邻回归）
#       tools/t29-verify.sh dyn 3    （只跑 DYN，跑 N 次；默认 3）
#
# 环境口径与 t17..t28-verify.sh **完全一致**（同一后端 Metal）——**刻意不换口径**。
#
# ⚠️ 2026-09-29 新发现的环境门（**跑之前必读**）：
#   本机 `pmset -g custom` 电池档 `displaysleep=2`（2 分钟息屏）。套件的 IT-06/IT-13
#   有"窗口必须获得系统焦点"的硬门（focusObject() 失活时恒 nullptr，守卫前提不成立）；
#   **显示器一旦休眠，requestActivate 就再也拿不到焦点** ⇒ IT-06 明确判红"仪器不可用"、
#   且因为它在相位 7，后面 9 条判据全不跑（首轮实测：构建 6 分钟 → 屏幕已睡 →
#   连续 3 跑全卡在 IT-06，判据 5/6）。
#   处置：**跑之前先唤醒 + 全程保持唤醒**（本脚本已内置 `wake_display`）。
#   注意与既有记忆的区别：那条"caffeinate -dimsu 无效"说的是**锁屏**场景；
#   这里是**显示器休眠**场景，caffeinate 有效。别再混淆。
#
# ── T29 的正题：输入法组合键 ───────────────────────────────────────────────────
# 先把事实钉在 Qt 6.11.2 源码上（**全部实证，不是推测**）：
#
# ① 组合期间 macOS **根本不产生 QKeyEvent**：
#      src/plugins/platforms/cocoa/qnsview_keys.mm:137
#        `if (m_sendKeyEvent && m_composingText.isEmpty()) { ... sendWindowSystemEvent }`
#      ⇒ `m_composingText` 非空（正在组合）时这一整段被跳过，空格/数字/回车/Esc
#      都到不了 Qt 层。**"组合中天空快捷键被抢"在平台层就不成立。**
#      （Esc 取消组合走的是 `cancelOperation:`（:185-204）：IME 回调时把
#       m_sendKeyEvent 置 true 后直接 return，最终仍被 :137 的 m_composingText
#       闸门挡住 ⇒ 不发 key event。）
#
# ② 非组合态真正的漏洞在**我们自己的 QML 里**：
#      QQuickTextInput::processKeyEvent（qquicktextinput.cpp:4747-4797）对
#      "未命中任何编辑键 + 文本不可接受"的键走 `event->ignore()`；Esc 不在它处理的
#      QKeySequence 列表里（全文件无 `QKeySequence::Cancel`）⇒ ignore()
#      ⇒ QQuickDeliveryAgentPrivate::deliverKeyEvent 的
#        `while (!e->isAccepted() && (item = item->parentItem()))`
#        （qquickdeliveryagent.cpp:994-999）沿父链冒泡到 keySink
#      ⇒ 而 `MainWindow.qml` 的 Esc 返回分支写在**焦点守卫之前** ⇒
#        焦点在搜索框时按 Esc **直接跳页**，把用户半途的输入丢在搜索框里。
#      与 U-ACT-03「焦点在可编辑控件时不触发天空快捷键」自相矛盾：**守卫被绕过**。
#
# 修法：Esc 分支先问 `ActionRouter.canDispatchToSky()`（口径只有一份，在 C++；
#   QML 不复刻判断）。输入控件持焦 ⇒ Esc 收下但不做事。
#   刻意**不**顺手"清空输入框"——那是新增交互特性，得单独立项 + 单独判据。
#
# 判据（INTERACTCHECK 12 → 16，新增 IT-13..16）：
#   IT-13 IME 组合链：注入 preedit → preeditText 变组合串 **且** inputMethodComposing
#         置位 **且** text 不被污染（成对三腿；只断言第一条会放过"把 preedit 当 text
#         插入"的实现）。这是合流形态的输入法通路**首次**被端到端测到。
#   IT-14 **组合态注入 Esc 不得跳页**（页不切 + rate 不变 + dispatched 不增）。
#         真机组合中 Esc 由 IME 消费、无 key event ⇒ 本判据**刻意比真实更严苛**：
#         即使它到达，也必须被守卫拦住。**修复前必红的那一条。**
#   IT-15 IME 提交：commit 真写进 text（=组合前正文+提交串）+ composing 归位
#         + preeditText 清空。
#   IT-16 **判别性对照**：同一个键、同一个页，只把焦点移出输入控件 ⇒ Esc 必须
#         **仍然**返回天空页。IT-14 是否定式判据，单独绿没有意义（把它实现成
#         "搜索页总是吞掉 Esc" 也会绿）；这条对照就是它的"证明它会红"（血泪第 20 条）。
#
# 三轮代码级负控（各命中**一条**判据、互不串扰；判据已刻意解耦）：
#   ① 撤掉守卫（回 T29 修复前写法）        → 15/16，**只有 IT-14 红**（页 2→1）
#   ② 过宽修复（无条件吞 Esc）             → 15/16，**只有 IT-16 红**（页没切）
#      ⚠️ ②下 IT-14 是**假绿** —— 这正是"否定式判据必须自带对照"的实证
#   ③ 判据取值敏感性（preedit 注入值改成 yueqiu-bogus）→ 15/16，**只有 IT-13 红**
#   （①-③ 日志见 negctrl/；改动用 /tmp 备份 + cmp 校验还原，
#     **绝不用 `git checkout --`**：会连未提交的本次改动一起抹掉）
#   ⚠️ 血泪补充：备份快照必须在**所有编辑完成之后**取。本轮踩了一次——
#      备份早取一步，一次还原把"判据解耦"改动一起回滚了（跑出的 5 次
#      "全绿"其实是旧版判据的读数，靠日志文案差异才发现）。
#
# 为什么**不跑** S3 旧宿主（stellarium）：
#   本轮改动面 = `src/ui/qml/MainWindow.qml` + `src/ui/main.cpp`。二者都不在旧宿主
#   的构建目标里（旧宿主是 Widgets + 自己的 main），且**未动 `src/core/`**、
#   未动任何两形态共用的引擎路径 ⇒ S3 对本轮改动**不敏感**，跑了也只是噪音。
#   （T28 跑 S3 是因为捏合链终点 `StelMovementMgr::handlePinch` 与旧宿主共用；
#     T29 的 Esc 守卫是纯 QML 层，没有对应的共用路径。）
set -u
cd /Users/ztuqfvy/qt_demo/stellarium_vulkan

export VK_DRIVER_FILES=/opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json
export QT_VULKAN_LIB=/opt/homebrew/opt/vulkan-loader/lib/libvulkan.1.dylib
export STELQUICK_GRAPHICS_API=metal

BIN=./build-release/src/ui/stelQuickUI.app/Contents/MacOS/stelQuickUI
OUT=docs/evidence/2026-09-29-t29-ime
TARGET=${1:-all}
CORE_RUNS=${2:-5}
DYN_RUNS=${2:-3}
mkdir -p "$OUT"

# 显示器休眠门（见头注）：先模拟用户活动唤醒，再挂一个长时断言保持唤醒。
wake_display() {
  caffeinate -u -t 2 >/dev/null 2>&1
  caffeinate -dimsu -t 7200 >/dev/null 2>&1 &
  CAFFEINATE_PID=$!
  sleep 1
  echo "环境：已请求唤醒显示器并挂 caffeinate（pid=$CAFFEINATE_PID，2 小时）"
}
CAFFEINATE_PID=""
wake_display

if [[ "$TARGET" == "dyn" ]]; then
  if [[ -f "$OUT/rc-summary.txt" ]]; then
    grep -vE "dyn-(engine|stub)-metal" "$OUT/rc-summary.txt" \
      > "$OUT/rc-summary.txt.tmp" 2>/dev/null || : > "$OUT/rc-summary.txt.tmp"
    mv "$OUT/rc-summary.txt.tmp" "$OUT/rc-summary.txt"
  fi
else
  : > "$OUT/rc-summary.txt"
fi

env_note() {
  echo "环境：$(uptime | sed 's/^ *//')"
  local ds
  ds=$(pmset -g custom 2>/dev/null | grep -E "^[[:space:]]*displaysleep" | head -1 | awk '{print $2}')
  [[ -n "$ds" ]] && echo "环境注记：displaysleep=${ds} 分钟（**本机为 2** ⇒ 跑前必须 caffeinate 唤醒，见脚本头注）"
  if pgrep -q mdbulkimport; then
    echo "环境注记：Spotlight 批量索引正在运行（mdbulkimport）——点击注入可能被干扰"
  fi
  echo '读数采信规则：窗口未获得系统焦点时 focusObject()==nullptr，IT-06/IT-13 的守卫前提'
  echo '  不成立（判据的按键是 sendEvent 直达窗口、绕过系统焦点）⇒ 明确报「仪器不可用」'
  echo '  并 rc=10，**不洗成 PASS、也不改写成 INVALID**。处置是 caffeinate 唤醒后重跑。'
}

run_dyn_engine() {
  local n=$DYN_RUNS pass=0 i rc
  : > "$OUT/regression-dyn-engine-metal.txt"
  for i in $(seq 1 $n); do
    # ⚠️ 2026-09-29 更正（W-T29 跨平台复验时抓到）：此处原先漏了
    #    `STELQUICK_DYN_PRODUCER=engine`（t16..t28 全都有），而 main.cpp:4120-4121
    #    只在**显式等于 "engine"** 时才装真实引擎 ⇒ 原版这里跑的是**替身**，
    #    所谓"真实引擎 vs 替身"的判别性对照实际是两次替身。
    #    本行已补上；T29 当轮已归档的日志保持原样（append-only），
    #    更正说明见 docs/evidence/2026-09-29-t29-ime/CORRECTION.md。
    STELQUICK_DYN_CHECK=1 STELQUICK_DYN_PRODUCER=engine "$BIN" \
      > "$OUT/regression-dyn-engine-metal-run$i.txt" 2>&1
    rc=$?
    echo "──────── DYN(真实引擎) run $i/$n（rc=$rc）────────" >> "$OUT/regression-dyn-engine-metal.txt"
    grep -E "DYNCHECK:" "$OUT/regression-dyn-engine-metal-run$i.txt" \
      >> "$OUT/regression-dyn-engine-metal.txt"
    [[ $rc -eq 0 ]] && (( pass++ ))
  done
  {
    echo "──────── 汇总 ────────"
    env_note
    echo "DYN 显示/降级判据（真实引擎生产者）：$pass/$n 次 PASS"
    echo "定性：本机为环境敏感/间歇（T18 的 A/B 记录见 t18 证据目录）。"
    echo "零退化的依据是同口径 A/B + 替身路径不含本任务代码，不是本行的某一次绿色。"
  } >> "$OUT/regression-dyn-engine-metal.txt"
  cat "$OUT/regression-dyn-engine-metal.txt"
  echo "regression-dyn-engine-metal $pass/$n PASS（间歇，DYN 环境敏感）" \
    | tee -a "$OUT/rc-summary.txt"
}

run_dyn_stub() {
  local n=$DYN_RUNS pass=0 i rc
  : > "$OUT/regression-dyn-stub-metal.txt"
  for i in $(seq 1 $n); do
    STELQUICK_DYN_CHECK=1 STELQUICK_DYN_PRODUCER=test "$BIN" \
      > "$OUT/regression-dyn-stub-metal-run$i.txt" 2>&1
    rc=$?
    echo "──────── DYN(替身) run $i/$n（rc=$rc）────────" >> "$OUT/regression-dyn-stub-metal.txt"
    grep -E "DYNCHECK:" "$OUT/regression-dyn-stub-metal-run$i.txt" \
      >> "$OUT/regression-dyn-stub-metal.txt"
    [[ $rc -eq 0 ]] && (( pass++ ))
  done
  {
    echo "──────── 汇总 ────────"
    env_note
    echo "DYN 显示/降级判据（替身生产者，不 boot 引擎）：$pass/$n 次 PASS"
    echo "替身路径不含 T29 的 IME/Esc 守卫代码（它不跑 INTERACTCHECK、不注入任何键）"
    echo "⇒ 它失败即与本次改动无关。"
  } >> "$OUT/regression-dyn-stub-metal.txt"
  cat "$OUT/regression-dyn-stub-metal.txt"
  echo "regression-dyn-stub-metal $pass/$n PASS（判别性对照：替身不含 T29 代码）" \
    | tee -a "$OUT/rc-summary.txt"
}

if [[ "$TARGET" == "all" || "$TARGET" == "core" ]]; then
  # ① T29 正题：INTERACTCHECK × N（判据 16 条：IT-01..IT-16）
  integer pass=0 i rc
  pass=0
  : > "$OUT/interactcheck-mac-n$CORE_RUNS.txt"
  for i in $(seq 1 $CORE_RUNS); do
    STELQUICK_INTERACT_UI_CHECK=1 "$BIN" > "$OUT/interactcheck-mac-run$i.txt" 2>&1
    rc=$?
    echo "──────── INTERACTCHECK run $i/$CORE_RUNS（rc=$rc）────────" >> "$OUT/interactcheck-mac-n$CORE_RUNS.txt"
    grep -E "INTERACTCHECK:" "$OUT/interactcheck-mac-run$i.txt" \
      >> "$OUT/interactcheck-mac-n$CORE_RUNS.txt"
    [[ $rc -eq 0 ]] && (( pass++ ))
  done
  echo "interactcheck-mac $pass/$CORE_RUNS PASS" | tee -a "$OUT/rc-summary.txt"

  # ② 相邻回归：T26 搜索（IME 判据改的正是搜索页的键盘面）
  STELQUICK_SEARCH_CHECK=1 "$BIN" > "$OUT/regression-searchcheck.txt" 2>&1
  echo "regression-searchcheck rc=$?" | tee -a "$OUT/rc-summary.txt"

  # ③ 相邻回归：T18 定位两套 + T15 命令面（Esc 守卫改的是 routeKey 的前置判断）
  STELQUICK_ACTION_CHECK=1 "$BIN" > "$OUT/regression-actioncheck.txt" 2>&1
  echo "regression-actioncheck rc=$?" | tee -a "$OUT/rc-summary.txt"
  STELQUICK_LOCATE_CHECK=1 "$BIN" > "$OUT/regression-locatecheck.txt" 2>&1
  echo "regression-locatecheck rc=$?" | tee -a "$OUT/rc-summary.txt"
  STELQUICK_UI_CHECK=1 "$BIN" > "$OUT/regression-locate-uicheck.txt" 2>&1
  echo "regression-locate-uicheck rc=$?" | tee -a "$OUT/rc-summary.txt"

  # ④ 相邻回归：T19/T20/T22（**T20 的 Esc 返回是本次修复的直接邻居**）
  STELQUICK_TIME_CHECK=1 "$BIN" > "$OUT/regression-timecheck.txt" 2>&1
  echo "regression-timecheck rc=$?" | tee -a "$OUT/rc-summary.txt"
  STELQUICK_TIME_UI_CHECK=1 "$BIN" > "$OUT/regression-timeuicheck.txt" 2>&1
  echo "regression-timeuicheck rc=$?" | tee -a "$OUT/rc-summary.txt"
  STELQUICK_RETURN_UI_CHECK=1 "$BIN" > "$OUT/regression-returnuicheck.txt" 2>&1
  echo "regression-returnuicheck rc=$?" | tee -a "$OUT/rc-summary.txt"
  STELQUICK_REPLAY_CHECK=1 "$BIN" > "$OUT/regression-replaycheck.txt" 2>&1
  echo "regression-replaycheck rc=$?" | tee -a "$OUT/rc-summary.txt"

  # ⑤ T16 时钟
  STELQUICK_CLOCK_CHECK=1 "$BIN" > "$OUT/regression-clockcheck.txt" 2>&1
  echo "regression-clockcheck rc=$?" | tee -a "$OUT/rc-summary.txt"
fi

if [[ "$TARGET" == "all" || "$TARGET" == "regress" ]]; then
  # ⑥ 回归：A2 静态纹理（Metal）——本轮动了 MainWindow.qml（keySink 的 Esc 分支），
  #    QML 页外层改动必须证明没有污染逐像素探针 ⇒ 硬要求
  STELQUICK_A2_CHECK=1 "$BIN" > "$OUT/regression-a2-metal.txt" 2>&1
  echo "regression-a2-metal rc=$?" | tee -a "$OUT/rc-summary.txt"

  # ⑦ DYN 双跑（真实引擎 + 替身判别性对照）
  run_dyn_engine
  run_dyn_stub
fi

if [[ "$TARGET" == "dyn" ]]; then
  run_dyn_engine
  run_dyn_stub
fi

cat "$OUT/rc-summary.txt"
[[ -n "$CAFFEINATE_PID" ]] && kill "$CAFFEINATE_PID" 2>/dev/null
echo "T29 证据已写入 $OUT/"
