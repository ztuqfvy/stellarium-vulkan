#!/bin/zsh
# T40 验证脚本（2026-09-30）：**快捷键编辑页面**。
#
# 用法：tools/t40-verify.sh              （跑全部）
#       tools/t40-verify.sh pos 3        （只跑正题 SHORTCUTCHECK ×3）
#       tools/t40-verify.sh negctl       （只跑两组负控 ×2）
#       tools/t40-verify.sh regress      （只跑相邻回归）
#
# 证据落 docs/evidence/2026-09-30-t40-shortcuts/mac/。
# 环境口径（Metal + MoltenVK）**刻意与 t17..t39 完全一致，不换**。
#
# ── A-1.0 范围表的这一格 ────────────────────────────────────────────────────
# `|快捷键编辑、帮助、版本与许可证页面 | 可延后 | 必须 | 中文输入焦点不能触发天空快捷键|`
#   —— 第三列的约束**已由 T29 兜住**（`keySink` 的 Esc 分支先问
#   `ActionRouter::canDispatchToSky()`，中文输入焦点下天空快捷键不触发）；
#   **本任务交付第一列那个页面本体**（"帮助/版本/许可证"归 T41）。
#
# ── 这一轮要定的"性"（先探针、再产品、后判据）────────────────────────────────
# T40-A 探针（STELQUICK_SHORTCUT_PROBE）实测坐实 12 条（见 ShortcutProbe.hpp 头注）：
#   ① `QKeySequence` 往返恒等 14/15 —— 唯一不等的 `PageUp` 是 **Qt 键名别名**
#      （正名 `PgUp`；`PageUp`/`PageDown` 解析成**空序列**，`Escape`→`Esc`）
#      🔴 ⇒ **键名一律由 C++ 生成**，QML 绝不自拼；
#   ② 判"多键序列"只能用 `QKeySequence::count()` —— 注册表有 `,`（`Key_Comma`）
#      当主键的动作 ⇒ 用"串里有逗号"判会误计（探针首轮踩过）；
#   ③ `setShortcut()` **即时生效**，但**只发 `changed()`、不发 `shortcutsChanged()`**
#      ⇒ 下游要自己刷新；
#   ④ `saveShortcuts()` 落盘**只动 `shortcuts` 组**（非该组零差异＝不写穿物证）；
#   ⑤ **空串 = 移除**（落盘 `"" ""`，重启 split 出两段都回解成空序列）；
#   ⑥ 🔴 **落盘会规范化字符串**（`Ctrl+E, Ctrl+2` → `Ctrl+E,Ctrl+2`，分隔空格被删）
#      ⇒ 比"改没改成功"要**比序列语义**（`QKeySequence ==`），**不能比字符串**；
#   ⑦ `restoreDefaultShortcut` **安全**：整组重写但按"等于默认则跳过"剔除
#      ⇒ **不冲别人的自定义键**（⚠️ 探针首轮曾因两靶撞成同一动作得出相反结论）；
#   ⑧ 出厂注册表**无重复键**（133 种非空键 / 0 重复）；
#   ⑨ `setAllActionsEnabled(false)` 门**只挡 `pushKey`**，**`routeKey` 不查它**；
#   ⑩ 注册表 **505 动作 / 15 分组**；`getText()` 中文、`getGroup()` 英文 ⇒ 产品侧映射；
#   ⑪ "是否被用户改过"的真源 = `conf->contains("shortcuts/"+id)`（与引擎构造同判据）；
#      `defaultKeySequence` 是私有成员读不到 ⇒ UI **不显示"默认值"列**；
#   ⑫ 配置落点 = T36 个人版目录 `.../Stellarium-quick/config.ini`。
#
# ── 首轮 FAIL 反哺（这一条是**真产品缺陷**，判据抓出来的）──────────────────────
#   · SC-10 **真缺陷**：非法键名闸原本只查 `QKeySequence::count()`。而
#     `QKeySequence("PageUp").count()` 是 **1**（Qt 当"一个未知组合"存下）、
#     `toString()` 却是**空** ⇒ 闸放行、`norm` 变空串 ⇒ 引擎侧等于
#     **静默删除该快捷键**（正是探针①警告的形态）。修法：闸加 `|| norm.isEmpty()`。
#     ⚠️ 这个缺陷**只有 SC-10 的判别对照**（合法输入 vs 别名键名）才抓得到 ——
#     普通"设个键、读回来"的正测**永远绿**。
#   · 判据自身方法缺陷（非产品，四处，均已修）：
#     ① `Ctx::finish()` 用的是 `Ctx::onDone` 成员，`run()` 里**漏赋值** ⇒ 14 步
#        全跑完、收尾 `std::bad_function_call`、**零判据输出**（RC=134）；
#     ② 判据台账 id 用 `arg(i)` 生成 `SC-1..SC-14`（**无前导零**）而 note 首词是
#        `SC-01..SC-14` ⇒ 只有 SC-10..SC-14 匹配、**前 9 条 mark 静默丢失**
#        （现象：汇总"判据 5/5"却 VERDICT=FAIL）⇒ 台账改**显式 id 列表**；
#     ③ QML 绑定的 model 与判据自建副本是**两个实例**：自检路径走不到正常路径
#        末尾的 `shortcutModel.refresh()` ⇒ ListView 的 model 是空表、**一个 delegate
#        都不创建**（判据自建副本有 505 行，SC-01 照样绿，所以看不出来）；
#     ④ StackLayout 隐藏页 ListView **尺寸 0 ⇒ delegate 一个都不创建** ⇒ 切页后
#        要**等 ≥1 帧（400ms）**才有 delegate（T39 同款血泪）。
#
# ── 本脚本的判据分工（**内外两层，别互相顶替**）──────────────────────────────
#   内层（被测自检 SHORTCUTCHECK，**15 个 id / 14 条**）：见 app/ShortcutCheck.hpp。
#     表/口径 SC-01..SC-03 ｜ 键名生成 SC-04（含别名判别对照）｜ 写入 SC-05a/05b ｜
#     不写穿 SC-06 ｜ 冲突 SC-07 ｜ 单恢复 SC-08 ｜ 空串移除 SC-09 ｜ 非法键闸 SC-10 ｜
#     全恢复 SC-11 ｜ UI 面 SC-12 ｜ UI 端到端 SC-13 ｜ 复原 SC-14
#   外层（本脚本，**换来源的对照量**，陷阱 43）：
#     · 正题 ×3 全 PASS（rc=0 / 判据 15/15 / 红项空）；
#     · 负控 A `STELQUICK_SHORTCUT_WRITE_OFF=1`（`setKey` 不写引擎）
#       ⇒ 红 **[SC-05a,SC-05b,SC-07,SC-08,SC-09,SC-11,SC-13]**（rc=10 / 8/15）；
#     · 负控 B `STELQUICK_SHORTCUT_SAVE_OFF=1`（写引擎但不落盘）
#       ⇒ 红 **[SC-05b,SC-09,SC-11]**（rc=10 / 12/15）；
#     · 三组红项集合**两两不同**：∅ / A / B，且 **B ⊂ A**；
#     · 🔴 红项比靶心大是**步骤链的事实**不是判据缺陷：写引擎腿一断，后步的
#       "前提态"根本不存在 ⇒ 凡经由 `setKey` 承重或依赖前序写入态的判据必然红。
#       本脚本据此再钉一条**正交定位**断言：
#         A\B = {SC-05a,SC-07,SC-08,SC-13}（**引擎写入必须真生效**的集合）
#         B\A = ∅ 且 B = {SC-05b,SC-09,SC-11}（**断言磁盘态**的集合）
#       ⇒ 看红项落在哪半边即可判断坏在写入腿还是落盘腿。
#
# ── 环境门（沿用 t34..t39 的血泪，写进脚本而不是靠人记）──────────────────────
#   · 显示器休眠门照抄 t34..t39；
#   · **Spotlight 索引（mdbulkimport）= 负载毒源**：检测到就打环境注记，**不许洗 PASS**；
#   · 每个 app 运行都带**看门狗**：引导门挂死时强杀，rc=124 计失败；
#   · ⚠️ 连续起停会让 Metal 掉设备（`kIOGPUCommandBufferCallbackErrorInnocentVictim`，
#     rc=139 且**零判据输出**）—— 环境噪声**不是判据失败**，重跑即可；
#   · ⚠️ 本自检**会写用户配置**（个人版目录 `Stellarium-quick/config.ini` 的
#     `shortcuts` 组）：S11/S14 会清干净，但**中途强杀可能留试验键位** ⇒
#     重跑一次正题即可复原（S14 的整本指纹核对就是这条纪律的判据）。
set -u
cd /Users/ztuqfvy/qt_demo/stellarium_vulkan

export VK_DRIVER_FILES=/opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json
export QT_VULKAN_LIB=/opt/homebrew/opt/vulkan-loader/lib/libvulkan.1.dylib
export STELQUICK_GRAPHICS_API=metal

BIN=./build-release/src/ui/stelQuickUI.app/Contents/MacOS/stelQuickUI
ROOT=docs/evidence/2026-09-30-t40-shortcuts
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

# ── 显示器休眠门（照抄 t34..t39）────────────────────────────────────────────
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
judge_line() { /usr/bin/grep -E "SHORTCUTCHECK: .*判据 [0-9]+/[0-9]+" "$1" | LC_ALL=C /usr/bin/tail -1; }
verdict_line() { /usr/bin/grep -E "SHORTCUTCHECK: VERDICT=" "$1" | LC_ALL=C /usr/bin/tail -1; }
red_ids() {
  /usr/bin/grep -E "SHORTCUTCHECK: +\[FAIL\] " "$1" \
    | /usr/bin/sed -E 's/.*\[FAIL\] (SC-[0-9A-Za-z]+).*/\1/' \
    | LC_ALL=C /usr/bin/sort -u | /usr/bin/tr '\n' ',' | sed 's/,$//'
}
# 集合差（逗号串，已排序）：$1 - $2
set_minus() {
  local a=$1 b=$2 out=""
  local IFS=,
  for x in ${=a}; do
    [[ ",$b," == *",$x,"* ]] || out="$out${out:+,}$x"
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

# ══ ① 正题：SHORTCUTCHECK ×3（全绿才算数）════════════════════════════════════
POS_REDS=""
if [[ "$TARGET" == "all" || "$TARGET" == "pos" ]]; then
  echo "──────── 正题 SHORTCUTCHECK ×$POS_RUNS（预期 rc=0 / 判据 15/15 / 红项空）────────" \
    | tee -a "$OUT/rc-summary.txt"
  pos_ok=0
  for i in $(seq 1 $POS_RUNS); do
    run_suite "$OUT/shortcutcheck-run$i.txt" env STELQUICK_SHORTCUT_CHECK=1 "$BIN"
    rc=$?
    line=$(judge_line "$OUT/shortcutcheck-run$i.txt"); verd=$(verdict_line "$OUT/shortcutcheck-run$i.txt")
    reds=$(red_ids "$OUT/shortcutcheck-run$i.txt")
    echo "  · run$i rc=$rc（$verd；红项 [$reds]）" | tee -a "$OUT/rc-summary.txt"
    if [[ $rc -eq 0 && "$verd" == *"VERDICT=PASS"* && "$line" == *"判据 15/15"* && -z "$reds" ]]; then
      (( pos_ok++ ))
    else
      judge 1 "正题 run$i 不绿（rc=$rc $verd 红项 [$reds]）——ENV 劣化不许洗 PASS，查明再跑"
    fi
  done
  judge $([[ $pos_ok -eq $POS_RUNS ]] && echo 0 || echo 1) \
    "正题 $pos_ok/$POS_RUNS 全绿（判据 15/15 × $POS_RUNS）"
  POS_REDS=$(red_ids "$OUT/shortcutcheck-run1.txt")
  echo "  · run1 判据全文（见 $OUT/shortcutcheck-run1.txt）：" | tee -a "$OUT/rc-summary.txt"
  /usr/bin/grep -E "^SHORTCUTCHECK: " "$OUT/shortcutcheck-run1.txt" | sed 's/^/      /' \
    | tee -a "$OUT/rc-summary.txt"
fi

NEG_A_REDS="SC-05a,SC-05b,SC-07,SC-08,SC-09,SC-11,SC-13"
NEG_B_REDS="SC-05b,SC-09,SC-11"

# ══ ② 负控 A：`setKey` 不写引擎 ⇒ 红 [SC-05a…]（**含下游连锁，见头注**）×2 ═════
if [[ "$TARGET" == "all" || "$TARGET" == "negctl" ]]; then
  echo "──────── 负控 A STELQUICK_SHORTCUT_WRITE_OFF=1 ×$NEG_RUNS（预期 rc=10 / 8/15 / 红项 [$NEG_A_REDS]）────────" \
    | tee -a "$OUT/rc-summary.txt"
  nega_ok=0
  for i in $(seq 1 $NEG_RUNS); do
    run_suite "$OUT/negctl-writeoff-run$i.txt" env STELQUICK_SHORTCUT_WRITE_OFF=1 \
      STELQUICK_SHORTCUT_CHECK=1 "$BIN"
    rc=$?
    line=$(judge_line "$OUT/negctl-writeoff-run$i.txt"); verd=$(verdict_line "$OUT/negctl-writeoff-run$i.txt")
    reds=$(red_ids "$OUT/negctl-writeoff-run$i.txt")
    echo "  · run$i rc=$rc（$verd；红项 [$reds]）" | tee -a "$OUT/rc-summary.txt"
    if [[ $rc -eq 10 && "$line" == *"判据 8/15"* && "$reds" == "$NEG_A_REDS" ]]; then
      (( nega_ok++ ))
    else
      judge 1 "负控 A run$i 红项不是 [$NEG_A_REDS]（rc=$rc $line $reds）——负控必须命中写入腿承重项"
    fi
  done
  judge $([[ $nega_ok -eq $NEG_RUNS ]] && echo 0 || echo 1) \
    "负控 A $nega_ok/$NEG_RUNS 命中 [$NEG_A_REDS]（写入腿承重证明）"
  echo "  负控 A 的红是**预期**结果（引擎写入被关 ⇒ 凡经 setKey 承重/依赖前序写入态的判据必红）；读数：" \
    | tee -a "$OUT/rc-summary.txt"
  /usr/bin/grep -E "^SHORTCUTCHECK: +\[FAIL\]|VERDICT" "$OUT/negctl-writeoff-run1.txt" | sed 's/^/      /' \
    | tee -a "$OUT/rc-summary.txt"
  NEG_A_REDS=$(red_ids "$OUT/negctl-writeoff-run1.txt")

  # ══ ③ 负控 B：写引擎但不落盘 ⇒ 红 [SC-05b,SC-09,SC-11] ×2 ═════════════════
  echo "──────── 负控 B STELQUICK_SHORTCUT_SAVE_OFF=1 ×$NEG_RUNS（预期 rc=10 / 12/15 / 红项 [$NEG_B_REDS]）────────" \
    | tee -a "$OUT/rc-summary.txt"
  negb_ok=0
  for i in $(seq 1 $NEG_RUNS); do
    run_suite "$OUT/negctl-saveoff-run$i.txt" env STELQUICK_SHORTCUT_SAVE_OFF=1 \
      STELQUICK_SHORTCUT_CHECK=1 "$BIN"
    rc=$?
    line=$(judge_line "$OUT/negctl-saveoff-run$i.txt"); verd=$(verdict_line "$OUT/negctl-saveoff-run$i.txt")
    reds=$(red_ids "$OUT/negctl-saveoff-run$i.txt")
    echo "  · run$i rc=$rc（$verd；红项 [$reds]）" | tee -a "$OUT/rc-summary.txt"
    if [[ $rc -eq 10 && "$line" == *"判据 12/15"* && "$reds" == "$NEG_B_REDS" ]]; then
      (( negb_ok++ ))
    else
      judge 1 "负控 B run$i 红项不是 [$NEG_B_REDS]（rc=$rc $line $reds）——负控必须命中落盘腿承重项"
    fi
  done
  judge $([[ $negb_ok -eq $NEG_RUNS ]] && echo 0 || echo 1) \
    "负控 B $negb_ok/$NEG_RUNS 命中 [$NEG_B_REDS]（落盘腿承重证明）"
  echo "  负控 B 的红是**预期**结果（落盘被关 ⇒ 断言磁盘态的判据读不到内容）；读数：" \
    | tee -a "$OUT/rc-summary.txt"
  /usr/bin/grep -E "^SHORTCUTCHECK: +\[FAIL\]|VERDICT" "$OUT/negctl-saveoff-run1.txt" | sed 's/^/      /' \
    | tee -a "$OUT/rc-summary.txt"
  NEG_B_REDS=$(red_ids "$OUT/negctl-saveoff-run1.txt")

  # ══ ④ 两腿**正交定位**断言（红项集合不只是"不同"，还要**指向不同的腿**）═══
  echo "──────── 两腿正交定位（A\\B = 引擎写入生效组；B\\A 应空）────────" \
    | tee -a "$OUT/rc-summary.txt"
  ab=$(set_minus "$NEG_A_REDS" "$NEG_B_REDS")
  ba=$(set_minus "$NEG_B_REDS" "$NEG_A_REDS")
  echo "  · A\\B = [$ab]（期望 SC-05a,SC-07,SC-08,SC-13）" | tee -a "$OUT/rc-summary.txt"
  echo "  · B\\A = [$ba]（期望：空 —— B 的每一项都同时被 A 打中）" | tee -a "$OUT/rc-summary.txt"
  judge $([[ "$ab" == "SC-05a,SC-07,SC-08,SC-13" && -z "$ba" ]] && echo 0 || echo 1) \
    "两腿正交定位成立（写入腿 Δ=A\\B｜落盘腿 B）"
  # ⚠️ 正题的期望是**空集**（全绿）——首版这里写成 `-n`（要求非空）是**脚本自身的缺陷**
  #    （负控对照的第一条要求就是"正题必须与负控不同"，而正题的"不同"就是**空**）。
  judge $([[ -z "$POS_REDS" && "$NEG_A_REDS" != "$NEG_B_REDS" ]] && echo 0 || echo 1) \
    "三组红项集合两两不同（正题 [${POS_REDS:-∅}] ｜ A [$NEG_A_REDS] ｜ B [$NEG_B_REDS]）"
fi

# ══ ⑤ 相邻回归（十六套件 + INTERACTCHECK + A2 + DYN 双路 + S3）══════════════
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
    "STELQUICK_HIDPI_CHECK=1:hidpicheck"
  do
    var=${spec%%:*}; name=${spec##*:}
    run_suite "$OUT/regression-$name.txt" env $var "$BIN"
    rc=$?
    echo "regression-$name rc=$rc" | tee -a "$OUT/rc-summary.txt"
    [[ $rc -eq 0 ]] || FAILED=1
  done

  # INTERACTCHECK：依赖窗口真的拿到系统焦点，ENV-SKIP 通道照抄 t36..t39
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
