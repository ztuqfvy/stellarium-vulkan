#!/bin/zsh
# T28 验证脚本（2026-09-29）。判据 + 回归，证据落 docs/evidence/2026-09-29-t28-pinch/
#
# 用法：tools/t28-verify.sh          （跑全部）
#       tools/t28-verify.sh core 5   （T28 正题 INTERACTCHECK，跑 N 次；默认 5）
#       tools/t28-verify.sh regress  （A2/DYN + 相邻回归）
#       tools/t28-verify.sh dyn 3    （只跑 DYN，跑 N 次；默认 3）
#
# 环境口径与 t17..t27-verify.sh **完全一致**（同一后端 Metal）——**刻意不换口径**。
#
# ── T28 的正题：天空页触控板捏合 → 引擎缩放端到端 ─────────────────────────────
# 与 T25 滚轮、T27 鼠标同族：旧宿主链路 StelMainView::grabGesture(Qt::PinchGesture)
# → gestureEvent → pinchTriggered → StelApp::handlePinch(scaleFactor, true)，合流形态
# QML 从未接过（天空页无任何 PinchHandler；这是"引擎输入面"剩下的第三块）。
# 修法（语义零复刻）：SkyTestPage 挂 PinchHandler（target: null）→
# AppFacade::pinchZoom(scaleChanged 的乘法倍率) → StelApp::handlePinch。
#
# Qt 源码实证的真链（不是猜的）：
#   macOS beginGestureWithEvent / magnifyWithEvent / endGestureWithEvent
#   → QNativeGestureEvent(Begin/Zoom/End) —— Zoom 的 value 是**增量分数**
#   → QQuickDeliveryAgent::event() 的 `case QEvent::NativeGesture`
#     （deliverSinglePointEventUntilAccepted，**单点**投递）
#   → QQuickPinchHandler（QQuickMultiPointHandler 显式放行 NativeGesture，
#     见 qquickmultipointhandler.cpp:49；PinchHandler 的 native 分支见
#     qquickpinchhandler.cpp:341-364 与 509-535）
#   → setActiveScale(activeValue * (1 + value)) → scaleChanged(delta=乘法倍率)
#   → QML onScaleChanged → AppFacade::pinchZoom → StelMovementMgr::handlePinch
#   → zoomTo(previousFov / scale, 0)。
#
# 判据（INTERACTCHECK 9 → 12，新增 IT-10..12）：
#   IT-10 捏开（两指张开，倍率 1.25 ×3）→ FOV 严格变小（成对：before>0 且变小）
#   IT-11 方向对照：反向捏拢（0.8 ×3）→ FOV **回升且回到原值**（2% 内）。
#         两根腿缺一不可：只写"回升"会让"每次捏合都乘固定倍率"的实现蒙混过关
#         （1.25³ 与 0.8³ 互逆 ⇒ 理论精确还原，实测偏差 0.00000）。
#   IT-12 负控：时间页捏合 → FOV 不动（页守卫；PinchHandler 只挂天空页）。
#         ⚠️ 本判据**只在 IT-10/11 绿的前提下**才有判别力 —— 整链全死时它也绿
#         （孤立断言可假绿，血泪第 4 条）。其判别性由负控③（把 handler 挪到窗口级）
#         实证：负控③下 IT-12 必须红。
#
# 仪器注记（**受理位不可作判据**）：QML PointerHandler 走独占 grab，
# **不调用 QEvent::accept()** ⇒ 注入函数的"受理=0"对滚轮/鼠标/捏合全都出现，
# 而它们其实都生效了（IT-02 与 IT-10 同款）。判据一律读**引擎可观测的 FOV**，
# 不读受理位。首版注入只发裸 Zoom 且把 value 传成倍率 ⇒ FOV 60→60 不动，
# 说明这条链的判据对"事件形状"是敏感的（不是恒绿）。
set -u
cd /Users/ztuqfvy/qt_demo/stellarium_vulkan

export VK_DRIVER_FILES=/opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json
export QT_VULKAN_LIB=/opt/homebrew/opt/vulkan-loader/lib/libvulkan.1.dylib
export STELQUICK_GRAPHICS_API=metal

BIN=./build-release/src/ui/stelQuickUI.app/Contents/MacOS/stelQuickUI
OUT=docs/evidence/2026-09-29-t28-pinch
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
    echo "环境注记：Spotlight 批量索引正在运行（mdbulkimport）——点击注入可能被干扰"
  fi
  echo '读数采信规则（IT-06 焦点腿）：窗口未获得系统焦点时 focusObject()==nullptr，'
  echo '  守卫前提不成立（本判据的按键是 sendEvent 直达窗口，绕过系统焦点）⇒ 判据'
  echo '  明确报「仪器不可用」并 rc=10，**不洗成 PASS、也不改写成 INVALID**。'
  echo '  同一次运行内 IT-01..05/IT-07..12 的读数不受影响（它们不依赖系统焦点）。'
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
    echo "替身路径不含 T28 捏合转发代码（DYN 走 startPage=\"sky\" 但从不注入手势）"
    echo "⇒ 它失败即与本次改动无关。"
  } >> "$OUT/regression-dyn-stub-metal.txt"
  cat "$OUT/regression-dyn-stub-metal.txt"
  echo "regression-dyn-stub-metal $pass/$n PASS（判别性对照：替身不含 T28 代码）" \
    | tee -a "$OUT/rc-summary.txt"
}

if [[ "$TARGET" == "all" || "$TARGET" == "core" ]]; then
  # ① T28 正题：INTERACTCHECK × N（判据 12 条：IT-01..12）
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

  # ② 相邻回归：T26 搜索（捏合与搜索无耦合，护栏照跑）
  STELQUICK_SEARCH_CHECK=1 "$BIN" > "$OUT/regression-searchcheck.txt" 2>&1
  echo "regression-searchcheck rc=$?" | tee -a "$OUT/rc-summary.txt"

  # ③ 相邻回归：T18 定位两套 + T15 命令面
  STELQUICK_ACTION_CHECK=1 "$BIN" > "$OUT/regression-actioncheck.txt" 2>&1
  echo "regression-actioncheck rc=$?" | tee -a "$OUT/rc-summary.txt"
  STELQUICK_LOCATE_CHECK=1 "$BIN" > "$OUT/regression-locatecheck.txt" 2>&1
  echo "regression-locatecheck rc=$?" | tee -a "$OUT/rc-summary.txt"
  STELQUICK_UI_CHECK=1 "$BIN" > "$OUT/regression-locate-uicheck.txt" 2>&1
  echo "regression-locate-uicheck rc=$?" | tee -a "$OUT/rc-summary.txt"

  # ④ 相邻回归：T19/T20/T22（捏合改的正是"缩放/视场"这条引擎面，时间页共用视觉层）
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
  # ⑥ 回归：A2 静态纹理（Metal）——本轮动了 SkyTestPage.qml（新增 PinchHandler），
  #    QML 页内新增项必须证明没有污染逐像素探针 ⇒ 硬要求
  STELQUICK_A2_CHECK=1 "$BIN" > "$OUT/regression-a2-metal.txt" 2>&1
  echo "regression-a2-metal rc=$?" | tee -a "$OUT/rc-summary.txt"

  # ⑦ 回归：DYN（真实引擎 + 替身对照，N 次）
  run_dyn
  run_dyn_stub

  # ⑧ 回归：S3 引擎集成（旧宿主）——本轮**未动 src/core**，但捏合链的终点
  #    StelApp::handlePinch → StelMovementMgr::handlePinch 与旧宿主共用，
  #    旧宿主行为必须零变化 ⇒ 照跑
  STELA3_CHECK=1 ./build-release/src/stellarium > "$OUT/regression-s3-stela3.txt" 2>&1
  echo "regression-s3-stela3 rc=$?" | tee -a "$OUT/rc-summary.txt"
fi

if [[ "$TARGET" == "dyn" ]]; then
  run_dyn
  run_dyn_stub
fi

echo "════════ 完成 ════════"
cat "$OUT/rc-summary.txt"
