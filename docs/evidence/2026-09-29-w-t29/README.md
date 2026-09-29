# W-T29 证据目录（Windows 跨平台复验，2026-09-29）

对应交付文档：`docs/WT29_WINDOWS_VERIFY.zh_CN.md`；BUILD_RECORD `2026-09-29｜W-T29` 章；
计划文档 §9.4.14。

## 结论一句话

Windows 侧**原生 Vulkan** 上跑齐 **18 项自检套件，全 rc=0**（首轮 2 红 → 定性为**仪器缺陷**
→ 修仪器 → 复跑全绿）；macOS 侧同轮复验全绿。**两侧都变绿靠的是同一个提交** `b4e8cf2`。

## 目录结构

| 子目录 | 内容 | 关键文件 |
|---|---|---|
| `suites-round1/` | **首轮**（`20d4157`，`exe md5=6E6B37B3…`）：2 红 | `SUMMARY.txt`（`returnuicheck rc=10`、`interactcheck-run1..5 rc=10`）、`returnuicheck.out.txt`、`interactcheck-run1.out.txt` |
| `suites/` | **第二轮**（`b4e8cf2`，`exe md5=838A7B55…`）：**18/18 rc=0** | `SUMMARY.txt`、`returnuicheck.out.txt`、`interactcheck-run1..5.out.txt` |
| `mac/` | macOS 侧同轮复验（Metal） | `rc-summary.txt`、`interactcheck-mac-run1..5.txt`、`regression-*.txt`（含 `dyn-engine-metal` / `dyn-test-metal` 各 5 跑） |
| `longrun/` | **Windows 30 min 长跑**（rc=0，**SL-C01..C11 全 PASS**，50.00 fps / 0.080 MiB·min⁻¹） | `t29w-win-30min.{out,head,err,rc}.txt` + 逐秒 `.csv.gz`（2699 行）+ 逐帧 `.frames.csv.gz`（138 015 行） |
| `dyn-two-way/` | **Windows DYN 双路重采**（engine ×3 + test ×3，**6/6 rc=0**，六次 `producer-readback … OK`） | `SUMMARY.txt`、`dyn-{engine,test}-run{1,2,3}.{out,err}.txt` |

## 首轮 2 红 → 仪器缺陷（不是产品缺陷）

两条红都指向**同一个仪器缺口**：`schtasks /it` 投递的进程**窗口不是前台窗口**
（foreground lock），Qt Quick 的键事件派发依赖 `QQuickWindow::activeFocusItem`，
窗口未激活时 `forceActiveFocus()` 拿不到 active focus ⇒ 注入的键根本没进 QML。

**决定性对比**（`suites-round1/` vs `suites/`）：

| 套件 | 首轮 | 第二轮（同一判据、同一读法） |
|---|---|---|
| `returnuicheck` | RT-10 `currentIndex=2（期望 1）` **红**；RT-11 连带红 | 门报 `未激活（尝试 1/3）→ 已激活（重试 1 次）`；RT-10 `currentIndex=1` **绿**；RT-11 绿 |
| `interactcheck` | IT-05 `timeRate 0.1 → 0.1`、`dispatched 累计=0` **红**；紧随其后 `IT-note 窗口未激活（尝试 1/3）` 这才把窗口激活 ⇒ IT-06 起 11 条全绿 | 门提前到相位 switch 之前 ⇒ IT-05 `timeRate 0.1 → 1`、`dispatched 累计=1`、`lastActionId="actionIncrease_Time_Speed"` 全绿 |

`returnuicheck` 首轮的 RT-note 曾报「受理=true」，**这是相反结论**：Esc 分支无论走
"守卫吞掉"还是"返回天空"都会把 `accepted` 置真。⇒ 直接读原始状态，别用受理位反推。

## 第二轮的决定性读数

```
RETURNUICHECK: RT-note Esc 派发后焦点快照：
  windowActive=true keySinkHasActiveFocus=true
  activeFocusItem=skyKeySink focusObject=QQuickItem canDispatchToSky=true
INTERACTCHECK: ✓ IT-05 天空页注入 L 键 → timeRate 0.1 → 1（应变），dispatched 累计=1
INTERACTCHECK: ✓ IT-14 组合态注入 Esc：页 2 → 2、rate 1 → 1、dispatched 1 —— 焦点在输入控件时 Esc 必须走守卫
INTERACTCHECK: ✓ IT-16 判别性对照：焦点移出输入控件后注入 Esc → currentIndex=1（天空页）
```

macOS 侧同轮（`mac/`）同样拿到 `keySinkHasActiveFocus=true activeFocusItem=skyKeySink`，
且 8 项相邻回归 + A2 逐像素 + DYN 双路（engine 5/5 + test 5/5）全 rc=0。

## 附带抓出的真实缺陷：DYN 判别性对照跑的是两次替身

`tools/t29-verify.sh` 的 `run_dyn_engine()` **漏了** `STELQUICK_DYN_PRODUCER=engine`。

- `src/ui/main.cpp:4120-4121` 只认**显式** `qgetenv("STELQUICK_DYN_PRODUCER") == "engine"`，
  **不设变量 = 替身**；长跑同款 `STELQUICK_LONGRUN_PRODUCER`（`main.cpp:4678-4679`）。
- 后果：T29 的"真实引擎 vs 替身"对照**两路都是替身**，而两份读数确实**互不相同**
  （同一路径两次采样差异）⇒ 从读数上完全看不出破绽。
- 处置：修 `tools/t29-verify.sh`（补显式生产者）+ `CORRECTION.md` 留证 + **降级** T29 的
  DYN 结论；本项目改用 `tools/wt29-verify.sh` / `tools/windows/wt29-suites.ps1`，
  跑完**回读**生产者标签（`生产者=engine(真实引擎)`）才算数。

## 复现方式

```bash
# macOS 侧（Metal，口径刻意不换）
tools/wt29-verify.sh all 5

# Windows 侧（见 docs/WT29_WINDOWS_VERIFY.zh_CN.md 第 2 节的通路说明）
powershell -NoProfile -ExecutionPolicy Bypass -File tools\windows\wt29-build.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File tools\windows\wt29-suites.ps1
```

日志采集一律 `Start-Process -RedirectStandardOutput/-Error` **原始字节直落盘**（exe 以
MSVC `/utf-8` 编译 ⇒ 产物即 UTF-8），Mac 侧直接 `cat`；**别用 `*>>`**（UTF-16LE × CP936 往返）。
