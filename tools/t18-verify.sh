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
# `dyn` 子命令只补跑 DYN，此时保留既有的其它项读数。
[[ "$TARGET" != "dyn" ]] && : > "$OUT/rc-summary.txt"

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
    echo "DYN 显示/降级判据：$pass/$n 次 PASS"
    echo "定性：本机为间歇缺陷（2026-09-24 A/B：T17 基线 6/9 与 T18 6/9 失败率相同）。"
    echo "零退化的依据是同口径 A/B，不是本行的某一次绿色。见 dyn-ab-baseline-vs-t18.txt。"
  } >> "$OUT/regression-dyn-engine-metal.txt"
  cat "$OUT/regression-dyn-engine-metal.txt"
  echo "regression-dyn-engine-metal $pass/$n PASS（间歇，见 dyn-ab-baseline-vs-t18.txt）" \
    | tee -a "$OUT/rc-summary.txt"
}

if [[ "$TARGET" == "all" || "$TARGET" == "core" ]]; then
  # ① T18 定位/跟踪自检（守卫 / 锁定角距 / 推进时间判别性对照 / 幂等重选）
  STELQUICK_LOCATE_CHECK=1 "$BIN" > "$OUT/locatecheck-mac.txt" 2>&1
  echo "locatecheck-mac rc=$?" | tee -a "$OUT/rc-summary.txt"

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

  # ⑦ 回归：S3 引擎集成（旧宿主 stellarium —— EngineWallClock 路径零退化）
  STELA3_CHECK=1 ./build-release/src/stellarium > "$OUT/regression-s3-stela3.txt" 2>&1
  echo "regression-s3-stela3 rc=$?" | tee -a "$OUT/rc-summary.txt"
fi

if [[ "$TARGET" == "dyn" ]]; then
  run_dyn
fi

echo "════════ 完成 ════════"
cat "$OUT/rc-summary.txt"
