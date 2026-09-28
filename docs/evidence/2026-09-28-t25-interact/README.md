# 证据目录 — T25 交互级（键盘/滚轮）UI 层端到端

- 环境：macOS + Metal（`VK_DRIVER_FILES=MoltenVK`、`QT_VULKAN_LIB`、`STELQUICK_GRAPHICS_API=metal`），与 t17..t24-verify.sh 同口径，**刻意不换**。
- 复跑：`tools/t25-verify.sh`（core / regress / all / dyn）。

## 文件

- `interactcheck-mac-run{1..5}.txt` + `interactcheck-mac-n5.txt`：正题 INTERACTCHECK 6/6 ×5，全 PASS（rc=0）。
  - 首跑（修复前）存档于 `negctrl/first-run-guard-defect-IT06-red.log`：**5/6，IT-06 红**——
    `canDispatchToSky()` className 精确匹配对 QQuickTextField 失效（真缺陷实抓）。
- `negctrl/negctrl1-wheelhandler-disconnected.log`：QML `onWheel` 临时断开 ⇒ IT-02/03 红（60°→60°不动）、rc=10。判据不是摆设①。
- `negctrl/negctrl2-guard-disabled.log`：守卫临时整体失效 ⇒ IT-06 红（L 穿透触发引擎）、IT-05 仍绿。判据不是摆设②。
- `regression-*.txt`：10 项全 rc=0（actioncheck / locatecheck / locate-uicheck / timecheck / timeuicheck / returnuicheck / replaycheck / searchcheck / clockcheck / a2-metal）。
- `regression-dyn-engine-metal*.txt`：引擎 DYN 3/3（环境敏感量，照惯例 N 次 + 替身对照）。
- `regression-dyn-stub-metal*.txt`：替身 DYN 3/3（替身路径不含 T25 代码）。
- `regression-s3-stela3.txt`：**S3 旧宿主 rc=0**（本轮动了 `src/core/StelApp.hpp` 的 friend 声明 ⇒ 硬要求）。
- `rc-summary.txt`：一行一结论。

## 判据对照（三代读数）

| 判据 | 首跑（守卫缺陷在） | 负控1（滚轮断开） | 负控2（守卫失效） | 修复后 ×5 |
| --- | --- | --- | --- | --- |
| IT-01 锚点 | 绿 | 绿 | 绿 | 绿 |
| IT-02 滚轮向前 FOV 变小 | 绿（60→11.33） | **红**（60→60） | 绿 | 绿 |
| IT-03 方向对照变大 | 绿 | **红** | 绿 | 绿 |
| IT-04 时间页负控 | 绿 | 绿 | 绿 | 绿 |
| IT-05 L 键活链 | 绿 | 绿 | 绿 | 绿 |
| IT-06 焦点守卫 | **红**（L 穿透） | 绿 | **红** | 绿 |
