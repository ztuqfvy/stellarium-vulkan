# W-T29 Windows 跨平台复验（2026-09-29）

> 提交：仪器 `b4e8cf2`、产物 `234b232` ｜ 证据：`docs/evidence/2026-09-29-w-t29/`
>
> 一句话：**Windows 侧第一次跑通全量自检套件——18/18 全 rc=0**；
> 首轮的两个红**不是产品缺陷**，是**仪器缺了一道窗口激活门**；
> 顺着这条线还挖出**一个真实的脚本缺陷**（`t29-verify.sh` 的 DYN 生产者变量丢失）。

---

## 1. 结论先行

| 项目 | 结果 |
|---|---|
| Windows 侧全量自检套件 | **18/18 rc=0**（含 T17–T29 全部增量功能） |
| 首轮两个红的定性 | **仪器缺陷**（缺窗口激活门），非产品缺陷 |
| 顺带挖出的真实缺陷 | `tools/t29-verify.sh` 的 `run_dyn_engine()` **漏了 `STELQUICK_DYN_PRODUCER=engine`** ⇒ T29 那条"真实引擎 vs 替身"的判别性对照实际跑了**两次替身** |
| macOS 侧复验（本次插桩改动） | 全绿：INTERACTCHECK **5/5**、8 项相邻回归 rc=0、A2 逐像素 rc=0、DYN **engine 5/5 + test 5/5** |
| Windows 30 分钟长跑 | ✅ **rc=0，SL-C01..SL-C11 全 PASS**，50.00 fps、内存斜率 **0.080 MiB/min**（见 §6） |
| Windows 侧 DYN 判别性对照 | ✅ 重采 **engine ×3 + test ×3 全 rc=0**，且 **`producer-readback: requested=… observed=… OK` 逐次回读**（见 §4） |
| 结论可信度的一个前提 | Windows 侧跑的是**同一份源码**（`b4e8cf2`），二进制 md5 与 mtime 全部留档 |

**W 支线此前记的阻塞点（"`schtasks` 被沙箱禁用"）不成立**——`/query`、`/create /it`、
`/run`、`/delete` 实测全 rc=0，探针任务确实写出了一个文件。该条记录已作废。

---

## 2. Windows 侧通路（环境事实，全部实测）

| 能力 | 状态 | 物证 |
|---|---|---|
| SSH 免密 | ✅ | `127.0.0.1:2222`（UU远程端口映射）→ `desktop-0pji1so\ztuqfvy`，Windows 10.0.19045.6159 |
| 交互桌面会话 | ✅ | `query session` → `console ztuqfvy 13 **Active**`；`LogonUI` 进程数 **0**（**未锁屏**） |
| `schtasks /it` | ✅ | create/run/delete 全 rc=0；探针任务写出 `C:\temp\probe.out` |
| 磁盘 | ✅ | C 18.5G / D 370.6G / E 321.8G |
| 仓库位置 | — | `E:\Qt_demo\stellarium-vulkan`（注意大小写与实际不同，Windows 路径不敏感） |
| 构建目录 | — | `build-win`（`CMAKE_HOME_DIRECTORY` 指向仓库根 ⇒ 合法根构建） |
| 工具链 | — | VS 自带 cmake 4.3.1（Qt Tools 自带的 3.30.5 不认 "Visual Studio 18 2026" 生成器） |
| 后端 | — | **原生 Vulkan**（`device=NVIDIA GeForce RTX 4060 api=1.4.325 driver=79.296.0`） |

### 2.1 三条 Windows 侧的操作坑（已写进技能）

1. **SSH 里 `Start-Process` 起的子进程会随会话退出被杀** —— 首次构建脚本起了 20 秒就没了。
   长任务**必须**走 `schtasks /it`。
2. **中文输出在 Mac 侧全乱码** —— 远程命令前加
   `[Console]::OutputEncoding=[Text.Encoding]::UTF8; chcp 65001 | Out-Null` 解决。
3. **收日志别用 `*>>`** —— PS 5.1 会先按 CP936 解码再写成 UTF-16LE。
   改用 `Start-Process -RedirectStandardOutput`，**原始字节直落盘**，产物就是
   exe 吐出的 UTF-8（exe 以 MSVC `/utf-8` 编译），Mac 侧直接 `cat`。

---

## 3. 故事主线：首轮两红 → 根因 → 修法

### 3.1 首轮（`20d4157`，二进制 `28877824 B / md5=6E6B37…`）

18 个套件里 **16 个 rc=0**，两个红：

| 套件 | 结果 | 红在哪 |
|---|---|---|
| `returnuicheck` | rc=10，9/11 | **RT-10**（非天空页投递 Esc → 应回天空页，实际停在搜索页）；**RT-11** 因 RT-10 未复原态而前提失败（级联，非独立红） |
| `interactcheck` ×5 | rc=10，15/16 | **IT-05**（天空页注入 L 键 → timeRate 应变，实际 0.1→0.1、dispatched=0） |

两条红的**共同形态**：都是该套件里**第一次键注入**。

### 3.2 现场留下的两条硬线索

**线索 A（决定性）**：INTERACTCHECK 的日志里，IT-05 红之后紧接的一条 note 是

```
IT-note 窗口未激活（尝试 1/3），requestActivate 后重试本相位
```

—— 这是 **IT-06 相位**自带的窗口激活门（T27 加的）。也就是说：
**窗口在 IT-05 跑的时候还没激活，是 IT-06 的重试把它激活的**；之后 IT-06…IT-16
共 11 条键/手势判据**全绿**。而 RETURNUI **完全没有激活门**，它唯一的键注入 RT-10 恰好红。

**线索 B（反向）**：RETURNUI 的 `RT-note` 报"Esc 是否被受理 = **true**"。
单看这条会得出"键送到了、是守卫吞了"的相反结论 —— 这正是**间接读数的危险**：
Esc 分支无论走"守卫吞掉"还是"返回天空"都会把 `accepted` 置真，这个读数**不区分**两者。
（`interactcheck` 里 IT-06 的"事件受理=0"倒是有效的——它证明那个读数**不是恒真**。）

⇒ 不能靠反推。**必须把状态直接读出来。**

### 3.3 修法：只动仪器，不动判据口径

在 `src/ui/main.cpp` 加两件东西（提交 `b4e8cf2`）：

**① `uiFocusSnapshot()` —— 焦点快照探针**

键派发后立刻取五项**原始状态**，不复刻任何被测逻辑（血泪第 4 条）：

```
windowActive / keySinkHasActiveFocus / activeFocusItem.objectName /
focusObject 类名 / canDispatchToSky()
```

挂在 RT-09、IT-05、IT-06、IT-14 四个相位。

**② `uiWindowActivationGate()` —— 前导有界窗口激活门**

≤3 次 × 400ms，装在 `uiReturnStep` / `uiInteractStep` 的 **`switch` 之前**
（门放在判据之后会让重试重跑判据、计数虚高——T22 的教训）。
**门超时不洗成 PASS**：只留一条 note，后续键类判据照原样判红。

**判据一条没改**：INTERACT 还是 16 条、RETURNUI 还是 11 条，门槛全部原样。

### 3.4 修复后的决定性读数（同一份二进制 `b4e8cf2`，`28884480 B / md5=838A7B…`）

**RETURNUI**：

```
T29W-note 前导窗口激活门：窗口未激活（尝试 1/3），requestActivate 后重入本相位
T29W-note 前导窗口激活门：窗口已激活（重试 1 次）
RT-note Esc 派发后焦点快照：windowActive=true keySinkHasActiveFocus=true
        activeFocusItem=skyKeySink focusObject=QQuickItem canDispatchToSky=true
RT-10 ... currentIndex=1（期望 1）：OK
RT-11 ... currentIndex=1（不变）且 JD 位移 0.000e+00 天：OK
判据 11/11   VERDICT=PASS
```

**INTERACTCHECK**：同一形态的门日志，且

```
IT-note L 键派发后焦点快照：windowActive=true keySinkHasActiveFocus=true
        activeFocusItem=skyKeySink focusObject=QQuickItem canDispatchToSky=true
✓ IT-05 ... timeRate 0.1 → 1，dispatched 累计=1，
        lastActionId="actionIncrease_Time_Speed"
```

**⇒ 根因确定**：Windows 上由计划任务投递到交互会话的进程受 **foreground lock**
限制，窗口**默认不是前台窗口**；窗口未激活时 `keySink->forceActiveFocus()` 拿不到
active focus ⇒ 注入的键根本没被派发到 QML。缺的是**仪器**，不是产品。

### 3.5 顺带证明的两件事（守卫判据的"前提"是看得见的）

修复后的快照把两个关键状态钉住了：

| 场景 | `activeFocusItem` | `focusObject` | `canDispatchToSky` | 期望 | 实测 |
|---|---|---|---|---|---|
| IT-05 / RT-10（键应生效） | `skyKeySink` | `QQuickItem` | **true** | 生效 | ✅ |
| IT-06 / IT-14（键应被守卫拦） | `searchQueryField` | `TextField_QMLTYPE_18` | **false** | 被拦 | ✅ |

⇒ 之前"否定式判据（IT-06/IT-14 断言'不变'）可能是键根本没送到"的隐忧，现在
有直接读数排除：**焦点确实在输入控件上，守卫确实判 false，键确实到了**。

---

## 4. 第二个真实缺陷：脚本的"判别性对照"没对照上

排查 Windows 侧 DYN 时顺手核对 `tools/t29-verify.sh`，发现：

```
$ grep -m1 '生产者=' regression-dyn-engine-metal-run1.txt regression-dyn-stub-metal-run1.txt
regression-dyn-engine-metal-run1.txt: DYNCHECK: 生产者=test(替身场景)   ← 标着 engine，跑的是替身
regression-dyn-stub-metal-run1.txt:  DYNCHECK: 生产者=test(替身场景)
```

`src/ui/main.cpp:4120-4121` 的口径是 **只认显式 `"engine"`**：

```cpp
const bool engineProducer = (qgetenv("STELQUICK_DYN_PRODUCER") == "engine");
```

而 `t29-verify.sh` 的 `run_dyn_engine()` **漏了这个变量**——`t16..t28-verify.sh`
全都有（`t27-verify.sh:74`、`t28-verify.sh:87`），只有 t29 丢了。
⇒ **T29 当轮的"真实引擎 vs 替身"判别性对照，事实上是同一路径的两次独立采样。**

**归类**：与血泪第 8 条同款——**"规则正确"与"规则被调用"是两件事**；
再延伸一条：**"判别性对照"必须回读被对照对象的身份**，不能只看两跑读数不同就认定对照成立。

**处置**：
- 已修 `tools/t29-verify.sh`（engine 补上变量；stub/替身也显式写 `=test`，不再依赖默认值）。
- T29 已归档的日志**保持原样**（证据 append-only），另加
  `docs/evidence/2026-09-29-t29-ime/CORRECTION.md` 作更正记录，并**降级** T29 的 DYN 结论为
  "替身路径 3/3 ×2 组"。
- 本轮用 `tools/wt29-verify.sh` **重新采集**，两路都做了**生产者回读**：

```
engine   DYNCHECK: 生产者=engine(真实引擎)      ← 尾窗 49.8 fps（下限 30.0），735 帧，5/5 PASS
test     DYNCHECK: 生产者=test(替身场景)        ← 5/5 PASS
```

Windows 侧 `win-suites.ps1` 同款漏项也一并修掉（旧注释 "engine producer is chosen
internally" 是错的），改跑 engine ×3 + test ×3，并在 SUMMARY 里回读生产者标签：

```
# Windows 侧重采（`-DynOnly`，2026-09-29T13:07–13:09，`b4e8cf2` / md5=838A7B55…）
SUITE dyn-engine-run1  rc=0  producer=engine   producer-readback: requested=engine observed=engine OK
SUITE dyn-engine-run2  rc=0  producer=engine   producer-readback: requested=engine observed=engine OK
SUITE dyn-engine-run3  rc=0  producer=engine   producer-readback: requested=engine observed=engine OK
SUITE dyn-test-run1    rc=0  producer=test     producer-readback: requested=test observed=test OK
SUITE dyn-test-run2    rc=0  producer=test     producer-readback: requested=test observed=test OK
SUITE dyn-test-run3    rc=0  producer=test     producer-readback: requested=test observed=test OK
```

两路读数**分离得很清楚**（说明这次判别性对照真的对照上了）：

| 路 | 生产者自报 | 尾窗稳态 fps | 全程累计帧 | D1-C01..C07 | VERDICT |
|---|---|---|---|---|---|
| engine | `生产者=engine(真实引擎)` | **49.9**（下限 30.0） | 723 | 6 PASS + 1 SKIP | **PASS** |
| test | `生产者=test(替身场景)` | **57.9**（下限 36.0） | 370 | 6 PASS + 1 SKIP | **PASS** |

（`D1-C06 SKIP` 是既定项：Vulkan 纹理路径属 §6.2 已知外部缺陷（黑屏），逐像素动态判据
在该后端不可用——**两侧、两路都一样 SKIP**，不是本轮引入。）

`wt29-suites.ps1` 另加了 `-DynOnly` 开关，用于**在不重跑整批**的前提下重取生产者指纹
（长跑/帧率读数不能被扰动）。判据缺口的**收口规则**：`producer-readback` 一旦出现
`MISMATCH`，脚本会记数并在末尾 `exit 98` —— **对照没对上不是"仅供参考"，是硬失败**。

---

## 5. 读数汇总

### 5.1 Windows（`b4e8cf2`，`stelQuickUI.exe 28884480 B / md5=838A7B…`，原生 Vulkan）

**18/18 rc=0**。逐项：

| 套件 | rc | 读数 |
|---|---|---|
| `clockcheck` | 0 | VERDICT=PASS |
| `actioncheck` | 0 | VERDICT=PASS |
| `searchcheck` | 0 | VERDICT=PASS（含 T24 拼音 `PINY-01..03`、T26 召回 `SRC-12` **全量 1573** vs 旧 cap=3 的 16） |
| `locatecheck` | 0 | 判据 15/15（含 LOC-09 换选归零对照） |
| `locate-uicheck` | 0 | 判据 10/10 |
| `timecheck` | 0 | 判据 21/21（含 TC-16 换历边界、TC-17 非法时区被拒、TC-18 时区成对、TC-19 速率接实况、TC-20 四档跳档） |
| `timeuicheck` | 0 | 判据 19/19 |
| `returnuicheck` | 0 | **判据 11/11**（首轮 9/11） |
| `replaycheck` | 0 | 判据 11/11（I-REP-02 全流程回放） |
| `interactcheck` ×5 | 0 | **16/16 ×5**（首轮 15/16 ×5） |
| `a2-vulkan` | 0 | **探针 12 项失败 0**，逐像素偏差全部 0（唯一非零：半透明块 α=128 偏差 22，在容差内） |
| `dyn` engine ×3 / test ×3 | 0 | **6/6 rc=0**，且每次 `producer-readback: requested=… observed=… OK`（见 §4 末） |

### 5.2 macOS 复验（插桩改动后，`39284312 B / md5=daf5c818…`，Metal）

| 项 | 结果 |
|---|---|
| `interactcheck` ×5 | **16/16 ×5** |
| `returnuicheck` | 11/11（前导门：窗口已激活，重试 0 次） |
| `searchcheck` / `actioncheck` / `locatecheck` / `locate-uicheck` | 全 rc=0 |
| `timecheck` / `timeuicheck` / `replaycheck` / `clockcheck` | 全 rc=0 |
| `a2-metal` 逐像素 | rc=0 |
| DYN engine 5/5 + test 5/5 | **两路生产者回读正确**，真引擎 49.8 fps |

环境注记：`displaysleep=2` 分钟（脚本内置 `wake_display()`）；
`mdbulkimport`（Spotlight 批量索引）当时在跑。

### 5.3 关键对照：同一个统计在两侧都成立

| 量 | macOS（Metal） | Windows（原生 Vulkan） |
|---|---|---|
| INTERACTCHECK | 16/16 ×5 | 16/16 ×5 |
| RETURNUI | 11/11 | 11/11 |
| 引擎 dp 输出 | `2560x1440 dppp=2` | `1280x720 dppp=1` |
| 拖拽位移读数 | 18.1988° | 18.1988° |
| 捏合 FOV | 60→30.72→60 | 60→30.72→60 |
| L 键 timeRate | 0.1→1 | 0.1→1 |

⚠️ 注意 `dppp` 不同（2 vs 1）⇒ **两侧的坐标映射口径本来就不同构**（T27 的教训），
而判据在两侧都过 ⇒ 说明**判据没有偷偷依赖某一侧的 dppp**。这是本轮的一份额外信心。

---

## 6. Windows 30 分钟长跑

**rc=0，`VERDICT=PASS`，SL-C01..SL-C11 全 PASS**（warmup 900s + measure 1800s，与
W-T13 / W-T17 同口径；`elapsed_min=45.23`）。

```
[start] 2026-09-29T12:20:21 warmup=900s measure=1800s
repo HEAD = b4e8cf2
exe md5   = 838A7B55BB0DB8654EF48813CB02F601
producer  = engine
backend   = native Vulkan (no STELQUICK_GRAPHICS_API override)
STELLRUN: 生产者=engine(真实引擎)
[done] 2026-09-29T13:05:35 rc=0
```

| 判据 | 读数 |
|---|---|
| SL-C01 生产者零失败 | 测量段渲染 **89 999 帧 / 失败 0**；稳态实测 **50.00 fps**（下限 40） |
| SL-C02 稳态显示帧率 | **50.00 fps**（下限 40）；显示帧号 75 043 → 134 943（窗口 1198 s） |
| SL-C03 上屏间隔 | 61 235 帧：mean **19.60** / p50 19.98 / p95 **21.23** / p99 **22.07** / max 35.59 ms（门槛 p99 ≤ 60.00） |
| SL-C04 邮箱丢弃 | 投递 89 999 / **丢弃 0**（须 0） |
| SL-C05 上传耗时 | 89 999 次，增量均值 **0.571 ms**（< 5.0），最坏 11.31 ms（< 100） |
| SL-C06 邮箱帧龄 | 最大 **15 ms**（< 500） |
| SL-C07 内存斜率 | phys_footprint **0.080 MiB/min**（上限 1.0）；窗口内 1423.9 → 1425.3 MiB（增 **1.4**） |
| SL-C08 环境漂移 | **0 次违规**（每 60 s 复核） |
| SL-C09 窗口暴露 | **1800 / 1800** 个采样点 |
| SL-C10 降级误报 | **0 / 1800**（0.000%，上限 1%） |
| SL-C11 稳态帧龄 | 10 971 个样本：mean 3.17 / p95 **13.00** / p99 15.00 / max 16.00 ms（门槛 p95 ≤ 100） |

**与历史基线对照**：

| 量 | W-T13（`0f2faae`） | W-T17（`6bce85d`） | **W-T29（`b4e8cf2`）** |
|---|---|---|---|
| 稳态 fps | 50.00 | 50.00 | **50.00** |
| 内存斜率 | 0.09 MiB/min | 0.09 MiB/min | **0.080 MiB/min** |
| VERDICT | PASS | PASS（SL-C01..C11 全绿） | **PASS** |

⇒ 三次数值同档，**W-T29 不退化**。原始证据：
`docs/evidence/2026-09-29-w-t29/longrun/t29w-win-30min.{out,head,err,rc}.txt`
+ 逐秒 `t29w-win-30min.csv.gz`（2699 行）+ 逐帧 `t29w-win-30min.frames.csv.gz`（138 015 行）。

---

## 7. 为什么**不再**跑 macOS 的 S3 旧宿主回归

本轮改动面 = `src/ui/main.cpp` **（纯自检仪器）** + `tools/*.sh` + `tools/windows/*.ps1` + 文档。

- `S3`（旧宿主 `stellarium` 的回归）针对的是**两形态共用的引擎路径**。本轮的
  `uiFocusSnapshot` / `uiWindowActivationGate` 只被 `runUiReturnCheck` /
  `runUiInteractCheck` 调用，这两个套件**只在合流形态**（`stelQuickUI`）里跑。
- `stellarium.exe` 在 Windows 侧**确实重新链接了**（构建日志可见），但 md5 与改动前
  **逐位相同**（`66C51B61…`）——这是"改动未触及旧宿主"的**字节级证据**，
  比"跑一遍没报错"更强。

---

## 8. 移交给后续

| 事项 | 状态 |
|---|---|
| ~~Windows 侧 DYN engine ×3 + test ×3~~ | ✅ 已完成：**6/6 rc=0**，六次生产者回读全 `OK`（`docs/evidence/2026-09-29-w-t29/dyn-two-way/`） |
| ~~Windows 侧 30 分钟长跑~~ | ✅ 已完成：**rc=0，SL-C01..SL-C11 全 PASS**，50.00 fps / 0.080 MiB·min⁻¹（§6） |
| `i18n`：Windows 侧中文日志在 Mac 侧阅读 | ✅ 已解决（UTF-8 直落盘） |
| W 支线其余（SL 长跑判据） | ✅ 本轮的 30 min 长跑已覆盖（SL-C01..C11） |
| T29 的 DYN 结论降级 | ✅ 已记 `CORRECTION.md` |
| 技能沉淀 | ✅ `uu-remote-windows`（foreground lock / 跑批口径 / `.ps1` 纯 ASCII）；`qt-quick-vulkan-macos` §36（跨平台复验）/ §37（引擎输入面） |
| **Windows 侧后续**：以后每次跨平台复验 | 直接用 `tools/windows/wt29-suites.ps1`（含回读）+ `tools/windows/wt29-longrun.ps1`；**投递必须走 `schtasks /it`** |
| **A4 剩余（均非功能缺口）** | 时间页视觉层 / 套件 IT-06 环境门短路 / 两段式 Esc 特性 / 时间链路脱钩线索 / 捏合后首击失灵线索 / DYN 停摆根因 —— 见计划文档 §9.4.14 移交表 |
