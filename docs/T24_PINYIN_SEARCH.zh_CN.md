# T24 交付文档 — 搜索排序的拼音检索（A4 加固）

日期：2026-09-28 ｜ 状态：**完成并推送** ｜ 提交：见 git log

---

## 1. 任务与通过条件

A4 加固项"排序策略（拼音 / 类型分组）"的落地。通过条件：

1. 拼音检索可用：zh_CN 下搜 `yueqiu` / `yq` 能找到月球（Planet:Moon）；
2. 判据两条腿：纯逻辑（PINY-01..05，环境无关）+ 活引擎（PINY-06..08，真实引擎 + 真实模型 + 真实数据表），负控（关拼音分支）必须红；
3. 既有判据零退化：SEARCHCHECK 34 → 51 全绿，相邻回归全 rc=0；
4. 证据归档 `docs/evidence/2026-09-28-t24-pinyin-search/`；
5. BUILD_RECORD / 计划文档 / 证据总索引 / 记忆与技能沉淀。

## 2. 设计与口径

### 2.1 问题定性：这是召回，不是排序

T21 头注写明了排序层的能力边界："候选截断在排序之前，本层只能在已返回的候选内重排，无法凭空召回"。拼音恰好落在边界之外——中文界面下天体名是翻译名（`月球`），而 `matchObjectName`（StelObjectModule.cpp:32-38）做的是**字面** contains/startsWith，`yueqiu` 与 `月球` 永远不匹配 ⇒ 拼音查询在引擎检索这路**一条候选都拿不到**。所以必须在检索阶段补一个拼音候选源。

### 2.2 架构：PinyinIndex + collect() 拼音分支

- **`src/app/PinyinIndex.{hpp,cpp}`**（纯逻辑，两种形态都能编）：
  - 拼音表：mozillazg/pinyin-data v0.15.0（MIT），构建期转无声调精简格式 `U+XXXX py1 py2 ...`（44435 条，544KB），qrc 嵌入 `:/StelQuickUI/pinyin-lite.txt`，懒加载一次 + 名字→拼音 memo；
  - 多音字按**主读音**（数据第一读音 = 最高频）拼，全组合展开是指数爆炸、不做；
  - `toPinyin / initials / matchQuality / isPinyinQuery / hasCjk`，无状态静态接口，可直接在自检里穷尽覆盖。
- **`SearchResultsModel::collect()` 拼音分支**：查询是纯 ASCII 字母时，遍历全部天体模块（`StelModuleMgr::getAllModules()` + `dynamic_cast<StelObjectModule*>`——`StelObjectMgr::objectsModules` 是 private，**不为读列表动引擎**）的 `listAllObjects(false)`（翻译名），对含汉字的名字做拼音匹配，候选并入既有 ①去重（按 stableId 保相关度最高）②排序 ③截断管线。
- **`SearchRanker` 两个新档位**：`PinyinFull`（全拼连续子串，插在 WordStart 与 Substring 之间——用户特意打的读音强于字面碰巧包含）、`PinyinInitial`（首字母词首，垫底——误命中率最高）。既有五档相对序不变 ⇒ 旧判据零改动。

### 2.3 为什么不改引擎

`listMatchingObjects` 是共用原语（旧 SearchDialog.cpp:985 也在用）；拼音表是应用层资产（544KB 数据文件），塞进 src/core 会让引擎背 UI 语义。T21 的先例同样适用。

### 2.4 类型分组：记录为产品决策（不做 UI 分段）

移交表原文："类型分组是 UI 层决策"。评估：T21 的 `typeWeight` 已是排序键次序因子（同 quality 内太阳系天体聚集）；UI 分段标题在 ≤20 行的结果列表里挤占行高，信息密度负收益。**结论：维持 typeWeight 次序，UI 分段不做**，留档于此。后续若结果上限放大或用户反馈要求，再加分组头。

## 3. 过程里抓到的三个真实问题

### 3.1 🔴 翻译布局坑：qm 加载失败让拼音整体失效

首跑活引擎腿拼音候选恒 0。诊断（打印模块 cast 数 / 名字样本）发现**名字全是英文**（cjk=0），而启动日志有：

```
Couldn't load translations for language "zh_CN" in section "stellarium"
```

根因：`StelFileMgr::getLocaleDir()`（StelFileMgr.cpp:459-487）按 `installDir/translations`、`appDir/../translations`、`appDir/../../translations` 找 .qm；合流形态 bundle 布局（`stelQuickUI.app/Contents/MacOS/`）下三个候选全不命中（translations 实际在 `build-release/translations`，且 `install location="."`）。**也就是说合流形态从 T10 合流起就没加载过任何翻译**——此前没有判据依赖翻译名，所以一直没暴露。

处置（src/ui/CMakeLists.txt）：POST_BUILD 把主域 `zh_CN.qm`（718KB）拷进 `Contents/translations/stellarium/`——恰好命中候选 2。只需主域：行星名与深空名翻译都在 stellarium 域；星座名在 stellarium-sky 域，未拷贝不覆盖（后续需要时扩清单）。

### 3.2 ⚠️ 判据口径：SRC-05b/05c 会为"旧现状"背书的反面案例

SRC-05a 的宽前缀探针（fixture 首字符，如 "S"）正是拼音形态——拼音候选一生效，`raw ≥ rows` 与 `raw ≥ 去重条数` 就报**假红**（拼音候选是截断前候选但不计入 raw）。这是 T21"判据别为上游缺陷背书"的反面：**判据也别为旧现状背书**——下游补上新能力后，旧口径必须跟着校正。两处都改为候选池口径 `raw + lastPinyinMatchCount()`。

### 3.3 私有成员与跨边界 cast

`StelObjectMgr::objectsModules` 是 private。两个选择：给引擎加只读 getter，或走公开的 `StelModuleMgr::getAllModules()` + `dynamic_cast`。选后者（引擎零改动）；cast 语义与引擎聚合内部一致（registerObject 塞的就是 StelObjectModule*）。

## 4. 判据（两条腿 + 负控）

| 腿 | 判据 | 断言 |
|---|---|---|
| 纯逻辑 | PINY-01 | 拼音表加载成功 |
| 纯逻辑 | PINY-02 | 读音转换 4 例：月球→yueqiu/yq、猎户座大星云→liehuzuodaxingyun/lhzdxy、Moon→空串 |
| 纯逻辑 | PINY-03 | 分档 7 例：全拼（含前缀/中间片段）、首字母、无关/非字母查询/无汉字名 → None |
| 纯逻辑 | PINY-04/05 | 查询形态（纯字母含大写=true；中文/空/含空格或连字符=false）与 CJK 判定 |
| 活引擎 | PINY-06 | 切 zh_CN 搜 `yueqiu` → 首行 = 月球(Planet:Moon)，质量 = 拼音全拼 |
| 活引擎 | PINY-07 | `lastPinyinMatchCount()>0` —— **成对**：证明 PINY-06 的首行来自拼音分支而非字面命中 |
| 活引擎 | PINY-08 | 搜 `yq`（首字母档）→ 首行 = Planet:Moon |
| 负控 | collect 拼音分支 `false &&` | PINY-06/07/08 全红（rc=10）✓ |

语言环境**段内切换、段尾必还原**（T22 TC-18 血泪）；还原路径无早退。

## 5. 验收读数

| 项 | 结果 |
|---|---|
| SEARCHCHECK（正题） | **51/51 PASS** rc=0 ×5（34 → 51） |
| 负控（关拼音分支） | 48/51 rc=10：PINY-06/07/08 红，其余全绿 |
| 回归 | timecheck 21/21、timeuicheck 19/19、returnuicheck 11/11、replaycheck 11/11、locatecheck 15/15、locate-uicheck 10/10、clockcheck、actioncheck、a2、S3 旧宿主——全 rc=0 |
| DYN | 引擎 N/3 + 替身 3/3（既有间歇形态，见 rc-summary） |

注意：`yq` 首字母在当前数据下只命中月球，是**数据事实**不是判据弱点（PINY-08 断言的是"候选中排第一的必是月球"，多命中时依旧成立）。

## 6. 改动清单

| 文件 | 内容 |
|---|---|
| `src/app/PinyinIndex.hpp/cpp`（新） | 拼音索引：懒加载 qrc 表 + memo + 分档判定 |
| `src/ui/assets/pinyin-lite.txt`（新） | 无声调精简拼音表（44435 条，544KB，MIT） |
| `src/app/SearchRanker.hpp/cpp` | MatchQuality 插入 PinyinFull/PinyinInitial 两档 + 文案 |
| `src/app/SearchResultsModel.cpp/hpp` | collect() 拼音候选分支 + `lastPinyinMatchCount()` 观测量 |
| `src/app/SearchModelCheck.cpp/hpp` | PINY-01..08 判据 + DIAG 段 + SRC-05b/c 口径校正 |
| `src/ui/CMakeLists.txt` | 源文件挂载 + qrc 拼音表 + POST_BUILD 拷 zh_CN.qm |
| `tools/t24-verify.sh`（新） | 验证脚本 |

## 7. 复现

```sh
/opt/homebrew/bin/cmake --build build-release --target stelQuickUI -j8
zsh tools/t24-verify.sh all 3
```

读结果纪律：`rc-summary.txt` 是权威；负控文件**不是 FAIL 是证据**；DYN 的 N/3 连替身对照一起读，替身也败 = 仪器测不到，不洗成 PASS。
