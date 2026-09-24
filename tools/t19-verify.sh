#!/bin/zsh
# T19 验证脚本（2026-09-24）。判据 + 回归，证据落 docs/evidence/2026-09-24-t19-time-ring/
#
# 用法：tools/t19-verify.sh          （跑全部）
#       tools/t19-verify.sh core     （只跑 T19 自检 + UI 自检 + T16/T15/T18-core 回归）
#       tools/t19-verify.sh regress  （只跑 A2/DYN/S3/T17/T18 回归）
#       tools/t19-verify.sh dyn 3    （只跑 DYN，跑 N 次；默认 3）
#
# 环境口径与 t17/t18-verify.sh 完全一致（同一后端 Metal：macOS 上 Vulkan 组合必丢设备，
# 见 T13 §7.27 的判定性实验）——**刻意不换口径**，否则跨任务读数不可比。
#
# ── T19 这一轮有两套自检，缺一不可 ──────────────────────────────────────────
#   ① `timecheck`    （STELQUICK_TIME_CHECK=1）：走 AppFacade 的 C++ 公共 API，
#      验"换算与写入逻辑对"（往返恒等 / 与旧对话框公式逐位一致 / UTC 往返 /
#      非法拒绝 / 星空随动 / 对照不动 / 可逆 / 现在 / 不被帧泵拽回）。
#   ② `timeuicheck`  （STELQUICK_TIME_UI_CHECK=1）：走**真实 QML 控件 + 窗口真实
#      鼠标事件**，验"接线是活的"（六个自旋框的 ×6 写入路径 / 应用 / 重置 / 现在 /
#      步进透传 / 状态行绑定）。
#   ②存在的理由：①结构上**测不到** QML 的 onClicked 与属性绑定 —— T15 的 `Keys`
#   挂错类型、QML 键盘路由整段死代码，就是这么埋了两个任务才被人肉发现。
#   ②首跑即抓到一条真实缺陷：`lastTimeRefusal` 当时只是 Q_INVOKABLE 方法，
#   QML 里 `=== "ok"` 恒 false ⇒ "已生效"分支是死代码，而 timeRefusalText() 在 ok
#   时返回空串 ⇒ **写入成功反而显示空白 + 红字**。现已是 Q_PROPERTY + NOTIFY。
#   反向对照（把 Q_PROPERTY 临时注掉后重跑）落在
#   `timeuicheck-negctrl-broken-property.txt`：UI-09a/09b 双双 FAIL、11/13、rc=10。
#
# ── DYN 判据为什么要跑 N 次 + 为什么同时跑「替身生产者」 ────────────────────
# 与 T18 同一处置（读 `dyn-ab-baseline-vs-t18.txt` §3/§6 的完整定性）：
# `D1-C02 显示 / D1-C07 降级`量的是"QML 场景图有没有在渲染"，它要求**窗口被暴露**。
# 本机（M3 / macOS / Metal RHI）实测该量**受环境支配**：
#   · 低负载：真引擎与 QML 同进程时偶发停摆（≈33%），替身生产者必 PASS；
#   · 高负载（Spotlight mdbulkimport + installd 装包，load avg ≈ 5.2）：**替身也全败**；
#   · 会话锁屏 / 显示器休眠：**两侧都全程 0 帧**，且 `caffeinate -dims`/`-dimsu` **无效**。
# 处置：跑 N 次如实报 `N/3`、**不跑"直到绿"**；替身对照也失败 ⇒ 该读数是**「仪器
# 测不到」**，不作为退化证据；**但也不改写成 INVALID**（原始 FAIL 照实保留，只加定性）。
# **不得**把 `N/3` 里的任一次 PASS 当成"零退化已证明"。
set -u
cd /Users/ztuqfvy/qt_demo/stellarium_vulkan

export VK_DRIVER_FILES=/opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json
export QT_VULKAN_LIB=/opt/homebrew/opt/vulkan-loader/lib/libvulkan.1.dylib
export STELQUICK_GRAPHICS_API=metal

BIN=./build-release/src/ui/stelQuickUI.app/Contents/MacOS/stelQuickUI
OUT=docs/evidence/2026-09-24-t19-time-ring
TARGET=${1:-all}
DYN_RUNS=${2:-3}
mkdir -p "$OUT"

# rc-summary 每轮从头累积（否则重跑会与上一轮叠加，读串）。
# `dyn` 子命令只补跑 DYN，此时保留既有的其它项读数，但要**先摘掉上一轮的 DYN 行**
# ——否则同一项会叠成两行（T18 真踩到过：7 行变 9 行）。
if [[ "$TARGET" == "dyn" ]]; then
  if [[ -f "$OUT/rc-summary.txt" ]]; then
    grep -vE "dyn-(engine|stub)-metal" "$OUT/rc-summary.txt" \
      > "$OUT/rc-summary.txt.tmp" 2>/dev/null || : > "$OUT/rc-summary.txt.tmp"
    mv "$OUT/rc-summary.txt.tmp" "$OUT/rc-summary.txt"
  fi
else
  : > "$OUT/rc-summary.txt"
fi

# 环境注记：帧率类读数会被机器负载/显示器状态污染（T13 起既有纪律）。把当时的环境
# 写进证据，否则"今天 0/3、昨天 2/3"这种差异事后无法解释。
env_note() {
  echo "环境：$(uptime | sed 's/^ *//')"
  local ds
  ds=$(pmset -g custom 2>/dev/null | grep -E "^[[:space:]]*displaysleep" | head -1 | awk '{print $2}')
  [[ -n "$ds" ]] && echo "环境注记：displaysleep=${ds} 分钟（显示器空闲即休眠）"
  if pgrep -q mdbulkimport; then
    echo "环境注记：Spotlight 批量索引正在运行（mdbulkimport）——帧率读数可能被压低"
  fi
  echo '读数采信规则：D1-C02/D1-C07 量的是「窗口有没有在渲染」——它要求窗口被暴露。'
  echo '  若同条件的**替身对照**也失败（尤其"全程推进 0 帧"），则该读数反映的是'
  echo '  「仪器测不到」而不是「被测程序失败」，**不作为退化证据**；'
  echo '  同时也不改写成 INVALID（原始读数照实保留，只加这条定性）。'
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
    echo "定性：本机为环境敏感/间歇（T18 的 A/B 记录见 dyn-ab-baseline-vs-t18.txt）。"
    echo "零退化的依据是同口径 A/B + 替身路径不含本任务代码，不是本行的某一次绿色。"
  } >> "$OUT/regression-dyn-engine-metal.txt"
  cat "$OUT/regression-dyn-engine-metal.txt"
  echo "regression-dyn-engine-metal $pass/$n PASS（间歇，见 dyn-ab-baseline-vs-t18.txt）" \
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
    echo "替身路径不含时间页代码（DYN 用 startPage=\"sky\"）⇒ 它失败即与本次改动无关。"
  } >> "$OUT/regression-dyn-stub-metal.txt"
  cat "$OUT/regression-dyn-stub-metal.txt"
  echo "regression-dyn-stub-metal $pass/$n PASS（判别性对照：替身不含 T19 代码）" \
    | tee -a "$OUT/rc-summary.txt"
}

if [[ "$TARGET" == "all" || "$TARGET" == "core" ]]; then
  # ① T19 时间自检（C++ 侧：往返恒等 / 公式一致 / UTC 往返 / 非法拒绝 /
  #    星空随动成对判据 / 对照不动 / 可逆 / 现在 / 不被帧泵拽回）
  STELQUICK_TIME_CHECK=1 "$BIN" > "$OUT/timecheck-mac.txt" 2>&1
  echo "timecheck-mac rc=$?" | tee -a "$OUT/rc-summary.txt"

  # ①b T19 **UI 层端到端**自检（objectName 锚点 + 窗口真实鼠标事件，13 条判据）。
  #     与①的区别：①全程走 AppFacade 公共 API，证明不了 QML 的 onClicked 接线
  #     与属性绑定是活的。②首跑即抓到 lastTimeRefusal 缺 Q_PROPERTY 的真实缺陷。
  STELQUICK_TIME_UI_CHECK=1 "$BIN" > "$OUT/timeuicheck-mac.txt" 2>&1
  echo "timeuicheck-mac rc=$?" | tee -a "$OUT/rc-summary.txt"

  # ② T18 定位/跟踪自检 + UI 自检（T19 动了 AppFacade 的 setTimeRefusal 与
  #    SearchPage 的 refusal 绑定 ⇒ T18 的两套都要复查）
  STELQUICK_LOCATE_CHECK=1 "$BIN" > "$OUT/regression-locatecheck.txt" 2>&1
  echo "regression-locatecheck rc=$?" | tee -a "$OUT/rc-summary.txt"
  STELQUICK_UI_CHECK=1 "$BIN" > "$OUT/regression-locate-uicheck.txt" 2>&1
  echo "regression-locate-uicheck rc=$?" | tee -a "$OUT/rc-summary.txt"

  # ③ T16 时钟纯逻辑自检（T19 的时间换算建在它上面，最关键的一套）
  STELQUICK_CLOCK_CHECK=1 "$BIN" > "$OUT/regression-clockcheck.txt" 2>&1
  echo "regression-clockcheck rc=$?" | tee -a "$OUT/rc-summary.txt"

  # ④ T15/T16 命令通路 + 集成自检（T19 又扩展了 AppFacade，AC-* 必须仍全绿）
  STELQUICK_ACTION_CHECK=1 "$BIN" > "$OUT/regression-actioncheck.txt" 2>&1
  echo "regression-actioncheck rc=$?" | tee -a "$OUT/rc-summary.txt"
fi

if [[ "$TARGET" == "all" || "$TARGET" == "regress" ]]; then
  # ⑤ T17 搜索/选择模型自检
  STELQUICK_SEARCH_CHECK=1 "$BIN" > "$OUT/regression-searchcheck.txt" 2>&1
  echo "regression-searchcheck rc=$?" | tee -a "$OUT/rc-summary.txt"

  # ⑥ 回归：A2 静态纹理（Metal）
  STELQUICK_A2_CHECK=1 "$BIN" > "$OUT/regression-a2-metal.txt" 2>&1
  echo "regression-a2-metal rc=$?" | tee -a "$OUT/rc-summary.txt"

  # ⑦ 回归：DYN 动态帧（真实引擎生产者 + Metal）—— 间歇量，跑 N 次
  run_dyn
  # ⑦b 同项判别性对照：替身生产者（不 boot 引擎，路径不含 T19 代码）
  run_dyn_stub

  # ⑧ 回归：S3 引擎集成（旧宿主 stellarium —— EngineWallClock 路径零退化）
  STELA3_CHECK=1 ./build-release/src/stellarium > "$OUT/regression-s3-stela3.txt" 2>&1
  echo "regression-s3-stela3 rc=$?" | tee -a "$OUT/rc-summary.txt"
fi

if [[ "$TARGET" == "dyn" ]]; then
  run_dyn
  run_dyn_stub
fi

echo "════════ 完成 ════════"
cat "$OUT/rc-summary.txt"
