#!/bin/zsh
# T18 验证脚本（2026-09-24）。判据 + 回归，证据落 docs/evidence/2026-09-24-t18-locate-track/
#
# 用法：tools/t18-verify.sh          （跑全部）
#       tools/t18-verify.sh core     （只跑 T18 自检 + T16/T15 回归）
#       tools/t18-verify.sh regress  （只跑 A2/DYN/S3/T17 回归）
#       tools/t18-verify.sh dyn 3    （只跑 DYN，跑 N 次；默认 3）
#
# 环境口径与 t17-verify.sh 完全一致（同一后端 Metal：macOS 上 Vulkan 组合必丢设备，
# 见 T13 §7.27 的判定性实验）——**刻意不换口径**，否则跨任务读数不可比。
#
# ── DYN 判据为什么要跑 N 次 ─────────────────────────────────────────────────
# `D1-C02 显示 / D1-C07 降级` 量的是"QML 场景图有没有在渲染"。真实引擎与 QML
# 同进程共存时，本机（M3 / macOS / Metal RHI）实测**间歇性**让 QML 侧停摆：
# 生产者照跑 50fps、邮箱帧龄个位数毫秒，而 displayedFrameNumber 停住不动，
# 日志里**没有任何**丢设备（vkDebug / VK_ERROR / swapchain）痕迹——静默停摆。
#
# 2026-09-24 的定性实验（判据：把这当"退化"还是"环境"）：
#   · T17 基线二进制（git stash 掉 T18 全部改动后重建） 9 次 → 6 PASS / 3 FAIL
#   · T18 二进制（含全部改动）                        9 次 → 6 PASS / 3 FAIL
#   两者失败率一致（33%），失败样本形状一致（0.0 fps 尾窗、无丢设备日志）
#   ⇒ **判定为既有环境间歇缺陷，不是 T18 引入的退化**。原始日志见
#     dyn-ab-baseline-vs-t18.txt。所以这里跑 3 次如实报 "N/3"，不跑"直到绿"。
#   注意：**不得**把 N/3 里的任一次 PASS 当成"零退化已证明"——零退化的证据是
#   上面那条"基线与本任务同口径同日 A/B 失败率相同"，不是某一次的绿色。
#
# ── 为什么要同时跑「替身生产者」那一半 ──────────────────────────────────────
# 只跑引擎侧会出现单侧读数、无法区分两种世界：
#   世界 A：只有"真实引擎 + QML 同进程"这条路会停摆  → 故障在共存链路
#   世界 B：整个 QML 消费者在负载下都会停摆            → 故障与引擎无关
# 2026-09-24 13:2x 实测落到了世界 B：机器处于高负载（Spotlight mdbulkimport 批量
# 索引 + system_installd 装包，load avg ≈ 5.2），**替身生产者 4/4 FAIL**
# （推进 73/80/84/138 帧后停摆，签名与引擎侧完全一致）。
# 13:3x 更进一步：两侧都变成**全程 0 帧**（一帧都没渲），且 `caffeinate -dimsu`
# （顶住显示器/系统休眠 + 声明用户活动）**无效** ⇒ 最可能是窗口未被暴露（会话锁屏 /
# 显示器睡眠）。此时 D1-C02/C07 **不可测**，不是被测程序失败。
# 这是本机既有的待查项（T13 已记过 `2026-09-23-sky-longrun-INVALID-screensaver`
# 同一类环境坑），**不是本任务的结论**；T18 只负责证明它与本次改动无关。
# 关键点：替身路径上**不含任何 T18 代码**——DYN 用 startPage="sky"，连 SearchPage
# 都不加载——所以这一半失败反而更干净地证明"与本次改动无关"。
# 由此：失败率是**负载相关**的量；低负载下引擎侧 ~33%、替身 ~0%，高负载下两者都高。
# 归档两个数（`regression-dyn-engine-metal` 与 `regression-dyn-stub-metal`）、
# 外加 `env_note` 负载注记，才不会被"今天失败了"带偏。
set -u
cd /Users/ztuqfvy/qt_demo/stellarium_vulkan

export VK_DRIVER_FILES=/opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json
export QT_VULKAN_LIB=/opt/homebrew/opt/vulkan-loader/lib/libvulkan.1.dylib
export STELQUICK_GRAPHICS_API=metal

BIN=./build-release/src/ui/stelQuickUI.app/Contents/MacOS/stelQuickUI
OUT=docs/evidence/2026-09-24-t18-locate-track
TARGET=${1:-all}
DYN_RUNS=${2:-3}
mkdir -p "$OUT"

# rc-summary 每轮从头累积（否则重跑会与上一轮叠加，读串）。
# `dyn` 子命令只补跑 DYN，此时保留既有的其它项读数，但要**先摘掉上一轮的 DYN 行**
# ——否则同一项会叠成两行（本次真踩到过：7 行变 9 行）。
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

# DYN：跑 N 次，汇总写 regression-dyn-engine-metal.txt，逐次日志写 ...-runk.txt
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
    echo "定性：本机为间歇缺陷（2026-09-24 A/B：T17 基线 6/9 与 T18 6/9 失败率相同）。"
    echo "零退化的依据是同口径 A/B，不是本行的某一次绿色。见 dyn-ab-baseline-vs-t18.txt。"
  } >> "$OUT/regression-dyn-engine-metal.txt"
  cat "$OUT/regression-dyn-engine-metal.txt"
  echo "regression-dyn-engine-metal $pass/$n PASS（间歇，见 dyn-ab-baseline-vs-t18.txt）" \
    | tee -a "$OUT/rc-summary.txt"
}

# DYN 的**判别性对照**：同一个消费者（QML 窗口 + SkyViewport），换成替身生产者
# （不 boot 引擎）。为什么必须每次一起跑：
#   · 替身路径上**不含任何 T18 代码**（DYN 用 startPage="sky"，SearchPage 都不加载）
#     ⇒ 它若也失败，就与"本次改动"完全无关，是消费者/环境侧的问题；
#   · 它若 PASS 而引擎侧 FAIL，则故障定位在"真实引擎与 QML 同进程共存"这条路上。
#   · 2026-09-24 实测：低负载下替身 **必 PASS**；高负载（Spotlight 批量索引 +
#     installd，load avg ≈ 5）下替身 **同样 4/4 FAIL**（推进 73~84 帧后停摆）。
#     ⇒ 停摆本身与引擎共存**无关**，引擎共存只是显著提高发生率。两个数一起报，
#       才不会被"今天失败了"这种单侧读数带偏。
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
    echo "替身路径不含任何 T18 代码；它失败 ⇒ 与本次改动无关（消费者/环境侧）。"
  } >> "$OUT/regression-dyn-stub-metal.txt"
  cat "$OUT/regression-dyn-stub-metal.txt"
  echo "regression-dyn-stub-metal $pass/$n PASS（判别性对照：替身不含 T18 代码）" \
    | tee -a "$OUT/rc-summary.txt"
}

if [[ "$TARGET" == "all" || "$TARGET" == "core" ]]; then
  # ① T18 定位/跟踪自检（守卫 / 锁定角距 / 推进时间判别性对照 / 幂等重选）
  STELQUICK_LOCATE_CHECK=1 "$BIN" > "$OUT/locatecheck-mac.txt" 2>&1
  echo "locatecheck-mac rc=$?" | tee -a "$OUT/rc-summary.txt"

  # ①b T18 **UI 层端到端**自检（objectName 锚点 + 窗口真实鼠标事件）。
  #     与①的区别：①全程走 AppFacade 公共 API，证明不了 QML 的 onClicked 接线是活的。
  #     本项从最外层注入真实点击，断言引擎状态因此改变（8 条判据 UI-01..08）。
  STELQUICK_UI_CHECK=1 "$BIN" > "$OUT/uicheck-mac.txt" 2>&1
  echo "uicheck-mac rc=$?" | tee -a "$OUT/rc-summary.txt"

  # ② T16 时钟纯逻辑自检（T18 未碰时钟，应仍 12/12）
  STELQUICK_CLOCK_CHECK=1 "$BIN" > "$OUT/regression-clockcheck.txt" 2>&1
  echo "regression-clockcheck rc=$?" | tee -a "$OUT/rc-summary.txt"

  # ③ T15/T16 命令通路 + 集成自检（T18 扩展了 AppFacade，AC-* 必须仍全绿）
  STELQUICK_ACTION_CHECK=1 "$BIN" > "$OUT/regression-actioncheck.txt" 2>&1
  echo "regression-actioncheck rc=$?" | tee -a "$OUT/rc-summary.txt"
fi

if [[ "$TARGET" == "all" || "$TARGET" == "regress" ]]; then
  # ④ T17 搜索/选择模型自检（T18 改了 ObjectInfoModel::selectByStableId 的幂等闸，
  #    这是最需要复查的一套：SRC-03/04 直接压在被改的那条路径上）
  STELQUICK_SEARCH_CHECK=1 "$BIN" > "$OUT/regression-searchcheck.txt" 2>&1
  echo "regression-searchcheck rc=$?" | tee -a "$OUT/rc-summary.txt"

  # ⑤ 回归：A2 静态纹理（Metal）
  STELQUICK_A2_CHECK=1 "$BIN" > "$OUT/regression-a2-metal.txt" 2>&1
  echo "regression-a2-metal rc=$?" | tee -a "$OUT/rc-summary.txt"

  # ⑥ 回归：DYN 动态帧（真实引擎生产者 + Metal）—— 间歇量，跑 N 次
  run_dyn
  # ⑥b 同项判别性对照：替身生产者（不 boot 引擎，路径不含 T18 代码）
  run_dyn_stub

  # ⑦ 回归：S3 引擎集成（旧宿主 stellarium —— EngineWallClock 路径零退化）
  STELA3_CHECK=1 ./build-release/src/stellarium > "$OUT/regression-s3-stela3.txt" 2>&1
  echo "regression-s3-stela3 rc=$?" | tee -a "$OUT/rc-summary.txt"
fi

if [[ "$TARGET" == "dyn" ]]; then
  run_dyn
  run_dyn_stub
fi

echo "════════ 完成 ════════"
cat "$OUT/rc-summary.txt"
