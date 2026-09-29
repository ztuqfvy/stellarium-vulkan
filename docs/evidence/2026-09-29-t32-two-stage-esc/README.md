# 证据：T32 搜索框两段式 Esc

> 任务：计划文档 §9.2 路线表 / 四处移交表的同一条 —— `**可选特性：搜索框两段式 Esc**`。
> 文档：`docs/T32_TWO_STAGE_ESC.zh_CN.md`
> 一键复跑：`tools/t32-verify.sh all 5`（macOS）｜`tools/windows/wt32-suites.ps1`（Windows）

## 目录

| 目录 | 内容 |
|---|---|
| `mac/` | macOS + Metal + MoltenVK 全量读数（`tools/t32-verify.sh all 5`） |
| `mac/locatecheck-ab/` | ⚠️ **`LOC-04 (b)` 红项的定性对照**（B/C/D 三组，见下） |
| `mac/code-negctl/` | **三轮代码级负控**（N1 去第一段 / N2 去第二段 / N3 去组合态例外） |
| `windows/` | Windows 原生 Vulkan 整批产物（SUMMARY 含 `SRC <file> md5` 自证） |

## 一句话读数

- **正题**：macOS `18/18` ×5（`pos-pass=5 env-skip=0 bad=0`）；Windows `18/18` ×5。
- **负控（环境级）**：macOS `5/5`；Windows `3/3` —— `rc=6`、`判据 12/12`、
  **7 条逐项点名 IT-06,13,14,15,16,17,18**、`INTERACT-INTEGRITY` 绿、IT-07..IT-12 真跑 6/6、零 ✗。
- **探针（边界实测）**：macOS `13/18` 红 `IT-06,13,14,17,18`；Windows `12/18` 红 `IT-05,06,13,16,17,18`。
- **代码级负控**：三轮各**只红一条**（N1→IT-17、N2→IT-18、N3→IT-14）。
- **回归**：macOS 10 项里 **9 项 `rc=0`**，`locatecheck` 红（见下）；Windows **10 项全 `rc=0`**。
- **DYN 双路**：macOS engine/test 各 5/5；Windows 各 3/3；`producer-readback` 全 OK。

## 那个红项：`LOC-04 (b) 0.5826° > 0.50°`

**不是 T32 引入的。** 三组对照（`mac/locatecheck-ab/README.txt`）：

| 组 | 二进制所含源码 | 5 次读数（阈值 0.50） |
|---|---|---|
| B | T32 全量 | 0.2896 / 0.4260 / 0.2494 / 0.2520 / 0.2525 |
| C | 仅回退两个 QML | 0.2673 / 0.2704 / 0.1868 / 0.2759 / 0.1600 |
| **D** | **全部回退到 T31 源** | 0.3102 / 0.3162 / **0.0505** / 0.3223 / 0.3265 |

D（**完全不含 T32 改动**）与 B/C 同分布 ⇒ 与 T32 源码无因果。同源 Windows（`SRC md5` 逐项相同）
本期 `locatecheck rc=0`。

机理：采样点在"跳变后 **0ms**" ⇒ 残余角 ≈ 一次事件循环延迟内的天球转角
（同段日志实测 ≈30°/s ⇒ 一帧 ≈0.3°）⇒ 读数**双峰**（单拍 ≈0.05°、双拍 ≈0.32°）。
阈值 0.50 对第二档没余量。**本轮不改判据、不洗成 PASS**，移交见文档 §8。

## 另一条发现：macOS 链接产物非位级可复现

同源连续两次 relink：字节数都是 `39339496`，**md5 必然不同**（`LC_UUID` 每次重掷，
实测 `A1014EFB-…` vs `02AE647E-…`）⇒ 判"同源"要看**源文件 md5**，不能看二进制 md5。
（Windows 侧本轮仍是位级可复现。）
