# T17 证据包 — 两个模型（SearchResultsModel / ObjectInfoModel）

日期：2026-09-25 · 提交：见仓库 `T17` 提交 · 交付文档：`docs/T17_SEARCH_OBJECT_MODELS.zh_CN.md`

一键复跑：`./tools/t17-verify.sh all`（或 `core` / `regress` 分跑）

---

## 1. 判据总览（全部 PASS，rc=0）

| # | 项目 | 文件 | rc | 结论 |
|---|---|---|---|---|
| ① | T17 搜索/选择模型自检 | `searchcheck-mac.txt` | 0 | **26/26 PASS**（阶段 A + 阶段 B + 纵向 V1–V9） |
| ② | T16 时钟纯逻辑自检（回归） | `regression-clockcheck.txt` | 0 | 12/12 PASS |
| ③ | T15/T16 命令通路集成自检（回归 + **新增 AC-12**） | `regression-actioncheck.txt` | 0 | 全 PASS，含 **AC-12 键盘挂载点端到端 OK** |
| ④ | A2 静态纹理（Metal，回归） | `regression-a2-metal.txt` | 0 | 12 探针 PASS |
| ⑤ | DYN 动态帧 · 真实引擎生产者（Metal，回归） | `regression-dyn-engine-metal.txt` | 0 | D1-C01..C07 7/7 PASS |
| ⑥ | S3 旧宿主引擎集成（`stellarium.exe`，回归） | `regression-s3-stela3.txt` | 0 | A3-C01..C08 8/8 PASS |

`rc-summary.txt` 是机器可读的汇总（六行 rc）。

---

## 2. 自检的判据（26 项）

### 阶段 A —— 不依赖真实天体（环境无关，必须恒可跑）

| 判据 | 内容 | 实测 |
|---|---|---|
| SRC-01a | 请求编号单调递增 | 1 → 2 |
| SRC-01b | `currentRequestId` = 最后一次搜索 | 2 |
| SRC-01c | 过期请求结果被拒收（`applyResults` 返回 false） | ✔ |
| SRC-01d | 过期结果未覆盖新查询 | 行数仍 **10**（修复前为 20，见 §4） |
| SRC-01e | 陈旧行未进入模型（`__STALE_ROW__` 不可见） | ✔ |
| SRC-01f | 丢弃计数 +2（过期 1 + 无效编号 1） | 实得 2 |
| SRC-01g | 编号 0 视为无效请求、被拒收 | ✔ |
| SRC-02a | 无匹配查询 → 0 行 + 非空空态理由 | 「未找到匹配「zzz_no_such_object_zzz」的天体」 |
| SRC-02b | 空查询 → 0 行 + 明确理由，不崩不挂 | 「请输入天体名称」 |
| SRC-03a | 清空后各字段归零（安全空值） | ✔ |
| SRC-03b | 5 种非法/失效标识均返回 false 且字段全空 | 5/5 ✔（空串 / 无分隔符 / 空类型 / 空 ID / 类型不存在） |

### 阶段 B —— 需要环境中至少有一个可检索天体

命中 fixture：`Sirius` → `englishName=HIP 32349`、`stableId=Star:HIP 32349 A`
（**注意**：这颗星的 `getEnglishName()` 返回的是目录号而非 "Sirius"，`displayName` 才是 "Sirius"。
这正是自检用**英文名**而非显示名做身份比对的原因——显示名随界面语言变。）

| 判据 | 内容 | 实测 |
|---|---|---|
| SRC-04a | 同一查询跨次得到的标识完全一致 | `Star:HIP 32349 A` == 自身 |
| SRC-04b | 标识形如 `type:id` 且类型段 = `getType()` | `Star:HIP 32349 A` \| `Star` |
| SRC-05a | **总量上限真的生效** | 查询「S」请求 3 条 → **实得 3 条（引擎原始匹配 21 条，已触发截断）** |
| SRC-05b | 原始匹配数 ≥ 截断后行数 | raw=21 ≥ 3 |
| SRC-05c | 未超限时行数 = min(原始, 上限) | 4 = min(4, 10) |
| SRC-04c | 按 stableId **跨查询回查**，重新选中同一天体 | ✔（HIP 32349） |
| T17-V1 | 选中第 0 行 → 信息模型英语名一致 | HIP 32349 |
| T17-V2 | 再次搜索不影响既有选中（选中与搜索解耦） | ✔ |
| T17-V3 | 取消选中 → 信息页归零 | ✔ |
| T17-V9 | 行越界 → 返回 false 且保持归零，不崩 | ✔ |
| T17-V4 | `displayName` 非空 | Sirius |
| T17-V5 | `typeName` 非空 | Star |
| T17-V6 | `infoText` 非空 | 881 字符 |
| T17-V7 | `infoMap` 非空 | 54 项 |
| T17-V8 | `stableId` 形如 `type:id` | `Star:HIP 32349 A` |

### 环境缺 fixture 的处置
阶段 A 全绿但阶段 B 找不到任何可检索天体 → 整体判 **UNAVAILABLE（rc=6）并照实打印阶段 A 结果**，
不伪装成 PASS；首次探测失败会自动重试一次（引擎数据渐进载入）。
阶段 A 有任何失败 → 一律 **FAIL（rc=10）**，不被"环境缺 fixture"掩盖。

---

## 3. 回归（三套 + 两套自检，零退化）

| 套件 | 结论 | 与 T16 首跑对照 |
|---|---|---|
| A2 静态纹理（Metal） | PASS | 12 探针偏差不变 |
| DYN 动态帧 · engine 生产者 | 7/7 PASS | 一致 |
| S3 旧宿主（`EngineWallClock` 路径） | 8/8 PASS | 一致（证明 T17 没碰时钟） |
| CLOCKCHECK（纯逻辑时钟） | 12/12 PASS | 一致 |
| ACTIONCHECK（命令通路） | 全 PASS + **AC-12 新增 OK** | **AppFacade 与 MainWindow 都被 T17 改过，此项是关键回归** |

### 3.1 AC-12：QML 键盘挂载点端到端（T17 新增判据）

T15 把 `Keys.onPressed` 挂在 `ApplicationWindow` 上，而 `Keys` 只能附加在 **`Item`** 上
（`ApplicationWindow` 继承自 `Window`）——**挂载从未生效**，QML 侧的键盘路由是死代码。
AC-5 抓不到它，因为 AC-5 是**直接调用** `router->routeKey(...)`。

| 断言 | 内容 | 实测 |
|---|---|---|
| 结构 | 场景图中存在名为 `skyKeySink` 的 `QQuickItem`（Keys 可附加的前提） | OK |
| 端到端 | 把 `Q` 键**真的从窗口投递**（`sendEvent(window, &QKeyEvent)`）→ `dispatchCount()+1` 且引擎动作翻转 | **OK** |

**旁证**：`Could not attach Keys property` 出现次数 **1 → 0**（在全部证据 .txt 中实测 0；
T15/T16 的每份证据里都是 1）。

---

## 4. 落地过程中发现并修掉的三个真实缺陷

### 4.1 引擎聚合语义（读 `StelObjectMgr.cpp::listMatchingObjects` 得来，非推测）

```cpp
QVector<QPair<QString,StelObjectP>> StelObjectMgr::listMatchingObjects(prefix, maxNbItem, useStartOfWords) const
{
    for (const auto* m : objectsModules)
        result += m->listMatchingObjects(objPrefix, maxNbItem, useStartOfWords);  // ← 每模块各取 maxNbItem 条
    std::sort(result.begin(), result.end(), [](auto& a, auto& b){ return a.first < b.first; });  // ← 按名称字典序重排
    return result;
}
```

**缺陷 ①：`maxItems` 形同虚设。** 引擎的 `maxNbItem` 是**每模块**上限，聚合后总量可达
`模块数 × maxNbItem`。实测：请求 10 条拿到 **20 条**（SRC-01d 首跑数据），请求 3 条本会拿到 **21 条**。
修复：本层把 `maxItems` 统一解释为**总量上限**，并暴露 `lastRawMatchCount()` 让"上限真的生效"可被证明。

**缺陷 ②：排序口径与注释不符。** 引擎头注写 `@return ... by order of relevance`（按相关度），
但聚合实现里 `result +=` 之后又做了一次 `std::sort`（按 `first` 即名称字符串字典序）——
**聚合结果的顺序是字典序，不是相关度**。影响：`RankRole` 不能宣称是"相关度序"，
搜索页若想按相关度排必须在更高层自己做。修复：改掉本层注释（原写"引擎按相关度排序"，
已更正），并把更进一步的排序策略明确移交 A4；本层不臆造排序。

### 4.2 缺陷 ③：T15 的键盘单点路由在 QML 侧从未接上

`Keys` 是 `Item` 的附加属性，而 T15 把它挂在了 `ApplicationWindow`（继承自 `Window`）上 ——
运行时告警 `Could not attach Keys property to ... is not an Item` 自 T15 起就在每份证据里，
却**没人把它与判据联系起来**。修复与 AC-12 验证见 §3.1。

---

## 5. 构建

| 文件 | 说明 |
|---|---|
| `build-t17.log.gz` | 首轮构建（含 cmake 重配置 + 引擎增量重编） |
| `build-t17-r2.log.gz` | 修完总量上限后的增量构建 |
| `build-t17-r3.log.gz` | 加入 SRC-05 宽前缀截断判据后的增量构建 |
| `build-t17-r4.log.gz` | 修完键盘挂载点 + 加入 AC-12 后的增量构建 |

四轮均 **0 error**；新增文件零编译告警（日志中的告警全部来自既有 `src/external`、引擎与插件代码）。

> ⚠️ 踩坑留档：`build-t17-r2` 的第一版命令把构建输出接进 `grep ... | head -20`，
> 结果第 20 行关闭管道触发 SIGPIPE，**整个构建被掐断**——表现是"构建成功但二进制时间戳没变、
> 新判据没出现"。教训：**给构建命令接管道截断输出，等于给构建装了个随机杀手**。
> 改为输出直接落日志、事后 grep 日志。

---

## 6. 复现步骤

```sh
cd /Users/ztuqfvy/qt_demo/stellarium_vulkan
/opt/homebrew/bin/cmake --build build-release --target stelQuickUI -j 10
./tools/t17-verify.sh all          # 六项，全部应 rc=0
```

---

## 7. 未覆盖 / 明确移交

- **QML 搜索页的交互级验证**（点击、输入法、滚动）属 A4 L2 交互测试，本证据只保证
  页面能被实例化（`startPage="search"` 时加载不报错）与模型链路正确。
- **排序策略**（相关度 / 类型分组 / 拼音匹配）移交 A4。
- **定位/跟踪**（选中后镜头移向天体）需要复用 `SearchDialog.cpp:1468–1484` 的
  "当前观察星球不可普通跟踪"判断，属 A4，不在 T17 通过条件内。
- **Windows 侧复验**：见 `WINDOWS` 相关记录（T17 提交后需重新 pull + 构建）。

---

## 8. 文件清单

```
searchcheck-mac.txt                  ① T17 模型自检（26/26）
regression-clockcheck.txt            ② T16 时钟纯逻辑（回归）
regression-actioncheck.txt           ③ T15/T16 命令通路（回归）+ AC-12（T17 新增）
regression-a2-metal.txt              ④ A2 静态纹理（回归）
regression-dyn-engine-metal.txt      ⑤ DYN engine 生产者（回归）
regression-s3-stela3.txt             ⑥ S3 旧宿主集成（回归）
rc-summary.txt                       六项 rc 汇总
build-t17.log.gz / -r2 / -r3 / -r4   四轮构建日志
```
