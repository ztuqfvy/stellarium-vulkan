#!/bin/zsh
# T17 验证脚本（2026-09-25）。全部判据 + 回归，证据落 docs/evidence/2026-09-25-t17-models/
#
# 用法：tools/t17-verify.sh          （跑全部）
#       tools/t17-verify.sh core     （只跑 T17 自检）
#       tools/t17-verify.sh regress  （只跑回归）
set -u
cd /Users/ztuqfvy/qt_demo/stellarium_vulkan

export VK_DRIVER_FILES=/opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json
export QT_VULKAN_LIB=/opt/homebrew/opt/vulkan-loader/lib/libvulkan.1.dylib
export STELQUICK_GRAPHICS_API=metal

BIN=./build-release/src/ui/stelQuickUI.app/Contents/MacOS/stelQuickUI
OUT=docs/evidence/2026-09-25-t17-models
TARGET=${1:-all}
mkdir -p "$OUT"

run() {   # run <名字> <env 赋值...> -- <二进制>
  local name=$1; shift
  echo "──────── $name ────────"
  "$@" > "$OUT/$name.txt" 2>&1
  local rc=$?
  echo "$name rc=$rc" | tee -a "$OUT/rc-summary.txt"
  return $rc
}

: > "$OUT/rc-summary.txt"

if [[ "$TARGET" == "all" || "$TARGET" == "core" ]]; then
  # ① T17 模型自检（阶段 A 逻辑 + 阶段 B 纵向 + SRC-05 上限）
  STELQUICK_SEARCH_CHECK=1 "$BIN" > "$OUT/searchcheck-mac.txt" 2>&1
  echo "searchcheck-mac rc=$?" | tee -a "$OUT/rc-summary.txt"

  # ② T16 时钟纯逻辑自检（回归：T17 未碰时钟，应仍 12/12）
  STELQUICK_CLOCK_CHECK=1 "$BIN" > "$OUT/regression-clockcheck.txt" 2>&1
  echo "regression-clockcheck rc=$?" | tee -a "$OUT/rc-summary.txt"

  # ③ T15/T16 命令通路 + 集成自检（回归：AppFacade 被 T17 扩展过）
  STELQUICK_ACTION_CHECK=1 "$BIN" > "$OUT/regression-actioncheck.txt" 2>&1
  echo "regression-actioncheck rc=$?" | tee -a "$OUT/rc-summary.txt"
fi

if [[ "$TARGET" == "all" || "$TARGET" == "regress" ]]; then
  # ④ 回归：A2 静态纹理（Metal）
  STELQUICK_A2_CHECK=1 "$BIN" > "$OUT/regression-a2-metal.txt" 2>&1
  echo "regression-a2-metal rc=$?" | tee -a "$OUT/rc-summary.txt"

  # ⑤ 回归：DYN 动态帧（真实引擎生产者 + Metal）
  STELQUICK_DYN_CHECK=1 STELQUICK_DYN_PRODUCER=engine "$BIN" > "$OUT/regression-dyn-engine-metal.txt" 2>&1
  echo "regression-dyn-engine-metal rc=$?" | tee -a "$OUT/rc-summary.txt"

  # ⑥ 回归：S3 引擎集成（旧宿主 stellarium.exe —— EngineWallClock 路径零退化）
  STELA3_CHECK=1 ./build-release/src/stellarium > "$OUT/regression-s3-stela3.txt" 2>&1
  echo "regression-s3-stela3 rc=$?" | tee -a "$OUT/rc-summary.txt"
fi

echo "════════ 完成 ════════"
cat "$OUT/rc-summary.txt"
