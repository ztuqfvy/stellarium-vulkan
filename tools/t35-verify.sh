#!/bin/zsh
# T35 验证脚本（2026-09-30）：**仿真时间链路定性**（T27 移交的"脱钩"线索结清）。
#
# 用法：tools/t35-verify.sh              （跑全部）
#       tools/t35-verify.sh core 5       （正题 ×5 + 三组负控 + 探针 + 相邻回归）
#       tools/t35-verify.sh negctl       （只跑三组负控 + 探针）
#       tools/t35-verify.sh regress      （只跑十套件 + INTERACTCHECK + S3 + A2 + DYN）
#
# 证据落 docs/evidence/2026-09-30-t35-timelink/mac/。
# 环境口径（Metal + MoltenVK）**刻意与 t17..t34 完全一致，不换**。
#
# ── 这一轮要定的"性" ─────────────────────────────────────────────────────────
# T27 留档（`docs/T27_MOUSE_NAV_UI_CHECK.zh_CN.md` §4 + 证据 README）：
#   「HostDriven 帧泵 400ms 推 0.61 天（≈1.5 天/秒），与 getTimeRate() 读数 10
#     脱钩，差 4 个数量级」—— 建议"若属设计需文档说明，否则单独立项"。
# 它悬了 T28..T34 六个任务，且**决定所有时间类判据的口径是否可信**。
#
# 本轮把它当成一件**测量问题**处理：三个量（ΔJD / 真实墙钟 / rate）必须在
# **同一时刻**量齐再下结论。读数（定稿轮见 rc-summary）：
#   · TL-01 链路自洽：W=3.065s、rate=0.2 ⇒ ΔJD=0.6122 天，相对偏差 **0.14%**
#   · TL-07 恒星时绝对腿：ΔLST 与 ΔJD×360.9856° 残差 **+0.0000°**
# 结论 = **不存在脱钩**；T27 那三个数各有出处：①`getTimeRate()` 的单位是
# **JDay/sec**（`StelCore.hpp:595`），README 的算术 `10 × JD_SECOND × 0.4` 把它
# 当成了"× 实时倍率"；②`engineRate` 在 INTERACTCHECK 里是**移动靶**——套件自己
# 的 IT-05 注入 L 键走 `increaseTimeSpeed()`，rate 被从 0.1 提到 1（再按到 10）；
# ③"400ms"是**相位名义 delay**，从未与 ΔJD 同一时刻测过真实窗口。
#
# ── 本轮改动面 ────────────────────────────────────────────────────────────────
# ① 仪器（产品侧**零语义改动**，只有两处 env 负控注入）：
#    `src/app/TimeLinkProbe.*` — `STELQUICK_TIMELINK_PROBE=1`，`TIMELINKPROBE:`
#      前缀，只报读数不打 PASS（Q1 时钟面 / Q2 链路原始读数 / Q3 窗口线性 /
#      Q4 速率阶梯台账 / Q5 冻结 / Q6 恒星时+下游重算+视线机理核验）
#    `src/app/TimeLinkCheck.*` — `STELQUICK_TIMELINK_CHECK=1`，**7 条判据** TL-01..TL-07
#    `src/ui/LiveSkyRuntime.cpp` — 三组负控注入（见下）
#
# ── 判据数恒为 **7**；四组读数（1 正题 + 2 负控 + 1 探针）──────────────────────
# ① 正题（`STELQUICK_TIMELINK_CHECK=1`）
#    期望 rc=0 ∧ `判据 7/7` ∧ `VERDICT=PASS` ∧ 0 条 ✗。
#    **脚本自证**（不信被测的 PASS，自己复算）：
#      · 从 TL-01 行解析 W / ΔJD / rate，**独立复算** |ΔJD/(W·rate)−1| ≤ 15%；
#      · 从 TL-07 行解析 ΔLST 与期望角，独立复算残差 ≤ max(1.0°, 0.1%×期望)；
#      · 全量日志里**不许有未替换的占位符**（`%1` / `%.4f`）—— 本轮血泪：
#        `.arg()` 找不到 `%n` 会**原样返回**，日志里留一串 `%.4f`，不报错。
#      · 收尾还原行必须在（判据碰过 rate/scale，必须还原；血泪第 9 条）。
# ② 负控 A（`STELQUICK_TIMELINK_BREAK=1`）：帧泵传 `dt×0.5` ⇒ 推进量与墙钟脱钩。
#    期望 rc=10 ∧ 判据 6/7 ∧ 红项**恰好** [TL-01]（比值型 TL-02/03/05 整体缩放不变）。
# ③ 负控 B（`STELQUICK_TIMELINK_RATE_IGNORED=1`）：链路用**钉死的 0.1** 推进，
#    但 `getTimeRate()` 仍返回用户设的值 —— 这就是 T27 README 里假设过的那个
#    缺陷形态（"帧推进走固定量，rate 只管别的语义"），本轮把它**实现出来**当负控。
#    期望 rc=10 ∧ 判据 4/7 ∧ 红项**恰好** [TL-01, TL-02, TL-05]。
# ③' 负控 C（`STELQUICK_TIMELINK_FREEZE_LEAK=1`）：冻结（scale=0）期间**照旧推进**
#    ——"只在推进那一瞬间把 scale 解成 1、推完写回 0"，所以**读数**仍是冻结，链路
#    却漏了（scale 没真进链路）。
#    期望 rc=10 ∧ 判据 6/7 ∧ 红项**恰好** [TL-04]（冻结窗 ΔJD ≠ 0）。
#    A/B/C 红项集合两两不同 ⇒ "链路自洽"、"rate 真进链路"、"冻结必须真零"三条腿
#    **各自承重**。
#    ⚠️ 为什么不用"解冻后把冻结期补回来"当 C？实测它**不可靠**：解冻后检查还要
#    等 300ms 才 `arm()`，"补"的那一脚落在开窗之前 ⇒ 判据照绿（比值 0.9947）。
#    负控形状对、落点不对 —— 这本身就是一条仪器教训。
#    ⚠️ TL-04 的带宽下界**刻意定在 0.4**：A/B 在该窗口的比值都恰好是 0.5，下界
#    写 0.5 会让 TL-04 的红/绿随计时噪声漂（实测 0.4990 ↔ 0.5002），且与 TL-01
#    职责重叠。TL-04 的两条腿 = "冻结真零"（负控 C 咬）+ "不许补"（带内即可）。
# ④ 探针（`STELQUICK_TIMELINK_PROBE=1`）—— 分类靠读数不靠猜。
#    期望 rc=0 ∧ `VERDICT=DONE`，且 Q1/Q2/Q3/Q5/Q6b/Q6c 行齐全。
#    rc=6（`VERDICT=UNAVAILABLE`）= 引擎未初始化，**不洗成 PASS**。
#
# ── 🔴 本轮三条仪器血泪（脚本要能抓住它们复现）───────────────────────────────
# ① **`.arg()` 与 printf 风格混用**：Qt 的 `QString::arg` 只认 `%1`；字符串里写
#    `%.4f` 时它**不报错、原样返回** ⇒ 日志打出一串 `%.4f`（"仪器会撒谎"的又一副
#    面孔）。脚本自证：全量日志 `%1` / `%.` 命中数必须为 **0**。
# ② **awk 里 `exp` 是内建函数名，不能当变量**（macOS BWK awk 直接 syntax error、
#    **零输出**）⇒ 自证门会静默变成空字符串，看起来像"没输出"而不是"算错了"。
#    脚本里现在只用 `expected` 这类非内建名。
# ③ **"三个量没在同一时刻量齐"就是可复现的口径缺陷**：脚本不引用被测 PASS，
#    而是**从日志行里把三个原始量重新解析出来自己算一遍**（下文的复算门）。
#
# ── 相邻回归（本轮动了 main.cpp / LiveSkyRuntime / CMake ⇒ 必须全套重跑）──────
# 10 套件（time / returnui / search / action / locate / locate-ui / replay / clock /
# timeui / location，另加 T34 的 toolbar）要求 rc=0；另加 A2 逐像素、DYN 双路
# （engine + 替身）、`producer-readback`、S3 旧宿主。
#  ⚠️ **INTERACTCHECK 单列**：它依赖"窗口真的拿到系统焦点"（血泪第 17 条）。
#     抢不到前台时判据记 UNAVAILABLE（rc=6）——那是**仪器前提不成立、不是回归**
#     ⇒ 记 ENV-SKIP，**不洗成 PASS**，也不算失败；真出 ✗ 才判红。
set -u
cd /Users/ztuqfvy/qt_demo/stellarium_vulkan

export VK_DRIVER_FILES=/opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json
export QT_VULKAN_LIB=/opt/homebrew/opt/vulkan-loader/lib/libvulkan.1.dylib
export STELQUICK_GRAPHICS_API=metal

BIN=./build-release/src/ui/stelQuickUI.app/Contents/MacOS/stelQuickUI
OUT=docs/evidence/2026-09-30-t35-timelink/mac
TARGET=${1:-all}
CORE_RUNS=${2:-5}
DYN_RUNS=${2:-3}
mkdir -p "$OUT"
FAILED=0

# 显示器休眠门（T29/W-T29 的教训）：本机电池档 displaysleep=2。显示器一旦睡，
# 帧泵节拍与 polish 都受影响 ⇒ 时间类读数直接污染。
wake_display() {
  caffeinate -u -t 3 >/dev/null 2>&1
  caffeinate -dimsu -t 7200 >/dev/null 2>&1 &
  CAFFEINATE_PID=$!
  sleep 1
  echo "环境：已请求唤醒显示器并挂 caffeinate（pid=$CAFFEINATE_PID，2 小时）"
}
CAFFEINATE_PID=""
wake_display

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
    echo "环境注记：Spotlight 批量索引正在运行（mdbulkimport）——可能拖慢帧泵/polish"
  fi
  echo "二进制：$(ls -l "$BIN" | awk '{print $5" B  "$6" "$7" "$8}')  md5=$(md5 -q "$BIN")"
}

# ✗ 行里的判据 ID 抽取（只用原始量，不复刻被测逻辑）。
red_ids() {
  /usr/bin/grep -E "TIMELINKCHECK: +✗ " "$1" \
    | /usr/bin/grep -oE "✗ TL-[0-9]+" \
    | /usr/bin/awk '{print $2}' | sort | tr '\n' ','
}

# 🔴 占位符漏替换自证：`%1` / `%.4f` 一律不许出现（本轮血泪①）。
placeholder_leak() {
  /usr/bin/grep -cE "TIMELINK(CHECK|PROBE): .*(%[0-9]|%\.[0-9])" "$1"
}

# 🔴 脚本**独立复算** TL-01：从读数行里把 W / ΔJD / rate 抠出来自己算比值。
# 不复述被测的 PASS（"对照量必须换来源"的精神：判据说它自洽，脚本自己再算一遍）。
recheck_tl01() {
  local f=$1 line w djd rate
  line=$(/usr/bin/grep -m1 -E "TIMELINKCHECK: +✓ TL-01 链路自洽" "$f")
  w=$(echo "$line" | sed -n 's/.*窗口 W=\([0-9.]*\)s.*/\1/p')
  djd=$(echo "$line" | sed -n 's/.*ΔJD=\([0-9.]*\) 天.*/\1/p')
  rate=$(echo "$line" | sed -n 's/.*rate=\([0-9.eE+-]*\) scale.*/\1/p')
  [[ -z "$w" || -z "$djd" || -z "$rate" ]] && { echo "PARSE-FAIL"; return 1; }
  awk -v w="$w" -v d="$djd" -v r="$rate" 'BEGIN {
      pred = w * r;
      if (pred <= 0) { print "BAD-PRED"; exit 1 }
      rel = (d - pred) / pred; if (rel < 0) rel = -rel;
      printf "W=%s ΔJD=%s rate=%s 复算比值=%.4f 复算偏差=%.2f%%\n", w, d, r, d/pred, rel*100;
      exit (rel <= 0.15) ? 0 : 1;
  }'
}

# 🔴 脚本独立复算 TL-07：ΔLST 与 ΔJD×360.9856° 的残差（含带符号解析）。
# ⚠️ awk 里**不许用 `exp` / `log` / `sqrt` / `length` 当变量名**（macOS BWK awk 会
#    直接 `syntax error`、零输出 —— 本轮踩过：`exp = d*360.9856` ⇒ 自证门静默变空）。
recheck_tl07() {
  local f=$1 line dlst djd
  line=$(/usr/bin/grep -m1 -E "TIMELINKCHECK: +✓ TL-07 恒星时腿" "$f")
  djd=$(echo "$line" | sed -n 's/.*ΔJD=\([0-9.]*\) 天.*/\1/p')
  dlst=$(echo "$line" | sed -n 's/.*ΔLST=\([+-][0-9.]*\)°.*/\1/p')
  [[ -z "$djd" || -z "$dlst" ]] && { echo "PARSE-FAIL djd=[$djd] dlst=[$dlst]"; return 1; }
  awk -v d="$djd" -v l="$dlst" 'BEGIN {
      expected = d * 360.9856091;
      res = l - expected; if (res < 0) res = -res;
      tol = (expected < 0) ? -expected * 0.001 : expected * 0.001; if (tol < 1.0) tol = 1.0;
      printf "ΔJD=%s ΔLST=%s 期望=%.4f° 复算残差=%.4f°（容差 %.2f°）\n", d, l, expected, res, tol;
      exit (res <= tol) ? 0 : 1;
  }'
}

: > "$OUT/rc-summary.txt"

# ══ ① 正题：TIMELINKCHECK × N（7 条）═════════════════════════════════════════
if [[ "$TARGET" == "all" || "$TARGET" == "core" || "$TARGET" == "pos" ]]; then
  integer pass=0 envskip=0 bad=0 i rc
  : > "$OUT/timelinkcheck-mac-n$CORE_RUNS.txt"
  for i in $(seq 1 $CORE_RUNS); do
    run_suite "$OUT/timelinkcheck-mac-run$i.txt" env STELQUICK_TIMELINK_CHECK=1 "$BIN"
    rc=$?
    line=$(/usr/bin/grep -E "TIMELINKCHECK: 判据" "$OUT/timelinkcheck-mac-run$i.txt" | tail -1)
    verd=$(/usr/bin/grep -E "TIMELINKCHECK: VERDICT" "$OUT/timelinkcheck-mac-run$i.txt" | tail -1)
    fails=$(/usr/bin/grep -cE "TIMELINKCHECK: +✗" "$OUT/timelinkcheck-mac-run$i.txt")
    leak=$(placeholder_leak "$OUT/timelinkcheck-mac-run$i.txt")
    restore=$(/usr/bin/grep -cE "TIMELINKCHECK: +收尾：已还原 rate=" \
                  "$OUT/timelinkcheck-mac-run$i.txt")
    r1=$(recheck_tl01 "$OUT/timelinkcheck-mac-run$i.txt"); ok1=$?
    r7=$(recheck_tl07 "$OUT/timelinkcheck-mac-run$i.txt"); ok7=$?
    kind="FAIL"
    if [[ $rc -eq 0 && "$line" == *"判据 7/7"* && "$verd" == *"VERDICT=PASS"* \
          && $fails -eq 0 && $leak -eq 0 && $restore -eq 1 && $ok1 -eq 0 && $ok7 -eq 0 ]]; then
      kind="PASS"; (( pass++ ))
    elif [[ $rc -eq 6 && "$verd" == *"UNAVAILABLE"* && $fails -eq 0 ]]; then
      kind="ENV-SKIP（引擎前提不成立 ⇒ UNAVAILABLE，非产品缺陷）"; (( envskip++ ))
    else
      kind="FAIL"; (( bad++ )); FAILED=1
    fi
    {
      echo "──────── TIMELINKCHECK run $i/$CORE_RUNS（rc=$rc）→ $kind（✗=$fails/0 占位符漏=$leak/0 还原行=$restore/1 脚本复算TL-01=$ok1 脚本复算TL-07=$ok7）────────"
      echo "  脚本独立复算 TL-01：$r1"
      echo "  脚本独立复算 TL-07：$r7"
      grep -E "TIMELINKCHECK:" "$OUT/timelinkcheck-mac-run$i.txt"
    } >> "$OUT/timelinkcheck-mac-n$CORE_RUNS.txt"
  done
  echo "timelinkcheck-mac pos-pass=$pass env-skip=$envskip bad=$bad（共 $CORE_RUNS 跑；env-skip = 引擎前提不成立，**不洗成 PASS**）" \
    | tee -a "$OUT/rc-summary.txt"
  ghosts=$(pgrep -fc stelQuickUI 2>/dev/null || echo 0)
  echo "timelinkcheck-mac 残留进程数=$ghosts（要求 0）" | tee -a "$OUT/rc-summary.txt"
  [[ "$ghosts" == "0" ]] || FAILED=1
  if [[ $pass -eq 0 && $envskip -eq 0 ]]; then
    echo "timelinkcheck-mac 正题未取得任何一次 7/7 —— 环境太差（FAILED）" | tee -a "$OUT/rc-summary.txt"
    FAILED=1
  fi
fi

# ══ ②..⑤ 三组负控 + 探针 ═════════════════════════════════════════════════════
if [[ "$TARGET" == "all" || "$TARGET" == "core" || "$TARGET" == "negctl" ]]; then
  : > "$OUT/negctl-mac.txt"

  # ── 负控 A：BREAK（帧泵 dt×0.5 ⇒ 推进量与墙钟脱钩）────────────────────────
  run_suite "$OUT/negctl-A-break.txt" env STELQUICK_TIMELINK_CHECK=1 \
    STELQUICK_TIMELINK_BREAK=1 "$BIN"
  rc=$?
  line=$(/usr/bin/grep -E "TIMELINKCHECK: 判据" "$OUT/negctl-A-break.txt" | tail -1)
  reds=$(red_ids "$OUT/negctl-A-break.txt")
  vA="negctl-A-BAD"
  if [[ $rc -eq 10 && "$line" == *"判据 6/7"* && "$reds" == "TL-01," ]]; then
    vA="negctl-A-OK"; else FAILED=1
  fi
  {
    echo "──────── 负控 A（BREAK，帧泵半速）（rc=$rc）→ $vA ────────"
    echo "  期望：rc=10 ∧ 判据 6/7 ∧ 红项**恰好** [TL-01]"
    echo "  实测：红=[$reds]"
    grep -E "TIMELINKCHECK: (✗|判据|VERDICT)" "$OUT/negctl-A-break.txt"
  } >> "$OUT/negctl-mac.txt"
  echo "negctl-A $vA（要求 rc=10 ∧ 6/7 ∧ 红恰好 TL-01）" | tee -a "$OUT/rc-summary.txt"

  # ── 负控 B：RATE_IGNORED（链路 rate 钉死 0.1，读数照旧）───────────────────
  run_suite "$OUT/negctl-B-rate-ignored.txt" env STELQUICK_TIMELINK_CHECK=1 \
    STELQUICK_TIMELINK_RATE_IGNORED=1 "$BIN"
  rc=$?
  line=$(/usr/bin/grep -E "TIMELINKCHECK: 判据" "$OUT/negctl-B-rate-ignored.txt" | tail -1)
  reds=$(red_ids "$OUT/negctl-B-rate-ignored.txt")
  vB="negctl-B-BAD"
  if [[ $rc -eq 10 && "$line" == *"判据 4/7"* \
        && "$reds" == "TL-01,TL-02,TL-05," ]]; then
    vB="negctl-B-OK"; else FAILED=1
  fi
  {
    echo "──────── 负控 B（RATE_IGNORED，链路 rate 钉死）（rc=$rc）→ $vB ────────"
    echo "  期望：rc=10 ∧ 判据 4/7 ∧ 红项**恰好** [TL-01,TL-02,TL-05]"
    echo "  （TL-03 是同 rate 下两窗口之比 ⇒ 整体缩放不变 ⇒ 照绿；TL-06/TL-07 照绿；"
    echo "    TL-04 恢复腿比值 0.5 落在带宽 [0.4,1.6] 内 ⇒ 照绿 —— TL-04 由负控 C 独家咬）"
    echo "  实测：红=[$reds]"
    grep -E "TIMELINKCHECK: (✗|判据|VERDICT)" "$OUT/negctl-B-rate-ignored.txt"
  } >> "$OUT/negctl-mac.txt"
  echo "negctl-B $vB（要求 rc=10 ∧ 4/7 ∧ 红恰好 TL-01/02/05）" | tee -a "$OUT/rc-summary.txt"

  # ── 负控 C：FREEZE_LEAK（冻结形同虚设：scale 没进链路）───────────────────
  run_suite "$OUT/negctl-C-freeze-leak.txt" env STELQUICK_TIMELINK_CHECK=1 \
    STELQUICK_TIMELINK_FREEZE_LEAK=1 "$BIN"
  rc=$?
  line=$(/usr/bin/grep -E "TIMELINKCHECK: 判据" "$OUT/negctl-C-freeze-leak.txt" | tail -1)
  reds=$(red_ids "$OUT/negctl-C-freeze-leak.txt")
  vC="negctl-C-BAD"
  if [[ $rc -eq 10 && "$line" == *"判据 6/7"* && "$reds" == "TL-04," ]]; then
    vC="negctl-C-OK"; else FAILED=1
  fi
  {
    echo "──────── 负控 C（FREEZE_LEAK，冻结漏推进）（rc=$rc）→ $vC ────────"
    echo "  期望：rc=10 ∧ 判据 6/7 ∧ 红项**恰好** [TL-04]"
    echo "  （冻结窗 ΔJD=0.5·W·rate ≠ 0 触发 TL-04 的冻结腿；其余窗口都在冻结之前）"
    echo "  实测：红=[$reds]"
    grep -E "TIMELINKCHECK: (✗|判据|VERDICT)" "$OUT/negctl-C-freeze-leak.txt"
  } >> "$OUT/negctl-mac.txt"
  echo "negctl-C $vC（要求 rc=10 ∧ 6/7 ∧ 红恰好 TL-04）" | tee -a "$OUT/rc-summary.txt"

  # ── ④ 探针：链路读数的实测依据 ────────────────────────────────────────────
  run_suite "$OUT/probe-timelink-mac.txt" env STELQUICK_TIMELINK_PROBE=1 "$BIN"
  prc=$?
  pline=$(/usr/bin/grep -E "TIMELINKPROBE: VERDICT" "$OUT/probe-timelink-mac.txt" | tail -1)
  pq1=$(/usr/bin/grep -cE "TIMELINKPROBE: +Q1 读面 " "$OUT/probe-timelink-mac.txt")
  pq2=$(/usr/bin/grep -cE "TIMELINKPROBE: +Q2 档A" "$OUT/probe-timelink-mac.txt")
  pq3=$(/usr/bin/grep -cE "TIMELINKPROBE: +Q3 档A'\(短\)" "$OUT/probe-timelink-mac.txt")
  pq4=$(/usr/bin/grep -cE "TIMELINKPROBE: +Q4 increaseTimeSpeed 第 3 次" "$OUT/probe-timelink-mac.txt")
  pq5=$(/usr/bin/grep -cE "TIMELINKPROBE: +Q5 冻结\(scale=0\)" "$OUT/probe-timelink-mac.txt")
  pq6b=$(/usr/bin/grep -cE "TIMELINKPROBE: +Q6b 恒星时恒等式" "$OUT/probe-timelink-mac.txt")
  pq6c=$(/usr/bin/grep -cE "TIMELINKPROBE: +Q6c 下游重算" "$OUT/probe-timelink-mac.txt")
  pleak=$(placeholder_leak "$OUT/probe-timelink-mac.txt")
  # 探针的 Q1 自证：三读面必须逐个打印出来（不是"只报了一个数"）。
  pq1faces=$(/usr/bin/grep -cE "TIMELINKPROBE: +Q1 读面 simClockJD=.*facade.julianDay=" \
                 "$OUT/probe-timelink-mac.txt")
  if [[ $prc -eq 0 && "$pline" == *"VERDICT=DONE"* && $pq1 -eq 1 && $pq1faces -eq 1 \
        && $pq2 -eq 1 && $pq3 -eq 1 && $pq4 -eq 1 && $pq5 -eq 1 \
        && $pq6b -eq 1 && $pq6c -eq 1 && $pleak -eq 0 ]]; then
    echo "probe-timelink-mac probe-OK（$pline；Q1读面=$pq1faces Q2=$pq2 Q3=$pq3 Q4阶梯第3次=$pq4 Q5=$pq5 Q6b=$pq6b Q6c=$pq6c 占位符漏=$pleak）" \
      | tee -a "$OUT/rc-summary.txt"
  elif [[ $prc -eq 6 && "$pline" == *"UNAVAILABLE"* ]]; then
    echo "probe-timelink-mac ENV-SKIP（$pline ⇒ 引擎未初始化，**不洗成 PASS**）" \
      | tee -a "$OUT/rc-summary.txt"
    FAILED=1
  else
    echo "probe-timelink-mac probe-BAD（rc=$prc / $pline / Q1=$pq1 Q1读面=$pq1faces Q2=$pq2 Q3=$pq3 Q4=$pq4 Q5=$pq5 Q6b=$pq6b Q6c=$pq6c 占位符漏=$pleak）" \
      | tee -a "$OUT/rc-summary.txt"
    FAILED=1
  fi
fi

# ══ ⑤ 相邻回归：本轮动了 main.cpp / LiveSkyRuntime / CMake ⇒ 全套重跑 ═════════
if [[ "$TARGET" == "all" || "$TARGET" == "core" || "$TARGET" == "regress" ]]; then
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
    "STELQUICK_TOOL_CHECK=1:toolbarcheck"
  do
    var=${spec%%:*}; name=${spec##*:}
    run_suite "$OUT/regression-$name.txt" env $var "$BIN"
    rc=$?
    echo "regression-$name rc=$rc" | tee -a "$OUT/rc-summary.txt"
    [[ $rc -eq 0 ]] || FAILED=1
  done

  # ── INTERACTCHECK 单独处理：它依赖**窗口真的拿到系统焦点**（血泪第 17 条）──
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

  # ── S3 旧宿主（`stellarium`）—— **真的跑一遍**，不再看 md5（陷阱 37）────────
  STELA3_CHECK=1 ./build-release/src/stellarium > "$OUT/regression-s3-stela3.txt" 2>&1
  rc=$?
  echo "regression-s3-stela3 rc=$rc" | tee -a "$OUT/rc-summary.txt"
  [[ $rc -eq 0 ]] || FAILED=1
fi

# ══ ⑥ A2 逐像素（Metal）═════════════════════════════════════════════════════
if [[ "$TARGET" == "all" || "$TARGET" == "regress" || "$TARGET" == "core" ]]; then
  run_suite "$OUT/regression-a2-metal.txt" env STELQUICK_A2_CHECK=1 "$BIN"
  rc=$?
  echo "regression-a2-metal rc=$rc" | tee -a "$OUT/rc-summary.txt"
  [[ $rc -eq 0 ]] || FAILED=1
fi

# ══ ⑦ DYN 双跑（真实引擎 + 替身判别性对照）══════════════════════════════════
# ⚠️ 生产者必须**显式**指定：`STELQUICK_DYN_PRODUCER` 默认是替身（main.cpp 只看
#    `== "engine"`），不设它就两次都跑替身 —— 见
#    docs/evidence/2026-09-29-t29-ime/CORRECTION.md。
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
if [[ "$TARGET" == "all" || "$TARGET" == "core" || "$TARGET" == "regress" || "$TARGET" == "dyn" ]]; then
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
echo "T35 证据已写入 $OUT/（FAILED=$FAILED）"
[[ $FAILED -eq 0 ]] || exit 1
