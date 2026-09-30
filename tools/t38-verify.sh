#!/bin/zsh
# T38 验证脚本（2026-09-30）：**显示参数（亮度/星等 · 视场 · 投影）**。
#
# 用法：tools/t38-verify.sh              （跑全部）
#       tools/t38-verify.sh pos 3        （只跑正题 DISPLAYCHECK ×3）
#       tools/t38-verify.sh negctl       （只跑两组负控 ×2）
#       tools/t38-verify.sh regress      （只跑相邻回归）
#
# 证据落 docs/evidence/2026-09-30-t38-display/mac/。
# 环境口径（Metal + MoltenVK）**刻意与 t17..t37 完全一致，不换**。
#
# ── A-1.0 范围表的这一格 ────────────────────────────────────────────────────
# `|亮度/星等、视场、投影、主题/夜视、高 DPI|基础|必须|天空夜视效果仍由旧后处理负责，
#   避免叠加两次|` —— T37 做掉"主题/夜视"（结论：落点改 Qt Quick 侧效果层），
#   T38 做前三项（高 DPI 归 T39）。
#
# ── 这一轮要定的"性"（先探针、再产品、后判据）────────────────────────────────
# T38-A 探针（STELQUICK_DISPLAY_PROBE）实测坐实三个陷阱 + 一个口径骗局：
#   ① `StelSkyDrawer` 的四个数值 setter **一个都不夹取**（写 4/3/5/12.5 原样落库，
#      还顺手 immediateSave 落盘）⇒ **范围闸必须在产品侧**（AppFacade）；
#   ② `StelMovementMgr::setFov` **自带 qBound 夹取**，而 **maxFov 是投影的函数**
#      （Perspective 120 / Stereographic 235 / Fisheye 360 / EqualArea 360 /
#       Mercator·Miller 270 / Orthographic·CylinderFill 180 / 其余 185）
#      ⇒ `maxFieldOfView` 必须是**活属性**，滑块上限跟着投影走；
#   ③ `setCurrentProjectionTypeKey("乱码")` **不报错**，静默落到 ProjectionStereographic
#      **并落盘** ⇒ 必须白名单闸；
#   ④ 🔴 `getLimitMagnitude()` **不是**用户设定值，是引擎按大气/光污染/瞳孔适应算出的
#      **有效限制**（白天实测 -4.44，与 customStarMagLimit=12.5 无关）⇒ 判据只读写
#      `getCustomStarMagnitudeLimit()` + `getFlagStarMagnitudeLimit()`
#      （读错量的名字 = 结论必错）；
#   ⑤ 星等/亮度的**视觉**效应只在星星可见时存在 ⇒ 判据布场必须**大气置关**；
#   ⑥ 静置 1.5s 内 rel/fov/proj 的 NOTIFY 增量 0/0/0 ⇒ 这些量**不是**每帧变的连续量
#      ⇒ 滑块**可以安全绑定**。
#
# ── 首轮 FAIL 反哺（三条都不是"调参"能修的）────────────────────────────────
#   ① DP-06 **0/12 —— 真产品缺陷**：`AppFacade::projectionTypeKeys()` 原本是
#      `Q_INVOKABLE`，而合流形态**先 engine.load()（QML 起来）、后 boot()（引擎起来）**
#      ⇒ QML 里 `model: appFacade.projectionTypeKeys()` 这种**函数式绑定不读任何属性**
#      ⇒ 没有依赖 ⇒ 只求值一次、那次拿到空表 ⇒ **12 个投影按钮一个都不出现且永不
#      自愈**（真机同路径同样空）⇒ 已改 `Q_PROPERTY + NOTIFY`，并在
#      `ensureDisplayParamsForwarding()` 末尾补一次"开机唤醒"emit；
#   ② DP-11 **基准被污染**：数据面写过的 absoluteStarScale / lightPollutionLuminance /
#      flag+customMag **没还原**就去抓"参考帧"（实测 nonBlack 被光污染顶到 1.0000）
#      ⇒ DP-09/10/11 全比在假基准上 ⇒ 已加 `Ctx::restoreAllDisplayParams()`；
#   ③ DP-08 红且**逐位相同** —— **延迟挂错步**：`delayAfter` 的语义是"跑完**本步**
#      之后等"（陷阱 41/69），首轮挂到了**读步** ⇒ 写后 0ms 抓帧 ⇒ 拿到上一次的
#      帧缓冲（哈希都相同）⇒ 已挪到写步。
#   另：第二轮修正**噪声容差口径**——冻结下亚 LSB 抖动**面积会随场景状态变、幅度不会**
#      （DP-00 实测 0.079%/Δ1，"对照量还原核对"实测 0.557%/Δ2）⇒「低于噪声容差」只能拿
#      **幅度**当判据（`maxDelta ≤ 2`）；面积只留给 DP-00 当"帧静没静下来"的粗门。
#
# ── 本脚本的判据分工（**内外两层，别互相顶替**）──────────────────────────────
#   内层（被测自检 DISPLAYCHECK，14 条）：见 app/DisplayCheck.hpp。
#     数据面 DP-00..DP-05 ｜ UI 腿 DP-06..DP-07 ｜ 帧级 DP-08/08b/09/10a/10b/11
#   外层（本脚本，**换来源的对照量**，陷阱 43）：
#     · 正题 ×3 全 PASS（rc=0 / 判据 14/14 / 红项空）；
#     · 负控 A `STELQUICK_DISPLAY_GATE_OFF=1`（关范围闸 + 白名单闸）
#       ⇒ **恰好**红 [DP-02,DP-04]（rc=10 / 12/14）—— 且读数自带证据：
#          "写 99 ⇒ 引擎收到 99"、"乱码 ⇒ 引擎 key 真的被兜底改成 Stereographic"；
#     · 负控 B `STELQUICK_DISPLAY_FWD_OFF=1`（断引擎→façade **订阅**）
#       ⇒ **恰好**红 [DP-07]（rc=10 / 13/14）—— 滑块停在开机唤醒那一刻的值（1）。
#         ⚠️ B **刻意不拦**开机唤醒那次 emit：拦了会连 DP-06 一起打红，
#            "恰好红 [DP-07]"这个口径就没了（两个负控各管一件事）；
#     · 三组红项集合**两两不同**：∅ / {DP-02,DP-04} / {DP-07}。
#
# ── 环境门（沿用 t34..t37 的血泪，写进脚本而不是靠人记）──────────────────────
#   · 显示器休眠门照抄 t34..t37；
#   · **Spotlight 索引（mdbulkimport）= 负载毒源**：高负载下出撕裂帧（T37-X2）、
#     噪声底失效、引导门挂死（T37-X3）。检测到就打环境注记，**不许洗 PASS**；
#   · 每个 app 运行都带**看门狗**：引导门挂死时强杀，rc=124 计失败。
set -u
cd /Users/ztuqfvy/qt_demo/stellarium_vulkan

export VK_DRIVER_FILES=/opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json
export QT_VULKAN_LIB=/opt/homebrew/opt/vulkan-loader/lib/libvulkan.1.dylib
export STELQUICK_GRAPHICS_API=metal

BIN=./build-release/src/ui/stelQuickUI.app/Contents/MacOS/stelQuickUI
ROOT=docs/evidence/2026-09-30-t38-display
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

# ── 显示器休眠门（照抄 t34..t37）────────────────────────────────────────────
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
judge_line() { /usr/bin/grep -E "DISPLAYCHECK: .*判据 [0-9]+/[0-9]+" "$1" | LC_ALL=C /usr/bin/tail -1; }
verdict_line() { /usr/bin/grep -E "DISPLAYCHECK: VERDICT=" "$1" | LC_ALL=C /usr/bin/tail -1; }
red_ids() {
  /usr/bin/grep -E "DISPLAYCHECK: +\[FAIL\] " "$1" \
    | /usr/bin/sed -E 's/.*\[FAIL\] (DP-[0-9A-Za-z]+).*/\1/' \
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

# ══ ① 正题：DISPLAYCHECK ×3（全绿才算数）═════════════════════════════════════
if [[ "$TARGET" == "all" || "$TARGET" == "pos" ]]; then
  echo "──────── 正题 DISPLAYCHECK ×$POS_RUNS（预期 rc=0 / 判据 14/14 / 红项空）────────" \
    | tee -a "$OUT/rc-summary.txt"
  local_ok=0
  for i in $(seq 1 $POS_RUNS); do
    run_suite "$OUT/displaycheck-run$i.txt" env STELQUICK_DISPLAY_CHECK=1 "$BIN"
    rc=$?
    line=$(judge_line "$OUT/displaycheck-run$i.txt"); verd=$(verdict_line "$OUT/displaycheck-run$i.txt")
    reds=$(red_ids "$OUT/displaycheck-run$i.txt")
    echo "  · run$i rc=$rc（$verd；红项 [$reds]）" | tee -a "$OUT/rc-summary.txt"
    if [[ $rc -eq 0 && "$verd" == *"VERDICT=PASS"* && "$line" == *"判据 14/14"* && -z "$reds" ]]; then
      (( local_ok++ ))
    else
      judge 1 "正题 run$i 不绿（rc=$rc $verd 红项 [$reds]）——ENV 劣化不许洗 PASS，查明再跑"
    fi
  done
  judge $([[ $local_ok -eq $POS_RUNS ]] && echo 0 || echo 1) \
    "正题 $local_ok/$POS_RUNS 全绿（判据 14/14 × $POS_RUNS）"
  echo "  · run1 判据全文（见 $OUT/displaycheck-run1.txt）：" | tee -a "$OUT/rc-summary.txt"
  /usr/bin/grep -E "^DISPLAYCHECK: " "$OUT/displaycheck-run1.txt" | sed 's/^/      /' \
    | tee -a "$OUT/rc-summary.txt"
fi

# ══ ② 负控 A：关范围闸 + 白名单闸 ⇒ 恰好红 [DP-02,DP-04] ×2 ═════════════════
if [[ "$TARGET" == "all" || "$TARGET" == "negctl" ]]; then
  echo "──────── 负控 A STELQUICK_DISPLAY_GATE_OFF=1 ×$NEG_RUNS（预期 rc=10 / 12/14 / 红项 [DP-02,DP-04]）────────" \
    | tee -a "$OUT/rc-summary.txt"
  nega_ok=0
  for i in $(seq 1 $NEG_RUNS); do
    run_suite "$OUT/negctl-gateoff-run$i.txt" env STELQUICK_DISPLAY_GATE_OFF=1 \
      STELQUICK_DISPLAY_CHECK=1 "$BIN"
    rc=$?
    line=$(judge_line "$OUT/negctl-gateoff-run$i.txt"); verd=$(verdict_line "$OUT/negctl-gateoff-run$i.txt")
    reds=$(red_ids "$OUT/negctl-gateoff-run$i.txt")
    echo "  · run$i rc=$rc（$verd；红项 [$reds]）" | tee -a "$OUT/rc-summary.txt"
    if [[ $rc -eq 10 && "$line" == *"判据 12/14"* && "$reds" == "DP-02,DP-04" ]]; then
      (( nega_ok++ ))
    else
      judge 1 "负控 A run$i 红项不是恰好 [DP-02,DP-04]（rc=$rc $line $reds）——负控必须命中且只命中承重项"
    fi
  done
  judge $([[ $nega_ok -eq $NEG_RUNS ]] && echo 0 || echo 1) \
    "负控 A $nega_ok/$NEG_RUNS 命中恰好 [DP-02,DP-04]（范围闸 + 白名单闸承重证明）"
  echo "  负控 A 的红是**预期**结果（闸门被关掉 ⇒ 闸门判据必须红）；读数自带证据：" \
    | tee -a "$OUT/rc-summary.txt"
  /usr/bin/grep -E "^DISPLAYCHECK: +\[FAIL\]|VERDICT" "$OUT/negctl-gateoff-run1.txt" | sed 's/^/      /' \
    | tee -a "$OUT/rc-summary.txt"

  # ══ ③ 负控 B：断引擎→façade 订阅 ⇒ 恰好红 [DP-07] ×2 ═══════════════════════
  echo "──────── 负控 B STELQUICK_DISPLAY_FWD_OFF=1 ×$NEG_RUNS（预期 rc=10 / 13/14 / 红项 [DP-07]）────────" \
    | tee -a "$OUT/rc-summary.txt"
  negb_ok=0
  for i in $(seq 1 $NEG_RUNS); do
    run_suite "$OUT/negctl-fwdoff-run$i.txt" env STELQUICK_DISPLAY_FWD_OFF=1 \
      STELQUICK_DISPLAY_CHECK=1 "$BIN"
    rc=$?
    line=$(judge_line "$OUT/negctl-fwdoff-run$i.txt"); verd=$(verdict_line "$OUT/negctl-fwdoff-run$i.txt")
    reds=$(red_ids "$OUT/negctl-fwdoff-run$i.txt")
    echo "  · run$i rc=$rc（$verd；红项 [$reds]）" | tee -a "$OUT/rc-summary.txt"
    if [[ $rc -eq 10 && "$line" == *"判据 13/14"* && "$reds" == "DP-07" ]]; then
      (( negb_ok++ ))
    else
      judge 1 "负控 B run$i 红项不是恰好 [DP-07]（rc=$rc $line $reds）——负控必须命中且只命中承重项"
    fi
  done
  judge $([[ $negb_ok -eq $NEG_RUNS ]] && echo 0 || echo 1) \
    "负控 B $negb_ok/$NEG_RUNS 命中恰好 [DP-07]（绑定腿承重证明）"
  echo "  负控 B 的红是**预期**结果（订阅被断 ⇒ 引擎侧改动刷不到 UI）；读数：" \
    | tee -a "$OUT/rc-summary.txt"
  /usr/bin/grep -E "^DISPLAYCHECK: +\[FAIL\]|VERDICT" "$OUT/negctl-fwdoff-run1.txt" | sed 's/^/      /' \
    | tee -a "$OUT/rc-summary.txt"
fi

# ══ ④ 相邻回归（十四套件 + INTERACTCHECK + A2 + DYN 双路 + S3）══════════════
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
    "STELQUICK_NIGHT_CHECK=1:nightcheck"
  do
    var=${spec%%:*}; name=${spec##*:}
    run_suite "$OUT/regression-$name.txt" env $var "$BIN"
    rc=$?
    echo "regression-$name rc=$rc" | tee -a "$OUT/rc-summary.txt"
    [[ $rc -eq 0 ]] || FAILED=1
  done

  # INTERACTCHECK：依赖窗口真的拿到系统焦点，ENV-SKIP 通道照抄 t36/t37
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
