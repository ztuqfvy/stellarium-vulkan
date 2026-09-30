# T36 — 个人版配置目录隔离 + 首次播种（A5 第一项）

> 任务来源：A-1.0 范围表里那句**「配置使用独立个人版目录，避免修改原程序设置」**。
> 进 A5 前先做**探针**，结果坐实了一个**真缺陷**：合流形态（`stelQuickUI`）此前直接用
> 引擎**默认**用户目录 = **原版 Stellarium 的用户目录**，跑一次就改原版配置与日志。
>
> **结论：缺陷已修，并且修的过程中又牵出第二条腿 —— 空用户目录会让引擎 SIGSEGV。**
>
> 改动面：`src/app/ConfigIsolation.{hpp,cpp}`（**新**，引导期隔离+播种）｜
> `src/app/ConfigIsolationCheck.{hpp,cpp}`（**新**，判据 CFG-01..CFG-08）｜
> `src/ui/LiveSkyRuntime.cpp`（**引导序列插入一行调用 —— 位置即判据**）｜
> `src/ui/main.cpp`（接线 `STELQUICK_CONFIG_CHECK`）｜`src/ui/CMakeLists.txt`｜
> `tools/t36-verify.sh`（**新**）。
> **产品语义零改动**：没有一行改了时间/渲染/命令的行为；只在引导期换了个用户目录。

---

## 1 探针：写穿是**实测**的，不是推测的

仪器：`tools/t36-probe-*.zsh` + `docs/evidence/2026-09-30-t36-config/probe/`。
做法：跑一次 `STELQUICK_TOOL_CHECK=1`（`CONFIGCHECK` 还不存在时的最简合流形态），
对原目录做**逐文件 `relpath|size|mtime|md5` 清单**，跑前跑后比对。

结果：`diff pre.manifest post.manifest` **恰好 3 处变化**：

| 文件 | 变化 | 性质 |
|---|---|---|
| `config.ini` | mtime `1790740596` → `1790745245`（md5 **不变**） | 被 QSettings 整体重写，内容恰好没变 |
| `log.txt` | `10308 B/579a81…` → `10387 B/611bcc…` | **原版日志被顶掉**（`StelLogger::init` 截断覆盖） |
| `modules/Oculars/ocular.ini` | mtime 变 | 模块自己的写回 |

`config.diff`（两个 config.ini 的内容 diff）**0 行** —— 这一条很关键：
**"内容没变"不等于"文件没被碰"**。按内容比对会得出"没写穿"的错误结论。

### 1.1 三条写穿路径

| 路径 | 出处 | 触发条件 |
|---|---|---|
| 配置直写 | `StelApp::immediateSave()`（`StelApp.cpp:1404-1408`） | `getFlagImmediateSave()` 为真 ⇒ `getSettings()->setValue()`。本机 `config.ini` 里 `immediate_save_details = true` ⇒ **翻一个开关就落盘** |
| 日志覆盖 | `StelLogger::init(userDir + "/log.txt")`（`LiveSkyRuntime.cpp:52`） | 每次启动，**截断**原版日志 |
| 模块写回 | 模块用 `findFile(..., Writable\|File)` 找到自己的数据文件 | 如 `modules/Oculars/ocular.ini` |

### 1.2 为什么不能"把原目录当只读回退"

直觉方案：`fileLocations = [个人版, …, 原版]`，个人版没有的数据文件回退到原版去**读**。
**这条路是漏的**，两条语义决定了它：

- `StelFileMgr::findFile()`（`StelFileMgr.cpp:181-228`）是"按 `fileLocations` 顺序返回
  **第一个满足 flags** 的路径"；
- `fileFlagsCheck()`（`:364-395`）里 `Writable` **只表示"那个文件可写"**。

⇒ 模块用 `findFile("modules/Satellites/tle0.txt", Writable|File)` 更新 TLE 时会命中
**原版**里的副本并**原地改写原版** —— 正是要消除的行为。

⇒ 只能**先把原目录播种到个人版目录**，之后个人版目录里**什么都有**，原目录彻底退出搜索路径。
（`setUserDir()` 是 `fileLocations.replace(0, userDir)`（`:422-428`），**不是 append** ⇒ 换掉即出局。）

---

## 2 第二条腿：空用户目录 ⇒ 引擎 **SIGSEGV**（rc=139）

缺陷修完、开始测边界时撞上的：

```
STEL_USERDIR=/tmp/t36-empty STELQUICK_CONFIG_CHECK=1 stelQuickUI
⇒ rc=139
⇒ stderr 停在 "LandscapeMgr: initialized Cache for 100 MB."
```

**分块播种二分**（把候选数据一块块丢进空目录，看哪块能让它起来）：

| 播种内容 | rc | 最后 stderr |
|---|---|---|
| 什么都不放 | **139** | `LandscapeMgr: initialized Cache for 100 MB.` |
| 只放 `stars/` | **139** | 同上 |
| 只放 `modules/` | **139** | 同上 |
| 只放 `data/` | **139** | 同上 |
| **只放 `config.ini`** | **10** | `Creating scene FBO with size 2560x1440` |

⇒ 崩因是「**用户目录里没有 `config.ini`**」，**不是缺数据**。

**上游本来就是这么处理的**：`src/main.cpp:398-403`

> `Config file … does not exist. Copying the default file.` ⇒ `copyDefaultConfigFile()`
> （`src/main.cpp:130-142`：从 `data/default_cfg.ini` 拷成 `config.ini` 并补 `WriteOwner` 权限）

**合流形态此前根本没有这一步** —— 只是因为"原目录里早就有 `config.ini`"一直被掩盖着。
一旦用户目录变成我们自己的个人版目录（或用户从没装过原版 Stellarium），
这个洞就会在**全新机器**上露出来。

⇒ 补上兜底腿，且**不随** `STELQUICK_CFG_MIGRATE_OFF` 关闭：
**播种是"搬用户的旧东西"，兜底是"没有旧东西也要能起来"，两件事。**

### 2.1 判定 crash 点的仪器教训

第一次定位时被**块缓冲**骗过：stdout 是块缓冲、stderr 不缓冲，两者混在一个重定向文件里，
"最后一行"落在插件加载区 ⇒ 一度误判成"崩在插件加载"。**分流失重跑**（`1>out 2>err`）
才看出 stderr 同样停在 `LandscapeMgr: initialized Cache`。

---

## 3 产品实现

### 3.1 落点：`<引擎默认用户目录> + "-quick"`

macOS：`~/Library/Application Support/Stellarium-quick`（**同级目录**，不是原目录的子目录）。

| 为什么同级 | 效果 |
|---|---|
| 原目录**整体**不动 | 连目录 mtime 都不变 ⇒ "原目录零改动"是**结构性**成立、可按字节比对 |
| 不嵌子目录 | 不会有"创建子目录改了父目录 mtime"的伪差异 |
| 不自己推导平台语义 | 一律以 `StelFileMgr::getUserDir()` 为基准加后缀，**不复刻** `QDir::homePath()` / `CSIDL_APPDATA` |

### 3.2 调用契约（硬性）

1. 必须在 `StelFileMgr::init()` **之后**（依赖 init 建好的 `fileLocations[0]`）；
2. 必须在 `StelLogger::init()` 与**任何** `findFile("config.ini")` **之前** —— 晚一步就写穿；
3. **幂等**：重复调用返回首次记录，不会推出 `-quick-quick`。

`LiveSkyRuntime::boot()` 里的落点（**位置即判据**）：

```cpp
StelFileMgr::init();
bootstrapPersonalConfigDir();            // ← T36 插在这里，别无选择
const QString userDir = StelFileMgr::getUserDir();
StelLogger::init(userDir + "/log.txt");
...
m_confSettings = new QSettings(configFileFullPath, StelIniFormat, nullptr);
```

### 3.3 流程

```
originalDir = StelFileMgr::getUserDir()
  ├─ STELQUICK_CFG_ISOLATE_OFF ？ → 不动目录，记 info，返回（负控 A）
  ├─ personalDir = originalDir + "-quick"
  ├─ StelFileMgr::setUserDir(personalDir)      ← 抛异常则记 note 并退回原目录（不静默降级）
  ├─ 首启播种（触发 = 个人版无 config.ini）：递归拷贝，跳过瞬态
  │    跳过：log.txt / output.txt / config.old（派生/瞬时产物）
  │    已存在的不覆盖（skippedExisting）
  │    STELQUICK_CFG_MIGRATE_OFF 只关这一步（负控 B）
  └─ 兜底腿：个人版仍无 config.ini ⇒ 从 data/default_cfg.ini 拷一份 + 补 WriteOwner
       **不受 MIGRATE_OFF 影响**
```

### 3.4 `note` 与 `info` 必须分开（**本轮返工点**）

`ConfigIsolationReport` 里有两个字符串字段，**差别是语义不是内容**：

| 字段 | 含义 | 影响判据？ |
|---|---|---|
| `note` | **降级/异常**（`setUserDir` 抛异常、兜底拷贝失败） | **是**：非空 ⇒ CFG-01 报红 |
| `info` | **正常**流程说明（"已存在 config.ini ⇒ 跳过播种"、"负控生效"、"原目录不存在"） | **否**，纯留痕 |

第一版把两者混用一个 `note`，后果是 **CFG-01 在"非首次启动"和负控 B 下假红**
（实测：负控 B 多红一条 CFG-01）。**CFG-01 验的是"隔离到底生没生效"，
跟"这次跳不跳播种"根本不是一回事。**

---

## 4 判据 CFG-01..CFG-08（`STELQUICK_CONFIG_CHECK=1`）

**不启帧泵、无就绪门** —— 全是同步事实（目录路径、文件字节、`sync()` 后的落盘内容）。

| 判据 | 内容 | 承重点 |
|---|---|---|
| **CFG-01** 隔离读数自洽 | `isolated=1` ∧ `note` 空 ∧ 独立重读 `getUserDir()` == 记录的 `personalDir` ∧ ≠ `originalDir` | 独立回读（陷阱 43），不复述自己的意图 |
| **CFG-02** 目录独立 | personalDir 存在/是目录/可写（真写临时文件再删）∧ 不是原目录的子树 ∧ 不同路径 | 前缀判断按**路径段**，防 `/a/bc` 被 `/a/b` 命中 |
| **CFG-03** 配置写侧落点 | `getSettings()->fileName()` 落在 personalDir 之下 **∧ 两目录不重合** | ★ 最强的一条：`fileName()` 是**引擎此刻真会写的文件**。后半句不能省，否则隔离关掉时**自洽假绿** |
| **CFG-04** 日志落点 | `personalDir/log.txt` 存在 ∧ 非空 ∧ 比原目录那份新 | 原目录那份是原版自己的历史日志 |
| **CFG-05** 配置种子完整 | 原目录有配置 ⇒ 个人版键集 ⊇ 它的（**只比键不比值**）；原目录无配置 ⇒ 个人版有一份非空的 | 两叉都不能省（否则全新机器恒红） |
| **CFG-06** 写侧不触原目录（成对） | 哨兵值 ①出现在个人版 ②不出现在原目录 ③原目录原本没有 config.ini 时跑完仍**不许出现** | ③ 是**非平凡化**（见 §4.1） |
| **CFG-07** 产品路径落点（成对） | `flagImmediateSave` 置真 ⇒ `ActionRouter.trigger("actionShow_Constellation_Lines")`（**用户点按钮走的就是这条路**）⇒ ①个人版该键 = 翻转后的值 ②原目录未被改动 | 判据自己读+强制+收尾还原 `immediateSave`（陷阱 44） |
| **CFG-08** 播种清单完整 | 原目录除瞬态外的每个文件，在个人版目录里都存在（只比存在性） | 守"数据没丢"：`modules/Satellites/tle*.txt`（20 MB 下载数据）等**不在安装目录里**，不播种就找不到 |

### 4.1 判据在"缺前提"的机器上会退化成**平凡真**（本轮新增纪律）

CFG-06/CFG-07 的原目录侧，机制是"读原目录 `config.ini` 比对"。
但在**全新机器**上原目录**没有** `config.ini`：

- CFG-06：`readAllText` 返回空串 ⇒ "不含哨兵"**天然成立**；
- CFG-07：`md5OfFile` 两次都返回 `<unreadable>` ⇒ "md5 不变"**天然成立**。

⇒ 两条判据在该机器上**不承重**。补第三半：
**"原本不存在 ⇒ 跑完仍不许出现"**（写穿会创建它）。加固后两条在所有机器上都承重。

判据的**承重性**另有独立证明：负控 A 跑在**含 `config.ini` 的替身目录**上，
CFG-06/CFG-07 确实变红（见 §5）。

---

## 5 两组负控（红项集**两两不同**，各自承重）

| 负控 | env | 期望红项 | 机制 |
|---|---|---|---|
| **A** 关隔离 | `STELQUICK_CFG_ISOLATE_OFF=1` | `[CFG-01,CFG-02,CFG-03,CFG-04,CFG-06,CFG-07]`（2/8） | 行为回到修复前（直接用原目录）⇒ 写穿 |
| **B** 关播种 | `STELQUICK_CFG_MIGRATE_OFF=1` | `[CFG-05,CFG-08]`（6/8） | 隔离在、但个人版目录空着 ⇒ 配置种子与数据种子两条腿断 |

两条**绿项**的解释（不然会以为是漏）：

- 负控 A 的 CFG-05 绿：`personal == original`，那个文件**当然**包含自己的键；
- 负控 A 的 CFG-08 绿：同一目录，文件**当然**"都在"；
- 负控 B 的 CFG-06/CFG-07 绿：兜底腿在，写侧仍然只写个人版。

### 5.1 负控跑在**替身用户目录**上（安全设计）

负控 A **故意**让程序写穿"原目录"。拿真实原版目录去撞 = 拿用户真实配置当耗材。

⇒ 负控统一用 `STEL_USERDIR=/tmp/t36-fake-original`（真实原目录的一份拷贝）
⇒ 派生的"原目录"是 `/tmp/t36-fake-original-quick`，**真实原目录全程零风险**。
判据全是路径/字节事实，替身上同样成立。

脚本里的**硬安全门** `guard_paths()`：`PERSONAL` 必须匹配 `*/Stellarium-quick`、
`≠ ORIG`、`ORIG` 非空非 `/`；`rm_personal()` / `rm_fake_all()` 都先过门；
`FAKE` 必须是字面量 `/tmp/t36-fake-original`。

---

## 6 实测结果（macOS，Metal/MoltenVK，2026-09-30）

产物：`stelQuickUI` **40096328 B `md5=dd1b0eb63669adc0a0ee7b623f9b23d8`**。

| 场景 | 结果 |
|---|---|
| **S1 首次**（清空个人版目录） | `rc=0`、**8/8 PASS**；`migration=ran files=57 bytes=28405076` |
| **S2 非首次**（个人版预埋用户哨兵） | `rc=0`、**8/8 PASS**；`migration=skipped`；哨兵 `keep_me=12345` 原样保留 |
| **全新机器**（`STEL_USERDIR=/tmp/t36-empty`） | `rc=0`（**此前 139**）、**8/8 PASS**；`migration=ran files=0`、`defaultConfigSeeded=1` |
| **负控 A** | `rc=10`、2/8、红项**恰好** `[CFG-01,CFG-02,CFG-03,CFG-04,CFG-06,CFG-07]` |
| **负控 B** | `rc=10`、6/8、红项**恰好** `[CFG-05,CFG-08]` |

**外层对照量（换来源，陷阱 43）**：

- **原目录逐文件 `size+mtime+md5` 清单**：S1 前后 **0 差异**、S2 前后 **0 差异**、
  合流形态全程 **0 差异** ⇒ 缺陷已修；
- **播种清单独立复算**（脚本自己 `find`，不采信 CFG-08 读数）：原目录 57 个非瞬态文件，
  个人版缺 **0**；
- **配置键集独立解析**（脚本自己按行解析，不用 QSettings）：原目录 **768** 键，
  个人版缺 **0**。

**负控 B 的定量读数**（事先量过，不是事后调判据）：
`data/default_cfg.ini` 239 键 vs 原目录 768 键 ⇒ **至少 529 键不在默认里**；
实测个人版缺 **348** 键（差额由引擎引导期自己补写的默认值填上）。
文件侧缺 **49**（首个 `modules/Satellites/tle10.txt`）。

**相邻回归全绿**：12 套件（timecheck/returnuicheck/searchcheck/actioncheck/locatecheck/
locate-uicheck/replaycheck/clockcheck/timeuicheck/locationcheck/toolbarcheck/timelinkcheck）
全 `rc=0`；`INTERACTCHECK` **18/18（本批拿到焦点）**；S3 旧宿主 `rc=0`；
A2 `rc=0`；DYN 双路 **3/3 + 3/3**。**脚本层判据 23 通过 / 0 失败，`SCRIPT-RC=0 FAILED=0`。**

---

## 7 本轮三条新血泪（已入 `TRAPS.md` 65–67）

### ① 🔴 判据的**条件**必须与判据的**名字**同义 —— 字段语义混用会造成假红

CFG-01 叫"隔离读数自洽"，条件里却有 `note.isEmpty()`。而 `note` 被同时当成
"降级通道"和"正常信息通道"用 ⇒ **非首次启动和负控 B 都会把正常路径写进 note ⇒ 假红**。

**定性**：不是"结果不好就改判据"，而是**条件与语义不符**：
`isolated=1` ∧ `userDir` 正确时，隔离**就是**自洽的，note 里写什么无关。
修法是**拆字段**（异常 vs 正常），不是删条件。

### ② 🔴 外层判据的**作用域**必须等于被测对象的**作用域**

脚本原本有一条收尾判据"整个脚本期间原目录零改动"，而 ④ 段里混跑了 **S3 旧宿主
`stellarium`（QWidget）**。旧宿主**不走** `LiveSkyRuntime` 的隔离引导 ——
**它就是"原版程序"的等价物，写原版用户目录是它应有的行为**。

⇒ ⑤ 因 4 个文件（`config.ini` / `log.txt` / `modules/Oculars/ocular.ini` / `output.txt`）
报红，是**判据范围划错造成的假红**，不是产品缺陷。

修法：把 S3 从"零改动"核对里**移出去**（挪到核对之后单跑，只验 `rc`），
并把它对原目录的影响**如实留证不作判据**（16 行）。
**A-1.0 要求隔离的是个人版 UI，不是旧形态宿主。**

### ③ 🔴 判据在"缺前提"的机器上会退化成平凡真

见 §4.1。**教训**：写"两侧比对"型判据时，先问一句
**"如果其中一侧根本不存在，这条判据还承重吗？"** —— 不承重就要补非平凡的那一臂。

### 附：两条仪器教训

- ⚠️ **stdout 块缓冲 / stderr 不缓冲**：混流重定向时"最后一行"不可信。
  判 crash 点必须**分流失**重跑。
- ⚠️ `StelFileMgr::findFile` 的 `Writable` **只保证"那个文件可写"**，
  不是"这个位置是新目录" ⇒ **"原目录当只读回退"在模块写回场景是漏的**。

---

## 8 A5 里的位置与后续

A-1.0 范围表相关行（原文）：

> 「资源路径、错误提示、渲染诊断、配置保存 | 最小 | **必须** | **配置使用独立个人版目录，
> 避免修改原程序设置**」

⇒ 本条**完成**。A5 剩余项（本节之后的顺序）：

| 序 | 内容 | 备注 |
|---|---|---|
| **T37** | 设置页骨架 + **夜视闭环** | ⚠️ T34 已把 `actionShow_Night_Mode` 接进工具栏，但**效果本身从未验证**：`StelApp::draw()` 直接画模块、`QGraphicsEffect` 在 QGraphicsScene 层 ⇒ 夜视红色滤镜**很可能不在读回帧里**，需先探针定性 |
| T38 | 显示参数：星等/亮度 + 视场 + 投影 | |
| T39 | 高 DPI + 渲染诊断（capabilities/status 预留，交计划二硬性契约第 7 条） | |
| T40 | 快捷键编辑 | 「中文输入焦点不能触发天空快捷键」已由 T29 兜住 |
| T41 | 帮助 / 版本 / 许可证页 | |
| T42 | 错误页 + "未支持项"清单 | 旧 UI **禁止**作为隐式回退 |

---

## 9 产物清单

**mac 侧（新增）**

- `src/app/ConfigIsolation.hpp` / `.cpp`（引导期隔离+播种+兜底）
- `src/app/ConfigIsolationCheck.hpp` / `.cpp`（CFG-01..CFG-08）
- `tools/t36-verify.sh`（`core N` / `negctl` / `regress` / `all N`）
- `docs/T36_CONFIG_ISOLATION.zh_CN.md`（本文）
- `docs/evidence/2026-09-30-t36-config/probe/`（探针：`pre/post.manifest`、`config.diff`、`run1.out`、`snap.zsh`、`README.md`）
- `docs/evidence/2026-09-30-t36-config/mac/`（`configcheck-{first,second,empty-machine}.txt`、
  `negctl-A/B-*.txt`、`orig-manifest-*`、`fileset-*`、`keys-*`、`regression-*`、`rc-summary.txt`）

**修改**

- `src/ui/LiveSkyRuntime.cpp`（引导序列插入 `bootstrapPersonalConfigDir()`）
- `src/ui/main.cpp`（`STELQUICK_CONFIG_CHECK` 接线 + 用法头注）
- `src/ui/CMakeLists.txt`（四个新源文件 + "位置即判据"注释）
