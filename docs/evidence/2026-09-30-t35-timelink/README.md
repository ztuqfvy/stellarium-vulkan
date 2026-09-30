# 证据：T35 仿真时间链路"脱钩"定性（2026-09-30，macOS + Metal/MoltenVK）

> 一键复跑：`tools/t35-verify.sh core 5`
> 产品文档：`docs/T35_TIMELINK.zh_CN.md`
> 环境口径与 T17–T34 一致、**刻意不换**：`VK_DRIVER_FILES=MoltenVK_icd.json`、
> `QT_VULKAN_LIB=libvulkan.1.dylib`、`STELQUICK_GRAPHICS_API=metal`。

## 1 汇总（`mac/rc-summary.txt`）

| 段 | 结果 |
|---|---|
| 正题 `TIMELINKCHECK` ×5 | **5/5** `判据 7/7 VERDICT=PASS`，残留进程 0，`env-skip=0` |
| 负控 A `BREAK` | 期望 rc=10 ∧ 6/7 ∧ 红 **[TL-01]** ⇒ **OK** |
| 负控 B `RATE_IGNORED` | 期望 rc=10 ∧ 4/7 ∧ 红 **[TL-01,TL-02,TL-05]** ⇒ **OK** |
| 负控 C `FREEZE_LEAK` | 期望 rc=10 ∧ 6/7 ∧ 红 **[TL-04]** ⇒ **OK** |
| 探针 `TIMELINKPROBE` | `VERDICT=DONE`，Q1..Q6d 齐全、占位符漏 0 ⇒ **OK** |
| 相邻回归 11 套 | `timecheck`/`returnuicheck`/`searchcheck`/`actioncheck`/`locatecheck`/`locate-uicheck`/`replaycheck`/`clockcheck`/`timeuicheck`/`locationcheck`/`toolbarcheck` **全 rc=0** |
| `INTERACTCHECK` | **rc=0（18/18，窗口已激活）** —— 本批**拿到**焦点，非 ENV-SKIP |
| S3 旧宿主 `stellarium` | rc=0 |
| A2 逐像素 | rc=0 |
| DYN 双路 | `engine` **5/5** ∧ `test`（替身）**5/5**，`producer-readback OK` |
| **总** | **FAILED=0** |

## 2 这一轮要定的"性"

T27 留档（`docs/T27_MOUSE_NAV_UI_CHECK.zh_CN.md` §4 + 本仓库 `../2026-09-29-t27-mouse-nav/README.md`）：

> 帧泵推进量（1.5 天/秒）与 `getTimeRate()`（=10）**不一致** … 建议单独立项核查。

**结论：不存在"推进量与 rate 脱钩"。** T27 那三个数全是**测量口径**问题（详见产品文档 §3）：

1. `getTimeRate()` 的单位是 **JDay/sec**（`src/core/StelCore.hpp:595`），README 的算术
   `10 × JD_SECOND × 0.4` 把它当成了"× 实时倍率"，凭空引入 1/86400 因子；
2. `engineRate` 在 INTERACTCHECK 内是**移动靶**——套件的 **IT-05 自己注入 L 键**
   走 `increaseTimeSpeed()`（×10 阶梯），把 rate 从相位设计值抬到 1（再按到 10）；
3. "400ms" 是**相位名义 delay**，从未与 ΔJD 在同一时刻测过真实窗口 ——
   "0.61 天 ÷ 0.4s = 1.5 天/秒"就是"用错窗口 + 读错单位"的商。

## 3 正题逐条读数（`mac/timelinkcheck-mac-run1.txt`）

```
✓ TL-06 读面同源：模式=HostDriven |getJD−simClockJD|=0.000e+00 |facade.julianDay−simClockJD|=0.000e+00
✓ TL-01 链路自洽：窗口 W=3.0596s ΔJD=0.617600 天 rate=0.2 scale=1 ⇒ 应走 0.611912 天 比值=1.0093
                  反推窗口=3.0880s ⇒ 相对偏差=0.93%（≤15%）
✓ TL-02 rate 多档线性：**速率归一化**后之比=4.0369（期望 4.0；裸 ΔJD 之比=4.0559、长度差 0.5%）
✓ TL-03 窗口线性：ΔJD 之比/W 之比=1.0077（短窗 0.8174s / 长窗 2.5033s）
✓ TL-04 冻结/恢复不补：冻结窗 ΔJD=0.000000000000 ∧ 恢复窗 W=1.5363s ΔJD=0.305200 比值=0.9933
✓ TL-05 速率阶梯 + 链路跟变：阶梯 {0.1,1,10} 成立 ∧ ΔJD 之比/rate 之比=0.9608
✓ TL-07 恒星时腿：W=4.2322s ΔJD=0.211600 天 ⇒ ΔLST=+76.3846° 期望 +76.3846°（残差=+0.0000°）
                  ∧ 固定 J2000 方向经 j2000ToAltAz 落点转过 76.384°
  收尾：已还原 rate=0.1 scale=1 ⇒ 回读 rate=0.1 scale=1
（**窗口就绪门本批触发 0 次** ⇒ 11 个窗口全部可信；脚本可据此判断本批是否干净）
```

## 4 脚本**独立复算**（`mac/timelinkcheck-mac-n5.txt`，五跑全绿）

脚本不引用被测的 PASS，而是从日志行里把原始量抠出来**自己再算一遍**：

```
run1  TL-01 W=3.0596 ΔJD=0.617600 rate=0.2 复算比值=1.0093 复算偏差=0.93% ｜ TL-07 残差=0.0000°
run2  TL-01 W=3.0493 ΔJD=0.611000 比值=1.0019 偏差=0.19% ｜ TL-07 残差=0.0000°
run3  TL-01 W=3.0996 ΔJD=0.628600 比值=1.0140 偏差=1.40% ｜ TL-07 残差=0.0000°
run4  TL-01 W=3.1482 ΔJD=0.634600 比值=1.0079 偏差=0.79% ｜ TL-07 残差=0.0000°
run5  TL-01 W=2.9770 ΔJD=0.596800 比值=1.0024 偏差=0.24% ｜ TL-07 残差=0.0000°
```

⚠️ 五跑偏差**全为正**、量级 0.19%..1.40%，与"帧泵 `dt` 含窗口开始**前**一小段（≈1 帧，~16ms）"
这条**固有正偏**一致（见产品文档 §7.1 的注）。TL-07 是恒等式 ⇒ 恒 0。

## 5 三组负控

| 组 | 文件 | rc | 判据 | 红项实测 |
|---|---|---|---|---|
| A `STELQUICK_TIMELINK_BREAK=1` | `mac/negctl-A-break.txt` | 10 | 6/7 | `TL-01` |
| B `STELQUICK_TIMELINK_RATE_IGNORED=1` | `mac/negctl-B-rate-ignored.txt` | 10 | 4/7 | `TL-01, TL-02, TL-05` |
| C `STELQUICK_TIMELINK_FREEZE_LEAK=1` | `mac/negctl-C-freeze-leak.txt` | 10 | 6/7 | `TL-04` |

红项集合**两两不同** ⇒ "链路自洽"、"rate 真进链路"、"冻结必须真零"三条腿**各自承重**。

> 🔴 本条路径上踩过一个坑：第一版负控 C 是"解冻后把冻结期一次性补回来"（形状完全正确），
> 但**判据照绿**（比值 0.9947）——解冻后检查还要等 300ms 才 `arm()`，
> **"补"的那一脚落在开窗之前**。见产品文档 §8.3。

## 6 探针（`mac/probe-timelink-mac.txt`）

```
Q1 时钟模式=HostDriven  scale=1
Q1 读面 simClockJD=getJD=facade.julianDay=2461313.639811805（差 0.000e+00 / 0.000e+00 天）
Q1 速率面 core->getTimeRate=0.1 天/秒（JDay/sec，见 StelCore.hpp:595）
Q2 档A(长) W=2.4883s ΔJD=0.501600 比值=1.0079 偏差=0.79%
Q3 档A'(短) W=0.8142s ΔJD=0.163400 比值=1.0034 偏差=0.34%
Q4 阶梯：0.1 ⇒ 1 ⇒ 10 ⇒ 100
Q5 冻结(scale=0) W=1.0268s ΔJD=0.000000
Q5b 恢复后 W=1.2073s ΔJD=0.242800 比值=1.0056 偏差=0.56%
Q6b 恒星时恒等式：ΔLST=34.8532° 期望 34.8532° 残差=0.0000°
Q6c 下游重算：固定 J2000 方向经 j2000ToAltAz 落点转过 34.853°
Q6d 视线随 JD 转：静置时 getViewDirectionJ2000 的 ΔRA=34.7580°（与 ΔLST 同阶）
```

Q6d 的机理（**静态分析被实测推翻后改口**得到的真机理）：
`updateVisionVector` 的「vision vector locked to its position in the mountFrame」分支
（`flagLockEquPos` 默认 false）每帧由 `mountFrameToJ2000(viewDirectionMountFrame)` 重算，
而量天系不动 ⇒ J2000 视线以恒星时速率旋转。
**这正是 T27「视线锁地平 ⇒ 随 JD 漂移」的正确机理——T27 错的只是"速率"读数。**

## 7 本轮改的三处**污染口径**（改的是口径，不是历史）

| 文件 | 原话 | 处置 |
|---|---|---|
| `docs/T27_MOUSE_NAV_UI_CHECK.zh_CN.md` §4 | "`engineRate=10` 但 `simJD` 差 0.61 天/0.4s（≈1.5 天/秒）—— **HostDriven 帧泵推进与 rate 读数脱钩**" | 就地标注**已被 T35 定性推翻**，指向本文档 |
| `../2026-09-29-t27-mouse-nav/README.md` §2 + "待查线索" | "0.61 天 vs rate=10 该走的 4.6e-5 天，**差 4 个数量级**" | 同上 |
| `src/ui/main.cpp` IT-07 注释 | "帧泵在 400ms 内推进仿真时间 0.61 天（≈1.5 天/秒，与 rate 读数脱钩）" | 同上 |

> 原则：**不删原文**（那是当时的真实观察），只在旁边标注"读数正确、解释错误"与出处。
> 0.61 天这个数字本身**是对的**——它等于"某个真实窗口 × 某个真实 rate"。

## 8 文件清单

```
mac/rc-summary.txt                     一键跑的汇总
mac/timelinkcheck-mac-run{1..5}.txt    正题 5 跑原始日志
mac/timelinkcheck-mac-n5.txt           5 跑聚合 + 脚本独立复算行（过程证据）
mac/negctl-A-break.txt                 负控 A
mac/negctl-B-rate-ignored.txt          负控 B
mac/negctl-C-freeze-leak.txt           负控 C
mac/negctl-mac.txt                     三组负控并列 + 期望/实测对照
mac/probe-timelink-mac.txt             探针
mac/regression-*.txt                   相邻回归（11 套件 + INTERACTCHECK + S3 + A2 + DYN 双路）
```
