# 构建记录（BUILD_RECORD）

> A0 要求：Qt/SDK/资源版本有记录，可复现构建。本文件随环境与构建结果更新。

## 2026-09-17｜旧版（OpenGL）Release 构建

| 项 | 值 |
|---|---|
| 源码 | Stellarium 26.1 快照（工作副本基线提交） |
| macOS | darwin（Apple Silicon，8 核） |
| CMake | /opt/homebrew/bin/cmake（Homebrew） |
| 编译器 | Apple clang++（/usr/bin/clang++） |
| Qt | 6.11.2（Homebrew；见下方版本错配说明） |
| Vulkan | molten-vk 1.4.2、vulkan-headers 1.4.357.0、vulkan-loader 1.4.357.0 |
| 构建目录 | build-release/（全新目录） |
| 配置命令 | `cmake -B build-release -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qt -DENABLE_TESTING=OFF` |
| 配置耗时 | 244 s |
| 构建命令 | `cmake --build build-release --parallel 8` |
| 构建耗时 | 5 分 27 秒 |
| 结果 | 100% 编译链接成功，产物 `build-release/src/stellarium`（约 36 MB） |
| 警告 | 仅 ld deployment-target 警告（目标 macOS-12 vs Homebrew dylib 14.0/26.0），无编译警告基线问题 |

## 运行时依赖问题（重要经验）

- **现象**：可执行文件 dyld 阶段报 `Symbol not found: __ZN14QObjectPrivateC2E16QtPrivate_6_11_1`。
- **根因**：Homebrew Qt 模块版本错配（qtbase 6.11.2 vs qtwebchannel 等滞留 6.11.1）。Qt 私有符号带补丁版本标签，跨补丁版本混链必失败。
- **结论**：计划一"锁定 Qt 补丁版本"的正确含义是**锁定整个 Qt 模块集的同一补丁版本**，不是只看 qtbase。处理：`brew upgrade qt` 统一至 6.11.2。
- **复查命令**：`for d in /opt/homebrew/Cellar/qt*; do echo "$(basename $d): $(ls $d | tail -1)"; done`

## Vulkan 运行环境变量（Qt 程序枚举 Vulkan 设备必需）

```sh
export VK_DRIVER_FILES=/opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json
export QT_VULKAN_LIB=/opt/homebrew/opt/vulkan-loader/lib/libvulkan.1.dylib
```

缺 `QT_VULKAN_LIB` 时 Qt 报 `Failed to load vulkan: Cannot load library vulkan`（dlopen 找不到加载库，因为 /opt/homebrew/lib 不在默认 dyld 搜索路径）。

## A1 后端诊断（tests/manual/a1_backend_diag.cpp）

```sh
cmake -B build-manual -S tests/manual -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qt
cmake --build build-manual
build-manual/a1_backend_diag vulkan   # REQUESTED=vulkan ACTUAL=Vulkan VERDICT=PASS
build-manual/a1_backend_diag opengl   # ACTUAL=OpenGL PASS（对照）
build-manual/a1_backend_diag metal    # ACTUAL=Metal PASS（对照）
```

诊断判定逻辑：请求 Vulkan 时实际 API 必须为 Vulkan，否则 VERDICT=FAIL（禁止静默回退）。

## 配置目录隔离（A0 要求，2026-09-17 教训）

- 2026-09-17 首次验证运行未隔离，读取并迁移了用户全局配置 `~/Library/Application Support/Stellarium/config.ini`（触发了 "Cleared cache and updated config.ini"）。
- **后续一切测试运行必须带隔离参数**，确保"原目录和设置不受影响"：

```sh
mkdir -p ~/qt_demo/stellarium_vulkan/profile
./build-release/src/stellarium --user-dir ~/qt_demo/stellarium_vulkan/profile
```

- 个人版 QML 应用（A1 起）的配置同样必须落在独立目录，不碰上述全局路径。
