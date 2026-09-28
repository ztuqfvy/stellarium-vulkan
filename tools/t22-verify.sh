#!/bin/zsh
# T22 验证脚本（2026-09-28）。判据 + 回归，证据落 docs/evidence/2026-09-28-t22-time-page/
#
# 用法：tools/t22-verify.sh          （跑全部）
#       tools/t22-verify.sh core     （T22 时间页收尾 + T15..T21 的 core 回归）
#       tools/t22-verify.sh regress  （A2/DYN/S3 + 搜索模型回归）
#       tools/t22-verify.sh dyn 3    （只跑 DYN，跑 N 次；默认 3）
#
# 环境口径与 t17/t18/t19/t20-verify.sh **完全一致**（同一后端 Metal：macOS 上 Vulkan 组合必丢
# 设备，见 T13 §7.27 的判定性实验）——**刻意不换口径**，否则跨任务读数不可比。
#
# ── T22 的正题：时间页收尾四项（显示历法 / MJD / 速率 GUI / 时区选择器）──────────
# 计划文档里挂着的最后一段 A4 缺口。每条判据都对准一个**具体的错法**：
#
# ① 速率：**仪表必须接在实况上**（TC-19 纯逻辑腿 + UI-15 活引擎腿，都是**成对**判据）
#    背景事实：改前 `AppFacade::timeRate()` 读的是本地缓存 `m_timeRate`，只在
#    `setTimeRate()` 里更新；而引擎的六个速率动作（`actionIncrease_Time_Speed` 等）
#    走 `StelCore::increaseTimeSpeed()`，**完全不经过 AppFacade** ⇒ 用户按 L 键、
#    引擎真的加速了，UI 读数纹丝不动。这就是本仓库反复复发的
#    "仪器没接在实况上"（T14/T15 血泪第 3 条）。
#    成对的写法：`engineMoved`（引擎值确实变了）∧ `meterFollows`（读数跟着变）。
#    只测后者，"读数恒为 0"也能绿；只测前者，就漏掉原缺陷。
#    UI-15 更进一步：**真实点击**「+」按钮（不是直接调函数）。
#
# ② 时区：**静默失败**是这里独有的坑，判据要盯住它
#    · `StelCore::setCurrentTimeZone()` 只接受 `getAllTimezoneNames()` 里的名字，
#      名单外的**只打一条 qWarning 就不设置**（StelCore.cpp:1717-1725）；
#    · 名字若不能被 `QTimeZone` 解析，`getUTCOffset` 会**悄悄落回系统本地时区**
#      （StelCore.cpp:1637）。
#    所以 ⓐ `availableTimeZoneIds()` 取**引擎名单 ∩ Qt 可解析**的交集；
#    ⓑ `setTimeZoneId()` **回读验证**才算成功。判据侧：TC-17 非法 id 必须被拒，
#    TC-18 用"偏移 0"与"偏移 +8h"两个**无夏令时**时区做**成对**断言（与跑在哪天无关）。
#    ⚠️ 还有一条引擎语义会让时区判据假红：`getUTCOffset` 里的
#    `JD >= TZ_ERA_BEGINNING`（1847-12-01）——不成立时它**完全不看时区名**而按经度
#    算地方平太阳时。所以 TC-16 把时钟挪到 1582 年测完历法后**必须挪回现代**。
#
# ③ MJD：与 JD **同源**。⚠️ 不能用 `StelCore::getMJDay()` —— 它读 `JD.first`
#    （StelCore.cpp:1280），那是"上一帧快照"；T19 已因同一理由把 JD 读侧改成
#    `getSimClockJD()`。写侧走 `setJulianDay(mjd + 2400000.5)`，复用同一条写入路径。
#
# ④ 显示历法：照抄旧界面判定 `jd < 2299161`（DateTimeDialog.cpp:251、StelUtils.cpp:848
#    的 JD_GREG_CAL）。判据取**边界两侧成对**：2299160.5 → julian、2299161.0 → gregorian。
#
# 速率文本换算照抄 `StelGuiItems.cpp:885-907` 的四档单位跳档（min/s→hr/s→d/s→yr/s），
# 判据用三组**可手算**的输入（1×/3600×/86400× JD_SECOND）。
#
# ⚠️ 这一轮**没有**动 `src/core/`（全是 src/app + src/ui）⇒ 引擎库不用全量重编。
#
# ── T21 的正题（保留在回归里）：搜索结果相关度排序 ─────────────────────────────
# SRC-06..SRC-09 是纯逻辑腿（分级/次序/稳定性/内建判别性对照），SRC-11 是活引擎腿
# （搜 "Moon" 首行必须是 Planet:Moon）。两条腿互补：纯逻辑证不了"有人调用"，
# 活引擎证不了"规则细节"。判据构造的完整说明见 docs/T21_SEARCH_RANKING.zh_CN.md。
#
# ── DYN 判据为什么要跑 N 次 + 为什么同时跑「替身生产者」 ────────────────────
# 与 T18/T19/T20 同一处置（完整定性见 `2026-09-24-t18-locate-track/dyn-ab-baseline-vs-t18.txt`）：
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
OUT=docs/evidence/2026-09-28-t22-time-page
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
    echo "替身路径不含 T22 时间页新增代码（DYN 用 startPage=\"sky\" 且不点任何按钮）"
    echo "⇒ 它失败即与本次改动无关。"
  } >> "$OUT/regression-dyn-stub-metal.txt"
  cat "$OUT/regression-dyn-stub-metal.txt"
  echo "regression-dyn-stub-metal $pass/$n PASS（判别性对照：替身不含 T22 代码）" \
    | tee -a "$OUT/rc-summary.txt"
}

if [[ "$TARGET" == "all" || "$TARGET" == "core" ]]; then
  # ① T22 正题·纯逻辑+引擎一致：MJD 同源 / 历法边界成对 / 时区成对 / 速率成对
  #    （TIMECHECK 内的 TC-15..TC-21）
  STELQUICK_TIME_CHECK=1 "$BIN" > "$OUT/timecheck-mac.txt" 2>&1
  echo "timecheck-mac rc=$?" | tee -a "$OUT/rc-summary.txt"

  # ② T22 正题·UI 层端到端（19 条：UI-12..UI-16 是本轮新增）
  #    真实点击速率「+」按钮 → 断言"引擎变了 ∧ 仪表跟着变"；真实读写时区组合框。
  #    ⚠️ 它同时回归 T19 的 UI-01..UI-11（时间页是同一个文件）。
  STELQUICK_TIME_UI_CHECK=1 "$BIN" > "$OUT/timeuicheck-mac.txt" 2>&1
  echo "timeuicheck-mac rc=$?" | tee -a "$OUT/rc-summary.txt"

  # ③ T21 搜索相关度排序（回归：本轮没动 SearchRanker，但 SearchPage/MainWindow 相邻）
  STELQUICK_SEARCH_CHECK=1 "$BIN" > "$OUT/regression-searchcheck-core.txt" 2>&1
  echo "regression-searchcheck-core rc=$?" | tee -a "$OUT/rc-summary.txt"

  # ② T20 返回环 UI 层端到端（11 条：配对判据 + 前置非平凡 + 两条负控 + 判别性对照）
  #    T21 动了 SearchResultsModel.collect()（排序）⇒ 回归它，同样不能只看"能跑"。
  STELQUICK_RETURN_UI_CHECK=1 "$BIN" > "$OUT/returnuicheck-mac.txt" 2>&1
  echo "returnuicheck-mac rc=$?" | tee -a "$OUT/rc-summary.txt"

  # ③ T20 I-REP-02 全流程回放（11 条：开机→搜月球→定位→改时间→返回，全程真实鼠标事件）
  #    ⚠️ T21 起 RP-04a 是**断言**（首行必须就是月球），不再是"按名字找月球那一行"。
  #    这条正是排序生效的端到端见证：把 SearchRanker 绕过，它立刻红。
  STELQUICK_REPLAY_CHECK=1 "$BIN" > "$OUT/replaycheck-mac.txt" 2>&1
  echo "replaycheck-mac rc=$?" | tee -a "$OUT/rc-summary.txt"

  # ④ T18 搜索/定位页的两套（SearchPage 未动 ⇒ 只作护栏）
  STELQUICK_LOCATE_CHECK=1 "$BIN" > "$OUT/regression-locatecheck.txt" 2>&1
  echo "regression-locatecheck rc=$?" | tee -a "$OUT/rc-summary.txt"
  STELQUICK_UI_CHECK=1 "$BIN" > "$OUT/regression-locate-uicheck.txt" 2>&1
  echo "regression-locate-uicheck rc=$?" | tee -a "$OUT/rc-summary.txt"

  # ⑥ T16 时钟纯逻辑
  STELQUICK_CLOCK_CHECK=1 "$BIN" > "$OUT/regression-clockcheck.txt" 2>&1
  echo "regression-clockcheck rc=$?" | tee -a "$OUT/rc-summary.txt"

  # ⑦ T15/T16 命令通路 + 集成
  STELQUICK_ACTION_CHECK=1 "$BIN" > "$OUT/regression-actioncheck.txt" 2>&1
  echo "regression-actioncheck rc=$?" | tee -a "$OUT/rc-summary.txt"
fi

if [[ "$TARGET" == "all" || "$TARGET" == "regress" ]]; then
  # ⑧ 回归：搜索/选择模型（T21 只改了 collect() 的排序与去重保留策略，
  #    其余语义必须零退化 —— SRC-01/02/03/04/05 就是这一层的护栏）
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
