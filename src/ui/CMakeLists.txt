# stelQuickUI — A1 阶段 Qt Quick Vulkan 宿主（独立构建）
#
# 说明（A3 转换时删除此注释）：本 CMakeLists 目前是**独立工程**，
# 与根 CMakeLists 互不引用——`cmake -B build-ui -S src/ui`。
# A3 起转为根构建的子目标（stelQuickUI），并接入 AppFacade。
#
# 构建：
#   /opt/homebrew/bin/cmake -B build-ui -S src/ui -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qt
#   /opt/homebrew/bin/cmake --build build-ui
# 运行（环境变量见 docs/BUILD_RECORD.zh_CN.md）：
#   export VK_DRIVER_FILES=/opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json
#   export QT_VULKAN_LIB=/opt/homebrew/opt/vulkan-loader/lib/libvulkan.1.dylib
#   STELQUICK_AUTOTEST_SECONDS=5 ./build-ui/stelQuickUI.app/Contents/MacOS/stelQuickUI
cmake_minimum_required(VERSION 3.21)
project(stelQuickUI LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Quick Qml Gui Core)

# Vulkan 设备探针：桌面默认开；鸿蒙交叉编译时由命令行关闭（OHOS NDK 不保证提供 libvulkan）
option(STELQUICK_VULKAN_PROBE "构建 VkDeviceProbe（需要有 Vulkan 加载库）" ON)

set(stelQuickUI_SOURCES
    main.cpp
    A2FrameCheck.hpp
    A2FrameCheck.cpp
    quick/BackendInfo.hpp
    quick/BackendInfo.cpp
    quick/SkyViewport.hpp
    quick/SkyViewport.cpp
    # A2 帧桥：邮箱 + 测试图案 + 开发期帧源（A2 主体完成后 StaticFrameSource 被旧 GL 读回替换）
    ../render/legacy/FrameMailbox.hpp
    ../render/legacy/FrameMailbox.cpp
    ../render/legacy/TestPattern.hpp
    ../render/legacy/TestPattern.cpp
    ../render/legacy/StaticFrameSource.hpp
    ../render/legacy/StaticFrameSource.cpp
)

if(STELQUICK_VULKAN_PROBE)
    find_package(Vulkan REQUIRED)
    list(APPEND stelQuickUI_SOURCES
        ../render/vulkan/VkDeviceProbe.hpp
        ../render/vulkan/VkDeviceProbe.cpp
    )
endif()

qt_add_executable(stelQuickUI ${stelQuickUI_SOURCES})

qt_add_qml_module(stelQuickUI
    URI StelQuickUI
    VERSION 1.0
    # 模块产物单独放 qml/ 子目录，避免与可执行文件名冲突（ld errno=21）
    OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/qmlmodule
    QML_FILES
        qml/MainWindow.qml
        qml/DiagnosticPage.qml
        qml/SkyTestPage.qml
    NO_PLUGIN
)

# 包含路径以 src/ 为根："ui/quick/..."、"render/vulkan/..."
target_include_directories(stelQuickUI PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)

target_link_libraries(stelQuickUI PRIVATE
    Qt6::Quick
    Qt6::Qml
    Qt6::Gui
    Qt6::Core
)

if(STELQUICK_VULKAN_PROBE)
    target_link_libraries(stelQuickUI PRIVATE Vulkan::Vulkan)
    target_compile_definitions(stelQuickUI PRIVATE STELQUICK_VULKAN_PROBE=1)
endif()

# macOS 专有：应用包（可从 Finder 双击）+ rpath 让 Qt 的 dlopen("libvulkan.dylib") 命中
if(APPLE)
    set_target_properties(stelQuickUI PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "stelQuickUI"
        MACOSX_BUNDLE_GUI_IDENTIFIER "org.stellarium.vulkan.stelquickui"
        BUILD_RPATH "/opt/homebrew/opt/vulkan-loader/lib"
        INSTALL_RPATH "@executable_path/../Frameworks;/opt/homebrew/opt/vulkan-loader/lib"
    )
endif()

# 说明：main.cpp 另有 QT_VULKAN_LIB 自动探测兜底（不依赖环境变量即可运行）。
# TODO（打包阶段）：把 libvulkan.1.dylib + libMoltenVK.dylib + MoltenVK_icd.json
# 复制进 <bundle>/Contents/Frameworks 并改写 ICD 的 library_path，实现自包含分发。
# 鸿蒙交叉编译（见 docs/HARMONY_PROBE.zh_CN.md）：
#   ~/Qt/6.12.0/harmonyos_arm64_v8a/bin/qt-cmake -S src/ui -B build-harmony \
#     -DCMAKE_FIND_ROOT_PATH=$HOME/.local/opt/ohos/additional-packages \
#     -DSTELQUICK_VULKAN_PROBE=OFF
