# A6 资源打包说明（materialize 符号链接目录）

> **出处**：开发指导文档 A6 行 ——
> 「**资源打包说明（先 materialize 符号链接目录）**、接口冻结、渲染诊断能力预留」。
> 配套判据：测试文档 §6.3 `P-CFG-03｜资源目录保护｜只读资源链接不被生成工具改写；
> materialize 前对链接目录的写操作被流程禁止`。

---

## 1. 现状：资源是**链接**，不是副本

本仓库是 Stellarium 的「改写研究副本」，**不跟踪上游资产**（`.gitignore` 已排除）。
要跑**根构建**（`cmake -B build-release .`）必须先跑：

```
tools/setup-upstream-assets.sh          # 默认上游 ~/qt_demo/stellarium/stellarium
STELLARIUM_UPSTREAM=/path ./tools/setup-upstream-assets.sh   # 或指定
```

它按**两类**处理，**不可混用**：

| 类 | 目录 | 方式 | 理由 |
|---|---|---|---|
| **配置期只读** | `atmosphere` `landscapes` `models` `nebulae` `po` `scenery3d` `skycultures` `stars` `textures` `plugins` `scripts` `util`（**12 个**） | **符号链接** → 上游 | 只有"读"，链接零成本且不占空间 |
| **配置期被写入** | `data`（1 个） | **实体副本**（`cp -R`） | 根 CMake 配置期会 `CONFIGURE_FILE` 写 `data/default_cfg.ini`、`data/Info.plist`；**若是软链会穿透改写上游源码树** —— 正是 04 号文档风险表里那条"通过符号链接误改原项目资源" |
| 仅需存在 | `guide/` | 空目录 | CMake 要往 `guide/version.tex` 写；完整 `Images/`（~50 MB）只用于生成 guide.pdf，与程序构建无关，故不复制 |

⚠️ **`src/ui` 独立工程（`cmake -B build-ui -S src/ui`）不需要这些资产** ——
只有根构建需要。

---

## 2. 为什么分发包里不能是链接

链接指向的是**开发机上的上游 checkout 路径**（如
`/Users/ztuqfvy/qt_demo/stellarium/stellarium/textures`）。
这种路径在别人的机器上、或在 App bundle 的沙箱内**不存在**，分发出去必然打不开资源。

⇒ 打包前必须先把**九个**有内容契约的资源链接 **materialize 成实体目录**。

> **为什么只九个**：`tools/materialize-resources.mjs` 只处理
> `docs/vulkan/source-manifest.json` 里登记的那 **9 个**（有逐文件 sha256 的那批：
> `models` `textures` `landscapes` `skycultures` `nebulae` `stars` `atmosphere`
> `scenery3d` `po`）。
> **剩下的 3 个链接目录**（`plugins` `scripts` `util`）**不在 materialize 范围内** ——
> 它们是构建期辅助内容，不是运行时资源。分发包是否需要它们，由打包形态决定
> （**本轮未做**，列为分发包工作的前置问题，见 §7 残余）。

---

## 3. materialize 流程（`tools/materialize-resources.mjs`）

```
node tools/materialize-resources.mjs --copy      # 需要约 1 GiB 空闲空间
```

逐步行为（**每一步都先校验、再替换**，失败不破坏现状）：

1. 读 `docs/vulkan/source-manifest.json`，逐项取 `resources[]`。
2. 对每个资源路径：
   - 若**已经是实体目录** ⇒ 打印 `Already independent` 并跳过（幂等）。
   - 若**不是符号链接** ⇒ 抛错退出（不认识的东西不碰）。
   - 若符号链接的 target **与清单不符** ⇒ 抛错退出（**只动清单里认得的那个链接**）。
3. 在**同盘临时兄弟目录** `.materialize-<路径>-XXXX/` 里 `cp -R`（保留链接语义，
   但过滤 `.DS_Store`）。
4. 对拷贝结果做**逐文件 sha256 校验**（对清单里登记的每个文件）：
   - 任一不符 ⇒ **抛错并保留原链接**，临时目录留在磁盘上供检查。
5. 校验通过后：**只 `unlink` 那个已知的符号链接**（绝不删它的 target），
   再把临时目录 `rename` 到目标位置。
   - `rename` 失败 ⇒ **把符号链接恢复回去**，再抛错。
6. 删除临时目录，打印 `Copied and verified: <路径>`。

★ 设计要点（值得记住的三条）：
- **先造副本、验完再替换** —— 中途失败时原链接仍在，工程仍可构建。
- **只 unlink 已知链接，绝不递归删除 target 目录** —— 上游源码树是"没有 git 保护的裸 checkout"，
  删了就没了。
- **sha256 校验是硬门** —— 拷贝过程出问题（磁盘满/中断）会被这一步拦下，而不是等到运行时黑屏。

---

## 4. 反向：怎么回到链接态

materialize 是**单向**的（脚本没有反向模式）。要回到"链接 + 省空间"的开发形态：

```
rm -rf models textures landscapes skycultures nebulae stars atmosphere scenery3d po
tools/setup-upstream-assets.sh        # 会重新建链接（它跳过已存在实体，故必须先删）
```

⚠️ `rm -rf` 这些目录**是安全的**（它们是副本），但**必须先确认它们已不是符号链接** ——
若误对着符号链接 `rm -rf`，某些 shell/工具的行为会穿透删除上游内容。
**先 `ls -la | grep '^l'` 确认，再删。**

---

## 5. 流程守则（"materialize 前禁止写链接目录"）

| 守则 | 为什么 |
|---|---|
| **任何生成工具不得写入链接目录** | 写会穿透到上游源码树，而上游**不是 git 仓库**（无版本保护） |
| 需要写入的目录一律用**实体副本**（现行：`data`） | 同上；这是 `setup-upstream-assets.sh` 分两类的唯一理由 |
| 改动前先 `tools/a6-resource-links-check.mjs` | 秒级确认当前占位形态与清单一致，再动手 |
| 破坏性操作前先备份 | 用户通用准则；对上游树尤其如此 |

**历史教训（本项目实际踩过）**：早期把 `models` 等**指向上游的软链**误当成本地目录，
写了一个探针输出文件进去，结果落在上游 checkout 里 ——
这正是 04 号文档风险表那条"通过符号链接误改原项目资源"的现场。
现在的防线是：①`setup-upstream-assets.sh` 对会被写的目录用副本；
②`P-CFG-03` 判据（§6）；③`materialize` 只在 `--copy` 显式模式下运行。

---

## 6. 校验（判据入口）

| 工具 | 查什么 | 速度 |
|---|---|---|
| `node tools/a6-resource-links-check.mjs` | 9 个清单资源的占位形态（链接/实体/缺失）、target 与清单一致且存在、materialize 临时残骸、**清单外链接**与 `data/` 的性质、资源路径下 git 是否有改动 | 秒级 |
| `node tools/a6-resource-links-check.mjs --deep` | 追加：4150 个文件逐一 sha256 比对清单 | 分钟级 |
| `node tools/source-snapshot.mjs verify` | 内容级全量审计（与上一条同源，另有上游树结构校验） | 分钟级 |

`--deep` 与 `verify` 的区别：前者只看**清单登记的文件**；后者还会检查上游树的
**结构**（目录集合、有无嵌套符号链接、有无非普通文件）。

---

## 7. 空间与残余

**空间预算**（本机实测，供打包决策）：

| 项 | 量 |
|---|---|
| 9 个资源的清单文件数 | **4150** 个 |
| materialize 所需空闲空间 | 脚本自报「约 1 GiB」 |
| `guide/Images/`（**不复制**） | ~50 MB |

**残余（诚实标注）**：

1. **`plugins` / `scripts` / `util` 三个链接目录未纳入 materialize**。
   它们不在 `source-manifest.json` 里（没有逐文件 sha256），
   也不在运行时资源清单里。**分发包是否需要它们未定** —— 这取决于打包形态，
   是**分发包工作的第一个待决问题**，本轮不做。
2. **没有"一键回到链接态"的反向脚本**（§4 是手工步骤）。
   开发/打包形态切换目前靠手工，容易出错（尤其 §4 那条 `rm -rf` 的前置确认）。
3. **App bundle 内的资源落点未验证**。目前的 materialize 只保证"仓库根下是实体目录"，
   没有验证"bundle 内的 `Contents/Resources/…` 布局与 `StelFileMgr` 的搜索路径一致"。
   这是**分发形态**的问题，随分发包工作一起做。
