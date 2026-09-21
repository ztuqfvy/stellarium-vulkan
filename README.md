# stellarium-vulkan

本仓库**不是** Stellarium 官方项目。它基于 Stellarium 26.1 源码（GPL-2.0-or-later，
见 [COPYING](COPYING)）做的一项独立实验：**用 Qt Quick / Vulkan 重写 Stellarium 的天空渲染层**，
并在 macOS（MoltenVK 转译）与 Windows（原生 Vulkan 驱动）之间做逐像素对照。

**项目入口文档：[README_VULKAN.zh_CN.md](README_VULKAN.zh_CN.md)** —— 项目定位、当前进度、
未决问题、快速开始、文档索引都在那里。

## 仓库范围说明

- 有效工程只有 `src/ui`（独立 CMake 工程，只依赖 Qt 6）与 `src/app`、`src/render`（本实验新增代码）。
- `src/` 其余部分（core/gui 等）是上游源码，作为重写对象与 A2 动态产帧的参考输入保留。
- **上游资产不入库**（`guide/` `plugins/` `data/` `scripts/` `util/` 等，2026-09-20 起从
  全部历史移除，见 `.gitignore`）。因此**全新 clone 无法直接配置仓库根的完整 Stellarium 构建**，
  需先在本机重建资产：
  `./tools/setup-upstream-assets.sh`（设 `STELLARIUM_UPSTREAM` 可指定上游 checkout 路径）。
  只构建 `src/ui` 的话**不需要**这一步。
- 注意：`data/`、`guide/` 是**实体副本**而非软链——根构建会在配置期向源码树写入
  `data/default_cfg.ini`、`data/Info.plist`、`guide/version.tex`，软链会把写入穿透到
  上游原始 checkout（该 checkout 无版本保护）。
- 上游的说明性文档（BUILDING.md、ChangeLog、CREDITS.md 等）随基座保留，属上游内容，
  不代表本实验的状态；本实验状态一律以 `docs/` 与 `README_VULKAN.zh_CN.md` 为准。
