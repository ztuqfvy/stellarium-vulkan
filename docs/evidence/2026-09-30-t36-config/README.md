# 证据：T36 个人版配置目录隔离 + 首次播种

> 交付文档：`docs/T36_CONFIG_ISOLATION.zh_CN.md`
> 环境口径与 T17–T35 一致、**刻意不换**：macOS + Metal（`VK_DRIVER_FILES=MoltenVK`、
> `QT_VULKAN_LIB`、`STELQUICK_GRAPHICS_API=metal`）。
> 产物：`stelQuickUI` **40096328 B / md5=dd1b0eb63669adc0a0ee7b623f9b23d8**。

---

## 目录

### `probe/` —— 探针：坐实"写穿原版用户目录"

| 文件 | 内容 |
|---|---|
| `pre.manifest` / `post.manifest` | 原目录**逐文件** `relpath\|size\|mtime\|md5` 清单，跑前 / 跑后（各 60 行） |
| `config.diff` | 两个 `config.ini` 的**内容 diff** —— **0 行** |
| `run1.out` | 那一次合流形态运行的原始日志（14062 B） |
| `snap.zsh` | 取清单的脚本（可复现） |
| `README.md` | 问题/方法/结果/根因表/文件清单 |

**结论**：`diff pre.manifest post.manifest` **恰好 3 处变化** ——
`config.ini` mtime（**md5 不变**）、`log.txt` `10308 B/579a81…` → `10387 B/611bcc…`、
`modules/Oculars/ocular.ini` mtime。

⚠️ **`config.diff` 是 0 行**：**"内容没变"≠"文件没被碰"**。
只看内容的判据会得出"没写穿"的**错误结论**。

### `mac/` —— 定稿轮（`zsh tools/t36-verify.sh all 3`）

| 文件 | 内容 |
|---|---|
| `configcheck-first.txt` | **S1 首次**（清空个人版目录）—— `8/8 PASS` |
| `configcheck-second.txt` | **S2 非首次**（预埋用户哨兵 `keep_me=12345`）—— `8/8 PASS` |
| `configcheck-empty-machine.txt` | **全新机器**（`STEL_USERDIR=/tmp/t36-empty`）—— `8/8 PASS`，`rc=0`（**此前 139**） |
| `negctl-A-isolate-off.txt` | 负控 A（关隔离）—— 红项 `[CFG-01,CFG-02,CFG-03,CFG-04,CFG-06,CFG-07]` |
| `negctl-B-migrate-off.txt` | 负控 B（关播种）—— 红项 `[CFG-05,CFG-08]` |
| `orig-manifest-before-s1/after-s1/before-s2/after-s2/final/after-s3.txt` | 原目录逐文件清单（6 个时点） |
| `orig-manifest-diff-s1/s2/final/s3.txt` | 三处核对：S1 **0 行**、S2 **0 行**、**全程 0 行**；S3 **16 行**（留证，不判） |
| `fileset-orig/personal/missing.txt` | 播种清单**独立复算**（脚本自己 `find`）：57 个非瞬态文件，缺 **0** |
| `keys-orig/keys-personal-s1/keys-missing-s1.txt` | 配置键集**独立解析**（脚本自己按行解析，**不用 QSettings**）：768 键，缺 **0** |
| `s2-sentinel-before/after.txt` | S2 哨兵（用户改过的值）在跑前/跑后都在 |
| `regression-*.txt` | 12 套件 + `INTERACTCHECK` + S3 + A2 + DYN 双路 ×3 |
| `rc-summary.txt` | 脚本层总账 |

---

## 一句话结论

**三个正题场景全部 `8/8`；负控两组红项两两不同且逐位符合预期；
合流形态全程原目录逐文件（size+mtime+md5）零差异；脚本层判据 23 通过 / 0 失败。**

⚠️ 两处**判据口径**必须连着读：

1. **S3 的 16 行差异是"预期"**：旧宿主 `stellarium`（QWidget）**不走** `LiveSkyRuntime` 的
   隔离引导 —— 它就是"原版程序"的等价物，写原版用户目录是**它应有的行为**。
   A-1.0 要求隔离的是**个人版 UI** ⇒ S3 被刻意挪到"零改动"核对**之后**单跑，只验 `rc`。
2. **CFG-06/CFG-07 的"原目录侧"在空机器上原本是退化的真**（原目录连 `config.ini` 都没有
   ⇒ 空串不含哨兵 / 两个 `<unreadable>` 相等）⇒ 已补**第三半**
   "原本不存在 ⇒ 跑完仍不许出现"（见 `configcheck-empty-machine.txt` 里
   `原本无配置时也未被创建=1` 与 `前提：原目录 config.ini 原本存在=0`）。
