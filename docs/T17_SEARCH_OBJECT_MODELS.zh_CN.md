# T17 交付报告 — 两个模型（SearchResultsModel / ObjectInfoModel）

日期：2026-09-25 · 阶段：A3 正题（业务接口层）· 证据包：`docs/evidence/2026-09-25-t17-models/`

---

## 1. 任务与通过条件

**任务书原文**（《2026-09-20-04-开发计划变更与进度实况》§9.2）：

> `SearchResultsModel` / `ObjectInfoModel`（增量列表、请求编号、过期丢弃；契约见指导文档 §3 表）

**通过条件**：

> 搜索→选择→信息页纵向可用；过期结果不覆盖新查询

**契约**（开发指导文档 §3 表，逐条对表）：

| 对象 | 职责 | 禁止事项 |
|---|---|---|
| `SearchResultsModel` / `ObjectInfoModel` | 增量列表、请求编号、稳定对象标识 | 过期搜索结果不覆盖新查询；**不传悬空裸指针** |

业务入口（指导文档 §3 对照表）：

> 搜索/选择 ← `StelObjectMgr.hpp:78–105 findAndSelect/listMatchingObjects`；空结果、类型与稳定标识；过期结果丢弃

**结论**：通过。自检 26/26 PASS，五套回归零退化，纵向链路实测可用。

---

## 2. 设计

### 2.1 稳定对象标识 = `type:id`（依据来自引擎，不是本层发明）

引擎自己的注释把答案写好了 —— `StelObject.hpp:365-377`：

> Returns a unique identifier for this object. The ID should be unique for all objects of the
> same type, **but may freely conflict with IDs of other types, so `getType()` must also be tested**.

⇒ `stableId = getType() + ":" + getID()`。回查走 `StelObjectMgr::searchByID(type, id)`
（`StelObjectMgr.hpp:150-157`；它会遍历同一类型下的所有 ID 变体）。

实测样本：`Star:HIP 32349 A`（类型段 `Star` + ID 段 `HIP 32349 A`）。
`ObjectInfoModel::splitStableId` 只按**首个**冒号切分 —— 天体 ID 自身可能含冒号
（目录号），从右边切会把类型名和 ID 都切错。

### 2.2 为什么一个天体指针都不能存

契约里"不传悬空裸指针"最容易理解错成"注意别把指针传给 QML"。真正的原因在类型定义里
（`StelObjectType.hpp:44`）：

```cpp
using StelObjectP = QSharedPointerNoDelete<StelObject>;
```

`StelObjectP` 是**不持有所有权**的共享指针 —— 它**不延长对象寿命**。对象何时销毁由各自模块
（`StarMgr` / `SolarSystem` / …）决定，与"谁拿着 `StelObjectP`"无关。
所以"模型还在、对象已走"不是意外，而是**必然会发生**。

处置（本层采取的唯一策略）：

| 层 | 存什么 |
|---|---|
| `SearchResultsModel` | 只存**字符串**（名称/英文名/类型/`stableId`），行结构里没有任何指针字段 |
| `ObjectInfoModel` | 只存**值**（`QString` / `QVariantMap`）；每次 `refresh()` 当场重新向引擎取一次 |

需要"回到某个对象"时用 `stableId` **重新解析**（`selectByStableId`），绝不复用旧指针。

### 2.3 请求编号门：为什么它不是装饰

当前引擎搜索是**同步**的（`listMatchingObjects` 在 GUI 线程内直接返回），所以不存在真并发下
"旧请求后到达"。但 `applyResults(requestId, ...)` 对编号的校验**真实生效**，有三个理由：

1. **重入**：引擎搜索会经插件钩子产生重入 —— 一次 `search` 的执行过程中触发另一次 `search`；
2. 没有这道门，重入的表现是**结果错乱**（QML 上看到的是"后发起、先完成"的那个查询）；
3. 本层语义应独立于"引擎今天恰好是同步的"这一实现细节（异步化不该改模型语义）。

因此自检**直接构造**"旧请求后到达"来验证它（`applyResults` 是公开方法，理由写在头注里），
并要求丢弃计数可观测（`discardedRequests()`）。

### 2.4 安全空值：陈旧值比空值更危险

对象失效（被取消选中 / 对象已卸载）时，`ObjectInfoModel` 一律**归零**，绝不保留上一次的值。

理由：**QML 会把陈旧值当成当前状态照常渲染** —— 用户看到的是"一个已经不存在（或不是当前选中）
的天体的信息"，比看到空白危险得多（空白至少是诚实的）。

`resetToEmpty()` 还做了两件小事：只在**确有变化**时发 `selectionChanged`（否则"每帧刷新信息页"
会变成常态）；非法 `stableId` 与解析失败走**同一条归零路径**（少一条分支就少一个漏网）。

### 2.5 QML 接线分工：模型是数据，AppFacade 是命令

```
QML 列表   →  model: searchResults        （模型本体直供，QAbstractListModel）
QML 命令   →  appFacade.searchObjects(q)  （发起搜索）
              appFacade.selectSearchResult(row)  （选中第 row 行）
              appFacade.clearSelection()
```

选择**必须**经 `AppFacade`：它取该行的 `stableId`，交给引擎解析成对象并真的选中 ——
那是"引擎语义"，属于命令层。模型只负责把数据摆好，不碰引擎状态。

两个模型以 **context property** 注入（`searchResults` / `objectInfo`），与 T15 的
`ActionRouter` 同一惯例。刻意**不**给 `AppFacade` 加 `Q_PROPERTY` 指针：那样 QML 引擎
需要额外注册这两个类型（`qmlRegisterUncreatableType`），而 context property 的类型解析
走 QObject 元对象，零注册成本、零"类型未注册"风险。

---

## 3. 落地过程中发现并修掉的三个真实缺陷

### 缺陷 ①：`maxItems` 形同虚设（首跑就暴露）

两个前置事实都来自**读引擎源码**（`StelObjectMgr.cpp::listMatchingObjects`），不是推测：

```cpp
QVector<QPair<QString,StelObjectP>> StelObjectMgr::listMatchingObjects(prefix, maxNbItem, useStartOfWords) const
{
    for (const auto* m : objectsModules)
        result += m->listMatchingObjects(objPrefix, maxNbItem, useStartOfWords);  // 每模块各取 maxNbItem 条
    std::sort(result.begin(), result.end(), [](auto& a, auto& b){ return a.first < b.first; });
    return result;                                                               // 按名称字典序重排
}
```

引擎的 `maxNbItem` 是**每模块**上限，聚合后总量可达 `模块数 × maxNbItem`。

- 首跑自检数据：`SRC-01d` 显示**请求 10 条拿到 20 条**；
- 补测数据：请求 3 条，引擎原始匹配 **21 条**（查询「S」）。

**修复**：本层把 `maxItems` 统一解释为**总量上限**（取字典序前 `maxItems` 条），
并新增 `lastRawMatchCount()` 暴露截断前的原始匹配数 —— 让"上限真的生效"可被**证明**而非声称。
修复后同一判据：请求 3 条 → 实得 3 条（raw=21，已触发截断）。

### 缺陷 ②：排序口径与引擎注释不符

引擎头注写 `@return a list of matching object names by order of relevance`（按相关度），
但聚合实现里 `result +=` 之后又做了一次 `std::sort`（比较 `first`，即名称字符串）——
**聚合结果的顺序是字典序，不是相关度**（见上面那段代码）。

影响：`RankRole` 不能宣称是"相关度序"（本层初版注释就写错了，已更正）。
搜索页若想按相关度排，必须在更高层自己做——但"相关度"如何定义（星等？类型优先级？名称完全匹配优先？）
是**产品决策**，本层不臆造，明确移交 A4。

### 缺陷 ③：T15 的键盘单点路由在 QML 侧从未接上（最隐蔽的一个）

**发现方式**：验证搜索页是否干净时，在日志里捞到一条从 T15 起就一直存在、却**没人把它与判据
联系起来**的运行时告警：

```
Could not attach Keys property to:  MainWindow_QMLTYPE_39(0x767b717100)  is not an Item
```

**根因**：`Keys` 是 **`Item` 的附加属性**，而 `ApplicationWindow` 继承自 `Window`、**不是 `Item`** ——
T15 写的 `Keys.onPressed` 在 `ApplicationWindow` 根部**从未挂上**。

**后果**：T15 声称的"键盘唯一入口 = `ActionRouter::routeKey`"，在 C++ 侧是真的，在 **QML 侧是死代码**。
用户按快捷键不会有任何反应。

**为什么 T15/T16 的判据没抓到**：AC-5 是**直接调用** `router->routeKey(...)` 的 ——
它证明"C++ 逻辑正确"，但**证明不了 QML 把键盘接上了**。
这是典型的"**仪器没接在实况上**"：判据全绿而实况是坏的。

**修复**（`src/ui/qml/MainWindow.qml`）：把挂载点移到一个**真正可聚焦的 `Item`** 上
（`objectName: "skyKeySink"`），并让它做全部内容的父节点 —— 这样即使焦点在子控件
（如搜索框）里，按键仍会沿父链冒泡到这里；"该不该下发给天空快捷键"仍由
`ActionRouter::routeKey` 内部的焦点守卫判定（U-ACT-03），QML 侧不重复实现。

**验证**（新增判据 AC-12，两档）：

| 档 | 条件 | 断言 | 实测 |
|---|---|---|---|
| 结构 | 挂载点存在 | 能在场景图里找到名为 `skyKeySink` 的 **`QQuickItem`**（Keys 可附加的前提） | OK |
| 端到端 | 场景图有 `activeFocusItem` | 把 `Q` 键**真的从窗口投递**（`QCoreApplication::sendEvent(window, &QKeyEvent)`），断言 `dispatchCount()+1` 且引擎动作翻转 | **OK** |
| SKIP | 窗口未激活（自动化下常见） | 照实写"挂载点就位但端到端未验"，不伪装成 OK | 本次未走到 |

**旁证**：`Could not attach Keys property` 在全部证据文件中的出现次数 **1 → 0**
（T15/T16 的每份证据里都是 1）。

> 这条缺陷的价值不在修复量（10 行 QML），而在它揭示的**验证方法问题**：
> 一条判据如果**自己构造输入**（直接调函数），它就永远测不到"真实输入有没有接进来"。
> AC-12 的写法是"从最外层（窗口）注入、往内看"，这才是端到端。


---

## 4. 验收

### 4.1 自检：26/26 PASS（rc=0）

自检分两阶段，这是刻意的设计：

| 阶段 | 依赖 | 内容 |
|---|---|---|
| **A** | 无（不依赖任何真实天体） | SRC-01/02/03 —— 请求编号门、空结果安全、失效访问安全空值 |
| **B** | 环境中至少有一个可检索天体 | SRC-04/05 + 纵向 V1–V9 |

**为什么要分阶段**：模型的**语义**正确性与"这台机器的星表里恰好有 Sirius"是两件不相干的事。
混在一起会让"环境缺 fixture"伪装成"模型有 bug"。分阶段之后：

- 阶段 A 有失败 → **一律 FAIL（rc=10）**，不被"环境缺 fixture"掩盖；
- 阶段 A 全绿、阶段 B 无 fixture → **UNAVAILABLE（rc=6）**，并**照实打印阶段 A 的结果**，
  让"模型逻辑已验、只是环境没有天体"这一事实可见。

判据明细见 `docs/evidence/2026-09-25-t17-models/README.md` §2。关键数据：

| 判据 | 数据 |
|---|---|
| SRC-01d 过期结果不覆盖新查询 | 行数仍 **10**（修复前 20） |
| SRC-01f 丢弃计数 | +2（过期 1 + 无效编号 1） |
| SRC-05a 总量上限 | 请求 3 条 → 实得 3 条（**raw=21，已触发截断**） |
| SRC-04a/b 稳定标识 | `Star:HIP 32349 A`，跨次一致、类型段 = `getType()` |
| SRC-04c 跨查询回查 | ✔ 重新选中 HIP 32349 |
| T17-V1..V8 纵向可用 | displayName=Sirius / typeName=Star / infoText=881 字符 / infoMap=54 项 |

### 4.2 回归：五套零退化（+ ACTIONCHECK 新增 AC-12）

| 套件 | 结论 |
|---|---|
| CLOCKCHECK（T16 纯逻辑时钟） | 12/12 PASS |
| ACTIONCHECK（T15/T16 命令通路） | 全 PASS（含**新增 AC-12 键盘挂载点端到端 OK**）—— **AppFacade 与 MainWindow 都被 T17 改过，此项是关键回归** |
| A2 静态纹理（Metal） | 12 探针 PASS |
| DYN 动态帧 · engine 生产者 | 7/7 PASS |
| S3 旧宿主（`EngineWallClock` 路径） | 8/8 PASS —— 证明 T17 **没碰时钟** |

### 4.3 构建

三轮增量构建，**0 error**；新增文件零编译告警。

> ⚠️ 踩坑留档：`build-t17-r2` 的第一版命令把构建输出接进 `grep ... | head -20`，
> 第 20 行关闭管道触发 SIGPIPE，**整个构建被掐断** —— 表现极具误导性："构建成功"
> 但二进制时间戳没变、新判据没出现。教训：**给构建命令接管道截断输出，等于给构建装了个随机杀手**。
> 改为输出直接落日志、事后 grep 日志。

---

## 5. 改动清单

**新增**

| 文件 | 说明 |
|---|---|
| `src/app/SearchResultsModel.cpp` | 搜索引擎取数（唯一的引擎触点）+ 总量上限 |
| `src/app/ObjectInfoModel.cpp` | 选中对象投影 + `stableId` 回查 + 安全空值 |
| `src/app/SearchModelCheck.{hpp,cpp}` | 自检（阶段 A/B、SRC-01..05、纵向 V1–V9） |
| `src/ui/qml/SearchPage.qml` | 搜索页（搜索框 + 结果列表 + 信息面板） |
| `tools/t17-verify.sh` | 一键复跑（六项） |
| `tools/t17-win-build.ps1` | Windows 侧后台构建脚本（VS 自带 cmake + rc 落盘） |
| `docs/T17_SEARCH_OBJECT_MODELS.zh_CN.md` | 本文档 |
| `docs/evidence/2026-09-25-t17-models/` | 证据包 |

**修改**

| 文件 | 改了什么 |
|---|---|
| `src/app/SearchResultsModel.hpp` | 由骨架改为落地：角色/属性/门/观测量；**排序口径**注释 |
| `src/app/ObjectInfoModel.hpp` | 由骨架改为落地：只读属性面 + 引擎可用性 |
| `src/app/AppFacade.hpp` | 持有两个模型；新增 `searchObjects` / `selectSearchResult` / `selectByStableId` / `clearSelection`；**顺带更正头注里 T15 遗留的时钟说明**（T16 已改，原注释还写着"真源在帧泵"） |
| `src/app/AppFacade.cpp` | 上述四个命令的实现 |
| `src/ui/main.cpp` | 两个 context property；`startPage="search"`；`STELQUICK_SEARCH_CHECK=1` 独立分支（rc 0/6/10） |
| `src/ui/qml/MainWindow.qml` | 页名→索引映射表（`pageIndex`）+ 搜索页标签 + StackLayout 加页；**并把 `Keys` 从 `ApplicationWindow` 移到可聚焦 `Item`（`skyKeySink`）上——修掉 T15 的键盘挂载缺陷（缺陷 ③）** |
| `src/app/AppFacadeCheck.{hpp,cpp}` | 新增 **AC-12**：QML 键盘挂载点的结构断言 + 从窗口投递 `Q` 键的端到端断言（补 AC-5 的盲区） |
| `src/ui/CMakeLists.txt` | 新 C++ 文件入 `stelQuickUI_SOURCES`；`SearchPage.qml` 入 `QML_FILES`（**漏这一步页面会静默变空 Item**，T10 已踩过） |

---

## 6. 移交 A4（不假装完成）

以下明确**不在** T17 通过条件内，移交 A4：

1. **排序策略**：相关度 / 类型分组 / 拼音匹配 —— 需要产品决策，见 §3 缺陷 ②；
2. **定位与跟踪**：选中后镜头移向天体，须复用 `SearchDialog.cpp:1468–1484` 的
   "当前观察星球不可普通跟踪"判断（不是简单的 `moveToObject`）；
3. **QML 交互级验证**：点击、输入法、滚动 —— 属 L2 Qt Quick Test；本证据只保证
   页面能被实例化（`startPage="search"` 加载不报错）与模型链路正确；
4. **GUI 时钟写入对话框 ×6**（T14 盘点出的遗留项，仍挂在 A4）；
5. `AppFacade::julianDay()` 现读 `core->getJD()`；T16 之后可改读 `getSimClockJD()`，
   语义更直白（非缺陷，属清理）。

---

## 7. 复现

```sh
cd /Users/ztuqfvy/qt_demo/stellarium_vulkan
/opt/homebrew/bin/cmake --build build-release --target stelQuickUI -j 10
./tools/t17-verify.sh all          # 六项，全部应 rc=0
```

看 QML 搜索页（手动）：

```sh
STELQUICK_PAGE=search ./build-release/src/ui/stelQuickUI.app/Contents/MacOS/stelQuickUI
```
