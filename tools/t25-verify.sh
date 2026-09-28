#!/bin/zsh
# T25 验证脚本（2026-09-28）。判据 + 回归，证据落 docs/evidence/2026-09-28-t25-interact/
#
# 用法：tools/t25-verify.sh          （跑全部）
#       tools/t25-verify.sh core 5   （T25 正题，跑 N 次；默认 5）
#       tools/t25-verify.sh regress  （A2/DYN/S3 + 相邻回归）
#       tools/t25-verify.sh dyn 3    （只跑 DYN，跑 N 次；默认 3）
#
# 环境口径与 t17..t24-verify.sh **完全一致**（同一后端 Metal）——**刻意不换口径**。
#
# ── T25 的正题：交互级（键盘/滚轮）UI 层端到端判据（A4 加固项）──────────────
# 两条此前只有"C++ 直调"证据的交互面，本任务补上"最外层注入"证据：
#
# ① 滚轮（INTERACTCHECK IT-01..04）：旧宿主链路 StelMainView::wheelEvent →
#    StelApp::handleWheel 在合流形态**从未接过**（QML 此前无任何 WheelHandler
#    ⇒ 滚轮死路，T15 键盘死代码的同款缺陷）。T25 修复 = SkyTestPage 挂
#    WheelHandler → AppFacade::wheelZoom（friend 进 StelApp）→ handleWheel
#    保真转发。判据：真实 QWheelEvent 注入窗口 → FOV 严格变小（IT-02 成对）+
#    反向滚变大（IT-03 方向对照）+ 时间页滚轮纹丝不动（IT-04 页守卫负控）。
# ② 键盘活链（IT-05）：QKeyEvent 经窗口 → keySink.Keys.onPressed → routeKey，
#    注入真实 L 键断言 timeRate 变化 + dispatched 信号 = actionIncrease_Time_Speed。
#    此前 ActionCheck 是 C++ 直调 routeKey，证明不了 QML 链是活的（T15 教训）。
# ③ 焦点守卫（IT-06）：首跑实抓第二个真缺陷 —— canDispatchToSky() 用 className
#    精确比较 "QQuickTextInput"，真实搜索框是 QQuickTextField（子类）不命中
#    ⇒ 守卫在真实输入框上是死代码（U-ACT-03 的直调判据构造 TextInput 原语，
#    假绿了 15 个任务）。修法 = inherits()。判据：聚焦搜索框注入 L → 必须被拦。
#
# ⚠️ 仪器陷阱（首跑踩中）：uiReturnSendKey 会 forceActiveFocus(keySink)，把焦点
# 从搜索框抢走 —— 仪器亲手拆掉守卫前提。IT-06 必须用"不抢焦点"注入变体。
#
# ⚠️ 动了 src/core/StelApp.hpp（friend 声明，行为零变化）⇒ S3 旧宿主回归是硬要求。
#
# ── DYN 判据为什么要跑 N 次 + 替身对照 ──────────────────────────────────────
# 与 T18..T24 同一处置（完整定性见 2026-09-24-t18-locate-track/dyn-ab-baseline-vs-t18.txt）。
set -u
cd /Users/ztuqfvy/qt_demo/stellarium_vulkan

export VK_DRIVER_FILES=/opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json
export QT_VULKAN_LIB=/opt/homebrew/opt/vulkan-loader/lib/libvulkan.1.dylib
export STELQUICK_GRAPHICS_API=metal

BIN=./build-release/src/ui/stelQuickUI.app/Contents/MacOS/stelQuickUI
OUT=docs/evidence/2026-09-28-t25-interact
TARGET=${1:-all}
CORE_RUNS=${2:-5}
DYN_RUNS=${2:-3}
mkdir -p "$OUT"

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
  [[ -n "$ds" ]] && echo "环境注记：displaysleep=${ds} 分钟（显示器空闲即休眠）"
  if pgrep -q mdbulkimport; then
    echo "环境注记：Spotlight 批量索引正在运行（mdbulkimport）——帧率读数可能被压低"
  fi
  echo '读数采信规则：D1-C02/D1-C07 量的是「窗口有没有在渲染」——它要求窗口被暴露。'
  echo '  若同条件的**替身对照**也失败，则该读数反映「仪器测不到」而不是「被测程序失败」，'
  echo '  不作为退化证据；也不改写成 INVALID（原始读数照实保留，只加这条定性）。'
}

run_dyn() {
  local n=$DYN_RUNS pass=0 i rc
  : > "$OUT/regression-dyn-engine-metal.txt"
  for i in $(seq 1 $n); do
    STELQUICK_DYN_CHECK=1 STELQUICK_DYN_PRODUCER=engine "$BIN" \
      > "$OUT/regression-dyn-engine-metal-run$i.txt" 2>&1
    rc=$?
    echo "──────── DYN run $i/$n（rc=$rc）────────" >> "$OUT/regression-dyn-engine-metal.txt"
    grep -E "DYNCHECK:" "$OUT/regression-dyn-engine-metal-run$i.txt" \
      >> "$OUT/regression-dyn-engine-metal.txt"
    [[ $rc -eq 0 ]] && (( pass++ ))
  done
  {
    echo "──────── 汇总 ────────"
    env_note
    echo "DYN 显示/降级判据（真实引擎生产者）：$pass/$n 次 PASS"
    echo "定性：本机为环境敏感/间歇（T18 的 A/B 记录见 dyn-ab-baseline-vs-t18.txt）。"
    echo "零退化的依据是同口径 A/B + 替身路径不含本任务代码，不是本行的某一次绿色。"
  } >> "$OUT/regression-dyn-engine-metal.txt"
  cat "$OUT/regression-dyn-engine-metal.txt"
  echo "regression-dyn-engine-metal $pass/$n PASS（间歇，见 dyn-ab-baseline-vs-t18.txt）" \
    | tee -a "$OUT/rc-summary.txt"
}

run_dyn_stub() {
  local n=$DYN_RUNS pass=0 i rc
  : > "$OUT/regression-dyn-stub-metal.txt"
  for i in $(seq 1 $n); do
    STELQUICK_DYN_CHECK=1 "$BIN" > "$OUT/regression-dyn-stub-metal-run$i.txt" 2>&1
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
    echo "替身路径不含 T25 交互级代码（DYN 用 startPage=\"sky\" 且不点任何按钮）"
    echo "⇒ 它失败即与本次改动无关。"
  } >> "$OUT/regression-dyn-stub-metal.txt"
  cat "$OUT/regression-dyn-stub-metal.txt"
  echo "regression-dyn-stub-metal $pass/$n PASS（判别性对照：替身不含 T25 代码）" \
    | tee -a "$OUT/rc-summary.txt"
}

if [[ "$TARGET" == "all" || "$TARGET" == "core" ]]; then
  # ① T25 正题：INTERACTCHECK × N（判据 6 条：IT-01..06）
  integer pass=0 i rc
  pass=0  : > "$OUT/interactcheck-mac-n$CORE_RUNS.txt"
  for i in $(seq 1 $CORE_RUNS); do
    STELQUICK_INTERACT_UI_CHECK=1 "$BIN" > "$OUT/interactcheck-mac-run$i.txt" 2>&1
    rc=$?
    echo "──────── INTERACTCHECK run $i/$CORE_RUNS（rc=$rc）────────" >> "$OUT/interactcheck-mac-n$CORE_RUNS.txt"
    grep -E "INTERACTCHECK:" "$OUT/interactcheck-mac-run$i.txt" \
      >> "$OUT/interactcheck-mac-n$CORE_RUNS.txt"
    [[ $rc -eq 0 ]] && (( pass++ ))
  done
  echo "interactcheck-mac $pass/$CORE_RUNS PASS" | tee -a "$OUT/rc-summary.txt"

  # ② 相邻回归：T18 定位两套（焦点守卫 belongs ActionRouter，相邻最紧）
  STELQUICK_ACTION_CHECK=1 "$BIN" > "$OUT/regression-actioncheck.txt" 2>&1
  echo "regression-actioncheck rc=$?" | tee -a "$OUT/rc-summary.txt"
  STELQUICK_LOCATE_CHECK=1 "$BIN" > "$OUT/regression-locatecheck.txt" 2>&1
  echo "regression-locatecheck rc=$?" | tee -a "$OUT/rc-summary.txt"
  STELQUICK_UI_CHECK=1 "$BIN" > "$OUT/regression-locate-uicheck.txt" 2>&1
  echo "regression-locate-uicheck rc=$?" | tee -a "$OUT/rc-summary.txt"

  # ③ 相邻回归：T19/T20（动过 SkyTestPage.qml / startPage 选择）
  STELQUICK_TIME_CHECK=1 "$BIN" > "$OUT/regression-timecheck.txt" 2>&1
  echo "regression-timecheck rc=$?" | tee -a "$OUT/rc-summary.txt"
  STELQUICK_TIME_UI_CHECK=1 "$BIN" > "$OUT/regression-timeuicheck.txt" 2>&1
  echo "regression-timeuicheck rc=$?" | tee -a "$OUT/rc-summary.txt"
  STELQUICK_RETURN_UI_CHECK=1 "$BIN" > "$OUT/regression-returnuicheck.txt" 2>&1
  echo "regression-returnuicheck rc=$?" | tee -a "$OUT/rc-summary.txt"
  STELQUICK_REPLAY_CHECK=1 "$BIN" > "$OUT/regression-replaycheck.txt" 2>&1
  echo "regression-replaycheck rc=$?" | tee -a "$OUT/rc-summary.txt"

  # ④ T17 搜索模型 + T16 时钟
  STELQUICK_SEARCH_CHECK=1 "$BIN" > "$OUT/regression-searchcheck.txt" 2>&1
  echo "regression-searchcheck rc=$?" | tee -a "$OUT/rc-summary.txt"
  STELQUICK_CLOCK_CHECK=1 "$BIN" > "$OUT/regression-clockcheck.txt" 2>&1
  echo "regression-clockcheck rc=$?" | tee -a "$OUT/rc-summary.txt"
fi

if [[ "$TARGET" == "all" || "$TARGET" == "regress" ]]; then
  # ⑤ 回归：A2 静态纹理（Metal）——动过 SkyTestPage.qml（WheelHandler），A2 逐像素必查
  STELQUICK_A2_CHECK=1 "$BIN" > "$OUT/regression-a2-metal.txt" 2>&1
  echo "regression-a2-metal rc=$?" | tee -a "$OUT/rc-summary.txt"

  # ⑥ 回归：DYN（真实引擎 + 替身对照，N 次）
  run_dyn
  run_dyn_stub

  # ⑦ 回归：S3 引擎集成（旧宿主 —— 本轮动了 src/core/StelApp.hpp ⇒ 硬要求）
  STELA3_CHECK=1 ./build-release/src/stellarium > "$OUT/regression-s3-stela3.txt" 2>&1
  echo "regression-s3-stela3 rc=$?" | tee -a "$OUT/rc-summary.txt"
fi

if [[ "$TARGET" == "dyn" ]]; then
  run_dyn
  run_dyn_stub
fi

echo "════════ 完成 ════════"
cat "$OUT/rc-summary.txt"
