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

## 2026-09-18｜A1：stelQuickUI Vulkan 宿主构建与验收

构建：`cmake -B build-ui -S src/ui -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qt` → `cmake --build build-ui`，零错误。

### 关键发现与修复

1. **MoltenVK portability 驱动**：手写 vkCreateInstance 必须设
   `VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR` 并启用 `VK_KHR_portability_enumeration`
   扩展，否则返回 VK_ERROR_INCOMPATIBLE_DRIVER(-9)。Qt 的 QVulkanInstance 内部已自动处理，
   手写探针必须显式对齐（VkDeviceProbe.cpp 已修）。
2. **ICD 环境变量**：Homebrew loader 默认搜索路径含 `/opt/homebrew/etc/vulkan/icd.d`，
   `VK_DRIVER_FILES` 实际非必需；`QT_VULKAN_LIB` 仍必需（Qt 默认 dlopen("vulkan") 找不到）。
3. **Qt 6.11.2 崩溃路径**：Vulkan 无法加载时场景图阶段直接 SIGSEGV（退出码 139）。
   已用 QVulkanInstance::create() 预检兜住：失败 → 明确报错 → 退出码 3。

### 验收结果

| 场景 | 结果 | 退出码 |
|---|---|---|
| 正向（Vulkan 可用） | probe: Apple M3 / api 1.1.357 / MoltenVK 0.2.2210；runtimeApi=Vulkan | 0 |
| 负向（禁用 Vulkan） | "QVulkanInstance::create() 失败——Vulkan 不可用"，明确报错 | 3（无崩溃） |

运行命令：

```sh
export QT_VULKAN_LIB=/opt/homebrew/opt/vulkan-loader/lib/libvulkan.1.dylib
STELQUICK_AUTOTEST_SECONDS=6 ./build-ui/stelQuickUI.app/Contents/MacOS/stelQuickUI
```

（环境变量 `VK_DRIVER_FILES` 可省；诊断页显示设备/DPR/版本信息。）

### 待办（A1 剩余手动项）

- [ ] 缩放、关闭、重开 ×N 无错（手动）
- [ ] 鸿蒙真机最小 QML/Vulkan 窗口探测（前后台、触摸、DPR、交换链恢复）
