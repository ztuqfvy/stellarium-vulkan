# stelQuickUI — A1 阶段 Qt Quick Vulkan 宿主（独立构建）
#
# 说明（A3 转换时删除此注释）：本 CMakeLists 目前是**独立工程**，
# 与根 CMakeLists 互不引用——`cmake -B build-ui -S src/ui`。
# A3 起转为根构建的子目标（stelQuickUI），并接入 AppFacade。
#
# 构建：
#   macOS：
#     /opt/homebrew/bin/cmake -B build-ui -S src/ui -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qt
#     /opt/homebrew/bin/cmake --build build-ui
#   Windows（VS2022 x64 开发者命令行 / Qt 自带终端，见 docs/WINDOWS_BUILD.zh_CN.md）：
#     cmake -B build-ui -S src/ui -DCMAKE_PREFIX_PATH=C:/Qt/6.11.2/msvc2022_64 -G "Visual Studio 17 2022" -A x64
#     cmake --build build-ui --config Release --parallel
# 运行（macOS 环境变量见 docs/BUILD_RECORD.zh_CN.md；Windows 无需任何环境变量）：
#   export VK_DRIVER_FILES=/opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json
#   export QT_VULKAN_LIB=/opt/homebrew/opt/vulkan-loader/lib/libvulkan.1.dylib
#   STELQUICK_AUTOTEST_SECONDS=5 ./build-ui/stelQuickUI.app/Contents/MacOS/stelQuickUI
cmake_minimum_required(VERSION 3.21)
project(stelQuickUI LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Quick Qml Gui Core)

# Vulkan 设备探针：桌面默认开；鸿蒙交叉编译时由命令行关闭（OHOS NDK 不保证提供 libvulkan）。
# Windows/Linux 上若未安装 Vulkan SDK（没有 find_package 所需的 headers/loader），
# 下面会自动降级为 OFF 并告警——渲染本身不依赖 SDK（Qt 运行时 dlopen 系统 Vulkan），
# 只有"设备探针"这一项诊断数据会缺。这让"没装 SDK 也能先跑起来看效果"成立。
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
    find_package(Vulkan QUIET)
    if(NOT Vulkan_FOUND)
        message(WARNING
            "未找到 Vulkan 开发包（find_package(Vulkan) 失败）→ 本构建关闭 VkDeviceProbe。\n"
            "  渲染不受影响（Qt 运行时加载系统 Vulkan）。\n"
            "  需要设备诊断数据时：Windows 装 LunarG Vulkan SDK（设 VULKAN_SDK），"
            "macOS 用 brew install vulkan-headers vulkan-loader。")
        set(STELQUICK_VULKAN_PROBE OFF)
    endif()
endif()

if(STELQUICK_VULKAN_PROBE)
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

# ── qmldir 必须编进资源（2026-09-21 定位的真 bug）────────────────────────────
# 症状：窗口正常出现，但同模块内的 QML 组件（SkyTestPage / DiagnosticPage）
#   实例化为空 Item —— A2 视口恒 0x0，报"视口物理尺寸过小，无法放置角标：1x1"。
# 根因：NO_PLUGIN 模式下 qt_add_qml_module 只把 QML_FILES 塞进 qrc，
#   生成的 qmldir（含 "SkyTestPage 1.0 qml/SkyTestPage.qml" 等类型声明）
#   仅以磁盘文件形式留在 OUTPUT_DIRECTORY。运行时资源树 qrc:/StelQuickUI/
#   里没有 qmldir → 模块类型表缺失 → MainWindow 能按 URL 直载，
#   其内部对 SkyTestPage 的引用却解析不到（静默变空 Item，无报错）。
# 处置：显式把 qmldir 作为资源加入模块。
qt_add_resources(stelQuickUI "stelquickui_qmldir"
    PREFIX "/StelQuickUI"
    BASE "${CMAKE_BINARY_DIR}/qmlmodule"
    FILES "${CMAKE_BINARY_DIR}/qmlmodule/qmldir"
)

# 包含路径以 src/ 为根："ui/quick/..."、"render/vulkan/..."
target_include_directories(stelQuickUI PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)

target_link_libraries(stelQuickUI PRIVATE
    Qt6::Quick
    Qt6::Qml
    Qt6::Gui
    Qt6::Core
)

# Qt6::GuiPrivate：main.cpp 需要 <QtGui/private/qvulkandefaultinstance_p.h>
# 取 Qt 全局唯一的默认 Vulkan 实例（避免自建实例与 Qt 内部实例并存导致
# surface 归属冲突 + 退出期崩溃，见 main.cpp 注释）。2026-09-21。
# 注意：Qt6::GuiPrivate 不在 Qt6 COMPONENTS 里，必须显式 find_package 才会出现。
find_package(Qt6 QUIET COMPONENTS GuiPrivate)
if(TARGET Qt6::GuiPrivate)
    target_link_libraries(stelQuickUI PRIVATE Qt6::GuiPrivate)
else()
    message(WARNING
        "未找到 Qt6::GuiPrivate → main.cpp 无法包含 qvulkandefaultinstance_p.h。\n"
        "  需要 Qt 的 private 开发头（Qt 官方安装器默认自带；包管理器可能要装 qt6-base-private-dev）。")
endif()

if(STELQUICK_VULKAN_PROBE)
    target_link_libraries(stelQuickUI PRIVATE Vulkan::Vulkan)
    target_compile_definitions(stelQuickUI PRIVATE STELQUICK_VULKAN_PROBE=1)
endif()

# ── Windows / MSVC 可移植性（2026-09-20 增补，移植前必须项）──────────────────
if(MSVC)
    # /utf-8：源码含大量中文注释，且为 UTF-8 **无 BOM**。MSVC 默认按系统代码页
    # （简体中文 Windows = GBK/936）解释源文件 → C4819 警告、注释串码，严重时
    # 字符串字面量被按多字节截断，printf 里带中文的诊断行会变成乱码或吞掉后续内容。
    # 全项目诊断信息都靠 stdout，乱码等于丢失证据链。必须显式 /utf-8。
    target_compile_options(stelQuickUI PRIVATE /utf-8)
endif()

if(WIN32)
    # 关键：强制**控制台子系统**。qt_add_executable 默认按 GUI 应用处理
    # （WIN32_EXECUTABLE=TRUE），进程不挂到父控制台 → 所有 printf 诊断与
    # 退出码证据全部丢失，自动化验收（A2CHECK / WINDOWTEST）直接变瞎。
    set_target_properties(stelQuickUI PROPERTIES WIN32_EXECUTABLE FALSE)

    # 部署辅助：build-ui\deploy 生成可独立拷贝到无 Qt 环境运行的目录。
    # 用法：cmake --build build-ui --config Release --target deploy
    find_program(STELQUICK_WINDEPLOYQT windeployqt HINTS "${QT6_INSTALL_PREFIX}/bin"
                 "${CMAKE_PREFIX_PATH}/bin")
    if(STELQUICK_WINDEPLOYQT)
        add_custom_target(deploy
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_BINARY_DIR}/deploy"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                    "$<TARGET_FILE:stelQuickUI>" "${CMAKE_BINARY_DIR}/deploy/"
            COMMAND "${STELQUICK_WINDEPLOYQT}" --release --no-translations
                    "$<TARGET_FILE:stelQuickUI>"
            WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/deploy"
            DEPENDS stelQuickUI
            COMMENT "windeployqt：收集 Qt 运行时到 build-ui/deploy")
    else()
        message(STATUS "未找到 windeployqt → 跳过 deploy 目标（仅影响独立分发，不影响本机运行）")
    endif()
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
# Windows 侧不需要这一步：Vulkan 由显卡驱动安装到 System32\vulkan-1.dll。
# 鸿蒙交叉编译（见 docs/HARMONY_PROBE.zh_CN.md）：
#   ~/Qt/6.12.0/harmonyos_arm64_v8a/bin/qt-cmake -S src/ui -B build-harmony \
#     -DCMAKE_FIND_ROOT_PATH=$HOME/.local/opt/ohos/additional-packages \
#     -DSTELQUICK_VULKAN_PROBE=OFF
#
# ── 本次移植的范围边界（重要）────────────────────────────────────────────
# 本文件是**独立工程**，只编 15 个文件（见 stelQuickUI_SOURCES），不涉及 Stellarium
# 根构建的依赖（Qt6 WebEngine/Charts/MultiMedia、libnova、gettext…）。
# 因此 Windows 上"看渲染效果 + 做驱动形态对照"只需 VS2022 + Qt6，**不需要**碰根
# CMakeLists。根构建（A3 集成）是另一件事，依赖面大得多，不要和本次混在一起做。
