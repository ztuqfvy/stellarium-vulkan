#!/bin/zsh
# T39 验证脚本（2026-09-30）：**高 DPI + 渲染诊断**。
#
# 用法：tools/t39-verify.sh              （跑全部）
#       tools/t39-verify.sh pos 3        （只跑正题 HIDPICHECK ×3）
#       tools/t39-verify.sh negctl       （只跑两组负控 ×2）
#       tools/t39-verify.sh regress      （只跑相邻回归）
#
# 证据落 docs/evidence/2026-09-30-t39-hidpi/mac/。
# 环境口径（Metal + MoltenVK）**刻意与 t17..t38 完全一致，不换**。
#
# ── A-1.0 范围表的这两格 ────────────────────────────────────────────────────
# `|亮度/星等、视场、投影、主题/夜视、高 DPI|基础|必须|` —— T37 做掉"主题/夜视"、
#   T38 做掉前三项，**"高 DPI"是本轮**（该格至此清空）；
# `|资源路径、错误提示、渲染诊断、配置保存|最小|必须|` —— T36 只做掉"配置保存"，
#   **"渲染诊断"是本轮**（同格余下"资源路径/错误提示"归 T41/T42）。
#
# ── 这一轮要定的"性"（先探针、再产品、后判据）────────────────────────────────
# T39-A 探针（STELQUICK_HIDPI_PROBE）实测坐实 7 条：
#   ① 三个高 DPI setter（screenFontSize / guiFontSize / screenButtonScale）
#      **一个都不夹取**（写 99/99/999 原样回读，还顺手 immediateSave 落盘）
#      ⇒ **范围闸必须在产品侧**（AppFacade，同一把 `STELQUICK_DISPLAY_GATE_OFF`
#      负控与 T38 共用）；
#   ② 派生量算术全对（`getScreenScale() = dpp × 字号/13`）；
#   ③ 🔴 `getGuiFontSize()` **读的就是 `QGuiApplication::font().pixelSize()`**、
#      `setGuiFontSize()` **改的是全局字体** —— 它不是"用户设定值"的可靠真源
#      （T38「读错量的名字」同族）⇒ 判据**只读写 façade 值 + 引擎 getter 独立回读**；
#   ④ 🔴 `setGuiFontSize(25)` 后 QML 视觉树 **325 项一项都不跟随**（三重仪器正控
#      验证完备：读取链路能读到 / 真实树人为扰动恰查 1 项 / 改全局 pointSize 同样
#      0 跟随）⇒ 引擎的 guiFontSize 只作用于老 QWidget，**QML 缩放必须产品侧自己做
#      系数** ⇒ DisplayPage 上**只给 screenFontSize 一个控件，并诚实标注作用域**
#      （guiFontSize / screenButtonScale 的 SpinBox 不搬 —— 搬过来就是假控件）；
#   ⑤ `screenFontSize` 有**强帧效应**：噪声底**逐位相同（哈希一致）** → 字号 13→40
#      差异 **4.054%**（Δmax 247），还原后**逐位回到原哈希** ⇒ 帧级判据信噪比充足；
#   ⑥ 静置 1.5s 三条 NOTIFY 增量 0 ⇒ 这些量**不是**每帧变的连续量 ⇒ 滑块可安全绑定；
#   ⑦ `FrameMailbox::Stats` 全字段可读（published/dropped/leased/completeSlots/
#      readersHeld/latestFrameAgeMs/bytesPerFrame）⇒ **渲染诊断有真数据面可暴露**。
#      ⚠️ `takeLatestFrame()` 会让 `leased +1`（**诊断污染被诊断的量**）⇒ 判据刻意
#      **不报帧宽高**，`bytesPerFrame` 用抓帧的 FrameSample 独立验算。
#
# ── 首轮 FAIL 反哺（这一条是**真产品缺陷**，判据抓出来的）──────────────────────
#   · HP-08 **真缺陷**：`DisplayPage.qml` 的 `onMoved` 原本写
#     `appFacade.setScreenFontSize(Math.round(value))` —— `Q_PROPERTY` 的 **WRITE
#     函数不是 Q_INVOKABLE / slot**，QML 侧调用它**静默**
#     `TypeError: Property 'setScreenFontSize' ... is not a function`
#     ⇒ 用户拖滑块 UI 数字会动、**引擎纹丝不动**（点击后 UI value=38 而 façade=30）。
#     修法：改**赋值语法** `appFacade.screenFontSize = Math.round(value)`。
#     ⚠️ 这个缺陷只有 **HP-08 的真实点击**（不是 `invokeMethod("increase")`）才抓得到
#     —— T38 五个滑块恰好用的就是赋值语法，所以没同病。
#   · 另一处方法缺陷（非产品）：`QMetaObject::invokeMethod(slider, "increase")` **不发
#     moved 信号**（只有真实鼠标输入才发）⇒ 交互腿必须 **真实点击**（clickAt 最外层注入）；
#     且**隐藏页不收事件** ⇒ 先真实点导航按钮切页；`isVisible()` **不看 ScrollView 视口
#     裁剪** ⇒ 点击前须滚进视口；找 Flickable **不能从 root DFS**（StackLayout 所有页都
#     在视觉树，会摸到别页的）⇒ 沿**视觉父链**向上找（findAncestorFlickable）。
#   · 口径校正：判据清单**就是 12 条**（HP-00..HP-11），`mark()` 按 id 去重
#     （HP-02/03/04 各有上沿+下沿两次 mark，同一 id）⇒ 分母是 **/12**，不是 /14。
#     本脚本的期望值**全部按实跑读数写死**，不照抄上一轮的印象。
#
# ── 本脚本的判据分工（**内外两层，别互相顶替**）──────────────────────────────
#   内层（被测自检 HIDPICHECK，12 条）：见 app/HiDpiCheck.hpp。
#     状态面 HP-00..HP-05 ｜ UI 腿 HP-06..HP-08 ｜ 帧级 HP-09 ｜ 数据面/复原 HP-10/HP-11
#   外层（本脚本，**换来源的对照量**，陷阱 43）：
#     · 正题 ×3 全 PASS（rc=0 / 判据 12/12 / 红项空）；
#     · 负控 A `STELQUICK_DISPLAY_GATE_OFF=1`（关范围闸）
#       ⇒ **恰好**红 [HP-02,HP-03,HP-04]（rc=10 / 9/12）—— 且读数自带证据：
#          "写 99 ⇒ 引擎收到 99"（三个量都不夹取，探针结论①现场复现）；
#     · 负控 B `STELQUICK_DISPLAY_FWD_OFF=1`（断引擎→façade **订阅**）
#       ⇒ **恰好**红 [HP-07,HP-08]（rc=10 / 10/12）—— 引擎侧改动刷不到 UI
#         （HP-08 读数形如"UI value=37 不跟随"：交互本身是活的，死的是绑定）；
#     · 三组红项集合**两两不同**：∅ / {HP-02,HP-03,HP-04} / {HP-07,HP-08}。
#
# ── 环境门（沿用 t34..t38 的血泪，写进脚本而不是靠人记）──────────────────────
#   · 显示器休眠门照抄 t34..t38；
#   · **Spotlight 索引（mdbulkimport）= 负载毒源**：高负载下出撕裂帧（T37-X2）、
#     噪声底失效、引导门挂死（T37-X3）。检测到就打环境注记，**不许洗 PASS**；
#   · 每个 app 运行都带**看门狗**：引导门挂死时强杀，rc=124 计失败；
#   · ⚠️ 连续起停会让 Metal 掉设备（`kIOGPUCommandBufferCallbackErrorInnocentVictim`，
#     rc=139 且**零判据输出**）—— 这是环境噪声**不是判据失败**，重跑即可。
set -u
cd /Users/ztuqfvy/qt_demo/stellarium_vulkan

export VK_DRIVER_FILES=/opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json
export QT_VULKAN_LIB=/opt/homebrew/opt/vulkan-loader/lib/libvulkan.1.dylib
export STELQUICK_GRAPHICS_API=metal

BIN=./build-release/src/ui/stelQuickUI.app/Contents/MacOS/stelQuickUI
ROOT=docs/evidence/2026-09-30-t39-hidpi
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

# ── 显示器休眠门（照抄 t34..t38）────────────────────────────────────────────
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
judge_line() { /usr/bin/grep -E "HIDPICHECK: .*判据 [0-9]+/[0-9]+" "$1" | LC_ALL=C /usr/bin/tail -1; }
verdict_line() { /usr/bin/grep -E "HIDPICHECK: VERDICT=" "$1" | LC_ALL=C /usr/bin/tail -1; }
# ⚠️ `sort -u`：HP-02/03/04 各有上沿+下沿两行 FAIL，按 id **去重**后才是判据台账口径。
red_ids() {
  /usr/bin/grep -E "HIDPICHECK: +\[FAIL\] " "$1" \
    | /usr/bin/sed -E 's/.*\[FAIL\] (HP-[0-9A-Za-z]+).*/\1/' \
    | LC_ALL=C /usr/bin/sort -u | /usr/bin/tr '\n' ',' | sed 's/,$//'
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

# ══ ① 正题：HIDPICHECK ×3（全绿才算数）══════════════════════════════════════
if [[ "$TARGET" == "all" || "$TARGET" == "pos" ]]; then
  echo "──────── 正题 HIDPICHECK ×$POS_RUNS（预期 rc=0 / 判据 12/12 / 红项空）────────" \
    | tee -a "$OUT/rc-summary.txt"
  local_ok=0
  for i in $(seq 1 $POS_RUNS); do
    run_suite "$OUT/hidpicheck-run$i.txt" env STELQUICK_HIDPI_CHECK=1 "$BIN"
    rc=$?
    line=$(judge_line "$OUT/hidpicheck-run$i.txt"); verd=$(verdict_line "$OUT/hidpicheck-run$i.txt")
    reds=$(red_ids "$OUT/hidpicheck-run$i.txt")
    echo "  · run$i rc=$rc（$verd；红项 [$reds]）" | tee -a "$OUT/rc-summary.txt"
    if [[ $rc -eq 0 && "$verd" == *"VERDICT=PASS"* && "$line" == *"判据 12/12"* && -z "$reds" ]]; then
      (( local_ok++ ))
    else
      judge 1 "正题 run$i 不绿（rc=$rc $verd 红项 [$reds]）——ENV 劣化不许洗 PASS，查明再跑"
    fi
  done
  judge $([[ $local_ok -eq $POS_RUNS ]] && echo 0 || echo 1) \
    "正题 $local_ok/$POS_RUNS 全绿（判据 12/12 × $POS_RUNS）"
  echo "  · run1 判据全文（见 $OUT/hidpicheck-run1.txt）：" | tee -a "$OUT/rc-summary.txt"
  /usr/bin/grep -E "^HIDPICHECK: " "$OUT/hidpicheck-run1.txt" | sed 's/^/      /' \
    | tee -a "$OUT/rc-summary.txt"
fi

# ══ ② 负控 A：关范围闸 ⇒ 恰好红 [HP-02,HP-03,HP-04] ×2 ═══════════════════════
if [[ "$TARGET" == "all" || "$TARGET" == "negctl" ]]; then
  echo "──────── 负控 A STELQUICK_DISPLAY_GATE_OFF=1 ×$NEG_RUNS（预期 rc=10 / 9/12 / 红项 [HP-02,HP-03,HP-04]）────────" \
    | tee -a "$OUT/rc-summary.txt"
  nega_ok=0
  for i in $(seq 1 $NEG_RUNS); do
    run_suite "$OUT/negctl-gateoff-run$i.txt" env STELQUICK_DISPLAY_GATE_OFF=1 \
      STELQUICK_HIDPI_CHECK=1 "$BIN"
    rc=$?
    line=$(judge_line "$OUT/negctl-gateoff-run$i.txt"); verd=$(verdict_line "$OUT/negctl-gateoff-run$i.txt")
    reds=$(red_ids "$OUT/negctl-gateoff-run$i.txt")
    echo "  · run$i rc=$rc（$verd；红项 [$reds]）" | tee -a "$OUT/rc-summary.txt"
    if [[ $rc -eq 10 && "$line" == *"判据 9/12"* && "$reds" == "HP-02,HP-03,HP-04" ]]; then
      (( nega_ok++ ))
    else
      judge 1 "负控 A run$i 红项不是恰好 [HP-02,HP-03,HP-04]（rc=$rc $line $reds）——负控必须命中且只命中承重项"
    fi
  done
  judge $([[ $nega_ok -eq $NEG_RUNS ]] && echo 0 || echo 1) \
    "负控 A $nega_ok/$NEG_RUNS 命中恰好 [HP-02,HP-03,HP-04]（三处范围闸承重证明）"
  echo "  负控 A 的红是**预期**结果（闸门被关掉 ⇒ 闸门判据必须红）；读数自带证据" \
    "（探针结论①现场复现：三个 setter 一个都不夹取）：" | tee -a "$OUT/rc-summary.txt"
  /usr/bin/grep -E "^HIDPICHECK: +\[FAIL\]|VERDICT" "$OUT/negctl-gateoff-run1.txt" | sed 's/^/      /' \
    | tee -a "$OUT/rc-summary.txt"

  # ══ ③ 负控 B：断引擎→façade 订阅 ⇒ 恰好红 [HP-07,HP-08] ×2 ═════════════════
  echo "──────── 负控 B STELQUICK_DISPLAY_FWD_OFF=1 ×$NEG_RUNS（预期 rc=10 / 10/12 / 红项 [HP-07,HP-08]）────────" \
    | tee -a "$OUT/rc-summary.txt"
  negb_ok=0
  for i in $(seq 1 $NEG_RUNS); do
    run_suite "$OUT/negctl-fwdoff-run$i.txt" env STELQUICK_DISPLAY_FWD_OFF=1 \
      STELQUICK_HIDPI_CHECK=1 "$BIN"
    rc=$?
    line=$(judge_line "$OUT/negctl-fwdoff-run$i.txt"); verd=$(verdict_line "$OUT/negctl-fwdoff-run$i.txt")
    reds=$(red_ids "$OUT/negctl-fwdoff-run$i.txt")
    echo "  · run$i rc=$rc（$verd；红项 [$reds]）" | tee -a "$OUT/rc-summary.txt"
    if [[ $rc -eq 10 && "$line" == *"判据 10/12"* && "$reds" == "HP-07,HP-08" ]]; then
      (( negb_ok++ ))
    else
      judge 1 "负控 B run$i 红项不是恰好 [HP-07,HP-08]（rc=$rc $line $reds）——负控必须命中且只命中承重项"
    fi
  done
  judge $([[ $negb_ok -eq $NEG_RUNS ]] && echo 0 || echo 1) \
    "负控 B $negb_ok/$NEG_RUNS 命中恰好 [HP-07,HP-08]（绑定腿承重证明）"
  echo "  负控 B 的红是**预期**结果（订阅被断 ⇒ 引擎侧改动刷不到 UI）；读数：" \
    | tee -a "$OUT/rc-summary.txt"
  /usr/bin/grep -E "^HIDPICHECK: +\[FAIL\]|VERDICT" "$OUT/negctl-fwdoff-run1.txt" | sed 's/^/      /' \
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
    "STELQUICK_NIGHT_CHECK=1:nightcheck" \
    "STELQUICK_DISPLAY_CHECK=1:displaycheck"
  do
    var=${spec%%:*}; name=${spec##*:}
    run_suite "$OUT/regression-$name.txt" env $var "$BIN"
    rc=$?
    echo "regression-$name rc=$rc" | tee -a "$OUT/rc-summary.txt"
    [[ $rc -eq 0 ]] || FAILED=1
  done

  # INTERACTCHECK：依赖窗口真的拿到系统焦点，ENV-SKIP 通道照抄 t36..t38
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
