# T23 — 跟踪标志泄漏根治（引擎侧）交付文档

- 日期：2026-09-28
- 提交：见 git log（T23）
- 任务基线：A4 剩余清单里唯一的**真实产品缺陷**，计划文档 §9.4.5 移交表挂账项
- 口径：macOS + Metal（与 T17..T22 刻意一致，见 `tools/t23-verify.sh` 头注）

---

## 1. 任务与通过条件

**缺陷**（上游 vintage `22c8f8ed` 原样带入，T18 定性、挂账至今）：

```
StelObjectMgr::unSelect()            StelMovementMgr::selectedObjectChange()
  lastSelectedObjects.clear()  ──先──▶  if (getWasSelected())        ← 恒 false！
  emit selectedObjectChanged(           { ... setFlagTracking(false); }
    RemoveFromSelection)  ──后──▶           ↑ 永不执行
```

`getWasSelected()` = `!lastSelectedObjects.empty()`（StelObjectMgr.hpp:111）。
unSelect **先清空再发信号**，槽（StelMovementMgr.cpp:757-766）整段包在
`getWasSelected()` 里 ⇒ 取消选中后 `flagTracking` 泄漏为 `true` —— 一个
"无目标的跟踪"谎言状态。`updateVisionVector()` 靠
`flagTracking && getWasSelected()` 的合取侥幸没把它变成行为（StelMovementMgr.cpp:1239）。

T18 的应对是 UI 层掩盖：`AppFacade::isTracking()` 读**合取真值**。
T23 要的是根治：引擎状态干净，UI 不必再打补丁。

**通过条件**：
1. 取消选中后引擎原始标志归零（加严判据 LOC-08b 全绿）；
2. 换选路径零退化（对照腿 LOC-09 绿）；
3. UI 活引擎腿：真实点击「清除选中」→ 跟踪归零 ∧ 文案归位（UI-09/UI-10）;
4. 判别性负控：临时回退修复 ⇒ LOC-08b/UI-08/UI-10 必须红；
5. 全量回归零退化（含**旧宿主 stellarium** S3 —— 本轮动了 src/core/）。

## 2. 修复内容

### 2.1 引擎（唯一一处对上游的行为偏离，注释已标明）

`StelMovementMgr::selectedObjectChange()`：槽签名里的
`StelModule::StelModuleSelectAction action` 参数**本来就带到了槽里却从未被读过**。
修复只加了 RemoveFromSelection 分支：

```cpp
if (action == StelModule::RemoveFromSelection)
{
    setFlagTracking(false);
    return;
}
// 上游原有分支（换选/新选）原样保留
```

其余监听 `selectedObjectChanged` 的模块（SearchDialog / AstroCalcDialog /
SolarSystem / ConstellationMgr / AsterismMgr）不受影响 —— 只动了 MovementMgr 自己的槽。

### 2.2 AppFacade：合取退役 + 信号转发

- **`isTracking()` 简化**：直接读 `getFlagTracking()`。根治后
  `flagTracking==true` 蕴含有选中（引擎 `setFlagTracking(true)` 分支本就要求
  `getWasSelected()`，`StelMovementMgr.cpp:1388`），合取第二因子成死代码。
- **`ensureTrackingForwarding()`（新增，懒连接一次）**：
  `StelMovementMgr::flagTrackingChanged` → `emit trackingChanged()`。
  这是**负控第二轮抓到的第二个缺口**：清除选中后引擎标志确实归零了，
  但 QML 状态文案残留"正在跟踪：Moon" —— 因为 AppFacade 从不连接引擎信号，
  `trackingChanged` 只在自己的命令路径上手动 emit（locateSelected/setTracking），
  引擎侧状态变化（unSelect / 换选 / 旧键位）QML 永远收不到通知。
  连接点：clearSelection / locateSelected / setTracking 三处入口懒连（幂等）。

### 2.3 UI

SearchPage 的「清除选中」按钮补 `objectName: "clearSelectionButton"`
（UI 腿判据的锚点；按钮本身与接线未改）。

## 3. 判据与证据链

### 3.1 三代读数对照（同一判据 LOC-08b，同一序列：进入跟踪 → 清选中）

| 版本 | 清前 合取/引擎原始 | 清后 合取/引擎原始 | 判定 |
|---|---|---|---|
| T18（缺陷在，判据弱） | true / true | false / **true** | OK（只断言合取）|
| **T23 修复** | true / true | **false / false** | OK（加严：两者都断言）|
| **T23 负控**（回退修复） | true / true | **true / true** | FAIL ✓ |

T18 那行的 `引擎原始=true` 就是泄漏的原始存档
（`docs/evidence/2026-09-24-t18-locate-track/locatecheck-mac.txt`）。

### 3.2 判据设计（两条腿 + 对照腿）

- **LOC-08b（加严）**：`entered && trackBefore && rawBefore && !trackAfter && !rawAfter`。
  修复前它**会红**（负控实证 rc=10）—— 这正是"判据别为上游缺陷背书"
  （血泪第 7 条）的落实：T18 只敢断言合取，根治后断言升级为正确性。
- **LOC-09（换选对照腿，新增）**：跟踪 → 换选另一天体（直接走引擎
  ReplaceSelection，绕开 UI 层幂等闸——闸挡的是同 id 重选）→ 引擎原始标志归零。
  **负控下它仍绿**：换选路径在旧代码本来就对。作用是证明 T23 把"取消选中"
  拉齐到"换选"早已正确的行为，而不是顺手改坏后者。
- **UI-09/UI-10（活引擎腿，新增）**：UI-09-prep 重进跟踪（前提腿，前提不成立
  照实判红不进入下一条）；真实点击「清除选中」→ UI-10 断言
  `isTracking=false ∧ trackedName 空`；UI-08（文案档）合并在同一相位
  —— 它曾因新相位提前 finish 被短路，第一跑 UICHECK 9/9 时抓回。

### 3.3 负控读数（回退修复 → 两套必须红）

```
LOCATECHECK 14/15 rc=10：LOC-08b FAIL（合取=true/引擎原始=true）、LOC-09 OK（对照腿）
UICHECK     8/10  rc=10：UI-08 FAIL（文案="正在跟踪：Moon"）、UI-10 FAIL（isTracking=true）
```

注意负控里**合取腿也红了**：`isTracking()` 简化后读原始标志，泄漏直接可见 ——
这同时证明了简化是加严而非放宽（T18 时代合取值 false 会把泄漏藏住）。

### 3.4 正跑读数

- LOCATECHECK **15/15**（13 → 15）rc=0 ×5
- UICHECK **10/10**（8 → 10）rc=0 ×5
- LOC-08b：`清后 合取=false/引擎原始=false`
- LOC-09：`rawBefore=true → 换选「Jupiter」后 rawAfter=false`
- UI-10：`isTracking=false trackedName=""`；UI-08 文案归位为空

### 3.5 过程记录（两条，供读结果的人参考）

1. **变量名乌龙**：两次"挂死"（10 分钟无输出 + `A2: 视口仍无效 0x0`）是
   把 `STELQUICK_UI_CHECK` 敲成 `STELQUICK_LOCATE_UI_CHECK` —— 等于没设模式，
   程序进入默认手动查看模式等人操作。教训：跑套件前 `grep 环境变量名 main.cpp`，
   别凭记忆。
2. **UI-08 短路**：新相位 case 8 提前 finish，把原 default 里的 UI-08 跳过了
   （首跑 9/9 PASS 看似全绿，实丢一条）。修法：UI-08 合并进 case 8。
   教训：改相位结构后核对判据总数（8 → 10，不是 9）。

## 4. 验收

| 项 | 结果 |
|---|---|
| LOCATECHECK | 15/15 rc=0 ×5 |
| UICHECK | 10/10 rc=0 ×5 |
| 负控（回退修复） | 14/15 + 8/10，预期红项全红，rc=10 |
| 回归 | 见 `rc-summary.txt`（timecheck/timeuicheck/searchcheck/returnuicheck/replaycheck/clockcheck/actioncheck/a2/dyn×2）|
| S3 旧宿主（动了 core 的硬要求） | 见 `rc-summary.txt` |
| 临时残留反查 | `TMP-NEGCTRL` 0 处 |

## 5. 改动清单

| 文件 | 改动 |
|---|---|
| `src/core/StelMovementMgr.cpp` | `selectedObjectChange` 处理 RemoveFromSelection（根治）|
| `src/app/AppFacade.cpp` / `.hpp` | `isTracking()` 合取退役；`ensureTrackingForwarding()` 懒连接引擎信号（clearSelection/locateSelected/setTracking 三入口）|
| `src/app/LocateCheck.cpp` / `.hpp` | LOC-08b 加严；新增 LOC-09（步骤 13）；头注同步 |
| `src/ui/main.cpp` | UiLocateCheck 新锚点 `clearSelButton`；新增 case 7（UI-09）/case 8（UI-08+UI-10）；判据数 8 → 10 |
| `src/ui/qml/SearchPage.qml` | 「清除选中」按钮补 objectName |
| `tools/t23-verify.sh` | 新建（正题 = 定位两套；S3 为硬要求回归）|

## 6. 移交 A4

A4 剩余：排序的拼音/类型分组、召回问题（引擎侧，候选截断在排序前）、
QML 交互级测试补键盘滚轮与视觉层、DYN 停摆根因、Windows 侧复验。
跟踪标志泄漏从清单**移除**（已根治）。

## 7. 复现

```zsh
/opt/homebrew/bin/cmake --build build-release --target stelQuickUI -j8
zsh tools/t23-verify.sh all 3
```

读结果纪律：`rc-summary.txt` 全 rc=0 为零退化；DYN 是 `N/3` + 替身对照
（替身也败 = 仪器测不到）；负控文件是证据不是失败。
