#!/bin/zsh
# A6 全量收口脚本（2026-10-01）：**回归与交接、接口冻结**。
#
# 用法：tools/a6-verify.sh                （跑 all = 除长跑外的全部）
#       tools/a6-verify.sh longrun        （只跑帧桥全量长跑 900+1800s，45 分钟）
#       tools/a6-verify.sh t43            （帧桥短窗 + 报告生成器刷新）
#       tools/a6-verify.sh t44 2          （生命周期 P-LIF ×2）
#       tools/a6-verify.sh t45 2          （配置/数据安全 P-CFG ×2）
#       tools/a6-verify.sh t46            （接口冻结刷新 + --check 冻结校验 + 硬性禁区 + 页面协议）
#       tools/a6-verify.sh regress        （只跑相邻 24 套件）
#
# 证据落 docs/evidence/2026-10-01-a6-framebridge/mac/（T43）、
#        docs/evidence/2026-10-01-a6-lifecycle/mac/（T44）、
#        docs/evidence/2026-10-01-a6-configsafety/mac/（T45）、
#        docs/evidence/2026-10-01-a6-regress/mac/（T46/T47）。
# 环境口径（Metal + MoltenVK）**刻意与 t17..t42 完全一致，不换**。
#
# ── A6 要还的账（对照测试文档 §8 A-1.0 出口 7 条）─────────────────────────────
#   ① A-1.0 功能全从 QML 操作  → ✅ T36–T42 已交付（本脚本跑回归证明未退化）
#   ② P-BRG-01..04 + 帧桥统计报告归档 → T43（**A4/A5 后没复跑过，本轮必须复跑**）
#   ③ P-LIF-01..04 生命周期      → T44（**A1 起挂的欠账，main.cpp:512 注释为证**）
#   ④ P-CFG-01..04 配置/数据安全 → T45（T36 只覆盖 CFG-02 一部分）
#   ⑤ 交接契约 7 项             → T46（A6_HANDOFF_FREEZE 台账 + 接口冻结刷新）
#   ⑥ 性能三分账               → T43（渲染 / 读回 / 上传，见 A6_FRAME_BRIDGE_STATS）
#   ⑦ 声明天空仍由旧 OpenGL 渲染 → T46（写进 A6_A1.0_EXIT_AUDIT 的正式声明）
#
# ── 本脚本的"性"（先读三条，别踩）───────────────────────────────────────────
#   · **长跑与其它 target 互斥**：长跑要 45 分钟独占环境，期间不许编译、不许跑
#     别的 app（污染 FPS/帧龄测量）。本脚本把长跑单独设 target，默认不跑。
#   · **全屏遮挡预检**（陷阱 93）：批跑前必须 `screencapture -x` 查全屏应用 ——
#     全屏窗口会让场景图停摆 ⇒ 抓帧型判据**假绿**。T41 尾巴就是这么栽的。
#   · **屏保门**（冻结协议 §6.1.1 第 5 条）：2026-09-23 因屏保废过一次跑。
#     `defaults -currentHost read com.apple.screensaver idleTime` 必须为 0。
#   · **config 零污染门**：整轮脚本前后真实 config.ini md5 必须一致（陷阱 94）。
#
# ── 环境门（沿用 t34..t42）──────────────────────────────────────────────────
#   · 显示器休眠门照抄；Spotlight 索引（mdbulkimport）= 负载毒源：检测到就打
#     环境注记，不许洗 PASS；每个 app 运行都带看门狗；连续起停 Metal 掉设备
#     = 环境噪声不是判据失败（陷阱 50）。
set -u
cd /Users/ztuqfvy/qt_demo/stellarium_vulkan

export VK_DRIVER_FILES=/opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json
export QT_VULKAN_LIB=/opt/homebrew/opt/vulkan-loader/lib/libvulkan.1.dylib
export STELQUICK_GRAPHICS_API=metal

BIN=./build-release/src/ui/stelQuickUI.app/Contents/MacOS/stelQuickUI
NODE=/Users/ztuqfvy/.workbuddy/binaries/node/versions/22.22.2-3/bin/node
EV=docs/evidence
R43=$EV/2026-10-01-a6-framebridge/mac
R44=$EV/2026-10-01-a6-lifecycle/mac
R45=$EV/2026-10-01-a6-configsafety/mac
R46=$EV/2026-10-01-a6-regress/mac
TARGET=${1:-all}
RUNS=${2:-2}

# ⚠️ 看门狗预算：T44 LIFECHECK 是**三组 ×100 轮生命周期事件**，每轮都要给收敛期
#   （最小化 500 / 恢复 400 / 尺寸 350ms）⇒ 单跑 ≈ 190s，加引导 ≈ 210s。
#   240s 会把它误当"挂死"强杀（症状：rc=124，日志无 VERDICT ⇒ 看着像判据红）。
WATCHDOG_SEC=${WATCHDOG_SEC:-480}
FAILED=0
declare -i SCRIPT_PASS=0 SCRIPT_FAIL=0

for d in "$R43" "$R44" "$R45" "$R46"; do mkdir -p "$d"; done

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

# ── 显示器休眠门 + 屏保门 + 全屏遮挡预检（陷阱 93）──────────────────────────
CAFFEINATE_PID=""
wake_display() {
  caffeinate -u -t 3 >/dev/null 2>&1
  caffeinate -dimsu -t 10800 >/dev/null 2>&1 &
  CAFFEINATE_PID=$!
  sleep 1
  # ⚠️ $CAFFEINATE_PID 必须写成 ${CAFFEINATE_PID}：后面紧跟全角逗号，
  #    非 UTF-8 locale 下 bash 会把全角逗号的首字节并进变量名
  #    ⇒ "CAFFEINATE_PID<0xEF>: unbound variable"，脚本当场死（SCRIPT-RC=1）。
  echo "环境：已请求唤醒显示器并挂 caffeinate（pid=${CAFFEINATE_PID}，3 小时）"
}
screen_doors() {
  # 屏保门：idleTime 必须为 0（否则冻结期可能黑屏）
  local idle
  idle=$(defaults -currentHost read com.apple.screensaver idleTime 2>/dev/null || echo "(unset)")
  if pgrep -q ScreenSaverEngine; then
    echo "环境门：🔴 ScreenSaverEngine 正在运行 —— 先退出屏保再跑（冻结协议 §6.1.1）"
  else
    echo "环境门：屏保 idleTime=${idle}，无 ScreenSaverEngine 进程"
  fi
  # 全屏遮挡门：截图 + 报告前台应用（陷阱 93）
  local shot=/tmp/a6-preflight-$$.png
  screencapture -x "$shot" 2>/dev/null && echo "环境门：预检截图 ${shot}（异常时人工查看全屏遮挡）"
  osascript -e 'tell application "System Events" to get name of first application process whose frontmost is true' \
    2>/dev/null | sed 's/^/环境门：前台应用 = /' || true
}
wake_display
screen_doors

# ── config 零污染门（陷阱 94）──────────────────────────────────────────────
CFG="$HOME/Library/Application Support/Stellarium-quick/config.ini"
CFG_MD5_PRE=$(md5 -q "$CFG" 2>/dev/null || echo "(missing)")
# 键级 diff 的 pre 快照（陷阱 111：md5 门会被插件联网更新时间戳打假红 ⇒
# 精判需要起跑时的完整键值，光有 md5 不够）
cp "$CFG" "$R46/config-pre-run.ini" 2>/dev/null || true

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

judge() {
  if [[ "$1" -eq 0 ]]; then
    (( SCRIPT_PASS++ )); echo "  ✓ $2" | tee -a "$R46/rc-summary.txt"
  else
    (( SCRIPT_FAIL++ )); FAILED=1; echo "  ✗ $2" | tee -a "$R46/rc-summary.txt"
  fi
}

# 套件就绪门：判据套件由源码里的 env 名存在性证明（**第三态 UNAVAILABLE 不记
# FAIL** —— 沿用项目纪律：环境/前置不具备时报 UNAVAILABLE，不洗成红也不洗成绿）。
suite_ready() { /usr/bin/grep -rqF "$1" src/ 2>/dev/null; }
notready() { echo "  ⏭ NOT-READY：套件 $1 尚未实现（源码无此 env 名）——跳过，不记 FAIL" \
               | tee -a "$R46/rc-summary.txt"; }
: > "$R46/rc-summary.txt"
env_note | tee -a "$R46/rc-summary.txt"
echo "config 零污染门：起跑 md5=$CFG_MD5_PRE" | tee -a "$R46/rc-summary.txt"
echo "二进制 md5=$(md5 -q "$BIN")" | tee -a "$R46/rc-summary.txt"

# ══ T43 帧桥：短窗冒烟 + 报告生成器刷新 ═════════════════════════════════════
if [[ "$TARGET" == "all" || "$TARGET" == "t43" ]]; then
  echo "──────── T43 帧桥：短窗冒烟（60s+180s）────────" | tee -a "$R46/rc-summary.txt"
  SR=/tmp/a6-short-$$
  mkdir -p "$SR"
  run_suite "$R43/longrun-short.log" env STELQUICK_LONGRUN=1 \
    STELQUICK_LONGRUN_PRODUCER=engine STELQUICK_LONGRUN_WARMUP_SECONDS=60 \
    STELQUICK_LONGRUN_SECONDS=180 STELQUICK_LONGRUN_CSV="$SR/short.csv" \
    STELQUICK_LONGRUN_FRAMES_CSV="$SR/short.frames.csv" "$BIN"
  src=$?
  verd=$(/usr/bin/grep -E "STELLRUN: VERDICT=" "$R43/longrun-short.log" | /usr/bin/tail -1)
  echo "  · 短窗 rc=${src}（${verd}）" | tee -a "$R46/rc-summary.txt"
  # ── 判据口径（T47 定稿，替代原「rc==0 && VERDICT=PASS」）─────────────────
  #   原口径与**已做出的管辖权裁定**不一致 ⇒ 必然假红（同 T46「硬性禁区」教训）：
  #   `SL-C03`「稳态上屏间隔尾部」已在 T43 §6.1 七层判别中裁定为**管辖外** ——
  #   它属 T13 B-LSR3 的性能回归范畴，**不在 A-1.0 出口 7 条**（见
  #   `docs/A6_A1.0_EXIT_AUDIT.zh_CN.md` 的管辖权裁定），根因二分已立 **#160**
  #   （计划二开工前必须做）。于是改为「**允许集合**」口径：
  #     ① 11 条判据必须**全部跑出来**（防"判据没跑/UNAVAILABLE"被当成通过）；
  #     ② 红项集合必须 ⊆ 允许集合 {SL-C03}（出现任何其它红项 ⇒ FAIL）；
  #     ③ 允许集合内的红项**如实打印**（不静默、不洗绿），并注明裁定出处。
  #   ⚠️ 允许集合写死在本脚本里；要增删必须过代码评审 + 归档新证据。
  crit=$(/usr/bin/grep -oE 'STELLRUN: SL-C[0-9]+ (PASS|FAIL)' "$R43/longrun-short.log" \
         | /usr/bin/sort -u | /usr/bin/wc -l | /usr/bin/tr -d ' ')
  reds=$(/usr/bin/grep -oE 'STELLRUN: SL-C[0-9]+ FAIL' "$R43/longrun-short.log" \
         | /usr/bin/awk '{print $2}' | /usr/bin/sort -u | /usr/bin/tr '\n' ',' \
         | /usr/bin/sed -E 's/,$//')
  unex=$(/usr/bin/grep -oE 'STELLRUN: SL-C[0-9]+ FAIL' "$R43/longrun-short.log" \
         | /usr/bin/awk '{print $2}' | /usr/bin/sort -u | /usr/bin/grep -v '^SL-C03$' \
         | /usr/bin/wc -l | /usr/bin/tr -d ' ')
  echo "  · 判据齐全度 crit=${crit}/11；红项=${reds:-（无）}；允许集合外红项=${unex}（须 0）" \
    | tee -a "$R46/rc-summary.txt"
  [[ -n "$reds" ]] && echo "      ⚠️ 允许集合内红项经裁定管辖外（T43 §6.1 / A-1.0 出口第 ② 条 / #160）" \
    | tee -a "$R46/rc-summary.txt" || true
  judge $([[ "$crit" == "11" && "$unex" == "0" ]] && echo 0 || echo 1) \
    "T43 短窗冒烟：11 条判据齐全，红项 ⊆ {SL-C03}（rc=$src ${verd}）"
  cp "$SR/short.csv" "$R43/" 2>/dev/null || true
  cp "$SR/short.frames.csv" "$R43/" 2>/dev/null || true
  # 报告生成器：用归档的 T13/T9 复算，与日志结论逐位对齐才算数
  echo "──────── T43 报告生成器自检（复算 vs 日志）────────" | tee -a "$R46/rc-summary.txt"
  if [[ -x tools/a6-frame-bridge-report.sh ]]; then
    tools/a6-frame-bridge-report.sh > "$R43/report-regen.txt" 2>&1
    rrc=$?
    echo "  · 报告生成器 rc=$rrc" | tee -a "$R46/rc-summary.txt"
    /usr/bin/grep -aE "不一致|⚠" "$R43/report-regen.txt" | sed 's/^/      /' \
      | tee -a "$R46/rc-summary.txt" || true
    judge $rrc "T43 报告生成器：CSV 复算与日志结论一致（rc=${rrc}）"
  else
    judge 1 "T43 报告生成器脚本缺失或不可执行"
  fi
fi

# ══ T43 帧桥：全量长跑（独占，45 分钟）═══════════════════════════════════════
if [[ "$TARGET" == "longrun" ]]; then
  echo "──────── T43 全量长跑（900s 预热 + 1800s 测量）——期间勿编译勿跑别的 app ────────" \
    | tee -a "$R46/rc-summary.txt"
  LR=/tmp/a6-longrun
  mkdir -p "$LR"
  caffeinate -dimsu env STELQUICK_LONGRUN=1 STELQUICK_LONGRUN_PRODUCER=engine \
    STELQUICK_LONGRUN_WARMUP_SECONDS=900 STELQUICK_LONGRUN_SECONDS=1800 \
    STELQUICK_LONGRUN_CSV="$LR/full.csv" \
    STELQUICK_LONGRUN_FRAMES_CSV="$LR/full.frames.csv" "$BIN" \
    > "$LR/full.log" 2>&1
  lrc=$?
  echo "LONGRUN-RC=$lrc" | tee -a "$R46/rc-summary.txt"
  /usr/bin/grep -aE "STELLRUN: (VERDICT|SL-C[0-9]+)" "$LR/full.log" \
    | tee -a "$R46/rc-summary.txt"
  judge $lrc "T43 全量长跑 rc=$lrc"
  # 刷新报告 ② 腿（本轮数字替换归档数字）
  tools/a6-frame-bridge-report.sh "$LR/full.log" "$LR/full.csv" "$LR/full.frames.csv" \
    > "$R43/report-full.txt" 2>&1
  echo "  · 报告刷新 rc=$? ⇒ docs/A6_FRAME_BRIDGE_STATS.zh_CN.md" | tee -a "$R46/rc-summary.txt"
  cp "$LR/full.log" "$R43/" && gzip -kf "$LR/full.csv" "$LR/full.frames.csv" 2>/dev/null
  cp "$LR/full.csv.gz" "$LR/full.frames.csv.gz" "$R43/" 2>/dev/null || true
fi

# ══ T44 生命周期 P-LIF-01..04 ═══════════════════════════════════════════════
if [[ "$TARGET" == "all" || "$TARGET" == "t44" ]]; then
  echo "──────── T44 生命周期 LIFECHECK ×${RUNS}（预期 rc=0 / 全绿 / 红项空）────────" \
    | tee -a "$R46/rc-summary.txt"
  if ! suite_ready STELQUICK_LIFECYCLE_CHECK; then
    notready STELQUICK_LIFECYCLE_CHECK
  else
  ok=0
  for i in $(seq 1 $RUNS); do
    run_suite "$R44/lifecheck-pos-run$i.txt" env STELQUICK_LIFECYCLE_CHECK=1 "$BIN"
    rc=$?
    line=$(/usr/bin/grep -E "LIFECHECK: .*判据 [0-9]+/[0-9]+" "$R44/lifecheck-pos-run$i.txt" | /usr/bin/tail -1)
    verd=$(/usr/bin/grep -E "LIFECHECK: VERDICT=" "$R44/lifecheck-pos-run$i.txt" | /usr/bin/tail -1)
    reds=$(/usr/bin/grep -E "LIFECHECK:? ?\[FAIL\] " "$R44/lifecheck-pos-run$i.txt" \
      | /usr/bin/sed -E 's/.*\[FAIL\] (LF-[0-9A-Za-z]+).*/\1/' | /usr/bin/sort -u \
      | /usr/bin/tr '\n' ',' | sed 's/,$//')
    echo "  · run$i rc=${rc}（${verd}；${line}；红项 [$reds]）" | tee -a "$R46/rc-summary.txt"
    if [[ $rc -eq 0 && "$verd" == *"VERDICT=PASS"* && -z "$reds" ]]; then (( ok++ ))
    else judge 1 "T44 run$i 不绿（rc=$rc $verd 红项 [$reds]）——ENV 劣化不许洗 PASS"; fi
  done
  judge $([[ $ok -eq $RUNS ]] && echo 0 || echo 1) "T44 生命周期 $ok/$RUNS 全绿"
  # P-LIF-01：**进程级** ×N（独立脚本；替身生产者形态，诚实性声明见脚本头注与
  #   docs/T44_LIFECYCLE.zh_CN.md §2.2）。默认冒烟 20 轮，收口时用 A6_LIF_N=100。
  LIF_N=${A6_LIF_N:-20}
  echo "──────── T44 P-LIF-01 进程级 ×$LIF_N ────────" | tee -a "$R46/rc-summary.txt"
  tools/a6-lifecycle-100.sh "$LIF_N" > "$R44/lifecycle-$LIF_N.txt" 2>&1
  lrc=$?
  /usr/bin/grep -aE "首帧延迟|成功（|失败：|config 零污染门|VERDICT=" "$R44/lifecycle-$LIF_N.txt" \
    | sed 's/^/      /' | tee -a "$R46/rc-summary.txt"
  judge $lrc "T44 P-LIF-01 进程级 ×${LIF_N}（rc=${lrc}）"
  # 负控：期望值**实跑出来再写死**（陷阱 87）。以下三组的红项集合是 2026-10-01
  # 三轮独立实跑**逐位一致**后落笔的台账（docs/T44_LIFECYCLE.zh_CN.md §6.4）：
  #   NOSIZE   ⇒ {LF-03a, LF-04a}          （关"被测行为"：不做尺寸切换）
  #   NOSETTLE ⇒ {LF-02b, LF-03a, LF-03c, LF-04a, LF-04c}
  #            （关"观测"：与修正前那版仪器的红项集合逐位一致 = 仪器灵敏度哨兵）
  #   HIDE     ⇒ {LF-02f}                  （关 A1 的 hide() 代理：进不了最小化窗口态）
  # 若实跑结果与本表不符 ⇒ 先查仪器/环境，不许改台账迁就读数。
  if [[ -n "${A6_T44_NEGCTL:-}" ]]; then
    # ⚠️ 护栏（2026-10-01 实测踩到）：变量已设却一枚举失败（如用 bash 跑本 zsh 脚本，
    #   ${=VAR} 是 bad substitution）⇒ 循环体一次都不执行，而"未跑"的 judge 在 else
    #   分支里 ⇒ **静默跳过还报 PASS**。必须数出"实际跑了几组"，为 0 就记账失败。
    nk=0
    for pair in ${=A6_T44_NEGCTL}; do
      envvar=${pair%%:*}; base=${pair##*:}
      (( nk++ ))
      nok=0
      for i in $(seq 1 $RUNS); do
        run_suite "$R44/lifecheck-negctl-$base-run$i.txt" env "$envvar" \
          STELQUICK_LIFECYCLE_CHECK=1 "$BIN"
        echo "  · 负控 $base run$i rc=$?" | tee -a "$R46/rc-summary.txt"
      done
      /usr/bin/grep -aE "LIFECHECK:? ?\[FAIL\] " "$R44/lifecheck-negctl-$base-run1.txt" \
        | /usr/bin/sed -E 's/.*\[FAIL\] (LF-[0-9A-Za-z]+).*/\1/' | /usr/bin/sort -u \
        | /usr/bin/tr '\n' ',' | sed 's/,$//' | sed "s/^/      负控 $base 红项 = /" \
        | tee -a "$R46/rc-summary.txt"
    done
    if (( nk == 0 )); then
      judge 1 "T44 负控枚举失败（A6_T44_NEGCTL 已设但 0 组被解析）—— 不许静默跳过（陷阱 40 同族）"
    else
      echo "  ⚠️ A6_T44_NEGCTL 已跑（$nk 组）：**红项集合须与台账写死值逐位一致**，请人工核对" \
        | tee -a "$R46/rc-summary.txt"
    fi
  else
    echo "  · T44 负控未跑（未设 A6_T44_NEGCTL）—— 收口时必须跑，见 T44 文档 §负控" \
      | tee -a "$R46/rc-summary.txt"
    judge 1 "T44 负控未跑：期望值须实跑写死（陷阱 87）"
  fi
  fi   # end suite_ready
fi

# ══ T45 配置/数据安全 P-CFG-01..04 ══════════════════════════════════════════
if [[ "$TARGET" == "all" || "$TARGET" == "t45" ]]; then
  echo "──────── T45 配置健康 CFGHEALTHCHECK 形态矩阵（7 形态 ×${RUNS}）────────" \
    | tee -a "$R46/rc-summary.txt"
  if suite_ready STELQUICK_CFGHEALTH_CHECK; then
  # 形态 × 期望严重度（**期望值的证据** = T45-A 探针 Q2 五形态实测 + Q3b；
  # 逐条理由写在 ui/ConfigHealthCheck.hpp 的形态表里）。表驱动而非各自 if ——
  # 加形态只改这两行，避免"漏跑一个形态而没人发现"。
  FORMS=(none truncate badline badutf8 binary empty)
  SEVS=(1 3 3 3 3 3)
  # ⚠️ `readonly` 形态**刻意不进矩阵**：个人版配置不可写 ⇒ 引擎
  #   `findFile("config.ini", Writable|File)` 落空（fileFlagsCheck 的 Writable 门）
  #   ⇒ 落到**安装目录**用 `New` 兜底新建 `./config.ini` ⇒ 该形态测到的不是
  #   "个人版只读"，而且会污染仓库根与**后续 run**。机制与处置见 T45 文档 §6.5 残余。
  #   产品侧"不可写 ⇒ severity 3"的判定仍在（`QFileInfo::isWritable()`），但无端到端 run。
  nf=0; nokall=0
  for k in $(seq 1 ${#FORMS[@]}); do
    FORM=${FORMS[$k]}; ESEV=${SEVS[$k]}
    for i in $(seq 1 $RUNS); do
      (( nf++ ))
      ROOT=$(mktemp -d /tmp/t45-cfghealth.XXXXXX)
      tools/a6-cfg-inject.sh "$FORM" "$ROOT" >> "$R45/inject-$FORM-run$i.txt" 2>&1
      EXPECT_BYTES=""; EXPECT_SHA256=""
      source "$ROOT/inject.env"
      LOG="$R45/cfghealthcheck-$FORM-run$i.txt"
      # ⚠️ STEL_USERDIR 指到**临时目录** ⇒ 个人版目录推出到临时目录 ⇒ 真实用户配置零触碰。
      #   STELQUICK_BOOTLOG 指回本 run 的日志本身，判据据此做"诊断页 ↔ 启动日志"对照。
      run_suite "$LOG" env "STEL_USERDIR=$ROOT/Stellarium" "STELQUICK_BOOTLOG=$LOG" \
        STELQUICK_CFGHEALTH_CHECK=1 "STELQUICK_CFGHEALTH_FORM=$FORM" \
        "STELQUICK_CFGHEALTH_EXPECT_BYTES=$EXPECT_BYTES" \
        "STELQUICK_CFGHEALTH_EXPECT_SHA256=$EXPECT_SHA256" "$BIN"
      rc=$?
      verd=$(/usr/bin/grep -aE "CFGHEALTHCHECK: VERDICT=" "$LOG" | /usr/bin/tail -1)
      summ=$(/usr/bin/grep -aE "CFGHEALTHCHECK: .*判据 [0-9]+/[0-9]+" "$LOG" | /usr/bin/tail -1)
      sev=$(/usr/bin/grep -a "CONFIGISO: configHealth" "$LOG" | /usr/bin/tail -1 \
            | /usr/bin/sed -E 's/.*severity=([0-9-]+).*/\1/')
      echo "  · form=${FORM}(期望 sev=${ESEV}) run$i rc=${rc} sev观测=${sev:-?} ${verd}" \
        | tee -a "$R46/rc-summary.txt"
      echo "      ${summ}" | tee -a "$R46/rc-summary.txt"
      if [[ $rc -eq 0 && "$verd" == *"VERDICT=PASS"* && "$sev" == "$ESEV" ]]; then (( nokall++ ))
      else judge 1 "T45 form=${FORM} run$i 不绿（rc=$rc sev=${sev:-?} 期望=${ESEV} ${verd}）"; fi
      /bin/chmod -R u+w "$ROOT" 2>/dev/null
      /bin/rm -rf "$ROOT"
      # 起停间隔：本形态矩阵是**连续起停**，测前多份跑偏记录都栽在这
      #（`VK_ERROR_DEVICE_LOST` / `Failed to build or resize swapchain`，
      #  陷阱 50）。**与配置无关**：同一形态重复跑时 rc 在 0 与 139 之间摆动。
      # 故留出沉降时间，别让判据替环境背锅。
      sleep "${A6_SETTLE_SEC:-8}"
    done
  done
  judge $([[ $nokall -eq $nf ]] && echo 0 || echo 1) \
    "T45 配置健康形态矩阵 $nokall/$nf 全绿（6 形态：none/truncate/badline/badutf8/binary/empty）"
  # 卫生门：引擎的 `findFile("config.ini", New)` 会在**安装目录**兜底新建一份
  # （本轮形态矩阵不含只读形态 ⇒ 不该发生）。若出现 ⇒ 它既污染仓库、又会污染后续 run
  # （下一轮的 findFile 会先命中它）⇒ 记账 + 清理。**先证明它没被 git 跟踪**才敢删。
  if [[ -f "$PWD/config.ini" ]]; then
    if git ls-files --error-unmatch config.ini > /dev/null 2>&1; then
      judge 1 "T45 卫生：仓库根出现 config.ini 且它是**被跟踪文件**（不许自动删）—— 请人工检查"
    else
      judge 1 "T45 卫生：仓库根出现未被跟踪的 config.ini（引擎安装目录兜底新建）—— 已清理"
      /bin/rm -f "$PWD/config.ini"
    fi
  fi
  # ── 负控：期望值**实跑出来再写死**（陷阱 87）。每组只开一个开关，红项集合必须
  #    **互不相同且与关掉的东西一一对应**（否则说明判据在互相搭便车）。
  #    条目格式 `<envvar>=1:<组名>:<形态>[@orig]`（`@orig` ⇒ 注入到**原目录**）：
  #      ISOLATE_OFF + full@orig ⇒ 关 T36 隔离 ⇒ product 用**原目录**；
  #        那份必须放**完整**配置（form=full）引擎才起得来 —— 放 none 的话测到的是
  #        "没配置 ⇒ 崩"（T45-A 已证空用户目录 SIGSEGV），与"隔离关掉"这个被测命题
  #        不是一回事 ⇒ 期望红项 = {CH-02}（isolated=0）
  #      STATUS_OFF  + truncate ⇒ 关"健康报告"（修复照常）⇒ 严重度/原始读数退成 0/-1
  #      REPAIR_OFF  + truncate ⇒ 关"引导修复"⇒ **引擎应崩**（rc≠0、无 VERDICT）
  #      DRIFT       + none     ⇒ 打印探针行后改诊断面 ⇒ 与启动日志漂移
  #    ⚠️ 期望红项集合**实跑出来再写死**（陷阱 87）；两轮逐位一致才落笔。
  #       2026-10-01 实跑（round1 / round2 **逐位一致**，二进制 = ConfigHealthCheck 含
  #       `full` 形态的"字节不必存活"分支）：
  #         ISOLATE_OFF + full@orig  rc=5 红项={CH-02,CH-03,CH-05,CH-06} SKIP={CH-04}
  #                                  （isolated=0 ⇒ CH-02 必须红；健康读整段被跳过 ⇒
  #                                   status=-1/severity=0 ⇒ CH-03/CH-05 红；
  #                                   CH-04 因"产品读的那份文件已不存在"而 SKIP）
  #         STATUS_OFF  + truncate   rc=5 红项={CH-03,CH-04,CH-05}       SKIP={}
  #                                  （修复照常 repaired=1；只有报告被关）
  #         REPAIR_OFF  + truncate   rc=139 **无任何判据输出**            ← 修复腿承重的铁证
  #                                  （severity=3 status=2 keys=99 repaired=0 ⇒ 引擎 SIGSEGV）
  #         DRIFT       + none       rc=5 红项={CH-07}                   SKIP={CH-04}
  #       四组红项集合**互不相同**且与关掉的东西一一对应（否则说明判据在搭便车，陷阱 22）。
  if [[ -n "${A6_T45_NEGCTL:-}" ]]; then
    nk=0
    for trip in ${=A6_T45_NEGCTL}; do
      envvar="${trip%%:*}"; rest="${trip#*:}"
      base="${rest%%:*}"; FORM="${rest##*:}"
      TGT=personal
      if [[ "$FORM" == *"@orig" ]]; then FORM="${FORM%@orig}"; TGT=original; fi
      # 形如 `<env>=1:NAME`（没有第三个冒号段）⇒ 形态缺省 none。
      if [[ -z "$FORM" || "$FORM" == "$rest" ]]; then FORM=none; fi
      (( nk++ ))
      ROOT=$(mktemp -d /tmp/t45-negctl.XXXXXX)
      tools/a6-cfg-inject.sh "$FORM" "$ROOT" "$TGT" >> "$R45/inject-negctl-$base.txt" 2>&1
      EXPECT_BYTES=""; EXPECT_SHA256=""
      source "$ROOT/inject.env"
      LOG="$R45/cfghealthcheck-negctl-$base-run1.txt"
      run_suite "$LOG" env "STEL_USERDIR=$ROOT/Stellarium" "STELQUICK_BOOTLOG=$LOG" \
        "$envvar" \
        STELQUICK_CFGHEALTH_CHECK=1 "STELQUICK_CFGHEALTH_FORM=$FORM" \
        "STELQUICK_CFGHEALTH_EXPECT_BYTES=$EXPECT_BYTES" \
        "STELQUICK_CFGHEALTH_EXPECT_SHA256=$EXPECT_SHA256" "$BIN"
      nrc=$?
      verd=$(/usr/bin/grep -aE "CFGHEALTHCHECK: VERDICT=" "$LOG" | /usr/bin/tail -1)
      reds=$(/usr/bin/grep -aE "CFGHEALTHCHECK: +\[FAIL\] " "$LOG" \
             | /usr/bin/sed -E 's/.*\[FAIL\] (CH-[0-9]+).*/\1/' | /usr/bin/sort -u \
             | /usr/bin/tr '\n' ',' | sed 's/,$//')
      echo "  · 负控 $base（form=$FORM）rc=$nrc ${verd}" | tee -a "$R46/rc-summary.txt"
      echo "      负控 $base 红项 = ${reds:-（无判据输出 —— 见台账：REPAIR_OFF 的期望就是崩）}" \
        | tee -a "$R46/rc-summary.txt"
      /bin/chmod -R u+w "$ROOT" 2>/dev/null
      /bin/rm -rf "$ROOT"
      # 连续起停 Metal 掉设备（陷阱 50）⇒ 与形态矩阵同款收敛期。
      sleep "${A6_SETTLE_SEC:-8}"
    done
    if (( nk == 0 )); then
      judge 1 "T45 负控枚举失败（A6_T45_NEGCTL 已设但 0 组被解析）—— 不许静默跳过"
    else
      echo "  ⚠️ A6_T45_NEGCTL 已跑（$nk 组）：**红项集合须与台账写死值逐位一致**，请人工核对" \
        | tee -a "$R46/rc-summary.txt"
    fi
  else
    echo "  · T45 负控未跑（未设 A6_T45_NEGCTL）—— 收口时必须跑，见 T45 文档 §负控" \
      | tee -a "$R46/rc-summary.txt"
    judge 1 "T45 负控未跑：期望值须实跑写死（陷阱 87）"
  fi
  else
    notready STELQUICK_CFGHEALTH_CHECK
  fi
  # 资源链接判据（P-CFG-03）：脚本自带 rc 语义 0=PASS / 1=FAIL（与套件就绪无关）
  echo "──────── T45 资源链接占位校验 ────────" | tee -a "$R46/rc-summary.txt"
  "$NODE" tools/a6-resource-links-check.mjs > "$R45/resource-links.txt" 2>&1
  rlrc=$?
  /usr/bin/tail -6 "$R45/resource-links.txt" | sed 's/^/      /' | tee -a "$R46/rc-summary.txt"
  judge $rlrc "T45 资源链接占位校验（rc=${rlrc}；0=PASS）"
  # T36 的 CONFIGCHECK 回归（配置目录隔离不许退化）
  run_suite "$R45/configcheck-regress.txt" env STELQUICK_CONFIG_CHECK=1 "$BIN"
  judge $? "T45 T36 CONFIGCHECK 未退化（rc=$?）"
fi

# ══ T46 接口冻结 + 硬性禁区 + 页面协议 ══════════════════════════════════════
if [[ "$TARGET" == "all" || "$TARGET" == "t46" ]]; then
  echo "──────── T46 接口冻结刷新 + 硬性禁区 + 页面协议 ────────" | tee -a "$R46/rc-summary.txt"
  if [[ -x tools/a6-interface-freeze.sh ]]; then
    # (1) 刷新：把"冻结那一刻的公开面"落到证据目录（人读 + 留档）
    tools/a6-interface-freeze.sh > "$R46/interface-freeze-regen.txt" 2>&1
    judge $? "T46 接口冻结清单可生成"
    # (2) **冻结校验**：源码公开面 == 已提交的冻结清单（忽略生成时间/HEAD 两行）。
    #     这是"冻结"二字的兑现 —— 冻结之后有人偷改公开面，这里必须红。
    tools/a6-interface-freeze.sh --check > "$R46/interface-freeze-check.txt" 2>&1
    ifc=$?
    /usr/bin/grep -aE 'IF-CHECK' "$R46/interface-freeze-check.txt" | sed 's/^/      /' \
      | tee -a "$R46/rc-summary.txt" || true
    judge $ifc "T46 接口冻结校验（源码公开面 == 已冻结清单）"
    # (3) 硬性禁区：**取 §5 的机器可读行**（单一口径）。首版 t46 段自起一套
    #     "按文件 + 不剥注释"的正则 ⇒ 把 ` * 禁止：不暴露 GL/Vulkan 句柄` 这类
    #     **说明注释**判成泄漏（10 个文件假红），且该段从未跑过 = 藏在脚本里的坏判据。
    fb=$(/usr/bin/grep -aE '^FORBIDDEN ' "$R46/interface-freeze-regen.txt" | /usr/bin/tail -1)
    echo "  · $fb（须两处均 0）" | tee -a "$R46/rc-summary.txt"
    judge $([[ "$fb" == *"app_hpp_hits=0"* && "$fb" == *"qml_hits=0"* ]] && echo 0 || echo 1) \
      "T46 硬性禁区：app 头文件与 QML 面零 GL/Vulkan 泄漏"
  else
    judge 1 "T46 接口冻结脚本缺失或不可执行"
  fi
  # (4) 页面协议入口（契约第 7 条"B 阶段不改页面协议"的**可核对面**）：
  #     7 个上下文属性名 + SkyViewport 类型注册，不得增删改名。
  proto=$(/usr/bin/grep -cE 'setContextProperty\(QStringLiteral\("(appFacade|ActionRouter|searchResults|objectInfo|ShortcutModel|HelpModel|ErrorModel)"\)' src/ui/main.cpp)
  skyreg=$(/usr/bin/grep -cE 'qmlRegisterType<stelapp::SkyViewport>' src/ui/main.cpp)
  echo "  · 页面协议入口：setContextProperty 命中 $proto/7；SkyViewport 注册 $skyreg/1" \
    | tee -a "$R46/rc-summary.txt"
  judge $([[ "$proto" == "7" && "$skyreg" == "1" ]] && echo 0 || echo 1) \
    "T46 页面协议入口未变（7 属性 + SkyViewport）"
fi

# ══ 回归：全部 24 套件 ══════════════════════════════════════════════════════
if [[ "$TARGET" == "all" || "$TARGET" == "regress" ]]; then
  echo "──────── 回归：相邻 24 套件（全部 rc=0 才算数）────────" | tee -a "$R46/rc-summary.txt"
  regress_fail=0
  for pair in \
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
    "STELQUICK_HELP_CHECK=1:helpcheck" \
    "STELQUICK_ERROR_CHECK=1:errorcheck"
  do
    var=${pair%%:*}; name=${pair##*:}
    run_suite "$R46/regression-$name.txt" env $var "$BIN"
    rc=$?
    [[ $rc -ne 0 ]] && regress_fail=1
    echo "regression-$name rc=$rc" | tee -a "$R46/rc-summary.txt"
  done
  # 交互套件（窗口需获系统焦点，拿不到焦点就 ENV-SKIP **不洗 PASS**）
  run_suite "$R46/regression-interactcheck.txt" env STELQUICK_INTERACT_UI_CHECK=1 "$BIN"
  iline=$(/usr/bin/grep -E "INTERACTCHECK: 判据" "$R46/regression-interactcheck.txt" | /usr/bin/tail -1)
  ireds=$(/usr/bin/grep -cE "INTERACTCHECK: ✗" "$R46/regression-interactcheck.txt")
  if [[ $ireds -eq 0 && "$iline" == *"18/18"* ]]; then
    echo "regression-interactcheck rc=0（18/18，窗口已激活）" | tee -a "$R46/rc-summary.txt"
  elif /usr/bin/grep -qE "未激活|未获|no focus" "$R46/regression-interactcheck.txt"; then
    echo "regression-interactcheck ENV-SKIP（$iline ⇒ 窗口未获系统焦点，**不洗成 PASS**）" \
      | tee -a "$R46/rc-summary.txt"
    regress_fail=1
  else
    echo "regression-interactcheck FAIL（${iline}；✗=${ireds}）" | tee -a "$R46/rc-summary.txt"
    regress_fail=1
  fi
  # A2：跨形态合流（Metal）
  run_suite "$R46/regression-a2-metal.txt" env STELQUICK_A2_CHECK=1 "$BIN"
  rc=$?; [[ $rc -ne 0 ]] && regress_fail=1
  echo "regression-a2-metal rc=$rc" | tee -a "$R46/rc-summary.txt"
  # DYN 双路（动态量：真实引擎 vs 替身，各 3 轮 —— 陷阱 3/4）
  for kind in engine stub; do
    : > "$R46/regression-dyn-$kind-metal.txt"
    pass=0
    for i in 1 2 3; do
      run_suite "$R46/regression-dyn-$kind-metal-run$i.txt" env STELQUICK_DYN_CHECK=1 \
        STELQUICK_DYN_PRODUCER=$kind "$BIN"
      rc=$?
      [[ $rc -eq 0 ]] && (( pass++ ))
      echo "──────── DYN($kind) run $i/3（rc=${rc}）────────" >> "$R46/regression-dyn-$kind-metal.txt"
      /usr/bin/grep -E "DYNCHECK:" "$R46/regression-dyn-$kind-metal-run$i.txt" >> "$R46/regression-dyn-$kind-metal.txt"
    done
    echo "regression-dyn-$kind-metal $pass/3 PASS" | tee -a "$R46/rc-summary.txt"
    [[ $pass -eq 3 ]] || regress_fail=1
  done
  # S3：旧形态（QWidget 引擎）仍能起
  STELA3_CHECK=1 ./build-release/src/stellarium > "$R46/regression-s3-stela3.txt" 2>&1
  rc=$?; [[ $rc -ne 0 ]] && regress_fail=1
  echo "regression-s3-stela3 rc=${rc}（旧形态）" | tee -a "$R46/rc-summary.txt"
  judge $regress_fail "回归：24 套件 + INTERACT + A2 + DYN×2 + S3 全部 rc=0"
fi

# ══ 收尾：config 零污染门 + 计划任务残留 + 汇总 ═════════════════════════════
CFG_MD5_POST=$(md5 -q "$CFG" 2>/dev/null || echo "(missing)")
# ⚠️ 陷阱 110：**量具本身没跑通时不许打 ✅**。首版只比 pre==post ⇒ 两侧都是
#    "(missing)"（如 `md5` 不在 PATH）时照样打"✅ 前后一致" —— 自证静默失效，
#    而且是在"证明没有污染"这种**否定性结论**上失效（陷阱 79 的自证面变体）。
#    判据：两侧都必须是 32 位 hex；否则 UNAVAILABLE —— 不洗绿、也不当红（第三态）。
hex32() { [[ "$1" =~ ^[0-9a-f]{32}$ ]] }
if ! hex32 "$CFG_MD5_PRE" || ! hex32 "$CFG_MD5_POST"; then
  echo "config 零污染门：⚠️ UNAVAILABLE（量具未跑通 pre=$CFG_MD5_PRE post=$CFG_MD5_POST —— 检查 md5 是否在 PATH；**不记 PASS**）" \
    | tee -a "$R46/rc-summary.txt"
  FAILED=1
elif [[ "$CFG_MD5_PRE" == "$CFG_MD5_POST" ]]; then
  echo "config 零污染门：✅ 前后一致（${CFG_MD5_POST}）" | tee -a "$R46/rc-summary.txt"
else
  # ⚠️ T47（2026-10-01 16:37 实测）：md5 相同不再必然成立 —— T43 短窗一跑，
  #    Satellites/Exoplanets 插件的**联网更新检查**就写 last_update 时间戳
  #    （键数 771==771、零丢键；16:37:23 与短窗起跑时刻逐秒吻合）。
  #    这是**引擎自然行为**（真实用户每次启动也会发生），不是布场污染；
  #    但 md5 门抓字节级差异 ⇒ 会被它打假红。
  #    治法 = md5 粗筛 + **键级 diff 精判**：diff 仅含白名单时间戳键 ⇒ 显式记
  #    ⚠️ BENIGN（不静默 ✅，附 diff 证据）；出现任何其它差异 ⇒ 🔴 FAILED。
  #    白名单写死在脚本里，改白名单必须过代码评审 + 归档新证据。
  DIFFLOG="$R46/config-pollution-diff.txt"
  CFG_SNAPSHOT_PRE="$R46/config-pre-run.ini" \
    /usr/bin/python3 - "$CFG" "$DIFFLOG" <<'PYEOF'
import configparser, sys
cfg_path, out_path = sys.argv[1], sys.argv[2]
def keys(p):
    cp = configparser.ConfigParser(interpolation=None, strict=False)
    cp.optionxform = str
    cp.read_string(open(p, 'rb').read().decode('utf-8', 'replace'))
    d = {}
    for s in cp.sections():
        for k, v in cp.items(s):
            d[s + '/' + k] = v
    return d
cur = keys(cfg_path)
import os
pre_path = os.environ.get('CFG_SNAPSHOT_PRE', '')
with open(out_path, 'w', encoding='utf-8') as f:
    f.write(f"当前键数 {len(cur)}\n")
    if pre_path and os.path.exists(pre_path):
        pre = keys(pre_path)
        changed = {k for k in (set(pre) & set(cur)) if pre[k] != cur[k]}
        for k in sorted(changed):
            f.write(f"变值 {k}: {pre[k]!r} -> {cur[k]!r}\n")
        for k in sorted(set(pre) - set(cur)):
            f.write(f"丢键 {k}\n")
        for k in sorted(set(cur) - set(pre)):
            f.write(f"增键 {k}\n")
        f.write(f"pre 键数 {len(pre)}\n")
    else:
        f.write("(pre 快照缺失，无法键级比对)\n")
PYEOF
  /usr/bin/grep -aE '^(变值|丢键|增键)' "$DIFFLOG" | /usr/bin/sed 's/^/      /' \
    | tee -a "$R46/rc-summary.txt" || true
  # 白名单：仅"插件更新时间戳"键（引擎联网自然行为）。其它任何差异 ⇒ 污染。
  nonbenign=$(/usr/bin/grep -aE '^(变值|丢键|增键)' "$DIFFLOG" \
    | /usr/bin/grep -avE '^(变值|丢键|增键) (Exoplanets|Satellites)/last_update:' | /usr/bin/wc -l | /usr/bin/tr -d ' ')
  keyn_pre=$(/usr/bin/grep -a '^pre 键数' "$DIFFLOG" | /usr/bin/grep -oE '[0-9]+')
  keyn_cur=$(/usr/bin/grep -a '^当前键数' "$DIFFLOG" | /usr/bin/grep -oE '[0-9]+')
  if [[ -n "$keyn_pre" && "$keyn_pre" != "$keyn_cur" ]]; then
    echo "config 零污染门：🔴 键数变化 pre=$keyn_pre cur=$keyn_cur（宁可错杀，键数不变才谈白名单）" \
      | tee -a "$R46/rc-summary.txt"
    FAILED=1
  elif (( nonbenign > 0 )); then
    echo "config 零污染门：🔴 被污染！pre=$CFG_MD5_PRE post=$CFG_MD5_POST（白名单外差异 $nonbenign 处，见 $DIFFLOG）" \
      | tee -a "$R46/rc-summary.txt"
    FAILED=1
  else
    echo "config 零污染门：⚠️ BENIGN-WRITE（md5 变、但 diff 仅含插件更新时间戳白名单，键数 $keyn_pre==$keyn_cur；证据 $DIFFLOG）" \
      | tee -a "$R46/rc-summary.txt"
  fi
fi
# 清理临时截图
rm -f /tmp/a6-preflight-$$.png 2>/dev/null || true
# 残留进程自查
echo "残留进程自查：$(pgrep -ifl stelQuickUI | /usr/bin/wc -l | /usr/bin/tr -d ' ') 个 stelQuickUI" \
  | tee -a "$R46/rc-summary.txt"
pgrep -ifl stelQuickUI | sed 's/^/      /' | tee -a "$R46/rc-summary.txt" || true

echo "════════ SCRIPT-RC=$([[ $FAILED -eq 0 ]] && echo 0 || echo 1)  FAILED=$SCRIPT_FAIL  PASS=$SCRIPT_PASS ════════" \
  | tee -a "$R46/rc-summary.txt"
[[ -n "$CAFFEINATE_PID" ]] && kill "$CAFFEINATE_PID" 2>/dev/null || true
exit $([[ $FAILED -eq 0 ]] && echo 0 || echo 1)
