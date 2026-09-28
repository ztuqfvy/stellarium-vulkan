# `layout-gate` —— 布局就绪门：正跑 + 负控（**成对证据**）

背景与定性过程见 `../flaky-layout-207x0/README.md`。
本目录只放**门本身**的两条腿证据 —— 缺任何一条都有假绿空间：
只有正跑（门从不触发）证明不了"门是活的"；只有负控证明不了"门不改正常路径"。

## 门是什么

`src/ui/main.cpp`：

```cpp
constexpr int kUiLayoutWaitTries = 25;          // 25 × 100ms = 2.5s 上限
bool uiLayoutReady(QQuickItem *item)            // 有正尺寸且可见
{ return item && item->width() > 1.0 && item->height() > 1.0 && item->isVisible(); }
```

接在两处"切页后立刻点新页面里的控件"的相位上：

- `REPLAYCHECK` **case 2 开头**（点在 `searchResultList` 第 0 行之前）；
- `RETURNUICHECK` **case 5 开头**（点时间页 `timeAddHourButton` 之前；
  若切页没生效会**有界**补点一次 `navTimeButton`）。

**位置很关键**：门用"停在本相位重试"实现（`uiReplayAdvance` + `return`，不 `++phase`），
所以必须放在本相位**所有 `uiReplayMark` 之前** —— 否则每次重试都会把前面的判据重跑一遍，
计数虚高。负控第一次就是这么暴露的：`判据 80/81`（上移到相位开头后变成正常的 `判据 2/3`）。

## 腿一：正跑（健康环境，8 + 8 次）

| 文件 | 读数 |
|---|---|
| `positive-N8-replay-summary.txt` | `replaycheck` **8/8 PASS**，每次 `门触发=0` |
| `positive-N8-return-summary.txt` | `returnuicheck` **8/8 PASS**，每次 `门触发=0` |

`门触发=0` ⇒ 门在正常路径上**一次都没等**，不改变任何既有行为。

## 腿二：负控（临时把 `uiLayoutReady` 强制恒 `false`）

| 文件 | 读数 |
|---|---|
| `negctrl-forced-notready-replay.txt` | **rc=10**、`判据 2/3`，报文明说"仪器没接上……也**不作 PASS**" |
| `negctrl-forced-notready-return.txt` | **rc=10**、`判据 7/8`，同上 |

⇒ 门的**失败路径是活的**，不会把"就绪不了"静默放行成 PASS。

> 负控是**临时**改的，跑完立即还原（`uiLayoutReady` 已恢复为真实判定）。
