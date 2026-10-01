#!/bin/zsh
# Q-WIN-01..06 窗口交互回归（测试文档 §6.4「窗口交互回归，A1 起生效」）
#
# ── 为什么有这个脚本（#159）─────────────────────────────────────────────────
#   六条判据的**源码从 A1 起就在**：
#     src/ui/main.cpp  STELQUICK_WINDOW_TEST=1       （WINDOWTEST：放大/缩小/隐藏显示）
#                      STELQUICK_AUTOTEST_SECONDS=N  （N 秒后自动退出）
#                      STELQUICK_RENDER_WORKAROUND=… （none/transaction-off/basic-loop）
#   A1 阶段手工跑过并 PASS（计划文档记 maxStall 52ms / maxResize 46ms），
#   但 `grep -rln WINDOW_TEST tools/` **零命中** —— 判据写了没人执行。
#   本脚本把它装回回归：判据不再靠"当时跑过一次"活着。
#
# ── 后端选择：Vulkan（验收形态），**不是** A6 其它回归用的 Metal ──────────────
#   ① 测试文档 §6.4 是 A1 验收项，A1 的验收形态就是 Vulkan；
#   ② Q-WIN-06 判的是 **MoltenVK 路径**的 Metal 图层锁 workaround ——
#      `applyMacOsVulkanWorkaround()` 整个函数体在 `#ifdef Q_OS_MACOS` 内，做的是
#      `QT_MTL_NO_TRANSACTION=1` / `QSG_RENDER_LOOP=basic`。Metal 后端下这两个
#      开关**根本不参与**，三档全 PASS，测的是空气。
#   **判别对照实测（2026-10-01，二进制 md5 4cf585dd）**：
#     后端 = Metal （STELQUICK_GRAPHICS_API=metal）：
#         none            rc=0  wall=5.1s  VERDICT=PASS maxStall=52  ← 假绿
#         transaction-off rc=0  wall=4.0s  VERDICT=PASS
#         basic-loop      rc=0  wall=4.0s  VERDICT=PASS
#     后端 = Vulkan（不设该变量）：
#         none            rc=124 wall=121.0s  **看门狗强杀、零 WINDOWTEST 结果行** ← 原始故障复现
#         transaction-off rc=0  wall=4.1s  VERDICT=PASS
#         basic-loop      rc=0  wall=4.0s  VERDICT=PASS
#   ⇒ 只有 Vulkan 后端能让 `none` 档复现故障。本脚本因此**不设**该变量。
#
# ── Q-WIN-06 的「必须 FAIL」是什么形态 ───────────────────────────────────────
#   不是判据行变红，而是**渲染循环卡死**（原始故障 = 每帧 present 阻塞 5s）
#   ⇒ rc=124（看门狗强杀）+ **零 WINDOWTEST 输出行**。
#   「零判据输出 ≠ 判据失败」（陷阱 50 同族），所以 QW-06 判的是
#   「有没有复现故障」，不是 rc；三条腿的期望是 **相反方向**的，必须分开判。
#
# ── 诚实性字段 ───────────────────────────────────────────────────────────────
#   QW-03 的文本是"恢复后画面正确，无停滞"。**"画面正确"没有客观量** ——
#   源码只测 hide/show 的阻塞时长。本脚本如实只判"三阶段序列跑完 + 无停滞"，
#   并把"画面正确性未验"作为 reading 打印，不编近似量充数。
#
# 用法：tools/qwin-check.sh             （正题：QW-01..06）
#       tools/qwin-check.sh negctl      （负控：证 QW-06 三档的判别力）
set -u
cd /Users/ztuqfvy/qt_demo/stellarium_vulkan

export VK_DRIVER_FILES=/opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json
export QT_VULKAN_LIB=/opt/homebrew/opt/vulkan-loader/lib/libvulkan.1.dylib
# 刻意 unset：默认即 Vulkan 验收形态（见头注）。若继承 a6-verify.sh 的
# STELQUICK_GRAPHICS_API=metal，QW-06 会退化成假绿。
unset STELQUICK_GRAPHICS_API

BIN=./build-release/src/ui/stelQuickUI.app/Contents/MacOS/stelQuickUI
OUT=${QW_OUT:-docs/evidence/2026-10-01-a6-regress/mac}
THRESHOLD=250
WATCH=60            # 正常档 ~4s；none 档无限卡死 ⇒ 60s 的两档差距足以判别
MODE=${1:-pos}

mkdir -p "$OUT"
SUM="$OUT/qwin-summary.txt"
[[ "$MODE" == "negctl" ]] && SUM="$OUT/qwin-negctl-summary.txt"

PASS=0
FAIL=0
: > "$SUM"

say()  { echo "$1" | tee -a "$SUM"; }
judge() {
  if [[ "$1" -eq 0 ]]; then
    (( PASS++ )); say "  ${CHECK_MARK} $2"
  else
    (( FAIL++ )); say "  ${CROSS_MARK} $2"
  fi
}
CHECK_MARK=$'\u2713'
CROSS_MARK=$'\u2717'

# 跑一次 GUI 自检：$1=日志，其余参数=命令行。**echo** 出 rc（124 = 看门狗强杀）。
# ⚠️ 必须是 echo 而不是 return：调用方写成 rc=$(run_gui ...)，而 $() 捕获的是
#    stdout，不是返回码 —— 用 return 会静默拿到空串，于是 `rc != 0` 恒真、
#    正题全红或全绿都看不出来。这是"仪器自身的静默失效"，不是风格问题。
run_gui() {
  local log=$1
  shift
  "$@" > "$log" 2>&1 &
  local pid=$!
  local waited=0
  while (( waited < WATCH )); do
    kill -0 $pid 2>/dev/null || break
    sleep 1
    (( waited += 1 ))
  done
  if kill -0 $pid 2>/dev/null; then
    kill -9 $pid 2>/dev/null
    wait $pid 2>/dev/null
    echo 124
    return
  fi
  wait $pid
  echo $?
}

# ── 环境门（陷阱 93：全屏遮挡 ⇒ 场景图停摆 ⇒ WINDOWTEST 永不预热完 ──────────
#    ⇒ 症状是"卡死"，与 QW-06 的 none 档**同形**。必须先查，否则假红/假绿都可能）
say "Q-WIN 窗口交互回归（Vulkan 验收形态）  $(date '+%Y-%m-%d %H:%M:%S')"
say "二进制：$(ls -l "$BIN" | awk '{print $5" B"}')  md5=$(md5 -q "$BIN")"
shot=/tmp/qwin-preflight-$$.png
screencapture -x "$shot" 2>/dev/null && say "环境门：预检截图 $shot（异常时人工查看全屏遮挡 —— 见陷阱 93）"
idle=$(defaults -currentHost read com.apple.screensaver idleTime 2>/dev/null || echo "(unset)")
say "环境门：屏保 idleTime=${idle}｜负载 $(uptime | sed 's/^.*load averages: //')"
[[ "$MODE" == "pos" ]] && say ""

# ══ 正题 ════════════════════════════════════════════════════════════════════
if [[ "$MODE" == "pos" ]]; then

  # ── QW-01/02/03/04：一次 WINDOWTEST 给出三个量与三个阶段 ──────────────────
  WLOG="$OUT/qwin-windowtest.txt"
  rc=$(run_gui "$WLOG" env STELQUICK_WINDOW_TEST=1 "$BIN")
  resline=$(/usr/bin/grep -E "WINDOWTEST: 结果" "$WLOG" | /usr/bin/tail -1)

  if [[ -z "$resline" ]]; then
    say "  ${CROSS_MARK} QW-01..04 无 WINDOWTEST 结果行（rc=${rc}）——"
    say "          先查全屏遮挡/屏保（陷阱 93）再看是否是产品缺陷；本项记 FAIL"
    (( FAIL += 4 ))
  else
    say "  读数：$resline"
    stall=$(echo "$resline"    | /usr/bin/sed -nE 's/.*maxStallMs=([0-9]+).*/\1/p')
    resize=$(echo "$resline"   | /usr/bin/sed -nE 's/.*maxResizeMs=([0-9]+).*/\1/p')
    gap=$(echo "$resline"      | /usr/bin/sed -nE 's/.*maxFrameGapMs=([0-9]+).*/\1/p')
    verdict=$(echo "$resline"  | /usr/bin/sed -nE 's/.*VERDICT=([A-Z]+).*/\1/p')

    # 三阶段齐全（QW-03 的前置：hide/show 那一相真的跑到了）
    ph1=$(/usr/bin/grep -cE "阶段 1 完成" "$WLOG" || true)
    ph2=$(/usr/bin/grep -cE "阶段 2 完成" "$WLOG" || true)

    judge $(( rc != 0 ))                       "QW-01 放大 24 步：rc=${rc}（须 0）"
    judge $(( stall > THRESHOLD ))             "QW-02 缩小 24 步：maxStallMs=${stall} maxResizeMs=${resize}（门 ${THRESHOLD}ms）"
    judge $(( ph1 < 1 || ph2 < 1 || resize > THRESHOLD )) \
                                               "QW-03 隐藏/显示 ×3：阶段 1/2 完成=${ph1}/${ph2}，maxResizeMs=${resize}（门 ${THRESHOLD}ms）"
    say "          ⚠ 诚实性：源码未提供「画面正确」的客观量，本项只判序列跑完 + 无停滞"
    judge $(( gap > THRESHOLD ))               "QW-04 帧间隔连续：maxFrameGapMs=${gap}（门 ${THRESHOLD}ms）｜VERDICT=${verdict}"
  fi
  say ""

  # ── QW-05：启停开销 ──────────────────────────────────────────────────────
  Q5LOG="$OUT/qwin-autotest.txt"
  t0=$(perl -MTime::HiRes=time -e 'printf "%.3f", time')
  env STELQUICK_AUTOTEST_SECONDS=5 "$BIN" > "$Q5LOG" 2>&1
  rc5=$?
  t1=$(perl -MTime::HiRes=time -e 'printf "%.3f", time')
  wall=$(perl -e "printf '%.2f', ${t1}-${t0}")
  # 预算 5..7s：判的是"自动退出真的按 5s 生效"，不是"这台机器快"
  judge $(( rc5 != 0 || ${wall%.*} < 5 || ${wall%.*} > 7 )) \
        "QW-05 启停开销：rc=${rc5} wall=${wall}s（5s 目标，额外开销须 ≤1s）"
  say ""

  # ── QW-06：渲染循环三档（**负控式设计**：none 必须复现故障）───────────────
  # ⚠️ 实测（2026-10-01，两轮逐位一致）与测试文档 §6.4 的口径**不一致**：
  #    文档写「none 必须 FAIL / transaction-off PASS / basic-loop PASS」，
  #    实测基本-loop 与 none **同样卡死**（各 2 轮 90~121s 看门狗强杀、零结果行）。
  #    basic-loop 这条毛病早有记录，文档间本就自相矛盾：
  #      docs/BUILD_RECORD.zh_CN.md:227   「basic-loop + A2_CHECK：校验序列不退出」
  #      docs/BUILD_RECORD.zh_CN.md:157   「basic-loop 备选…被长跑看门狗打断，未纳入默认」
  #      docs/WINDOWS_BUILD.zh_CN.md:308  「basic-loop ❌禁用：与 A2 校验不兼容（序列不退出）」
  #    ⇒ 结论：**测试文档 §6.4 的口径过期**（把 basic-loop 当成了可用逃生门），
  #      不是产品缺陷 —— 默认档 transaction-off 工作正常，产品只用它。
  #    本脚本按**实测**判，并把这条矛盾记成 FINDING，不按文档洗绿。
  say "  QW-06 渲染循环三档（Vulkan 后端；见头注的文档口径矛盾）："
  ok_all=0

  # 腿 1：none 必须复现故障 —— 这是「workaround 必需」的证伪腿
  NLOG="$OUT/qwin06-none.txt"
  rcn=$(run_gui "$NLOG" env STELQUICK_RENDER_WORKAROUND=none STELQUICK_WINDOW_TEST=1 "$BIN")
  nres=$(/usr/bin/grep -cE "WINDOWTEST: 结果" "$NLOG" || true)
  if [[ $rcn -eq 124 && $nres -eq 0 ]]; then
    say "    ${CHECK_MARK} none             rc=124 看门狗强杀 ∧ 零结果行 ⇒ 原始故障**复现**（每帧 present 阻塞）"
  else
    say "    ${CROSS_MARK} none             rc=${rcn} 结果行=${nres} ⇒ **未复现故障**：关掉 workaround 却没出事"
    say "                 ⇒ QT_MTL_NO_TRANSACTION 可能已非必需（Qt 上游修了？）—— QW-06 口径过期，须重新定性"
    ok_all=1
  fi

  # 腿 2：transaction-off（默认档，产品实际使用的配置）必须 PASS
  TLOG="$OUT/qwin06-transaction-off.txt"
  rct=$(run_gui "$TLOG" env STELQUICK_RENDER_WORKAROUND=transaction-off STELQUICK_WINDOW_TEST=1 "$BIN")
  tres=$(/usr/bin/grep -E "WINDOWTEST: 结果" "$TLOG" | /usr/bin/tail -1)
  tv=$(echo "$tres" | /usr/bin/sed -nE 's/.*VERDICT=([A-Z]+).*/\1/p')
  if [[ $rct -eq 0 && "$tv" == "PASS" ]]; then
    say "    ${CHECK_MARK} transaction-off  rc=0 $(echo "$tres" | /usr/bin/sed -nE 's/.*(maxStallMs=[0-9]+ maxResizeMs=[0-9]+ maxFrameGapMs=[0-9]+).*/\1/p')  VERDICT=PASS（默认档）"
  else
    say "    ${CROSS_MARK} transaction-off  rc=${rct} VERDICT=${tv:-NONE} ⇒ **默认档未通过 = 产品缺陷**"
    ok_all=1
  fi

  # 腿 3：basic-loop 如实记录（**不设期望 = 不洗绿**），跑两轮证可复现（陷阱 87）
  for i in 1 2; do
    BLOG="$OUT/qwin06-basic-loop-run$i.txt"
    eval "rcb${i}=\$(run_gui \"\$BLOG\" env STELQUICK_RENDER_WORKAROUND=basic-loop STELQUICK_WINDOW_TEST=1 \"\$BIN\")"
    eval "bres${i}=\$(/usr/bin/grep -cE 'WINDOWTEST: 结果' \"\$BLOG\" || true)"
  done
  say "    READ basic-loop       rc=${rcb1}/${rcb2} 结果行=${bres1}/${bres2}"
  say "             测试文档 §6.4 期望 PASS；实测卡死 ⇒ **文档口径过期**（FINDING，非产品缺陷）"
  say "             依据：BUILD_RECORD:157/227 与 WINDOWS_BUILD:308 早有「不兼容 / ❌禁用」记录"
  if [[ $rcb1 -eq $rcb2 && $bres1 -eq $bres2 ]]; then
    say "    ${CHECK_MARK} basic-loop 两轮逐位一致（卡死形态稳定）"
  else
    say "    ${CROSS_MARK} basic-loop 两轮不一致（rc=${rcb1}/${rcb2}，结果行=${bres1}/${bres2}）⇒ 不可复现，须再跑"
    ok_all=1
  fi

  judge $ok_all "QW-06 三档（none 复现故障 ∧ transaction-off PASS ∧ basic-loop 两轮一致）"
fi

# ══ 负控 ════════════════════════════════════════════════════════════════════
# 证 QW-06 的判别力**靠后端选择**承重：同一套三档在 Metal 后端下
# `none` 也应 PASS（即判据在这个后端上**没有判别力**）。若它在 Metal 下
# 反而红了，说明承重来源不是后端 —— 头注的归因就是错的。
if [[ "$MODE" == "negctl" ]]; then
  say "负控：三档 × Metal 后端（预期 none 也 PASS ⇒ 证「必须 Vulkan」这条归因）"
  nlog="$OUT/qwin-negctl-metal-none.txt"
  rcn=$(run_gui "$nlog" env STELQUICK_GRAPHICS_API=metal STELQUICK_RENDER_WORKAROUND=none STELQUICK_WINDOW_TEST=1 "$BIN")
  nv=$(/usr/bin/grep -oE "VERDICT=[A-Z]+" "$nlog" | /usr/bin/tail -1)
  # 期望：Metal 下 none 档 rc=0 且 VERDICT=PASS —— 与 Vulkan 下的卡死成对
  # ⚠️ 字符串比较**不能**写进 $(( ))：算术上下文里 "$nv" 会展开成裸词
  #    `VERDICT=PASS`，zsh 把 VERDICT 当变量名 ⇒ set -u 直接报
  #    "VERDICT: parameter not set"（实测 2026-10-01，本脚本首版就栽在这）。
  neg_ok=0
  [[ $rcn -eq 0 && "$nv" == "VERDICT=PASS" ]] || neg_ok=1
  judge $neg_ok "NEG 后端判别：Metal + none ⇒ rc=${rcn} ${nv}（期望 PASS ⇒ 证明该 workaround 在 Metal 路径上不参与）"
fi

say ""
say "──────── Q-WIN 判据 $( [[ "$MODE" == "negctl" ]] && echo "（负控）" )${PASS}/$(( PASS + FAIL )) ────────"
if [[ $FAIL -eq 0 ]]; then
  say "QWIN VERDICT=PASS"
  exit 0
else
  say "QWIN VERDICT=FAIL（红 ${FAIL}）"
  exit 10
fi
