#!/bin/zsh
# T41 验证脚本（2026-09-30）：**帮助 / 版本 / 许可证页**。
#
# 用法：tools/t41-verify.sh              （跑全部）
#       tools/t41-verify.sh pos 3        （只跑正题 HELPCHECK ×3）
#       tools/t41-verify.sh negctl       （只跑两组负控 ×2）
#       tools/t41-verify.sh regress      （只跑相邻回归）
#
# 证据落 docs/evidence/2026-09-30-t41-help/mac/。
# 环境口径（Metal + MoltenVK）**刻意与 t17..t40 完全一致，不换**。
#
# ── A-1.0 范围表的这一格 ────────────────────────────────────────────────────
# `|快捷键编辑、帮助、版本与许可证页面 | 可延后 | 必须 | 中文输入焦点不能触发天空快捷键|`
#   —— 第三列的约束**已由 T29 兜住**；"快捷键编辑"由 T40 交付。
#   **本任务交付第一列剩下的三项**（帮助 / 版本 / 许可证）。
#
# ── 这一轮要定的"性"（先探针、再产品、后判据）────────────────────────────────
# T41-A 探针（STELQUICK_HELP_PROBE）三轮迭代实测（见 HelpProbe.hpp 头注与
# docs/evidence/2026-09-30-t41-help/mac/probe-help-mac.txt），**三处开工推断被推翻**：
#   ① `src/gui/` **编进了** stelQuickUI（随 `stelMain` 静态库，src/CMakeLists.txt:330）
#      —— "src/ui 的源表里没有" ≠ "运行时不存在"；
#   ② 老宿主 8 个窗口动作**全在注册表且中文**（F1 说明 / F2 设置 / F3 搜索 /
#      F4 星空及显示 / F5 日期时间 / F6 所在地点 / F7 快捷键窗口 / F10 天文计算 /
#      F12 脚本控制台；组=Windows）；
#   ③ 🔴 **F1 trigger() 真的会弹出老 QWidget 对话框**（QWidget 12→58，Δ46 常驻；
#      setVisible(false) 只隐藏不销毁）⇒ T41 必须**接管**而不是"自建入口并共存"。
# 其余关键口径：`QCoreApplication::applicationVersion()` 是「1.0.0」⇒ 版本页必须走
#   `StelUtils::getApplicationVersion()`；**COPYING 不在 bundle 里**（findFile 命中
#   "./COPYING" 纯因 cwd 恰为源码树根 ⇒ 分发必然失效）⇒ GPL 全文**编进 qrc**；
#   贡献者 246 条去重后 245、排序是**大小写不敏感**（首项 adalava）。
#
# ── 本轮判据抓出/修掉的东西 ─────────────────────────────────────────────────
#   · 🔴 **真产品缺陷**（T41-B 冒烟抓帧抓到）：`BackendInfo::applyRuntimeApi` 自判
#     `m_backendOk = (apiName == "Vulkan")` —— 判据字符串写死，T39 转 Metal 后
#     **恒 false** ⇒ 工具栏标签/诊断页/关于页状态色全错（C++ 日志明明 backendOk=1）。
#     修法：BackendInfo 不再自判，main.cpp 经 `applyBackendResult(name, ok)` 把
#     自己的判定（`api == wantedApi`）传入 —— **判定只有一份**。
#   · **真产品缺陷**（T41-C 判据首轮实抓）：helpGotoShortcutsButton 沉底
#     （中心 y=1036 > 窗口 640）⇒ **用户也点不到** ⇒ 按钮挪到页首（查键位是高频动作）。
#   · 判据自身方法缺陷（非产品，均已修）：
#     ① HC-11/HC-13 首轮 NA —— aboutLicenseButton 同样沉底（y=879）⇒ 判据加
#        `ensureVisible()`（沿父链找 Flickable，把 contentY 滚到目标居中）；
#     ② HC-17 首轮红 —— 基线可见顶层混入 **QSplashScreen/QProgressBar**（启动
#        瞬态，引擎引导完成后消失）⇒ 快照排除这两类；
#     ③ 🔴 **HC-16 在负控 A 下假绿** —— HC-15 判别对照的恢复段无条件
#        `setHostTakeover(id,true)`，把被负控打断的路径**自己修好了**（hostActionRequested
#        0→1、index=7，看着像接管生效，其实是判据刚注册的接管在干活）⇒ 恢复只
#        恢复到**布场时的状态**（负控 A 下布场就没有接管）；
#     ④ HC-17 曾被 HC-16 透传弹出的可见 QDialog 连带红 ⇒ HC-16 末尾补
#        "每步自己收尾"的清理 —— 后面的判据只反映自己那件事。
#
# ── 本脚本的判据分工（**内外两层，别互相顶替**）──────────────────────────────
#   内层（被测自检 HELPCHECK，**17 条**）：见 app/HelpCheck.hpp。
#     版本 HC-01（含源分离判别对照）｜ 系统 HC-02 ｜ 贡献者 HC-03 ｜ 许可证内容
#     HC-04 ｜ 资源 HC-05（md5 == 磁盘 COPYING）｜ 手势 HC-06 ｜ 外链白名单 HC-07
#     （含表外必拒对照）｜ 键名平台化 HC-08 ｜ 导航 HC-09 ｜ 帮助页 HC-10 ｜
#     跳转 HC-11 ｜ 关于页 HC-12 ｜ 许可证端到端 HC-13 ｜ 接管表 HC-14 ｜
#     接管生效 HC-15（含"撤销接管必弹老对话框"的判别对照）｜ 键盘路径 HC-16 ｜ 复原 HC-17
#   外层（本脚本，**换来源的对照量**，陷阱 43）：
#     · 正题 ×3 全 PASS（rc=0 / 判据 17/17 / 红项空）；
#     · 负控 A `STELQUICK_HELP_TAKEOVER_OFF=1`（main.cpp 不注册接管）
#       ⇒ 红 **[HC-14,HC-15,HC-16]**（rc=10 / 14/17）；
#     · 负控 B `STELQUICK_HELP_LICENSE_OFF=1`（HelpModel 不读 GPL）
#       ⇒ 红 **[HC-04,HC-13]**（rc=10 / 15/17）；
#     · ⚠️ 负控 B 下 **HC-05 不红是对的**（它验"编进 qrc 没有"= 资源存在性，
#       负控打的是"加载"这条腿 —— 两件事）。首版预期（推理填的）把 HC-05 写进
#       B 的红项，实测没有，按实跑改写（T40 教训的重演）；
#     · **两腿正交**：A\B = {HC-14,15,16}（**接管域**）、B\A = {HC-04,13}
#       （**许可证域**）、无交集 ⇒ 看红项落哪半边即知坏在哪个环节。
#
# ── 环境门（沿用 t34..t40 的血泪，写进脚本而不是靠人记）──────────────────────
#   · 显示器休眠门照抄 t34..t40；
#   · **Spotlight 索引（mdbulkimport）= 负载毒源**：检测到就打环境注记，**不许洗 PASS**；
#   · 每个 app 运行都带**看门狗**：引导门挂死时强杀，rc=124 计失败；
#   · ⚠️ 连续起停会让 Metal 掉设备（rc=139 且零判据输出）—— 环境噪声不是判据失败；
#   · 本自检**全程只读**用户配置（HC-17 核对整本指纹）。
set -u
cd /Users/ztuqfvy/qt_demo/stellarium_vulkan

export VK_DRIVER_FILES=/opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json
export QT_VULKAN_LIB=/opt/homebrew/opt/vulkan-loader/lib/libvulkan.1.dylib
export STELQUICK_GRAPHICS_API=metal

BIN=./build-release/src/ui/stelQuickUI.app/Contents/MacOS/stelQuickUI
ROOT=docs/evidence/2026-09-30-t41-help
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

# ── 显示器休眠门（照抄 t34..t40）────────────────────────────────────────────
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
judge_line() { /usr/bin/grep -E "HELPCHECK: .*判据 [0-9]+/[0-9]+" "$1" | LC_ALL=C /usr/bin/tail -1; }
verdict_line() { /usr/bin/grep -E "HELPCHECK: VERDICT=" "$1" | LC_ALL=C /usr/bin/tail -1; }
red_ids() {
  /usr/bin/grep -E "HELPCHECK: +\[FAIL\] " "$1" \
    | /usr/bin/sed -E 's/.*\[FAIL\] (HC-[0-9A-Za-z]+).*/\1/' \
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

# ══ ① 正题：HELPCHECK ×3（全绿才算数）════════════════════════════════════════
POS_REDS=""
if [[ "$TARGET" == "all" || "$TARGET" == "pos" ]]; then
  echo "──────── 正题 HELPCHECK ×$POS_RUNS（预期 rc=0 / 判据 17/17 / 红项空）────────" \
    | tee -a "$OUT/rc-summary.txt"
  pos_ok=0
  for i in $(seq 1 $POS_RUNS); do
    run_suite "$OUT/helpcheck-run$i.txt" env STELQUICK_HELP_CHECK=1 "$BIN"
    rc=$?
    line=$(judge_line "$OUT/helpcheck-run$i.txt"); verd=$(verdict_line "$OUT/helpcheck-run$i.txt")
    reds=$(red_ids "$OUT/helpcheck-run$i.txt")
    echo "  · run$i rc=$rc（$verd；红项 [$reds]）" | tee -a "$OUT/rc-summary.txt"
    if [[ $rc -eq 0 && "$verd" == *"VERDICT=PASS"* && "$line" == *"判据 17/17"* && -z "$reds" ]]; then
      (( pos_ok++ ))
    else
      judge 1 "正题 run$i 不绿（rc=$rc $verd 红项 [$reds]）——ENV 劣化不许洗 PASS，查明再跑"
    fi
  done
  judge $([[ $pos_ok -eq $POS_RUNS ]] && echo 0 || echo 1) \
    "正题 $pos_ok/$POS_RUNS 全绿（判据 17/17 × $POS_RUNS）"
  POS_REDS=$(red_ids "$OUT/helpcheck-run1.txt")
  echo "  · run1 判据全文（见 $OUT/helpcheck-run1.txt）：" | tee -a "$OUT/rc-summary.txt"
  /usr/bin/grep -E "^HELPCHECK: " "$OUT/helpcheck-run1.txt" | sed 's/^/      /' \
    | tee -a "$OUT/rc-summary.txt"
fi

NEG_A_REDS="HC-14,HC-15,HC-16"
NEG_B_REDS="HC-04,HC-13"

# ══ ② 负控 A：不注册接管 ⇒ 红 [HC-14,15,16] ×2 ═══════════════════════════════
if [[ "$TARGET" == "all" || "$TARGET" == "negctl" ]]; then
  echo "──────── 负控 A STELQUICK_HELP_TAKEOVER_OFF=1 ×$NEG_RUNS（预期 rc=10 / 14/17 / 红项 [$NEG_A_REDS]）────────" \
    | tee -a "$OUT/rc-summary.txt"
  nega_ok=0
  for i in $(seq 1 $NEG_RUNS); do
    run_suite "$OUT/negctl-takeoveroff-run$i.txt" env STELQUICK_HELP_TAKEOVER_OFF=1 \
      STELQUICK_HELP_CHECK=1 "$BIN"
    rc=$?
    line=$(judge_line "$OUT/negctl-takeoveroff-run$i.txt"); verd=$(verdict_line "$OUT/negctl-takeoveroff-run$i.txt")
    reds=$(red_ids "$OUT/negctl-takeoveroff-run$i.txt")
    echo "  · run$i rc=$rc（$verd；红项 [$reds]）" | tee -a "$OUT/rc-summary.txt"
    if [[ $rc -eq 10 && "$line" == *"判据 14/17"* && "$reds" == "$NEG_A_REDS" ]]; then
      (( nega_ok++ ))
    else
      judge 1 "负控 A run$i 红项不是 [$NEG_A_REDS]（rc=$rc $line $reds）——负控必须命中接管域承重项"
    fi
  done
  judge $([[ $nega_ok -eq $NEG_RUNS ]] && echo 0 || echo 1) \
    "负控 A $nega_ok/$NEG_RUNS 命中 [$NEG_A_REDS]（接管域承重证明）"
  echo "  负控 A 的红是**预期**结果（接管未注册 ⇒ F1 透传引擎 ⇒ 老对话框弹出）；读数：" \
    | tee -a "$OUT/rc-summary.txt"
  /usr/bin/grep -E "^HELPCHECK: +\[FAIL\]|VERDICT" "$OUT/negctl-takeoveroff-run1.txt" | sed 's/^/      /' \
    | tee -a "$OUT/rc-summary.txt"
  NEG_A_REDS=$(red_ids "$OUT/negctl-takeoveroff-run1.txt")

  # ══ ③ 负控 B：不读 GPL ⇒ 红 [HC-04,HC-13] ×2 ═══════════════════════════════
  echo "──────── 负控 B STELQUICK_HELP_LICENSE_OFF=1 ×$NEG_RUNS（预期 rc=10 / 15/17 / 红项 [$NEG_B_REDS]）────────" \
    | tee -a "$OUT/rc-summary.txt"
  negb_ok=0
  for i in $(seq 1 $NEG_RUNS); do
    run_suite "$OUT/negctl-licenseoff-run$i.txt" env STELQUICK_HELP_LICENSE_OFF=1 \
      STELQUICK_HELP_CHECK=1 "$BIN"
    rc=$?
    line=$(judge_line "$OUT/negctl-licenseoff-run$i.txt"); verd=$(verdict_line "$OUT/negctl-licenseoff-run$i.txt")
    reds=$(red_ids "$OUT/negctl-licenseoff-run$i.txt")
    echo "  · run$i rc=$rc（$verd；红项 [$reds]）" | tee -a "$OUT/rc-summary.txt"
    if [[ $rc -eq 10 && "$line" == *"判据 15/17"* && "$reds" == "$NEG_B_REDS" ]]; then
      (( negb_ok++ ))
    else
      judge 1 "负控 B run$i 红项不是 [$NEG_B_REDS]（rc=$rc $line $reds）——负控必须命中许可证域承重项"
    fi
  done
  judge $([[ $negb_ok -eq $NEG_RUNS ]] && echo 0 || echo 1) \
    "负控 B $negb_ok/$NEG_RUNS 命中 [$NEG_B_REDS]（许可证域承重证明；HC-05 不红是对的——资源存在性与加载行为分离）"
  echo "  负控 B 的红是**预期**结果（加载被关 ⇒ 断言许可证内容的判据读不到内容）；读数：" \
    | tee -a "$OUT/rc-summary.txt"
  /usr/bin/grep -E "^HELPCHECK: +\[FAIL\]|VERDICT" "$OUT/negctl-licenseoff-run1.txt" | sed 's/^/      /' \
    | tee -a "$OUT/rc-summary.txt"
  NEG_B_REDS=$(red_ids "$OUT/negctl-licenseoff-run1.txt")

  # ══ ④ 两腿**正交定位**断言（红项集合不只是"不同"，还要**指向不同的域**）═══
  echo "──────── 两腿正交定位（A\\B = 接管域；B\\A = 许可证域，应无交集）────────" \
    | tee -a "$OUT/rc-summary.txt"
  ab=$(set_minus "$NEG_A_REDS" "$NEG_B_REDS")
  ba=$(set_minus "$NEG_B_REDS" "$NEG_A_REDS")
  echo "  · A\\B = [$ab]（期望 HC-14,HC-15,HC-16）" | tee -a "$OUT/rc-summary.txt"
  echo "  · B\\A = [$ba]（期望 HC-04,HC-13）" | tee -a "$OUT/rc-summary.txt"
  judge $([[ "$ab" == "HC-14,HC-15,HC-16" && "$ba" == "HC-04,HC-13" ]] && echo 0 || echo 1) \
    "两腿正交定位成立（A\\B=接管域｜B\\A=许可证域，无交集）"
  # ⚠️ 正题的期望是**空集**（全绿）——t40 首版在这里写成 `-n`（要求非空）是脚本自身的
  #    缺陷；本轮直接写对（-z）。
  judge $([[ -z "$POS_REDS" && "$NEG_A_REDS" != "$NEG_B_REDS" ]] && echo 0 || echo 1) \
    "三组红项集合两两不同（正题 [${POS_REDS:-∅}] ｜ A [$NEG_A_REDS] ｜ B [$NEG_B_REDS]）"
fi

# ══ ⑤ 相邻回归（十七套件 + INTERACTCHECK + A2 + DYN 双路 + S3）══════════════
# T41 改动面：ActionRouter（接管 + routeKey 查表分支）、BackendInfo（backendOk 判定）、
# main.cpp（装配）、Toolbar.qml（+2 导航按钮）、MainWindow.qml（+2 页 + hostActionPages）⇒
# toolbarcheck / actioncheck / **shortcutcheck**（T40 的套件 —— routeKey 正是它的
# SC-14 交互腿的路径，与 T40 把 hidpicheck 纳入回归同理）是最近邻，其余为全量回归。
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
    "STELQUICK_SHORTCUT_CHECK=1:shortcutcheck"
  do
    var=${spec%%:*}; name=${spec##*:}
    run_suite "$OUT/regression-$name.txt" env $var "$BIN"
    rc=$?
    echo "regression-$name rc=$rc" | tee -a "$OUT/rc-summary.txt"
    [[ $rc -eq 0 ]] || FAILED=1
  done

  # INTERACTCHECK：依赖窗口真的拿到系统焦点，ENV-SKIP 通道照抄 t36..t40
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
