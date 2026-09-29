#!/bin/zsh
# T32 验证脚本（2026-09-29）：**搜索框两段式 Esc**（有文本先清空、空则返回天空）。
#
# 用法：tools/t32-verify.sh              （跑全部）
#       tools/t32-verify.sh core 5       （正题 ×5 + 负控 ×3 + 探针 + 相邻回归）
#       tools/t32-verify.sh negctl 3     （只跑负控）
#       tools/t32-verify.sh regress      （只跑 A2 逐像素 + DYN 双跑）
#
# 证据落 docs/evidence/2026-09-29-t32-two-stage-esc/mac/。
# 环境口径（Metal + MoltenVK）**刻意与 t17..t31 / wt29 完全一致，不换**。
#
# ── 本轮改动面 ────────────────────────────────────────────────────────────────
# ① 产品：`src/ui/qml/MainWindow.qml` 的 Esc 分支加**两段式**（第一段清空文本、
#    第二段返回天空页），并排除 IME 组合态。**C++ 侧一行未改**、无新 Q_INVOKABLE。
# ② 仪器：`src/ui/main.cpp` 的 INTERACTCHECK 由 **16 条 → 18 条**（追加 IT-17/IT-18），
#    并把这两条追加进 `kFocusGatedIds`（相位序在 IT-16 之后 ⇒ 收尾的"后缀"自检仍成立）。
#
# ── 三组读数（口径随判据数一起变，**判据数增长是刻意的**）────────────────────────
# ① 正题（`STELQUICK_INTERACT_UI_CHECK=1` + `..._REQUEST_ACTIVATE=1`）
#    期望 `判据 18/18` + `VERDICT=PASS` + rc=0，且日志里必须有 `act-note`。
# ② 负控（`..._FORCE_FOCUSGATE_FAIL=1`）
#    期望 rc=6、`判据 12/12（另 7 条 UNAVAILABLE：IT-06,IT-13,IT-14,IT-15,IT-16,IT-17,IT-18）`、
#    `INTERACT-INTEGRITY` 绿、**IT-07..IT-12 真跑了 6/6**、零 ✗。
#    算账：18 条里 7 条被判据自己的相位点名跳过 ⇒ 11 条执行 + 1 条收尾自检 = 12/12。
#    （T31 时是 16 条跳 5 ⇒ 12/12 —— **执行数不变、UNAVAILABLE 5 → 7**，这是账、不是巧合。）
# ③ 探针（`..._FORCE_INACTIVE=1 ..._PROBE_ACTIVATION=1`）—— 分类的**实测依据**
#    期望 `判据 13/18`，红项**恰好** IT-06 / IT-13 / IT-14 / **IT-17 / IT-18**。
#    · IT-17 真红：失活 ⇒ 布场失败 ⇒ 前提腿（注入前文本非空）不成立；
#    · IT-18 真红：**靠判别腿**（注入时 `canDispatchToSky()` 必须为 false）。
#      没有那条腿它会**偶然通过** —— 失活时守卫回真 ⇒ Esc 走 T20 的返回分支同样回天空页。
#    ⚠️ 边界**平台相关**（W-T31 实证）：Windows 上同一探针另加 IT-05 / IT-16。
#       出厂表 = 两平台并集；本平台边界是它的子集。探针 rc=10 是**期望**，不是回归项。
set -u
cd /Users/ztuqfvy/qt_demo/stellarium_vulkan

export VK_DRIVER_FILES=/opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json
export QT_VULKAN_LIB=/opt/homebrew/opt/vulkan-loader/lib/libvulkan.1.dylib
export STELQUICK_GRAPHICS_API=metal

BIN=./build-release/src/ui/stelQuickUI.app/Contents/MacOS/stelQuickUI
OUT=docs/evidence/2026-09-29-t32-two-stage-esc/mac
TARGET=${1:-all}
CORE_RUNS=${2:-5}
NEG_RUNS=${2:-3}
DYN_RUNS=${2:-3}
mkdir -p "$OUT"
FAILED=0

# 显示器休眠门（T29/W-T29 的教训，照抄）：本机电池档 displaysleep=2。显示器一旦睡，
# 焦点类判据的前提就不成立（而且布局/polish 也会停）⇒ 假红的来源。
wake_display() {
  caffeinate -u -t 3 >/dev/null 2>&1
  caffeinate -dimsu -t 7200 >/dev/null 2>&1 &
  CAFFEINATE_PID=$!
  sleep 1
  echo "环境：已请求唤醒显示器并挂 caffeinate（pid=$CAFFEINATE_PID，2 小时）"
}
CAFFEINATE_PID=""
wake_display

# ── 前台提升（T31 定案：**由 app 自己抢前台**，不靠外部 `open -a`）──────────────
# 脚本 `exec` 直启的进程**不被 LaunchServices 认作 `.app` 实例** ⇒ 外部 `open -a` 只会
# 另起**幽灵实例**，要等的那个依旧 `isActive()==false`；且时序上必然晚于前导激活门
# 3×400ms 的窗口。**正解 = `STELQUICK_INTERACT_REQUEST_ACTIVATE=1`**（见 main.cpp 长注释）。
# 正题必须带这个 env；负控/探针**刻意不带**（它们要造失活）。
run_suite() {
  local out=$1; shift
  "$@" > "$out" 2>&1
  return $?
}

env_note() {
  echo "环境：$(uptime | sed 's/^ *//')"
  local ds
  ds=$(pmset -g custom 2>/dev/null | grep -E "^[[:space:]]*displaysleep" | head -1 | awk '{print $2}')
  [[ -n "$ds" ]] && echo "环境注记：displaysleep=${ds} 分钟（本机为 2 ⇒ 跑前必须 caffeinate 唤醒）"
  if pgrep -q mdbulkimport; then
    echo "环境注记：Spotlight 批量索引正在运行（mdbulkimport）——可能拖慢布局/polish"
  fi
  echo "二进制：$(ls -l "$BIN" | awk '{print $5" B  "$6" "$7" "$8}')  md5=$(md5 -q "$BIN")"
}

: > "$OUT/rc-summary.txt"

# ══ ① 正题：INTERACTCHECK × N（18 条）═══════════════════════════════════════
if [[ "$TARGET" == "all" || "$TARGET" == "core" ]]; then
  integer pass=0 envskip=0 bad=0 i rc
  : > "$OUT/interactcheck-mac-n$CORE_RUNS.txt"
  for i in $(seq 1 $CORE_RUNS); do
    run_suite "$OUT/interactcheck-mac-run$i.txt" env STELQUICK_INTERACT_UI_CHECK=1 \
      STELQUICK_INTERACT_REQUEST_ACTIVATE=1 "$BIN"
    rc=$?
    line=$(/usr/bin/grep -E "INTERACTCHECK: 判据" "$OUT/interactcheck-mac-run$i.txt" | tail -1)
    # 前台提升有没有真的武装（仪器自证；没有它跑出来的 18/18 不能信）
    armed=$(/usr/bin/grep -c "INTERACTCHECK: act-note" "$OUT/interactcheck-mac-run$i.txt")
    fails=$(/usr/bin/grep -cE "INTERACTCHECK: ✗" "$OUT/interactcheck-mac-run$i.txt")
    kind="FAIL"
    if [[ $rc -eq 0 && "$line" == *"判据 18/18"* && $armed -eq 1 && $fails -eq 0 ]]; then
      kind="PASS"; (( pass++ ))
    elif [[ $rc -eq 6 && "$line" == *"UNAVAILABLE"* && $fails -eq 0 ]]; then
      # 窗口没抢到前台 ⇒ 部分判据记 UNAVAILABLE（**不洗成 PASS**；具体条数落在日志里）
      kind="ENV-SKIP（窗口未激活 ⇒ 部分判据 UNAVAILABLE，非产品缺陷）"; (( envskip++ ))
    else
      kind="FAIL"; (( bad++ )); FAILED=1
    fi
    {
      echo "──────── INTERACTCHECK run $i/$CORE_RUNS（rc=$rc）→ $kind（前台提升已武装=$armed）────────"
      grep -E "INTERACTCHECK:" "$OUT/interactcheck-mac-run$i.txt"
    } >> "$OUT/interactcheck-mac-n$CORE_RUNS.txt"
  done
  echo "interactcheck-mac pos-pass=$pass env-skip=$envskip bad=$bad（共 $CORE_RUNS 跑；env-skip = 窗口未激活导致部分 UNAVAILABLE，**不洗成 PASS**）" \
    | tee -a "$OUT/rc-summary.txt"
  # 幽灵实例自证：本轮**不再**用外部 `open -a`，跑完不该有任何残留进程
  ghosts=$(pgrep -fc stelQuickUI 2>/dev/null || echo 0)
  echo "interactcheck-mac 残留进程数=$ghosts（要求 0；非 0 说明又出现了幽灵实例）" \
    | tee -a "$OUT/rc-summary.txt"
  [[ "$ghosts" == "0" ]] || FAILED=1
  # 至少要拿到一次"激活环境下全绿"，否则这一轮等于没测到正题
  if [[ $pass -eq 0 ]]; then
    echo "interactcheck-mac 正题未取得任何一次 18/18 —— 环境太差（FAILED）" | tee -a "$OUT/rc-summary.txt"
    FAILED=1
  fi

  # ══ ② 负控：强制降级 ⇒ 判据必须"被点名跳过 + 其余照跑"═══════════════════════
  integer npass=0
  : > "$OUT/negctl-mac-n$NEG_RUNS.txt"
  for i in $(seq 1 $NEG_RUNS); do
    run_suite "$OUT/negctl-mac-run$i.txt" env STELQUICK_INTERACT_UI_CHECK=1 \
      STELQUICK_INTERACT_FORCE_FOCUSGATE_FAIL=1 "$BIN"
    rc=$?
    line=$(/usr/bin/grep -E "INTERACTCHECK: 判据" "$OUT/negctl-mac-run$i.txt" | tail -1)
    verd=$(/usr/bin/grep -E "INTERACTCHECK: VERDICT" "$OUT/negctl-mac-run$i.txt" | tail -1)
    unavail=$(/usr/bin/grep -cE "INTERACTCHECK: ⊘ IT-(06|13|14|15|16|17|18) UNAVAILABLE" "$OUT/negctl-mac-run$i.txt")
    integrity=$(/usr/bin/grep -cE "INTERACTCHECK: ✓ INTERACT-INTEGRITY" "$OUT/negctl-mac-run$i.txt")
    # IT-07..IT-12 必须**真的跑了**（旧逻辑下它们根本不存在）
    ran=0
    for id in IT-07 IT-08 IT-09 IT-10 IT-11 IT-12; do
      /usr/bin/grep -qE "INTERACTCHECK: [✓✗] $id " "$OUT/negctl-mac-run$i.txt" && (( ran++ ))
    done
    fails=$(/usr/bin/grep -cE "INTERACTCHECK: ✗" "$OUT/negctl-mac-run$i.txt")
    verdict="negctl-BAD"
    if [[ $rc -eq 6 \
          && "$line" == *"判据 12/12（另 7 条 UNAVAILABLE：IT-06,IT-13,IT-14,IT-15,IT-16,IT-17,IT-18）"* \
          && "$verd" == *"UNAVAILABLE"* && $unavail -eq 7 && $integrity -eq 1 \
          && $ran -eq 6 && $fails -eq 0 ]]; then
      verdict="negctl-OK"
      (( npass++ ))
    else
      FAILED=1
    fi
    {
      echo "──────── NEGCTL run $i/$NEG_RUNS（rc=$rc）→ $verdict ────────"
      echo "  rc=$rc（要求 6）  UNAVAILABLE 逐条点名=$unavail（要求 7）  INTERACT-INTEGRITY 绿=$integrity（要求 1）"
      echo "  IT-07..IT-12 真跑了=$ran/6（要求 6；旧逻辑下这 6 条根本不存在）  全日志 ✗ 数=$fails（要求 0）"
      echo "  判据行： $line"
      grep -E "INTERACTCHECK: (⊘|判据|VERDICT|✓ INTERACT-INTEGRITY)" "$OUT/negctl-mac-run$i.txt"
    } >> "$OUT/negctl-mac-n$NEG_RUNS.txt"
  done
  echo "negctl-mac $npass/$NEG_RUNS 降级成立（要求 rc=6 ∧ 判据 12/12 ∧ 7 条逐项点名 ∧ INTERACT-INTEGRITY 绿 ∧ IT-07..12 全跑 ∧ 零 ✗）" \
    | tee -a "$OUT/rc-summary.txt"

  # ══ ③ 探针：分类的实测依据（rc=10 是**期望**，不是回归）══════════════════════
  # 探针要**造失活**，所以**不能**用上面的 run_suite（那是把 app 带到前台）。
  # ⚠️ 实测坑：`Qt::WindowDoesNotAcceptFocus` **挡不住**"新启动的 app 自动上前台"
  #   —— 带了这个 flag 的窗口照样 `isActive()=true`（前导门报"重试 0 次"）。
  #   可靠做法 = 用另一个 app（Finder）把前台抢走，stelQuickUI 变后台 ⇒ 真失活。
  STELQUICK_INTERACT_UI_CHECK=1 STELQUICK_INTERACT_FORCE_INACTIVE=1 \
    STELQUICK_INTERACT_PROBE_ACTIVATION=1 "$BIN" > "$OUT/probe-inactive-mac.txt" 2>&1 &
  probe_pid=$!
  sleep 1.5
  kill -0 "$probe_pid" 2>/dev/null && \
    open -a /System/Library/CoreServices/Finder.app >/dev/null 2>&1
  wait $probe_pid
  probe_rc=$?
  pline=$(/usr/bin/grep -E "INTERACTCHECK: 判据" "$OUT/probe-inactive-mac.txt" | tail -1)
  reds=$(/usr/bin/grep -E "INTERACTCHECK: ✗" "$OUT/probe-inactive-mac.txt" | sed -E 's/^INTERACTCHECK: ✗ (IT-[0-9]+) .*/\1/' | sort | tr '\n' ',')
  probe_verdict="probe-BAD"
  if [[ "$pline" == *"判据 13/18"* && "$reds" == "IT-06,IT-13,IT-14,IT-17,IT-18," ]]; then
    probe_verdict="probe-OK（本平台边界 = IT-06/13/14/17/18，是 kFocusGatedIds 两平台并集的子集）"
  else
    FAILED=1
  fi
  {
    echo "──────── 探针（FORCE_INACTIVE + PROBE_ACTIVATION）（rc=$probe_rc）→ $probe_verdict ────────"
    echo "  期望：判据 13/18，红项**恰好** IT-06 / IT-13 / IT-14 / IT-17 / IT-18（rc=10 是期望，非回归）"
    echo "  实测：$pline   红项=[$reds]"
    grep -E "INTERACTCHECK: (PROBE-note|✗|判据|VERDICT)" "$OUT/probe-inactive-mac.txt"
  } > "$OUT/probe-inactive-mac-summary.txt"
  echo "probe-inactive-mac $probe_verdict（$pline；红=[$reds]）" | tee -a "$OUT/rc-summary.txt"

  # ══ ④ 相邻回归：本组改的是 INTERACTCHECK 所在的同一个 main.cpp ⇒ 全套重跑 ═══
  for spec in \
    "STELQUICK_TIME_CHECK=1:timecheck" \
    "STELQUICK_RETURN_UI_CHECK=1:returnuicheck" \
    "STELQUICK_SEARCH_CHECK=1:searchcheck" \
    "STELQUICK_ACTION_CHECK=1:actioncheck" \
    "STELQUICK_LOCATE_CHECK=1:locatecheck" \
    "STELQUICK_UI_CHECK=1:locate-uicheck" \
    "STELQUICK_REPLAY_CHECK=1:replaycheck" \
    "STELQUICK_CLOCK_CHECK=1:clockcheck" \
    "STELQUICK_TIME_UI_CHECK=1:timeuicheck"
  do
    var=${spec%%:*}; name=${spec##*:}
    run_suite "$OUT/regression-$name.txt" env $var "$BIN"
    rc=$?
    echo "regression-$name rc=$rc" | tee -a "$OUT/rc-summary.txt"
    [[ $rc -eq 0 ]] || FAILED=1
  done
fi

# ══ ⑤ A2 逐像素（Metal）═════════════════════════════════════════════════════
if [[ "$TARGET" == "all" || "$TARGET" == "regress" || "$TARGET" == "core" ]]; then
  run_suite "$OUT/regression-a2-metal.txt" env STELQUICK_A2_CHECK=1 "$BIN"
  rc=$?
  echo "regression-a2-metal rc=$rc" | tee -a "$OUT/rc-summary.txt"
  [[ $rc -eq 0 ]] || FAILED=1
fi

# ══ ⑥ DYN 双跑（真实引擎 + 替身判别性对照）══════════════════════════════════
# ⚠️ 生产者必须**显式**指定：`STELQUICK_DYN_PRODUCER` 默认是替身（main.cpp 只看
#    `== "engine"`），不设它就两次都跑替身 —— `tools/t29-verify.sh` 正是在这里漏了
#    变量，见 docs/evidence/2026-09-29-t29-ime/CORRECTION.md。
run_dyn() {
  local kind=$1 n=$DYN_RUNS pass=0 i rc
  : > "$OUT/regression-dyn-$kind-metal.txt"
  for i in $(seq 1 $n); do
    run_suite "$OUT/regression-dyn-$kind-metal-run$i.txt" env STELQUICK_DYN_CHECK=1 \
      STELQUICK_DYN_PRODUCER=$kind "$BIN"
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
echo "T32 证据已写入 $OUT/（FAILED=$FAILED）"
[[ $FAILED -eq 0 ]] || exit 1
