# T45 收口批次异常：真实个人版 config.ini 被改写丢键（2026-10-01）

> 状态：**已定位到产品侧真缺陷并修正**（健康读用错解析器）；**改写触发方的最后一环仍是未决项**（见 §5）。
> 真实配置**已从备份恢复**（`md5=c847cd85f3b12585e84ac6bb204b0187`，771 键，用户值完整）。

## 1. 现象

T45 收口第一批（15:32–15:37，`tools/a6-verify.sh t45 2`）末尾：

```
config 零污染门：🔴 被污染！pre=b1e4443b3a11f5a5243603f6d19da3bd post=6f6422d58511dbc0a3d96e37acc0c5c7
  ✗ T45 T36 CONFIGCHECK 未退化（rc=10）
CONFIGCHECK: 判据 7/8  VERDICT=FAIL
CONFIGCHECK:   ✗ CFG-05 配置种子完整（原目录有配置）：原目录键数=768 个人版缺失=80
              （首个=DialogPositions/Location）个人版字节=38326
```

被污染的就是 `~/Library/Application Support/Stellarium-quick/config.ini`（T36 隔离出来的个人版真身）。
**这不只是"门红了"**：丢的是用户值（见 §3）。

## 2. 三个时间点的对照

| 时刻 | 文件 | md5 | 字节 | 键数（plain ini 解析） |
|---|---|---|---|---|
| 2026-10-01 10:06（T42 会话留存备份） | `/tmp/t42-config-backup-1790820395.ini` | `c847cd85…` | 42895 | **771** |
| 15:32（本批次**起跑**） | 真实个人版 | `b1e4443b…` | **41990** | 771 |
| 15:37（本批次**跑完**） | 真实个人版 | `6f6422d5…` | **38326** | 690 |

> `b1e44` ≠ `c847`：两者都是 771 键，但字节差 905 ⇒ **文件内容不同**（引擎规范化过的版本 vs T42 快照版）。
> 这一步很关键：**"起跑时的文件"与"我手上的备份"不是同一份**，所以"恢复备份后再跑不丢"**不能**证明"从 b1e44 跑也不丢"。

## 3. 丢了什么（相对 10:06 备份，逐 section 对比）

| section | 丢的键 | 备注 |
|---|---|---|
| `color` | **54** | `*_color` 整族消失；同时 `earth_orbit_color` / `mercury_orbit_color` / `saturn_orbit_color` / `sdo_orbits_color` 等**值被清零**（`0.000000,0.000000,0.000000`） |
| `DialogSizes` | **11** | **整节消失**（AstroCalc/Configuration/DateTime/Help/Location/ObservingList/Oculars/ScriptConsole/Search/Shortcuts/View） |
| `DialogPositions` | **3** | **整节消失**（Location/Oculars/Search） |
| `Satellites` | 5 | `hint_color` / `invisible_satellite_color` / `penumbra_color` / `transit_satellite_color` / `umbra_color` |
| `Exoplanets` | 2 | `exoplanet_marker_color` / `habitable_exoplanet_marker_color` |
| `astro` | 2 | `de430_path` / `de431_path` |
| `init_location` | 1 | `location`；且 `last_location` **由用户值 `1.2929, 103.855`（新加坡）退回 `37.751, -97.822`（默认）** |
| `landscape` | 1 | `label_color` |
| `navigation` | 1 | `init_view_pos` |
| `tui` | 1 | `admin_shutdown_cmd` |
| 合计 | **81** | 771 − 81 = 690 ✓ |

另有 2 个 section 整体消失（`DialogPositions` / `DialogSizes`），新增 section **0**。
丢键族几乎全是 **`*_color` 颜色族 + 老 QWidget 对话框几何 + 外部文件路径**。

## 4. 🔴 抓到真缺陷：同一文件两个解析器分歧 83 键

**同一次运行**（批次末尾那次 `configcheck` 回归，15:37）里，两个读数互相矛盾：

| 读数（同一文件 `b1e44`，同一时刻） | 解析器 | 结果 |
|---|---|---|
| `CONFIGISO: configHealth severity=1 status=0 bytes=41990 keys=771 … text=正常（771 键）repaired=0` | 我的健康读 —— **plain `QSettings::IniFormat`** | **771 键 ⇒ 判"正常"** |
| `CFG-05 … 原目录键数=768 个人版缺失=80` ⇒ **688 键** | 引擎的 **`StelIniFormat`**（`StelIniParser.hpp`） | **688 键 ⇒ 判"缺 80"** |

⇒ **plain IniFormat 与 `StelIniFormat` 对同一份 config.ini 分歧 83 键。**
健康读的职责是"替引擎预判它能不能起来"，判定基准必须是**引擎同一个解析器**。

**修法（已落地）**：`src/app/ConfigIsolation.cpp` 的配置健康读 + 随包默认基线读，
一律由 `QSettings::IniFormat` 改为 `StelIniFormat`（`#include "StelIniParser.hpp"`）。

**为什么这是"真缺陷"而不是洁癖**：
1. 轻则误报健康（本例：健康读说正常、引擎实际缺键）；
2. **重则误触发修复** —— 若分歧方向反过来（plain 解析 < 252 而引擎解析正常），
   引导修复腿会**把用户好端端的配置改名成 `.corrupt` 并重建为随包默认** ⇒ 用户设置被"修掉"。
   数据没丢（备份在），但用户态被抹平。

## 5. 未决项：谁把 `b1e44` 写成了"两解析器分歧"的形态

已排除的（都做了对照实验，见 §6）：

| 假设 | 实验 | 结论 |
|---|---|---|
| "普通启动+退出"会丢键 | 临时 userdir + 771 键配置 + 完整 CONFIGCHECK | ❌ 不丢（42683 B，8/8 PASS） |
| "CFG-06/07 哨兵写 + immediateSave"会丢键 | 同上（该套件本身就跑这两条） | ❌ 不丢 |
| "从健康配置跑必丢" | 真实目录 + 恢复 `c847` + CONFIGCHECK | ❌ 不丢，md5 `c847→c847` 不变 |
| "会持续丢" | `6f64` 状态下连跑两次 | ❌ 不再变（已成为稳定不动点） |
| "本批次的形态矩阵/负控写的" | 逐 run 查 `CONFIGISO: personal=` 落点 | ❌ 16 个 run **全在 `/tmp` 临时目录**；批次里唯一碰真实目录的是末尾的 `configcheck` 回归 |

**剩余嫌疑（未证）**：`b1e44` 是"引擎规范化"过的文件，其内容让 `StelIniFormat` 的读取器
漏解析 83 个键（而 plain ini 能读全）；`StelIniFormat` 在某次 `sync()` 里把它们**写丢**，
之后就稳定了（自愈）。这类"先漏解析、后写丢"的形态最可能由**跨格式改写**造成 ——
即**有人用非 `StelIniFormat` 的 `QSettings` 写过这同一个文件**。
历史线索：T42 探针**真的打开过老 QWidget 对话框**（`F1/F10/⌥B` 各一次），
而那些对话框走的是老 GUI 的 `QSettings` 路径（`src/gui/StelDialog.cpp` 一族）。
⇒ **待办**：对 `StelIniParser.cpp` 的读取器做"分歧键型"专项探针（拿 `6f64` 的父态不可得，
可造同型样本），并把"产品侧任何写 config.ini 的代码都必须用 `StelIniFormat`"升为纪律。

## 6. 对照实验原始读数

| 实验 | 命令要点 | 读数 |
|---|---|---|
| A 临时目录 771 键 | `STEL_USERDIR=$T/Stellarium` + `STELQUICK_CONFIG_CHECK=1`，个人版预置 T42 备份 | `rc=0`、`判据 8/8 VERDICT=PASS`、跑后 42683 B、**丢 0 键** |
| B 真实目录恢复后跑 | 恢复 `c847` → `STELQUICK_CONFIG_CHECK=1` | `rc=0`、`8/8 PASS`、`CFG-05 个人版缺失=0`、md5 `c847→c847` |
| C 稳定态复跑 | `6f64` 状态下再跑一次 | md5 `6f64→6f64`（**不再丢**） |

## 7. 恢复处置

```bash
cp "/Users/ztuqfvy/Library/Application Support/Stellarium-quick/config.ini" /tmp/t45-preserve-690keys.ini
cp /tmp/t42-config-backup-1790820395.ini \
   "/Users/ztuqfvy/Library/Application Support/Stellarium-quick/config.ini"
md5 -q "…/Stellarium-quick/config.ini"   # c847cd85f3b12585e84ac6bb204b0187（771 键，用户值完整）
```

- 恢复后已复跑 `tools/a6-verify.sh t45 2`：**`SCRIPT-RC=0 FAILED=0 PASS=3`**，
  形态矩阵 12/12、负控 4 组红项与台账逐位一致、资源链接 PASS、
  `CONFIGCHECK 未退化 rc=0`、**config 零污染门 ✅ 前后一致（c847）**。
- 690 键版本留档 `/tmp/t45-preserve-690keys.ini`（如需复现分歧读数）。
- 原始目录（`~/Library/Application Support/Stellarium`）**全程未动**（CFG-06/07 双双为证）。

## 8. 落档

- TRAPS：新增「**同一文件双解析器分歧**」与「**干跑脚本必须与真回归脚本对齐环境变量**」两条（见 `TRAPS.md`）。
- 纪律升级：**产品侧任何读写 `config.ini` 的代码一律用 `StelIniFormat`**；
  判据侧独立解析同一份文件时也必须同格式（否则"判据自建副本 ≠ 被测接线实例"的变体，陷阱 86 同族）。
