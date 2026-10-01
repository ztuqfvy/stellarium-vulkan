# T46 (A6-D) 接口冻结取证 — macOS

**任务**：接口冻结 + 渲染诊断通道核对 + 计划二交接契约 7 项逐项核对。
**产出文档**：`docs/A6_HANDOFF_FREEZE.zh_CN.md`（含「冻结口径」）、`docs/A6_INTERFACE_FREEZE.zh_CN.md`（刷新）。
**冻结点**：`HEAD = ad28257`。

## 文件

| 文件 | 内容 |
|---|---|
| `interface-freeze-regen.txt` | 本轮生成的公开面清单（人读留档；与 `docs/A6_INTERFACE_FREEZE.zh_CN.md` 同源） |
| `interface-freeze-check.txt` | `tools/a6-interface-freeze.sh --check` 的输出（`IF-CHECK: OK`） |
| `t46-rc-summary.txt` | `tools/a6-verify.sh t46` 的 4 条判据结果（`SCRIPT-RC=0 FAILED=0 PASS=4`） |
| `negctl-round1.txt` / `negctl-round2.txt` | 负控 A–D 两轮实测（**两轮红项逐位一致**） |

## 复跑

```zsh
tools/a6-verify.sh t46          # 4 条判据；期望 SCRIPT-RC=0 FAILED=0 PASS=4
tools/a6-interface-freeze.sh --check   # OK / DRIFT + rc
```

## 关键读数（正题）

```
✓ T46 接口冻结清单可生成
    IF-CHECK: OK（当前源码公开面与冻结清单逐字一致）
✓ T46 接口冻结校验（源码公开面 == 已冻结清单）
· FORBIDDEN app_hpp_hits=0 qml_hits=0（须两处均 0）
✓ T46 硬性禁区：app 头文件与 QML 面零 GL/Vulkan 泄漏
· 页面协议入口：setContextProperty 命中 7/7；SkyViewport 注册 1/1
✓ T46 页面协议入口未变（7 属性 + SkyViewport）
config 零污染门：✅ 前后一致（c847cd85f3b12585e84ac6bb204b0187）
```

## 负控（两轮逐位一致）

| 负控 | 改什么 | 实测红项 |
|---|---|---|
| A | `src/app/ViewportState.hpp` 加一个字段 | 冻结校验 |
| B | QML 里加一行**真代码**的 `VulkanInstance` | 硬性禁区 |
| C | `main.cpp` 把 `setContextProperty("ErrorModel")` 改名 | 冻结校验 + 页面协议入口（同一事实两条独立判据） |
| D | 新增一个 `src/ui/qml/*.qml` 文件 | 冻结校验（证它**独立承重**） |

每轮起跑/收尾三文件 md5 一致（源码零残留），末轮干净收尾 `rc=0 PASS=4`。

## 本轮抓到的三个真缺陷

1. **冻结清单此前不可信**：`tools/a6-interface-freeze.sh` 只取**第一个** `^class`、
   不认 `struct` ⇒ 5 处**假类名**（`AppFacade.hpp`→`ISimPacing`、`BackendInfo.hpp`→`FrameMailbox`、
   `SkyViewport.hpp`→`QSGImageNode`、`FrameCompare.hpp`→`QQuickWindow` 等）
   + 2 处**整段漏项**（`ViewportState` / `ConfigIsolationReport` 两个纯 struct 头）。
   逐条对照见 `docs/A6_HANDOFF_FREEZE.zh_CN.md` §契约 1。
2. **`t46` 段原「硬性禁区」判据从未跑过且必假红**：按**文件**计数 + **不剥注释**的正则
   命中了 10 个含"禁止暴露 GL/Vulkan 句柄"**说明注释**的文件。改为取 §5 的机器可读行
   （`FORBIDDEN …`）——**单一口径**。
3. **`config` 零污染门的量具自证失效（陷阱 110）**：`md5` 不在 PATH 时 `pre`/`post`
   都取到 `"(missing)"`，旧逻辑 `pre == post` 成立 ⇒ 照样打「✅ 前后一致」——
   在**否定性结论**上静默失效。已改为：两侧都必须是 32 位 hex，否则报
   **UNAVAILABLE + rc=1**（不洗绿、也不当红）。两态实测：正常 ⇒ ✅/rc=0；
   `env -i PATH=/usr/bin:/bin` ⇒ UNAVAILABLE/rc=1。

