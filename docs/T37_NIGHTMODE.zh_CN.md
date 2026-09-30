# T37 — 设置页骨架 + 夜视闭环（主题/夜视效果层定性）

> 任务来源：A-1.0 范围表「主题/夜视」列为**必须**，备注里带一句**前提性假设**：
> 「天空夜视效果仍由旧后处理负责，避免叠加两次」。这句话在合流形态**从未被验证过**。
>
> **最终结论（经三轮反转）**：
> ① 引擎旧夜视后处理 `NightModeGraphicsEffect`（挂 QGraphicsItem 的**纯 OpenGL**
> shader + QOpenGLFramebufferObject）在合流形态**物理不可达** —— 实验证明禁用该
> effect 行为不变。⇒ 夜视由 **Qt Quick 侧单一实现**（`keySink.layer` + `.qsb`
> ShaderEffect，公式逐字复刻引擎 `lum=max(r,g,b)→(lum,0.3lum,0)`）——
> 这正是 A-1.0「夜视效果只做一次、避免叠加两次」的**正确落地**。
> ② 引擎**自己对夜视有既有渲染反应**（原版语义，不是缺陷）：三处大气类
> `if (getVisionModeNight()) return;` ⇒ 夜视开启时**大气整层退场**；旧宿主靠 GL
> effect 滤红、合流形态靠 Qt Quick 滤镜承担红移，各做各的、互不重复。
>
> ⚠️ 这条结论不是一次测对的：**第一轮探针的头条读数是假绿，第二轮把引擎的正常
> 语义误判成了缺陷**。两轮错误同病根 —— 等待挂错步 / 判据的隐含前提没被隔离
> （沉淀为 TRAPS 69）。本文按三轮的先后写，错误过程本身就是证据。

改动面：`src/app/NightModeProbe.{hpp,cpp}`（**新**，探针，只报读数）｜
`src/app/FrameCompare.{hpp,cpp}`（**新**，探针与判据**同一把尺子**）｜
`src/app/NightModeCheck.{hpp,cpp}`（**新**，判据 NC-01..NC-05）｜
`src/ui/shaders/nightmode.frag`（**新**，GLSL 源，构建期 qsb）｜
`src/ui/qml/MainWindow.qml`（夜视 layer + ShaderEffect）｜
`src/ui/quick/BackendInfo.{hpp,cpp}`（负控属性 `nightEffectOff`）｜
`src/ui/quick/SkyViewport.cpp`（trace 上限可配）｜
`src/ui/main.cpp`（接线 `STELQUICK_NIGHT_PROBE` / `STELQUICK_NIGHT_CHECK`）｜
`src/ui/CMakeLists.txt`（`qt6_add_shaders`）｜`tools/t37-verify.sh`（**新**）。

---

## 1 开工前的代码审计（推断，不是证据）

| 环节 | 出处 | 合流形态下的命运 |
|---|---|---|
| 夜视本体 = `NightModeGraphicsEffect`（GL shader + FBO） | `StelMainView.cpp:166`，`setGraphicsEffect` 于 `:991-994` | `QGraphicsEffect::draw()` 只在 **QGraphicsView 场景渲染循环**里被调；合流宿主是 `WA_DontShowOnScreen`（`LiveSkyRuntime.cpp:79`）⇒ **不被调用** |
| 触发链 = `setVisionModeNight()` → `visionNightModeChanged` → `updateNightModeProperty` | `StelApp.cpp:1284` / `StelMainView.cpp:1091` | connect 链**活着**（`STELLARIUM_GUI_MODE=Standard`，未定义 `NO_GUI`）⇒ `setEnabled()` 确实执行，但执行了也白执行 |
| 帧生产 = `LegacySkyHost` 自建 FBO + `StelApp::draw()` | — | `StelApp::draw()` 路径里**没有任何夜视分支**（逐行核过） |
| 图形栈 | GL effect vs Qt Quick **Metal RHI** | 两条独立栈 |

⇒ 推断候选：**夜视对读回帧零影响** ⇒「叠加两次」的顾虑不存在 ⇒ 夜视须在
Qt Quick 侧自实现。**但推断不是证据**，必须实测（T34 立的规矩：先探针再写 UI）。

---

## 2 第一轮（T37-A 探针）：头条读数是**假绿**

仪器：`NightModeProbe`（`STELQUICK_NIGHT_PROBE=1`），冻结仿真后用**两条独立读回
路径**（`FrameMailbox` 原始 RGBA / `QQuickWindow::grabWindow()`）测夜视 OFF↔ON
像素差异，星座线 OFF↔ON 作判别性对照。只报读数、不下 PASS/FAIL。

探针报出的头条：「**上游 OFF↔ON 逐位相同**（帧号 178 == 178）」⇒ 顺势得出
「引擎夜视反应不存在，Qt Quick 侧自实现即可」。

**这是假的。** 帧号相同就是铁证：S5 的 900ms 等待挂在 `delayAfter`（**读步之后**），
S5 抓帧紧跟 S4 翻转 ⇒ **读到的是翻转前的旧帧**（TRAPS 5/69 同族）。探针当场留了
帧号，才可能在下一轮翻案 —— 探针报帧号不是装饰。

---

## 3 第二轮（T37-C 判据 + 静态性观测）：把引擎语义误判成「缺陷」

修掉等待位置后（S4/S6 写入步等待、S5/S7 读步零等待），判据立刻读出**新事实**：
翻转夜视 ⇒ **引擎上游帧非黑占比 1.0 → 0.0003（确定性黑帧）**。且与 QML layer
无关（`STELQUICK_NIGHT_EFFECT_OFF=1` 恒关效果仍复现）。

当时的初判：「引擎夜视反应有害，撕裂帧/黑帧缺陷」⇒ 用户拍板方案 A
（**抑制引擎对夜视的渲染侧反应**）。

**方案 A 动手前先做判别实验**（这是纪律：动引擎前先证明病灶真的在引擎）：
对照动作从星座线换成 `actionShow_Atmosphere`（把「大气」从被测变量变成对照变量），
逐腿抓帧落盘。结果三连：

1. **探针/判据的基帧是白天亮天空** —— 场景初始时刻在白天、大气开着。「星星消失」
   是误判：基帧里**本来就没有星星**（白昼亮度模型压着）；
2. **夜视开 + 大气关 = 漂亮星空 + 银河 + 星名标签**
   （`probe-frames/night-upstream-b.png`，Peacock/Alnair 清晰可见）⇒
   **引擎夜视渲染无病**；
3. 「黑天空」= 夜视 → 大气退场（三处 `if (getVisionModeNight()) return;`，
   `AtmosphereLightweight.cpp:735` / `AtmospherePreetham.cpp:395` /
   `AtmosphereShowMySky.cpp:766`，**原版语义**）+ 白天星星被白昼亮度模型压住。

⇒ **不存在「撕裂帧」缺陷。方案 A 不需要执行**（用户选的 A 被判别实验推翻 ——
推翻的不是用户的选择标准，而是 A 的前提：引擎反应根本无害）。
黑帧取证：`probe-frames/nightcheck-nc05-anom-up.png`（近全黑 + 微弱地平辉光，
即「夜视开 + 大气开 + 白天」的真实引擎输出）。

### 3.1 三轮反转的账本

| 轮 | 读数 | 当时结论 | 错在哪 |
|---|---|---|---|
| ① 探针 | 上游 OFF↔ON 逐位相同（帧号 178==178） | 引擎无夜视反应 | 等待挂读步 ⇒ 读到旧帧（假绿） |
| ② 判据 | 翻转夜视 ⇒ 上游变黑帧 | 引擎夜视反应有害 | 没隔离「大气退场」这个正常语义；基帧是白天亮天空 |
| ③ 判别实验 | 大气置关后：夜视 OFF↔ON 上游低于噪声容差；夜视开+大气关=正常星空 | **引擎无病；A-1.0 的正确落地 = 现状** | —— |

---

## 4 产品实现（T37-B）

### 4.1 落点：`MainWindow.qml` 的 `keySink.layer`

```qml
layer.enabled: {
    appFacade.displayTogglesRevision;          // T15 铁律：必须真读 token，绑定才会重算
    if (BackendInfo.nightEffectOff)
        return false;                          // T37-C 负控开关
    return appFacade.actionChecked("actionShow_Night_Mode");
}
layer.effect: ShaderEffect {
    fragmentShader: "qrc:/StelQuickUI/shaders/nightmode.frag.qsb"
}
```

- 挂 `keySink` = 与引擎「挂 rootItem（整个视图）」同语义：**天空与 UI 一起变红**；
- 公式**逐字复刻**引擎 `NightModeGraphicsEffect`（`StelMainView.cpp:186-194`）：
  `lum = max(max(r,g), b); out = (lum, lum*0.3, 0)` —— 红光暗视，保护暗适应；
- 引擎状态面照旧走 `actionShow_Night_Mode`（T34 工具栏已接），**产品侧零语义复制**。

### 4.2 🔴 Qt 6.11 的 ShaderEffect 只认 `.qsb`

`fragmentShader` 不再接受内联 GLSL —— 必须指向 qsb 预编译产物，且失败形态不是
「效果不生效」而是**整个 layer 不渲染 ⇒ 全白屏**。构建侧：

```cmake
find_package(Qt6 QUIET COMPONENTS ShaderTools)
if(TARGET Qt6::ShaderTools)
    qt6_add_shaders(stelQuickUI "stelquickui_nightmode_shader"
        PREFIX "/StelQuickUI" FILES shaders/nightmode.frag)
else()
    message(WARNING "未找到 Qt6::ShaderTools → 夜视 layer 会渲染失败（全白屏）…")
endif()
```

ShaderTools 缺失时给**显式告警** —— 全白屏的唯一线索。

---

## 5 判据 `NIGHTCHECK`（NC-01..NC-05，`STELQUICK_NIGHT_CHECK=1`）

**S1 布场**（收尾全还原）：冻结仿真（`setSimScale(0)`，帧泵照常出帧、只冻 JD）
+ 对照量置关 + **大气置关**。大气关掉的原因：引擎夜视反应里唯一动帧内容的就是
大气退场 —— 关掉它，NC-03① 的前提才成立。

| 判据 | 内容 | 承重点 |
|---|---|---|
| **NC-01** 状态面 | `trigger(actionShow_Night_Mode)` ⇒ **引擎 getter** `getVisionModeNight()` 翻转 | 独立回读引擎 getter，**不经** `actionChecked` 复述（陷阱 43） |
| **NC-02** 噪声底门 | 冻结后同状态连取两帧，上游/下游都**低于噪声容差**（占比<0.1% ∧ 通道差≤2） | 口径有效性门。⚠️ 不能用「逐位相同」—— 冻结下引擎渲染有亚 LSB 抖动（实测上游 0.029%/Δ1、下游 0.019%/Δ1）。超限 ⇒ 整套 **UNAVAILABLE**（环境门第三态，不记 FAIL） |
| **NC-03①** 上游不变 | 夜视 OFF↔ON：上游低于噪声容差 | 大气已关 ⇒ 引擎夜视反应成 no-op。将来有人把滤镜挪进引擎、或引擎夜视反应扩大，这里**立刻红** |
| **NC-03②** 下游效果 | 下游**视口内**差异像素 ≥90% ∧ 平均通道差 >1.0 | 滤镜真的生效。⚠️ 均值门不能用 50：暗星空下滤镜只显著改写星点/线条，全帧均值实测 ~14.5。**伪证守门 = NC-03①**：上游不变时下游任何变化只能来自滤镜（黑帧伪装成效果的通道被堵死 —— 黑帧差异量级 ~157 ≈ 滤镜 ~162） |
| **NC-04** 判别对照 | 同一套取帧+比较方法，对对照量 OFF↔ON ⇒ 上游差异**超出**噪声容差 | 证明 NC-03① 的「上游=0」不是「比较方法测不出东西」的假绿。⚠️ 等待挂**写入步**（S6 `delayAfter=900`） |
| **NC-05** 往返复原 | 还原后：getter 回初值 ∧ 复原帧 vs 参考帧B **无全帧级红移**（占比<0.5 ∧ 均值<20；滤镜没关的签名是 ~100%/~150） | 效果可逆、一次夜视不是永久损伤。已知有界容忍：星线 fader 在仿真冻结期间停中间态（~1.5% 余辉，**T37-X4 另案**）。抓到异常帧 ⇒ **INCONCLUSIVE**，不记 FAIL |

退出码：`0=PASS`、`10=FAIL`、`6=UNAVAILABLE/INCONCLUSIVE`（竞态型读数不硬判）。

---

## 6 负控（证明判据承重）

`STELQUICK_NIGHT_EFFECT_OFF=1` ⇒ `BackendInfo.nightEffectOff` ⇒ QML
`layer.enabled` 强制 false ⇒ 效果物理不生效 ⇒ **NC-03② 必红（且只它红）**。

实证方式同 T33/T34：注掉机制 → 必须看到对应判据 FAIL，否则判据是摆设。
没有这条负控，「QML 滤镜被悄悄删掉 / shader 路径写错」这类缺陷只能靠 NC-03①
的上游零差异碰运气 —— 那是「引擎侧没画」，测不出「UI 侧没画」。

---

## 7 实测结果（macOS，Metal/MoltenVK，2026-09-30）

产物 `stelQuickUI` **40304760 B，`md5=2c900daa33540b46f753f05ef3509db2`**
（QML 注释修正后的同源重建；此前验证轮 `14fd1cfb1e328264e9ee52c506f8541d`）。

| 场景 | 结果 |
|---|---|
| 正题 ×3 | `rc=0`、**6/6 PASS**、红项 `[]` |
| 负控 ×3 | `rc=10`、5/6、红项**恰好** `[NC-03②]` |

关键读数（run1）：NC-02 上游 0.029%/Δ1、下游 0.019%/Δ1；NC-03② 视口内
比例 1.0000、平均通道差 14.45；NC-04 差异像素 8693/921600（0.943%，门 0.1%）；
NC-05 占比 0.0000 / 均值 0.00。

**全量回归**：13 套件 + `INTERACTCHECK 18/18` + S3 旧宿主 + A2 + DYN 双路
3/3+3/3 全 `rc=0`；脚本层判据 2/0，`SCRIPT-RC=0 FAILED=0`。
（⚠️ 本机当时有 Spotlight 批量索引（`mdbulkimport`）在跑 —— 它是 T37-X2 撕裂帧
与 T37-X3 引导挂死的实测负载毒源，脚本带环境注记与看门狗。）

---

## 8 本轮新血泪（已入 `TRAPS.md` 69/70）

### 陷阱 69 🔴 「效果判据」的三种伪装

1. **等待挂读步** ⇒ 翻转后抓到旧帧 ⇒「逐位相同」假绿（第一轮头条）。修法：等待挂
   **写入步**；效果帧**必须打印帧号并断言比基线大**；
2. **效果判据可被「内容消失」伪装** —— 黑帧差异量级（~157）与滤镜效果（~162）同
   量级 ⇒ 单看下游差异分不出「滤镜生效」和「画面没了」。修法：NC-03① 伪证守门
   （上游不变时，下游变化只能来自滤镜）；
3. **冻结 ≠ 逐位相同** —— 引擎渲染有亚 LSB 抖动 ⇒ 口径用「低于噪声容差」，
   别用「逐位相同」。

### 陷阱 70 🔴 ad-hoc 内联命令漏传模式变量 ⇒ 症状面目全非

排查期用内联命令跑 app，漏传 `STELQUICK_NIGHT_CHECK=1` ⇒ 程序落回**普通 GUI 模式**
（设计上永不退出）⇒ 症状是「引导门挂死」（T37-X3 幻影）。铁证手法：拿**日志行**
反推运行模式 —— 日志停在 `A2: 静态测试图案投递失败`（位于自检 `_exit` **之后**的
代码路径）⇒ 自检模式物理走不到。⇒ **跑自检一律走 verify 脚本**，别手敲 env。

### 附：仪器教训

- **探针的值别进判据**：boot 初值会落盘、逐跑可变 ⇒ 判据只认结构性事实（帧号、
  getter、哈希关系）；
- `QString::arg` 是 `%1…%n`，`%.6f` 是**字面量** ⇒ 占位符错位、后续内容全丢；
- steps 由 QTimer 异步执行 ⇒ 栈上 lambda 按引用捕获**悬垂**，改自由函数；
- 大气/白昼亮度模型是**隐含前提**：基帧里有啥先看一眼，别把「本来就没有」
  当成「消失了」（同陷阱 44：判据的前提可能只在当前环境里成立）。

---

## 9 A5 里的位置与后续

A-1.0 范围表相关行（原文）：

> 「主题 / 夜视 | 最小 | **必须** |（备注）天空夜视效果仍由旧后处理负责，避免叠加两次」

⇒ 前提假设被**探针+判别实验证伪并重新落地**：旧后处理物理不可达、「叠加两次」
顾虑不存在、夜视由 Qt Quick 侧承担**唯一一次**红移。**本项完成**。

A5 余下：

| 序 | 内容 | 备注 |
|---|---|---|
| T38 | 显示参数：星等/亮度 + 视场 + 投影 | |
| T39 | 高 DPI + 渲染诊断（capabilities/status 预留） | |
| T40 | 快捷键编辑 | 「中文输入焦点不能触发天空快捷键」已由 T29 兜住 |
| T41 | 帮助 / 版本 / 许可证页 | |
| T42 | 错误页 + 「未支持项」清单 | 旧 UI **禁止**作为隐式回退 |

**本任务移交的线索**：

- **T37-X4**：仿真冻结期间 fader 动画停摆（星线切换后卡半亮度，3s 不衰减，
  NC-05 的有界容忍来源）⇒ 疑 fader dt 取自被冻结时钟或宿主 update 调度问题；
- **T37-X1**：夜视翻转引起 5 个工具栏按钮重绘（疑 T34 绑定首算 race）—— 功能
  正确、纯性能线索；
- **Windows 复验（W-T37）**：待排期。T33/W-T35 已证明跨平台复验能逼出真缺陷
  （`flagUseCTZ` 判据前提只在用户 config 里成立，TRAPS 44）。

---

## 10 产物清单

**新增**

- `src/app/NightModeProbe.hpp` / `.cpp`（探针，只报读数）
- `src/app/FrameCompare.hpp` / `.cpp`（帧采样/逐像素比较共用工具 —— 探针与判据
  同一把尺子，防口径漂移；DiffStats 含 bbox + 高度 8 带）
- `src/app/NightModeCheck.hpp` / `.cpp`（NC-01..NC-05）
- `src/ui/shaders/nightmode.frag`（GLSL 源，构建期 qsb）
- `tools/t37-verify.sh`（`pos N` / `negctl` / `regress` / `all N`；看门狗 +
  Spotlight 环境注记）
- `docs/T37_NIGHTMODE.zh_CN.md`（本文）
- `docs/evidence/2026-09-30-t37-nightmode/mac/`（`nightcheck-run1..3`、
  `negctl-effectoff-run1..2`、`regression-*`、`rc-summary.txt`、
  `probe-frames/`（黑帧实锤 / 夜视开+大气关正常星空 / 判别对照帧））

**修改**

- `src/ui/qml/MainWindow.qml`（夜视 layer + ShaderEffect + 前提注释）
- `src/ui/quick/BackendInfo.hpp` / `.cpp`（负控属性 `nightEffectOff`）
- `src/ui/quick/SkyViewport.cpp`（`STELQUICK_A2_TRACE_LIMIT` 可配）
- `src/ui/main.cpp`（`STELQUICK_NIGHT_PROBE` / `STELQUICK_NIGHT_CHECK` 接线；
  NIGHTCHECK 退出码 PASS=0 / FAIL=10 / UNAVAILABLE·INCONCLUSIVE=6）
- `src/ui/CMakeLists.txt`（六个新源文件 + `qt6_add_shaders` + ShaderTools 缺失告警）
