#!/bin/zsh
# P-LIF-01 的 ×N 驱动：进程级「启动 → 首帧上屏 → 退出」反复跑。
#
# 用法：tools/a6-lifecycle-100.sh [N]        # 默认 100
#       WATCHDOG_SEC=60 tools/a6-lifecycle-100.sh 20
#
# 出处：测试文档 §6.2 `P-LIF-01 | 关闭/重开循环 ×100 | 无崩溃、无死锁、无悬空纹理`。
#
# ── 这条判据"诚实地"能测到什么（先读，别把平凡真当成绩）─────────────────────
#   ✔ 进程级 ×N 启停：**无崩溃**（rc 恒 0）、**无死锁**（每轮都在看门狗内结束）、
#     首帧确实每轮都上屏（`displayedFrameNumber > 0`）。
#   ✘ "**无悬空纹理**"在进程级**是平凡真** —— 进程退出时内核回收全部资源，
#     卡不卡纹理都看不出来。那半句归 LF-03（进程内尺寸/DPR 重建 ×100，验内存不
#     无界增长），本条**不重复主张**（陷阱 22：判据不许搭便车）。
#   ✘ 本腿用**替身生产者**（P-LIF-01 要 ×100，引擎真实预热 ~900s ⇒ 物理不可行）
#     ⇒ **不含**引擎 GL 资源面。完整诚实性声明见 docs/T44_LIFECYCLE.zh_CN.md §2.2。
#
# ── 两个环境陷阱 ───────────────────────────────────────────────────────────
#   · 陷阱 50：连续起停 Metal 可能掉设备 ⇒ `rc≠0` **先怀疑环境**，连跑几轮复现才算数；
#     零判据输出 ≠ 判据失败。
#   · 陷阱 94：只判不净会留残留 ⇒ 本脚本带 config 零污染门（前后 md5 一致）。
set -u
cd /Users/ztuqfvy/qt_demo/stellarium_vulkan

export VK_DRIVER_FILES=/opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json
export QT_VULKAN_LIB=/opt/homebrew/opt/vulkan-loader/lib/libvulkan.1.dylib
export STELQUICK_GRAPHICS_API=metal

BIN=./build-release/src/ui/stelQuickUI.app/Contents/MacOS/stelQuickUI
OUT=docs/evidence/2026-10-01-a6-lifecycle/mac
LOG=$OUT/lifecycle-N-runs.txt
TMPF=/tmp/a6-lifecycle-oneshot-$$.txt
N=${1:-100}
WATCHDOG_SEC=${WATCHDOG_SEC:-90}
mkdir -p "$OUT"

if [[ ! -x "$BIN" ]]; then echo "FAIL 二进制不存在：$BIN"; exit 2; fi

# 环境门：屏保必须关（冻结协议 §6.1.1 第 5 条；2026-09-23 因此废过一次跑）
idle=$(defaults -currentHost read com.apple.screensaver idleTime 2>/dev/null || echo "(unset)")
if pgrep -q ScreenSaverEngine; then
  echo "环境门：🔴 ScreenSaverEngine 在跑 —— 先退出屏保"
  exit 9
fi
echo "环境门：屏保 idleTime=${idle}｜二进制 md5=$(md5 -q "$BIN")"

# config 零污染门（陷阱 94）
CFG="$HOME/Library/Application Support/Stellarium-quick/config.ini"
CFG_PRE=$(md5 -q "$CFG" 2>/dev/null || echo "(missing)")

: > "$LOG"
echo "# P-LIF-01 ×${N}（替身生产者形态；每行 = 一轮）" >> "$LOG"
echo "# run rc verdict firstFrameMs wallMs" >> "$LOG"

declare -i ok=0 bad=0 to=0
T_ALL0=$(date +%s)
: > /tmp/a6-life-frames-$$.txt

for i in $(seq 1 $N); do
  # 秒级足够（每轮数秒）；刻意不用 python3（沙箱 PATH 不可靠）
  T0=$(date +%s)
  env STELQUICK_LIFECYCLE_ONESHOT=1 "$BIN" > "$TMPF" 2>&1 &
  pid=$! waited=0
  while (( waited < WATCHDOG_SEC )); do
    kill -0 $pid 2>/dev/null || break
    sleep 1; (( waited += 1 ))
  done
  if kill -0 $pid 2>/dev/null; then
    kill -9 $pid 2>/dev/null; wait $pid 2>/dev/null
    rc=124; (( to++ ))
  else
    wait $pid; rc=$?
  fi
  T1=$(date +%s)
  wall=$(( T1 - T0 ))
  ff=$(/usr/bin/grep -oE "firstFrameMs=-?[0-9]+" "$TMPF" | /usr/bin/tail -1 | /usr/bin/cut -d= -f2)
  vd=$(/usr/bin/grep -oE "VERDICT=[A-Z]+" "$TMPF" | /usr/bin/tail -1 | /usr/bin/cut -d= -f2)
  [[ -z "$ff" ]] && ff="(none)"
  [[ -z "$vd" ]] && vd="(none)"
  echo "$i $rc $vd $ff $wall" >> "$LOG"
  [[ "$ff" != "(none)" && "$ff" -gt 0 ]] && echo "$ff" >> /tmp/a6-life-frames-$$.txt
  if [[ $rc -eq 0 && "$vd" == "PASS" ]]; then (( ok++ )); else (( bad++ )); fi
  (( i % 10 == 0 )) && echo "  … $i/${N}（ok=$ok bad=$bad timeout=${to}）"
done

T_ALL1=$(date +%s)
TOTAL=$(( T_ALL1 - T_ALL0 ))

# ── 统计首帧延迟（用 sort -n 求中位数/p95；⚠️ awk 里别用 `exp`，那是内建名）──
if [[ -s /tmp/a6-life-frames-$$.txt ]]; then
  /usr/bin/sort -n /tmp/a6-life-frames-$$.txt > /tmp/a6-life-sorted-$$.txt
  cnt=$(/usr/bin/wc -l < /tmp/a6-life-sorted-$$.txt | /usr/bin/tr -d ' ')
  mn=$(/usr/bin/head -1 /tmp/a6-life-sorted-$$.txt)
  mx=$(/usr/bin/tail -1 /tmp/a6-life-sorted-$$.txt)
  med=$(/usr/bin/sed -n "$(( (cnt + 1) / 2 ))p" /tmp/a6-life-sorted-$$.txt)
  p95i=$(( (cnt * 95 + 99) / 100 )); (( p95i < 1 )) && p95i=1
  p95=$(/usr/bin/sed -n "${p95i}p" /tmp/a6-life-sorted-$$.txt)
  sum=$(/usr/bin/awk '{s+=$1} END{printf "%.0f", s}' /tmp/a6-life-sorted-$$.txt)
  mean=$(/usr/bin/awk '{s+=$1} END{printf "%.1f", s/NR}' /tmp/a6-life-sorted-$$.txt)
else
  cnt=0; mn="—"; mx="—"; med="—"; p95="—"; sum="—"; mean="—"
fi
rm -f /tmp/a6-life-frames-$$.txt /tmp/a6-life-sorted-$$.txt

CFG_POST=$(md5 -q "$CFG" 2>/dev/null || echo "(missing)")

{
  echo ""
  echo "════ P-LIF-01 结论（×${N}）════"
  echo "成功（rc=0 且 VERDICT=PASS）：$ok / $N"
  echo "失败：${bad}（其中看门狗超时 $to 轮 ⇒ 疑似死锁，见陷阱 50）"
  echo "首帧延迟（启动→首帧上屏，采样 $cnt 轮）：min=${mn}ms｜median=${med}ms｜p95=${p95}ms｜max=${mx}ms｜mean=${mean}ms"
  echo "总墙钟：${TOTAL}s（每轮均 $(( TOTAL / N ))s）"
  if [[ "$CFG_PRE" == "$CFG_POST" ]]; then
    echo "config 零污染门：✅ 前后一致（${CFG_POST}）"
  else
    echo "config 零污染门：🔴 被污染！pre=$CFG_PRE post=$CFG_POST"
  fi
  if [[ $ok -eq $N ]]; then
    echo "VERDICT=PASS（进程级 ×$N 无崩溃、无死锁；首帧每轮都上屏）"
    echo "⚠️ 本腿**不主张**「无悬空纹理」—— 那半句归 LF-03（进程内资源重建）"
  else
    echo "VERDICT=FAIL（$bad 轮未通过）"
  fi
} | /usr/bin/tee -a "$LOG"

rm -f "$TMPF"
[[ $ok -eq $N ]] && exit 0 || exit 1
