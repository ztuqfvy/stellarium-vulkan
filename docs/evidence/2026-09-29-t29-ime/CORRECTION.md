# 更正说明：本目录的 DYN「真实引擎」跑实际是替身

**发现时间**：2026-09-29（W-T29 Windows 跨平台复验期间）
**性质**：验证脚本缺陷（不是判据缺陷，也不是产品缺陷）
**影响范围**：本目录 `regression-dyn-engine-metal*.txt` 的**标注**，以及
`rc-summary.txt` 里 `regression-dyn-engine-metal 3/3 PASS` 这一行的**含义**。

## 事实

`src/ui/main.cpp:4120-4121` 选生产者的口径是：

```cpp
const QByteArray producerKind = qgetenv("STELQUICK_DYN_PRODUCER");
const bool engineProducer = (producerKind == "engine");   // 只认显式 "engine"
```

即 **不设该变量 = 替身（LiveFrameSource / LegacyTestScene）**。

而 `tools/t29-verify.sh` 的 `run_dyn_engine()` 当时写的是：

```zsh
STELQUICK_DYN_CHECK=1 "$BIN" > "$OUT/regression-dyn-engine-metal-run$i.txt" 2>&1
```

—— **漏了 `STELQUICK_DYN_PRODUCER=engine`**。`t16..t28-verify.sh` 全都有这一项
（例如 `t27-verify.sh:74`、`t28-verify.sh:87`），只有 t29 丢了。

可直接复核的物证：两份日志的第一行判据都打出生产者标签，**都是替身**：

```
$ grep -m1 '生产者=' regression-dyn-engine-metal-run1.txt regression-dyn-stub-metal-run1.txt
regression-dyn-engine-metal-run1.txt: DYNCHECK: 生产者=test(替身场景)
regression-dyn-stub-metal-run1.txt:  DYNCHECK: 生产者=test(替身场景)
```

## 影响

- T29 当轮**"真实引擎 vs 替身"的判别性对照实际上跑了两次替身**。两跑读数确实不同
  （52.7/52.4 vs 52.0/51.6 fps 等），但那是**同一路径的两次独立采样**，不是跨路径对照。
- 因此 T29 的 DYN 结论应降级为：**"替身路径 3/3 PASS ×2 组"**，
  而不是"真实引擎 3/3 + 替身 3/3"。
- 其余判据（CLOCK/ACTION/SEARCH/LOCATE/TIME/RETURN/REPLAY/INTERACT/A2）与本项无关，不受影响。

## 处置

1. **已修** `tools/t29-verify.sh`：`run_dyn_engine` 补 `STELQUICK_DYN_PRODUCER=engine`；
   `run_dyn_stub` 也显式写 `=test`，让标注不再依赖默认值。
2. **本目录的日志与 `rc-summary.txt` 保持原样**（证据 append-only 精神，不回改历史读数），
   仅以本文件作更正记录。
3. **在 W-T29 轮重新采集**：`tools/wt29-verify.sh` 用显式生产者跑 engine/test 两路，
   证据落 `docs/evidence/2026-09-29-w-t29/mac/`；Windows 侧同步（`win-suites.ps1` 原先
   同样漏了该变量，注释里"engine producer is chosen internally"是错的）。
4. 归类：与项目血泪第 8 条同款——**"规则正确"与"规则被调用"是两件事**，
   脚本自己的标签也会为现状背书。凡"判别性对照"，必须**回读被对照对象的身份**
   （这里是 `生产者=` 标签），不能只看两跑读数不同就认定对照成立。
