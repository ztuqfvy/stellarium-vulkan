# 证据索引：2026-09-29-t30-visual（T30 时间页视觉层判据组）

> 交付文档：`docs/T30_TIME_VISUAL.zh_CN.md`｜提交：`d04ea31`
> 改动面：`src/ui/main.cpp`（**纯仪器**：TIMEUICHECK 追加 UI-17..UI-24 + 负控开关
> `STELQUICK_TIME_VISUAL_NEGCTL`）｜`tools/t30-verify.sh`｜`tools/windows/wt30-*.ps1`
> **产品代码零改动**（页面/面板几何一律沿父链取；既有 19 条判据口径一条未改）。

## 一句话

`TIMEUICHECK` 的 8 条新判据把"控件存在但**看不见 / 点不到**"变成可复跑判据；
负控用四个扰动（月框 `height→0`、年框 `visible→false`、JD 行 `text→""`、
状态行前景色→自身背景色）证明其中 **5 条真的会翻转**（UI-17/20/21/22/23），
且**既有 19 条零 FAIL**（扰动不泄漏到前面的相位）。

## 目录

| 子目录 / 文件 | 内容 | 关键读数 |
|---|---|---|
| `mac/rc-summary.txt` | macOS 侧各项 rc 一览 | — |
| `mac/timeuicheck-mac-run1..5.txt` | **正题**：TIMEUICHECK ×5 | 每跑 **27/27 PASS**、rc=0 |
| `mac/timeuicheck-mac-n5.txt` | 5 跑的判据行汇总 | — |
| `mac/negctl-mac-run1..5.txt` | **负控** ×5（`STELQUICK_TIME_VISUAL_NEGCTL=1`） | 每跑 **22/27**、rc=10；FAIL 恰好 = UI-17/20/21/22/23 |
| `mac/negctl-mac-n5.txt` | 负控汇总（含逐条 FAIL 计数） | `negctl-OK` ×5；UI-18/UI-19 FAIL=0（设计如此）；既有 19 条 FAIL=0 |
| `mac/regression-{timecheck,returnuicheck,interactcheck,searchcheck,actioncheck,locatecheck,locate-uicheck,replaycheck,clockcheck}.txt` | 9 项相邻回归 | **全 rc=0** |
| `mac/regression-a2-metal.txt` | A2 逐像素（Metal） | **rc=0**；`runtimeApi=Metal`；**探针 12 项、失败 0** |
| `mac/regression-dyn-{engine,test}-metal-run1..5.txt` | DYN 双路（**显式生产者 + 回读**） | `engine` **4/5** + `test` **5/5**；生产者回读 OK |
| `mac/a2-manual-probe-INVALID-wrongenv/` | ⚠️ **无效证据留档**：手工重探 A2 时漏了 Metal 三件套 ⇒ 落到 Vulkan 组合、12 项全黑 | **不作为任何结论依据**（该目录 `README.md` 说明来龙去脉） |
| `windows/README.md` + `windows/suites/SUMMARY.txt` | **W-T30 跨平台复验**（原生 Vulkan，58 个文件） | **26 个套件全符合预期**：正题 27/27 ×5、负控 3/3 翻转、`interactcheck` ×5、8 项回归、DYN `engine` 3/3 + `test` 3/3（**6 次回读全 OK**）、A2 `VERDICT=PASS` |
| `windows/t30w-{pull,build}.log` + `build.rc` + `build-manifest.txt` | Windows 侧快进与增量构建 | `merge_rc=0`；构建 `rc=0`（`0.3 min`）；**旧宿主 `stellarium.exe` 的 size/md5/mtime 三项与 W-T29 基线全等** ⇒ S3 字节级证明 |

### 两处环境间歇（如实留档，不洗成 PASS）

1. **A2 一次 `rc=6` UNAVAILABLE**：某一轮全量跑（二进制 `9467b501…`）里应用内报
   `A2CHECK: 超时：场景图未确认上传（等待 4000ms）` —— **仪器等待窗口**在重负载下不够，
   **不是"像素不对"**；最终轮（`d33f8732…`）同码同机 `rc=0`。
2. **DYN engine 4/5**：掉的那一跑是 `D1-C02`（显示尾窗稳态 `0.0 fps`）+ `D1-C07`
   （degraded 标志），而同跑的生产者腿 `D1-C01` 仍 `PASS`（49.9 fps / 642 帧）
   ⇒ 显示侧停摆，属既有**环境敏感量**（T27 `1/3`、T28 `3/3`、T29 `5/5` 同现象）。

## 二进制口径（证据必须能对上二进制）

| 平台 | 二进制 | 大小 | md5 |
|---|---|---|---|
| macOS（Metal + MoltenVK） | `build-release/src/ui/stelQuickUI.app/Contents/MacOS/stelQuickUI` | `39320456 B` | `d33f873224cfec8e4a39504d832a58a7`（`2026-09-29 15:09`，正题+负控+回归+DYN **同一枚**） |
| Windows（原生 Vulkan） | `build-win/src/ui/Release/stelQuickUI.exe` | `28917248 B` | `2BB6FBD49DFD3262B9C31DA9996087DF`（`2026-09-29 16:23:09`，全部 26 个套件 **同一枚**） |
| Windows 旧宿主（对照） | `build-win/src/Release/stellarium.exe` | `27634176 B` | `66C51B61582BAC065C7A7FE5ACA44A42`（mtime `11:56:35`）—— 与 W-T29 基线**三项全等** ⇒ 未被重写 |

## 为什么"负控里 UI-18/UI-19 是绿的"不是漏了

它们的几何矩形取自**下一拍**的快照，那时布局的 polish 已经把被扰动的高度改回去了。
这两条判据的正确性由 **UI-24 的纯逻辑腿**背书（`uiRectInside` / `uiIntersectArea` /
`uiHitPickChild` 三组已知输入 ⇒ 已知输出）。这正是 T21 那套"**纯逻辑腿 + 活引擎腿**"
的两条腿分工 —— 只有活体判据会让缺陷以"全绿"溜过去（血泪第 8 条）。
