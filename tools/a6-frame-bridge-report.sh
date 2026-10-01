#!/bin/zsh
# A6 帧桥统计报告生成器（计划一 §7 契约第 6 条「分开的性能记录」+ 测试文档 §8
# A-1.0 出口第 2 条「P-BRG-01…04 全部通过，帧桥统计报告归档」）。
#
# 为什么是脚本而不是手写文档：A6 的数字必须能随二进制重跑而**重新生成**，
# 手抄的数字第二个人无法复核、也无法随代码演进更新。本脚本只做组装：
#   三腿（原 GL 基线 / A 阶段组合 / 桥接独立成本）各自的读数由
#   tools/frame-bridge-report.mjs 从 **原始 log + CSV** 机械复算，
#   本脚本把它们拼成 docs/A6_FRAME_BRIDGE_STATS.zh_CN.md。
#
# 用法：
#   tools/a6-frame-bridge-report.sh                 # 用归档（T9/T13）生成
#   tools/a6-frame-bridge-report.sh <本轮长跑 log> <逐秒 csv> <逐帧 csv>
#                                                   # A 阶段那一条腿换成本轮复跑
#
# ⚠️ PATH 污染（本项目两条已知毒源）：diff→DevEco、grep→WorkBuddy shim ⇒ 一律全路径。
set -u
export PATH="/opt/homebrew/bin:/usr/bin:/bin"

REPO="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO" || exit 1
# node 定位：优先 WorkBuddy 托管运行时（项目规范），退回 PATH。
for cand in \
  "$HOME/.workbuddy/binaries/node/versions/22.22.2-3/bin/node" \
  /opt/homebrew/bin/node \
  /usr/local/bin/node
do
  [ -x "$cand" ] && NODE="$cand" && break
done
: "${NODE:=$(command -v node)}"
[ -x "$NODE" ] || { echo "FAIL 找不到 node（试过托管运行时与 PATH）"; exit 2; }
OUT="docs/A6_FRAME_BRIDGE_STATS.zh_CN.md"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

# ── 三条腿的输入 ────────────────────────────────────────────────────────────
# ① 原 GL 基线（T9 LegacyLongRun：引擎 GL 出图 + 读回，**无 QML 消费者**）
BASE_LOG="docs/evidence/2026-09-22-stelt9-longrun-30min.txt"
BASE_FRAMES="docs/evidence/2026-09-22-stelt9-baseline-frames.csv.gz"
# ①b 一致性重跑（同日晚上，接电后；用来证明基线不是一次性侥幸）
BASE2_LOG="docs/evidence/2026-09-22-stelt9-consistency-reac/run.log"

# ② A 阶段组合（引擎 GL + 帧桥 + QML Vulkan 上屏）。默认用 T13 归档；
#    给了命令行参数则用本轮 A6 复跑（A4/A5 之后的形态）。
if [ $# -ge 3 ]; then
  A_LOG="$1"; A_CSV="$2"; A_FRAMES="$3"
  A_LABEL="A 阶段组合（**A6 本轮复跑**）"
  A_SRC="本轮复跑"
else
  D13="docs/evidence/2026-09-23-t13-live-longrun"
  A_LOG="$D13/18-official-run2-Metal-warm900+measure1800-PASS-11of11.log"
  A_CSV="$D13/18-official-run2-Metal-warm900+measure1800-PASS-11of11.csv.gz"
  A_FRAMES="$D13/18-official-run2.frames.csv.gz"
  A_LABEL="A 阶段组合（T13 归档，2026-09-23）"
  A_SRC="T13 归档（T42 之前形态 → **须以本轮复跑为准**）"
fi

echo "── 生成中 ──────────────────────────────────────────────"
echo "① 原 GL 基线        : $BASE_LOG"
echo "② A 阶段组合        : $A_LOG  [$A_SRC]"

# ── 逐腿复算 ────────────────────────────────────────────────────────────────
# ⚠️ **必须检查退出码**：工具在"日志结论 ≠ CSV 复算"时会 exit 1 —— 若不看 rc，
#    报告照样生成，告警只落在 stderr 里没人看（就成了装饰品）。
"$NODE" tools/frame-bridge-report.mjs "$BASE_LOG" --frames "$BASE_FRAMES" \
        --label "① 原 GL 基线（T9：引擎 GL 出图 + 读回，无 QML 消费者）" > "$TMP/arm1.md" 2>"$TMP/arm1.err"
RC1=$?

"$NODE" tools/frame-bridge-report.mjs "$A_LOG" --csv "$A_CSV" --frames "$A_FRAMES" \
        --label "② $A_LABEL" > "$TMP/arm2.md" 2>"$TMP/arm2.err"
RC2=$?

if [ "$RC1" -ne 0 ] || [ "$RC2" -ne 0 ]; then
  echo "🔴 复算一致性告警：arm1 rc=${RC1}｜arm2 rc=$RC2 ⇒ 报告中相应数字**与日志结论不一致**，须人工核对"
  CONSIST="🔴 **复算一致性告警**（arm1 rc=${RC1}｜arm2 rc=${RC2}）—— 上表数字与日志结论不一致，须人工核对"
else
  echo "✅ 复算一致性：两腿 rc=0（CSV 复算与日志结论逐位一致）"
  CONSIST="✅ 两腿 rc=0：CSV 复算与日志结论**逐位一致**（工具内建一致性硬门）"
fi

# ── 组装文档 ────────────────────────────────────────────────────────────────
{
  cat <<'HDR'
# A6 帧桥统计报告（P-BRG-01…04）

> **本文由 `tools/a6-frame-bridge-report.sh` 生成**（读原始 log + CSV 机械复算，
> 不手抄数字）。重跑二进制后重新执行该脚本即可刷新。
> 单页总览见 `docs/PROGRESS_SNAPSHOT.zh_CN.md`；任务文档见 `docs/T43*.md`。

---

## 1. 这份报告要回答什么

两处文档口径：

| 出处 | 原话 |
|---|---|
| 开发计划一 §7 契约第 6 条 | 「**分开的性能记录**：原 GL 基线、A 阶段 GL+帧桥+QML Vulkan、桥接独立成本」 |
| 测试文档 §8 A-1.0 出口第 2 条 | 「P-BRG-01…04 全部通过，**帧桥统计报告归档**」 |
| 开发指导文档 A6 行 | 「按软件测试文档执行全部回归；**输出帧桥统计（平均/p95 帧年龄、读回/上传时间、队列长度、内存）**」 |

即：既要**四条 P-BRG 判据全过**，也要把**帧桥的代价单独记一笔**，
不能让"合流形态慢一点"这件事糊在总账里。

---

## 2. 三分账的定义（三腿各量什么，为什么这么分）

| 腿 | 量什么 | 数据来源 | 为什么单列 |
|---|---|---|---|
| **① 原 GL 基线** | 引擎 OpenGL **出图 + 读回**，无 QML 消费者（`LegacyLongRun`，T9） | `render_ms` / `readback_ms` / `total_ms` | 这是"旧渲染器本身多快"的**下界**：帧桥还没接消费者 |
| **② A 阶段组合** | 引擎 GL + 帧桥 + **QML Vulkan 上屏**（`SkyLongRun` producer=engine，T13 起） | `SL-C01..C11` | 这才是**用户实际看到的那条路** |
| **③ 桥接独立成本** | 帧桥**新增**的那两笔开销：GPU→CPU `readback` + CPU→GPU `upload` | ①的 `readback_ms` + ②的 `SL-C05` | 契约要求把"桥的代价"从"渲染的代价"里剥出来 —— 计划二（原生 Vulkan 天空）要拿它做取舍依据 |

口径纪律：
- 所有数字都从**原始 log + 逐秒/逐帧 CSV** 复算（`tools/frame-bridge-report.mjs`），
  日志结论与 CSV 复算**不一致时报告会显式告警**，不静默取一个。
- **稳态窗 = 测量段的后 2/3**（`warmup + measure/3`，对齐 T9「最后 1200s」口径）。
  `SL-C04/05/06/09/10` 用 measure 全段；`SL-C01/02/03/07/11` 用稳态窗。
  混用会让复算值对不上日志（本工具首版就栽在这）。

---

## 3. 三腿读数

HDR

  cat "$TMP/arm1.md"
  echo
  cat "$TMP/arm2.md"
  echo
  cat <<'TAIL'

**③ 桥接独立成本**（从上两腿中抽出，不另跑）：

| 项 | 值 | 来源 |
|---|---|---|
| GPU→CPU 读回 `readback` | 见 ① 表 | `LegacyLongRun` 逐帧 `readback_ms` |
| CPU→GPU 上传 `upload` | 见 ② 表「上传（读回→上传）」 | `SkyLongRun` `SL-C05` / 逐秒 CSV 增量 |
| 邮箱队列 | 见 ② 表「邮箱丢弃 / 完整槽」 | `FrameMailbox::Stats` |

> 这两项在**原版 Stellarium 直接上屏**时**不存在** —— 它们是帧桥为"把 GL 画面交给
> Qt Quick 的 Vulkan 渲染器"付出的代价。计划二把旧天空换成原生 Vulkan 后，
> 这两笔开销**应当消失**，届时与本表对比即为收益的直接度量。

---

## 4. P-BRG-01…04 判据结论

| 编号 | 用例 | 判据落在哪 | 结论 |
|---|---|---|---|
| P-BRG-01 | 默认 1280×720 持续运行 30 分钟：记录平均/p95 帧年龄、读回/上传、队列长度、内存 | `SkyLongRun` `SL-C01..C11`（producer=engine） | 见 ② 表 `VERDICT` |
| P-BRG-02 | 内存随时间不持续增长（斜率 ≈ 0） | `SL-C07`（phys_footprint 稳态斜率 ≤1.0 MiB/min） | 见 ② 表 `SL-C07` |
| P-BRG-03 | 吞吐门槛达到 A2 时冻结值，且**冻结后不得临时降低门槛再宣称达标** | `SL-C01/SL-C02`（≥40 fps，2026-09-23 冻结，见 `BUILD_RECORD §3`） | 见 ② 表 `SL-C01/SL-C02` |
| P-BRG-04 | 性能不足进入 15fps 预览时 UI 明确显示降级状态 | `DynFrameCheck` `D1-C07`（降级切换）+ 长跑 `SL-C10`（**不误报**，占比 ≤1%） | `D1-C07` PASS（见 `docs/PROGRESS_SNAPSHOT` §3）；误报率见 ② 表 |

> ⚠️ **P-BRG-03 的门槛冻结**：40 fps 是 2026-09-23 在**消费侧长跑**上冻结的值；
> 合流形态（engine 生产者共占 GUI 线程）自 T13 起另有 `SL-C03` 的**形态相关口径**
> （p99 ≤ min(3×帧间隔,100ms) 天花板 + max 档占比 ≤0.01%）。两套口径的分工写在
> `SkyLongRun.hpp` 顶部注释，改口径必须同步改那里。

---

## 5. 环境前置（不达标的数据不算数）

冻结协议（测试文档 §6.1.1）要求：**接通电源 + 关闭低电量模式 + 关闭屏幕保护 +
机器闲置**，`caffeinate -dimsu` 包裹；测量期间每 60s 复核电源状态。
不达标 ⇒ `SL-C00` 拒绝测量（退出码 9，`VERDICT=ENV_FAIL`），**不产生任何测量数据**。
窗口暴露不全 ⇒ `SL-C09` 不过 ⇒ `VERDICT=INVALID`（退出码 9，**不是** FAIL）。

历史教训（留档以示警戒）：
- `docs/evidence/2026-09-22-stelt9-consistency-INVALID-throttled.*` —— 电池 + 低电量模式，
  7.32 fps，**判据全 PASS 但门槛全不达标**（判据不含吞吐门槛 ⇒ 环境失真会静默通过）。
- `docs/evidence/2026-09-23-sky-longrun-INVALID-screensaver/` —— 屏保接管 591s，
  窗口 unexposed，产出假 FAIL。

---

## 6. 生成信息

TAIL
  echo "- 生成时间：$(date '+%Y-%m-%d %H:%M:%S %Z')"
  echo "- 生成器：\`tools/a6-frame-bridge-report.sh\` + \`tools/frame-bridge-report.mjs\`"
  echo "- **复算一致性**：$CONSIST"
  echo "- ① 原 GL 基线来源：\`$BASE_LOG\`（一致性重跑 \`$BASE2_LOG\`）"
  echo "- ② A 阶段组合来源：\`$A_LOG\`（**$A_SRC**）"
  echo "- 二进制：\`build-release/src/ui/stelQuickUI.app/Contents/MacOS/stelQuickUI\`"
} > "$OUT"

echo "── 完成：${OUT}（$(wc -l < "$OUT") 行）"
if [ -s "$TMP/arm1.err" ]; then echo "⚠ arm1 stderr:"; cat "$TMP/arm1.err"; fi
if [ -s "$TMP/arm2.err" ]; then echo "⚠ arm2 stderr:"; cat "$TMP/arm2.err"; fi
