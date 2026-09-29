#!/bin/zsh
# W-T29 验证脚本（2026-09-29）。macOS 侧的**复验**：本任务在两台机器上跑同一份
# 自检套件，而 W-T29 这一轮改的正是自检仪器本身（`src/ui/main.cpp`），所以
# macOS 侧必须重跑一遍，证明"仪器改动没有把 macOS 的读数改坏"。
#
# 用法：tools/wt29-verify.sh            （跑全部）
#       tools/wt29-verify.sh core 5     （只跑 INTERACTCHECK ×5 + 相邻回归）
#       tools/wt29-verify.sh regress    （只跑 A2 逐像素 + DYN 双跑）
#       tools/wt29-verify.sh dyn 3      （只跑 DYN）
#
# 证据落 docs/evidence/2026-09-29-w-t29/mac/。
# ⚠️ 与 tools/t29-verify.sh 的区别**只有一个**：OUT 目录不同。
#    T29 那份脚本的证据是 T29 的（正题=IME 组合键 + Esc 守卫），
#    W-T29 这份的证据是**跨平台复验**的（正题=Windows 侧的首轮两红与根因）。
#    环境口径（Metal）**刻意与 t17..t29 完全一致，不换**。
#
# ── 本轮改动面与判据影响 ──────────────────────────────────────────────────────
# 改动 = `src/ui/main.cpp` 的**仪器**，两处：
#   ① `uiFocusSnapshot()` —— 键派发后取五项原始状态（windowActive /
#      keySinkHasActiveFocus / activeFocusItem.objectName / focusObject 类名 /
#      canDispatchToSky()）。**只报原始量，不复刻被测逻辑**（血泪第 4 条）。
#   ② `uiWindowActivationGate()` —— **前导**有界激活门（≤3×400ms），装在
#      `uiReturnStep` / `uiInteractStep` 的 **switch 之前**。
#
# **判据口径一条没改**（16 条还是 16 条、11 条还是 11 条，门槛全部原样）。
# 所以本脚本的期望与 t29-verify.sh 完全一致：INTERACTCHECK 16/16、RETURNUI 11/11。
# 如果 macOS 上出现任何偏离，那是仪器改坏了 —— 不是"环境差异"，必须查。
#
# ── 为什么加激活门（Windows 侧首跑实证，见 docs/T29W_CROSS_PLATFORM.zh_CN.md）────
# Windows 上由 `schtasks /it` 投递到交互会话的进程受 **foreground lock** 限制，
# 窗口默认**不是**前台窗口；窗口未激活时 `keySink->forceActiveFocus()` 拿不到
# active focus ⇒ 注入的键根本没被派发到 QML。
# 实证：INTERACTCHECK IT-05（该套件第一次键注入）红，而紧接的 IT-06 相位做了
# `requestActivate` 重试（日志"窗口未激活（尝试 1/3）"）之后，余下 11 条键/手势
# 判据全绿；RETURNUI 完全没有激活门，它**唯一**的键注入 RT-10 恰好红。
# 加了前导门之后 Windows 侧 18/18 全 rc=0（同一份二进制）。
# ⚠️ 门**超时不洗成 PASS**：只留一条 note，后续键类判据照原样判红。
set -u
cd /Users/ztuqfvy/qt_demo/stellarium_vulkan

export VK_DRIVER_FILES=/opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json
export QT_VULKAN_LIB=/opt/homebrew/opt/vulkan-loader/lib/libvulkan.1.dylib
export STELQUICK_GRAPHICS_API=metal

BIN=./build-release/src/ui/stelQuickUI.app/Contents/MacOS/stelQuickUI
OUT=docs/evidence/2026-09-29-w-t29/mac
TARGET=${1:-all}
CORE_RUNS=${2:-5}
DYN_RUNS=${2:-3}
mkdir -p "$OUT"

# 显示器休眠门（T29 的教训，照抄）：本机 `pmset -g custom` 电池档 displaysleep=2。
# 显示器一旦睡，`requestActivate` 再也拿不到焦点 ⇒ 焦点类判据前提不成立 ⇒ 假红。
wake_display() {
  caffeinate -u -t 2 >/dev/null 2>&1
  caffeinate -dimsu -t 7200 >/dev/null 2>&1 &
  CAFFEINATE_PID=$!
  sleep 1
  echo "环境：已请求唤醒显示器并挂 caffeinate（pid=$CAFFEINATE_PID，2 小时）"
}
CAFFEINATE_PID=""
wake_display

env_note() {
  echo "环境：$(uptime | sed 's/^ *//')"
  local ds
  ds=$(pmset -g custom 2>/dev/null | grep -E "^[[:space:]]*displaysleep" | head -1 | awk '{print $2}')
  [[ -n "$ds" ]] && echo "环境注记：displaysleep=${ds} 分钟（本机为 2 ⇒ 跑前必须 caffeinate 唤醒）"
  if pgrep -q mdbulkimport; then
    echo "环境注记：Spotlight 批量索引正在运行（mdbulkimport）——点击注入可能被干扰"
  fi
  echo "二进制：$(ls -l "$BIN" | awk '{print $5" B  "$6" "$7" "$8}')  md5=$(md5 -q "$BIN")"
}

: > "$OUT/rc-summary.txt"

# ══ ① 正题：INTERACTCHECK × N（16 条判据 IT-01..IT-16）═══════════════════════
if [[ "$TARGET" == "all" || "$TARGET" == "core" ]]; then
  integer pass=0 i rc
  : > "$OUT/interactcheck-mac-n$CORE_RUNS.txt"
  for i in $(seq 1 $CORE_RUNS); do
    STELQUICK_INTERACT_UI_CHECK=1 "$BIN" > "$OUT/interactcheck-mac-run$i.txt" 2>&1
    rc=$?
    echo "──────── INTERACTCHECK run $i/$CORE_RUNS（rc=$rc）────────" >> "$OUT/interactcheck-mac-n$CORE_RUNS.txt"
    grep -E "INTERACTCHECK:" "$OUT/interactcheck-mac-run$i.txt" >> "$OUT/interactcheck-mac-n$CORE_RUNS.txt"
    [[ $rc -eq 0 ]] && (( pass++ ))
  done
  echo "interactcheck-mac $pass/$CORE_RUNS PASS" | tee -a "$OUT/rc-summary.txt"

  # ══ ② 前导门直接影响的套件：RETURNUI（它唯一那条键注入就是 RT-10）═══════════
  STELQUICK_RETURN_UI_CHECK=1 "$BIN" > "$OUT/regression-returnuicheck.txt" 2>&1
  echo "regression-returnuicheck rc=$?" | tee -a "$OUT/rc-summary.txt"

  # ══ ③ 相邻回归：搜索（键盘面）/命令面（routeKey 前置）/定位/时间/回放/时钟 ══
  for spec in \
    "STELQUICK_SEARCH_CHECK=1:searchcheck" \
    "STELQUICK_ACTION_CHECK=1:actioncheck" \
    "STELQUICK_LOCATE_CHECK=1:locatecheck" \
    "STELQUICK_UI_CHECK=1:locate-uicheck" \
    "STELQUICK_TIME_CHECK=1:timecheck" \
    "STELQUICK_TIME_UI_CHECK=1:timeuicheck" \
    "STELQUICK_REPLAY_CHECK=1:replaycheck" \
    "STELQUICK_CLOCK_CHECK=1:clockcheck"
  do
    var=${spec%%:*}; name=${spec##*:}
    env $var "$BIN" > "$OUT/regression-$name.txt" 2>&1
    echo "regression-$name rc=$?" | tee -a "$OUT/rc-summary.txt"
  done
fi

# ══ ④ A2 逐像素（Metal）═════════════════════════════════════════════════════
# 本轮没有动 `.qml`，但 main.cpp 是同一个二进制的另一半 ⇒ 顺手确认逐像素探针
# 没被污染（便宜，且能挡住"仪器改动顺手碰了渲染路径"这类意外）。
if [[ "$TARGET" == "all" || "$TARGET" == "regress" || "$TARGET" == "core" ]]; then
  STELQUICK_A2_CHECK=1 "$BIN" > "$OUT/regression-a2-metal.txt" 2>&1
  echo "regression-a2-metal rc=$?" | tee -a "$OUT/rc-summary.txt"
fi

# ══ ⑤ DYN 双跑（真实引擎 + 替身判别性对照）══════════════════════════════════
# ⚠️ 生产者必须**显式**指定：`STELQUICK_DYN_PRODUCER` 默认是替身（main.cpp:4120-4121
#    只看 `== "engine"`），不设它就两次都跑替身。
#    🔴 本轮跨平台复验时抓到：`tools/t29-verify.sh` 的 `run_dyn_engine()` **漏了**这个
#    变量（t16..t28 全都有，只有 t29 丢了）⇒ T29 那条"真实引擎 vs 替身"的判别性对照
#    实际上跑的是**两次替身**（两份日志都打印 `生产者=test(替身场景)`）。
#    证据归档未改（append-only 精神），更正说明见
#    docs/evidence/2026-09-29-t29-ime/CORRECTION.md。
run_dyn() {
  local kind=$1 n=$DYN_RUNS pass=0 i rc
  : > "$OUT/regression-dyn-$kind-metal.txt"
  for i in $(seq 1 $n); do
    STELQUICK_DYN_CHECK=1 STELQUICK_DYN_PRODUCER=$kind "$BIN" \
      > "$OUT/regression-dyn-$kind-metal-run$i.txt" 2>&1
    rc=$?
    echo "──────── DYN($kind) run $i/$n（rc=$rc）────────" >> "$OUT/regression-dyn-$kind-metal.txt"
    grep -E "DYNCHECK:" "$OUT/regression-dyn-$kind-metal-run$i.txt" >> "$OUT/regression-dyn-$kind-metal.txt"
    [[ $rc -eq 0 ]] && (( pass++ ))
  done
  { echo "──────── 汇总 ────────"; env_note
    echo "DYN 显示/降级判据（producer=$kind）：$pass/$n 次 PASS"
    echo "生产者回读：$(/usr/bin/grep -m1 -h '生产者=' "$OUT/regression-dyn-$kind-metal-run1.txt" 2>/dev/null)"
  } >> "$OUT/regression-dyn-$kind-metal.txt"
  echo "regression-dyn-$kind-metal $pass/$n PASS" | tee -a "$OUT/rc-summary.txt"
}
if [[ "$TARGET" == "all" || "$TARGET" == "regress" || "$TARGET" == "dyn" ]]; then
  run_dyn engine
  run_dyn test
fi

cat "$OUT/rc-summary.txt"
[[ -n "$CAFFEINATE_PID" ]] && kill "$CAFFEINATE_PID" 2>/dev/null
echo "W-T29 macOS 复验证据已写入 $OUT/"
