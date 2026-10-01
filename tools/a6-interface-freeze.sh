#!/bin/zsh
# A6 接口冻结清单生成器（计划一 §7「交给计划二的硬性契约」第 1/4/5/7 条的取证）。
#
# 为什么是脚本：接口冻结的价值在于"**冻结那一刻是什么**"可复核。手抄的表无法
# 保证与代码同步，也无法在冻结后证明"没人偷偷改过"。本脚本只从**头文件原文**
# 提取公开面（Q_PROPERTY / Q_INVOKABLE / signals / public 方法），不改一个字节。
#
# 用法： tools/a6-interface-freeze.sh            # 输出到 stdout
#        tools/a6-interface-freeze.sh > docs/A6_INTERFACE_FREEZE.zh_CN.md
#        tools/a6-interface-freeze.sh --check    # **冻结校验**：重新生成并与已冻结清单
#                                                # 逐字对比（忽略生成时间/HEAD 两行）；
#                                                # 有差异 ⇒ 打印差异 + rc=1（"冻结"的语义）
#
# ⚠️ PATH 污染：grep→WorkBuddy shim 会**静默零命中** ⇒ 一律 /usr/bin/grep 全路径。
# ⚠️ `diff` 同样被 DevEco 覆盖（**不同文件静默 rc=0 无输出**）⇒ 一律 /usr/bin/diff。
set -u
export PATH="/opt/homebrew/bin:/usr/bin:/bin"

REPO="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO" || exit 1
G=/usr/bin/grep
FROZEN="docs/A6_INTERFACE_FREEZE.zh_CN.md"

# ── --check：冻结校验模式 ────────────────────────────────────────────────────
# 这是"冻结"二字的兑现：冻结清单的价值不在于"当时长什么样"，而在于**冻结之后
# 有没有人偷偷改过公开面**。本模式重新生成一份，与已提交的冻结清单对比；
# 只忽略两行**天然易变**的头部（生成时间 / 仓库 HEAD），其余逐字节比。
#   ⚠️ 对比范围**止于 §5 之前**：§5 是"当前实测读数"，源码里出现一个 GL 记号
#   也会让它变 —— 首版把它纳入 ⇒ 真正的接口越界被禁区噪音淹没（信噪比垮掉），
#   而且 §5 本来就不是"冻结面"，它由 t46 段第 (3) 项单独判。
# 公开面真变了 ⇒ rc=1 并打印差异 —— CI 里这就是"接口越界"的报警器。
if [ "${1:-}" = "--check" ]; then
  TMPF=$(mktemp -t ifcheck.XXXXXX) || exit 2
  "$REPO/tools/a6-interface-freeze.sh" > "$TMPF"
  gen_rc=$?
  if [ $gen_rc -ne 0 ]; then
    echo "IF-CHECK: 生成失败（rc=$gen_rc）⇒ UNAVAILABLE（不记 PASS）"
    /bin/rm -f "$TMPF"; exit 2
  fi
  if [ ! -f "$FROZEN" ]; then
    echo "IF-CHECK: 冻结清单不存在（$FROZEN）⇒ UNAVAILABLE"
    /bin/rm -f "$TMPF"; exit 2
  fi
  # 规范：剥易变头部；截到 §5 之前（§1–§4 = 公开面本体）
  normf() { $G -vE '^> (生成时间|仓库 HEAD)：' "$1" | awk '/^## 5\./{exit} {print}'; }
  if /usr/bin/diff <(normf "$TMPF") <(normf "$FROZEN") > /tmp/if-check-diff.txt 2>&1; then
    echo "IF-CHECK: OK（当前源码公开面与冻结清单逐字一致）"
    /bin/rm -f "$TMPF"; exit 0
  else
    echo "IF-CHECK: DRIFT（公开面与冻结清单有差异 —— 要么是本次有意变更，要么是越界）"
    echo "──────── diff（< 当前源码生成 / > 冻结清单）────────"
    /usr/bin/head -60 /tmp/if-check-diff.txt
    /bin/rm -f "$TMPF"; exit 1
  fi
fi

#! 提取文件里**所有顶层类型**声明名（`class` 与 `struct`，行首，跳过前向声明）。
#  ⚠️ 首版只取**第一个** `^class`，因此三重错误（T46/A6-D 实抓，2026-10-01）：
#    ① **命中前向声明**：`FrameCompare.hpp:27 class QQuickWindow;` ⇒ 整段标成
#       `QQuickWindow`（且该段恰好"无 QML 可见面"，假数据一路躺到 A6 才被抓）；
#    ② **漏掉同文件的第二个类**：`AppFacade.hpp:83 class ISimPacing` 之后的
#       `:95 class AppFacade` —— **契约 1 的核心接口在冻结清单里根本不存在**，
#       清单上只剩 `ISimPacing` 顶名（最坏的一种错：看着有、其实没有）；
#    ③ **完全不认 `struct`**：`ViewportState.hpp` / `ConfigIsolation.hpp` 是**纯 struct**
#       头文件（契约 4 点名的"可复制状态结构"）⇒ 整段缺失。
#  ⇒ 冻结清单曾**同时含假数据与漏项**，而它正是"接口冻结"的交付物本身。
#  正解 = 列全部顶层类型；前向声明（行尾 `;`）不是定义，一律跳过。
top_types() {
  awk '
    /^(class|struct)[ \t]+[A-Za-z_][A-Za-z0-9_]*/ {
      if ($0 ~ /;[ \t]*$/) next          # 前向声明 `class Foo;` 不是类型定义
      s = $0
      sub(/^(class|struct)[ \t]+/, "", s)
      sub(/[^A-Za-z0-9_].*$/, "", s)
      print s
    }' "$1"
}

#! 提取**第一个** struct 的公开字段名（机械提取；权威源永远是头文件本身）。
#  为什么需要：契约 4 要交接"可复制状态结构"，而这些结构是 struct 不是 class
#  —— 机械清单若不列字段，交接方只能去翻源码，冻结的意义就没了。
struct_fields() {
  awk '
    /^struct[ \t]+[A-Za-z_][A-Za-z0-9_]*/ { inS = 1; next }
    inS && /^};/ { exit }
    inS {
      line = $0
      sub(/\/\/.*$/, "", line)            # 剥行尾注释
      sub(/^[ \t]+/, "", line)
      sub(/[ \t]+$/, "", line)
      if (line == "") next
      if (line ~ /[(){]/) next            # 方法 / 内联函数体不是字段
      if (line !~ /;/) next
      sub(/;[ \t]*$/, "", line)           # ⚠️ 次序要紧：先剥**分号**再剥默认值。
      sub(/=.*$/, "", line)               # 反了的话尾随空白会被 split 当成空元素，
      sub(/[ \t]+$/, "", line)            # a[n] 取到空串 ⇒ 字段静默丢失 + 空行
      n = split(line, a, /[ \t]+/)
      if (n >= 2) print a[n]
    }' "$1"
}

emit_type() {
  local f="$1"
  local types
  types=$(top_types "$f")
  [ -z "$types" ] && return
  local title=""
  while IFS= read -r t; do
    [ -z "$t" ] && continue
    if [ -z "$title" ]; then title="\`$t\`"; else title="$title / \`$t\`"; fi
  done <<< "$types"
  local props invok signals
  # Q_PROPERTY 可能跨行：把 Q_PROPERTY( ... ) 折叠成一行再取类型与名
  props=$($G -n 'Q_PROPERTY' "$f" | sed -E 's/^[0-9]+: *//' | sed -E 's/.*Q_PROPERTY\(([^,]+)[^)]*\).*/\1/' | sed -E 's/ +/ /g' | head -80)
  invok=$($G -n 'Q_INVOKABLE' "$f" | sed -E 's/^[0-9]+: *//' | sed -E 's/Q_INVOKABLE *//' | head -60)
  signals=$($G -n -A 200 '^signals:' "$f" | $G -E '^[a-zA-Z_].*\(.*\)\s*;' | sed -E 's/^ *//' | head -40)

  printf '#### %s\n\n' "$title"
  printf '_头文件：`%s`_\n\n' "$f"

  if [ -n "$props" ]; then
    printf '**QML 可见属性（Q_PROPERTY）**\n\n```\n%s\n```\n\n' "$props"
  fi
  if [ -n "$invok" ]; then
    printf '**QML 可调用方法（Q_INVOKABLE）**\n\n```\n%s\n```\n\n' "$invok"
  fi
  if [ -n "$signals" ]; then
    printf '**信号**\n\n```\n%s\n```\n\n' "$signals"
  fi
  if [ -z "$props" ] && [ -z "$invok" ] && [ -z "$signals" ]; then
    local fields
    fields=$(struct_fields "$f" | head -60)
    if [ -n "$fields" ]; then
      printf '**公开字段（机械提取，权威源是头文件）**\n\n```\n%s\n```\n\n' "$fields"
    else
      printf '（无 QML 可见面：纯 C++ 内部类）\n\n'
    fi
  fi
}

echo "# A6 接口冻结清单（QML 与 C++ 的公开面）"
echo
echo "> **本文件由 \`tools/a6-interface-freeze.sh\` 从头文件原文生成**，不要手工编辑。"
echo "> 冻结含义见 \`docs/A6_HANDOFF_FREEZE.zh_CN.md\` §「冻结口径」。"
echo "> 生成时间：$(date '+%Y-%m-%d %H:%M:%S %Z')"
echo "> 仓库 HEAD：$(git rev-parse --short HEAD 2>/dev/null || echo '(不可用)')"
echo
echo "---"
echo
echo "## 1. 数据面 / 命令面（\`src/app/\`）"
echo
echo "这一层是**计划二必须接过的东西**：QML 只通过它们与旧引擎对话。"
echo

for f in src/app/*.hpp; do
  case "$f" in
    *Probe.hpp|*Check.hpp) continue ;;   # 探针/判据是测试设施，不属于产品接口
    *FrameCompare.hpp) continue ;;       # T37 提炼的"探针/判据共用尺子"（FrameSample
                                         # 等），性质同探针设施 ⇒ 同样不进产品接口面
  esac
  emit_type "$f"
done

echo "---"
echo
echo "## 2. 渲染/宿主面（\`src/ui/quick/\`）"
echo
echo "这一层把帧邮箱接到 Qt Quick 场景图。**计划二会把它换掉**（改为原生 Vulkan 天空），"
echo "因此冻结的重点是它**对外**暴露了什么（页面协议），而不是内部实现。"
echo

for f in src/ui/quick/*.hpp; do
  emit_type "$f"
done

echo "---"
echo
echo "## 3. 注入 QML 的上下文属性（页面协议的实际入口）"
echo
echo "来自 \`src/ui/main.cpp\` 的 \`setContextProperty\` / \`qmlRegisterType\`。"
echo "**B 阶段不得改这些名字与语义**（契约第 7 条：预留通道，不改页面协议）。"
echo
echo '```'
$G -nE 'setContextProperty\(|qmlRegisterType<' src/ui/main.cpp | sed -E 's/^[0-9]+: *//' | sed -E 's/ +/ /g' | sort -u
echo '```'
echo
echo "---"
echo
echo "## 4. QML 文件清单（页面协议的消费侧）"
echo
echo '```'
ls src/ui/qml/*.qml | sed 's|src/ui/qml/||'
echo '```'
echo
echo "---"
echo
echo "## 5. 硬性禁区自查（编码规范 §5，违反即打回）"
echo
echo "CI 化的 grep 门禁要查三条：\`src/app/\` 头文件无 GL/Vulkan 类型；"
echo "\`src/ui/qml/\` 无 GL/Vulkan 调用；QML 不持旧引擎裸指针。本脚本给出**当前实测**："
echo
echo '```'
# ⚠️ 首版用 `grep -v '//'` 排除"注释里提到 GL 记号"的说明行 —— 但那会把
#    **代码 + 行尾注释**（`GLuint fb; // 旧帧缓冲`）整行也滤掉 ⇒ **假绿**。
#    正确姿势：先去掉 `//` 之后的内容、再在**代码部分**查记号。
#    `grep -c` 无命中时 exit 1（接 `&&` 会静默截断），故一律用 `|| echo "(0 行)"`。
# ⚠️ 另一条同族陷阱（T46 实抓）：**按文件计数且不剥注释**的正则（t46 段首版）会
#    把 ` * 禁止：不暴露 GL/Vulkan 句柄` 这类**说明注释**判成泄漏 ⇒ 10 个文件假红。
#    本脚本是**唯一口径**：t46 段解析下面那行 `FORBIDDEN` 取数，不另起一套正则。
PAT_APP='GLuint|GLenum|GLfloat|GLint|VkDevice|VkInstance|VkImage|VkQueue|VkPhysicalDevice|QSGImageNode|QSGTexture'
PAT_QML='VulkanInstance|QSGRendererInterface|VkDevice|GLuint|graphicsApi'
echo "── src/app/*.hpp 里出现 GL/Vulkan 记号的行（应为 0）："
app_hits_out=$($G -nHE "$PAT_APP" src/app/*.hpp 2>/dev/null | sed -E 's|//.*$||' | $G -E "$PAT_APP")
app_hits=$(printf '%s' "$app_hits_out" | $G -c . )
if [ "$app_hits" = "0" ]; then echo "(0 行)"; else printf '%s\n' "$app_hits_out"; fi
echo "── src/ui/qml/*.qml 里出现 GL/Vulkan 记号的行（应为 0）："
qml_hits_out=$($G -nHE "$PAT_QML" src/ui/qml/*.qml 2>/dev/null | sed -E 's|//.*$||' | $G -E "$PAT_QML")
qml_hits=$(printf '%s' "$qml_hits_out" | $G -c . )
if [ "$qml_hits" = "0" ]; then echo "(0 行)"; else printf '%s\n' "$qml_hits_out"; fi
echo '```'
echo
echo "<!-- 机器可读：t46 段解析此行，**不要手改** -->"
echo "FORBIDDEN app_hpp_hits=${app_hits} qml_hits=${qml_hits}"

