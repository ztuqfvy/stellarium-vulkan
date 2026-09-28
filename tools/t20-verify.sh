#!/bin/zsh
# T20 验证脚本（2026-09-28）。判据 + 回归，证据落 docs/evidence/2026-09-28-t20-return-ring/
#
# 用法：tools/t20-verify.sh          （跑全部）
#       tools/t20-verify.sh core     （T20 两套自检 + T15..T19 的 core 回归）
#       tools/t20-verify.sh regress  （A2/DYN/S3/T17 回归）
#       tools/t20-verify.sh dyn 3    （只跑 DYN，跑 N 次；默认 3）
#
# 环境口径与 t17/t18/t19-verify.sh **完全一致**（同一后端 Metal：macOS 上 Vulkan 组合必丢
# 设备，见 T13 §7.27 的判定性实验）——**刻意不换口径**，否则跨任务读数不可比。
#
# ── T20 这一轮有两套自检 ────────────────────────────────────────────────────
#   ① `returnuicheck`（STELQUICK_RETURN_UI_CHECK=1）：**返回环本身**。11 条判据。
#      语义已定案（2026-09-27）= 切回天空视口页 + **状态全保留**（不是撤销）。
#      这条**只能**做 UI 层判据：整个实现就是 MainWindow.returnToSky() 里的一句 QML，
#      C++ 侧没有任何可断言的对象。观测面 = 真实控件 + 真实事件 + pageStack.currentIndex。
#      判据成对 + 一条判别性对照：
#        · 配对①「页面确实切回去了」   RT-04
#        · 配对②「状态纹丝不动」       RT-05/06（JD / 跟踪 / 选中）
#        · 前置「状态非平凡」           RT-03（没有它，配对②就是空转）
#        · 负控「切页往返不动世界」     RT-07
#        · **判别性对照「返回 ≠ 撤销」** RT-08 ← 唯一能区分"返回"与"恢复快照"的判据
#   ② `replaycheck`（STELQUICK_REPLAY_CHECK=1）：**I-REP-02 全流程回放**（A-alpha
#      出口测试）。9 条判据，全程只投递真实鼠标事件；末态四连断言（页面/时间/跟踪/星空）。
#
# ── DYN 判据为什么要跑 N 次 + 为什么同时跑「替身生产者」 ────────────────────
# 与 T18/T19 同一处置（完整定性见 `2026-09-24-t18-locate-track/dyn-ab-baseline-vs-t18.txt`）：
# `D1-C02 显示 / D1-C07 降级`量的是"QML 场景图有没有在渲染"，它要求**窗口被暴露**。
# 本机（M3 / macOS / Metal RHI）实测该量**受环境支配**：
#   · 低负载：真引擎与 QML 同进程时偶发停摆（≈33%），替身生产者必 PASS；
#   · 高负载（Spotlight mdbulkimport + 装包，load avg ≈ 5）：**替身也全败**；
#   · 会话锁屏 / 显示器休眠：**两侧都全程 0 帧**，且 `caffeinate -dims`/`-dimsu` **无效**。
# 处置：跑 N 次如实报 `N/3`、**不跑"直到绿"**；替身对照也失败 ⇒ 该读数是**「仪器
# 测不到」**，不作为退化证据；**但也不改写成 INVALID**（原始 FAIL 照实保留，只加定性）。
# **不得**把 `N/3` 里的任一次 PASS 当成"零退化已证明"。
set -u
cd /Users/ztuqfvy/qt_demo/stellarium_vulkan

export VK_DRIVER_FILES=/opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json
export QT_VULKAN_LIB=/opt/homebrew/opt/vulkan-loader/lib/libvulkan.1.dylib
export STELQUICK_GRAPHICS_API=metal

BIN=./build-release/src/ui/stelQuickUI.app/Contents/MacOS/stelQuickUI
OUT=docs/evidence/2026-09-28-t20-return-ring
TARGET=${1:-all}
DYN_RUNS=${2:-3}
mkdir -p "$OUT"

# rc-summary 每轮从头累积（否则重跑会与上一轮叠加，读串）。
# `dyn` 子命令只补跑 DYN，此时保留其它项读数，但要**先摘掉上一轮的 DYN 行**。
if [[ "$TARGET" == "dyn" ]]; then
  if [[ -f "$OUT/rc-summary.txt" ]]; then
    grep -vE "dyn-(engine|stub)-metal" "$OUT/rc-summary.txt" \
      > "$OUT/rc-summary.txt.tmp" 2>/dev/null || : > "$OUT/rc-summary.txt.tmp"
    mv "$OUT/rc-summary.txt.tmp" "$OUT/rc-summary.txt"
  fi
else
  : > "$OUT/rc-summary.txt"
fi

# 环境注记：帧率类读数会被机器负载/显示器状态污染（T13 起既有纪律）。
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
    echo "替身路径不含返回环/回放代码（DYN 用 startPage=\"sky\" 且不点任何按钮）"
    echo "⇒ 它失败即与本次改动无关。"
  } >> "$OUT/regression-dyn-stub-metal.txt"
  cat "$OUT/regression-dyn-stub-metal.txt"
  echo "regression-dyn-stub-metal $pass/$n PASS（判别性对照：替身不含 T20 代码）" \
    | tee -a "$OUT/rc-summary.txt"
}

if [[ "$TARGET" == "all" || "$TARGET" == "core" ]]; then
  # ① T20 返回环 UI 层端到端（11 条：配对判据 + 前置非平凡 + 两条负控 + 判别性对照）
  STELQUICK_RETURN_UI_CHECK=1 "$BIN" > "$OUT/returnuicheck-mac.txt" 2>&1
  echo "returnuicheck-mac rc=$?" | tee -a "$OUT/rc-summary.txt"

  # ② T20 I-REP-02 全流程回放（9 条：开机→搜月球→定位→改时间→返回，全程真实鼠标事件）
  STELQUICK_REPLAY_CHECK=1 "$BIN" > "$OUT/replaycheck-mac.txt" 2>&1
  echo "replaycheck-mac rc=$?" | tee -a "$OUT/rc-summary.txt"

  # ③ T19 时间环的两套（T20 动了 MainWindow 的页头/keySink 与 SearchPage 的锚点，
  #    时间页本身没动，但 Esc 拦截面在它上游 ⇒ 必须复查）
  STELQUICK_TIME_CHECK=1 "$BIN" > "$OUT/regression-timecheck.txt" 2>&1
  echo "regression-timecheck rc=$?" | tee -a "$OUT/rc-summary.txt"
  STELQUICK_TIME_UI_CHECK=1 "$BIN" > "$OUT/regression-timeuicheck.txt" 2>&1
  echo "regression-timeuicheck rc=$?" | tee -a "$OUT/rc-summary.txt"

  # ④ T18 搜索/定位页的两套（SearchPage 加了三个 objectName、locateButton 未动）
  STELQUICK_LOCATE_CHECK=1 "$BIN" > "$OUT/regression-locatecheck.txt" 2>&1
  echo "regression-locatecheck rc=$?" | tee -a "$OUT/rc-summary.txt"
  STELQUICK_UI_CHECK=1 "$BIN" > "$OUT/regression-locate-uicheck.txt" 2>&1
  echo "regression-locate-uicheck rc=$?" | tee -a "$OUT/rc-summary.txt"

  # ⑤ T16 时钟纯逻辑（返回环断言 JD 不动，建立在它之上）
  STELQUICK_CLOCK_CHECK=1 "$BIN" > "$OUT/regression-clockcheck.txt" 2>&1
  echo "regression-clockcheck rc=$?" | tee -a "$OUT/rc-summary.txt"

  # ⑥ T15/T16 命令通路 + 集成（Esc 拦截写在 keySink 里，AC-12 与它同址 ⇒ 必须复查）
  STELQUICK_ACTION_CHECK=1 "$BIN" > "$OUT/regression-actioncheck.txt" 2>&1
  echo "regression-actioncheck rc=$?" | tee -a "$OUT/rc-summary.txt"
fi

if [[ "$TARGET" == "all" || "$TARGET" == "regress" ]]; then
  # ⑦ T17 搜索/选择模型
  STELQUICK_SEARCH_CHECK=1 "$BIN" > "$OUT/regression-searchcheck.txt" 2>&1
  echo "regression-searchcheck rc=$?" | tee -a "$OUT/rc-summary.txt"

  # ⑧ 回归：A2 静态纹理（Metal）
  STELQUICK_A2_CHECK=1 "$BIN" > "$OUT/regression-a2-metal.txt" 2>&1
  echo "regression-a2-metal rc=$?" | tee -a "$OUT/rc-summary.txt"

  # ⑨ 回归：DYN 动态帧（真实引擎生产者 + Metal）—— 间歇量，跑 N 次 + 替身判别性对照
  run_dyn
  run_dyn_stub

  # ⑩ 回归：S3 引擎集成（旧宿主 stellarium —— EngineWallClock 路径零退化）
  STELA3_CHECK=1 ./build-release/src/stellarium > "$OUT/regression-s3-stela3.txt" 2>&1
  echo "regression-s3-stela3 rc=$?" | tee -a "$OUT/rc-summary.txt"
fi

if [[ "$TARGET" == "dyn" ]]; then
  run_dyn
  run_dyn_stub
fi

echo "════════ 完成 ════════"
cat "$OUT/rc-summary.txt"
