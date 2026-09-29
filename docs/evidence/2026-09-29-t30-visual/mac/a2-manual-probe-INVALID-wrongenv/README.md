# ⚠️ INVALID —— 这三次 A2 探针是**环境错配**，不作为任何结论的依据

## 这三份是什么

T30 验证期间（`2026-09-29 15:0x`），操作者手工重探 A2 逐像素，命令是：

```sh
STELQUICK_A2_CHECK=1 ./build-release/src/ui/stelQuickUI.app/Contents/MacOS/stelQuickUI
```

**漏了验收口径的三件套**（`tools/t30-verify.sh:54-56` 里有，但手工命令没带）：

```sh
export VK_DRIVER_FILES=/opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json
export QT_VULKAN_LIB=/opt/homebrew/opt/vulkan-loader/lib/libvulkan.1.dylib
export STELQUICK_GRAPHICS_API=metal
```

后果：程序落到 **Vulkan 组合后端**（文件里 `STELQUICK: runtimeApi=Vulkan` 可自证），
不是验收口径的 Metal。三次全部 `VERDICT=FAIL`（rc=5），**12 项探针全黑**
（`偏差255`、抓帧中心像素 `(0,0,0)`）。

## 为什么留档而不是删掉

1. **"非验收配置的读数不能当证据"** 这件事值得留一个实物 —— 本仓库对"非验收配置"
   一贯是**明确标注、不采信**（同类先例：`2026-09-23-sky-longrun-INVALID-screensaver/`、
   `2026-09-23-t13-live-longrun/` 的丢设备现场）。
2. 它顺带说明 **A2 判据的"环境"不只是负载**，还包括**后端选型** —— 后者是脚本
   硬编码的，手工复跑极易丢掉。

## 正确读数在哪

| 轮次 | A2 结果 | 文件 |
|---|---|---|
| 第 1 轮（二进制 `cfc87f37…`） | `rc=0` | 已被后续轮次覆盖（同目录名的旧一轮） |
| 第 2 轮（二进制 `9467b501…`） | `rc=6` **UNAVAILABLE**（应用内 `A2CHECK: 超时：场景图未确认上传（等待 4000ms）`） | 同上 |
| 第 3 轮 = **最终轮**（二进制 `d33f8732…`） | **`rc=0`，12 项探针失败 0** | `../regression-a2-metal.txt` |

⇒ 第 2 轮那次 `UNAVAILABLE` 是**仪器可用性间歇**（同一份代码、同一台机，另两轮 rc=0），
与 T22 那次"布局就绪门"同源：**重负载下等待窗口不够，不洗成 PASS 也不改 INVALID**。
