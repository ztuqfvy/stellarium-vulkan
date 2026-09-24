# T18 交付报告 — 定位与跟踪（A4 固定流程的"定位环"）

日期：2026-09-24 · 提交：见仓库 `T18` 提交
证据包：`docs/evidence/2026-09-24-t18-locate-track/`（README 为索引）
一键复跑：`./tools/t18-verify.sh all`

---

## 1. 任务与通过条件

A4 的通过条件是固定流程 **I-REP-02「开机→搜月球→定位→改时间→返回」全程可用**。
T17 交付了两个模型（搜索→选择→信息页），**流程里的"定位"这一环还是空的** ——
选中天体之后视向不会动。计划文档 §9.4.1 把"定位与跟踪"从 A4 清单提到最前，T18 就只做这一环。

自定的通过条件（都可判、都能自动跑）：

| # | 条件 | 为什么这么定 |
|---|---|---|
| 1 | 能把视向对准选中天体，且**能量出"对准了"** | "看起来像对准"不是判据 |
| 2 | 推进仿真时间后夹角仍然 ≈ 0（**持续跟随**） | 一次性归中和"锁住"是两件事 |
| 3 | 解除跟随后同样推进，夹角**必须显著偏离** | 判别性对照——否则条件 2 可以被假绿 |
| 4 | 家园行星守卫在本层同样生效，**理由可读** | 引擎在多处都做了这条判断，不能漏 |
| 5 | 重复选中**同一对象**不会静默打断跟踪 | 引擎固有行为，必须在本层挡住 |
| 6 | **QML 接线端到端可用**（从最外层注入真实鼠标事件） | C++ 自检走不到 `onClicked`，会留一个洞（§3 缺陷 ④） |
| 7 | 全部判据自动化、一键复跑；回归零退化 | 与前几个任务同一纪律 |

范围外（明确移交 A4，§6）：排序策略、GUI 时钟对话框、QML **视觉**层面的验收
（布局审美、文案措辞）——条件 6 只覆盖"接线是否活着"，不覆盖"好不好看"。

---

## 2. 设计

### 2.1 先读源码：定位与跟踪在引擎里是**两件事**

| 引擎接口 | 行为 |
|---|---|
| `moveToObject(obj, dur)` | **只做一次平滑移动**，`dur` 秒后停 |
| `setFlagTracking(true)` | **兼做移动 + 持续锁定**（`StelMovementMgr.cpp:1386-1405` 内部就调 `moveToObject`） |

自动移动时长 `autoMoveDuration` 默认 **1.5 s**（`StelMovementMgr.cpp:114`，可被
`navigation/auto_move_duration` 配置覆盖）。

⇒ 本层把两者做成一个参数：`locateSelected(track=true)` = 移动+锁定（等价旧 GUI 的空格键）；
`locateSelected(track=false)` = 只移动。QML 上分别对应"定位并跟踪"与"取消跟踪"两个按钮。

顺带发现一条**安全性质**：`setFlagTracking(false)` 的第一分支是
`if (!b || !objectMgr->getWasSelected())`——即"关跟踪"**天然幂等且不依赖当前有选中**。
所以 `setTracking(false)` 可以无条件走引擎，不需要本层再加守卫。

### 2.2 家园行星守卫：复用引擎的判据，不发明等价关系

选中天体就是"脚下这颗星球"时不能把视线对准它。引擎在至少 6 处做了同一判断，
口径完全一致（`SearchDialog.cpp:1438/1458/1478`、`AstroCalcDialog.cpp:2641/8689/9218`）：

```cpp
if (newSelected[0]->getEnglishName() != core->getCurrentLocation().planetName)
```

注意是 **`!=` 严格比对（区分大小写）**，而且比的是**英文名**而不是 ID
（`StelLocation::planetName` 存的本来就是行星英文名）。本层**照抄这条关系**，
不引入 case-insensitive / 别名映射之类的"更宽松等价"——那会让本层与旧 GUI 行为分叉。

处置：

```cpp
bool AppFacade::isHomePlanet(const QString &objectEnglishName, const QString &locationPlanetName)
{
    if (objectEnglishName.isEmpty() || locationPlanetName.isEmpty())
        return false;   // 取不到名字时判"不是"：宁可漏判，也不误拒
    return objectEnglishName == locationPlanetName;
}
```

**为什么抽成 `static` 纯谓词**：这样自检可以在"家园行星当前根本搜不到"的机器上
照样验证守卫逻辑（`LOC-01` 四情形：同名拒绝 / 异名放行 / 空串不误拒 / 大小写严格），
不必依赖运行环境里恰好能检索到 Earth。端到端那一环另有 `LOC-08`。

### 2.3 🔴 跟踪标志泄漏：为什么 `isTracking()` 必须读**合取真值**

这是 T18 读源码时发现的一条**引擎侧缺陷**，也是本层唯一"看起来多余"的设计的真正理由。

```cpp
// StelObjectMgr.cpp:537-544
void StelObjectMgr::unSelect(void)
{
    if(!lastSelectedObjects.isEmpty())
    {
        lastSelectedObjects.clear();                                   // ← 先清
        emit selectedObjectChanged(StelModule::RemoveFromSelection);   // ← 后发信号
    }
}
```

```cpp
// StelMovementMgr.cpp:757-766
void StelMovementMgr::selectedObjectChange(StelModule::StelModuleSelectAction)
{
    if (objectMgr->getWasSelected())      // getWasSelected() = !lastSelectedObjects.empty()
    {
        if (getFlagTracking()) setFlagLockEquPos(true);
        setFlagTracking(false);           // ← 清跟踪只在这个分支里
    }
}
```

`unSelect()` **先**清列表**再**发信号，于是槽里跑的那一刻 `getWasSelected()` 已经是
`false` ⇒ **整个 `if` 块被跳过 ⇒ `setFlagTracking(false)` 永不执行 ⇒ 标志泄漏**
（清掉选中之后，引擎自己的 `getFlagTracking()` 仍然返回 `true`）。

处置：**不改引擎**（跨 GUI/动作表/脚本的改动面太大，超出 T18 范围），本层读合取真值：

```cpp
bool AppFacade::isTracking() const
{
    return mv->getFlagTracking() && mgr.getWasSelected();
}
```

**这条判定是被实测逼出来的，不是猜的**：自检 `LOC-08b` 同时打印两个值，
精确复现了泄漏——`清前 合取=true/引擎原始=true → 清后 合取=false/引擎原始=true`。
也就是说，如果 T18 图省事直接转发 `getFlagTracking()`，UI 会在"已取消选中"之后
依然显示"跟踪中：X"。

### 2.4 幂等闸：引擎在**每一次**选择变化时都无条件关跟踪

同一段 `selectedObjectChange()` 里，`setFlagTracking(false)` 是**无条件**执行的。
后果：**"重选同一个对象"会静默取消正在进行的跟踪** —— 用户看到的是"跟踪自己断了"。

触发路径不止一条：QML 列表重入、信息页刷新、回到列表再点一次同一条结果……

处置：在 `ObjectInfoModel::selectByStableId` 加幂等闸——已选中同一 `(type, id)` 时
**一次 `setSelectedObject` 都不发**，只 `emit selectionChanged()` 让 QML 知道"命令收到了"，
然后照常 `refresh()`：

```cpp
const QList<StelObjectP> &cur = mgr.getSelectedObject();
const bool alreadySame = !cur.isEmpty() && cur.first()
                         && cur.first()->getType() == type
                         && cur.first()->getID() == id;
if (!alreadySame) { if (!mgr.setSelectedObject(obj)) { resetToEmpty(); return false; } }
else { emit selectionChanged(); return refresh(); }
```

判据用 `(type, id)` 而不是 `stableId` 字符串：与 `selectByStableId` 进来的参数
是同一份语义（`splitStableId` 已保证两段非空），不做二次拼接。
对应判据 `LOC-07`。

### 2.5 判据必须成对：为什么孤立断言"夹角 ≈ 0"是假绿

"目标到视向的夹角 ≈ 0"这一个断言可以**假绿**，至少有三种反例：

| 反例 | 为什么夹角也会 ≈ 0 |
|---|---|
| 世界根本没动 | 仿真时间没推进，目标当然没跑掉 |
| 视向被锁在赤道坐标里 | 跟踪的是"赤道坐标里的那个方向"，天球一转反而对不上——除非时间没动 |
| 定到了别的对象上 | 夹角小但对象错了 |

所以判据设计成**成对 + 一个判别性对照**：

| 断言 | 门槛 | 实测 |
|---|---|---|
| **世界确实动了**：跳 0.25 天后目标 AltAz 变化 | **> 5°** | 127.67° / 127.19° |
| **视向跟着走**：跟踪中夹角 | **< 0.5°** | 0.0747° / 0.0475° |
| **对照组**：不跟踪时同样跳变后的夹角 | **> 5°** | 32.97° → 158.17° |

⇒ 三条一起才能说"确实在跟"。这也解释了为什么选 **0.25 天（6 小时）**这个跳变量：
它要求目标"中等自转即大幅移动"，同时不至于把目标甩到视野完全外。

**关于夹具选月球**：`LOC-fixture` 用 `Moon`，因为 A4 的固定流程就是"搜月球"。
月球离天极近，同样自转下 AltAz 变化极大（实测 127°），是这条判据最理想的载荷。

### 2.6 两个容易被误读的量级，已在自检里显式打印

**（a）"只归中不跟踪"后残留 33°/158° 不是缺陷。**
自检的 `simRate = 0.1 天/秒` ⇒ 墙钟 **1 秒 = 天空自转 36°**。自动移动 1.5 s 追完之后
视向就固定了，天空继续转，余量由此而来。`LOC-05b-note` 把这 2.5 s 等待期间的自转量
**显式测出来打印**（实测 **82.02°**），防止读者把"残留角"误读成"跟踪没生效"。
这条是 `note()`，**不计入判据数**，避免信息行被伪装成 OK。

**（b）跟踪分支还有一项视口中心偏移。**

```cpp
// StelMovementMgr.cpp:1264
double latOffset = viewportCenterOffset[1] * currentFov * M_PI_180;
lat += latOffset;
```

⇒ 期望夹角严格来说是 `|offset|` 而不是 0。自检把该值打出来（`LOC-info`，默认配置实测
**0.0000°**），让判定表达式的来源可查，而不是含糊地写"≈0"。

### 2.7 夹角口径：**刻意不复刻**被测量的换算

夹角在 **J2000 赤道系**里算：`obj->getJ2000EquatorialPos(core).angle(mgr->getViewDirectionJ2000())`。

为什么"不复刻"：跟踪分支内部用的是 `mountFrameToJ2000(getAltAzPosAuto(core))`，
而 `mountFrameToJ2000` 在 AltAz 挂载帧下就是 `core->altAzToJ2000(v, RefractionOff)`
（`StelMovementMgr.cpp:1537-1552`）—— **那串换算是被测量的对象，不该混进判据的输入**。
判据只需要"同一个参考系里比两个方向"，J2000 已经满足。

等价性另行核对过（写在代码注释里）：跟踪分支的输入是 `altAzToJ2000(AltAz 位置)`，
自检用的是 `getJ2000EquatorialPos()`，两者同系 ⇒ "期望夹角 0"这个推理成立。
**口径分离 + 等价性单独论证**，比"判据里抄一遍被测代码"更可信。

### 2.8 自检驱动器：线性步骤表，而不是"步骤体自己挂定时器"

自检是**有状态、跨多个定时器间隔**的（自动移动 1.5 s、归中等待 2.5 s、重采样 +1.5 s），
所以用一个线性步骤表 + 一个统一的 `tick`：

```cpp
struct Step { int delayAfter = 0; std::function<void(Ctx *)> body; };

auto tick = std::make_shared<std::function<void()>>();
*tick = [ctx, steps, idx, tick]() {
    if (*idx >= steps->size()) { ctx->finish(); return; }
    const Step s = steps->at((*idx)++);
    ctx->nextDelay = s.delayAfter;
    s.body(ctx);
    QTimer::singleShot(ctx->nextDelay, ctx->app, [tick]() { (*tick)(); });
};
```

关键设计点：**步骤体要"再等一会儿"时，改写 `ctx->nextDelay`，而不是自己再挂一个定时器**。
初稿就是后者——结果两条推进路径并存，同一个处理器跑两遍。

`LOC-08` 拿不到当前观察行星名时**照实 SKIP**（`note()` 不计入判据数），
不把"环境给不了"伪装成"验过了"。

### 2.9 QML 接线必须**从最外层**验证：为什么另立一套 UICHECK

`LocateCheck` 全程走 `AppFacade` 的 **C++ 公共 API**。它能证明"定位逻辑对"，
但**结构上证明不了** QML 里 `onClicked: appFacade.locateSelected(true)` 是活的——
名字写错、按钮没接上、信号名拼错，`LOCATECHECK` 照样 14/14 全绿。

项目已经在同一类坑上付过学费（MEMORY 的"验证陷阱 3"）：T15 把 `Keys` 挂在
`ApplicationWindow` 上，而 `Keys` 是 **`Item`** 的附加属性 ⇒ 挂载从未生效、QML
键盘路由整段死代码，`AC-5` 却抓不到（它直接调 `routeKey`）——埋了两个任务才被发现。

所以 T18 另立 `STELQUICK_UI_CHECK=1`，原则是**从最外层注入**：

| 步骤 | 做法 | 为什么不能省 |
|---|---|---|
| 1 | 按 `objectName` 找到**真实 QML 控件** | 找不到 ⇒ 接线断了；这是唯一能发现"控件不存在"的方式 |
| 2 | 向 `QQuickWindow` 投递**真实鼠标按下/抬起** | 直接 `emit clicked()` 或直接调 `AppFacade` 都测不到 `onClicked` 这段 |
| 3 | 断言**引擎状态真的变了**（`isTracking` / `trackedName`） | 证明 QML → C++ 方向活 |
| 4 | 反向读 QML 控件真实属性（`enabled` / `Label.text`） | 证明 C++ → QML 绑定活，而不是硬编码 |
| 5 | **负控**：向非按钮控件（状态 Label）中心投递同一次点击 | 否则第 3 步的 PASS 可能只是"点哪都算" |

8 条判据（UI-01..08）。一个细节：合成事件的坐标用 `mapToScene()` 从控件实时算出来
并打印进证据（本次 `@(425.5,541.0)`、按钮 `835×32`、窗口 `960×640`），
否则事后无法判断"点的是不是那个按钮"。

`uiLocatePickFixture` 复用与 `LocateCheck` 同一张候选表、同一套"按下标逐个比对名字"
的写法——因为引擎聚合结果按**名称字典序**排序，第 0 行不一定是想要的那个。

---

## 3. 落地过程中发现并修掉的问题

### 问题 ①（设计级）引擎的跟踪标志泄漏 —— 见 §2.3

不是 T18 引入的，是读源码发现的既有缺陷。处置是"本层读合取真值"，并把证据
（`LOC-08b` 同时打印两个值）固化进自检。**没有改引擎**——那是另一个任务的范围。

### 问题 ② 引擎的"无条件关跟踪"导致幂等重选静默断跟踪 —— 见 §2.4

同上，属引擎既有行为。处置在本层（`ObjectInfoModel` 幂等闸）。

### 问题 ③ 自检报告每条判据打印了**两遍**

第一版 `LocateCheck` 的 `mark()` / `note()` 同时做了两件事：`qDebug()` 实时打印
**和** `result.details.append()`；而 `main.cpp` 又在回调里把 `details` 打印一遍。
验证脚本把 stdout/stderr 合并重定向（`> file 2>&1`），于是证据文件里每条判据**成对出现**
（`locatecheck-mac.txt` 里 `LOC-01` 在 167 行和 185 行各一次）。

这个模式是从 T15 的 `AppFacadeCheck` 抄来的（`AppFacadeCheck.cpp:72-73` 同样是
`qDebug` + `append`），**T17 期的 `ACTIONCHECK` 证据里就已经是成对的**——
所以它是既有缺陷、不是 T18 引入的（已核对 T15/T16/T17 三份证据：每条恰好 2 次）。

处置：`LocateCheck` 改成 **只 `append`、由 `main.cpp` 单点打印**，与 T17 的
`SearchModelCheck` 风格一致（那才是"只需 append"的先例）。同时删掉因此不再使用的
`#include <QDebug>`。修复后 `locatecheck-mac.txt` 每条判据恰好 1 次（186 行，原来 202 行）。

**`AppFacadeCheck` 保持原样未动**：它是 T15 的代码、不在 T18 范围，且改动会降低与
T15/T16/T17 证据的可比性。已在证据 README 里记明它是"实时行 + 报告"两份，不是漏读。

### 问题 ④ DYN 回归 `rc=8`：**是判据协议问题，不是代码退化**

首轮全量回归里 DYN 出红：

```
D1-C02 FAIL 显示 尾窗稳态 0.0 fps（下限 20.0）；全程推进 0 帧
D1-C07 FAIL 降速窗内 degraded=false（须 true）/ 恢复后 degraded=false（须 false）
```

而 T15/T16/T17 三次同项都是 7/7 PASS。定性过程（完整记录在
`docs/evidence/2026-09-24-t18-locate-track/dyn-ab-baseline-vs-t18.txt`）：

| 实验 | 做法 | 结果 |
|---|---|---|
| 换生产者（低负载） | 同一份 T18 二进制，去掉引擎（替身生产者） | **PASS**（28.0 fps / 223 帧 / 降级正确）⇒ 该次 QML 侧没问题 |
| 换二进制 | `git stash` 掉 T18 全部改动 → 重建 T17 基线 → 跑 9 次；恢复 T18 → 重建 → 跑 9 次 | 基线 **6 PASS / 3 FAIL**；T18 **6 PASS / 3 FAIL** ⇒ 失败率相同（33%） |
| 换生产者（高负载复测） | 同日下午、同一份二进制，替身 vs 引擎 | 替身 **4/4 FAIL**、引擎 **0/3 FAIL** ⇒ 停摆**与引擎共存无关**（见下） |

⇒ **本机（M3 / macOS / Metal RHI）既有的环境敏感现象**：QML 侧场景图偶发/常发停摆
——生产者照跑 50 fps、邮箱帧龄个位数毫秒，日志里**没有任何**丢设备痕迹
（`vkDebug` / `VK_ERROR` / `device lost` / `swapchain` 全 grep 不到），
而显示侧推进 0~364 帧后停住。

⚠️ **同日 13:2x 复测把"替身必 PASS"推翻了**（高负载：Spotlight 批量索引 + 装包，
load avg ≈ 5.2；`displaysleep=2`）：替身同样全败，13:3x 两侧更是**全程 0 帧**，
且 `caffeinate -dimsu` 无效（最可能是**会话锁屏**，锁屏下没有 app 能拿到 drawable）。
**这反而给出比 A/B 更干净的论证**：DYN 用 `startPage="sky"`、替身又不 boot 引擎
⇒ 替身那一跑的**执行路径上不含 T18 的任何一个字节**，它照样失败
⇒ 与本次改动**在代码层面就无因果关系**，不必依赖统计推断。
完整记录见 `dyn-ab-baseline-vs-t18.txt` §6。

机械旁证（不依赖统计）：T18 对 `src/ui/main.cpp` 有 5 个 hunk（include /
`startPage` 三元式 / 两条 ActionRouter 注册 / 新增 `locateCheck` 分支 /
新增 `uiCheck` 分支），**DYN 分支逐字节未变**；`SkyViewport.*`、`DynFrameCheck.*`、
`LiveSkyRuntime.*` 一个字节都没碰。

读数采信规则：D1-C02/C07 量的是"窗口有没有在渲染"，前提是窗口被暴露。
**若同条件的替身对照也失败（尤其"全程 0 帧"），该读数反映的是"仪器测不到"，
不是"被测程序失败"** —— 不作为退化证据，但也不改写成 INVALID（原始 FAIL 照实保留）。

处置（判据协议，不是洗绿）：

1. `tools/t18-verify.sh` 的 DYN 段跑 3 次、如实报 `N/3`，**不跑"直到绿"**；
2. **零退化的依据是同口径 A/B 的失败率相同**，不是某一次的绿色；
3. 失败样本照实归档（`regression-dyn-engine-metal-run*.txt`），不丢弃、不改名成 INVALID；
4. 顺手拿到一条性质：本次三次里有一次是 `C02 FAIL 但 C07 PASS`（显示先跑了 343 帧、
   跨过降速窗之后才停），说明 **C07 不是 C02 的冗余项**。

Windows 侧同形态 30 分钟长跑的 `SL-C09 窗口暴露 = 1799/1799`、`SL-C10 降级误报 = 0/1799`
⇒ 该现象是**本机特有**，不是"引擎 + QML 同进程"的普遍缺陷。

### 问题 ⑤（过程教训）BSD grep 的 BRE 不支持 `|` 交替

排查时用 `grep -n "A|B" src/...` 得到的"无匹配"是**假的**——macOS 自带 grep 是 BSD，
BRE 里 `|` 是字面量，模式实际匹配的是字符串 `A|B`。因为这一点，
"`LocateCheck.cpp` 里没有 printf"这个中间结论错了两次，直到改用 `grep -E` 才看见
`qDebug()`（正是问题 ③）。**教训：在 macOS 上排查代码，一律用 `grep -E`。**

### 问题 ⑥ 交付文档只验了 C++ 侧，"QML 接线是否活着"是个洞

计划里 T18-C（QML 接线）原本的验收方式是"人工跑一眼 UI"。这是**不可复跑**的验收，
而且按项目"验证陷阱 3"的教训，"人工跑一眼"恰好是那种**看不见死代码**的检查方式
（T15 的 `Keys` 死代码当初也是人眼看着"像是对的"）。

处置：另立 `STELQUICK_UI_CHECK=1`，从最外层注入真实鼠标事件（设计见 §2.9），
把"接线是否活着"变成可复跑的 8 条判据；`SearchPage.qml` 给三个控件补 `objectName`
作为 C++ 侧的锚点。**这才是 T18-C 的真正验收手段**，人工看一眼降级为辅助（见 §6）。

---

## 4. 验收

### 4.1 自检：14/14 PASS（rc=0）

夹具 `Moon`（`Planet:Moon`）。判据明细见证据 README §2，关键读数：

| 判据 | 实测 | 门槛 |
|---|---|---|
| `LOC-03b` 自动移动结束后锁定夹角 | **0.0475°** | 0 ± 0.50 |
| `LOC-04 (a)` 跳 0.25 天后目标 AltAz 变化 | **127.67°** | > 5.0 |
| `LOC-04 (b)` 跟踪中夹角 | **0.0747°** | < 0.50 |
| `LOC-05 (a2)` 对照组的目标 AltAz 变化 | **127.19°** | > 5.0 |
| `LOC-05 (c)` 对照组夹角（跳变前 → 后） | **32.97° → 158.17°** | > 5.0 |
| `LOC-08` 家园行星端到端 | 选中 `Earth` → 拒绝，理由 `home-planet` | — |
| `LOC-08b` 清选中后的两个真值 | `合取=true/引擎原始=true` → **`合取=false/引擎原始=true`** | — |

（`LOC-05b-note`：归中等待 2.5 s 内天空自转 **82.02°**，不计入判据数。）

### 4.1b UI 层端到端自检：8/8 PASS（rc=0，连跑 3 次稳定）

`STELQUICK_UI_CHECK=1`，证据 `uicheck-mac.txt`。判据：

| 判据 | 内容 | 实测 |
|---|---|---|
| UI-01 | 三个 `objectName` 锚点可寻且可见 | 定位/取消/状态 Label 全「有」，`visible=true` |
| UI-02 | 无选中时两按钮**由绑定**禁用 | `enabled: 定位=false 取消=false` |
| UI-03 | 选中后定位按钮变为可用、取消跟踪仍禁用 | `enabled: 定位=true 取消=false` |
| UI-04 | **真实点击「定位并跟踪」→ 引擎 `isTracking=true`** | `trackedName="Moon"`（期望 `Moon`） |
| UI-05 | 跟踪中文案跟着重绑 | `"正在跟踪：Moon"` |
| UI-06 | **负控**：点在非按钮控件上，跟踪状态纹丝不动 | `isTracking=true`、`trackedName="Moon"` 不变 |
| UI-07 | **真实点击「取消跟踪」→ `isTracking=false`** | ✔ |
| UI-08 | 取消后文案不再声称跟踪 | `""`（回到第三档） |

关键读数：合成点击落在 `@(425.5,541.0)`（按钮中心，按钮 `835×32`、窗口 `960×640`），
负控靶点是状态 Label 中心（非按钮、无交互处理器）。坐标与尺寸都打印进证据，
否则事后无法判断"点的是不是那个按钮"。

### 4.2 回归

| 套件 | 结论 |
|---|---|
| CLOCKCHECK（T16 时钟纯逻辑） | **12/12 PASS** —— T18 没碰时钟 |
| ACTIONCHECK（T15/T16 命令通路 + 集成） | **27 判据 0 FAIL**（含 AC-12 键盘挂载点）；**AppFacade 被 T18 扩展过，此项是关键回归** |
| SEARCHCHECK（T17 两个模型） | **26/26 PASS** —— **`selectByStableId` 被 T18 改过，此项最关键** |
| A2 静态纹理（Metal） | 12 探针 PASS / 失败 0 项 |
| DYN 动态帧 · engine 生产者（Metal） | **0/3 PASS（低负载时为 2/3）** —— **环境不可测，非退化证据**；定性见 §3 问题 ④ |
| DYN 动态帧 · **替身生产者**（判别性对照） | **0/3 PASS** —— 替身路径**不含 T18 代码**却同样失败 ⇒ 与本次改动无因果关系 |
| S3 旧宿主（`EngineWallClock` 路径） | **8/8 PASS** |

> DYN 那两行**必须成对读**。只报引擎那一半会在"今天全败"时被误判成退化——本次真踩到。
> 详见证据包 `dyn-ab-baseline-vs-t18.txt` §6 与 `regression-dyn-stub-metal.txt`。

### 4.3 构建

`cmake --build build-release --target stelQuickUI`：**0 error**；
新增 `LocateCheck.{hpp,cpp}` 零编译告警。

> 踩坑留档（承 T17 的同一条）：给构建命令接 `| head` / `| tail` 会在截断点触发 SIGPIPE，
> **把构建掐断**，表现是"命令返回了但二进制时间戳没变"。本次全部构建都写成
> **输出落日志文件、事后 grep 日志**。

### 4.4 Windows 侧（交付战场）复验

`main@6bce85d` 在 Windows（RTX 4060 + 原生 Vulkan）同步后跑 30 分钟正式长跑：
**`VERDICT=PASS`、`rc=0`、`SL-C01..C11` 全绿**（稳态 50.00 fps / 邮箱零丢弃 /
内存斜率 0.091 MiB/min / 窗口暴露 1799/1799 / 降级误报 0.000%），设备丢失 0 次。
证据与对比表：`docs/evidence/2026-09-24-windows-30min/README.md` §2。

---

## 5. 改动清单

**新增**

| 文件 | 说明 |
|---|---|
| `src/app/LocateCheck.hpp` | 自检声明 + 判据设计与"为什么必须成对"的论证（`LOC-01..08`，退出码 0/10/6） |
| `src/app/LocateCheck.cpp` | 自检实现：线性步骤表驱动器 + 12 步判据 |
| `src/app/LocateCheck.cpp` 里的 UI 段（`main.cpp` 内） | `STELQUICK_UI_CHECK=1` 分支 + `uiLocate*` 家族（8 条判据，见 §2.9） |
| `tools/t18-verify.sh` | 一键复跑（core / regress / dyn N）；DYN 段**同时跑引擎与替身两半** + `env_note` 环境注记 |
| `tools/t18-win-longrun.ps1` | Windows 长跑 wrapper **进仓库**（补 T13 那次"用完即丢"的欠账；参数化以便先跑 8 秒冒烟） |
| `docs/T18_LOCATE_TRACK.zh_CN.md` | 本文档 |
| `docs/evidence/2026-09-24-t18-locate-track/` | 证据包（含 DYN 定性的 A/B 记录） |

**修改**

| 文件 | 改了什么 |
|---|---|
| `src/app/AppFacade.hpp` | 新增头注「定位/跟踪的语义（先读这段再改代码）」；3 个 `Q_PROPERTY`（`tracking` / `trackedName` / `lastLocateRefusal`，**只读**，写侧必须走命令）；`locateSelected` / `setTracking` / `toggleTracking` / `isTracking` / `trackedName` / `locateRefusalText` / `isHomePlanet` + 观测量 |
| `src/app/AppFacade.cpp` | 上述实现；`setRefusal()` **只在确有变化时**发信号；`locateSelected` 的四个拒绝理由（`engine-unavailable` / `no-selection` / `home-planet` / `ok`）；`isTracking()` 的合取真值与长注释 |
| `src/app/ObjectInfoModel.cpp` | `selectByStableId` 幂等闸（问题 ②） |
| `src/ui/main.cpp` | `locateCheck` / `uiCheck` 环境开关；`startPage` 含 `search`；两条 ActionRouter 注册（**刻意不绑键位**——引擎自带对准/跟踪快捷键经透传已可用，再绑就是双轨）；`STELQUICK_LOCATE_CHECK=1`（14 判据）与 `STELQUICK_UI_CHECK=1`（8 判据）两个独立分支（rc 0/10/6）；新增 `<QQuickItem>` / `<QMouseEvent>` 头 |
| `src/ui/CMakeLists.txt` | `LocateCheck.{hpp,cpp}` 入 `stelQuickUI_SOURCES` |
| `src/ui/qml/SearchPage.qml` | 双击结果行 = 先**确认选中成功**再定位（否则定位会打在上一个对象上）；新增「定位并跟踪」/「取消跟踪」按钮 + 三档状态行；信息面板加「跟踪」行；给两个按钮与状态 Label 补 `objectName`（`locateButton` / `untrackButton` / `locateStatusLabel`）作为 UI 检查的锚点 |
| `src/ui/qml/MainWindow.qml` | 命令栏加定位/跟踪按钮（`enabled: objectInfo.hasSelection`）+ 跟踪状态标签（用 `trackingChanged` 通知驱动，不像视场那样轮询——跟踪是离散状态，没有动画中间值） |
| `src/ui/qml/SearchPage.qml` 头注 | 记明"为什么不在 QML 复刻判断"（T17 的 `maxItems` 就是这么错的） |
| `docs/evidence/2026-09-24-windows-30min/README.md` | 加 §2（W-T17 复验长跑）+ §3 日志编码警告（比 W-T13 的"按 BOM 转码"更细一层） |

---

## 6. 移交 A4（不假装完成）

1. **排序策略**：相关度 / 类型分组 / 拼音匹配（承 T17 §6.1，仍需要产品决策）；
2. **GUI 时钟写入对话框 ×6**（T14 盘点，仍挂在 A4）——"改时间"这一环；
3. **QML 交互级验证（已部分结清）**：T18 新增的 `STELQUICK_UI_CHECK` 已把
   「定位并跟踪 / 取消跟踪两个按钮的点击链路」做成可复跑的端到端判据（§2.9、§4.1b），
   并含一条负控 —— **这一条不再是"尚未验证"**。仍未覆盖的：输入法（中文输入）、
   滚动列表、以及 `SearchPage` **双击结果行**那条路径（`onDoubleClicked` 需要
   结果行的坐标，未纳入本套判据）；视觉层（布局审美、文案措辞）仍属人工；
4. `AppFacade::julianDay()` 现读 `core->getJD()`；T16 之后可改读 `getSimClockJD()`
   （非缺陷，属清理）；
5. **跟踪标志泄漏的根治**（§2.3）：本层读合取真值只是绕开。真正的修法在引擎侧
   （`unSelect()` 先发信号后清列表，或让 `selectedObjectChange()` 不依赖 `getWasSelected()`）——
   影响面涉及 GUI/动作表/脚本，属独立任务；
6. **DYN 显示侧停摆**（§3 问题 ④）：已定性为**环境敏感、与 T18 无关**（替身路径
   不含 T18 代码却同样失败）。倾向方向是**窗口未被暴露**（会话锁屏 / 显示器睡眠 /
   窗口遮挡 → `isExposed()` 变化）。已试过且**无效**的干预：`caffeinate -dims`、
   `caffeinate -dimsu`（见 `dyn-ab-baseline-vs-t18.txt` §6.2）——
   **下次不要重复试**。**待查**。

---

## 7. 复现

```sh
cd /Users/ztuqfvy/qt_demo/stellarium_vulkan
/opt/homebrew/bin/cmake --build build-release --target stelQuickUI -j 8   # cmake 不在 PATH

./tools/t18-verify.sh all        # 全部；DYN 跑 3 次
./tools/t18-verify.sh core       # T18 自检 + T16/T15 回归
./tools/t18-verify.sh dyn 6      # 只跑 DYN 6 次（定性用）

# 单独跑自检
export STELQUICK_GRAPHICS_API=metal
STELQUICK_LOCATE_CHECK=1 ./build-release/src/ui/stelQuickUI.app/Contents/MacOS/stelQuickUI
# → LOCATECHECK: 判据 14/14 / VERDICT=PASS（rc=0）
STELQUICK_UI_CHECK=1 ./build-release/src/ui/stelQuickUI.app/Contents/MacOS/stelQuickUI
# → UICHECK: 判据 8/8 / VERDICT=PASS（rc=0）；最外层注入，窗口需能渲染
```

**读结果时的两条纪律**（T18 明确写下来的）：

- `DYN` 那两行是 `N/3` 而不是 `rc=`。**不要**把其中某一次绿色当成"零退化已证明"；
  依据是 `dyn-ab-baseline-vs-t18.txt` 里的同口径 A/B 失败率相同 + 替身路径不含 T18 代码。
- `DYN` **必须把引擎与替身两行一起读**：替身也失败（尤其"全程 0 帧"）⇒ 那是
  **仪器测不到**，不是被测程序失败（采信规则见证据 README §3.1）。
- 跑 DYN 前先确认机器空载、**且会话未锁屏、显示器亮着**。
- `note()` 行（如 `LOC-05b-note`）**不计入判据数**。它们是"防止误读"的说明，
  不是判据。
