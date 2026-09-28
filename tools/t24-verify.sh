#!/bin/zsh
# T24 验证脚本（2026-09-28）。判据 + 回归，证据落 docs/evidence/2026-09-28-t24-pinyin-search/
#
# 用法：tools/t24-verify.sh          （跑全部）
#       tools/t24-verify.sh core     （T24 拼音检索 + 相邻回归）
#       tools/t24-verify.sh regress  （A2/DYN/S3 + 搜索模型回归）
#       tools/t24-verify.sh dyn 3    （只跑 DYN，跑 N 次；默认 3）
#
# 环境口径与 t17..t22-verify.sh **完全一致**（同一后端 Metal）——**刻意不换口径**。
#
# ── T24 的正题：搜索排序的拼音检索（A4 加固项）──────────────────────────────
# 中文界面下天体名是翻译名（"月球"），键盘只能敲 ASCII ⇒ 引擎字面 contains
# 对 "yueqiu" 必然空手而归。这是**召回**问题（T21 头注写明的边界之外），必须
# 在模型层补一个拼音候选源。判据分两条腿：
#
# ① 纯逻辑腿（PINY-01..05）：表加载 / 读音转换 / 分档判定 / 查询形态 / CJK 判定，
#    环境无关恒可跑。分档插位有讲究：PinyinFull 在 WordStart 与 Substring 之间
#    （用户特意打的读音强于字面碰巧包含）；PinyinInitial 垫底（误命中率最高）。
# ② 活引擎腿（PINY-06..08）：切 zh_CN 后搜 "yueqiu" 首行必须 = 月球(Planet:Moon)
#    且 lastPinyinMatchCount()>0（**成对**：计数证明首行来自拼音分支而非字面命中）；
#    再搜 "yq"（首字母档）首行同样是月球。**段内切语言、段尾必还原**（TC-18 血泪）。
# ③ 负控（T21 先例）：临时把 collect() 的拼音分支关掉（`false && ...`）⇒
#    PINY-06/07/08 必须红。见证据目录 negctrl/。
#
# ⚠️ 连带修正（"判据别为上游缺陷背书"的反向应用——**判据别为旧现状背书**）：
# SRC-05b/05c 原口径 "raw ≥ rows" 在拼音候选生效后会报**假红**（拼音候选也是
# 截断前候选，但不计入 raw）⇒ 候选池口径改为 raw + pinyin。
#
# ⚠️ 翻译布局坑（本轮实测抓到）：引擎 StelFileMgr::getLocaleDir() 按
# applicationDirPath/../translations 找 .qm，bundle 布局下三个候选全不命中
# ⇒ "Couldn't load translations"、天体名退回英文、拼音整体失效。
# 处置：POST_BUILD 把主域 zh_CN.qm 拷进 bundle（src/ui/CMakeLists.txt）。
#
# 拼音表：mozillazg/pinyin-data v0.15.0（MIT），构建期转无声调精简格式
# （44435 条，544KB），qrc 嵌入 /StelQuickUI/pinyin-lite.txt，懒加载 + memo。
# 数据转换脚本不留档（一次性 Python，逻辑 20 行；源文件上游可得）。
#
# ── 类型分组（移交表里的另一半）─────────────────────────────────────────────
# T21 的 typeWeight 已是排序键次序因子（同 quality 内太阳系天体聚集）。
# UI 分段呈现评估后**不做**：搜索结果 ≤20 行、分段标题挤占行高，信息密度负收益。
# 记录为产品决策，理由在交付文档 T24。
#
# ── DYN 判据为什么要跑 N 次 + 替身对照 ──────────────────────────────────────
# 与 T18..T22 同一处置（完整定性见 2026-09-24-t18-locate-track/dyn-ab-baseline-vs-t18.txt）。
set -u
cd /Users/ztuqfvy/qt_demo/stellarium_vulkan

export VK_DRIVER_FILES=/opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json
export QT_VULKAN_LIB=/opt/homebrew/opt/vulkan-loader/lib/libvulkan.1.dylib
export STELQUICK_GRAPHICS_API=metal

BIN=./build-release/src/ui/stelQuickUI.app/Contents/MacOS/stelQuickUI
OUT=docs/evidence/2026-09-28-t24-pinyin-search
TARGET=${1:-all}
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
    echo "环境注记：Spotlight 批量索引正在运行（mdbulkimport）——帧率读数可能被压低"
  fi
  echo '读数采信规则：D1-C02/D1-C07 量的是「窗口有没有在渲染」——它要求窗口被暴露。'
  echo '  若同条件的**替身对照**也失败，则该读数反映「仪器测不到」而不是「被测程序失败」，'
  echo '  不作为退化证据；也不改写成 INVALID（原始读数照实保留，只加这条定性）。'
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
    echo "替身路径不含 T24 拼音检索代码（DYN 用 startPage=\"sky\" 且不点任何按钮）"
    echo "⇒ 它失败即与本次改动无关。"
  } >> "$OUT/regression-dyn-stub-metal.txt"
  cat "$OUT/regression-dyn-stub-metal.txt"
  echo "regression-dyn-stub-metal $pass/$n PASS（判别性对照：替身不含 T24 代码）" \
    | tee -a "$OUT/rc-summary.txt"
}

if [[ "$TARGET" == "all" || "$TARGET" == "core" ]]; then
  # ① T24 正题：SEARCHCHECK（34 → 51，新增 PINY-01..08 + SRC-05b/c 口径修正）
  STELQUICK_SEARCH_CHECK=1 "$BIN" > "$OUT/searchcheck-mac.txt" 2>&1
  echo "searchcheck-mac rc=$?" | tee -a "$OUT/rc-summary.txt"

  # ② 相邻回归：T22 时间页两套（TimePage/AppFacade 相邻；本轮动了 SearchRanker.hpp 枚举）
  STELQUICK_TIME_CHECK=1 "$BIN" > "$OUT/regression-timecheck.txt" 2>&1
  echo "regression-timecheck rc=$?" | tee -a "$OUT/rc-summary.txt"
  STELQUICK_TIME_UI_CHECK=1 "$BIN" > "$OUT/regression-timeuicheck.txt" 2>&1
  echo "regression-timeuicheck rc=$?" | tee -a "$OUT/rc-summary.txt"

  # ③ 相邻回归：T20 返回环 + 全流程回放（依赖"搜月球首行是月球"⇒ 排序管线必回归）
  STELQUICK_RETURN_UI_CHECK=1 "$BIN" > "$OUT/regression-returnuicheck.txt" 2>&1
  echo "regression-returnuicheck rc=$?" | tee -a "$OUT/rc-summary.txt"
  STELQUICK_REPLAY_CHECK=1 "$BIN" > "$OUT/regression-replaycheck.txt" 2>&1
  echo "regression-replaycheck rc=$?" | tee -a "$OUT/rc-summary.txt"

  # ④ 相邻回归：T18 定位两套（SearchPage 相邻）
  STELQUICK_LOCATE_CHECK=1 "$BIN" > "$OUT/regression-locatecheck.txt" 2>&1
  echo "regression-locatecheck rc=$?" | tee -a "$OUT/rc-summary.txt"
  STELQUICK_UI_CHECK=1 "$BIN" > "$OUT/regression-locate-uicheck.txt" 2>&1
  echo "regression-locate-uicheck rc=$?" | tee -a "$OUT/rc-summary.txt"

  # ⑤ T16/T15 纯逻辑与命令通路
  STELQUICK_CLOCK_CHECK=1 "$BIN" > "$OUT/regression-clockcheck.txt" 2>&1
  echo "regression-clockcheck rc=$?" | tee -a "$OUT/rc-summary.txt"
  STELQUICK_ACTION_CHECK=1 "$BIN" > "$OUT/regression-actioncheck.txt" 2>&1
  echo "regression-actioncheck rc=$?" | tee -a "$OUT/rc-summary.txt"
fi

if [[ "$TARGET" == "all" || "$TARGET" == "regress" ]]; then
  # ⑥ 回归：A2 静态纹理（Metal）
  STELQUICK_A2_CHECK=1 "$BIN" > "$OUT/regression-a2-metal.txt" 2>&1
  echo "regression-a2-metal rc=$?" | tee -a "$OUT/rc-summary.txt"

  # ⑦ 回归：DYN（真实引擎 + 替身对照，N 次）
  run_dyn
  run_dyn_stub

  # ⑧ 回归：S3 引擎集成（旧宿主 —— 本轮虽未动 src/core，跑一次对齐既有口径）
  STELA3_CHECK=1 ./build-release/src/stellarium > "$OUT/regression-s3-stela3.txt" 2>&1
  echo "regression-s3-stela3 rc=$?" | tee -a "$OUT/rc-summary.txt"
fi

if [[ "$TARGET" == "dyn" ]]; then
  run_dyn
  run_dyn_stub
fi

echo "════════ 完成 ════════"
cat "$OUT/rc-summary.txt"
