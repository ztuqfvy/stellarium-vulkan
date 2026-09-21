# Windows 构建与跨驱动形态对照（WINDOWS_BUILD）

> 目标平台：AMD Ryzen 7 3700X + NVIDIA GeForce RTX 4060（Windows 10/11 x64）
> 目的：用**平台原生 Vulkan 驱动**替换 macOS 的 MoltenVK 转译层，把 A2 的
> 图像纹理黑屏问题收敛到单一自变量上。

---

## 0. 先说清楚这次要回答什么（否则跑完不知道看什么）

**唯一问题：Qt 6.11.2 的 Vulkan RHI 在"静态图像纹理"路径
（`QSGTextureMaterial` / `QSGImageNode` / `createTextureFromImage`）上，
是否只在 MoltenVK 下失效？**

三种结果，三种后续动作——**先认下这个对照表，再动手**：

| Windows 上 `STELQUICK_A2_CHECK=1` 的结果 | 结论 | 后续动作 |
|---|---|---|
| 12 探针 **0 失败** | 缺陷在 MoltenVK 侧（或 Qt 的 MoltenVK 特化路径） | 向 Qt 报 bug（本机已有逐像素证据 + Windows 反证）；A2 动态帧开发不受阻，继续在 macOS 上推进 |
| **仍全黑（12/12）** | 缺陷在 Qt Vulkan RHI 的纹理路径本身，与驱动形态无关 | 问题升级，不再是"Mac 特有的坑"；评估 Qt 版本回退 / 换 `QSGSimpleTextureNode` 等替代路径 |
| 部分对 / 花屏 / 位置错 | 驱动差异导致的次生问题 | 逐探针看是坐标映射、通道序还是采样问题——日志里每条探针都有期望/实际/偏差 |

**结论不看"窗口看起来正不正常"**。看 stdout 的 `A2CHECK:` 行，那是逐像素证据。

---

## 1. 范围边界（重要：别把根构建一起拖进来）

只编 `src/ui` 这个**独立工程**（`stelQuickUI_SOURCES`，15 个文件）。

**不要**在 Windows 上尝试 Stellarium 根构建（`cmake -B build`）。根构建要
Qt6 WebEngine / Charts / MultiMedia、libnova、gettext、FFmpeg 等一大堆依赖，
Windows 上首次配通是另一个量级的任务，**和本次要回答的问题毫无关系**。

---

## 2. 软件清单

| 组件 | 版本要求 | 说明 |
|---|---|---|
| Visual Studio 2022 | Community 即可 | 安装时勾选 **"使用 C++ 的桌面开发"**（含 MSVC v143 + Windows SDK + CMake） |
| Qt | **6.11.2**，`msvc2022_64` | 在线安装器（需免费 Qt 账号）。勾选 Qt Quick / Quick Controls |
| CMake | ≥ 3.21 | VS2022 自带的够用；也可单独装 |
| LunarG Vulkan SDK | 最新即可，**推荐装** | 提供 `vulkaninfo` / `vkvia`，是"驱动到底受不受支持"的直接证据；也是 `find_package(Vulkan)` 能找到包的前提 |
| Git for Windows | 任意 | 取代码用 |

### 版本对齐纪律（和 macOS 侧是同一条）

**Qt 必须严格 6.11.2。** 用 6.11.1 或 6.12 会在"驱动形态"之外**多引入一个自变量**，
结论就没法归因了——而这次跨平台对照的全部价值就在于自变量只有一个。

检查方法：在线安装器里展开 `Qt → 6.11.2 → MSVC 2022 64-bit`，确认版本号。

---

## 3. 步骤

### 3.1 取代码

```powershell
git clone <仓库地址> stellarium_vulkan
cd stellarium_vulkan
```

（若用 `git bundle` 传输：`git clone stellarium_vulkan.bundle stellarium_vulkan`）

`build-release/`、`build-ui/` 等构建目录已被 `.gitignore` 排除，不会带过来——这是对的，
构建目录跨平台不可复用，必须在 Windows 上重新生成。

### 3.2 配置与构建

在 **"x64 Native Tools Command Prompt for VS 2022"** 里执行（普通 PowerShell 也行，
只要 `cmake` 在 PATH 且能找到 MSVC）：

```powershell
cmake -B build-ui -S src/ui `
  -DCMAKE_PREFIX_PATH=C:/Qt/6.11.2/msvc2022_64 `
  -G "Visual Studio 17 2022" -A x64

cmake --build build-ui --config Release --parallel
```

产物：`build-ui\Release\stelQuickUI.exe`

首次配置时**留意两类输出**：

- `-- Found Vulkan: ...` → 探针会编进去（有 SDK 时）
- `CMake Warning ... 未找到 Vulkan 开发包 ... 本构建关闭 VkDeviceProbe` →
  能跑，但诊断页缺设备名/驱动版本。想要完整诊断数据就装 Vulkan SDK。

### 3.3 独立分发（可选，只在需要拷到别的机器时做）

```powershell
cmake --build build-ui --config Release --target deploy
```

产出 `build-ui\deploy\`（含 Qt 运行时 DLL + QML 模块，双击可运行）。

---

## 4. 首次运行：按顺序做这三步验证

**顺序不能跳**，每一步为下一步排掉一类可能。

### 第 1 步：确认这个 Qt 编了 Vulkan 支持

```powershell
& "C:\Qt\6.11.2\msvc2022_64\bin\qtdiag.exe" | Select-String -Pattern "Vulkan"
```

- 有 Vulkan 条目 → 继续。
- 没有 → **停下来**。说明这份 Qt 没编 Vulkan，得换安装包或自己编 Qt，
  后面两步都不用做了。（macOS 上正是栽在这一点：官方二进制无 Vulkan，只有 Homebrew 版有。）

### 第 2 步：确认后端真跑在 Vulkan 上

```powershell
$env:STELQUICK_AUTOTEST_SECONDS = "5"
.\build-ui\Release\stelQuickUI.exe
```

期望输出（注意 `portability_driver=0` —— 原生驱动。macOS 上这行是 `portability_driver=1`）：

```
STELQUICK: probe ok=1 device=NVIDIA GeForce RTX 4060 api=1.4.x driver=79.x.x portability_driver=0 portability_enum_ext=1 err=
STELQUICK: runtimeApi=Vulkan backendOk=1 device=NVIDIA GeForce RTX 4060（判定来源=信号/兜底）
```

判定：`runtimeApi=Vulkan` 且 `backendOk=1` 且 `portability_driver=0`。

字段含义（2026-09-21 拆分，避免误判）：
- `portability_driver` = **设备级** `VK_KHR_portability_subset` → 真正的驱动形态判据。
  1 说明走了转译层（不该出现在 Windows 原生驱动上），先查是不是装错了驱动。
- `portability_enum_ext` = 实例级 `VK_KHR_portability_enumeration` → 只是 loader 能力，
  **Windows 上 1 是正常的**（Vulkan 1.4.357 loader 恒定提供），不代表转译层。
- 退出码 `3` → 请求 Vulkan 但拿不到（`QVulkanInstance::create()` 失败）。
  查显卡驱动是否为 NVIDIA 官方驱动（不要用 Windows 自带的"基本显示适配器"）。
  另注：若 ICD 枚举被软件光栅器（Vulkan SDK 自带的 SwiftShader/lavapipe）抢到，
  设备名会不对，此时用 `VK_ICD_FILENAMES` 指定 NVIDIA ICD 再跑。
- `runtimeApi` 不是 Vulkan → 后端回退判定生效，禁止继续，先解决这个。

顺便：诊断页（默认起始页）会显示"驱动类型"一行，Windows 上应当显示
**"平台原生驱动（windows）"**——这是"驱动形态"这个自变量的显式记录，抄下来。

### 第 3 步：核心对照——逐像素校验

```powershell
$env:STELQUICK_A2_CHECK = "1"
$env:STELQUICK_A2_DUMP  = "C:\temp\a2_vulkan.png"    # 抓帧存盘，留证据
.\build-ui\Release\stelQuickUI.exe
```

**典型全对输出**（这就是"Vulkan 在 Windows 正常"的样子）：

```
A2CHECK: 投递测试图案 逻辑=1140x600 DPR=1.00 物理=1140x600 → 成功
A2CHECK: 抓帧尺寸=1140x600 DPR=1 视口场景原点=(0,72) 视口逻辑尺寸=1140x600 纹理物理尺寸=1140x600
A2CHECK:   [PASS] 左上角标(红) 纹理点(24,24) 抓帧点(24,96) 期望(255,0,0) 实际(255,0,0) 偏差0
...（12 条）
A2CHECK: VERDICT=PASS
```

**macOS 现有故障在 Windows 上复现的样子**：

```
A2CHECK:   [FAIL] 左上角标(红) 纹理点(24,24) 抓帧点(24,96) 期望(255,0,0) 实际(0,0,0) 偏差255
A2CHECK: 探针 12 项，失败 12 项
A2CHECK: VERDICT=FAIL
```

**再跑一组对照组**（同代码，换成 Qt 在 Windows 的默认后端 D3D11）：

```powershell
$env:STELQUICK_GRAPHICS_API = "d3d11"
$env:STELQUICK_A2_DUMP = "C:\temp\a2_d3d11.png"
.\build-ui\Release\stelQuickUI.exe
```

对照组的用处：如果 Vulkan 全对而 D3D11 也全对 → 说明测试图案/坐标换算/抓帧机制本身没问题，
Vulkan 的 PASS 是可信的。**若 D3D11 反而失败，那问题在测量工具，不在被测对象。**

### 退出码速查

| 码 | 含义 |
|---|---|
| 0 | 通过 |
| 2 | 窗口创建失败 |
| 3 | 后端校验失败（实际 API 非 Vulkan） |
| 4 | 交互自测失败（`STELQUICK_WINDOW_TEST`） |
| 5 | A2 静态图校验失败（探针有失败项） |
| 6 | **校验手段不可用**（`grabWindow()` 返回空图）——不得据此声称通过 |
| 7 | **被测 exe 不存在**（2026-09-21 加）——此前这种情况静默返回 0，即"什么都没跑却报绿灯" |
| 8 | **T6 显式帧驱动自检失败**（`STELQUICK_LEGACY_HOST_TEST=1`，2026-09-21 加）。该项**不需要窗口、不依赖 Vulkan**，在创建任何窗口之前同步执行 |

---

## 5. 建议跑全的矩阵（约 5 分钟）

| # | 命令 | 期望 | 这一格在证明什么 |
|---|---|---|---|
| 1 | `$env:STELQUICK_A2_CHECK="1"` | 见第 4 节 | **主结论** |
| 2 | `+ $env:STELQUICK_GRAPHICS_API="d3d11"` | PASS | 对照组：测量工具可信 |
| 3 | `+ $env:STELQUICK_GRAPHICS_API="opengl"` | PASS | 第二对照组（跨平台可比，macOS 上也是 PASS） |
| 4 | `$env:STELQUICK_WINDOW_TEST="1"` | `VERDICT=PASS`，`maxStallMs` 个位数~几十 | Windows 上无 macOS 那个 5 秒显示锁问题（预期不需要 `QT_MTL_NO_TRANSACTION`） |
| 5 | `$env:STELQUICK_GRAB_AT_SECONDS="4"` + `STELQUICK_GRAB_PATH` | PNG 落盘 | 人眼留证，顺手看一眼图案长相 |

---

## 6. 结果回传格式（照抄即可）

把下面这些**原始 stdout 行**发回来（不要只发"能跑"/"不行"）：

```
[环境]
Windows 版本 / NVIDIA 驱动版本 / Qt 版本（在线安装器显示） / 是否装了 Vulkan SDK
诊断页"驱动类型"一行：(  )

[1] 默认 Vulkan + A2_CHECK
A2CHECK: 投递测试图案 ...（原文）
A2CHECK: 探针 N 项，失败 M 项
A2CHECK: VERDICT=...
退出码：...

[2] d3d11 对照
A2CHECK: VERDICT=...

[3] opengl 对照
A2CHECK: VERDICT=...

[4] WINDOWTEST
WINDOWTEST: 结果 ...

[5] 抓帧 PNG：<路径>
```

如有失败项，附上**完整 12 条 `[FAIL]` 明细**（探针名 + 期望 + 实际 + 偏差）——
偏差值本身就能区分"全黑"（偏差 255 且实际恒为 0,0,0）和"位置错"（实际是别的探针的颜色）。

---

## 7. Windows 侧已知坑（代码里已处置的 + 需要人工确认的）

### 已在本仓库代码中修复（2026-09-20 移植准备）

1. **控制台子系统**：`qt_add_executable` 默认按 GUI 应用处理（`WIN32_EXECUTABLE=TRUE`），
   进程不挂父控制台 → 所有 `printf` 诊断和退出码全部丢失，自动化验收直接变瞎。
   已在 `src/ui/CMakeLists.txt` 强制 `WIN32_EXECUTABLE FALSE`。
2. **UTF-8 无 BOM 源文件**：源码含大量中文注释且为 UTF-8 无 BOM，MSVC 默认按系统代码页
   （中文 Windows = GBK/936）解释 → C4819 警告、注释串码，严重时字符串字面量被多字节截断，
   诊断行变乱码。已加 MSVC `/utf-8`。
3. **portability 扩展**：`VkDeviceProbe` 原本无条件启用
   `VK_KHR_portability_enumeration`。Windows 原生 NVIDIA 驱动不提供该扩展，
   `vkCreateInstance` 会返回 `VK_ERROR_EXTENSION_NOT_PRESENT(-7)` → 出现
   "探针报失败、Qt 渲染却正常"的假故障。已改为**先枚举、存在才启用**，
   并把结果写入诊断页的"驱动类型"行。
4. **后端开关补 D3D11/D3D12**：原先只认 `metal|opengl`，Windows 上拿不到最贴近
   平台基线的对照组。已补 `d3d11` / `d3d12`（D3D12 需要 `QSGRendererInterface::Direct3D12`）。
5. **Vulkan SDK 缺失不再致命**：`find_package(Vulkan REQUIRED)` 改为 `QUIET` + 自动降级
   （关掉探针，渲染不受影响），并给告警说明。

### 需要人工确认 / 可能踩的

6. **显示器缩放**：Windows 常见 125%/150%。DPR 为小数时探针坐标会有 ±1px 往返误差——
   本项目的探针都取在色块**中心**（距离最近的色块边界 ≥ 十几像素），所以 ±1px 打不到边界，
   数学上是安全的。但**若真出现个别探针偏差落在 8~60 之间的"疑似边界污染"**，
   先切到 100% 缩放重跑一次排除这一项，再下结论。
7. **HDR / 10-bit 显示**：可能改变 `grabWindow()` 回读的色彩空间，让纯色探针偏差变大。
   做校验时**关掉 HDR**。
8. **不要最小化窗口**：`grabWindow()` 在窗口不可见时可能返回空图（退出码 6）。
   让窗口正常显示在桌面上跑。
9. **路径含空格/中文**：本次独立工程文件数少，一般没事；但注意构建目录别放在
   类似 `C:\Users\张三\我的文档\...` 这种长中文路径下（MSVC 偶发路径长度问题）。
10. **不要在 Windows 上用 `STELQUICK_GRAPHICS_API=metal`**——没有该后端，
    未知名字会被当成 Vulkan 并打印提示。
11. **不需要任何环境变量**：`QT_VULKAN_LIB` / `VK_DRIVER_FILES` 是 macOS 专有的
    （main.cpp 里被 `#ifdef Q_OS_MACOS` 包住，Windows 上是空操作）。Windows 的
    `vulkan-1.dll` 由 NVIDIA 驱动装进 `System32`，Qt 直接自己找得到。

---

## 8. 这个方案值不值（结论）

**值，但要按"诊断实验"来跑，不是按"看看效果"来跑。**

- 拿到的不是"Windows 上能不能跑"，而是**一个能定性的反证**：
  Qt 的 Vulkan 纹理路径在原生驱动下到底行不行。这直接决定下一步是
  "报 Qt bug 然后继续开发"还是"换渲染路径"。
- 成本：一台机器上一次性的 Qt 环境（约 1~2 GB 下载）。
- **不要**顺手去搞根构建、鸿蒙适配、打包分发——那些都会引入新变量，
  把这次的结论搅浑。

跑完之后，无论结果如何，都把第 6 节的原始输出贴回来，本机对照写进
`docs/BUILD_RECORD.zh_CN.md`。

---

## 9. 验收纪律：环境变量白名单（2026-09-21 补）

**验收只允许在干净环境（无任何 STELQUICK_* 变量）下跑。** 例外仅限下表左侧三个。

| 变量 | 验收可用 | 作用 / 为什么限制 |
|---|---|---|
| `STELQUICK_A2_CHECK` | ✅ 必需 | 开启 12 探针逐像素校验 |
| `STELQUICK_GRAPHICS_API` | ✅ 必需 | 选后端（vulkan/d3d11/opengl） |
| `STELQUICK_A2_DUMP` | ✅ 可选 | 存盘抓帧 PNG 供人工复核 |
| `STELQUICK_A2_IGNORE_SGWAIT` | ❌ **禁用** | 跳过"等场景图初始化"。调试用逃生门；设置它等于让校验在场景图未就绪时硬跑，结论不可信 |
| `STELQUICK_A2_TRACE` | ⚪ 允许（不影响判定） | 追加 SGWAIT/上传时序跟踪到 stderr，仅诊断 |
| `STELQUICK_RENDER_WORKAROUND=basic-loop` | ❌ **禁用** | 已知与 A2 校验不兼容（校验序列不退出） |
| `STELQUICK_PATTERN_DUMP` | ⚪ 允许（不影响判定） | 给 QML 对照 Image 注入路径 |

配套：用仓库根的 `run_autotest.cmd matrix` 一次性跑三后端（已修退出码聚合；
旧版无论成败都报 `EXIT CODE = 0`，若你手上有旧版输出，那个 0 不可作证据）。
证据留存要求：把 `run_autotest.cmd matrix` 的**完整 stdout 原始文本**存盘并随提交入库
（例如 `docs/evidence/` 下带日期文件名），不要只写结论矩阵——本次复核已发现
工具本身的缺陷，"结论有据"必须落到原始输出上。

### 9.1 采集与留证的配套工具

| 工具 | 用途 |
|---|---|
| `tools/evidence/collect.ps1` | **规范生成器**：一次产出完整证据文件（元数据头 + matrix 原始输出 + 负控原始输出 + 结论段），跑前拒绝验收禁用变量，并对两段做结构自检、在文件头第一行盖 `VERDICT`。组装路径已在 macOS 用 PowerShell 7.6.6 实测（4 个用例）；仅 `chcp` 交互与两条 `cmd /c` 捕获行无法离机验证，首次真机运行当调试 |
| `run_evidence_matrix.cmd <输出文件>` | 清空所有 `STELQUICK_*` 后跑真实 matrix，把 stdout+stderr 原样落盘、并追加真实返回码。用 `< nul` 绕开 `run_autotest.cmd` 结尾的 `pause`，避免自动化挂住 |
| `tools/negctl/run_negctl.cmd` | 退出码聚合的**负控实验**：注入 `d3d11 -> 5`，期望聚合报出 `EXIT CODE = 5`。用来证明绿灯不是无条件默认值 |
| `docs/evidence/README.md` | 采集方式、编码坑、.cmd 硬规则的完整说明 |

### 9.2 两条硬规则（都踩过坑）

1. **`.cmd` 文件一律只用 ASCII 注释。**
   `cmd.exe` 按 OEM 代码页（简中系统 936/GBK）逐行解析 `.cmd`，**中文写在 `rem`
   注释里同样不安全**：误解码字节可能跨行拼接，残余片段会被当作命令执行，
   往 stdout 灌 `'xxx' is not recognized as an internal or external command`。
   校验：`python -c "b=open('<file>.cmd','rb').read(); print(sum(1 for x in b if x>=0x80))"`
   必须为 `0`。
2. **不要在捕获作用域内调用 `chcp`。**
   `chcp` 会让 `cmd.exe` 打印本地化版权横幅（CP936 字节），把两种编码混进本应
   纯 UTF-8 的流，使证据文件无法用单一编码解码。代码页由**父进程**预先设好：

   ```powershell
   [Console]::OutputEncoding = [System.Text.Encoding]::UTF8
   chcp 65001 | Out-Null
   .\run_evidence_matrix.cmd docs\evidence\YYYY-MM-DD-run_autotest-matrix-<范围>.txt
   ```

### 9.3 留证只需一趟（照做即可）

```powershell
git pull                                       # 取含 2026-09-21 生成器修正的代码
cmake --build build-ui --config Release        # deploy 目标可选：生成器会在 deploy/Release/Debug 里自动找 exe
powershell -ExecutionPolicy Bypass -File tools\evidence\collect.ps1
```

判读顺序**不能调换**：

1. **先看文件头第一行 `VERDICT`。** 必须是 `COMPLETE`。
   若是 `NOT USABLE AS EVIDENCE`（脚本退出码 `1`），这份文件**不是证据**——
   把完整报错贴回来，不要尝试解读 SEGMENT 1。
2. 只在 `COMPLETE` 时才看 `dut result` 行（被测程序自己的返回码）与 SEGMENT 1 的逐探针明细。
   记住二者的区别：**`VERDICT` 说明"这份记录可不可信"，`dut result` 才是被测程序的结论。**
   被测程序失败但记录完整时，`VERDICT` 仍是 `COMPLETE`、退出码仍是 `0`——那份红色结果
   同样是有效结论。
3. `worktree: dirty` 必须是 `no`。若为 `YES`，说明被测代码与提交不一致（告警行会给出
   差异路径数），结论无法归因——先提交或还原改动再重跑。
   （`docs/evidence/` 已被显式排除：生成器自己的产物不该触发这个告警。）
4. 入库：

```powershell
git add docs\evidence\<文件> ; git commit -m "验收留证：<日期> 三后端 matrix 原始输出" ; git push
```

> ~~首次运行请当调试~~ **已于 2026-09-21 在真机（Windows 10 19045）跑通**：生成器里
> 两条捕获行与 `chcp` 的交互均正常，产物 UTF-8 无 BOM、可用单一编码解码、0 行污染。
> 唯一残留：`run_autotest.cmd` 结尾 `pause` 的提示语 `Press any key to continue . . .`
> 被原样一并捕获。那属**真实原始输出**，不影响任何判据，故保留不做修饰。

### 9.4 别混淆返回码：链上一共有三个数字（2026-09-21 真机首跑后补）

| 数字 | 出现在哪 | 含义 |
|---|---|---|
| `collect.ps1` 的退出码 | 终端（`0` / `1`） | 这份**记录**可不可信。`1` = 没跑完，不得引用 |
| `dut result` 行 | 证据文件头 | 被测程序的结论。取自 `run_autotest.cmd` 的**进程返回码** |
| `EXIT CODE = N` | SEGMENT 1 正文 | 同一个结论，被测程序自己**打印**出来的聚合码 |

真机首跑实测发现：第三个数早已修好，**第二个仍是恒 `0`**。`run_autotest.cmd` 走到
`:end` 之后是 `echo.` → `pause` → `endlocal` → 文件结束，**没有把 RC 作为进程返回码
带出去**，而那个 `echo.` 已经把 `ERRORLEVEL` 重置为 0。实测对照（桩 DUT 固定返回 2）：

| | 脚本打印 | 进程返回码 |
|---|---|---|
| 修复前（`838b1b7`） | `EXIT CODE = 2` | `0` ← 红被吞成绿 |
| 修复后（`:end` 收尾改为 `endlocal & exit /b %RC%`） | `EXIT CODE = 2` | `2` ✓ |

后果：`collect.ps1` 的 `dut result` 行**永远不会变红**——比 `d717056` 修的"matrix 恒报 0"
高一层。它之所以一直没被发现，是因为 `tools/negctl/run_negctl.cmd` 复刻逻辑时**自己带了
`exit /b`**：负控能红、被测程序不能红，两者不对称，于是掩盖了缺陷。

回归检查：`tools\negctl\check_rc_propagation.cmd`。它跑的是**真的** `run_autotest.cmd`
（不是副本），桩 DUT 返回 2，断言"打印码 == 进程返回码"。对修复前的副本实测
`RESULT: FAIL - printed=2 process=0 / exit 1`，对修复后实测 `PASS / exit 0`。
