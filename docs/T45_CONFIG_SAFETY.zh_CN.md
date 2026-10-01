# T45｜A6-C：配置与数据安全 P-CFG-01…04

> **A6 的第三格**。还的是测试文档 §8「A-1.0 出口」**第 4 条**：
> 「P-CFG-01…04 通过」，外加 A6 点名的交付物「**资源打包说明（先 materialize
> 符号链接目录）**」。

---

## 1. 范围从哪里来（不是自己发明的）

| 出处 | 原话 | 现状 |
|---|---|---|
| 测试文档 §6.3 P-CFG-01 | 「写入**损坏**的个人版配置文件 ⇒ 启动可恢复（回退默认或引导修复），给出**明确提示**」 | ❌ **无判据**；且 T36 只处理了"没有 config.ini" |
| 测试文档 §6.3 P-CFG-02 | 「个人版配置路径隔离 ⇒ 原版配置文件未被读写（对比文件哈希）」 | ✅ T36 覆盖（`CFG-03/04/06/07`） |
| 测试文档 §6.3 P-CFG-03 | 「资源目录保护 ⇒ 只读资源链接不被生成工具改写；materialize 前对链接目录的写操作被流程禁止」 | ❌ **无判据** |
| 测试文档 §6.3 P-CFG-04 | 「渲染诊断页一致性 ⇒ 诊断页显示的 API/GPU/交换链信息与**启动日志**一致」 | ❌ **无判据** |
| 开发指导文档 A6 行 | 「资源打包说明（**先 materialize 符号链接目录**）」 | ✅ 见 `A6_RESOURCE_PACKAGING.zh_CN.md` |

---

## 2. 已完成的半格：资源打包说明（P-CFG-03 的"流程禁止"面）

**`docs/A6_RESOURCE_PACKAGING.zh_CN.md`** 已交付，7 节。要点：

| 项 | 事实 |
|---|---|
| 现状 | `setup-upstream-assets.sh` 建 **12 个链接目录** + **`data/` 实体副本** + `guide/` 空目录 |
| 为什么 `data/` 必须是实体副本 | CMake 配置期会往 `data/` 写 ⇒ **软链会穿透改写上游**（历史教训已记录） |
| 为什么分发包不能带链接 | `tar`/`zip`/`.app` 语义各不同；链接目标在用户机上不存在 |
| materialize 流程 | `tools/materialize-resources.mjs` 6 步，含「先造副本验完再替换」「只 unlink 已知链接，**绝不删 target**」「sha256 硬门」 |
| 反向回到链接态 | `tools/materialize-resources.mjs --relink` |
| 校验入口 | `tools/a6-resource-links-check.mjs`（P-CFG-03 判据脚本） |

> **与 P-CFG-03 的关系**：测试文档问的是"链接不被改写"+"写操作被流程禁止"。
> 前者是**可判量的**（脚本逐项 `lstat` / `readlink` / target 存在性）；
> 后者是**流程命题**，只能靠"materialize 只有一条入口 + 入口自带 sha256 硬门 + 入口不删 target"来主张。
> **二者不可混为一谈** —— 本任务分开报（陷阱 75：只许验"被声称的命题"）。

---

## 3. 待还的三条：探针先实测（T45-A）

**纪律：判据必须先探针实测。** 本轮 Q1–Q6：

| 问题 | 为什么要问 | 实测读数 |
|---|---|---|
| Q1 | 个人版 / 原版 config.ini 的路径、大小、md5、键数（基线读数） | **个人版** `~/Library/Application Support/Stellarium-quick/config.ini`｜41.9 KiB｜771 键｜**原版** `~/Library/Application Support/Stellarium/config.ini`｜41.7 KiB｜768 键｜**兜底源** `data/default_cfg.ini`｜12.4 KiB｜**252 键** ⇒ 三者是**三个不同量**，"键数塌陷"因此有可判的基线 |
| Q2 | ★ **QSettings 对"损坏"到底是什么语义**（5 形态，全在 `/tmp` 副本上做） | 🔴 **五种全 `NoError`**：(a) 截断在键值行中间｜键数 **35**、`flag_constellation_drawing` 读不到；(b) 整文件二进制垃圾（256 B）｜键数 **0**；(c) 坏行 + 后续合法键｜键数 **1**；(d) 非法 UTF-8 混在值里｜键数 **1**；(e) 空文件（0 B）｜键数 **0**。⇒ **"回退默认"是 Qt 行为，不是产品行为** ⇒ 判据不许把 Qt 的功劳记在产品头上（陷阱 75）；产品该负责的是①**起得来** ②**把话说出来** |
| Q3 | 只读 config.ini 时 `sync()` 会怎样？产品有没有出口看出来 | 只读文件上 `setValue+sync` ⇒ **`status=AccessError`**（写路径才报）；而**单纯 `sync()` 读**不报错 ⇒ ⚠️ **"不可写"必须显式查 `QFileInfo::isWritable()`**（Q3b 已证 `QSettings::isWritable()` 是**静态**方法，答的是全局状态，不能判单个文件）。产品侧此前**零出口** |
| Q4 | **"明确提示"的现有出口盘点** —— `ErrorModel` 有没有"配置健康"这一项 | 🔴 **没有**。Q4b 三条代码事实：① `ConfigIsolation::Record::note` 只在 `setUserDir` 抛异常/兜底拷贝失败时才有值；② `ErrorModel::statusRows` 当时只有 **4 行**（引擎引导/图形后端/配置目录/帧通路）；③ **`QSettings::status()` 在正常引导路径上从未被读取** ⇒ **"有但坏了"这一支零提示出口**，缺口坐实 |
| Q5 | 资源链接现状：12 个占位逐项 `lstat` / `readlink` / target 存在性 / mtime | 9 个清单内占位**全是符号链接**（实体目录 0 / 缺失 0），target 均存在：`models→…/stellarium/models`、`textures`、`landscapes`、`skycultures`、`nebulae`、`stars`、`atmosphere`、`scenery3d`、`po`。⚠️ 根 = **进程工作目录**（必须 `cd` 到仓库根，否则读数恒为"缺失"）；清单外的 3 个（`plugins/scripts/util`）由 `tools/a6-resource-links-check.mjs` §1b 兜 |
| Q6 | `docs/vulkan/source-manifest.json` 的资源清单条数（供交叉核对） | 1.17 MiB（校验结论由 `node tools/source-snapshot.mjs verify` 给 —— 探针**不复述别人的结论**，陷阱 86） |

> 探针原始输出：`docs/evidence/2026-10-01-a6-configsafety/mac/probe-confighealth-mac.txt`
> （23 条读数；写入面 6/6 临时文件已自报删除，真实配置全程零触碰）。

### 3.1 Q2 是本任务最关键的一个问题

Q2 的答案**决定 P-CFG-01 怎么判**：

| 若 QSettings 的行为是… | 那么… |
|---|---|
| 坏行**跳过**、好行**照读**（`status()` 仍 `NoError`） | "回退默认"**不是产品行为而是 Qt 行为** ⇒ 判据**不能**把 Qt 的功劳记在产品头上。能被主张的只剩「**明确提示**」那一半 |
| 整文件垃圾 ⇒ `status()==FormatError` + `allKeys()` 空 | 产品**有**机会察觉 ⇒ "启动可恢复"可以判成产品命题 |
| 部分形态可恢复、部分不可 | 判据必须**分形态**写，且逐形态标注"这一条主张的是 Qt 还是产品" |

> 这正是陷阱 75 的场景：只许验"被声称的命题"。

### 3.2 写入面纪律

**本探针的第一条纪律：绝不触碰个人版或原版的真实 config.ini。**
Q2/Q3 的全部写入落在 `QDir::tempPath()` 下的独立临时文件（每个形态一个），
探针收尾逐个 `rm` 并**自报删除数**（漏删即红 —— 陷阱 94：只判不净会留残留）。

写探针时顺手修掉的三个"名不符实/平凡真"：

| 处 | 问题 | 修法 |
|---|---|---|
| `humanBytes()` | 名叫 humanBytes 却只返回 `"%1 B"`，不做 KiB/MiB 换算 | 真做换算 |
| `QSettings(p,Ini).isWritable()` | `isWritable()` 是**静态**方法，答的是"QSettings 全局能不能写"，与 `p` 无关 ⇒ 读到的东西与标签不同义（陷阱 65 同族） | 改 `QFileInfo(p).isWritable()`（真读文件属性），并在读数里注明这个区别 |
| `writeTemp()` 未先清残留 | S3 会把文件改成**只读** ⇒ 下次 `open(Truncate)` 失败 ⇒ "写进去的字节根本不是本次造的"（**静默假绿**） | 先 `QFile::remove(path)`；open 失败时**显式报错**而不是静默继续 |

### 3.3 注入机制：**放在测试装置里，不放在产品里**（已定型）

要判 P-CFG-01，必须真把损坏字节放到**个人版 config.ini** 的位置上再跑一遍。做法：

> 脚本设 `STEL_USERDIR=<临时目录>/Stellarium` ⇒ `ConfigIsolation` 推出的个人版目录 =
> `<临时目录>/Stellarium-quick` —— **一个临时目录**。损坏字节由 `tools/a6-cfg-inject.sh`
> 写进那个目录里的 `config.ini`。**真实用户的配置一字节都不碰**，
> 路径/文件/引导链路全是真的。

**与本节原设想（`STELQUICK_CFG_INJECT=<形态>`，产品侧实现）的分歧与结论**：
⊗ 产品侧实现 ⇒ 出货二进制里带一段"往用户配置里写垃圾"的代码 —— 为了测试给产品加一把
能伤到用户的刀，不划算。✔ 归测试装置管，产品侧**只读不写**。

铁律仍然成立：**不许造假的 `QSettings` / 假配置对象**来"验"产品行为
（陷阱 86：判据自建副本 ≠ 被测接线实例）。判据 `ConfigHealthCheck` **只读**文件。

---

## 4. P-CFG-01…04 判据落点

| 编号 | 用例 | 判据落点 |
|---|---|---|
| P-CFG-01 | 损坏配置可恢复 + 明确提示 | `CFGHEALTHCHECK` `CH-01`（注入自证）/ `CH-02`（**启动可恢复**）/ `CH-03`（提示出口）/ `CH-05`（严重度分形态）/ `CH-09`（引导修复账目） |
| P-CFG-02 | 个人版路径隔离 | _已由 T36 `CONFIGCHECK` 覆盖_（本任务只跑回归，不重复主张） |
| P-CFG-03 | 资源目录保护 | `tools/a6-resource-links-check.mjs`（rc 0=PASS/1=FAIL）+ §2 的流程主张 |
| P-CFG-04 | 诊断页与启动日志一致 | `CFGHEALTHCHECK` `CH-07`（6 项逐字段对照）+ `CH-08`（"交换链"如实 N/A） |
| （公共） | 复现注入形态 | `tools/a6-cfg-inject.sh`（6 形态；见 §3.3 为什么放测试装置里） |
| （收尾） | 产品读数与磁盘事实一致 / 状态行渲染一致 | `CH-04`（仅在"产品读的那份文件还在"时判）/ `CH-06` |

> ⚠️ **P-CFG-02 不许搭便车**：它在 T36 已经立过判据，T45 **不重复主张**，
> 只跑回归证明未退化。若本任务文档把 P-CFG-02 记成"T45 交付"，就是**判据搭便车**
> （陷阱 22）。

### 4.0 形态矩阵（6 形态 × 期望严重度）

| form | 脚本写什么 | 期望严重度 | 为什么 |
|---|---|---|---|
| `none` | 不写（个人版目录留空，产品兜底播种） | **1 正常** | 播种出的 `default_cfg.ini` 键数 == 随包默认（252）⇒ **不许修** |
| `truncate` | `default_cfg.ini` 前 40%（99 键） | **3 错误** | < 252 键 ⇒ 不完整 ⇒ 引导修复 |
| `badline` | 一行垃圾 + 一个合法键（1 键） | **3 错误** | 同上 |
| `badutf8` | 合法行 + 值里混非法 UTF-8（1 键） | **3 错误** | 同上 |
| `binary` | 256 字节 0x00–0xFF（0 键） | **3 错误** | 同上 |
| `empty` | 0 字节（0 键） | **3 错误** | 同上 |
| ~~`readonly`~~ | chmod 444 | —— | **刻意不进矩阵**，理由见 §6.5 残余 · 1 |

### 4.1 P-CFG-04 逐字段对照表（静态盘出的**可判面**）

诊断页（`src/ui/qml/DiagnosticPage.qml`）实际读的字段 vs 启动日志里的对应项：

| `BackendInfo` 字段 | 诊断页 | 启动日志对应行 | 可判 |
|---|---|---|---|
| `runtimeApiName` | ✓ | `STELQUICK: runtimeApi=Metal backendOk=1 device=Apple M3（判定来源=兜底）` | ✅ |
| `backendOk` | ✓ | 同上（`backendOk=1`） | ✅ |
| `deviceName` | ✓ | `STELQUICK: probe ok=1 device=Apple M3 api=1.1.357 driver=0.2.2210 portability_driver=1 …` | ✅ |
| `vulkanVersion` | ✓ | 同上（`api=1.1.357`） | ✅ |
| `driverVersion` | ✓ | 同上（`driver=0.2.2210`） | ✅ |
| `portabilityDriver` | ✓ | 同上（`portability_driver=1`） | ✅ |
| `probeError` | ✓ | 同上（`err=` 后缀，正常为空） | ✅ |
| `runtimeDiagAvailable` | ✓ | **无对应**：实现是 `m_mailbox != nullptr`（帧泵是否接上） | ⚪ 不属对照面 —— 它是**运行时装配状态**的反射，启动日志里没有对应量 |
| 帧邮箱 9 项（`frameNumber`/`framesPublished`/…/`sizeGeneration`） | ✓ | 无启动日志行（**运行时轮询量**，由 `refresh()` 取） | ⚪ 同上 |

**🔴 关键发现**：测试文档 P-CFG-04 的原话是「诊断页显示的 **API/GPU/交换链**信息与启动日志一致」——
但**本形态的诊断页里没有"交换链"信息**。

原因有二，都不是缺陷而是**刻意的**：
1. **交换链属 QML/Vulkan 后端**（`QQuickWindow` 的 swapchain），产品侧（`src/app`、`src/ui/quick`）
   拿不到，且**硬性禁区**禁止 QML 侧接触 Vulkan 记号（契约第 1 条）；
2. T39 立 `BackendInfo` 时**刻意不报帧宽高** —— 那些量在 QML 侧随时可读，
   报进 C++ 面会造成"同一事实两个来源"（迟早失同步）。

⇒ **处置**：`CH-0x` 只判"**API / GPU / 驱动**三类字段一致"，**"交换链"一项如实记为
**N/A 并写明理由**。**不得编一个近似量来充数**（陷阱 67：缺前提 ⇒ 退化成平凡真；
陷阱 75：只许验"被声称的命题"——"被声称"的那三项验，"没被声称"的那一项说清为什么没有）。

---

## 5. 环境前置

- **config 零污染门**：脚本前后真实 config.ini md5 必须一致（陷阱 94）。
- 资源链接校验的 `--deep`（全量 sha256）会做重磁盘 IO ⇒ **不放在与帧率测量同批**跑
  （会污染 `LIFECHECK`/帧桥的读数）。
- 探针模式（`STELQUICK_CONFIG_HEALTH_PROBE=1`）自身**不启真实引擎**，
  已在 `main.cpp` 的 `skyFirst` 门里处理。

---

## 6. 本轮实测（2026-10-01）

### 6.1 正题：形态矩阵 ×2（修正 `StelIniFormat` 后的收口轮）

`A6_T45_NEGCTL=… tools/a6-verify.sh t45 2` ⇒ **`SCRIPT-RC=0 FAILED=0 PASS=3`**：

| form | 期望 sev | run1 / run2 | VERDICT | 判据 |
|---|---|---|---|---|
| `none` | 1 | rc=0 / rc=0，sev 观测 1/1 | PASS ×2 | 8/9（CH-04 前提不齐 SKIP，不记 FAIL） |
| `truncate` | 3 | rc=0，sev 3/3 | PASS ×2 | 9/9 |
| `badline` | 3 | rc=0，sev 3/3 | PASS ×2 | 9/9 |
| `badutf8` | 3 | rc=0，sev 3/3 | PASS ×2 | 9/9 |
| `binary` | 3 | rc=0，sev 3/3 | PASS ×2 | 9/9 |
| `empty` | 3 | rc=0，sev 3/3 | PASS ×2 | 9/9 |

形态矩阵 **12/12 全绿**；六形态的严重度读数与 §4.0 期望**逐位一致**
（`none` sev=1 keys=252 repaired=0；损坏五形态 sev=3、repaired=1、keys=99/1/1/0/0）。

### 6.2 负控：四条（期望值实跑两轮逐位一致后写死，陷阱 87）

| 负控 | 开关 + 形态 | rc | 红项（两轮逐位一致） | SKIP | 证明什么 |
|---|---|---|---|---|---|
| `ISOLATE_OFF` | `STELQUICK_CFG_ISOLATE_OFF=1` + `full@orig` | 5 | `{CH-02, CH-03, CH-05, CH-06}` | `{CH-04}` | 关隔离 ⇒ isolated=0 ⇒ CH-02 必红；健康读整段被跳过 ⇒ CH-03/05 红 |
| `STATUS_OFF` | `STELQUICK_CFG_STATUS_OFF=1` + `truncate` | 5 | `{CH-03, CH-04, CH-05}` | `{}` | **修复照常**（`repaired=1`，日志自证"修复是正确性，不是可观测性"），只有报告被关 |
| `REPAIR_OFF` | `STELQUICK_CFG_REPAIR_OFF=1` + `truncate` | **139（无判据输出）** | — | 0 | **修复腿承重的铁证**：不修 ⇒ 引擎 SIGSEGV（`keys=99 repaired=0` ⇒ 崩） |
| `DRIFT` | `STELQUICK_CFGHEALTH_DRIFT=1` + `none` | 5 | `{CH-07}` | `{CH-04}` | 诊断面与启动日志漂移 ⇒ 恰好只红一致性判据 |

四组红项集合**互不相同**且与关掉的东西一一对应（陷阱 22 不搭便车）。
台账（期望值）已写死进 `tools/a6-verify.sh` 负控段头注。

### 6.3 P-CFG-03 / P-CFG-02 的收口读数

- 资源链接：`node tools/a6-resource-links-check.mjs` ⇒ **rc=0，`VERDICT=PASS`**
  （资源链接结构完好；materialize 临时残骸 0；资源路径无 git 改动）。
- P-CFG-02 回归：T36 `CONFIGCHECK` **rc=0（8/8）**，未退化（CFG-05 `缺失=0`）。
- **config 零污染门**：`✅ 前后一致（c847cd85…）`。

### 6.4 本轮抓出的两个真缺陷（都修了）

1. **健康读用错解析器**（`ANOMALY-config-rewrite-2026-10-01.md` §4）：plain
   `QSettings::IniFormat` 与引擎 `StelIniFormat` 对同一文件**分歧 83 键**
   （771 vs 688）⇒ 健康读改用 `StelIniFormat`。
2. **ISOLATE_OFF 负控的装置缺陷**：损坏文件放进个人版目录、但隔离已关 ⇒
   产品用**原目录**（空）⇒ 测到的是"没配置 ⇒ 崩"而不是"隔离关掉"。
   修法 = 注入器加第三参数 `original` + `full` 形态（`full@orig`）；
   判据侧配套：`none`/`full` 形态**不要求注入字节存活**（引擎起得来必然整体重写完整配置）。

### 6.5 残余

1. **`readonly` 形态的结构性困境（不修，记录在案）**：个人版配置不可写 ⇒ 引擎
   `findFile("config.ini", Writable|File)` 落空 ⇒ 退到 `findFile(…, New)` ⇒ 在**安装目录**
   （进程 cwd）新建 `config.ini` ⇒ 测到的不是"个人版只读"，还污染仓库根与后续 run。
   处置：移出形态矩阵 + 脚本加**卫生门**（仓库根出现未跟踪 `config.ini` ⇒ 记账 + 清理，
   被跟踪文件则拒绝自动删）。根治要动引擎 `findFile` 语义，移交。
2. **`b1e44 → 6f64` 一次性改写的触发方未决**（§6.4-1 的上游疑云）：见
   `ANOMALY-config-rewrite-2026-10-01.md` §5 —— 已排除普通启动/哨兵写/持续丢失五个假设；
   剩余嫌疑 = 跨格式改写（老 QWidget 对话框路径）+ `StelIniFormat` 读取器漏解析后写丢。
   待办：分歧键型专项探针 + "写 config.ini 必须用 `StelIniFormat`"升为纪律。
3. **CH-04 在 `none`/`full` 形态必然 SKIP**：引擎整体重写配置 ⇒ "产品读的那份原始文件"
   已不在磁盘上，无从对照（判据如实 SKIP，不记 FAIL —— 陷阱 3/纪律第三态）。
4. **`REPAIR_OFF` 的证据形态是"rc=139 + 无判据输出"**：修复腿承重的证明只能靠
   `CONFIGISO` 日志行（`keys=99 repaired=0` ⇒ 崩）+ rc，不是一条 FAIL 判据 ——
   人工核对台账时按这个口径读。

---

## 7. 结论与移交

### 7.1 四条 P-CFG 的最终口径

| 编号 | 结论 | 证据 |
|---|---|---|
| P-CFG-01 损坏配置可恢复 + 明确提示 | ✅ **成立（产品侧两腿）**：①引导修复腿（备份 `.corrupt` → 重建随包默认；不修则引擎 SIGSEGV，`REPAIR_OFF` 负控为铁证）；②报告腿（`ErrorModel` 新增「配置文件」状态行，严重度由 `ConfigIsolation` 引导期唯一判定） | `CFGHEALTHCHECK` 形态矩阵 12/12 + 负控 4 组正交 |
| P-CFG-02 个人版路径隔离 | ✅ **T36 既有判据，本轮回归 8/8 未退化**（不重复主张 —— 陷阱 22） | `CONFIGCHECK rc=0` |
| P-CFG-03 资源目录保护 | ✅ 可判量由脚本判（rc=0 PASS）；"materialize 前写操作被流程禁止"是流程主张，见 §2 与 `A6_RESOURCE_PACKAGING.zh_CN.md` | `resource-links.txt` |
| P-CFG-04 诊断页与启动日志一致 | ✅ API/GPU/驱动 6 项逐字段一致（CH-07）；"交换链"**如实 N/A**（诊断面无此信息是刻意的，CH-08 把"不主张"钉成断言） | `CFGHEALTHCHECK` + §4.1 |

### 7.2 移交

- **W-T45（Windows 复验）**：`CFGHEALTHCHECK` + 形态注入器 + 负控四条，脚本已跨平台
  （纯 zsh + `stat -f%z` 需换成 Windows 侧口径；`.ps1` 纯 ASCII 纪律照旧）。
- **"写 config.ini 必须用 `StelIniFormat`"升为产品纪律**：任何新代码读写配置一律走
  `StelApp::getSettings()` 或 `StelIniFormat`，**禁止** plain `QSettings::IniFormat`。
  判据侧独立解析同一文件时同格式（陷阱 86 变体）。
- **分歧键型专项探针**（§6.5-2 未决项）：拿"跨格式改写后的样本"验证 `StelIniFormat`
  读取器在哪类键上漏解析；结论出来后决定是否给健康读加"两解析器交叉验证"。
- **`.corrupt` 备份堆积**：同一文件反复损坏会堆 `config.ini.corrupt.N`（上限 10），
  目前无自动清理 —— 移交产品决策（是否提示用户手动清理）。
- **修复文案的措辞**：状态行主张的是「不修就起不来，修了才起得来」，
  **不主张「回退默认」**（那是 Qt 的语义，探针 Q2 —— 陷阱 75）。

### 7.3 证据清单（`docs/evidence/2026-10-01-a6-configsafety/mac/`）

- `probe-confighealth-mac.txt` —— T45-A 探针 23 条读数（前后 md5 自证零污染，6/6 临时文件自净）
- `cfghealthcheck-{form}-run{1,2}.txt` ×12 —— 形态矩阵正题
- `cfghealthcheck-negctl-*.txt` ×4 + `inject-negctl-*.txt` ×4 —— 负控与注入记录
- `configcheck-regress.txt` —— T36 回归（8/8）
- `resource-links.txt` —— P-CFG-03（VERDICT=PASS）
- `ANOMALY-config-rewrite-2026-10-01.md` —— 真实配置改写异常的完整调查（含对照实验与恢复处置）

**已知残余（先记）**：
1. `plugins/ scripts/ util/` 三个链接目录**未纳入** `source-manifest.json` 的 9 个覆盖范围
   ⇒ 是否纳入 materialize 需在计划二前定（已在 `A6_RESOURCE_PACKAGING` §7 标注）。
2. App bundle 内资源落点（`Contents/Resources` 下的实际布局）**未验证** ——
   本轮只验了源码树里的占位形态。
3. P-CFG-04 的"交换链信息"**已由 §4.1 静态盘出：本形态诊断页确实没有这一项**
   （交换链属 QML/Vulkan 后端，产品侧拿不到；且硬性禁区禁止 QML 侧接触 Vulkan 记号）。
   ⇒ 判据只判 API/GPU/驱动三项，交换链**如实记 N/A + 理由**，不得编近似量充数
   （陷阱 67 / 75）。
