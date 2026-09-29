T32 三轮代码级负控（单点判别）

N1 去掉第一段（有文本也直接返回天空）
INTERACTCHECK: ✗ IT-17 两段式 Esc 第一段：text "mars" → "mars"（应被清空）、页 2 → 1（应不动，仍=搜索页 2）、dispatched 1（应=基线 1）—— 焦点在输入框且有文本时，Esc 只清空、不跳页
INTERACTCHECK: 判据 17/18
INTERACTCHECK: VERDICT=FAIL

N2 去掉第二段（空文本不返回）
INTERACTCHECK: ✗ IT-18 两段式 Esc 第二段：注入前 text=""（应空）、页=2（应=搜索页 2）、守卫判定不可派发=true（判别腿）→ 注入后 currentIndex=2（应=1 天空页）—— 空框再按一次 Esc 才离开搜索，**且走的是守卫路径**（不是 canDispatchToSky 回真的回退路径）
INTERACTCHECK: 判据 17/18
INTERACTCHECK: VERDICT=FAIL

N3 去掉组合态例外（IME 组合中 Esc 也走两段式）
INTERACTCHECK: ✗ IT-14 组合态注入 Esc：页 2 → 1（应仍=2）、rate 1 → 1（应不变）、dispatched 1（应=基线 1）—— 焦点在输入控件时 Esc 必须走守卫，不得跳页
INTERACTCHECK: 判据 17/18
INTERACTCHECK: VERDICT=FAIL

还原后正题复核
INTERACTCHECK: 判据 18/18
INTERACTCHECK: VERDICT=PASS
