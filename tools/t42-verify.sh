#!/bin/zsh
# T42 验证脚本（2026-10-01）：**状态与错误页 + "未支持项"清单**。
#
# 用法：tools/t42-verify.sh              （跑全部）
#       tools/t42-verify.sh pos 3        （只跑正题 ERRORCHECK ×3）
#       tools/t42-verify.sh failleg 2    （只跑失败腿 ×2）
#       tools/t42-verify.sh negctl       （只跑三组负控 ×2）
#       tools/t42-verify.sh regress      （只跑相邻回归）
#
# 证据落 docs/evidence/2026-10-01-t42-errors/mac/。
# 环境口径（Metal + MoltenVK）**刻意与 t17..t41 完全一致，不换**。
#
# ── A-1.0 / A5 的这一格 ─────────────────────────────────────────────────────
# A-1.0 范围表第二格 `|资源路径、错误提示、渲染诊断、配置保存 | 最小 | 必须 |`：
#   配置保存 T36 交付、渲染诊断 T39 交付 ⇒ **本任务交付余下两项（资源路径 + 错误提示）**。
# A5 行 `A-1.0 全部页面、配置迁移、夜视、错误页` 的"错误页"。
# 开发指导文档：旧 UI 禁止作为隐式回退；**未支持项有清单和明确提示**。
#
# ── 这一轮的"性"（T42-A 探针先实测，见 probe-error-mac.txt，57 行读数）────────
#   ① Windows 组真身 **10 个**（9 个 actionShow_*_Window_Global + ⌥B 观测列表
#      —— **⌥B 是 T41 的遗漏**，本轮收编）；
#   ② 未接管动作 trigger() **真的会弹老窗口**（F10 Δ697、⌥B Δ58 —— 证实 T41 推断）；
#   ③ 🔴 **推翻开工假设**：老对话框会把尺寸写进 QSettings 的 `DialogSizes/<Name>`
#      ⇒ 判据必须自净，也是比 QWidget 计数更精细的独立证据通道；
#   ④ 🔴 `StelFileMgr::init()` 用户目录不可创建 ⇒ **qFatal/SIGABRT**（进程直接崩），
#      但 `STEL_USERDIR` 可注入**真实**失败 ⇒ 产品侧在 boot() 加**可创建性预检**。
#
# ── 本轮判据抓出/修掉的东西 ─────────────────────────────────────────────────
#   · 陷阱 85 第三次重演：mark() 只写 details 没写台账 ⇒ 14 条全 PASS 却
#     VERDICT=FAIL(0/14) ⇒ mark 按 note 首词回写台账 + finish() 报"未记账 id"；
#   · EC-13 首轮红：clickButton 的 QMouseEvent localPos 传了按钮局部坐标，
#     而 QQuickWindow 用窗口坐标 ⇒ 点击落在窗口左上角 ⇒ 两处都传 scenePos；
#   · EC-14 首轮红：EC-10 判别对照弹老对话框写 DialogSizes ⇒ 收尾按基线还原；
#   · 🔴 **EC-09 首版只判不净** ⇒ 负控 A 下 EC-10 早退、无人还原 ⇒
#     DialogSizes/Configuration 765,605→770,656 残留真实 config ⇒ EC-09 补自净
#     （本脚本因此加了 **config 零污染门**：全程 md5 前后一致）。
#
# ── 本脚本的判据分工（内外两层）──────────────────────────────────────────────
#   内层（被测自检 ERRORCHECK）：A 组 **15 条**（引擎可用）/ B 组 **5 条**（失败腿），
#     见 app/ErrorCheck.hpp 头注。两条腿同一二进制同一判据代码，差异只在环境。
#   外层（本脚本，换来源的对照量）：
#     · 正题 ×3 全 PASS（rc=0 / 判据 15/15 / 红项空）；
#     · 失败腿 ×2（STEL_USERDIR=/dev/null/xxx 造**真实**失败，rc=0 / 判据 5/5）；
#     · 负控 A `STELQUICK_ERROR_TAKEOVER_OFF=1` ⇒ 红 **[EC-08,EC-09,EC-14]**（接管域）
#     · 负控 B `STELQUICK_ERROR_PATHS_OFF=1` ⇒ 红 **[EC-01..EC-04,EC-11]**（路径域）
#     · 负控 C `STELQUICK_ERROR_UNSUPPORTED_OFF=1` ⇒ 红 **[EC-06,EC-07,EC-11]**（清单域）
#       （期望值全部**实跑出来**再写死 —— 陷阱 87；两轮逐位一致后才落笔）
#     · 正交性：A∩B=∅、A∩C=∅；B∩C={EC-11}（同一判据的不同子段，note 里以
#       "缺 pathRow_*/缺 unsupportedRow_*"区分）；
#     · ⚠️ `STELQUICK_ERROR_OPEN_OFF` **不是负控**（没有判据因它变红 —— 红不了的
#       负控不承重，陷阱 67）；它是 EC-15 受理半边的无副作用布场开关。
#     · **config 零污染门**：整轮脚本前后真实 config.ini md5 必须一致。
#
# ── 环境门（沿用 t34..t41）──────────────────────────────────────────────────
#   · 显示器休眠门照抄；**批跑前 screencapture -x 查全屏遮挡**（陷阱 93）；
#   · Spotlight 索引（mdbulkimport）= 负载毒源：检测到就打环境注记，不许洗 PASS；
#   · 每个 app 运行都带看门狗；连续起停 Metal 掉设备 = 环境噪声不是判据失败。
set -u
cd /Users/ztuqfvy/qt_demo/stellarium_vulkan

export VK_DRIVER_FILES=/opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json
export QT_VULKAN_LIB=/opt/homebrew/opt/vulkan-loader/lib/libvulkan.1.dylib
export STELQUICK_GRAPHICS_API=metal

BIN=./build-release/src/ui/stelQuickUI.app/Contents/MacOS/stelQuickUI
ROOT=docs/evidence/2026-10-01-t42-errors
OUT=$ROOT/mac
TARGET=${1:-all}
POS_RUNS=${2:-3}
NEG_RUNS=${2:-2}
WATCHDOG_SEC=${WATCHDOG_SEC:-180}
mkdir -p "$OUT"
FAILED=0

# ── 看门狗运行器 ────────────────────────────────────────────────────────────
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

# ── 显示器休眠门 + 全屏遮挡预检（陷阱 93）────────────────────────────────────
wake_display() {
  caffeinate -u -t 3 >/dev/null 2>&1
  caffeinate -dimsu -t 7200 >/dev/null 2>&1 &
  CAFFEINATE_PID=$!
  sleep 1
  echo "环境：已请求唤醒显示器并挂 caffeinate（pid=$CAFFEINATE_PID，2 小时）"
}
CAFFEINATE_PID=""
wake_display

# ── config 零污染门（T42 本轮血泪：判据自净不彻底会写穿真实配置）──────────────
CFG="$HOME/Library/Application Support/Stellarium-quick/config.ini"
CFG_MD5_PRE=$(md5 -q "$CFG" 2>/dev/null || echo "(missing)")

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

# ── 读数与判据抽取（只用原始量）─────────────────────────────────────────────
judge_line() { /usr/bin/grep -E "ERRORCHECK: .*判据 [0-9]+/[0-9]+" "$1" | LC_ALL=C /usr/bin/tail -1; }
verdict_line() { /usr/bin/grep -E "ERRORCHECK: VERDICT=" "$1" | LC_ALL=C /usr/bin/tail -1; }
red_ids() {
  # ⚠️ ErrorCheck 的明细行是 `ERRORCHECK:[FAIL] ...`（冒号后**无空格**；与
  #    HELPCHECK 的 `ERRORCHECK: [FAIL]` 风格不同）—— 首版正则 `: +\[FAIL\]`
  #    要求空格 ⇒ 提取恒空，负控明明 rc=5/12/15 却判"红项 []"（本轮实测翻车）。
  /usr/bin/grep -E "ERRORCHECK: ?\[FAIL\] " "$1" \
    | /usr/bin/sed -E 's/.*\[FAIL\] (EC-[0-9A-Za-z]+).*/\1/' \
    | LC_ALL=C /usr/bin/sort -u | /usr/bin/tr '\n' ',' | sed 's/,$//'
}
# 交集（逗号串）：$1 ∩ $2
set_inter() {
  local a=$1 b=$2 out=""
  local IFS=,
  for x in ${=a}; do
    [[ ",$b," == *",$x,"* ]] && out="$out${out:+,}$x"
  done
  echo "$out"
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
echo "config 零污染门：起跑 md5=$CFG_MD5_PRE" | tee -a "$OUT/rc-summary.txt"

# ══ ① 正题：ERRORCHECK ×3（全绿才算数）═══════════════════════════════════════
POS_REDS=""
if [[ "$TARGET" == "all" || "$TARGET" == "pos" ]]; then
  echo "──────── 正题 ERRORCHECK ×$POS_RUNS（预期 rc=0 / 判据 15/15 / 红项空）────────" \
    | tee -a "$OUT/rc-summary.txt"
  pos_ok=0
  for i in $(seq 1 $POS_RUNS); do
    run_suite "$OUT/errorcheck-pos-run$i.txt" env STELQUICK_ERROR_CHECK=1 "$BIN"
    rc=$?
    line=$(judge_line "$OUT/errorcheck-pos-run$i.txt"); verd=$(verdict_line "$OUT/errorcheck-pos-run$i.txt")
    reds=$(red_ids "$OUT/errorcheck-pos-run$i.txt")
    echo "  · run$i rc=$rc（$verd；红项 [$reds]）" | tee -a "$OUT/rc-summary.txt"
    if [[ $rc -eq 0 && "$verd" == *"VERDICT=PASS"* && "$line" == *"判据 15/15"* && -z "$reds" ]]; then
      (( pos_ok++ ))
    else
      judge 1 "正题 run$i 不绿（rc=$rc $verd 红项 [$reds]）——ENV 劣化不许洗 PASS，查明再跑"
    fi
  done
  judge $([[ $pos_ok -eq $POS_RUNS ]] && echo 0 || echo 1) \
    "正题 $pos_ok/$POS_RUNS 全绿（判据 15/15 × $POS_RUNS）"
  POS_REDS=$(red_ids "$OUT/errorcheck-pos-run1.txt")
  echo "  · run1 判据全文（见 $OUT/errorcheck-pos-run1.txt）：" | tee -a "$OUT/rc-summary.txt"
  /usr/bin/grep -E "^ERRORCHECK:?" "$OUT/errorcheck-pos-run1.txt" | sed 's/^/      /' \
    | tee -a "$OUT/rc-summary.txt"
fi

# ══ ② 失败腿：STEL_USERDIR 造真实失败 ×2（rc=0 / 判据 5/5）═══════════════════
if [[ "$TARGET" == "all" || "$TARGET" == "failleg" ]]; then
  echo "──────── 失败腿 STEL_USERDIR=/dev/null/xxx ×2（预期 rc=0 / 判据 5/5 / VERDICT=PASS）────────" \
    | tee -a "$OUT/rc-summary.txt"
  fl_ok=0
  for i in $(seq 1 2); do
    run_suite "$OUT/errorcheck-failleg-run$i.txt" env STEL_USERDIR=/dev/null/xxx \
      STELQUICK_ERROR_CHECK=1 "$BIN"
    rc=$?
    line=$(judge_line "$OUT/errorcheck-failleg-run$i.txt"); verd=$(verdict_line "$OUT/errorcheck-failleg-run$i.txt")
    reds=$(red_ids "$OUT/errorcheck-failleg-run$i.txt")
    echo "  · run$i rc=$rc（$verd；红项 [$reds]）" | tee -a "$OUT/rc-summary.txt"
    if [[ $rc -eq 0 && "$verd" == *"VERDICT=PASS"* && "$line" == *"判据 5/5"* && -z "$reds" ]]; then
      (( fl_ok++ ))
    else
      judge 1 "失败腿 run$i 不绿（rc=$rc $verd 红项 [$reds]）——预检应把 qFatal 变成可操作的失败"
    fi
  done
  judge $([[ $fl_ok -eq 2 ]] && echo 0 || echo 1) \
    "失败腿 $fl_ok/2 全绿（引导失败 ⇒ 错误页，真实失败路径不造假桩）"
fi

# ══ ③ 负控 A/B/C ×2（期望值实跑出来再写死，陷阱 87）══════════════════════════
NEG_A_REDS="EC-08,EC-09,EC-14"
NEG_B_REDS="EC-01,EC-02,EC-03,EC-04,EC-11"
NEG_C_REDS="EC-06,EC-07,EC-11"
if [[ "$TARGET" == "all" || "$TARGET" == "negctl" ]]; then
  for spec in \
    "A:STELQUICK_ERROR_TAKEOVER_OFF=1:$NEG_A_REDS:12/15:negctl-takeoveroff" \
    "B:STELQUICK_ERROR_PATHS_OFF=1:$NEG_B_REDS:10/15:negctl-pathsoff" \
    "C:STELQUICK_ERROR_UNSUPPORTED_OFF=1:$NEG_C_REDS:12/15:negctl-unsupportedoff"
  do
    name=${spec%%:*}; rest=${spec#*:}
    envvar=${rest%%:*}; rest=${rest#*:}
    want=${rest%%:*}; rest=${rest#*:}
    frac=${rest%%:*}; base=${rest#*:}
    echo "──────── 负控 $name $envvar ×$NEG_RUNS（预期 rc=5 / $frac / 红项 [$want]）────────" \
      | tee -a "$OUT/rc-summary.txt"
    ok=0
    for i in $(seq 1 $NEG_RUNS); do
      run_suite "$OUT/$base-run$i.txt" env "$envvar" STELQUICK_ERROR_CHECK=1 "$BIN"
      rc=$?
      line=$(judge_line "$OUT/$base-run$i.txt"); verd=$(verdict_line "$OUT/$base-run$i.txt")
      reds=$(red_ids "$OUT/$base-run$i.txt")
      echo "  · run$i rc=$rc（$verd；红项 [$reds]）" | tee -a "$OUT/rc-summary.txt"
      if [[ $rc -eq 5 && "$line" == *"判据 $frac"* && "$reds" == "$want" ]]; then
        (( ok++ ))
      else
        judge 1 "负控 $name run$i 红项不是 [$want]（rc=$rc $line $reds）——负控必须命中本域承重项"
      fi
    done
    judge $([[ $ok -eq $NEG_RUNS ]] && echo 0 || echo 1) \
      "负控 $name $ok/$NEG_RUNS 命中 [$want]"
    echo "  负控 $name 的红是**预期**结果；读数：" | tee -a "$OUT/rc-summary.txt"
    /usr/bin/grep -E "^ERRORCHECK: ?\[FAIL\]|VERDICT" "$OUT/$base-run1.txt" | sed 's/^/      /' \
      | tee -a "$OUT/rc-summary.txt"
  done

  # ══ ④ 正交定位（A∩B=∅、A∩C=∅；B∩C={EC-11} 且子段可分）════════════════════
  NEG_A_REDS=$(red_ids "$OUT/negctl-takeoveroff-run1.txt")
  NEG_B_REDS=$(red_ids "$OUT/negctl-pathsoff-run1.txt")
  NEG_C_REDS=$(red_ids "$OUT/negctl-unsupportedoff-run1.txt")
  ab=$(set_inter "$NEG_A_REDS" "$NEG_B_REDS")
  ac=$(set_inter "$NEG_A_REDS" "$NEG_C_REDS")
  bc=$(set_inter "$NEG_B_REDS" "$NEG_C_REDS")
  echo "──────── 三腿正交定位（A∩B=[$ab] 应空｜A∩C=[$ac] 应空｜B∩C=[$bc] 应恰 EC-11）────────" \
    | tee -a "$OUT/rc-summary.txt"
  judge $([[ -z "$ab" && -z "$ac" && "$bc" == "EC-11" ]] && echo 0 || echo 1) \
    "三腿正交定位成立（接管域 / 路径域 / 清单域；EC-11 为共享 UI 行判据，子段可分）"
  judge $([[ -z "$POS_REDS" ]] && echo 0 || echo 1) \
    "正题红项空集（[${POS_REDS:-∅}]）"
fi

# ══ ⑤ config 零污染门 ════════════════════════════════════════════════════════
CFG_MD5_POST=$(md5 -q "$CFG" 2>/dev/null || echo "(missing)")
if [[ "$CFG_MD5_POST" == "$CFG_MD5_PRE" ]]; then
  judge 0 "config 零污染：全程 md5 一致（$CFG_MD5_POST）"
else
  judge 1 "config 被写！md5 $CFG_MD5_PRE → $CFG_MD5_POST（判据自净失效？）"
fi

# ══ ⑥ 相邻回归（十七套件 + INTERACTCHECK + A2 + DYN 双路 + S3）══════════════
# T42 改动面：main.cpp（装配 + liveEngine 失败通道）、LiveSkyRuntime.cpp（boot 预检）、
# ErrorModel/ErrorPage（新）、MainWindow.qml（+error 页 + 未支持对话框）、
# Toolbar.qml（+navStatusButton）⇒ toolbarcheck / configcheck（T36 落点相邻）
# 最近邻，其余为全量回归。
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
    "STELQUICK_CONFIG_CHECK=1:configcheck" \
    "STELQUICK_NIGHT_CHECK=1:nightcheck" \
    "STELQUICK_DISPLAY_CHECK=1:displaycheck" \
    "STELQUICK_HIDPI_CHECK=1:hidpicheck" \
    "STELQUICK_SHORTCUT_CHECK=1:shortcutcheck" \
    "STELQUICK_HELP_CHECK=1:helpcheck"
  do
    var=${spec%%:*}; name=${spec##*:}
    run_suite "$OUT/regression-$name.txt" env $var "$BIN"
    rc=$?
    echo "regression-$name rc=$rc" | tee -a "$OUT/rc-summary.txt"
    [[ $rc -eq 0 ]] || FAILED=1
  done

  # INTERACTCHECK：ENV-SKIP 通道照抄 t36..t41
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
