# 鸿蒙探测文档（A1 平台验证）

日期：2026-09-18｜对应计划一第 7 节｜状态：**静态验证完成、真机验证被"无设备"阻塞**

## 1. 结论摘要

| 问题 | 结论 | 证据类型 |
|---|---|---|
| Qt 鸿蒙套件有没有 ohos QPA 插件？ | **有**，`libqohos.so` | 文件存在 |
| 该 QPA 是否实现 Vulkan 平台实例？ | **是**，含完整 `QOhosPlatformVulkanInstance` + `QOhosVulkanSurface`，走 `vkCreateSurfaceOHOS`（VK_OHOS_surface） | 二进制字符串符号 |
| A1 应用能否交叉编译为鸿蒙目标？ | **能**，产出 aarch64 ELF `libstelQuickUI.so`，QML 通过 qmlcachegen | 实际编译 |
| 真机能否渲染？ | **未验证**——`hdc list targets` 为空，无设备 | 待办 |

关键点：Qt QPA 层的 Vulkan 实现**不是空的**，但它是运行时通过 `vkGetInstanceProcAddr` 取 `vkCreateSurfaceOHOS` 的。**设备侧驱动若不导出该函数，就会打印 `vkCreateSurfaceOHOS not available` 并失败**——这才是真机要验证的核心未知数，不是 Qt 侧缺功能。

## 2. 静态证据（可复现命令）

```sh
K=~/Qt/6.12.0/harmonyos_arm64_v8a
ls $K/plugins/platforms/libqohos.so          # ohos QPA 插件存在
strings $K/plugins/platforms/libqohos.so | grep -i vulkan
```

输出要点：

```
Failed to create OHOS VkSurface: %d
vkCreateSurfaceOHOS not available
QOhosPlatformVulkanInstance
QOhosPlatformNativeInterface: No QOhosSurface available for Vulkan window
QOhosPlatformNativeInterface: No Vulkan instance; was QWindow::setVulkanInstance() called?
QOhosVulkanSurface::tryGetOrCreateVulkanWindowSurface(VkInstance, PFN_vkCreateSurfaceOHOS, PFN_vkDestroySurfaceKHR)
```

→ Qt 侧确实有 Vulkan 窗口表面路径，且错误信息已内置（便于真机日志判读）。

## 3. 已完成：鸿蒙交叉编译

```sh
cd /Users/ztuqfvy/qt_demo/stellarium_vulkan
~/Qt/6.12.0/harmonyos_arm64_v8a/bin/qt-cmake -S src/ui -B build-harmony \
  -DCMAKE_FIND_ROOT_PATH=$HOME/.local/opt/ohos/additional-packages \
  -DSTELQUICK_VULKAN_PROBE=OFF
cmake --build build-harmony --parallel 8
```

结果：`build-harmony/libstelQuickUI.so`，`ELF 64-bit LSB shared object, ARM aarch64`，613 KB。
说明：
- 鸿蒙构建关闭 `VkDeviceProbe`（OHOS NDK 不保证提供 libvulkan；探针属诊断，非必要）。
- A1 的 QML 页面（MainWindow/DiagnosticPage）已通过 qmlcachegen 编译，无 QML 语法/模块问题。

## 4. 真机步骤（设备到位后执行）

1. **连接与识别**
   ```sh
   export PATH="$PATH:/Applications/DevEco-Studio.app/Contents/sdk/default/openharmony/toolchains"
   hdc list targets          # 期望出现设备序列号
   hdc shell param get const.product.software.version
   ```
2. **打包为 DevEco 工程 / HAP**（按 qt-harmony-build 技能流程）
   ```sh
   source ~/qt-harmony-env.sh
   /Applications/DevEco-Studio.app/Contents/tools/hvigor/bin/hvigorw --stop-daemon
   ~/Qt/6.12.0/macos/bin/harmonydeployqt \
     --hvigor /Applications/DevEco-Studio.app/Contents/tools/hvigor/bin/hvigorw \
     --input build-harmony/stelQuickUI-harmony-deployment-settings.json \
     --output <输出目录>
   sh ~/.workbuddy/skills/qt-harmony-build/scripts/fix-harmony-libs.sh <输出目录>
   ```
   末尾 `compatibleSdkVersion not in SDK Manager` 是已知正常现象。
3. **安装运行 + 抓日志**
   ```sh
   hdc install -r <hap 路径>
   hdc shell aa start -a EntryAbility -b <bundleName>
   hdc hilog | grep -iE "vulkan|qohos|vkCreateSurfaceOHOS|QSG|RHI"
   ```
4. **验收清单**（对应计划一第 7 节）
   - [ ] 最小 QML 窗口能起，无崩溃
   - [ ] 实际渲染后端为 Vulkan（诊断页显示；若为 GL/其他，记"未达成"）
   - [ ] 无 `vkCreateSurfaceOHOS not available` / `No QOhosSurface available for Vulkan window`
   - [ ] 前后台切换 ×10：无崩溃，交换链恢复（画面继续更新）
   - [ ] 触摸事件到达 QML 且坐标正确（含 DPR ≠ 1 情况）
   - [ ] DPR 变化/旋转后画面尺寸与坐标正确
   - [ ] 窗口尺寸变化后 `viewportGeneration` 递增、旧输入被拒

## 5. 判定规则与分支（计划一第 7 节）

| 真机结果 | 处理 |
|---|---|
| Vulkan 后端 + 交换链恢复均通过 | 记录"QML Vulkan 原型通过"，A 阶段可在鸿蒙推进一步 |
| QPA 能起但 Vulkan 呈现失败（如驱动不导出 `vkCreateSurfaceOHOS`） | 记为**平台阻塞**，不冒称适配完成；A 阶段完整帧桥产品先在桌面交付 |
| 设备无可用旧 OpenGL 路径 | A 阶段帧桥在桌面交付，鸿蒙等 B 阶段 Vulkan 天空接入 |
| Qt QPA 完全不能呈现 Vulkan | **新增平台适配工作包并重估工期**——不许把问题留到 UI 全写完 |

## 6. 当前阻塞与下一步

- 阻塞：需要一台鸿蒙真机（或 DevEco 鸿蒙模拟器）并用 hdc 连接。模拟器注意：其 Vulkan 支持与真机可能不同，结论需标注来源（真机/模拟器）。
- 设备到位后的第一件事就是把第 4 节跑一遍，日志与结论追加到本文档。
- 本机已具备：Qt 6.12.0 鸿蒙套件、DevEco Studio、Additional Packages、`~/qt-harmony-env.sh`、硬编码路径软链 `/opt/harmonyos/command-line-tools/sdk/...`。
