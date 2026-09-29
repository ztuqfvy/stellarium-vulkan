#!/bin/zsh
# T27 验证脚本（2026-09-29）。判据 + 回归，证据落 docs/evidence/2026-09-29-t27-mouse-nav/
#
# 用法：tools/t27-verify.sh          （跑全部）
#       tools/t27-verify.sh core 5   （T27 正题 INTERACTCHECK，跑 N 次；默认 5）
#       tools/t27-verify.sh regress  （A2/DYN + 相邻回归）
#       tools/t27-verify.sh dyn 3    （只跑 DYN，跑 N 次；默认 3）
#
# 环境口径与 t17..t26-verify.sh **完全一致**（同一后端 Metal）——**刻意不换口径**。
#
# ── T27 的正题：天空页鼠标（点击选中 / 拖拽平移 / 右键反选）端到端 ────────────
# 与 T25 的滚轮死路同源：旧宿主链路 StelMainView mousePress/Release → StelApp::
# handleClick、mouseMove → handleMove，合流形态 QML 从未接过（天空页无任何
# MouseArea；SkyViewport 头注写了"向引擎转发手势"但实现里一行鼠标代码都没有）。
# 修法（语义零复刻）：SkyTestPage 挂 MouseArea → AppFacade::skyMouse{Press,
# Release,Move}（合成事件转发；含**坐标空间适配**——引擎投影视口 2560×1440 与
# QML 窗口 960×728 不同构，按比例映射 + y 翻转 + 除以 dppp）。
#
# 判据（INTERACTCHECK 6 → 9，新增 IT-07..09，活链）：
#   IT-07 冻结仿真时间后向左拖 160px → 视线转过 >0.2° ∧ 再向右拖的两次增量
#         点积 <0（反向对照）。🔴 首跑实测：HostDriven 帧泵 400ms 推 **0.61 天**
#         仿真时间 ⇒ 视线（锁地平）随 JD 每秒转 200°+，旧口径"视线转过"被自然
#         漂移淹掉（负控下仍绿）⇒ 段内冻结时间 + 双向对照（血泪第 4/9 条）。
#   IT-08 月球居中（<0.5°）→ 清选中 → 真实点击视口中心 → 选中 == Moon
#         （坐标映射正确性的守卫：映射退化 → 红；负控②实证）。
#   IT-09 右键 press+release → 反选（与 IT-08 成对；release 必须整对投递）。
#
# 仪器加固（本任务新增，均**不放宽判据**）：
#   · IT-06 焦点门：点击搜索框后校验 focusObject 是 QQuickTextInput，有界重试；
#     窗口未获得系统焦点（isActive=false，后台跑常见）⇒ 明确判红并标注
#     "仪器不可用"，不洗成 PASS。
set -u
cd /Users/ztuqfvy/qt_demo/stellarium_vulkan

export VK_DRIVER_FILES=/opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json
export QT_VULKAN_LIB=/opt/homebrew/opt/vulkan-loader/lib/libvulkan.1.dylib
export STELQUICK_GRAPHICS_API=metal

BIN=./build-release/src/ui/stelQuickUI.app/Contents/MacOS/stelQuickUI
OUT=docs/evidence/2026-09-29-t27-mouse-nav
TARGET=${1:-all}
CORE_RUNS=${2:-5}
DYN_RUNS=${2:-3}
mkdir -p "$OUT"

if [[ "$TARGET" == "dyn" ]]; then
  if [[ -f "$OUT/rc-summary.txt" ]]; then
    grep -vE "dyn-(engine|stub)-metal" "$OUT/rc-summary.txt" \
      > "$OUT/rc-summary.txt.tmp" 2>/dev/null || : > "$OUT/rc-summary.txt.tmp"
    mv "$OUT/rc-summary.txt.tmp" "$OUT/rc-summary.txt"
  fi
else
  : > "$OUT/rc-summary.txt"
fi

env_note() {
  echo "环境：$(uptime | sed 's/^ *//')"
  local ds
  ds=$(pmset -g custom 2>/dev/null | grep -E "^[[:space:]]*displaysleep" | head -1 | awk '{print $2}')
  [[ -n "$ds" ]] && echo "环境注记：displaysleep=${ds} 分钟（显示器空闲即休眠）"
  if pgrep -q mdbulkimport; then
    echo "环境注记：Spotlight 批量索引正在运行（mdbulkimport）——点击注入可能被干扰"
  fi
  echo '读数采信规则（IT-06 焦点腿）：窗口未获得系统焦点时 focusObject()==nullptr，'
  echo '  守卫前提不成立（本判据的按键是 sendEvent 直达窗口，绕过系统焦点）⇒ 判据'
  echo '  明确报「仪器不可用」并 rc=10，**不洗成 PASS、也不改写成 INVALID**。'
  echo '  同一次运行内 IT-01..05/IT-07..09 的读数不受影响（它们不依赖系统焦点）。'
}

run_dyn() {
  local n=$DYN_RUNS pass=0 i rc
  : > "$OUT/regression-dyn-engine-metal.txt"
  for i in $(seq 1 $n); do
    STELQUICK_DYN_CHECK=1 STELQUICK_DYN_PRODUCER=engine "$BIN" \
      > "$OUT/regression-dyn-engine-metal-run$i.txt" 2>&1
    rc=$?
    echo "──────── DYN run $i/$n（rc=$rc）────────" >> "$OUT/regression-dyn-engine-metal.txt"
    grep -E "DYNCHECK:" "$OUT/regression-dyn-engine-metal-run$i.txt" \
      >> "$OUT/regression-dyn-engine-metal.txt"
    [[ $rc -eq 0 ]] && (( pass++ ))
  done
  {
    echo "──────── 汇总 ────────"
    env_note
    echo "DYN 显示/降级判据（真实引擎生产者）：$pass/$n 次 PASS"
    echo "定性：本机为环境敏感/间歇（T18 的 A/B 记录见 t18 证据目录）。"
    echo "零退化的依据是同口径 A/B + 替身路径不含本任务代码，不是本行的某一次绿色。"
  } >> "$OUT/regression-dyn-engine-metal.txt"
  cat "$OUT/regression-dyn-engine-metal.txt"
  echo "regression-dyn-engine-metal $pass/$n PASS（间歇，DYN 环境敏感）" \
    | tee -a "$OUT/rc-summary.txt"
}

run_dyn_stub() {
  local n=$DYN_RUNS pass=0 i rc
  : > "$OUT/regression-dyn-stub-metal.txt"
  for i in $(seq 1 $n); do
    STELQUICK_DYN_CHECK=1 "$BIN" > "$OUT/regression-dyn-stub-metal-run$i.txt" 2>&1
    rc=$?
    echo "──────── DYN(替身) run $i/$n（rc=$rc）────────" >> "$OUT/regression-dyn-stub-metal.txt"
    grep -E "DYNCHECK:" "$OUT/regression-dyn-stub-metal-run$i.txt" \
      >> "$OUT/regression-dyn-stub-metal.txt"
    [[ $rc -eq 0 ]] && (( pass++ ))
  done
  {
    echo "──────── 汇总 ────────"
    env_note
    echo "DYN 显示/降级判据（替身生产者，不 boot 引擎）：$pass/$n 次 PASS"
    echo "替身路径不含 T27 鼠标转发代码（DYN 走 startPage=\"sky\" 且不注入鼠标）"
    echo "⇒ 它失败即与本次改动无关。"
  } >> "$OUT/regression-dyn-stub-metal.txt"
  cat "$OUT/regression-dyn-stub-metal.txt"
  echo "regression-dyn-stub-metal $pass/$n PASS（判别性对照：替身不含 T27 代码）" \
    | tee -a "$OUT/rc-summary.txt"
}

if [[ "$TARGET" == "all" || "$TARGET" == "core" ]]; then
  # ① T27 正题：INTERACTCHECK × N（判据 9 条：IT-01..09）
  integer pass=0 i rc
  pass=0
  : > "$OUT/interactcheck-mac-n$CORE_RUNS.txt"
  for i in $(seq 1 $CORE_RUNS); do
    STELQUICK_INTERACT_UI_CHECK=1 "$BIN" > "$OUT/interactcheck-mac-run$i.txt" 2>&1
    rc=$?
    echo "──────── INTERACTCHECK run $i/$CORE_RUNS（rc=$rc）────────" >> "$OUT/interactcheck-mac-n$CORE_RUNS.txt"
    grep -E "INTERACTCHECK:" "$OUT/interactcheck-mac-run$i.txt" \
      >> "$OUT/interactcheck-mac-n$CORE_RUNS.txt"
    [[ $rc -eq 0 ]] && (( pass++ ))
  done
  echo "interactcheck-mac $pass/$CORE_RUNS PASS" | tee -a "$OUT/rc-summary.txt"

  # ② 相邻回归：T26 搜索（本轮未动搜索链，护栏照跑）
  STELQUICK_SEARCH_CHECK=1 "$BIN" > "$OUT/regression-searchcheck.txt" 2>&1
  echo "regression-searchcheck rc=$?" | tee -a "$OUT/rc-summary.txt"

  # ③ 相邻回归：T18 定位两套（鼠标点击与定位都改选中状态，相邻最紧）
  STELQUICK_ACTION_CHECK=1 "$BIN" > "$OUT/regression-actioncheck.txt" 2>&1
  echo "regression-actioncheck rc=$?" | tee -a "$OUT/rc-summary.txt"
  STELQUICK_LOCATE_CHECK=1 "$BIN" > "$OUT/regression-locatecheck.txt" 2>&1
  echo "regression-locatecheck rc=$?" | tee -a "$OUT/rc-summary.txt"
  STELQUICK_UI_CHECK=1 "$BIN" > "$OUT/regression-locate-uicheck.txt" 2>&1
  echo "regression-locate-uicheck rc=$?" | tee -a "$OUT/rc-summary.txt"

  # ④ 相邻回归：T19/T20/T22
  STELQUICK_TIME_CHECK=1 "$BIN" > "$OUT/regression-timecheck.txt" 2>&1
  echo "regression-timecheck rc=$?" | tee -a "$OUT/rc-summary.txt"
  STELQUICK_TIME_UI_CHECK=1 "$BIN" > "$OUT/regression-timeuicheck.txt" 2>&1
  echo "regression-timeuicheck rc=$?" | tee -a "$OUT/rc-summary.txt"
  STELQUICK_RETURN_UI_CHECK=1 "$BIN" > "$OUT/regression-returnuicheck.txt" 2>&1
  echo "regression-returnuicheck rc=$?" | tee -a "$OUT/rc-summary.txt"
  STELQUICK_REPLAY_CHECK=1 "$BIN" > "$OUT/regression-replaycheck.txt" 2>&1
  echo "regression-replaycheck rc=$?" | tee -a "$OUT/rc-summary.txt"

  # ⑤ T16 时钟
  STELQUICK_CLOCK_CHECK=1 "$BIN" > "$OUT/regression-clockcheck.txt" 2>&1
  echo "regression-clockcheck rc=$?" | tee -a "$OUT/rc-summary.txt"
fi

if [[ "$TARGET" == "all" || "$TARGET" == "regress" ]]; then
  # ⑥ 回归：A2 静态纹理（Metal）——本轮动了 SkyTestPage.qml（新增 MouseArea），
  #    QML 页内新增项必须证明没有污染逐像素探针 ⇒ 硬要求
  STELQUICK_A2_CHECK=1 "$BIN" > "$OUT/regression-a2-metal.txt" 2>&1
  echo "regression-a2-metal rc=$?" | tee -a "$OUT/rc-summary.txt"

  # ⑦ 回归：DYN（真实引擎 + 替身对照，N 次）
  run_dyn
  run_dyn_stub

  # ⑧ 回归：S3 引擎集成（旧宿主）——本轮**未动 src/core**，但鼠标链与旧宿主的
  #    StelApp::handleClick/handleMove 是同一入口，旧宿主行为必须零变化 ⇒ 照跑
  STELA3_CHECK=1 ./build-release/src/stellarium > "$OUT/regression-s3-stela3.txt" 2>&1
  echo "regression-s3-stela3 rc=$?" | tee -a "$OUT/rc-summary.txt"
fi

if [[ "$TARGET" == "dyn" ]]; then
  run_dyn
  run_dyn_stub
fi

echo "════════ 完成 ════════"
cat "$OUT/rc-summary.txt"
