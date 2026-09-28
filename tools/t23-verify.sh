#!/bin/zsh
# T23 验证脚本（2026-09-28）。判据 + 回归，证据落 docs/evidence/2026-09-28-t23-tracking-leak/
#
# 用法：tools/t23-verify.sh          （跑全部）
#       tools/t23-verify.sh core     （T23 正题 + 回归 core 组）
#       tools/t23-verify.sh regress  （A2/DYN/S3 + 搜索模型回归）
#       tools/t23-verify.sh dyn 3    （只跑 DYN，跑 N 次；默认 3）
#
# 环境口径与 t17..t22-verify.sh **完全一致**（同一后端 Metal）——**刻意不换口径**。
#
# ── T23 的正题：跟踪标志泄漏根治（引擎侧）────────────────────────────────────
# 缺陷（上游 vintage 22c8f8ed 原样带入）：
#   `StelMovementMgr::selectedObjectChange()` 整段包在 `getWasSelected()` 里，
#   而 `StelObjectMgr::unSelect()` 是**先** clear 掉 lastSelectedObjects **再**
#   emit（StelObjectMgr.cpp:537-544）⇒ 取消选中时槽里看到"没有选中"，
#   `setFlagTracking(false)` **永不执行** ⇒ flagTracking 泄漏为 true。
#   T18 只能用"合取真值"（引擎标志 ∧ 确有选中）在 UI 层掩盖。
# 修复：
#   ① 引擎：槽签名里的 `action` 参数（从未被读过！）用于 RemoveFromSelection
#      分支 —— 取消选中 ⇒ 无物可跟踪，直接 setFlagTracking(false)。
#   ② AppFacade：`isTracking()` 合取退役（根治后 flagTracking==true 蕴含有选中）；
#      新增 `ensureTrackingForwarding()`（懒连接引擎 flagTrackingChanged →
#      trackingChanged）—— 引擎侧状态变化（unSelect/换选/旧键位）现在能推到 QML。
#      后者是 T23 新相位实测抓到的第二个缺口：清除选中后文案残留"正在跟踪：×"。
# 判据（成对 + 对照 + 活引擎腿）：
#   LOC-08b 加严（合取 ∧ **引擎原始标志**都必须归零）——修复前该判据红（负控实证）
#   LOC-09 换选对照腿 —— 修复前也绿（换选路径本来就对），证明修复没改坏旧路径
#   UI-09/UI-10 真实点击「清除选中」（unSelect 的 UI 路径）—— 修复前 UI-10 红
#
# ⚠️ 本轮**动了 src/core/**（StelMovementMgr.cpp）⇒ 旧宿主 stellarium 回归（S3）
#    是硬要求，不是可选项：引擎行为变化必须对两种宿主同时零退化。
#
# ── DYN 判据跑 N 次 + 替身对照的理由 ────────────────────────────────────────
# 与 T18..T22 同一处置（完整定性见 `2026-09-24-t18-locate-track/dyn-ab-baseline-vs-t18.txt`
# 与 `2026-09-28-t22-time-page/flaky-layout-207x0/`）：显示/降级量环境敏感，
# 替身也败 ⇒ "仪器测不到"，不作为退化证据，也不改写 INVALID。
set -u
cd /Users/ztuqfvy/qt_demo/stellarium_vulkan

export VK_DRIVER_FILES=/opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json
export QT_VULKAN_LIB=/opt/homebrew/opt/vulkan-loader/lib/libvulkan.1.dylib
export STELQUICK_GRAPHICS_API=metal

BIN=./build-release/src/ui/stelQuickUI.app/Contents/MacOS/stelQuickUI
OUT=docs/evidence/2026-09-28-t23-tracking-leak
TARGET=${1:-all}
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
  echo '  若同条件的**替身对照**也失败（尤其"全程推进 0 帧"），则该读数反映的是'
  echo '  「仪器测不到」而不是「被测程序失败」，**不作为退化证据**；'
  echo '  同时也不改写成 INVALID（原始读数照实保留，只加这条定性）。'
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
  } >> "$OUT/regression-dyn-engine-metal.txt"
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
    echo "替身路径不含 T23 改动（不选中天体、不触发 unSelect 路径）⇒ 它失败即与本次改动无关。"
  } >> "$OUT/regression-dyn-stub-metal.txt"
  echo "regression-dyn-stub-metal $pass/$n PASS（判别性对照：替身不含 T23 代码）" \
    | tee -a "$OUT/rc-summary.txt"
}

if [[ "$TARGET" == "all" || "$TARGET" == "core" ]]; then
  # ① T23 正题·引擎/命令层（15 条：LOC-08b 加严 + LOC-09 新增）
  STELQUICK_LOCATE_CHECK=1 "$BIN" > "$OUT/locatecheck-mac.txt" 2>&1
  echo "locatecheck-mac rc=$?" | tee -a "$OUT/rc-summary.txt"

  # ② T23 正题·UI 层端到端（10 条：UI-09/UI-10 新增，走真实「清除选中」点击）
  STELQUICK_UI_CHECK=1 "$BIN" > "$OUT/uicheck-mac.txt" 2>&1
  echo "uicheck-mac rc=$?" | tee -a "$OUT/rc-summary.txt"

  # ③ 回归：T22 时间页两套（AppFacade 是同一文件）
  STELQUICK_TIME_CHECK=1 "$BIN" > "$OUT/regression-timecheck.txt" 2>&1
  echo "regression-timecheck rc=$?" | tee -a "$OUT/rc-summary.txt"
  STELQUICK_TIME_UI_CHECK=1 "$BIN" > "$OUT/regression-timeuicheck.txt" 2>&1
  echo "regression-timeuicheck rc=$?" | tee -a "$OUT/rc-summary.txt"

  # ④ 回归：T21 搜索排序（活引擎腿 SRC-11 对"首行即月球"的断言依赖选中/跟踪语义）
  STELQUICK_SEARCH_CHECK=1 "$BIN" > "$OUT/regression-searchcheck.txt" 2>&1
  echo "regression-searchcheck rc=$?" | tee -a "$OUT/rc-summary.txt"

  # ⑤ 回归：T20 返回环 + I-REP-02 全流程回放（回放里有"定位并跟踪"环节）
  STELQUICK_RETURN_UI_CHECK=1 "$BIN" > "$OUT/regression-returnuicheck.txt" 2>&1
  echo "regression-returnuicheck rc=$?" | tee -a "$OUT/rc-summary.txt"
  STELQUICK_REPLAY_CHECK=1 "$BIN" > "$OUT/regression-replaycheck.txt" 2>&1
  echo "regression-replaycheck rc=$?" | tee -a "$OUT/rc-summary.txt"

  # ⑥ 回归：T16 时钟纯逻辑 + T15/T16 命令通路
  STELQUICK_CLOCK_CHECK=1 "$BIN" > "$OUT/regression-clockcheck.txt" 2>&1
  echo "regression-clockcheck rc=$?" | tee -a "$OUT/rc-summary.txt"
  STELQUICK_ACTION_CHECK=1 "$BIN" > "$OUT/regression-actioncheck.txt" 2>&1
  echo "regression-actioncheck rc=$?" | tee -a "$OUT/rc-summary.txt"
fi

if [[ "$TARGET" == "all" || "$TARGET" == "regress" ]]; then
  # ⑦ 回归：A2 静态纹理（Metal）
  STELQUICK_A2_CHECK=1 "$BIN" > "$OUT/regression-a2-metal.txt" 2>&1
  echo "regression-a2-metal rc=$?" | tee -a "$OUT/rc-summary.txt"

  # ⑧ 回归：DYN（真实引擎 + 替身判别性对照）
  run_dyn
  run_dyn_stub

  # ⑨ 回归：S3 引擎集成（**旧宿主 stellarium**）—— T23 动了 src/core/，
  #    引擎行为变化必须对两种宿主同时零退化（本组是硬要求）。
  STELA3_CHECK=1 ./build-release/src/stellarium > "$OUT/regression-s3-stela3.txt" 2>&1
  echo "regression-s3-stela3 rc=$?" | tee -a "$OUT/rc-summary.txt"
fi

if [[ "$TARGET" == "dyn" ]]; then
  run_dyn
  run_dyn_stub
fi

echo "════════ 完成 ════════"
cat "$OUT/rc-summary.txt"
