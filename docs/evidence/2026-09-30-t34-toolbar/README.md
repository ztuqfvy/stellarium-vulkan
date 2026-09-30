# 证据：T34 真实工具栏 + 显示开关（2026-09-30，macOS + Metal/MoltenVK）

> 一键复跑：`tools/t34-verify.sh core 5`
> 产品文档：`docs/T34_TOOLBAR.zh_CN.md`
> 环境口径与 T17–T33 一致、**刻意不换**：`VK_DRIVER_FILES=MoltenVK_icd.json`、
> `QT_VULKAN_LIB=libvulkan.1.dylib`、`STELQUICK_GRAPHICS_API=metal`。

## 1 汇总（`mac/rc-summary.txt`）

| 段 | 结果 |
|---|---|
| 正题 `TOOLBARCHECK` ×5 | **5/5** `判据 12/12 VERDICT=PASS`，残留进程 0 |
| 负控 A `REV_OFF` | 期望 rc=10 ∧ 9/12 ∧ 红 **[TB-07,TB-09,TB-12]** ⇒ **OK** |
| 负控 B `TOKEN_OFF` | 期望 rc=10 ∧ 10/12 ∧ 红 **[TB-09,TB-12]** ⇒ **OK** |
| 负控 C `CLICK_OFF` | 期望 rc=10 ∧ 11/12 ∧ 红 **[TB-10]** ⇒ **OK** |
| 负控 D `LAYOUT_BREAK` | 期望 rc=10 ∧ 11/12 ∧ 红 **[TB-10]** ∧ `covered=false` ⇒ **OK** |
| 探针 `TOOLBARPROBE` | `VERDICT=DONE`，Q3b 对照 4/4、Q4 发射 4 次 ⇒ **OK** |
| 相邻回归 10 套 | `timecheck`/`returnuicheck`/`searchcheck`/`actioncheck`/`locatecheck`/`locate-uicheck`/`replaycheck`/`clockcheck`/`timeuicheck`/`locationcheck` **全 rc=0** |
| `INTERACTCHECK` | **rc=0（18/18，窗口已激活）** —— 本批**拿到**焦点，非 ENV-SKIP |
| S3 旧宿主 `stellarium` | rc=0（8 条） |
| A2 逐像素 | rc=0 |
| **总** | **FAILED=0** |

## 2 正题逐条读数（`mac/toolbarcheck-mac-run1.txt`）

```
前提：12 个候选动作 present=12/12 checkable=12/12
前提：revisionBase=0 写前值 4 个（4/12 入写入腿）
前提：boot 初值 Lines=true, Grid=false, =false, Mode=false
✓ TB-01..04 写入生效：getter 逐个翻转
✓ TB-06 信号腿：actionToggled 发射 4/4 个 id
✓ TB-07 revision 腿：净增 8 ≥ 8
✓ TB-05 往返复原：4/4 回到写前值
✓ TB-08 负控：trigger(actionShow_Nonexistent_T34) ⇒ executed=false revision Δ=0 toggled Δ=0
✓ TB-11 判别负控：registry(app.togglePause) executed=true revision Δ=0（必须为 0）
✓ TB-09 UI 腿：按钮 12/12 找到、态一致 12/12
  TB-10 落点覆盖：covered=true（未覆盖的祖先：<无>）
  TB-10 落点：center=(48.0,58.0) 按钮 scene(0,0)=(6.0,42.0) 84x32
  TB-10 父链：… Toolbar(x=0 y=0 w=960 h=116) ← ColumnLayout(x=6 y=6 w=948 h=104)
             ← toolbarToggleFlow(x=0 y=36 w=948 h=68) ← toolToggle_actionShow_Constellation_Lines(x=0 y=0 w=84 h=32)
✓ TB-10 UI 点击腿：真实点击星座连线 ⇒ 引擎 true ⇒ false（翻转）
✓ TB-12 绑定重算腿：点击后 engineOn=false vs 引擎=false；复原后 engineOn=true vs 引擎=true
判据 12/12  VERDICT=PASS
```

## 3 关键读数（探针 `mac/probe-tool-data-mac.txt`）

- **Q1**：引擎动作注册表 **505 个动作 / 15 个分组**。
- **Q2**：12 个候选 **12/12 checkable**；`getText()` 返回**中文**
  （`星座连线` / `星座标签` / `星座图绘` / `赤道网格` / `地平网格` / `地面` / `方位基点` /
  `大气层` / `深空天体` / `行星标签` …，T24 翻译链路的红利）；
  快捷键 `C/V/R/E/Z/G/Q/A/D/Alt+P/O/Ctrl+N`。
- **Q3b**：4 个代表开关 trigger 后**逐个对照写前值**（4/4 行）。
  ⚠️ **值的真假逐跑可变**（引擎把开关态落盘，boot 初值有时 `Lines=true` 有时 `false`）
  ⇒ 判据只认"**4 条对照行**"这件结构性事实，**不认**某一具体真假。
- **Q4/Q4b**：`actionToggled` 发射 4 次、`displayTogglesRevision=4` ⇒ Facade 订阅生效。

## 4 🔴 本轮复现并固化的两条"假绿"缺陷形态

| 负控 | 缺陷形态 | 唯一哨兵 | 实测读数 |
|---|---|---|---|
| C `CLICK_OFF` | 按钮 `onClicked` **不派发**（点击"没接上"） | TB-10 | 11/12，红 = [TB-10]；`covered=true`（落点是对的，是派发被摘） |
| D `LAYOUT_BREAK` | Toolbar `implicitHeight=0` ⇒ 布局坏掉但**不裁剪**（按钮照画） | TB-10 + 落点覆盖自证 | 11/12，红 = [TB-10]；`covered=**false**（未覆盖的祖先：QQuickColumnLayout,Toolbar_QMLTYPE_1）` |

⇒ C/D 的**红项集合相同、证据链不同**（一个靠引擎读回真值、一个靠几何覆盖），
两者合起来证明「点击腿」与「落点自证」**各自承重**。

## 5 环境注（不洗成 PASS 的账）

- 密集起停时出现一次 Metal 设备掉线（`VK_ERROR_OUT_OF_DEVICE_MEMORY` / `Device lost` /
  `kIOGPUCommandBufferCallbackErrorPageFault`）⇒ 该跑**零判据输出**。停几秒重跑即恢复。
  **无判据输出 ≠ 判据失败**：脚本按 rc 与判据行双重判定。
- `boot 初值` 逐跑可变的成因未追（引擎把开关态落盘）；不影响判据 —— 全套 TB-* 的对照
  一律取**写前快照**，不依赖出厂值。
