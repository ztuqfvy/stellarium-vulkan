# T36 探针：合流形态**写穿原版 Stellarium 用户目录**

> 日期：2026-09-30｜环境：macOS + Metal/MoltenVK（与 T17–T35 同口径）
> 被测产物：`build-release/src/ui/stelQuickUI.app/Contents/MacOS/stelQuickUI`
> （`40055912 B`，即 T35 定稿轮那一颗；本轮探针**未**改任何被测代码）

## 1 问题

A-1.0 范围表写着「资源路径 …… 配置保存 | 最小 | **必须**」，A5 一栏写着
「配置使用**独立个人版目录**，避免修改原程序设置」。

但在动手之前必须先回答一个**事实**问题：合流形态现在到底读写哪个目录？
直觉上"从没配过就会用默认目录"，可是另一条线索指向相反的结论：
`~/Library/Application Support/Stellarium/config.ini` 与 `log.txt` 的 mtime
**恰好等于 T35 mac 跑批的时间**（今天 11:56）—— 那说明有东西在写它。

## 2 方法（可复现，只读仪器）

`snap.zsh`（本目录内，只有 `find` / `stat` / `md5`，不写任何文件）把原目录
每个文件的 `relpath|size|mtime|md5` 落一份清单，然后：

```
# ① 跑前快照
/tmp/cfgprobe/snap.zsh pre
# ② 跑一次**已有的**工具栏自检（它会翻转 4 个开关 ⇒ 触发 immediateSave）
env VK_DRIVER_FILES=… QT_VULKAN_LIB=… STELQUICK_GRAPHICS_API=metal \
    STELQUICK_TOOL_CHECK=1 <BIN> > run1.out 2>&1     # rc=0，TOOLBARCHECK 12/12 PASS
# ③ 跑后快照 + 逐行 diff
/tmp/cfgprobe/snap.zsh post
/usr/bin/diff pre.manifest post.manifest
```

选工具栏自检是有意的：它**不是**为配置面写的，走的是**用户点按钮**同一条路
（`ActionRouter.trigger` → 模块 setter → `StelApp::immediateSave`）。所以它写出来的
东西就是"用户翻一个开关"会写出来的东西。

## 3 结果：`diff pre.manifest post.manifest`（逐字引用）

```
1c1
< config.ini|42730|1790740596|07a9a4391f60c38e8d01b14001b0a568
---
> config.ini|42730|1790745245|07a9a4391f60c38e8d01b14001b0a568
6c6
< log.txt|10308|1790740600|579a81272a8cba3140300d35156d468f
---
> log.txt|10387|1790745244|611bcc9e3b56b4bde9b9b24ccfee719f
10c10
< modules/Oculars/ocular.ini|10256|1790740596|5e2bc2a74d60617ef6cfd2d58aa1de1d
---
> modules/Oculars/ocular.ini|10256|1790745240|5e2bc2a74d60617ef6cfd2d58aa1de1d
```

60 个文件里**恰好 3 个**被动了：

| 文件 | size | md5 | mtime |
|---|---|---|---|
| `config.ini` | 不变 | **不变** | `1790740596` → `1790745245`（**被整体重写**） |
| `log.txt` | 10308 → **10387** | `579a81…` → **`611bcc…`** | 变（**原版日志被顶掉**） |
| `modules/Oculars/ocular.ini` | 不变 | 不变 | `1790740596` → `1790745240`（被重写） |

另外 `config.diff` 的行数 = **0**：这一次 `config.ini` 的**内容**没变。
但这不是"没写"的证据，恰恰相反 —— 它证明**写发生在所有值都改回原值之后**
（工具栏自检有"收尾复原"腿），文件被重写、mtime 变了、只是最终内容撞回原样。
判据不能依赖"内容没变"这种巧合：**用户真去点一个开关，内容就会变。**

## 4 根因（代码位置，不是猜测）

| 环节 | 位置 | 说明 |
|---|---|---|
| 用户目录取默认值 | `LiveSkyRuntime.cpp:50` `StelFileMgr::init()` | macOS → `~/Library/Application Support/Stellarium` |
| 日志落盘 | `LiveSkyRuntime.cpp:52` | `StelLogger::init(userDir + "/log.txt")` ⇒ **覆盖原版日志** |
| 配置对象 | `LiveSkyRuntime.cpp:62` | `new QSettings(configFileFullPath, StelIniFormat, nullptr)` |
| 写侧触发器 | `StelApp.cpp:1404-1408` | `immediateSave()` 在 `flagImmediateSave` 为真时 `setValue` 直写 |
| 本机该开关 | 原目录 `config.ini:479` | `immediate_save_details = true` ⇒ **本机一直处于"一改就写"** |

## 5 为什么不能用"把原目录留作只读回退"来修

见 `src/app/ConfigIsolation.hpp` 头注：`StelFileMgr::findFile()`（`StelFileMgr.cpp:218`）
按 `fileLocations` 顺序返回**第一个满足 flags** 的路径，而 `Writable` 只表示
"**那个文件**可写"。模块用 `findFile(..., Writable|File)` 更新自己下载的数据
（`modules/Satellites/tle*.txt` 等）时会命中原目录里的副本并**原地改写原目录**。
⇒ 唯一可靠的做法是**先把原目录播种到个人版目录**，之后原目录彻底退出搜索路径
（`setUserDir` 是 `replace(0, …)`，`StelFileMgr.cpp:422-428`）。

## 6 本目录文件

| 文件 | 是什么 |
|---|---|
| `README.md` | 本文 |
| `pre.manifest` / `post.manifest` | 跑前/跑后各 60 行的 `relpath\|size\|mtime\|md5` |
| `config.diff` | 两份 `config.ini` 的内容 diff（**0 行** —— 见 §3 的提醒） |
| `run1.out` | 那次 `STELQUICK_TOOL_CHECK=1` 的完整输出（rc=0，`TOOLBARCHECK: VERDICT=PASS`） |
| `snap.zsh` | 快照器本体（只读，可复跑） |
