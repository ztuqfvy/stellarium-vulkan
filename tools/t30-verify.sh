#!/bin/zsh
# T30 验证脚本（2026-09-29）：**时间页视觉层判据组**（TIMEUICHECK 的 UI-17..UI-24）。
#
# 用法：tools/t30-verify.sh              （跑全部）
#       tools/t30-verify.sh core 5       （正题 ×5 + 负控 ×3 + 相邻回归）
#       tools/t30-verify.sh negctl 3     （只跑负控）
#       tools/t30-verify.sh regress      （只跑 A2 逐像素 + DYN 双跑）
#
# 证据落 docs/evidence/2026-09-29-t30-visual/mac/。
# 环境口径（Metal + MoltenVK）**刻意与 t17..t29 / wt29 完全一致，不换**。
#
# ── 本轮改动面 ────────────────────────────────────────────────────────────────
# `src/ui/main.cpp` 的**仪器**：TIMEUICHECK 追加 8 条**视觉层**判据
# （UI-17..UI-24），并新增负控开关 `STELQUICK_TIME_VISUAL_NEGCTL`。
# **产品代码零改动**（页面/面板一律沿父链取，不新增 QML 锚点）；
# **既有 19 条判据的口径一条没改**（门槛、锚点、相位语义全部原样）。
#
# 所以本脚本的期望与 t29-verify.sh 的 timeuicheck 那一行**只差判据总数**：
#   旧 19/19 → 新 27/27。若既有判据出现任何偏离，那是仪器改坏了，不是"环境差异"。
#
# ── 这一组判据是干什么的（为什么值得单开一个脚本）────────────────────────────
# UI-01..UI-16 断言的全是**数据面**：锚点找得到、值对不对、点击有没有落到引擎。
# 它们对"控件存在但**看不见**"完全无感 —— 而 Qt Quick 最典型的布局缺陷正是这一类
# （T22 已经实测到 207×0 的"布局前尺寸"）。新增的 8 条把下面五类变成可判据：
#   ① 零尺寸（控件在、宽或高为 0）                          → UI-17
#   ② 被祖先裁掉 / 溢出所在面板                            → UI-18（沿父链逐级比对）
#   ③ 两两叠在一处（布局塌陷时全部堆在 (0,0)）              → UI-19
#   ④ 可见性成对：靶控件全可见 ∧ 非当前页控件不可见         → UI-20
#   ⑤ 文本空串 / 只显示占位符"?"、文本被列宽裁掉、前景≈背景  → UI-21 / UI-22 / UI-23
# 另立 UI-24 作**纯逻辑腿**（判别性对照）：用已知输入验算对比度与几何两个自写函数
# —— 没有它，任何一个函数写成 `return 21.0` / `return true`，活体判据会在任何输入
# 下全绿，而"全绿"看起来毫无破绽（血泪第 8 条）。
#
# ── 负控（本脚本的核心新东西）────────────────────────────────────────────────
# `STELQUICK_TIME_VISUAL_NEGCTL=1` 让套件在**跑完 UI-01..UI-16 之后**故意破坏四个
# 视觉事实（只动**观测面**，不动产品代码）：
#   · 月框 `height=0`                    ⇒ 期望 UI-17（几何退化）判红
#   · 年框 `visible=false`               ⇒ 期望 UI-20（可见性）与 UI-22（命中测试）判红
#   · JD 行 `text=""`                    ⇒ 期望 UI-21（文本非空）判红
#   · 状态行前景色 = 它的有效背景色       ⇒ 对比度 = 1.0 ⇒ 期望 UI-23 判红
# 判据必须**翻转**才算负控成立：脚本断言 rc=10 ∧ 这 5 条各自至少 1 条 FAIL。
# 若 rc=0（判据没红），说明判据是摆设 —— 记 negctl-BAD 并以非零退出。
#
# ⚠️ **UI-18 与 UI-19 在负控里是绿的，这是设计如此、不是漏了**：
#   几何快照取自扰动**之后**的同一同步块（所以 UI-17 立刻看到 height=0），
#   而 UI-18/UI-19 读的是下一拍的矩形（布局的 polish 已经把 height 改回去了）。
#   这两条的正确性由 UI-24 的**纯逻辑腿**背书（`uiRectInside` / `uiIntersectArea`
#   用已知输入验算），正是 T21 那套"纯逻辑腿 + 活引擎腿"的两条腿分工。
# 另断言：既有 19 条（UI-01..UI-16）在负控里必须**零 FAIL** —— 扰动发生在它们之后，
#   它们看到的是干净页；若这里出现 FAIL，说明扰动泄漏到了前面的相位。
set -u
cd /Users/ztuqfvy/qt_demo/stellarium_vulkan

export VK_DRIVER_FILES=/opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json
export QT_VULKAN_LIB=/opt/homebrew/opt/vulkan-loader/lib/libvulkan.1.dylib
export STELQUICK_GRAPHICS_API=metal

BIN=./build-release/src/ui/stelQuickUI.app/Contents/MacOS/stelQuickUI
OUT=docs/evidence/2026-09-29-t30-visual/mac
TARGET=${1:-all}
CORE_RUNS=${2:-5}
NEG_RUNS=${2:-3}
DYN_RUNS=${2:-3}
mkdir -p "$OUT"
FAILED=0

# 显示器休眠门（T29/W-T29 的教训，照抄）：本机电池档 displaysleep=2。显示器一旦睡，
# 焦点类判据的前提就不成立（而且布局/polish 也会停）⇒ 假红的来源。
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
    echo "环境注记：Spotlight 批量索引正在运行（mdbulkimport）——布局/polish 可能被拖慢"
  fi
  echo "二进制：$(ls -l "$BIN" | awk '{print $5" B  "$6" "$7" "$8}')  md5=$(md5 -q "$BIN")"
}

: > "$OUT/rc-summary.txt"

# ══ ① 正题：TIMEUICHECK × N（27 条判据 = 数据面 19 + 视觉层 8）═══════════════
if [[ "$TARGET" == "all" || "$TARGET" == "core" ]]; then
  integer pass=0 i rc
  : > "$OUT/timeuicheck-mac-n$CORE_RUNS.txt"
  for i in $(seq 1 $CORE_RUNS); do
    STELQUICK_TIME_UI_CHECK=1 "$BIN" > "$OUT/timeuicheck-mac-run$i.txt" 2>&1
    rc=$?
    echo "──────── TIMEUICHECK run $i/$CORE_RUNS（rc=$rc）────────" >> "$OUT/timeuicheck-mac-n$CORE_RUNS.txt"
    grep -E "TIMEUICHECK:" "$OUT/timeuicheck-mac-run$i.txt" >> "$OUT/timeuicheck-mac-n$CORE_RUNS.txt"
    [[ $rc -eq 0 ]] && (( pass++ ))
  done
  echo "timeuicheck-mac $pass/$CORE_RUNS PASS" | tee -a "$OUT/rc-summary.txt"

  # ══ ② 负控：破坏两个视觉事实 ⇒ 判据必须翻转 ═════════════════════════════════
  integer npass=0
  : > "$OUT/negctl-mac-n$NEG_RUNS.txt"
  for i in $(seq 1 $NEG_RUNS); do
    STELQUICK_TIME_UI_CHECK=1 STELQUICK_TIME_VISUAL_NEGCTL=1 "$BIN" \
      > "$OUT/negctl-mac-run$i.txt" 2>&1
    rc=$?
    hit17=$(/usr/bin/grep -c "UI-17 .*：FAIL" "$OUT/negctl-mac-run$i.txt")
    hit20=$(/usr/bin/grep -c "UI-20 .*：FAIL" "$OUT/negctl-mac-run$i.txt")
    hit21=$(/usr/bin/grep -c "UI-21 .*：FAIL" "$OUT/negctl-mac-run$i.txt")
    hit22=$(/usr/bin/grep -c "UI-22 .*：FAIL" "$OUT/negctl-mac-run$i.txt")
    hit23=$(/usr/bin/grep -c "UI-23 .*：FAIL" "$OUT/negctl-mac-run$i.txt")
    # 既有 19 条必须**不受扰动影响**（扰动发生在它们跑完之后）⇒ 它们的 FAIL 数应为 0
    oldfail=$(/usr/bin/grep -cE "UI-(0[1-9]|1[0-6])[a-z]? .*：FAIL" "$OUT/negctl-mac-run$i.txt")
    verdict="negctl-BAD"
    if [[ $rc -eq 10 && $hit17 -ge 1 && $hit20 -ge 1 && $hit21 -ge 1 \
          && $hit22 -ge 1 && $hit23 -ge 1 && $oldfail -eq 0 ]]; then
      verdict="negctl-OK"
      (( npass++ ))
    else
      FAILED=1
    fi
    {
      echo "──────── NEGCTL run $i/$NEG_RUNS（rc=$rc）→ $verdict ────────"
      echo "  UI-17 FAIL=$hit17  UI-20 FAIL=$hit20  UI-21 FAIL=$hit21" \
           "UI-22 FAIL=$hit22  UI-23 FAIL=$hit23（五条都要求 ≥1）"
      echo "  UI-18 FAIL=$(/usr/bin/grep -c 'UI-18 .*：FAIL' "$OUT/negctl-mac-run$i.txt")" \
           "UI-19 FAIL=$(/usr/bin/grep -c 'UI-19 .*：FAIL' "$OUT/negctl-mac-run$i.txt")" \
           "（这两条预期 **绿**：几何快照取自下一拍，见脚本头注）"
      echo "  既有 19 条（UI-01..UI-16）FAIL 命中=$oldfail（扰动在它们之后 ⇒ 要求 0）"
      grep -E "TIMEUICHECK: (VIS-note|UI-1[7-9]|UI-2[0-4]|判据|VERDICT)" "$OUT/negctl-mac-run$i.txt"
    } >> "$OUT/negctl-mac-n$NEG_RUNS.txt"
  done
  echo "negctl-mac $npass/$NEG_RUNS 翻转成立（要求 rc=10 ∧ UI-17/20/21/22/23 各 ≥1 条 FAIL ∧ 既有 19 条零 FAIL）" \
    | tee -a "$OUT/rc-summary.txt"

  # ══ ③ 相邻回归：本组改的是 TIMEUICHECK 所在的同一个 main.cpp ⇒ 全套重跑 ═══
  for spec in \
    "STELQUICK_TIME_CHECK=1:timecheck" \
    "STELQUICK_RETURN_UI_CHECK=1:returnuicheck" \
    "STELQUICK_INTERACT_UI_CHECK=1:interactcheck" \
    "STELQUICK_SEARCH_CHECK=1:searchcheck" \
    "STELQUICK_ACTION_CHECK=1:actioncheck" \
    "STELQUICK_LOCATE_CHECK=1:locatecheck" \
    "STELQUICK_UI_CHECK=1:locate-uicheck" \
    "STELQUICK_REPLAY_CHECK=1:replaycheck" \
    "STELQUICK_CLOCK_CHECK=1:clockcheck"
  do
    var=${spec%%:*}; name=${spec##*:}
    env $var "$BIN" > "$OUT/regression-$name.txt" 2>&1
    rc=$?
    echo "regression-$name rc=$rc" | tee -a "$OUT/rc-summary.txt"
    [[ $rc -eq 0 ]] || FAILED=1
  done
fi

# ══ ④ A2 逐像素（Metal）═════════════════════════════════════════════════════
if [[ "$TARGET" == "all" || "$TARGET" == "regress" || "$TARGET" == "core" ]]; then
  STELQUICK_A2_CHECK=1 "$BIN" > "$OUT/regression-a2-metal.txt" 2>&1
  rc=$?
  echo "regression-a2-metal rc=$rc" | tee -a "$OUT/rc-summary.txt"
  [[ $rc -eq 0 ]] || FAILED=1
fi

# ══ ⑤ DYN 双跑（真实引擎 + 替身判别性对照）══════════════════════════════════
# ⚠️ 生产者必须**显式**指定：`STELQUICK_DYN_PRODUCER` 默认是替身（main.cpp 只看
#    `== "engine"`），不设它就两次都跑替身 —— `tools/t29-verify.sh` 正是在这里漏了
#    变量，见 docs/evidence/2026-09-29-t29-ime/CORRECTION.md。
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
  # 判别性对照必须**回读身份**：engine 路的标签里必须是"真实引擎"，test 路必须是"替身"
  r1=$(/usr/bin/grep -m1 -h '生产者=' "$OUT/regression-dyn-engine-metal-run1.txt" 2>/dev/null)
  r2=$(/usr/bin/grep -m1 -h '生产者=' "$OUT/regression-dyn-test-metal-run1.txt" 2>/dev/null)
  if [[ "$r1" == *"真实引擎"* && "$r2" == *"替身场景"* ]]; then
    echo "producer-readback OK" | tee -a "$OUT/rc-summary.txt"
  else
    echo "producer-readback MISMATCH（engine=$r1 / test=$r2）" | tee -a "$OUT/rc-summary.txt"
    FAILED=1
  fi
fi

cat "$OUT/rc-summary.txt"
[[ -n "$CAFFEINATE_PID" ]] && kill "$CAFFEINATE_PID" 2>/dev/null
echo "T30 证据已写入 $OUT/（FAILED=$FAILED）"
[[ $FAILED -eq 0 ]] || exit 1
