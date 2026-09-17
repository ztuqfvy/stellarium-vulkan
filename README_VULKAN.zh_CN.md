# Stellarium Vulkan 迁移研究工作副本

整理日期：2026-09-17。这里目前仍是 OpenGL 源码，**尚未实现 Vulkan 渲染器**。

## 从哪里开始

1. 按顺序阅读 [计划一：QML 界面重写与 Vulkan 后端](docs/plans/2026-09-17-01-qml-ui-vulkan-development.zh_CN.md)、[计划二：天空原生 Vulkan 渲染](docs/plans/2026-09-17-02-native-vulkan-sky-development.zh_CN.md)。此前的 [可行性调查](docs/plans/2026-09-17-stellarium-opengl-to-vulkan.zh_CN.md) 保留作历史参考；当前方向已经确定为个人维护的 QML + Vulkan 分支，不等待官方接受。
2. 看 `src/main.cpp`（程序入口）、`src/StelMainView.cpp`（窗口与每帧触发）、`src/core/StelApp.cpp`（模块更新与绘制调度）。
3. 看 `src/core/StelPainter.cpp`（通用绘图）、`src/core/StelSkyDrawer.cpp`（恒星等天空绘制）、`src/core/modules/`（天体模块）。
4. `src/gui/` 是 Qt 界面，`plugins/` 是可选功能，`data/shaders/` 是部分着色器；还有大量着色器直接写在 C++ 字符串里。
5. `CMakeLists.txt` 和 `src/CMakeLists.txt` 决定依赖及编译目标；这是 CMake 管理的 Qt/C++ 应用，不是只编译一个 main.cpp 就能运行的程序。

## 提取范围

原目录：`/Users/ztuqfvy/qt_demo/stellarium/stellarium`。原文件没有移动或删除。

物理复制 2,339 个文件，共 160,505,015 字节（约 153.1 MiB）：

- `src/`、`plugins/`：主要源码及随附插件资源，包括项目内嵌的第三方源码。
- `cmake/`、根目录构建文件、格式配置、许可证及项目说明。
- `data/`、`android/`、`util/`、`scripts/`、`doc/`、`guide/`：构建资源、平台文件、工具与文档。
- `guide/` 和 `data/` 使用真实副本，因为部分 CMake 配置会向这些源码目录生成文件。

下面九个目录是**指向原目录的绝对符号链接**，不是独立副本：

`models/`、`textures/`、`landscapes/`、`skycultures/`、`nebulae/`、`stars/`、`atmosphere/`、`scenery3d/`、`po/`。

这些资源含 4,150 个文件、1,099,157,187 字节（约 1.02 GiB）。保留它们是因为根 CMake 和程序运行会引用这些目录；只取 `src/` 会缺构建输入及运行数据。

**暂时不要编辑链接目录，也不要运行可能改写它们的资源生成/翻译维护命令。修改链接中的文件就是修改原项目。原目录移动后链接会失效。** 需要打包、发布或改资源时，先转成独立副本：

```sh
cd /Users/ztuqfvy/qt_demo/stellarium_vulkan
node tools/materialize-resources.mjs --copy
node tools/source-snapshot.mjs verify
```

该命令逐目录复制并校验后替换链接，只删除链接本身，不删除原资源。建议至少留出 1.2 GiB 空间。校验失败会保留原链接及临时副本供检查，不会自动覆盖已存在的真实目录。

没有复制旧 `build/`、`cmake-build-debug/`、IDE 配置、`.DS_Store` 或旧依赖下载缓存。没有复制 Git 历史：原目录本身没有 `.git`。CMake 声明版本为 26.1，不代表精确对应上游某个提交；原有本地修改保留不变。

## 校验与构建边界

```sh
node tools/source-snapshot.mjs verify
node tools/performance-model.mjs
```

第一条对提取文件及资源做 SHA-256 校验；原路径仍存在时也检查原文件。后续正常开发造成校验变化是预期现象，不应为了通过而重置代码。基线记录在 `docs/vulkan/source-manifest.json`。

第二条只运行计划中的假设性能模型，**不是跑 Stellarium 的性能测试**。

实际构建请先按保留的 `BUILDING.md` 配置 Qt 与依赖，再使用本目录自己的全新构建目录，例如：

```sh
cmake -S . -B build/opengl-release -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="你的桌面Qt安装前缀"
cmake --build build/opengl-release --parallel
```

这只是现有 OpenGL 基线的命令模板，不是已经验证成功的构建步骤。不要照抄占位路径；不要使用 HarmonyOS Qt 工具链构建桌面基线，也不要复用原项目的 CMakeCache。本次未安装 SDK、未执行完整配置/编译、未跑 GPU 基准测试。依赖下载（例如 ShowMySky）仍可能需要网络。

目前不存在可用的 `STELLARIUM_RENDER_BACKEND=Vulkan` 编译选项；计划里的后端接口和选项均是未来工作。
