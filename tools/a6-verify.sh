#!/bin/zsh
# A6 全量收口脚本（2026-10-01）：**回归与交接、接口冻结**。
#
# 用法：tools/a6-verify.sh                （跑 all = 除长跑外的全部）
#       tools/a6-verify.sh longrun        （只跑帧桥全量长跑 900+1800s，45 分钟）
#       tools/a6-verify.sh t43            （帧桥短窗 + 报告生成器刷新）
#       tools/a6-verify.sh t44 2          （生命周期 P-LIF ×2）
#       tools/a6-verify.sh t45 2          （配置/数据安全 P-CFG ×2）
#       tools/a6-verify.sh t46            （接口冻结刷新 + 硬性禁区自查）
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
  judge $([[ $src -eq 0 && "$verd" == *"VERDICT=PASS"* ]] && echo 0 || echo 1) \
    "T43 短窗冒烟 11/11（rc=$src ${verd}）"
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
  echo "──────── T45 配置健康 CFGHEALTHCHECK ×${RUNS}（预期 rc=0 / 全绿）────────" \
    | tee -a "$R46/rc-summary.txt"
  if suite_ready STELQUICK_CONFIG_HEALTH_CHECK; then
  ok=0
  for i in $(seq 1 $RUNS); do
    run_suite "$R45/cfghealthcheck-pos-run$i.txt" env STELQUICK_CONFIG_HEALTH_CHECK=1 "$BIN"
    rc=$?
    verd=$(/usr/bin/grep -E "CFGHEALTHCHECK: VERDICT=" "$R45/cfghealthcheck-pos-run$i.txt" | /usr/bin/tail -1)
    line=$(/usr/bin/grep -E "CFGHEALTHCHECK: .*判据 [0-9]+/[0-9]+" "$R45/cfghealthcheck-pos-run$i.txt" | /usr/bin/tail -1)
    echo "  · run$i rc=${rc}（${verd}；${line}）" | tee -a "$R46/rc-summary.txt"
    if [[ $rc -eq 0 && "$verd" == *"VERDICT=PASS"* ]]; then (( ok++ ))
    else judge 1 "T45 run$i 不绿（rc=$rc ${verd}）"; fi
  done
  judge $([[ $ok -eq $RUNS ]] && echo 0 || echo 1) "T45 配置健康 $ok/$RUNS 全绿"
  else
    notready STELQUICK_CONFIG_HEALTH_CHECK
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

# ══ T46 接口冻结 + 硬性禁区自查 ═════════════════════════════════════════════
if [[ "$TARGET" == "all" || "$TARGET" == "t46" ]]; then
  echo "──────── T46 接口冻结刷新 + 硬性禁区 ────────" | tee -a "$R46/rc-summary.txt"
  if [[ -x tools/a6-interface-freeze.sh ]]; then
    tools/a6-interface-freeze.sh > "$R46/interface-freeze-regen.txt" 2>&1
    ifrc=$?
    /usr/bin/grep -aE "禁区|GL|Vulkan|0 行|违规" "$R46/interface-freeze-regen.txt" \
      | sed 's/^/      /' | /usr/bin/tail -12 | tee -a "$R46/rc-summary.txt" || true
    judge $ifrc "T46 接口冻结刷新（rc=${ifrc}）"
  else
    judge 1 "T46 接口冻结脚本缺失或不可执行"
  fi
  # 硬性禁区自查：app 侧头文件与 QML 文件里不得出现 GL/Vulkan 记号
  gl=$(/usr/bin/grep -rlE "gl[A-Z]|GL_[A-Z]|Vk[A-Z]|VK_[A-Z]|vulkan|Vulkan" \
    src/app/*.hpp src/ui/qml/*.qml 2>/dev/null | /usr/bin/wc -l | /usr/bin/tr -d ' ')
  echo "  · 硬性禁区：app 头文件 + QML 命中 GL/Vulkan 记号的文件数 = ${gl}（须 0）" \
    | tee -a "$R46/rc-summary.txt"
  judge $([[ "$gl" == "0" ]] && echo 0 || echo 1) "T46 硬性禁区：QML/app 面零 GL/Vulkan 泄漏"
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
if [[ "$CFG_MD5_PRE" == "$CFG_MD5_POST" ]]; then
  echo "config 零污染门：✅ 前后一致（${CFG_MD5_POST}）" | tee -a "$R46/rc-summary.txt"
else
  echo "config 零污染门：🔴 被污染！pre=$CFG_MD5_PRE post=$CFG_MD5_POST" | tee -a "$R46/rc-summary.txt"
  FAILED=1
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
