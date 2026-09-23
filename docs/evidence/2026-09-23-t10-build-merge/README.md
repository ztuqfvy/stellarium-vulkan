# 2026-09-23｜T10 构建合流证据

**任务**：`stelQuickUI` 由独立工程转为根构建子目标（里程碑 A3 第一步），
使其与 `stellarium` 共用 `libstelMain`（同一份引擎代码），为"真实引擎帧送进 QML 窗口"建立链接前提。

**配置**：`cmake -B build-release -S . -DENABLE_STELQUICKUI=ON -DCMAKE_BUILD_TYPE=Release`（Qt 6.11.2 / Homebrew，Apple M3）

**结论**：**全部通过**。双产物单次配置；旧目标字节级不变；A1 四项 + DYN 7/7 + A2 12/12 + S3 8/8 全过。

| 文件 | 内容 | 判定 |
|---|---|---|
| `t10-configure-off-on.txt` | 三种开关状态的 configure 原始输出：`OFF` / 去掉缓存取默认值 / `ON` | OFF 与默认值下 `stelQuickUI` 目标数 = 0；ON = 11 |
| `t10-build-both.txt` | `cmake --build build-release --target stellarium stelQuickUI` 原始输出 | 一次构建产出两个可执行文件，rc=0 |
| `t10-link-facts.txt` | 引擎符号来源与动态依赖（`nm` / `otool -L`） | 合流版含 `LegacySkyHost`/`FrameMailbox` 符号（来自 `libstelMain.a`，未重复编译）；依赖含 `QtQuick`/`QtQml` |
| `t10-stellarium-invariance.txt` | 开关 OFF / ON 两次构建 `stellarium` 的 sha256 对比 | **两者字节完全一致**（`f860d5a1…`）→ 开关对旧目标零影响 |
| `t10-standalone-regression.txt` | 独立工程形态（`cmake -B build-ui -S src/ui`）重新配置 + 构建 | PASS，形态 ② 未被合流改造破坏 |
| `a2check-metal.txt` | 合流版 `STELQUICK_A2_CHECK=1` + Metal（逐像素 12 探针） | 12/12 PASS，rc=0 |
| `dyncheck-metal-merged-rerun.txt` | 合流版 `STELQUICK_DYN_CHECK=1` + Metal | 7/7 PASS，rc=0（生产者 54.2 fps） |
| `dyncheck-metal-standalone-ab.txt` | 独立工程版同一自检（同负载 A/B 对照） | 7/7 PASS，rc=0（54.8 fps） |
| `dyncheck-metal-INVALID-contended.txt` | 合流版首跑 | **INVALID**（rc=8）：紧跟 `-j8` 构建后跑，系统 load≈10（WindowServer 38% + IDE 渲染 30%） |
| `a1-1-vulkan-default.txt` | A1 第 1 项：默认后端自跑 | `runtimeApi=Vulkan`，rc=0 |
| `a1-2-vulkan-disabled-rc3.txt` | A1 第 2 项负控：Vulkan 加载库指向不存在路径 | 明确报错 + **rc=3**（不静默回退） |
| `a1-3-window-test.txt` | A1 第 3 项：`STELQUICK_WINDOW_TEST=1` | steps=54 / maxStall=41ms / VERDICT=PASS，rc=0 |
| `a1-4-no-env-vars.txt` | A1 第 4 项：清空 `QT_VULKAN_LIB`/`VK_DRIVER_FILES` | 自动定位加载库，`runtimeApi=Vulkan`，rc=0 |
| `stela3check-merged.txt` | 合流后 `stellarium` 的 `STELA3_CHECK=1`（S3 引擎集成自检） | 8/8 PASS，rc=0 |

## 一处需要解释的差异

`dyncheck-metal-INVALID-contended.txt` 与 `dyncheck-metal-merged-rerun.txt` 同为合流版，
前者 D1-C02 红、后者全绿。**原因不是合流退化**，而是测量环境：

- 首跑紧接 `-j8` 全量链接之后，`uptime` 显示 load average ≈ 10（8 核机器），
  生产者速率掉到 47.0 fps（对照 54.2 fps），显示帧号推进 151（下限 192）；
- 同一负载下立刻跑独立工程版对照（`...-standalone-ab.txt`）也拿到 54.8 fps，
  说明"先降后升"是负载回落而非构建形态差异；
- 复跑合流版即全绿。**环境异常判 INVALID，不计入结论**（沿用 §6.1.1 协议第 5 条的同一纪律）。

## 复现命令

```bash
cd /Users/ztuqfvy/qt_demo/stellarium_vulkan
cmake -B build-release -S . -DENABLE_STELQUICKUI=ON
cmake --build build-release --target stellarium stelQuickUI -j 8

export VK_DRIVER_FILES=/opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json
export QT_VULKAN_LIB=/opt/homebrew/opt/vulkan-loader/lib/libvulkan.1.dylib
STELQUICK_GRAPHICS_API=metal STELQUICK_A2_CHECK=1 ./build-release/src/ui/stelQuickUI.app/Contents/MacOS/stelQuickUI
STELQUICK_GRAPHICS_API=metal STELQUICK_DYN_CHECK=1 ./build-release/src/ui/stelQuickUI.app/Contents/MacOS/stelQuickUI
STELA3_CHECK=1 ./build-release/src/stellarium
```
