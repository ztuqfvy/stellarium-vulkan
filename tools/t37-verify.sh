#!/bin/zsh
# T37 验证脚本（2026-09-30）：**设置页骨架 + 夜视闭环（主题/夜视效果层定性）**。
#
# 用法：tools/t37-verify.sh              （跑全部）
#       tools/t37-verify.sh pos 3        （只跑正题 NIGHTCHECK ×3）
#       tools/t37-verify.sh negctl       （只跑负控 EFFECT_OFF ×2）
#       tools/t37-verify.sh regress      （只跑相邻回归）
#
# 证据落 docs/evidence/2026-09-30-t37-nightmode/mac/。
# 环境口径（Metal + MoltenVK）**刻意与 t17..t36 完全一致，不换**。
#
# ── 这一轮要定的"性"（**T37-C 三轮反转后的最终口径**）─────────────────────────
# A-1.0 范围表备注原写"天空夜视效果仍由旧后处理负责，避免叠加两次"。T37 探针+
# 判别实验证伪了它在合流形态的前提：
#   · 引擎旧夜视 = NightModeGraphicsEffect（纯 GL shader + QOpenGLFramebufferObject，
#     挂 QGraphicsItem）——只在 QGraphicsView 场景渲染循环里被调用；
#   · 合流形态宿主 WA_DontShowOnScreen + 帧走 LegacySkyHost 自建 FBO +
#     StelApp::draw()（无夜视分支）+ Metal RHI 与 GL effect 两条独立图形栈
#     ⇒ **物理不可达**（实验：禁用该 effect 行为**不变**；宿主 property("nightMode")
#     0→1 证明 connect 链活着，但 effect 没画）。
#   ⚠️ 别引用"上游 OFF↔ON 逐位相同"当证据 —— 那是首轮**假绿**（等待挂读步，抓到
#     旧帧，TRAPS 69）。真实情况：**引擎自己对夜视有既有反应**（三处大气类
#     `if (getVisionModeNight()) return;` ⇒ 夜视开启时大气整层退场，原版语义）；
#     这既不是"夜视滤镜"，与"红移归谁做"也无关 —— T37-C 自检在 S1 把大气置关
#     来隔离它（NC-03① 的前提）。
# ⇒ 落点改为 **Qt Quick 侧效果层**：keySink.layer + ShaderEffect（.qsb 预编译，
#   公式逐字复刻引擎 lum=max(r,g,b)→(lum,0.3lum,0)）。实测视口内 100% 像素差、
#   平均通道差 ~14.5（暗星空下滤镜只显著改写星点/线条，全帧均值个位数）；
#   引擎上游帧低于噪声容差。
#
# ── 本脚本的判据分工（**内外两层，别互相顶替**）──────────────────────────────
#   内层（被测自检 NIGHTCHECK，6 条）：NC-01..NC-05，见 NightModeCheck.hpp。
#     NC-01 状态面（getter 独立回读）｜NC-02 噪声底门｜NC-03① 上游低于噪声容差
#     NC-03② 下游视口 ≥90% ∧ 平均通道差 >1.0｜NC-04 判别对照（承重）｜NC-05 复原
#   外层（本脚本，**换来源的对照量**，陷阱 43）：
#     · 正题 ×3 全 PASS（rc=0 / 判据 6/6 / 红项空）；
#     · 负控：STELQUICK_NIGHT_EFFECT_OFF=1 恒关 layer ⇒ NC-03② **恰好**红
#       （rc=10 / 判据 5/6）——证明 NC-03② 承重，不是摆设；
#     · 红项集合按**预期集合**逐位比对。
#
# ── 环境门（本轮血泪，写进脚本而不是靠人记）──────────────────────────────────
#   · 显示器休眠门照抄 t34..t36（本机 displaysleep=2）；
#   · **Spotlight 索引（mdbulkimport）= 负载毒源**：实测高负载下帧流出现
#     "黑天空撕裂帧"（T37-X2）、噪声底失效、引导门挂死（T37-X3）。本脚本
#     检测到 mdbulkimport 时打环境注记；正题要求 3/3，ENV 劣化跑出来的结果
#     不许洗 PASS——停索引或换时段重跑；
#   · 每个 app 运行都带**看门狗**：引导门挂死（视口 0x0 不退出）时强杀，
#     rc=124 计失败（T37-X3 修复前这是真实风险）。
set -u
cd /Users/ztuqfvy/qt_demo/stellarium_vulkan

export VK_DRIVER_FILES=/opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json
export QT_VULKAN_LIB=/opt/homebrew/opt/vulkan-loader/lib/libvulkan.1.dylib
export STELQUICK_GRAPHICS_API=metal

BIN=./build-release/src/ui/stelQuickUI.app/Contents/MacOS/stelQuickUI
ROOT=docs/evidence/2026-09-30-t37-nightmode
OUT=$ROOT/mac
TARGET=${1:-all}
POS_RUNS=${2:-3}
NEG_RUNS=${2:-2}
WATCHDOG_SEC=${WATCHDOG_SEC:-180}
mkdir -p "$OUT"
FAILED=0

# ── 看门狗运行器（T37-X3：引导门会挂死不退出，必须有界）────────────────────
run_suite() {
  local out=$1; shift
  "$@" > "$out" 2>&1 &
  local pid=$! waited=0
  while (( waited < WATCHDOG_SEC )); do
    kill -0 $pid 2>/dev/null || break
    sleep 2; (( waited += 2 ))
  done
  if kill -0 $pid 2>/dev/null; then
    kill -9 $pid 2>/dev/null; wait $pid 2>/dev/null
    {
      echo "WATCHDOG: ${WATCHDOG_SEC}s 超时强杀（引导门挂死？见 T37-X3）"
      echo "WATCHDOG: 日志尾部："
      tail -5 "$out"
    } >> "$out.wd"
    return 124
  fi
  wait $pid; return $?
}

# ── 显示器休眠门（照抄 t34..t36）────────────────────────────────────────────
wake_display() {
  caffeinate -u -t 3 >/dev/null 2>&1
  caffeinate -dimsu -t 7200 >/dev/null 2>&1 &
  CAFFEINATE_PID=$!
  sleep 1
  echo "环境：已请求唤醒显示器并挂 caffeinate（pid=$CAFFEINATE_PID，2 小时）"
}
CAFFEINATE_PID=""
wake_display

env_note() {
  echo "环境：$(uptime | sed 's/^ *//')"
  if pgrep -q mdbulkimport; then
    echo "环境注记：⚠️ Spotlight 批量索引正在运行（mdbulkimport）——实测负载毒源" \
         "（T37-X2 撕裂帧 / T37-X3 引导挂死）。正题若不绿，先停索引再重跑。"
  else
    echo "环境注记：Spotlight 空闲"
  fi
  echo "二进制：$(ls -l "$BIN" | awk '{print $5" B  "$6" "$7" "$8}')  md5=$(md5 -q "$BIN")"
}

# ── 读数与判据抽取（只用原始量，不复刻被测逻辑）──────────────────────────────
judge_line() { /usr/bin/grep -E "NIGHTCHECK: .*判据 [0-9]+/[0-9]+" "$1" | LC_ALL=C /usr/bin/tail -1; }
verdict_line() { /usr/bin/grep -E "NIGHTCHECK: VERDICT=" "$1" | LC_ALL=C /usr/bin/tail -1; }
red_ids() {
  /usr/bin/grep -E "NIGHTCHECK: +\[FAIL\] " "$1" \
    | /usr/bin/sed -E 's/.*\[FAIL\] (NC-[^ ]+).*/\1/' \
    | LC_ALL=C /usr/bin/sort | /usr/bin/tr '\n' ',' | sed 's/,$//'
}

declare -i SCRIPT_PASS=0 SCRIPT_FAIL=0
judge() {
  if [[ "$1" -eq 0 ]]; then
    (( SCRIPT_PASS++ )); echo "  ✓ $2" | tee -a "$OUT/rc-summary.txt"
  else
    (( SCRIPT_FAIL++ )); FAILED=1; echo "  ✗ $2" | tee -a "$OUT/rc-summary.txt"
  fi
}

: > "$OUT/rc-summary.txt"
env_note | tee -a "$OUT/rc-summary.txt"

# ══ ① 正题：NIGHTCHECK ×3（全绿才算数）══════════════════════════════════════
if [[ "$TARGET" == "all" || "$TARGET" == "pos" ]]; then
  echo "──────── 正题 NIGHTCHECK ×$POS_RUNS（预期 rc=0 / 判据 6/6 / 红项空）────────" \
    | tee -a "$OUT/rc-summary.txt"
  local_ok=0
  for i in $(seq 1 $POS_RUNS); do
    run_suite "$OUT/nightcheck-run$i.txt" env STELQUICK_NIGHT_CHECK=1 "$BIN"
    rc=$?
    line=$(judge_line "$OUT/nightcheck-run$i.txt"); verd=$(verdict_line "$OUT/nightcheck-run$i.txt")
    reds=$(red_ids "$OUT/nightcheck-run$i.txt")
    echo "  · run$i rc=$rc（$verd；红项 [$reds]）" | tee -a "$OUT/rc-summary.txt"
    if [[ $rc -eq 0 && "$verd" == *"VERDICT=PASS"* && "$line" == *"判据 6/6"* && -z "$reds" ]]; then
      (( local_ok++ ))
    else
      judge 1 "正题 run$i 不绿（rc=$rc $verd 红项 [$reds]）——ENV 劣化不许洗 PASS，查明再跑"
    fi
  done
  judge $([[ $local_ok -eq $POS_RUNS ]] && echo 0 || echo 1) \
    "正题 $local_ok/$POS_RUNS 全绿（判据 6/6 × $POS_RUNS）"
  echo "  · run1 判据全文（见 $OUT/nightcheck-run1.txt）：" | tee -a "$OUT/rc-summary.txt"
  /usr/bin/grep -E "^NIGHTCHECK: " "$OUT/nightcheck-run1.txt" | sed 's/^/      /' \
    | tee -a "$OUT/rc-summary.txt"
fi

# ══ ② 负控：恒关 layer ⇒ NC-03② 恰好红 ×2 ══════════════════════════════════
if [[ "$TARGET" == "all" || "$TARGET" == "negctl" ]]; then
  echo "──────── 负控 STELQUICK_NIGHT_EFFECT_OFF=1 ×$NEG_RUNS（预期 rc=10 / 判据 5/6 / 红项 [NC-03②]）────────" \
    | tee -a "$OUT/rc-summary.txt"
  neg_ok=0
  for i in $(seq 1 $NEG_RUNS); do
    run_suite "$OUT/negctl-effectoff-run$i.txt" env STELQUICK_NIGHT_EFFECT_OFF=1 \
      STELQUICK_NIGHT_CHECK=1 "$BIN"
    rc=$?
    line=$(judge_line "$OUT/negctl-effectoff-run$i.txt"); verd=$(verdict_line "$OUT/negctl-effectoff-run$i.txt")
    reds=$(red_ids "$OUT/negctl-effectoff-run$i.txt")
    echo "  · run$i rc=$rc（$verd；红项 [$reds]）" | tee -a "$OUT/rc-summary.txt"
    if [[ $rc -eq 10 && "$line" == *"判据 5/6"* && "$reds" == "NC-03②" ]]; then
      (( neg_ok++ ))
    else
      judge 1 "负控 run$i 红项不是恰好 [NC-03②]（rc=$rc $reds）——负控必须命中且只命中承重项"
    fi
  done
  judge $([[ $neg_ok -eq $NEG_RUNS ]] && echo 0 || echo 1) \
    "负控 $neg_ok/$NEG_RUNS 命中恰好 [NC-03②]（NC-03② 承重证明）"
  echo "  负控的红是**预期**结果（效果被关掉 ⇒ 效果判据必须红）；读数：" \
    | tee -a "$OUT/rc-summary.txt"
  /usr/bin/grep -E "^NIGHTCHECK: +\[|VERDICT" "$OUT/negctl-effectoff-run1.txt" | sed 's/^/      /' \
    | tee -a "$OUT/rc-summary.txt"
fi

# ══ ③ 相邻回归（十二套件 + INTERACTCHECK + A2 + DYN 双路 + S3）══════════════
if [[ "$TARGET" == "all" || "$TARGET" == "regress" ]]; then
  for spec in \
    "STELQUICK_TIME_CHECK=1:timecheck" \
    "STELQUICK_RETURN_UI_CHECK=1:returnuicheck" \
    "STELQUICK_SEARCH_CHECK=1:searchcheck" \
    "STELQUICK_ACTION_CHECK=1:actioncheck" \
    "STELQUICK_LOCATE_CHECK=1:locatecheck" \
    "STELQUICK_UI_CHECK=1:locate-uicheck" \
    "STELQUICK_REPLAY_CHECK=1:replaycheck" \
    "STELQUICK_CLOCK_CHECK=1:clockcheck" \
    "STELQUICK_TIME_UI_CHECK=1:timeuicheck" \
    "STELQUICK_LOC_CHECK=1:locationcheck" \
    "STELQUICK_TOOL_CHECK=1:toolbarcheck" \
    "STELQUICK_TIMELINK_CHECK=1:timelinkcheck" \
    "STELQUICK_CONFIG_CHECK=1:configcheck"
  do
    var=${spec%%:*}; name=${spec##*:}
    run_suite "$OUT/regression-$name.txt" env $var "$BIN"
    rc=$?
    echo "regression-$name rc=$rc" | tee -a "$OUT/rc-summary.txt"
    [[ $rc -eq 0 ]] || FAILED=1
  done

  # INTERACTCHECK：依赖窗口真的拿到系统焦点，ENV-SKIP 通道照抄 t36
  run_suite "$OUT/regression-interactcheck.txt" env STELQUICK_INTERACT_UI_CHECK=1 \
    STELQUICK_INTERACT_REQUEST_ACTIVATE=1 "$BIN"
  irc=$?
  iline=$(/usr/bin/grep -E "INTERACTCHECK: 判据" "$OUT/regression-interactcheck.txt" | tail -1)
  ireds=$(/usr/bin/grep -cE "INTERACTCHECK: ✗" "$OUT/regression-interactcheck.txt")
  if [[ $irc -eq 0 && "$iline" == *"判据 18/18"* && $ireds -eq 0 ]]; then
    echo "regression-interactcheck rc=0（18/18，窗口已激活）" | tee -a "$OUT/rc-summary.txt"
  elif [[ $irc -eq 6 && "$iline" == *"UNAVAILABLE"* && $ireds -eq 0 ]]; then
    echo "regression-interactcheck ENV-SKIP（$iline ⇒ 窗口未获系统焦点，**不洗成 PASS**）" \
      | tee -a "$OUT/rc-summary.txt"
  else
    echo "regression-interactcheck FAIL（rc=$irc；$iline；✗=$ireds）" | tee -a "$OUT/rc-summary.txt"
    FAILED=1
  fi

  # A2 逐像素（Metal）
  run_suite "$OUT/regression-a2-metal.txt" env STELQUICK_A2_CHECK=1 "$BIN"
  rc=$?
  echo "regression-a2-metal rc=$rc" | tee -a "$OUT/rc-summary.txt"
  [[ $rc -eq 0 ]] || FAILED=1

  # DYN 双路 ×3
  run_dyn() {
    local kind=$1 n=3 pass=0 i rc
    : > "$OUT/regression-dyn-$kind-metal.txt"
    for i in $(seq 1 $n); do
      run_suite "$OUT/regression-dyn-$kind-metal-run$i.txt" env STELQUICK_DYN_CHECK=1 \
        STELQUICK_DYN_PRODUCER=$kind "$BIN"
      rc=$?
      echo "──────── DYN($kind) run $i/$n（rc=$rc）────────" >> "$OUT/regression-dyn-$kind-metal.txt"
      /usr/bin/grep -E "DYNCHECK:" "$OUT/regression-dyn-$kind-metal-run$i.txt" >> "$OUT/regression-dyn-$kind-metal.txt"
      [[ $rc -eq 0 ]] && (( pass++ ))
    done
    echo "regression-dyn-$kind-metal $pass/$n PASS" | tee -a "$OUT/rc-summary.txt"
    [[ $pass -eq $n ]] || FAILED=1
  }
  run_dyn engine 3
  run_dyn stub 3

  # S3 旧宿主（rc-only；写原版目录属其预期行为）
  STELA3_CHECK=1 ./build-release/src/stellarium > "$OUT/regression-s3-stela3.txt" 2>&1
  rc=$?
  echo "regression-s3-stela3 rc=$rc（旧形态）" | tee -a "$OUT/rc-summary.txt"
  [[ $rc -eq 0 ]] || FAILED=1
fi

# ── 汇总 ───────────────────────────────────────────────────────────────────
{
  echo "════════════════════════════════════════"
  echo "脚本层判据：$SCRIPT_PASS 通过 / $SCRIPT_FAIL 失败"
  env_note
} | tee -a "$OUT/rc-summary.txt"

[[ $FAILED -eq 0 && $SCRIPT_FAIL -eq 0 ]] && echo "SCRIPT-RC=0 FAILED=0" || echo "SCRIPT-RC=1 FAILED=1"
exit $(( FAILED || SCRIPT_FAIL ? 1 : 0 ))
