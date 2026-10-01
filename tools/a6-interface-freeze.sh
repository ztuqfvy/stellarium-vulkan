#!/bin/zsh
# A6 接口冻结清单生成器（计划一 §7「交给计划二的硬性契约」第 1/4/5/7 条的取证）。
#
# 为什么是脚本：接口冻结的价值在于"**冻结那一刻是什么**"可复核。手抄的表无法
# 保证与代码同步，也无法在冻结后证明"没人偷偷改过"。本脚本只从**头文件原文**
# 提取公开面（Q_PROPERTY / Q_INVOKABLE / signals / public 方法），不改一个字节。
#
# 用法： tools/a6-interface-freeze.sh            # 输出到 stdout
#        tools/a6-interface-freeze.sh > docs/A6_INTERFACE_FREEZE.zh_CN.md
#
# ⚠️ PATH 污染：grep→WorkBuddy shim 会**静默零命中** ⇒ 一律 /usr/bin/grep 全路径。
set -u
export PATH="/opt/homebrew/bin:/usr/bin:/bin"

REPO="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO" || exit 1
G=/usr/bin/grep

emit_class() {
  local f="$1"
  local cls
  cls=$($G -oE '^class[[:space:]]+[A-Za-z_][A-Za-z0-9_]*' "$f" | head -1 | awk '{print $2}')
  [ -z "$cls" ] && return
  local props invok signals
  # Q_PROPERTY 可能跨行：把 Q_PROPERTY( ... ) 折叠成一行再取类型与名
  props=$($G -n 'Q_PROPERTY' "$f" | sed -E 's/^[0-9]+: *//' | sed -E 's/.*Q_PROPERTY\(([^,]+)[^)]*\).*/\1/' | sed -E 's/ +/ /g' | head -80)
  invok=$($G -n 'Q_INVOKABLE' "$f" | sed -E 's/^[0-9]+: *//' | sed -E 's/Q_INVOKABLE *//' | head -60)
  signals=$($G -n -A 200 '^signals:' "$f" | $G -E '^[a-zA-Z_].*\(.*\)\s*;' | sed -E 's/^ *//' | head -40)

  printf '#### `%s`\n\n' "$cls"
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
    printf '（无 QML 可见面：纯 C++ 内部类）\n\n'
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
  esac
  emit_class "$f"
done

echo "---"
echo
echo "## 2. 渲染/宿主面（\`src/ui/quick/\`）"
echo
echo "这一层把帧邮箱接到 Qt Quick 场景图。**计划二会把它换掉**（改为原生 Vulkan 天空），"
echo "因此冻结的重点是它**对外**暴露了什么（页面协议），而不是内部实现。"
echo

for f in src/ui/quick/*.hpp; do
  emit_class "$f"
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
echo "── src/app/*.hpp 里出现 GL/Vulkan 记号的行（应为 0）："
$G -nHE 'GLuint|GLenum|GLfloat|GLint|VkDevice|VkInstance|VkImage|VkQueue|VkPhysicalDevice|QSGImageNode|QSGTexture' src/app/*.hpp \
  | sed -E 's|//.*$||' \
  | $G -E 'GLuint|GLenum|GLfloat|GLint|VkDevice|VkInstance|VkImage|VkQueue|VkPhysicalDevice|QSGImageNode|QSGTexture' \
  || echo "(0 行)"
echo "── src/ui/qml/*.qml 里出现 GL/Vulkan 记号的行（应为 0）："
$G -nHE 'VulkanInstance|QSGRendererInterface|VkDevice|GLuint|graphicsApi' src/ui/qml/*.qml \
  | sed -E 's|//.*$||' \
  | $G -E 'VulkanInstance|QSGRendererInterface|VkDevice|GLuint|graphicsApi' \
  || echo "(0 行)"
echo '```'
