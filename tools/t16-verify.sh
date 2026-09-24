#!/bin/zsh
# T16 验证脚本（2026-09-24）。全部判据 + 回归，证据落 docs/evidence/2026-09-24-t16-sim-clock/
set -u
cd /Users/ztuqfvy/qt_demo/stellarium_vulkan

export VK_DRIVER_FILES=/opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json
export QT_VULKAN_LIB=/opt/homebrew/opt/vulkan-loader/lib/libvulkan.1.dylib
export STELQUICK_GRAPHICS_API=metal

BIN=./build-release/src/ui/stelQuickUI.app/Contents/MacOS/stelQuickUI
OUT=docs/evidence/2026-09-24-t16-sim-clock
mkdir -p "$OUT"

run() {   # run <名字> <env...> -- <命令>
  local name=$1; shift
  echo "──────── $name ────────"
  "$@" > "$OUT/$name.txt" 2>&1
  local rc=$?
  echo "$name rc=$rc" | tee -a "$OUT/rc-summary.txt"
  echo "rc=$rc → $OUT/$name.txt"
  return $rc
}

: > "$OUT/rc-summary.txt"

# ① 纯逻辑自检（无窗口 / 无引擎）
STELQUICK_CLOCK_CHECK=1 "$BIN" > "$OUT/clockcheck-logic.txt" 2>&1
echo "clockcheck-logic rc=$?" | tee -a "$OUT/rc-summary.txt"

# ② 集成自检（T15 判据 + T16 判据）
STELQUICK_ACTION_CHECK=1 "$BIN" > "$OUT/actioncheck-mac.txt" 2>&1
echo "actioncheck-mac rc=$?" | tee -a "$OUT/rc-summary.txt"

# ③ 回归：A2 静态纹理（Metal）
STELQUICK_A2_CHECK=1 "$BIN" > "$OUT/regression-a2-metal.txt" 2>&1
echo "regression-a2-metal rc=$?" | tee -a "$OUT/rc-summary.txt"

# ④ 回归：DYN 动态帧（真实引擎生产者 + Metal）
STELQUICK_DYN_CHECK=1 STELQUICK_DYN_PRODUCER=engine "$BIN" > "$OUT/regression-dyn-engine-metal.txt" 2>&1
echo "regression-dyn-engine-metal rc=$?" | tee -a "$OUT/rc-summary.txt"

# ⑤ 回归：S3 引擎集成（旧宿主 stellarium.exe —— 证明 EngineWallClock 路径零退化）
STELA3_CHECK=1 ./build-release/src/stellarium > "$OUT/regression-s3-stela3.txt" 2>&1
echo "regression-s3-stela3 rc=$?" | tee -a "$OUT/rc-summary.txt"

echo "════════ 完成 ════════"
cat "$OUT/rc-summary.txt"
