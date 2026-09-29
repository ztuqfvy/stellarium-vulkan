#!/bin/zsh
# T33 验证脚本（2026-09-29）：**观察地点写入面**（地点页 + 引擎地点库联动）。
#
# 用法：tools/t33-verify.sh              （跑全部）
#       tools/t33-verify.sh core 5       （正题 ×5 + 四组负控 + 探针 + 相邻回归）
#       tools/t33-verify.sh negctl       （只跑四组负控 + 探针）
#       tools/t33-verify.sh regress      （只跑 A2 逐像素 + DYN 双跑 + 生产者回读）
#
# 证据落 docs/evidence/2026-09-29-t33-location/mac/。
# 环境口径（Metal + MoltenVK）**刻意与 t17..t32 / wt29 完全一致，不换**。
#
# ── 本轮改动面 ────────────────────────────────────────────────────────────────
# ① 产品：`src/app/LocationProbe.*`（T33-A 数据面探针）、`src/app/LocationCheck.*`
#    （T33-C 判据）、`AppFacade` 的 `setLocationById/setLocationLatLon`（写入面单一入口 +
#    范围闸 + 回读验证）、`src/ui/qml/LocationPage.qml` 新页 + `MainWindow.qml` 挂页。
# ② 仪器：`src/ui/main.cpp` 新增两段
#    - `STELQUICK_LOC_PROBE=1`  → 第一相位数据面探针（只报读数，**不打 PASS**）
#    - `STELQUICK_LOC_CHECK=1`  → 第二相位写入面自检 **10 条判据**（LC-01..LC-08）
#
# ── 六组读数（判据数恒为 **10**；四组负控各自的红项**互不相同**）─────────────────
# ① 正题（`STELQUICK_LOC_CHECK=1`）
#    期望 `判据 10/10` + `VERDICT=PASS` + rc=0，且日志里必须有
#    `就绪门[Paris/Abbeville/Beijing]` ×3 + `目录纬度：` ×1 + 零 ✗。
#    （前两项是**仪器自证**：没有它们，10/10 可能是"空跑全过"。）
# ② 负控 A（`..._NODELAY=1` + `..._GATE_OFF=1`）—— 复现本轮修掉的间歇红
#    🔴 **概率性**负控：被复现的缺陷**本身就是竞态**（回读步与写入步只隔一个事件循环）
#    ⇒ 帧泵**有时**抢先 tick ⇒ 该跑读到的已是新值 ⇒ **全绿**。
#    三批实测（**逐位相同**的二进制）：run1 `5/5` 红、run2 `5/5` 红、run3 **`3/5` 红**。
#    ⇒ 判据只要求：① 红项**只许是** LC-04/LC-04b（出现别的红 = 真回归）；
#                  ② 整批至少复现一次（否则记 INCONCLUSIVE）。
#    红起来时的签名：三个地点的天极高度角**全读成 48.853°**（= Paris，第一个写入的那个）
#    ⇒ 回读步读到了**上一个地点**的变换矩阵。
# ③ 负控 B（只 `..._NODELAY=1`，门兜底）—— 证明**门是承重件、不是装饰**
#    期望 rc=0、`判据 10/10`、零红。**不对"探测次数"设门**：摘掉写入延迟后帧泵可能
#    **恰好在回读步之前**先跑一拍 ⇒ 门读到的已是新值 ⇒ **探测 0 次也正确**（实测照样 10/10）。
#    探测次数只作**信息**记录。**"门承重"的证据是 A↔B 这一对**：
#    A（门关）5/5 红 8/10 ∧ B（门开）5/5 绿 10/10。
# ④ 负控 C（`..._RANGE_GATE_OFF=1`）—— 摘掉**写入路径**的范围闸调用点
#    期望 rc=10、`判据 8/10`，红项**恰好** LC-05 / LC-08；
#    **LC-01 仍绿**（谓词本体没动）⇒ "规则正确"与"规则被调用"两腿分离的实证。
# ⑤ 负控 D（`..._WRITE_NOOP=1`）—— 写入直接 no-op、**对外照旧报成功**
#    期望 rc=10、`判据 6/10`，红项**恰好** LC-03a / LC-03b / LC-04 / LC-04b。
#    ⚠️ 这条暴露过**最要命的一次假绿**：LC-04 的对照量原先取 `facade->locationLatitude()`
#       （与 `core->getCurrentLocation()` 同源）⇒ 写不进去时两边**一起停在旧地点**、
#       自洽通过。改取**地点库目录纬度** `locMgr().locationForString(id)` 后才真红
#       （Paris |Δ|=17.386°）。
# ⑥ 探针（`STELQUICK_LOC_PROBE=1`）—— 数据面的**实测依据**（分类靠它，不靠猜）
#    期望 rc=0、`LOCPROBE: VERDICT=DONE`，且日志里 `LOCPROBE: P2 地点库规模` 非零。
#    rc=6（`VERDICT=UNAVAILABLE`）= 引擎/depot 不可用，**不洗成 PASS**。
#
# ── 相邻回归（本轮动了 main.cpp / AppFacade / QML ⇒ 必须全套重跑）──────────────
#  9 套件（time / returnui / search / action / locate / locate-ui / replay / clock /
#  timeui）要求 rc=0；另加 A2 逐像素、DYN 双路（engine + 替身）、`producer-readback`。
#  ⚠️ **INTERACTCHECK 单列**：它依赖"窗口真的拿到系统焦点"（血泪第 17 条）。脚本
#     `env` 直启的进程不被 LaunchServices 认作 `.app` 实例 ⇒ 常常抢不到前台 ⇒
#     判据记 UNAVAILABLE（rc=6）。这是**仪器前提不成立、不是回归** ⇒ 记 ENV-SKIP，
#     **不洗成 PASS**，也不算失败；真出 ✗ 才判红。
set -u
cd /Users/ztuqfvy/qt_demo/stellarium_vulkan

export VK_DRIVER_FILES=/opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json
export QT_VULKAN_LIB=/opt/homebrew/opt/vulkan-loader/lib/libvulkan.1.dylib
export STELQUICK_GRAPHICS_API=metal

BIN=./build-release/src/ui/stelQuickUI.app/Contents/MacOS/stelQuickUI
OUT=docs/evidence/2026-09-29-t33-location/mac
TARGET=${1:-all}
CORE_RUNS=${2:-5}
NEG_A_RUNS=${2:-2}
NEG_B_RUNS=${2:-2}
DYN_RUNS=${2:-3}
mkdir -p "$OUT"
FAILED=0

# 显示器休眠门（T29/W-T29 的教训，照抄）：本机电池档 displaysleep=2。显示器一旦睡，
# 布局/polish 与帧泵节拍都会受影响 ⇒ 读数污染。地点判据要读**变换栈**（每帧重建），
# 更得保证窗口还活着。
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
    echo "环境注记：Spotlight 批量索引正在运行（mdbulkimport）——可能拖慢布局/polish"
  fi
  echo "二进制：$(ls -l "$BIN" | awk '{print $5" B  "$6" "$7" "$8}')  md5=$(md5 -q "$BIN")"
}

# ✗ 行里的判据 ID 抽取（只用原始量，不复刻被测逻辑）。
# 用 `grep -oE` 抓 `✗ LC-xx`，再取第 2 字段 ⇒ 排序后拼成 "LC-04,LC-04b,"。
red_ids() {
  /usr/bin/grep -E "LOCATIONCHECK: +✗ " "$1" \
    | /usr/bin/grep -oE "✗ LC-[0-9]+[ab]?" \
    | /usr/bin/awk '{print $2}' | sort | tr '\n' ','
}
# 三处就绪门"探测 N 次"之和（负控 B 的承重证据）。
gate_polls() {
  /usr/bin/grep -oE "探测 [0-9]+ 次" "$1" \
    | /usr/bin/grep -oE "[0-9]+" | /usr/bin/awk '{s+=$1} END{print s+0}'
}

: > "$OUT/rc-summary.txt"

# ══ ① 正题：LOCATIONCHECK × N（10 条）═════════════════════════════════════════
if [[ "$TARGET" == "all" || "$TARGET" == "core" || "$TARGET" == "pos" ]]; then
  integer pass=0 envskip=0 bad=0 i rc
  : > "$OUT/loccheck-mac-n$CORE_RUNS.txt"
  for i in $(seq 1 $CORE_RUNS); do
    run_suite "$OUT/loccheck-mac-run$i.txt" env STELQUICK_LOC_CHECK=1 "$BIN"
    rc=$?
    line=$(/usr/bin/grep -E "LOCATIONCHECK: 判据" "$OUT/loccheck-mac-run$i.txt" | tail -1)
    verd=$(/usr/bin/grep -E "LOCATIONCHECK: VERDICT" "$OUT/loccheck-mac-run$i.txt" | tail -1)
    fails=$(/usr/bin/grep -cE "LOCATIONCHECK: +✗" "$OUT/loccheck-mac-run$i.txt")
    # 仪器自证：三处就绪门 + 目录纬度快照。缺了它们 10/10 不算数。
    gates=$(/usr/bin/grep -cE "LOCATIONCHECK: +就绪门\[" "$OUT/loccheck-mac-run$i.txt")
    dbcat=$(/usr/bin/grep -cE "LOCATIONCHECK: +目录纬度：" "$OUT/loccheck-mac-run$i.txt")
    # 前提自证：判据必须**显式报出** `flagUseCTZ` 的状态（它在 Windows 那台起因
    # config 的 `localization/time_zone` 非空而为 true ⇒ 时区联动被按设计跳过 ⇒
    # LC-03a 恒红 / LC-03b 假绿）。没有这一行就不算数：说明跑的是旧仪器。
    ctz=$(/usr/bin/grep -cE "LOCATIONCHECK: +前提②" "$OUT/loccheck-mac-run$i.txt")
    kind="FAIL"
    if [[ $rc -eq 0 && "$line" == *"判据 10/10"* && "$verd" == *"VERDICT=PASS"* \
          && $fails -eq 0 && $gates -eq 3 && $dbcat -eq 1 && $ctz -ge 1 ]]; then
      kind="PASS"; (( pass++ ))
    elif [[ $rc -eq 6 && "$verd" == *"UNAVAILABLE"* && $fails -eq 0 ]]; then
      # 地点库取不到 Paris/Beijing（depot 缺失）⇒ 前提门判 UNAVAILABLE（**不洗成 PASS**）
      kind="ENV-SKIP（地点库前提不成立 ⇒ UNAVAILABLE，非产品缺陷）"; (( envskip++ ))
    else
      kind="FAIL"; (( bad++ )); FAILED=1
    fi
    {
      echo "──────── LOCATIONCHECK run $i/$CORE_RUNS（rc=$rc）→ $kind（就绪门行=$gates/3 目录纬度行=$dbcat/1 前提②行=$ctz）────────"
      grep -E "LOCATIONCHECK:" "$OUT/loccheck-mac-run$i.txt"
    } >> "$OUT/loccheck-mac-n$CORE_RUNS.txt"
  done
  echo "loccheck-mac pos-pass=$pass env-skip=$envskip bad=$bad（共 $CORE_RUNS 跑；env-skip = 地点库前提不成立，**不洗成 PASS**）" \
    | tee -a "$OUT/rc-summary.txt"
  ghosts=$(pgrep -fc stelQuickUI 2>/dev/null || echo 0)
  echo "loccheck-mac 残留进程数=$ghosts（要求 0）" | tee -a "$OUT/rc-summary.txt"
  [[ "$ghosts" == "0" ]] || FAILED=1
  if [[ $pass -eq 0 ]]; then
    echo "loccheck-mac 正题未取得任何一次 10/10 —— 环境太差（FAILED）" | tee -a "$OUT/rc-summary.txt"
    FAILED=1
  fi
fi

# ══ ②..⑤ 四组负控（每组红项**必须互不相同**）═══════════════════════════════════
if [[ "$TARGET" == "all" || "$TARGET" == "core" || "$TARGET" == "negctl" ]]; then
  : > "$OUT/negctl-mac.txt"

  # ── 负控 A：NODELAY + GATE_OFF（复现间歇红）────────────────────────────────
  # 🔴 这条负控是**概率性**的 —— **不要**把"每次都红"写成判据（初版这么写过，已被实测推翻）。
  #   被复现的缺陷**本身就是竞态**（回读步与写入步只隔一个事件循环，谁先醒看调度）⇒
  #   摘掉写入延迟 + 关掉门之后，帧泵**有时**抢先 tick ⇒ 读到新值 ⇒ 该跑**全绿**。
  #   三批实测：run1 `5/5` 红、run2 `5/5` 红、run3 **`3/5` 红**（同**逐位相同**的二进制）。
  #   ⇒ 判据口径：① **红项只许是 LC-04/LC-04b**（出现别的红 = 真回归，判红）；
  #              ② 整批**至少复现一次**（否则这一批没取得判别性证据 ⇒ 记 INCONCLUSIVE，
  #                 **既不洗成 PASS 也不判红** —— 硬币落反了不是产品的错）。
  integer a_expected=0 a_red=0 i rc
  for i in $(seq 1 $NEG_A_RUNS); do
    run_suite "$OUT/negctl-A-nodelay-gateoff-run$i.txt" env STELQUICK_LOC_CHECK=1 \
      STELQUICK_LOC_NODELAY=1 STELQUICK_LOC_GATE_OFF=1 "$BIN"
    rc=$?
    line=$(/usr/bin/grep -E "LOCATIONCHECK: 判据" "$OUT/negctl-A-nodelay-gateoff-run$i.txt" | tail -1)
    reds=$(red_ids "$OUT/negctl-A-nodelay-gateoff-run$i.txt")
    if [[ "$reds" == "LC-04,LC-04b," ]]; then
      v="negctl-A-red（竞态复现：读到上一个地点的矩阵）"; (( a_expected++ )); (( a_red++ ))
    elif [[ -z "$reds" && $rc -eq 0 && "$line" == *"判据 10/10"* ]]; then
      v="negctl-A-green（竞态**未**复现：帧泵抢先 tick，硬币落反）"; (( a_expected++ ))
    else
      v="negctl-A-BAD（出现了**预期之外**的红项 / rc 异常 ⇒ 真回归）"; FAILED=1
    fi
    {
      echo "──────── 负控 A run $i/$NEG_A_RUNS（NODELAY+GATE_OFF）（rc=$rc）→ $v ────────"
      echo "  只许红 LC-04/LC-04b；**不**要求每跑都红（竞态）；实测红=[$reds]"
      grep -E "LOCATIONCHECK: (✗|就绪门|判据|VERDICT|【主仪器)" "$OUT/negctl-A-nodelay-gateoff-run$i.txt"
    } >> "$OUT/negctl-mac.txt"
  done
  if [[ $a_expected -eq $NEG_A_RUNS && $a_red -ge 1 ]]; then
    echo "negctl-A 竞态复现 $a_red/$NEG_A_RUNS 跑，红项**恒为** LC-04/LC-04b（其余 $(( NEG_A_RUNS - a_red )) 跑硬币落反、全绿）；红项零越界 ⇒ OK" \
      | tee -a "$OUT/rc-summary.txt"
  elif [[ $a_expected -eq $NEG_A_RUNS ]]; then
    echo "negctl-A INCONCLUSIVE：$NEG_A_RUNS 跑**一次都没复现**竞态 ⇒ 本批未取得判别性证据（**不洗成 PASS、也不判红**；提高 -R 跑数或加大负载再试）" \
      | tee -a "$OUT/rc-summary.txt"
  fi

  # ── 负控 B：只 NODELAY（门兜底 ⇒ 应全绿）──────────────────────────────────
  # 🔴 判据口径已按实测收紧（2026-09-29，两轮读数各 5 跑）：
  #   起初要求"门探测次数 ≥1"当**判据**，但那是**竞态本身**的产物 —— 摘掉写入延迟后，
  #   帧泵可能**恰好在回读步之前**先跑一拍 ⇒ 门读到的已经是新值 ⇒ **探测 0 次也正确**。
  #   实测该跑（run5）**照样 `10/10`**。⇒ 探测次数降级为**信息行**，不进判定。
  #   **"门是承重件"的证据是 A↔B 这一对**：A（门关）5/5 红 8/10、B（门开）5/5 绿 10/10。
  integer b_ok=0 b_woken=0
  for i in $(seq 1 $NEG_B_RUNS); do
    run_suite "$OUT/negctl-B-nodelay-gate-on-run$i.txt" env STELQUICK_LOC_CHECK=1 \
      STELQUICK_LOC_NODELAY=1 "$BIN"
    rc=$?
    line=$(/usr/bin/grep -E "LOCATIONCHECK: 判据" "$OUT/negctl-B-nodelay-gate-on-run$i.txt" | tail -1)
    reds=$(red_ids "$OUT/negctl-B-nodelay-gate-on-run$i.txt")
    polls=$(gate_polls "$OUT/negctl-B-nodelay-gate-on-run$i.txt")
    v="negctl-B-BAD"
    if [[ $rc -eq 0 && "$line" == *"判据 10/10"* && -z "$reds" ]]; then
      v="negctl-B-OK"; (( b_ok++ ))
    else
      FAILED=1
    fi
    [[ $polls -ge 1 ]] && (( b_woken++ )) || true
    {
      echo "──────── 负控 B run $i/$NEG_B_RUNS（只 NODELAY，门兜底）（rc=$rc）→ $v ────────"
      echo "  期望：rc=0 ∧ 判据 10/10 ∧ 零红（**不**对探测次数设门 —— 竞态可能幸运转向）"
      echo "  实测：红=[$reds]  探测次数之和=$polls（>0 ⇒ 门确实投过票；=0 ⇒ 帧泵抢先、门不需出手）"
      grep -E "LOCATIONCHECK: (就绪门|判据|VERDICT)" "$OUT/negctl-B-nodelay-gate-on-run$i.txt"
    } >> "$OUT/negctl-mac.txt"
  done
  echo "negctl-B $b_ok/$NEG_B_RUNS（要求 rc=0 ∧ 10/10 ∧ 零红）；其中门被唤醒 $b_woken/$NEG_B_RUNS 跑 —— 探测次数**只作信息、不作判据**；门承重的证据是 A↔B 这一对" \
    | tee -a "$OUT/rc-summary.txt"

  # ── 负控 C：摘掉写入路径的范围闸调用点（谓词本体不动）──────────────────────
  run_suite "$OUT/negctl-C-range-gate-off.txt" env STELQUICK_LOC_CHECK=1 \
    STELQUICK_LOC_RANGE_GATE_OFF=1 "$BIN"
  rc=$?
  line=$(/usr/bin/grep -E "LOCATIONCHECK: 判据" "$OUT/negctl-C-range-gate-off.txt" | tail -1)
  reds=$(red_ids "$OUT/negctl-C-range-gate-off.txt")
  lc01=$(/usr/bin/grep -cE "LOCATIONCHECK: +✓ LC-01 " "$OUT/negctl-C-range-gate-off.txt")
  vC="negctl-C-BAD"
  if [[ $rc -eq 10 && "$line" == *"判据 8/10"* && "$reds" == "LC-05,LC-08," && $lc01 -eq 1 ]]; then
    vC="negctl-C-OK"; else FAILED=1
  fi
  {
    echo "──────── 负控 C（RANGE_GATE_OFF）（rc=$rc）→ $vC ────────"
    echo "  期望：rc=10 ∧ 判据 8/10 ∧ 红项**恰好** [LC-05,LC-08] ∧ **LC-01 仍绿**（两腿分离）"
    echo "  实测：红=[$reds]  LC-01 绿=$lc01/1"
    grep -E "LOCATIONCHECK: (✗|判据|VERDICT)" "$OUT/negctl-C-range-gate-off.txt"
  } >> "$OUT/negctl-mac.txt"
  echo "negctl-C $vC（要求 rc=10 ∧ 8/10 ∧ 红恰好 LC-05/LC-08 ∧ LC-01 仍绿）" \
    | tee -a "$OUT/rc-summary.txt"

  # ── 负控 D：写入 no-op（对外照旧报成功）────────────────────────────────────
  run_suite "$OUT/negctl-D-write-noop.txt" env STELQUICK_LOC_CHECK=1 \
    STELQUICK_LOC_WRITE_NOOP=1 "$BIN"
  rc=$?
  line=$(/usr/bin/grep -E "LOCATIONCHECK: 判据" "$OUT/negctl-D-write-noop.txt" | tail -1)
  reds=$(red_ids "$OUT/negctl-D-write-noop.txt")
  vD="negctl-D-BAD"
  if [[ $rc -eq 10 && "$line" == *"判据 6/10"* && "$reds" == "LC-03a,LC-03b,LC-04,LC-04b," ]]; then
    vD="negctl-D-OK"; else FAILED=1
  fi
  {
    echo "──────── 负控 D（WRITE_NOOP）（rc=$rc）→ $vD ────────"
    echo "  期望：rc=10 ∧ 判据 6/10 ∧ 红项**恰好** [LC-03a,LC-03b,LC-04,LC-04b]"
    echo "  （LC-04 的对照量若取 facade 纬度会同源自洽 ⇒ 假绿；取目录纬度才真红）"
    echo "  实测：红=[$reds]"
    grep -E "LOCATIONCHECK: (✗|LC-04 对照读数|判据|VERDICT)" "$OUT/negctl-D-write-noop.txt"
  } >> "$OUT/negctl-mac.txt"
  echo "negctl-D $vD（要求 rc=10 ∧ 6/10 ∧ 红恰好 LC-03a/03b/04/04b）" \
    | tee -a "$OUT/rc-summary.txt"

  # ── ⑥ 探针：数据面的实测依据（rc=0 是期望；rc=6 = 前提不成立，不洗成 PASS）──
  run_suite "$OUT/probe-loc-data-mac.txt" env STELQUICK_LOC_PROBE=1 "$BIN"
  prc=$?
  pline=$(/usr/bin/grep -E "LOCPROBE: VERDICT" "$OUT/probe-loc-data-mac.txt" | tail -1)
  pscale=$(/usr/bin/grep -cE "LOCPROBE: P2 地点库规模：" "$OUT/probe-loc-data-mac.txt")
  if [[ $prc -eq 0 && "$pline" == *"VERDICT=DONE"* && $pscale -eq 1 ]]; then
    echo "probe-loc-data-mac probe-OK（$pline；地点库规模行=$pscale）" | tee -a "$OUT/rc-summary.txt"
  elif [[ $prc -eq 6 && "$pline" == *"UNAVAILABLE"* ]]; then
    echo "probe-loc-data-mac ENV-SKIP（$pline ⇒ 环境不可用，**不洗成 PASS**）" | tee -a "$OUT/rc-summary.txt"
    FAILED=1
  else
    echo "probe-loc-data-mac probe-BAD（rc=$prc / $pline）" | tee -a "$OUT/rc-summary.txt"
    FAILED=1
  fi
fi

# ══ ⑦ 相邻回归：本轮改了 main.cpp / AppFacade / QML ⇒ 全套重跑 ═══════════════
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
    "STELQUICK_TIME_UI_CHECK=1:timeuicheck"
  do
    var=${spec%%:*}; name=${spec##*:}
    run_suite "$OUT/regression-$name.txt" env $var "$BIN"
    rc=$?
    echo "regression-$name rc=$rc" | tee -a "$OUT/rc-summary.txt"
    [[ $rc -eq 0 ]] || FAILED=1
  done

  # ── INTERACTCHECK 单独处理：它依赖**窗口真的拿到系统焦点**（血泪第 17 条）──
  # 脚本 `env` 直启的进程不被 LaunchServices 认作 .app 实例 ⇒ `requestActivate`
  # 常常抢不到前台 ⇒ 判据记 UNAVAILABLE（rc=6）。这是**仪器前提不成立**，不是回归；
  # 照 T32 的口径：记 ENV-SKIP、**不洗成 PASS**，也不算失败。
  run_suite "$OUT/regression-interactcheck.txt" env STELQUICK_INTERACT_UI_CHECK=1 \
    STELQUICK_INTERACT_REQUEST_ACTIVATE=1 "$BIN"
  irc=$?
  iline=$(/usr/bin/grep -E "INTERACTCHECK: 判据" "$OUT/regression-interactcheck.txt" | tail -1)
  ireds=$(/usr/bin/grep -cE "INTERACTCHECK: ✗" "$OUT/regression-interactcheck.txt")
  if [[ $irc -eq 0 && "$iline" == *"判据 18/18"* && $ireds -eq 0 ]]; then
    echo "regression-interactcheck rc=0（18/18，窗口已激活）" | tee -a "$OUT/rc-summary.txt"
  elif [[ $irc -eq 6 && "$iline" == *"判据 11/11"* && "$iline" == *"UNAVAILABLE"* && $ireds -eq 0 ]]; then
    echo "regression-interactcheck ENV-SKIP（$iline ⇒ 窗口未获系统焦点，**不洗成 PASS**）" \
      | tee -a "$OUT/rc-summary.txt"
  else
    echo "regression-interactcheck FAIL（rc=$irc；$iline；✗=$ireds）" | tee -a "$OUT/rc-summary.txt"
    FAILED=1
  fi

  # ── S3 旧宿主（`stellarium`）—— **真的跑一遍**，不再看 md5 ──────────────────
  # 为什么本轮要加它：以前的论据是"旧宿主二进制 md5 与上轮**逐位相同** ⇒ 未受影响"。
  # 🔴 本轮 Windows 侧实测该 md5 **变了**（`66C51B61…` → `F6E9CE85…`，**字节数不变**
  #    27634176），而构建日志显示**没有任何 T33 源码编进旧宿主** —— `src/app/*` 只编进
  #    `stelQuickUI`（`src/ui/CMakeLists.txt:112-146`）。真因是 **MSVC 链接默认不具
  #    确定性**（每次重掷时间戳/PDB 签名），而本轮 `_deps` 里 ShowMySky 等库被重链
  #    ⇒ 旧宿主跟着重链 ⇒ md5 必变。
  #    ⇒ 口径换成"**真的跑一遍 S3**"（8 条判据），这比"看 md5"强，也不依赖链接器行为。
  STELA3_CHECK=1 ./build-release/src/stellarium > "$OUT/regression-s3-stela3.txt" 2>&1
  rc=$?
  echo "regression-s3-stela3 rc=$rc" | tee -a "$OUT/rc-summary.txt"
  [[ $rc -eq 0 ]] || FAILED=1
fi

# ══ ⑧ A2 逐像素（Metal）═════════════════════════════════════════════════════
if [[ "$TARGET" == "all" || "$TARGET" == "regress" || "$TARGET" == "core" ]]; then
  run_suite "$OUT/regression-a2-metal.txt" env STELQUICK_A2_CHECK=1 "$BIN"
  rc=$?
  echo "regression-a2-metal rc=$rc" | tee -a "$OUT/rc-summary.txt"
  [[ $rc -eq 0 ]] || FAILED=1
fi

# ══ ⑨ DYN 双跑（真实引擎 + 替身判别性对照）══════════════════════════════════
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
echo "T33 证据已写入 $OUT/（FAILED=$FAILED）"
[[ $FAILED -eq 0 ]] || exit 1
