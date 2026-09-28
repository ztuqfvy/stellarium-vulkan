#!/bin/zsh
# T26 验证脚本（2026-09-28）。判据 + 回归，证据落 docs/evidence/2026-09-28-t26-recall/
#
# 用法：tools/t26-verify.sh          （跑全部）
#       tools/t26-verify.sh core 5   （T26 正题，跑 N 次；默认 5）
#       tools/t26-verify.sh regress  （A2/DYN/S3 + 相邻回归）
#       tools/t26-verify.sh dyn 3    （只跑 DYN，跑 N 次；默认 3）
#
# 环境口径与 t17..t25-verify.sh **完全一致**（同一后端 Metal）——**刻意不换口径**。
#
# ── T26 的正题：召回问题的引擎侧解法（A4 加固项）──────────────────────────
# T21/T24 时代搜索模型只能重排"引擎已返回的候选"；引擎候选集截断发生在
# 枚举序里（StelObjectModule::listMatchingObjects 每模块 maxNbItem 先到先得），
# 高相关度候选可能压根没进池——precision@k 闭环了，recall 没有。
#
# T26 修法（语义零复刻）：引擎新增**加性无截断原语**
#   StelObjectModule::listAllMatchingObjects   （默认实现 = 以 INT_MAX 预算调用
#     本模块 listMatchingObjects，各模块自定义匹配逻辑全部复用，只去预算）
#   StelObjectMgr::listAllMatchingObjects      （同构聚合，无每模块预算）
# 模型 collect() 改调新原语，截断移到**排序之后**——T21 留下的边界闭环。
# 旧 SearchDialog 走的原语一字未动（S3 回归硬要求：动了 src/core）。
#
# 判据（SEARCHCHECK 51 → 54，新增 SRC-12..14，活引擎腿）：
#   SRC-12 判别对照：无截断原语全量 > 旧每模块cap=3 调用（截断真的丢过候选）
#          且词首候选在池内；查询 "al"（星名 Al* 恒多，不依赖 SolarSystem）。
#   SRC-13 模型接线：cap=3 → 3 行 且 lastRawMatchCount == 引擎全量数
#          （负控靶心：模型回退旧调用 → raw 掉回截断数 → 红）。
#   SRC-14 端到端：全量池上排序层把词首/完全匹配顶到首行（recall 收益落 precision@1）。
#
# ── DYN 判据为什么要跑 N 次 + 替身对照 ──────────────────────────────────────
# 与 T18..T25 同一处置（完整定性见 2026-09-24-t18-locate-track/dyn-ab-baseline-vs-t18.txt）。
set -u
cd /Users/ztuqfvy/qt_demo/stellarium_vulkan

export VK_DRIVER_FILES=/opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json
export QT_VULKAN_LIB=/opt/homebrew/opt/vulkan-loader/lib/libvulkan.1.dylib
export STELQUICK_GRAPHICS_API=metal

BIN=./build-release/src/ui/stelQuickUI.app/Contents/MacOS/stelQuickUI
OUT=docs/evidence/2026-09-28-t26-recall
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
    echo "替身路径不含 T26 搜索候选池代码（DYN 用 startPage=\"sky\" 且不搜索）"
    echo "⇒ 它失败即与本次改动无关。"
  } >> "$OUT/regression-dyn-stub-metal.txt"
  cat "$OUT/regression-dyn-stub-metal.txt"
  echo "regression-dyn-stub-metal $pass/$n PASS（判别性对照：替身不含 T26 代码）" \
    | tee -a "$OUT/rc-summary.txt"
}

if [[ "$TARGET" == "all" || "$TARGET" == "core" ]]; then
  # ① T26 正题：SEARCHCHECK × N（判据 54 条：SRC-01..14 + PINY-01..08 + 纵向 V/T17）
  integer pass=0 i rc
  pass=0
  : > "$OUT/searchcheck-mac-n$CORE_RUNS.txt"
  for i in $(seq 1 $CORE_RUNS); do
    STELQUICK_SEARCH_CHECK=1 "$BIN" > "$OUT/searchcheck-mac-run$i.txt" 2>&1
    rc=$?
    echo "──────── SEARCHCHECK run $i/$CORE_RUNS（rc=$rc）────────" >> "$OUT/searchcheck-mac-n$CORE_RUNS.txt"
    grep -E "SEARCHCHECK:" "$OUT/searchcheck-mac-run$i.txt" \
      >> "$OUT/searchcheck-mac-n$CORE_RUNS.txt"
    [[ $rc -eq 0 ]] && (( pass++ ))
  done
  echo "searchcheck-mac $pass/$CORE_RUNS PASS" | tee -a "$OUT/rc-summary.txt"

  # ② 相邻回归：T25 交互级（搜索框守卫 belongs ActionRouter，相邻最紧）
  STELQUICK_INTERACT_UI_CHECK=1 "$BIN" > "$OUT/regression-interactcheck.txt" 2>&1
  echo "regression-interactcheck rc=$?" | tee -a "$OUT/rc-summary.txt"

  # ③ 相邻回归：T18 定位两套
  STELQUICK_ACTION_CHECK=1 "$BIN" > "$OUT/regression-actioncheck.txt" 2>&1
  echo "regression-actioncheck rc=$?" | tee -a "$OUT/rc-summary.txt"
  STELQUICK_LOCATE_CHECK=1 "$BIN" > "$OUT/regression-locatecheck.txt" 2>&1
  echo "regression-locatecheck rc=$?" | tee -a "$OUT/rc-summary.txt"
  STELQUICK_UI_CHECK=1 "$BIN" > "$OUT/regression-locate-uicheck.txt" 2>&1
  echo "regression-locate-uicheck rc=$?" | tee -a "$OUT/rc-summary.txt"

  # ④ 相邻回归：T19/T20/T22
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
  # ⑥ 回归：A2 静态纹理（Metal）——QML 本轮未动，但 A2 便宜，逐像素护栏照跑
  STELQUICK_A2_CHECK=1 "$BIN" > "$OUT/regression-a2-metal.txt" 2>&1
  echo "regression-a2-metal rc=$?" | tee -a "$OUT/rc-summary.txt"

  # ⑦ 回归：DYN（真实引擎 + 替身对照，N 次）
  run_dyn
  run_dyn_stub

  # ⑧ 回归：S3 引擎集成（旧宿主 —— 本轮动了 src/core/StelObjectModule/StelObjectMgr ⇒ 硬要求；
  #    旧 SearchDialog 走的原语一字未动，S3 必须证明这一点）
  STELA3_CHECK=1 ./build-release/src/stellarium > "$OUT/regression-s3-stela3.txt" 2>&1
  echo "regression-s3-stela3 rc=$?" | tee -a "$OUT/rc-summary.txt"
fi

if [[ "$TARGET" == "dyn" ]]; then
  run_dyn
  run_dyn_stub
fi

echo "════════ 完成 ════════"
cat "$OUT/rc-summary.txt"
