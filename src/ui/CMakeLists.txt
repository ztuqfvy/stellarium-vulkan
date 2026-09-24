# stelQuickUI — Qt Quick Vulkan 宿主
#
# ── 两种构建形态（2026-09-23，T10 构建合流）─────────────────────────────────
# ① 子目标形态（里程碑 A3，推荐）：作为根构建的一部分，与 stellarium 共用引擎。
#      cmake -B build-release -S . -DENABLE_STELQUICKUI=ON \
#            -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qt
#      cmake --build build-release            # 同时产出 stellarium 与 stelQuickUI.app
#    此时本文件**不能**自建引擎副本：render/legacy/{FrameMailbox,LegacySkyHost}
#    已在 stellarium_lib_SRCS 里编译进 stelMain，重复编译会让同名符号出现两份定义。
#    故子目标形态下这两个文件被排除（见下方 STELQUICKUI_SHARED_SOURCES），改为链接 stelMain。
#
# ② 独立工程形态（A1/A2 阶段遗留，保留用于"小依赖面快速迭代"与 Windows 留证）：
#      macOS：
#        /opt/homebrew/bin/cmake -B build-ui -S src/ui -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qt
#        /opt/homebrew/bin/cmake --build build-ui
#      Windows（VS2022 x64 开发者命令行 / Qt 自带终端，见 docs/WINDOWS_BUILD.zh_CN.md）：
#        cmake -B build-ui -S src/ui -DCMAKE_PREFIX_PATH=C:/Qt/6.11.2/msvc2022_64 -G "Visual Studio 17 2022" -A x64
#        cmake --build build-ui --config Release --parallel
#    此形态不含引擎（只编本目录 15 个文件 + render 下的开发期替代物），仅用于看渲染效果。
#
# 运行（macOS 环境变量见 docs/BUILD_RECORD.zh_CN.md；Windows 无需任何环境变量）：
#   export VK_DRIVER_FILES=/opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json
#   export QT_VULKAN_LIB=/opt/homebrew/opt/vulkan-loader/lib/libvulkan.1.dylib
#   STELQUICK_AUTOTEST_SECONDS=5 ./build-release/src/ui/stelQuickUI.app/Contents/MacOS/stelQuickUI

# ── 形态判定 ────────────────────────────────────────────────────────────────
# 独立工程形态自己起 project()；子目标形态继承根工程的 project(Stellarium)、
# C++17、CMAKE_AUTOMOC 等设置，不做任何重复声明。
IF(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    SET(STELQUICKUI_STANDALONE ON)
ELSE()
    SET(STELQUICKUI_STANDALONE OFF)
ENDIF()

IF(STELQUICKUI_STANDALONE)
    cmake_minimum_required(VERSION 3.21)
    project(stelQuickUI LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_AUTOMOC ON)
ENDIF()

# Qt6::OpenGL（QtOpenGL 模块）为 A2 主体产帧侧所需：
# LegacySkyHost / LegacyTestScene 用 QOpenGLShaderProgram / QOpenGLBuffer /
# QOpenGLVertexArrayObject —— 这三者不在 QtGui 里，必须显式链接 QtOpenGL。
# 它是 qtbase 的一部分（libQt6OpenGL），所有官方 Qt6 安装都自带，不额外引入第三方依赖。
# 子目标形态下由根 CMakeLists 依据 ENABLE_STELQUICKUI 预先 find_package 过；此处兜底，
# 保证两种形态都一定拿到这四个 target。
IF(NOT TARGET Qt6::Quick OR NOT TARGET Qt6::Qml OR NOT TARGET Qt6::OpenGL)
    find_package(Qt6 REQUIRED COMPONENTS Quick Qml Gui Core OpenGL)
ENDIF()

# Vulkan 设备探针：桌面默认开；鸿蒙交叉编译时由命令行关闭（OHOS NDK 不保证提供 libvulkan）。
# Windows/Linux 上若未安装 Vulkan SDK（没有 find_package 所需的 headers/loader），
# 下面会自动降级为 OFF 并告警——渲染本身不依赖 SDK（Qt 运行时 dlopen 系统 Vulkan），
# 只有"设备探针"这一项诊断数据会缺。这让"没装 SDK 也能先跑起来看效果"成立。
option(STELQUICK_VULKAN_PROBE "构建 VkDeviceProbe（需要有 Vulkan 加载库）" ON)

# ── A3 前置探针：Widgets 宿主形态（2026-09-23）──────────────────────────────
# 打开后宿主类从 QGuiApplication 换成 QApplication，用于验证两件事：
#   ① QApplication 承载 QQuickWindow 时渲染零退化（QApplication 是超集，理论安全，
#      但 stelQuickUI 历史上从未在这个宿主下跑过）；
#   ② 引擎现成引导路径（StelMainView/QGraphicsView，Widgets 类）能与 QML 窗口同进程。
# 默认关闭：既有 A1/A2/DYN 基线是在 QGuiApplication 下取的，不能悄悄换宿主。
option(STELQUICKUI_WIDGETS_HOST "用 QApplication 承载 QQuickWindow（A3 引擎共存前置）" OFF)

# ── 与 stelMain 重复的源文件（仅独立工程形态编译）──────────────────────────
# render/legacy/FrameMailbox 与 render/legacy/LegacySkyHost 已随 stellarium_lib_SRCS
# 编入 stelMain。子目标形态下必须排除，否则：
#   - stelQuickUI 自带一份定义，链接器又可能从 stelMain 拉入同名目标文件
#     → 轻则体积翻倍，重则 duplicate symbol；
#   - 更糟的是"看起来能跑"：两份实现各自持有静态状态（如 FrameMailbox 的序号），
#     调试时表现为随机不可复现的行为差异——比链接失败更难查。
# 因此这两个文件在子目标形态下**不编译**，统一由 stelMain 提供。
SET(STELQUICKUI_SOURCES_SHARED
    ../render/legacy/FrameMailbox.hpp
    ../render/legacy/FrameMailbox.cpp
    ../render/legacy/LegacySkyHost.hpp
    ../render/legacy/LegacySkyHost.cpp
)

# ── T16：仿真时钟控制器（纯逻辑，无引擎依赖）────────────────────────────────
# 子目标形态下它已随 stellarium_lib_SRCS 编入 stelMain；独立工程形态下没有引擎，
# 必须自带一份（否则 main.cpp 的 STELQUICK_CLOCK_CHECK 分支链接失败——
# 那个分支被刻意设计成可在无引擎环境运行，正是为了把"时钟语义"与"引擎是否跑通"解耦）。
# 与 FrameMailbox/LegacySkyHost 同一条纪律：**同一形态下只能有一份定义**。
SET(STELQUICKUI_SOURCES_CORE_PURE
    ../core/StelClockController.hpp
    ../core/StelClockController.cpp
)

set(stelQuickUI_SOURCES
    main.cpp
    A2FrameCheck.hpp
    A2FrameCheck.cpp
    # T12：动态帧生产者的运行期契约（DynFrameCheck 只依赖它，故两种生产者同构可换）
    IFrameProducer.hpp
    DynFrameCheck.hpp
    DynFrameCheck.cpp
    # 消费侧接线：动态帧生产者（LegacySkyHost 动态驱动，独立线程）
    LiveFrameSource.hpp
    LiveFrameSource.cpp
    # T11 引擎共进程帧驱动（真实引擎产帧；内容用双宏守卫，独立工程/默认形态编译为空）
    LiveSkyRuntime.hpp
    LiveSkyRuntime.cpp
    # 消费侧全量计量长跑（P-BRG-01 验收）
    SkyLongRun.hpp
    SkyLongRun.cpp
    LegacyHostCheck.hpp
    LegacyHostCheck.cpp
    # T15 命令通路：QML → ActionRouter → AppFacade → 引擎（无引擎形态可编译 no-op）
    ../app/AppFacade.hpp
    ../app/AppFacade.cpp
    ../app/ActionRouter.hpp
    ../app/ActionRouter.cpp
    ../app/AppFacadeCheck.hpp
    ../app/AppFacadeCheck.cpp
    # T17 搜索/选择：两个模型 + 自检。同样在独立工程形态下编为安全空实现
    # （模型内部用 STELQUICK_HAS_ENGINE 守卫，不是靠 CMake 排除文件——
    #  这样 QML 侧在无引擎形态也能拿到"明确的空态理由"而不是链接失败）。
    ../app/SearchResultsModel.hpp
    ../app/SearchResultsModel.cpp
    ../app/ObjectInfoModel.hpp
    ../app/ObjectInfoModel.cpp
    ../app/SearchModelCheck.hpp
    ../app/SearchModelCheck.cpp
    ../app/LocateCheck.hpp
    ../app/LocateCheck.cpp
    quick/BackendInfo.hpp
    quick/BackendInfo.cpp
    quick/SkyViewport.hpp
    quick/SkyViewport.cpp
    # A2 帧桥：测试图案 + 开发期帧源（A2 主体完成后 StaticFrameSource 被旧 GL 读回替换）
    ../render/legacy/TestPattern.hpp
    ../render/legacy/TestPattern.cpp
    ../render/legacy/StaticFrameSource.hpp
    ../render/legacy/StaticFrameSource.cpp
    # A2 主体产帧侧（T6/T7）：旧宿主显式帧驱动 + 读回。
    # LegacyTestScene 与 StaticFrameSource 同性质，都是开发期替代物，A3 换成真实天空。
    ../render/legacy/LegacyTestScene.hpp
    ../render/legacy/LegacyTestScene.cpp
)

# 独立工程形态自带帧桥与旧宿主；子目标形态交由 stelMain 提供。
IF(STELQUICKUI_STANDALONE)
    list(APPEND stelQuickUI_SOURCES ${STELQUICKUI_SOURCES_SHARED})
    list(APPEND stelQuickUI_SOURCES ${STELQUICKUI_SOURCES_CORE_PURE})
ENDIF()

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
    OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/qmlmodule
    QML_FILES
        qml/MainWindow.qml
        qml/DiagnosticPage.qml
        qml/SkyTestPage.qml
        qml/SearchPage.qml
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
    BASE "${CMAKE_CURRENT_BINARY_DIR}/qmlmodule"
    FILES "${CMAKE_CURRENT_BINARY_DIR}/qmlmodule/qmldir"
)

# 包含路径以 src/ 为根："ui/quick/..."、"render/vulkan/..."
target_include_directories(stelQuickUI PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)

target_link_libraries(stelQuickUI PRIVATE
    Qt6::Quick
    Qt6::Qml
    Qt6::Gui
    Qt6::Core
    Qt6::OpenGL
)

# A3 前置探针：QApplication 所在模块。
# 子目标形态下 stelMain 已把 Widgets 传递进来，仍显式声明——不依赖传递依赖的实现细节；
# 独立工程形态则必须显式查找。
IF(STELQUICKUI_WIDGETS_HOST)
    IF(NOT TARGET Qt6::Widgets)
        find_package(Qt6 REQUIRED COMPONENTS Widgets)
    ENDIF()
    target_link_libraries(stelQuickUI PRIVATE Qt6::Widgets)
    target_compile_definitions(stelQuickUI PRIVATE STELQUICK_WIDGETS_HOST=1)
ENDIF()

# ── 子目标形态：链接引擎 ────────────────────────────────────────────────────
# stelMain 是根构建的引擎静态库（stellarium_lib_SRCS，含 StelApp/StelCore/
# StelMainView 与 render/legacy 帧桥）。stelQuickUI 链接它 = "与旧宿主同进程同一份引擎"，
# 这正是 A3 后续把渲染回调换成 setJD/update/draw 的前提。
# TARGET_EXISTS 守卫：独立工程形态下 stelMain 不存在，跳过。
# 注意 target_include_directories(stelMain PRIVATE src/) 是 PRIVATE，
# 所以这里必须自己带 include 根（上面已做）。
IF(NOT STELQUICKUI_STANDALONE)
    IF(TARGET stelMain)
        target_link_libraries(stelQuickUI PRIVATE stelMain)
        # 声明"本形态能看见引擎符号"：main.cpp 的 A3 前置探针
        # （STELQUICK_ENGINE_COEXIST=1）需要 StelMainView/StelApp/StelFileMgr。
        # 独立工程形态没有引擎，相关代码整体不编译。
        target_compile_definitions(stelQuickUI PRIVATE STELQUICK_HAS_ENGINE=1)
    ELSE()
        message(FATAL_ERROR
            "子目标形态下未找到 stelMain。src/ui 必须在 src/CMakeLists.txt 之后 "
            "通过 ADD_SUBDIRECTORY(ui) 引入。")
    ENDIF()
ENDIF()

# Qt6::GuiPrivate：main.cpp 需要 <QtGui/private/qvulkandefaultinstance_p.h>
# 取 Qt 全局唯一的默认 Vulkan 实例（避免自建实例与 Qt 内部实例并存导致
# surface 归属冲突 + 退出期崩溃，见 main.cpp 注释）。2026-09-21。
# 注意：Qt6::GuiPrivate 不在 Qt6 COMPONENTS 里，必须显式 find_package 才会出现。
# 根构建在 USE_BUNDLED_QTCOMPRESS=1（默认）下不查 GuiPrivate，故此处独立查找。
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
# ── 依赖面边界（重要）────────────────────────────────────────────────────────
# 独立工程形态：只编本文件列出的源，不涉及 Stellarium 根构建的依赖
#   （Qt6 WebEngine/Charts/MultiMedia、libnova、gettext…）→ Windows 上"看渲染效果 +
#   做驱动形态对照"只需 VS2022 + Qt6。
# 子目标形态：随根构建一起承担全部依赖（含 WebEngine/Charts/MultiMedia 等），
#   这是"引擎与 QML 同进程"必须付的代价，也是 T10 合流的实质内容。
