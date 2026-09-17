# 构建 Stellarium

您好，感谢您对 Stellarium 的关注！

如果您想测试预构建的测试版，请访问 https://github.com/Stellarium/stellarium-data/releases/tag/weekly-snapshot

## 为什么要从源代码构建？

每当 Stellarium 发布新版本时，其源代码都会发布在 Github 的打包系统中。
通过这种方式构建发布的源代码，您将获得一个功能上与对应版本的二进制文件完全相同的 Stellarium 工作副本。

此外，您也可以通过 Git 获取"开发中"的源代码。
这类代码可能包含自上次 Stellarium 发布以来实现的新功能或错误修复，因此通常更有趣。

**警告：** Git 版本的 Stellarium 源代码是正在进行中的工作，
因此可能产生不稳定的程序，可能根本无法运行，甚至（极少情况下）无法编译。

## 集成开发环境 (IDE)

如果您计划开发 Stellarium，强烈建议使用 IDE。您可以选择任何您喜欢的 IDE，
但推荐使用 QtCreator，因为它最适合 Qt 开发。

在 QtCreator 中，打开 Stellarium 源代码目录中的 `CMakeLists.txt`。
默认设置会创建一个包含所有有用插件的调试版本。
在"项目"选项卡中（左侧垂直栏中的按钮），您至少应配置 Debug 和 Release 两种构建模式。

别忘了在 Extras/Settings/C++/Coding style（使用 Import... 按钮）中加载[代码风格文件](doc/stellarium-ide.xml)。

## 前提软件包

要构建和开发 Stellarium，您的发行版可能需要安装多个软件包。以下是列表。

### 必需依赖项

- 一个能够编译 C++17 代码的 C++ 编译器（[GCC](https://gcc.gnu.org/) 7 或更高版本，
  Clang 6 或更高版本，MSVC 2017 (15.7) 或更高版本；Qt6 需要 MSVC2019）
- [CMake](https://www.cmake.org/) 3.16.0 或更高版本 —— 众多开源项目使用的构建系统
- [Qt Framework](https://www.qt.io/) 5.12.0/6.2.0 或更高版本。我们推荐 5.15.2 或 6.5.1
- [OpenGL](https://www.opengl.org/) —— 图形库
- [Zlib](https://www.zlib.net) —— 压缩库

### 依赖项

### 可选依赖项

- [Git](https://git-scm.com) —— 获取最新源代码变更所必需
- [gettext](https://www.gnu.org/software/gettext/) —— 开发者提取翻译文本行所必需
- [Doxygen](http://doxygen.org/) —— 如果您想构建 API 文档，需要此工具
- [Graphviz](http://www.graphviz.org/) —— 构建 API 文档并包含精美类图所必需
- [libgps](https://gpsd.gitlab.io/gpsd/index.html) —— 如果您想构建带有 GPS 支持的 Stellarium（仅限 Linux/macOS）

### 可选捆绑依赖项

如果系统中未找到这些依赖项，它们将被自动下载。
详细信息请参见[维护者业务](MAINTAINER_BUSINESS.md)。

- [INDI](https://indilib.org)
- [QXlsx](https://github.com/QtExcel/QXlsx)
- [ShowMySky](https://10110111.github.io/CalcMySky/)，可通过 CMake 参数
  `-DENABLE_SHOWMYSKY=OFF` 禁用。如果启用（默认），还需要 `libglm-dev libeigen3-dev`。

### 手动下载依赖项 (CPM)

如果由于网络限制无法自动下载依赖项，您可以手动获取它们。依赖项通常在所有 `CMakeLists.txt` 文件中以 `URL https://github.com/...` 的形式列出，使用 `CPMAddPackage`、`CPMFindPackage` 等方式。

下载后，将它们放置在以下目录结构中（以 Windows 下的 `QXlsxQt6` 为例）：

```
<build_dir>/_deps/qxlsxqt6-subbuild/qxlsxqt6-populate-prefix/src/v1.5.0.tar.gz
```

> 文件名必须与 URL 完全匹配，目录名使用小写的软件包名称。

您也可以编写脚本来自动化此过程。

### 安装这些软件包

要安装所有这些软件包，请使用以下命令：

#### Debian / Ubuntu

##### Qt5

```
sudo apt install build-essential cmake zlib1g-dev libgl1-mesa-dev libdrm-dev gcc g++ \
                 graphviz doxygen gettext git libgps-dev libqt5qxlsx-dev \
                 gstreamer1.0-plugins-base gstreamer1.0-plugins-good gstreamer1.0-pulseaudio \
                 gstreamer1.0-libav gstreamer1.0-vaapi qtbase5-dev \
                 qtbase5-private-dev qtscript5-dev libqt5svg5-dev qttools5-dev-tools \
                 qttools5-dev libqt5opengl5-dev qtmultimedia5-dev libqt5multimedia5-plugins \
                 libqt5serialport5 libqt5serialport5-dev qtpositioning5-dev libqt5positioning5 \
                 libqt5positioning5-plugins qtwebengine5-dev libqt5charts5-dev \
                 libexiv2-dev libnlopt-cxx-dev libtbb-dev libtbb2 libqt5concurrent5 \
                 libmd4c-dev libmd4c-html0-dev qt5-image-formats-plugins
```

##### Qt6

Ubuntu 22.04 自带 Qt5.15 和 Qt6.2。要使用 Qt6 构建：

```
sudo apt install build-essential cmake zlib1g-dev libgl1-mesa-dev libdrm-dev libglx-dev \
                 gcc g++ graphviz doxygen gettext git libxkbcommon-x11-dev libgps-dev \
                 gstreamer1.0-plugins-base gstreamer1.0-plugins-good gstreamer1.0-pulseaudio \
                 gstreamer1.0-libav gstreamer1.0-vaapi qt6-image-formats-plugins libqt6svg6-dev \
                 qt6-base-private-dev qt6-multimedia-dev qt6-positioning-dev qt6-tools-dev \
                 qt6-tools-dev-tools qt6-base-dev-tools qt6-qpa-plugins libqt6svgwidgets6 \
                 qt6-l10n-tools qt6-webengine-dev qt6-webengine-dev-tools libqt6charts6-dev \
                 libqt6charts6 libqt6opengl6-dev libqt6positioning6-plugins libqt6serialport6-dev \
                 qt6-base-dev libqt6webenginecore6-bin libqt6webengine6-data \
                 libexiv2-dev libnlopt-cxx-dev libqt6concurrent6 libmd4c-dev libmd4c-html0-dev
```

Ubuntu 24.04 拥有 Qt6.4，因此支持语音输出。在上述软件包基础上添加以下内容（可根据需要添加更多 speech-dispatcher 组件）：

```
sudo apt install libqt6texttospeech6 qt6-speech-dev qt6-speech-speechd-plugin qt6-speech-flite-plugin \
                 flite speech-dispatcher speech-dispatcher-flite speech-dispatcher-espeak-ng
```


#### Fedora / CentOS / AlmaLinux / Rocky Linux

注意：此方法适用于 RHEL/CentOS/AlmaLinux/Rocky Linux 8 或更高版本以及较新版本的 Fedora。要在 CentOS 7 或更旧版本上构建，
请参阅[Qt 版本过旧的 Linux](#qt-版本过旧的-linux)。

```
sudo dnf install cmake gcc graphviz doxygen gettext git \
                 qt5-qtbase-devel qt5-qtbase-private-devel qt5-qttools-devel qt5-qtscript-devel \
                 qt5-qtsvg-devel qt5-qtmultimedia-devel qt5-qtserialport-devel qt5-qtlocation-devel \
                 qt5-qtcharts-devel qt5-qtwebengine-devel exiv2-devel
```

#### Qt 版本过旧的 Linux

Stellarium 紧跟 Qt 的近期发布版本，因此许多 Linux 发行版仓库中包含的 Qt 版本可能不够新，無法用于构建 Stellarium。对于 Ubuntu 来说，"backports" 仓库通常已足够，但有时可能需要在软件包管理器之外进行安装。以下是操作方法。

Qt 开发团队提供了二进制安装程序。如果您想从源代码自行构建 Qt，这没问题，但会花費''很长''的时间。我们推荐以下手动安装最新 Qt 的步骤（目前要求：5.12 或以上版本）：
- 从 [Qt 公司](http://www.qt.io/download-open-source/) 下载 Linux/X11 软件包。根据需要选择 32 位或 64 位。
- 将其安装到 `/opt/Qt5`
- 当您想要构建 Stellarium 时，执行以下命令来设置环境，以便使用新的 Qt（以 64 位软件包为例）：
```
export QTDIR=/opt/Qt5/5.12.12/gcc_64
export PATH=$QTDIR/bin:$PATH
export LD_LIBRARY_PATH=$QTDIR/lib:$LD_LIBRARY_PATH
```
- 安装完成后，您应编写一个脚本来设置 `LD_LIBRARY_PATH`，然后调用 Stellarium：
```
#!/bin/sh
export QTDIR=/opt/Qt5/5.12.12/gcc_64
export LD_LIBRARY_PATH=$QTDIR/lib:$LD_LIBRARY_PATH
./stellarium
```

#### 不含 QtWebEngine 的 Linux

在某些发行版上（已知的有 ARM 系统，如 Raspberry OS (Raspbian)），没有 QtWebEngine。CMake 脚本会检查已安装的 `qtwebengine5` 软件包，如果未找到则会发出警告，但 Stellarium 仍然会构建，只是不支持 QtWebEngine。其结果将在系统 Web 浏览器中显示。

#### macOS

- 安装最新版本的 [Apple 开发者工具](https://developer.apple.com/xcode/)。
- 安装 [Homebrew](https://brew.sh/)。
- 安装所需软件包：
  ```
  brew install cmake git gettext
  brew link gettext --force
  ```

  在 macOS 11 及以上版本中，如果
  ```
  brew link gettext --force
  ```

  因以下错误而失败：
  ```
  Linking /usr/local/Cellar/gettext/0.21...
  Error: Could not symlink include/autosprintf.h
  /usr/local/include is not writable.
  ```
  请尝试以下操作：

  ```
  sudo mkdir /usr/local/include
  sudo chown -R $(whoami) $(brew --prefix)/*

  ```

- 安装最新的 Qt 5：
  ```
  brew install qt@5
  ```
- 将 Qt 添加到您的 PATH 环境变量中：
  Intel Mac：在 `~/.bash_profile`（Bash）或 `~/.zprofile`（Zsh）中添加以下行：
  ```
  export PATH=/usr/local/opt/qt@5/bin:$PATH
  ```
  ARM 架构（Apple Silicon）Mac：在 `~/.bash_profile`（Bash）或 `~/.zprofile`（Zsh）中添加以下行：
  ```
  export PATH=/opt/homebrew/opt/qt@5/bin:$PATH
  ```

您也可以使用 Qt 公司的发行版来安装[最新稳定版本](https://www.qt.io/download-qt-installer)的 Qt。在这种情况下，将 Qt 添加到 PATH 环境变量需要在 `~/.bash_profile`（Bash）或 `~/.zprofile`（Zsh）文件中添加以下行（以安装 Qt 5.12.12 为例）：
```
export PATH=~/Qt/5.12/clang_64/bin:$PATH
```

#### 不含 QtWebEngine 的 macOS

在 ARM 架构（Apple Silicon）Mac 上，不支持 QtWebEngine 或其存在缺陷（Qt 5.15.2 中的 QtWebEngine）。CMake 脚本会检查已安装的 `QtWebEngine` 库，如果系统中不存在，则 Stellarium 将在不支持 QtWebEngine 的情况下进行构建。其结果将在系统 Web 浏览器中显示。

#### Windows

- 从微软官网安装 [Microsoft Visual Studio Community 2019 或 2022](https://visualstudio.microsoft.com/downloads/)（或"更好"的版本——如 Professional）。Qt 5.15 需要 MSVC2019。
- 要获取 Stellarium 的源代码，您需要安装 Git 环境。[Git for Windows](https://git-scm.com/download/win) 看起来不错，或者 Git Bash 和 Git GUI，任何适合您的都可以。但这不是必需的。
- 从 [Qt 公司](http://www.qt.io/download-open-source/) 获取[最新版本的 Qt]。我们推荐使用 Qt 5.15.2，或者更好的 Qt6。对于 Qt5，您必须在众多复选框中选中 Qt Script 和 msvc2019。

安装完所有必需的库和工具后，您应该配置构建环境。

将 `C:\Qt\Qt5.15.2` 添加到您的 `PATH` 变量中——对于 32 位系统，应将 `C:\Qt\Qt5.15.2\msvc2019;C:\Qt\Qt5.15.2\msvc2019\bin` 添加到 `PATH` 变量中；对于 64 位系统，应将 `C:\Qt\Qt5.15.2\msvc2019_64;C:\Qt\Qt5.15.2\msvc2019_64\bin` 添加到 `PATH` 变量中。
（请将 Qt 的版本号和 Visual Studio 的版本（2017/2019）替换为您安装的实际版本）
如果您还想运行 ShowMySky 天空模型，还需将另一个目录添加到 PATH 变量中。这取决于您的构建环境。如果构建输出到 `D:\StelDev\GIT\build-stellarium-Desktop_Qt_6_5_1_MSVC2019_64bit-Release\`，则路径应为 `D:\StelDev\GIT\build-stellarium-Desktop_Qt_6_5_1_MSVC2019_64bit-Release\_deps\showmysky-qt6-build\ShowMySky`

**ANGLE 问题：**

- 基于 Qt5 构建的 ANGLE 库应取自 Qt 5.6（所有后续版本均不可用），可以下载
- [x64 版本](https://github.com/Stellarium/stellarium-data/releases/download/qt-5.6/libGLES-x64.zip)
- 和 [x32 版本](https://github.com/Stellarium/stellarium-data/releases/download/qt-5.6/libGLES-Win32.zip)。
- （别问我们为什么。请自行寻找解决方案！）

**WSL：找不到 libQt5Core.so.5**

WSL 的全新安装可能会遇到找不到 libQt5Core.so.5 的问题。请运行：
```
sudo strip --remove-section=.note.ABI-tag /usr/lib/x86_64-linux-gnu/libQt5Core.so.5
```
（https://superuser.com/questions/1347723/arch-on-wsl-libqt5core-so-5-not-found-despite-being-installed）

**已知的 Qt 5.15.x 限制：**

- Qt 5.15.0 和 5.15.1 的 `lconvert` 存在缺陷，不应使用。此外，Qt 5.15.2 的 `lconvert` 在翻译几 MB 的字符串时仍可能分配 GB 级别的内存（如果能获取到的话）。

**注意：** 对 `PATH` 变量进行更改后，应重启计算机以使更改生效。

#### Windows（静态版本）

您可以使用 MSVC-static 套件构建静态版本（以安装 Qt 5.15.12 和 MSVC2019 为例）：

要准备静态套件，请准备 Qt 5.15.12 的源代码包，并配置编译工具（Python、Ruby、Perl 和 Visual Studio 2019）。进入源代码文件夹：

```
configure.bat -static -prefix "D:\Qt\msvc2019_static" -confirm-license -opensource  -debug-and-release -platform win32-msvc  -nomake examples -nomake tests  -plugin-sql-sqlite -plugin-sql-odbc -qt-zlib -qt-libpng -qt-libjpeg -opengl desktop -mp
nmake
nmake install
```

编译完成后，在 Qt Creator 中配置套件。将套件 "Desktop Qt 5.15.12 MSVC" 克隆为 "Desktop Qt 5.15.12 MSVC (static)"。然后配置 CMake Generator 使用 NMake Makefiles JOM + Extra generator: CodeBlocks。

最后，在 Qt Creator 中打开 CMakeLists.txt，使用 MSVC-static 套件进行构建。

## 获取源代码

我们推荐使用我们的 Git 仓库副本来构建您自己的安装版本，
因为它包含了构建所需的一些依赖项。

### 解压包含源代码的 tarball 或 ZIP 文件

您可以从以下地址获取源代码：

```
https://github.com/Stellarium/stellarium/releases
```

在终端中执行以下命令（如果您愿意，可以使用 arK 或其他图形化归档工具）：

```
$ tar zxf stellarium-26.1.tar.gz
```
现在您应该有一个名为 `stellarium-26.1` 的目录，其中包含源代码。


### 从 GitHub 克隆项目

要创建副本，请从您的操作系统发行版仓库或从 https://git-scm.com/ 安装 Git。

Git 仓库已经变得相当大（约 2GB）。您不需要完整的历史记录来构建或继续开发，可以尝试*无 blob 克隆*
（https://github.blog/2020-12-21-get-up-to-speed-with-partial-clone-and-shallow-clone/）：

```
$ git clone --filter=blob:none https://github.com/Stellarium/stellarium.git
$ cd stellarium
```

否则，要获取完整的仓库，请执行以下命令：

```
$ git clone https://github.com/Stellarium/stellarium.git
$ cd stellarium
```

如果您打算在 Windows 环境中贡献代码，您**必须**配置 Git 使用 Unix 风格的换行符。
（--global 参数会应用到所有项目。）
（https://docs.github.com/en/get-started/getting-started-with-git/configuring-git-to-handle-line-endings）

```
$ git config [--global] core.autocrlf true
```

### 从 GitHub 下载源代码

您可以通过网页从 GitHub [下载](https://github.com/Stellarium/stellarium/archive/master.zip)最新的源代码。

#### Windows 特别说明

在 Windows 上，将文件（`master.zip` 或 `stellarium-26.1.tar.gz`）保存到 `C:/Devel` 目录中（仅作示例）。您需要在 Windows 中安装一个解压缩程序，例如 [7-Zip](http://www.7-zip.org/)。为简单起见，源代码树的根目录将设为 `C:/Devel/stellarium`。

## 构建

假设您已收集了所有必要的库，以下是构建和运行 Stellarium 所需的步骤：

### 在 Linux 上
```
$ mkdir -p build/unix
$ cd build/unix
$ cmake -DCMAKE_INSTALL_PREFIX=/opt/stellarium ../..
$ make -jN
```

### 在 macOS 上
```
$ mkdir -p build/macosx
$ cd build/macosx
$ cmake ../..
$ make -jN
```

### 在 Windows 上
```
$ md build
$ cd build
$ md msvc
$ cd msvc
$ cmake -DCMAKE_INSTALL_PREFIX=c:\stellarium-bin -G "Visual Studio 16 2019" ../..
$ cmake --build . --  /maxcpucount:N /nologo
```

对于 Visual Studio 2017：
```
$ cmake -DCMAKE_INSTALL_PREFIX=c:\stellarium-bin -G "Visual Studio 15 2017 Win64" ../..
```

将 `-j` 中的 `N`（或 `/maxcpucount` 中的 `N`）替换为您希望在构建时使用的 CPU 核心数。

如果您使用官方 Qt 安装程序安装了 Qt5，则需要在配置 Stellarium 的 cmake 调用中传入 `CMAKE_PREFIX_PATH` 参数，例如：

```
$ cmake -DCMAKE_PREFIX_PATH=/opt/Qt5 ../..
```

当您使用 QtCreator IDE 时，构建目录由 IDE 创建。在 Windows 上，似乎会建议一个目录名称，但您需要手动创建它。

您可以通过在 ~/stellarium 中执行 `git pull --rebase` 来保持副本为最新。欢迎将补丁发送到我们的邮件列表 stellarium@googlegroups.com。

#### Visual Studio 2022（多配置构建）：

选择一个靠近驱动器根目录的工作目录 <work_dir>，例如 <work_dir> = "E:\Dev"。
**注意**：VS 2022 会为某些子目录生成非常长的路径，因此请保持 <work_dir> 尽量短，以避免超出 260 字符限制。

从 GitHub 下载 Stellarium 到：<stel_dir> = <work_dir> + "\stellarium"

创建一个 CMakePresets.json 文件并将其保存在 <stel_dir> 中。此文件必须与 Stellarium 的顶级 CMakeLists.txt 在同一目录中。CMakePresets.json 文件定义了所有必需的构建配置（例如 Debug、Release、RelWithDebInfo）。它应按照多配置构建流程进行结构化（参见下方示例）。但是，必须根据您的设备和软件设置进行自定义。

以下是一个为使用 Qt 6.7.3 框架构建 Stellarium 而创建的示例。CMake 参数（-D 标志）定义在 "cacheVariables" 部分中。根据需要修改/添加您自己的参数。部分参数用于指示 VS 2022 在哪里找到某些库（.lib）或包含文件（.hpp），这些特定于此示例，可能不适用于您的设置。

<CMakePresets.json 文件示例>

{
  "version": 3,
  "cmakeMinimumRequired": { "major": 3, "minor": 21, "patch": 0 },
  "configurePresets": [
    {
      "name": "vs2022-multi-config",
      "displayName": "Stellarium (VS2022 Multi-Config)",
      "description": "使用 Visual Studio 2022 进行 Debug、Release 和 RelWithDebInfo 的统一构建树",
      "generator": "Visual Studio 17 2022",
      "architecture": {
        "value": "x64"
      },
      "binaryDir": "${sourceDir}/build",
      "cacheVariables": {
        "CMAKE_CONFIGURATION_TYPES": "Debug;Release;RelWithDebInfo",
        "CMAKE_BUILD_TYPE": "Debug",
        "CMAKE_SUPPRESS_DEVELOPER_WARNINGS": "1",
        "CMAKE_PREFIX_PATH": "C:/Qt/6.7.3/msvc2022_64;C:/Dev/Libs/exiv2-0.28.7/lib/cmake/exiv2",
        "QT_QMAKE_EXECUTABLE": "C:/Qt/6.7.3/msvc2022_64/bin/qmake.exe",
        "ENABLE_SCRIPTING": "ON",
        "ENABLE_GPS": "ON",
        "ENABLE_TESTING": "ON",
        "ENABLE_NLS": "ON",
        "SCM_SHOULD_ENABLE_CONVERTER": "TRUE",
        "GETTEXTPO_LIBRARY": "C:/Dev/Libs/gettextpo/lib/libgettextpo.lib",
        "GETTEXTPO_INCLUDE_DIR": "C:/Dev/Libs/gettextpo/include",
        "LIBTIDY_LIBRARY": "C:/Dev/Libs/libtidy/lib/libtidy.lib",
        "LIBTIDY_INCLUDE_DIR": "C:/Dev/Libs/libtidy/include",
        "EXIV2_LIBRARY": "C:/Dev/Libs/exiv2-0.28.7/lib/exiv2.lib",
        "EXIV2_INCLUDE_DIR": "C:/Dev/Libs/exiv2-0.28.7/include",
        "exiv2_DIR": "C:/Dev/Libs/exiv2-0.28.7/lib/cmake/exiv2",
        "Qt6LinguistTools_DIR": "C:/Qt/6.7.3/msvc2022_64/lib/cmake/Qt6LinguistTools"
      }
    }
  ],
  "buildPresets": [
  {
    "name": "Debug",
    "configurePreset": "vs2022-multi-config",
    "configuration": "Debug"
  },
  {
    "name": "Release",
    "configurePreset": "vs2022-multi-config",
    "configuration": "Release"
  },
  {
    "name": "RelWithDebInfo",
    "configurePreset": "vs2022-multi-config",
    "configuration": "RelWithDebInfo"
  }
  ]
}

**注意**：用于解析运行时功能的动态库（.dll）应放置在您主系统驱动器上的一个公共位置。将该路径添加到您的用户/系统环境变量 PATH 中，以便 Stellarium 在运行时能够发现它们。

打开 Visual Studio 2022。选择"继续但无需代码"。

如果您想使用特定的 cmake.exe，请转到 Tools -> Options，导航到 Cmake -> General 并勾选相应复选框并填写路径。否则 VS 2022 将使用自带的 cmake.exe。外部 cmake 的路径同样应包含在您的系统环境变量 PATH 中。

现在您已准备好开始构建过程。转到 File -> Open -> Folder... 并选择 <stel_dir>。

VS 2022 将自动检测 CMakePresets.json 文件并开始配置第一个 Preset Configuration，即 'Debug'。您可以在 CMake 输出窗口中跟踪配置过程。如果成功（或有可接受的轻微错误），您可以在 VS 2022 中打开 Developer Command Prompt 来构建此配置。导航到 <stel_dir> 并输入：

cmake --build --preset Debug

**注意**：请勿使用 VS 2022 的顶部菜单（即 Build -> Build All）。虽然这可能对第一次构建有效，但最终此子菜单会消失……这是 VS 2022 的一个 Bug。所有构建和重新构建请依赖 Developer Command Prompt。

如果 Debug 构建成功，请从 VS 2022 的 Configuration 下拉菜单中选择 CMakePresets.json 文件中指定的下一个配置，在此示例中即 Release。

当您在 Configuration 下拉列表中更改此设置后，VS 2022 将启动 cmake 来配置构建。然后在 Developer Command Prompt 中输入以下命令进行构建：

cmake --build --preset Release

对最后一个构建（即 RelWithDebInfo）重复相同的过程。

cmake --build --preset RelWithDebInfo

要运行测试套件（如果 ENABLE_TESTING = ON），请再次使用 Developer Command Prompt。导航到 <stel_dir>+"\build"。然后输入以下三条命令之一：

ctest -C Debug
ctest -C Release
ctest -C RelWithDebInfo

一旦三种构建全部完成且单元测试验证通过，您可以关闭文件夹视图并打开位于 stellarium/build/Stellarium.sln 的 Stellarium 解决方案。

File --> Close Folder
File --> Open --> Project/Solution...

Visual Studio 2022 应以 Debug/x64 配置打开 Stellarium。Solution Explorer 应显示所有 Stellarium 项目。右键单击 stellarium 项目，选择 "Set as Startup Project"。在此阶段，您已准备好进一步修改/重新构建代码，测试和运行 Stellarium，或者在选择为启动项目后运行 Solution Explorer 视图中列出的任何测试用例项目。

注意：要使用 Release 或 RelWithDebInfo 配置，请先从 Visual Studio 2022 的顶部下拉菜单中选择相应配置。

#### Visual Studio 2026（多配置构建）：

您也可以使用这个于 2025 年 11 月新发布的 Visual Studio 版本来构建和开发 Stellarium。此版本修复了 VS 2022 在构建步骤中的 Bug，从而在一定程度上简化了 Stellarium 的构建过程。以下是较上述 Visual Studio 2022（多配置构建）流程的变更之处。

使用一个略有不同的 CMakePresets.json 文件，并根据您的开发环境进行调整。

<VS 2026 的 CMakePresets.json 文件示例>
{
  "version": 3,
  "cmakeMinimumRequired": { "major": 3, "minor": 21, "patch": 0 },
  "configurePresets": [
    {
      "name": "vs2026-multi-config",
      "displayName": "Stellarium (VS2026 Multi-Config)",
      "description": "使用 Visual Studio 2026 进行 Debug、Release 和 RelWithDebInfo 的统一构建树",
      "generator": "Visual Studio 18 2026",
      "architecture": {
        "value": "x64"
      },
      "binaryDir": "${sourceDir}/build",
      "cacheVariables": {
        "CMAKE_CONFIGURATION_TYPES": "Debug;Release;RelWithDebInfo",
        "CMAKE_BUILD_TYPE": "Debug",
        "CMAKE_SUPPRESS_DEVELOPER_WARNINGS": "1",
        "CMAKE_PREFIX_PATH": "C:/Qt/6.7.3/msvc2022_64;C:/Dev/Libs/exiv2-0.28.7/lib/cmake/exiv2",
        "QT_QMAKE_EXECUTABLE": "C:/Qt/6.7.3/msvc2022_64/bin/qmake.exe",
        "ENABLE_SCRIPTING": "ON",
        "ENABLE_GPS": "ON",
        "ENABLE_TESTING": "ON",
        "ENABLE_NLS": "ON",
        "SCM_SHOULD_ENABLE_CONVERTER": "TRUE",
        "GETTEXTPO_LIBRARY": "C:/Dev/Libs/gettextpo/lib/libgettextpo.lib",
        "GETTEXTPO_INCLUDE_DIR": "C:/Dev/Libs/gettextpo/include",
        "LIBTIDY_LIBRARY": "C:/Dev/Libs/libtidy/lib/libtidy.lib",
        "LIBTIDY_INCLUDE_DIR": "C:/Dev/Libs/libtidy/include",
        "EXIV2_LIBRARY": "C:/Dev/Libs/exiv2-0.28.7/lib/exiv2.lib",
        "EXIV2_INCLUDE_DIR": "C:/Dev/Libs/exiv2-0.28.7/include",
        "exiv2_DIR": "C:/Dev/Libs/exiv2-0.28.7/lib/cmake/exiv2",
        "Qt6LinguistTools_DIR": "C:/Qt/6.7.3/msvc2022_64/lib/cmake/Qt6LinguistTools"
      }
    }
  ],
  "buildPresets": [
  {
    "name": "Debug",
    "configurePreset": "vs2026-multi-config",
    "configuration": "Debug"
  },
  {
    "name": "Release",
    "configurePreset": "vs2026-multi-config",
    "configuration": "Release"
  },
  {
    "name": "RelWithDebInfo",
    "configurePreset": "vs2026-multi-config",
    "configuration": "RelWithDebInfo"
  }
  ]
}

注意：要使用 'Visual Studio 18 2026' 生成器，您需要最新版本的 CMake，即 4.2.0 版本。

打开 Visual Studio 2026。打开 Stellarium 文件夹。

File -> Open -> Folder... 然后选择 <stel_dir>。

VS 2026 将自动检测 CMakePresets.json 文件并开始配置第一个 Preset Configuration，即 'Debug'。要构建 Debug 配置，请从 VS 2026 的顶部菜单选择 Build --> Build All。构建信息应显示在 Build Output 窗口中。如果 Debug 构建成功，您应该会看到在子目录 <stel_dir>\build\src\Debug 中生成了可执行文件 stellarium.exe。

如果 Debug 构建成功，请从 VS 2026 的 Configuration 下拉菜单中选择 CMakePresets.json 文件中指定的下一个配置，在此示例中即 Release。VS 2026 将启动 cmake 来配置构建。配置完成后，要构建 Release 配置，请从 VS 2026 顶部菜单选择 Build --> Build All。如果 Release 构建成功，您应该会看到在子目录 <stel_dir>\build\src\Release 中生成了可执行文件 stellarium.exe。对 RelWithDebInfo 配置重复相同的步骤。如果 RelWithDebInfo 构建成功，您应该会看到在子目录 <stel_dir>\build\src\RelWithDebInfo 中生成了可执行文件 stellarium.exe。

三种配置的构建过程全部完成后，您可以关闭文件夹并打开 Stellarium 解决方案，操作方式与上述 VS 2022 相同。


### 支持的 CMake 参数

支持的参数列表（以 `-DPARAMETER=VALUE` 方式传入）：

| 参数                                | 类型   | 默认值           | 描述
|-------------------------------------| -------|------------------|-----------------------------------------------------
| CMAKE_INSTALL_PREFIX                | path   | *                | 安装 Stellarium 的目标前缀路径
| CMAKE_PREFIX_PATH                   | path   |                  | 查找库文件的附加路径
| CMAKE_BUILD_TYPE                    | string | Release          | Stellarium 的构建类型
| CMAKE_OSX_ARCHITECTURES             | string | arm64;x86_64     | macOS 架构**
| CMAKE_OSX_DEPLOYMENT_TARGET         | string | 11.0             | 最低 macOS 部署版本**
| OPENGL_DEBUG_LOGGING                | bool   | OFF              | 启用通过 GL_KHR_debug 扩展/QOpenGLLogger 记录 OpenGL 信息
| ENABLE_QT6                          | bool   | ON               | 启用构建基于 Qt6 的 Stellarium
| ENABLE_NLS                          | bool   | ON               | 启用界面翻译
| ENABLE_SHOWMYSKY                    | bool   | ON               | 启用 ShowMySky 模块支持，用于实现逼真的大气模型
| ENABLE_SPEECH                       | bool   | ON               | 启用语音输出支持。需要 Qt6.4+ 和 ENABLE_MEDIA
| ENABLE_GPS                          | bool   | ON               | 启用 GPS 支持
| ENABLE_LIBGPS                       | bool   | ON               | 启用基于 libGPS 库的 GPS 支持（Windows 上不适用）
| ENABLE_MEDIA                        | bool   | ON               | 启用声音和视频支持
| ENABLE_XLSX                         | bool   | ON               | 启用 XLSX (Excel) 文件支持
| ENABLE_SCRIPTING                    | bool   | ON               | 启用脚本功能
| ENABLE_RELEASE_BUILD                | bool   | OFF              | 此选项将构建标记为官方发布版本
| ENABLE_TESTING                      | bool   | OFF              | 启用单元测试
| ENABLE_QTWEBENGINE                  | bool   | ON               | 如果已安装，则启用 QtWebEngine 模块支持
| ENABLE_INDI                         | bool   | ON               | 在望远镜控制插件中激活 INDI 客户端支持
| USE_BUNDLED_QTCOMPRESS              | bool   | ON               | 使用捆绑版本的 qtcompress
| USE_PLUGIN_ANGLEMEASURE             | bool   | ON               | 启用角度测量插件构建
| USE_PLUGIN_ARCHAEOLINES             | bool   | ON               | 启用天文考古线插件构建
| USE_PLUGIN_CALENDARS                | bool   | ON               | 启用日历插件构建
| USE_PLUGIN_EQUATIONOFTIME           | bool   | ON               | 启用均时差插件构建
| USE_PLUGIN_EXOPLANETS               | bool   | ON               | 启用系外行星插件构建
| USE_PLUGIN_HELLOSTELMODULE          | bool   | OFF              | 启用 HelloStelModule 插件构建（简单插件示例）
| USE_PLUGIN_LENSDISTORTIONESTIMATOR  | bool   | ON               | 启用镜头畸变估计器插件构建
| USE_PLUGIN_METEORSHOWERS            | bool   | ON               | 启用流星雨插件构建
| USE_PLUGIN_MISSINGSTARS             | bool   | ON               | 启用缺失恒星插件构建
| USE_PLUGIN_MOSAICCAMERA             | bool   | OFF              | 启用马赛克相机插件构建
| USE_PLUGIN_NAVSTARS                 | bool   | ON               | 启用导航星插件构建
| USE_PLUGIN_NOVAE                    | bool   | ON               | 启用亮新星插件构建
| USE_PLUGIN_OBSERVABILITY            | bool   | ON               | 启用可观测性分析插件构建
| USE_PLUGIN_OCULARS                  | bool   | ON               | 启用目镜插件构建
| USE_PLUGIN_OCULUS                   | bool   | OFF              | 启用 Oculus 插件构建（支持 Oculus Rift - 已过时）
| USE_PLUGIN_ONLINEQUERIES            | bool   | ON               | 启用在线查询插件构建
| USE_PLUGIN_POINTERCOORDINATES       | bool   | ON               | 启用指针坐标插件构建
| USE_PLUGIN_PULSARS                  | bool   | ON               | 启用脉冲星插件构建
| USE_PLUGIN_QUASARS                  | bool   | ON               | 启用类星体插件构建
| USE_PLUGIN_REMOTECONTROL            | bool   | ON               | 启用远程控制插件构建
| USE_PLUGIN_REMOTESYNC               | bool   | ON               | 启用远程同步插件构建
| USE_PLUGIN_SATELLITES               | bool   | ON               | 启用卫星插件构建
| USE_PLUGIN_SCENERY3D                | bool   | ON               | 启用 3D 场景插件构建
| USE_PLUGIN_SIMPLEDRAWLINE           | bool   | OFF              | 启用 SimpleDrawLine 插件构建（简单图形插件示例）
| USE_PLUGIN_SKYCULTUREMAKER          | bool   | ON               | 启用星空文化制作器插件构建
| USE_PLUGIN_SOLARSYSTEMEDITOR        | bool   | ON               | 启用太阳系编辑器插件构建
| USE_PLUGIN_SUPERNOVAE               | bool   | ON               | 启用历史超新星插件构建
| USE_PLUGIN_TELESCOPECONTROL         | bool   | ON               | 启用望远镜控制插件构建
| USE_PLUGIN_TEXTUSERINTERFACE        | bool   | ON               | 启用文本用户界面插件构建
| USE_PLUGIN_VTS                      | bool   | OFF              | 启用 Vts 插件构建（允许在 CNES VTS 中将 Stellarium 作为插件使用）

注意事项：
 \* 类 Unix 系统上为 `/usr/local`，Windows 上为 `C:\Program Files` 或 `C:\Program Files (x86)`，
   具体取决于操作系统类型（32 位或 64 位）和构建配置。
 \** macOS 上 Qt6 环境的默认值

## 无需安装即可试运行编译后的程序

编译完成后，您可以在正确的目录中运行程序。

### Linux
假设 Stellarium 源代码位于 DEV/stellarium，构建目录为 DEV/stellarium/build/unix：
```
cd DEV/stellarium
./build/unix/src/stellarium
```

### Windows

大多数用户将使用 QtCreator，它会为调试和发布构建设置自己的路径。在 QtCreator 中使用指定的按钮（绿色箭头）运行应该是可行的。您也可以为 build 目录中 src 子目录下的可执行文件创建一个链接。将此链接移动到源代码目录，并编辑其属性以使其在源代码目录中运行。然后您可以双击此链接，甚至将其放在任务栏中。

## 代码测试

仓库中有多个测试程序。要构建它们，请定义 `-DENABLE_TESTING=ON`（或 `-DENABLE_TESTING=1`），或在 QtCreator 的 Projects 选项卡中配置 cmake。

然后配置一个 Debug 构建并选择一个测试应用程序来执行。

请尝试在提交到主分支之前测试您的更改。我们的自动化 [GitHub Actions](https://github.com/Stellarium/stellarium/actions/workflows/ci.yml) 和 [AppVeyor](https://ci.appveyor.com/project/alex-w/stellarium) 构建将在测试未通过时发出失败信号。

要在终端中执行所有单元测试，请运行：
```
$ make test
```
或
```
$ ctest --output-on-failure
```

## 打包

好的，您已从源代码构建了程序，现在可能想要将可执行文件安装到操作系统或创建用于分发的软件包。

要将可执行文件（以及必要的库和数据文件）安装到参数 `CMAKE_INSTALL_PREFIX` 定义的目录中，请运行：

```
$ sudo make install
```

### Linux 特别说明

要在 Linux 上创建源代码包，您需要运行：
```
$ make package_source
```

要在 Linux 上创建二进制包（TGZ），您需要运行：
```
$ make package
```

构建 TGZ 二进制包后，您还可以创建 DEB 或 RPM 包：
```
$ cpack -G DEB
```
或
```
$ cpack -G RPM
```

### macOS 特别说明

**重要**：每次新构建之前，您应该删除或移开旧的 `Stellarium.app`：
```
$ rm -r Stellarium.app
```

然后构建 macOS 应用程序：
```
$ make install
```

现在您将在构建目录中找到一个带有正确图标的 `Stellarium.app` 应用程序。

在 ARM 架构（Apple Silicon）Mac 上，ARM 应用程序需要进行代码签名。要使用临时签名对应用程序进行签名：
```
codesign --force --deep -s - Stellarium.app
```

要创建 DMG 文件（Apple 磁盘映像），请运行：
```
$ mkdir Stellarium
$ cp -r Stellarium.app Stellarium
$ hdiutil create -format UDZO -srcfolder Stellarium Stellarium.dmg
```

### Windows 特别说明

要创建 Windows 安装程序，您需要安装 [Inno Setup](http://www.jrsoftware.org/)。

如果您已遵循上述所有步骤，当前构建将在 `C:\Devel\stellarium\builds\msvc` 中生成必要的 `stellarium.iss` 文件。

双击打开它，然后从菜单栏选择 "build-compile"。它将构建 Stellarium 安装程序包，并将其放置在 Stellarium 源代码树根目录的 `installers` 文件夹中。因此您可以在 `C:\Devel\stellarium\stellarium\installers` 中找到它。

或者您可以使用 cmake 命令来创建安装程序：
```
$ cmake --build c:\devel\stellarium\build\msvc --target stellarium-installer
```

### 支持的 make 目标

Make 将各种任务分组为"目标"。不带任何参数启动 make 将导致 make 构建默认目标——在我们的情况下，即构建 Stellarium、其测试、本地化文件等。

| 目标           | 描述
|----------------|----------------------------------------------------------------------------------------------------
| install        | 将所有二进制文件和相关文件安装到由 `CMAKE_INSTALL_PREFIX` 确定的目录
| test           | 启动测试可执行文件套件
| apidoc         | 生成 API 文档
| package_source | 创建用于分发的源代码包
| package        | 在 Linux/UNIX 上创建用于分发的二进制包
| installer      | 在 Windows 上创建用于分发的二进制包


### 无 GUI 的 Stellarium

如果您在不需要向观众显示 GUI 对话框或信息文本输出面板的环境中运行 Stellarium，您可以构建一个無界面的版本。这样设计的目的不是直接控制，而是通过 RemoteControl Web 界面或仅通过脚本来进行控制。请注意，即使信息文本输出也需要用户界面，但选中对象的信息将显示在 Web 界面中。要构建这个同时缺少一些依赖 GUI 的插件的轻量版本，请在任意 cmake 运行中使用参数 `STELLARIUM_GUI_MODE="None"`。

这也是尝试替代 GUI 方案的起点。

谢谢！

\- *Stellarium 开发团队*
