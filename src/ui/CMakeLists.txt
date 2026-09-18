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
find_package(Vulkan REQUIRED)

qt_add_executable(stelQuickUI
    main.cpp
    quick/BackendInfo.hpp
    quick/BackendInfo.cpp
    ../render/vulkan/VkDeviceProbe.hpp
    ../render/vulkan/VkDeviceProbe.cpp
)

qt_add_qml_module(stelQuickUI
    URI StelQuickUI
    VERSION 1.0
    # 模块产物单独放 qml/ 子目录，避免与可执行文件名冲突（ld errno=21）
    OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/qmlmodule
    QML_FILES
        qml/MainWindow.qml
        qml/DiagnosticPage.qml
    NO_PLUGIN
)

# 包含路径以 src/ 为根："ui/quick/..."、"render/vulkan/..."
target_include_directories(stelQuickUI PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)

target_link_libraries(stelQuickUI PRIVATE
    Qt6::Quick
    Qt6::Qml
    Qt6::Gui
    Qt6::Core
    Vulkan::Vulkan
)
