#!/usr/bin/env bash
# setup-upstream-assets.sh — 在本工作站重建根构建所需的上游资产目录。
#
# 背景：本仓库是 Stellarium 的"改写研究副本"，**不跟踪上游资产**
# （.gitignore 已排除）。因此一个全新的 clone 无法配置根构建，必须先跑本脚本。
# 注意：src/ui 独立工程**不需要**这些资产，只有根构建（`cmake -B build-release .`）需要。
#
# 用法：
#   ./tools/setup-upstream-assets.sh                    # 用默认上游路径
#   STELLARIUM_UPSTREAM=/path/to/stellarium ./tools/setup-upstream-assets.sh
#
# 两类处理方式，**不要混用**（理由见下方注释）：
#   link  —— 软链到上游 checkout。仅用于配置期"只读"的目录。
#   copy  —— 实体副本。用于构建期会被 CMake **写入**的目录。
#
# 为什么不全部软链：上游 checkout（默认 ~/qt_demo/stellarium/stellarium）
# **不是 git 仓库**，没有版本保护。根 CMakeLists 会在配置期执行
#   :990   CONFIGURE_FILE(cmake/default_cfg.ini.cmake  -> data/default_cfg.ini)
#   :1052  CONFIGURE_FILE(cmake/Info.plist.cmake       -> data/Info.plist)
#   :203   CONFIGURE_FILE(cmake/version.tex.cmake      -> guide/version.tex)
# 源码树内写入。若 data/ guide/ 是软链，写入会**穿透软链改掉上游文件**——正是
# 04 号文档风险表里那条"通过符号链接误改原项目资源"。故这两个目录用实体副本。

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
UPSTREAM="${STELLARIUM_UPSTREAM:-$HOME/qt_demo/stellarium/stellarium}"

if [ ! -d "$UPSTREAM" ]; then
    echo "错误：找不到上游 Stellarium checkout：$UPSTREAM" >&2
    echo "      请设置 STELLARIUM_UPSTREAM 环境变量指向它。" >&2
    exit 1
fi

# 配置期只读 → 软链即可
LINK_DIRS="atmosphere landscapes models nebulae po scenery3d skycultures stars textures plugins scripts util"
# 配置期被写入 → 必须实体副本
COPY_DIRS="data"

echo "上游：$UPSTREAM"
echo "目标：$REPO_ROOT"
echo

# ── 只读目录：软链 ─────────────────────────────────────────────────────────
for d in $LINK_DIRS; do
    src="$UPSTREAM/$d"
    dst="$REPO_ROOT/$d"
    if [ ! -e "$src" ]; then
        echo "  跳过 $d（上游不存在）"
        continue
    fi
    if [ -L "$dst" ] && [ "$(readlink "$dst")" = "$src" ]; then
        echo "  已有软链 $d"
    elif [ -e "$dst" ]; then
        echo "  跳过 $d（本地已存在实体，不动它）"
    else
        ln -s "$src" "$dst"
        echo "  已建软链 $d -> $src"
    fi
done

# ── 被写入目录：实体副本 ───────────────────────────────────────────────────
for d in $COPY_DIRS; do
    src="$UPSTREAM/$d"
    dst="$REPO_ROOT/$d"
    if [ ! -d "$src" ]; then
        echo "  跳过 $d（上游不存在）"
        continue
    fi
    if [ -d "$dst" ] && [ ! -L "$dst" ]; then
        echo "  已有实体副本 $d（如怀疑内容不全，先 rm -rf $d 再重跑）"
        continue
    fi
    if [ -L "$dst" ]; then
        echo "  警告：$d 是软链，构建写入会穿透到上游。正在替换为实体副本…" >&2
        rm "$dst"
    fi
    cp -R "$src" "$dst"
    echo "  已复制实体副本 $d（$(du -sh "$dst" | cut -f1)）"
done

# ── guide/：只需要目录存在（CMake 要往里写 version.tex） ───────────────────
# 完整的 50MB Images/ 只用于生成 guide.pdf，与程序构建无关，故不复制。
if [ ! -d "$REPO_ROOT/guide" ]; then
    mkdir -p "$REPO_ROOT/guide"
    echo "  已建空目录 guide/（CMake 的 version.tex 输出目标）"
else
    echo "  已有目录 guide/"
fi

echo
echo "完成。现在可以配置根构建："
echo "  cmake -B build-release -S . -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qt"
echo
echo "提醒：src/ui 独立工程不需要这一步。"
