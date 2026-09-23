# 证据：A3 前置探针 —— QApplication 承载 QQuickWindow + 引擎无头引导同进程共存

**日期**：2026-09-23
**目的**：消解进度实况 §8.6 记录的未知数「QApplication × QQuickWindow 混用」。
**结论**：**未知数成立且是 T11 的硬前置；已做掉，共存成立。**
**对应提交**：T10 之后（`8869732` 的下一个提交）

---

## 1. 为什么这不是"多虑"

静态排查得到的事实链：

| 事实 | 出处 |
|---|---|
| 引擎现成的无头引导路径是 `new StelMainView(conf)` + `show()` + `WA_DontShowOnScreen` → `initializeGL` → `StelApp::init` | `src/render/legacy/LegacyAppCheck.cpp` A3-C01 |
| `StelMainView` **是 Widgets 类** | `src/StelMainView.hpp:46` `class StelMainView : public QGraphicsView` |
| `StelMainView::init()` 里 `gui = new StelGui()`（完整 Widgets 版，含 `QGraphicsTextItem`） | `src/StelMainView.cpp:947` |
| `stelQuickUI` 一直是 `QGuiApplication`，且**只链 `Qt6::Quick/Qml/Gui/Core/OpenGL`（无 Widgets）** | `src/ui/main.cpp` / `src/ui/CMakeLists.txt` |

`QGuiApplication` 下创建任何 `QWidget` 会 abort。也就是说：**"把渲染回调换成
`StelApp::update()/draw()`"这句 T11 的任务描述，在当时的宿主形态下根本无法执行**——
不是行为漂移，是进程直接死。

## 2. 两条判据，两次实验

### 阶段一：`QApplication` 承载 `QQuickWindow` 是否零退化

`QApplication` 是 `QGuiApplication` 的严格超集，理论上安全；但 `stelQuickUI` 历史上
从未在这个宿主下跑过，不能靠推理。

开关：`-DSTELQUICKUI_WIDGETS_HOST=ON`（默认 OFF，不扰动既有基线）。

| 自检 | 默认（`QGuiApplication`） | 探针（`QApplication`） |
|---|---|---|
| A2 逐像素 12/12 | `11-…` 见 T10 | ✅ PASS rc=0（`30-a2-widgets-host.txt`） |
| DYN 动态帧 7/7 | 54.8 fps（T10 基线） | ✅ PASS rc=0（`31-dyn-widgets-host.txt`） |
| 默认形态回归 | — | ✅ OFF 形态 A2 PASS / DYN 54.8 fps（`33-`/`34-`） |

**零退化。**

### 阶段二：引擎无头引导与 QML 窗口同进程

新增探针 `STELQUICK_ENGINE_COEXIST=1`：在**已经跑起来的 `QQuickWindow` 旁边**
引导同一份引擎（同一进程、同一份 `stelMain` 符号）。

| 判据 | 结果 |
|---|---|
| C-01 引擎无头初始化（路径与 A3-C01 相同，但 QML 窗口同时存活） | ✅ 成功，耗时 0ms（`show()` 内同步完成） |
| C-02 引擎 GL 形态 | ✅ `version=4.1 core=1 renderer="Apple M3"` |
| C-03 引导后 QML 窗口仍能出帧 | ✅ Metal：+179 帧/3s；Vulkan：+178 帧/3s（引导前基线 82~83 帧/1.5s） |
| C-04 引擎显式帧驱动 `update/draw` ×4 | ✅ 未抛异常（39~43ms） |

两大后端（Metal / Vulkan）均 `VERDICT=PASS` rc=0。

---

## 3. 过程中挖出的两个真问题

### 3.1 静态库里的 qrc 不会自动注册 → `qFatal` → SIGABRT(134)

首次运行崩在 `AtmospherePreetham.cpp:52 qFatal("Failed to open atmosphere
vertex shader source")`，而引擎的星表 / DSO / LandscapeMgr **全部加载成功**——
说明引导路径本身是通的，缺的是资源。

根因：`data/mainRes.qrc`、`data/gui/guiRes.qrc` 由 `QT_ADD_RESOURCES` 编成
`qrc_mainRes.cpp` / `qrc_guiRes.cpp` 并加入 `libstelMain.a`（`src/CMakeLists.txt:601/613`）。
**静态库中的 qrc 对象文件没有任何被引用的符号**，在按需拉取的链接模型下不会被装进
可执行文件 → `qInitResources_*` 从不执行 → `:/shaders/...` 全部读不到。

`stellarium` 主目标自己编了这两个 qrc，所以从没暴露；只有**链接静态库的新目标**才会踩。

处置：`src/ui/main.cpp` 的 `main()` 开头显式
```cpp
Q_INIT_RESOURCE(mainRes);
Q_INIT_RESOURCE(guiRes);
```

### 3.2 C-03 判据初版是错的（会把"按需渲染"误判为"窗口被拖死"）

初版只数 `frameSwapped`，得到「引导后 +0 帧 / 3s」→ 假红。
真实原因：**Qt Quick 是按需渲染**，静态页面渲完就停，没有新的事件请求重绘就永远不再产帧。

修正：两侧都改为**主动 `window->update()` 请求重绘**后取帧，判据才有可比性
（引导前 82 帧/1.5s vs 引导后 179 帧/3s → 零退化）。

> 这条对 T11 有直接影响：合流后的长跑**不能靠"窗口自己会出帧"来度量**，
> 必须由驱动方显式请求重绘，或由仿真时钟持续标记脏区。

### 3.3 退出路径 SIGSEGV(139) → `_exit` 交付退出码

探针把引擎与 QML 两套完整栈拉进同一进程，正常 `return` 会撞上静态对象析构顺序
（`StelApp`/`StelMainView` 与 `QQuickWindow` 各自的 GL/Metal 上下文谁先销毁）。
实测退出码被信号覆盖成 139。处置：`fflush` 后用 `_exit(rc)` 直接交付判据退出码。

### 3.4 `-runEngineCoexistProbe` 的前置：`installDir = "."` 依赖 cwd

`COEXIST: 安装目录 = .` —— `StelFileMgr` 在 stelQuickUI 的
`applicationDirPath`（`build-release/src/ui/stelQuickUI.app/Contents/MacOS`）附近
找不到 `CHECK_FILE`，最终命中 `STELLARIUM_DATA_ROOT` 默认值 `.`。
星表日志显示用的是相对路径 `./stars/...`。

⇒ **stelQuickUI 当前必须从仓库根目录启动**，否则找不到 `data/`。
`stellarium` 主目标无此限制（它在 `build-release/src` 下能上溯到仓库根）。
这是 T11 要处理的基础设施问题，不是本探针的结论范围。

---

## 4. 文件索引

| 文件 | 说明 |
|---|---|
| `01-configure.txt` | `-DSTELQUICKUI_WIDGETS_HOST=ON` 配置输出 + 缓存值确认 |
| `02-build.txt` | 首次构建（QtWidgets 已链入） |
| `03-build-with-probe.txt` | 含探针的构建输出 |
| `10-a2check-qapplication.txt` | 阶段一：A2 逐像素（`QApplication` 宿主）12/12 |
| `11-dyncheck-qapplication.txt` | 阶段一：DYN 7/7（54.9 fps） |
| `20-coexist-metal-FAIL-missing-qrc.txt` | **阶段二首跑崩溃现场**：`qFatal` 缺 shader 资源，rc=134 |
| `20-coexist-metal.txt` | **阶段二 PASS**：Metal 后端 4/4 判据 |
| `21-coexist-vulkan.txt` | **阶段二 PASS**：Vulkan 后端 4/4 判据 |
| `30-a2-widgets-host.txt` | Widgets 宿主形态 A2 回归 |
| `31-dyn-widgets-host.txt` | Widgets 宿主形态 DYN 回归（53.4 fps） |
| `32-build-default-off.txt` | 切回默认（Widgets OFF）构建 |
| `33-a2-default-off.txt` / `34-dyn-default-off.txt` | 默认形态 A2/DYN 回归（54.8 fps） |
| `35-negctl-probe-absent.txt` | **负控**：默认形态完全忽略 `STELQUICK_ENGINE_COEXIST`（0 行 COEXIST，rc=0） |
| `40-stellarium-invariance-recheck.txt` | `stellarium` 字节不变性复查 + 与 T10 基线差异的解释 |

## 5. 复现命令

```bash
cd <repo-root>                                    # 必须：installDir 依赖 cwd
export VK_DRIVER_FILES=/opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json
export QT_VULKAN_LIB=/opt/homebrew/opt/vulkan-loader/lib/libvulkan.1.dylib

/opt/homebrew/bin/cmake -B build-release -S . \
    -DENABLE_STELQUICKUI=ON -DSTELQUICKUI_WIDGETS_HOST=ON
/opt/homebrew/bin/cmake --build build-release --target stelQuickUI -j 6

EXE=build-release/src/ui/stelQuickUI.app/Contents/MacOS/stelQuickUI
STELQUICK_ENGINE_COEXIST=1 $EXE          # 默认 Vulkan 后端
STELQUICK_GRAPHICS_API=metal STELQUICK_ENGINE_COEXIST=1 $EXE
```

退出码：`0` = 全部判据通过；`8` = 存在失败项。
