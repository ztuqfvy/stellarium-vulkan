# T24 证据 — 搜索排序的拼音检索（2026-09-28）

任务：中文界面下 `yueqiu` / `yq` 能找到"月球"。交付文档 `docs/T24_PINYIN_SEARCH.zh_CN.md`。

## 判据总览

| 套件 | 结果 |
|---|---|
| `searchcheck-mac`（正题） | **51/51 PASS** rc=0（34 → 51，新增 PINY-01..08；×5 见 `positive-n5/`） |
| `negctrl/`（负控：collect 拼音分支 `false &&` 关掉） | **48/51 rc=10**：PINY-06/07/08 红、其余全绿 ⇒ 失败路径是活的 |
| 回归 9 项 | timecheck 21/21、timeuicheck 19/19、returnuicheck 11/11、replaycheck 11/11、locatecheck 15/15、locate-uicheck 10/10、clockcheck、actioncheck、a2、S3 旧宿主 —— 全 rc=0 |
| DYN | 引擎 **3/3** + 替身 **3/3**（本轮环境下双绿；间歇定性不变） |

权威读数 = `rc-summary.txt`。

## 关键判据读数（正跑首跑）

- `PINY-02 读音转换「月球」→ 全拼 yueqiu / 首字母 yq`；「猎户座大星云」→ liehuzuodaxingyun / lhzdxy；「Moon」→ 空串
- `PINY-03 分档「月球」×「yueqiu」→ 拼音全拼`（含前缀 yue、中间片段 qiu）；「yq」→ 拼音首字母；「moon」/「月球」→ 不匹配
- `PINY-06 活引擎全拼：搜「yueqiu」首行 = 月球(Planet:Moon)（1 行，匹配质量 拼音全拼）`
- `PINY-07 拼音候选计数 > 0（实得 1）—— 证明 PINY-06 的首行确实来自拼音分支而非字面命中`（成对判据）
- `PINY-08 活引擎首字母：搜「yq」首行 = Planet:Moon`
- `SRC-05b（口径校正后）raw=21 + pinyin=1 ≥ 3`

## 两个必须留档的发现

1. **合流形态从 T10 起就没加载过任何翻译**（本轮才暴露）：引擎 `StelFileMgr::getLocaleDir()` 的三个候选路径在 bundle 布局下全不命中 `build-release/translations` ⇒ 启动日志一直有 `Couldn't load translations`，天体名退回英文。此前没有判据依赖翻译名所以未暴露。处置：POST_BUILD 拷主域 `zh_CN.qm` 进 bundle（`src/ui/CMakeLists.txt`）。证据：`history-diag-translations-missing.txt`（修复前 DIAG 全 cjk=0）。
2. **SRC-05b/05c 口径必须随拼音候选校正**（raw → raw+pinyin）：拼音候选是截断前候选但不计入 raw，旧口径在新能力生效后报假红——"判据别为旧现状背书"。

## 拼音覆盖边界（如实记录）

覆盖 = 翻译在**主域 stellarium** 生效的中文名（行星全部 ✓、有中文名的深空天体）。星座名在 stellarium-sky 域（未拷贝，不覆盖）；DIAG 样本里的深空名（Winnecke 4 / Nova ×××）本身在 po 里就无中文翻译，属数据事实非缺陷。多音字按主读音（"长"→zhang），全组合展开不做。

## 文件清单

- `searchcheck-mac.txt` — 正题全跑（51 判据）
- `negctrl/searchcheck-pinyin-branch-disabled.txt` — 负控（PINY-06/07/08 红）
- `positive-n5/` — 正跑 ×5（51/51 ×5）
- `history-diag-translations-missing.txt` — 翻译缺失时的诊断证据
- `rc-summary.txt` + `regression-*` — 全量回归
