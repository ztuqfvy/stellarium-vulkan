# W-T35 Windows 跨平台复验（T34 工具栏 + T35 时间链路，并批）

> 提交：仪器 `d68cc32`（T35）+ 本轮 W 支线件 ｜ 证据：`docs/evidence/2026-09-30-t35-timelink/windows/`
> （仪器事故另档：`docs/evidence/2026-09-30-t35-timelink/windows-instrument/`；
> 送源对账：`docs/evidence/2026-09-30-t35-timelink/windows-src-sync/`）
>
> 一句话：**Windows 侧全量自检套件全绿（`launcher exit=0`、零 FATAL）**；
> T34 与 T35 的**全部红项集合与 mac 逐位一致**；
> 首轮 13 条期望校验失败**不是产品缺陷**，是**仪器被 PowerShell 变量名大小写不敏感这一条打死**，
> 修法从"实例级"升到"类级"，并用**三方闭环**证明真的修好了。

---

## 1 结论先行

| 项目 | 结果 |
|---|---|
| Windows 侧全量自检套件 | **全绿**：`launcher exit=0`、`[done]` 后**零 FATAL 行** |
| T34 `TOOLBARCHECK` 正题 | **3/3 `12/12 VERDICT=PASS`**，`red=[]`、`TB-09 自证`、`covered=true`（`bad=0 of 3`） |
| T34 四组负控红项 | A `[TB-07,TB-09,TB-12]` / B `[TB-09,TB-12]` / C `[TB-10]` / D `[TB-10]`+`coveredFalse` ⇒ **与 mac 逐位一致** |
| T35 `TIMELINKCHECK` 正题 | **3/3 `7/7 VERDICT=PASS`**，`red=[]`、占位符漏 0、`restore=1`、`gate=0`、脚本独立复算 TL-01/TL-07 `True`（`bad=0 of 3`） |
| T35 三组负控红项 | A `[TL-01]` / B `[TL-01,TL-02,TL-05]` / C `[TL-04]` ⇒ **与 mac 逐位一致** |
| 相邻回归 | **11 套件全 rc=0**；`S3 旧宿主 stellarium` rc=0；`A2 逐像素` rc=0 |
| 探针 | `TOOLBARPROBE` OK（Q1=1、Q2=12）；`TIMELINKPROBE` OK（漏 0、`missingAnchors=[]`） |
| `INTERACTCHECK` | **3× rc=0 18/18**，`armed=1`、`degraded=False` |
| `INTERACT` 失活探针（仪器回归断言） | 红项 `[IT-05,IT-06,IT-13,IT-16,IT-17,IT-18]` —— **与 W-T31/W-T32 实测的 Windows 边界逐位一致** |
| DYN 双路 | `engine` ×3 与 `test`（替身） ×3 全 rc=0，`producer-readback` 逐次回读 OK |
| 就绪门 | `window-gate hits=0`（13 个窗口全可信） |
| 首轮 13 条失败的性质 | **纯仪器缺陷**（见 §2）；产品侧在同一轮里就已全绿 |
| 送源同源 | **94 项比对 / 90 同源 / 0 内容不同 / 4 缺失**（4 个全是 mac 专用 `.sh`） |

---

## 2 这一轮主线：**先修仪器，再谈产品**

### 2.1 首轮的现象（`12:27:18 → 12:41:06`，`launcher exit=97`）

```
  TOOLBAR run1: rc=0 judge=12/12|PASS red=[] tb09found=1(>=1) covered=1(>=1) -> pos-pass
  TOOLBAR run2: rc=0 judge=|PASS      red=[] tb09found=1(>=1) covered=1(>=1) -> BAD
  TOOLBAR run3: rc=0 judge=/|PASS     red=[] tb09found=1(>=1) covered=1(>=1) -> BAD
  TIMELINK summary: pos-pass=0 env-skip=0 bad=3 of 3
FATAL TIMELINKCHECK never produced a full 7/7 run -- positive case not exercised
FATAL 13 expectation check(s) failed
```

**这一屏看起来完全像产品退步。** 但它不是——先别改产品。

### 2.2 鉴别特征：**坏的是哪一类字段**

| 字段 | run1 | run2 | run3 | 判定 |
|---|---|---|---|---|
| `judge=`（计数+裁决） | `12/12\|PASS` | `\|PASS` | `/\|PASS` | **坏** |
| `rc=` | 0 | 0 | 0 | 正常 |
| `red=[]`（红项集合） | `[]` | `[]` | `[]` | 正常 |
| `tb09found` / `covered` | 1 / 1 | 1 / 1 | 1 / 1 | 正常 |
| `recomputedTL01` / `recomputedTL07` | True | True | True | 正常 |
| 7 条负控的红项集合 | 精确命中 | 精确命中 | 精确命中 | 正常 |
| 两个探针 | OK | OK | OK | 正常 |
| `INTERACTCHECK 18/18` | pass | pass | pass | 正常 |

**两条鉴别特征**（同类事故都可先按这个筛）：

1. **只有"靠运行时变量拼正则"的那**一个**字段坏**，硬编码锚点的判据全对；
2. **从第 2 次调用起才坏**，第一次永远对。

### 2.3 诊断：给仪器加诊断，**在目标机的目标 PowerShell 上**

按纪律（读数为负先给仪器加诊断），三步：

1. **把真实字节拉回来**：`.out.txt` 里的判据行是**合法 UTF-8**（计数词 = `E5 88 A4 E6 8D AE`）、
   `12/12`、`VERDICT=PASS`、CRLF 都在 ⇒ **产品日志从来没写错**，问题在下游。
2. 送一个一次性 `judge-diag.ps1`，**用 Windows PowerShell 5.1 跑**（不是本机 pwsh 7），
   打印"解析器实际看到的量"：4 种构造的 `type/len/codepoints` + 7 种正则各命中几次。
   结果：**4 种构造全对**（`type=String len=2 cps=[21028,25454]`），**7 种正则全命中**。
3. ⇒ 字面、正则、日志三个嫌疑全部排除 ⇒ **只剩"这个变量在第 2 次调用时已经不是原来那个值"**。

### 2.4 根因：PowerShell 变量名**大小写不敏感** —— 而且这是**第二次**

- 脚本里有一个**从 code point 拼出来**的 token：
  `$JUDGE = [char]0x5224 + [char]0x636E`（"判据"二字），用来**拼判据行正则**；
- 调用点写 `$judge = Get-Judge …` —— **同一个变量**；
- 第 1 次调用正常返回 `12/12|PASS`，**赋值把 token 变成了结果**；
- 第 2 次调用正则退化成
  `^TOOLBARCHECK:\s*12/12|PASS\s*([0-9]+)/([0-9]+)…`
  —— 一个**右分支无锚定的交替式** ⇒ 匹配到垃圾、两个捕获组为 `$null`
  ⇒ `"$jm|$vd"` 渲染成 `|PASS`，再往后甚至 `/|PASS`。

> 🔴 **真正的教训是"修法的粒度"**：这个坑 W-T31 已经踩过（`$ok` 覆盖 `$OK`），
> 当时的修法是把**符号**前缀改成 `$MARK_OK`/`$MARK_BAD` —— **实例级**。
> 同一个文件里另一个**非 ASCII token** 没被推广到，于是同一个坑换了个门又进来了。
> 见 §6.1。

### 2.5 修法：两条腿，故意独立

1. **让 token 消失** —— `Get-Judge` 不再认识那个计数词，用通用跳过：
   ```powershell
   $reJ = "^" + $TagName + ":\s+[^\s]+\s+([0-9]+)/([0-9]+)(\s+VERDICT=([A-Z]+))?"
   ```
   现在匹配器里**零非 ASCII**，任何变量名都撞不上；顺带对"计数词换措辞/分隔宽度变化"免疫。
2. **调用点结果变量改名** `$judgeTxt`（4 处），即便将来重新引入 token 也不会撞车。

### 2.6 三方闭环 —— 宣布"修好了"的最低证据

| # | 断言 | 证据 |
|---|---|---|
| a | **旧仪器在新平台上照样坏**（⇒ 是**语义**问题，不是某个 PS 版本的怪癖） | `negctl-judge.ps1` 逐字重建故障版，在 **macOS / pwsh 7.6.6** 上复现出**逐字相同**的 `12/12\|PASS → \|PASS → /\|PASS` |
| b | **新仪器对全部真实日志全绿** | `test.ps1` **97/97 PASS**（正题 5×2 套 + 7 负控 + 两探针 + 邻套件 + 负向输入） |
| c | **新断言承重** | (a) 同时证明 `REPEATED CALLS` 段（连调三次同值 / 中间插别的 tag / 毒化大小写变体）在旧代码上**会红** —— 没有 (a)，(b) 的绿可能是装饰 |

⚠️ 这一步本身也修掉了一个**未造成真机损失**但同源的缺陷：探针门硬编码了单空格，
而实测 `TOOLBARPROBE:` 后是 **1 个空格**、`TIMELINKPROBE:` 后是 **3 个** ⇒ 一律改 `\s+`。
（见 §6.6）

---

## 3 定稿轮读数（Windows，`12:45:26 → 12:59:10`，13m44s）

### 3.1 仪器自证（缺一条，下面的绿色都不算数）

```
script md5 = d53c59586f51ee22bfdb1b597441baa0        （与 mac 三向逐位一致：local = C:\temp = 仓库）
repo HEAD = cf738bb                                  （⚠️ 会撒谎，见 §5）
exe = E:\Qt_demo\stellarium-vulkan\build-win\src\ui\Release\stelQuickUI.exe
exe size = 29282816 B   mtime = 2026-09-30T12:26:43
exe md5  = 7687513E293BAA23298231B4E7B46348          （= 构建轮产物，本批未重编）
11 项 SRC <file> md5 全 MATCH
```

### 3.2 T34 正题 + 四组负控（与 mac **逐位一致**）

```
  TOOLBAR run1/2/3: rc=0 judge=12/12|PASS red=[] tb09found=1(>=1) covered=1(>=1) -> pos-pass
  TOOLBAR summary: pos-pass=3 env-skip=0 bad=0 of 3

  TOOLBAR-NEG-A run1: rc=10 judge=9/12|FAIL  red=[TB-07,TB-09,TB-12] expect=[...] -> OK
  TOOLBAR-NEG-B run1: rc=10 judge=10/12|FAIL red=[TB-09,TB-12]       expect=[...] -> OK
  TOOLBAR-NEG-C run1: rc=10 judge=11/12|FAIL red=[TB-10]             expect=[...] -> OK
  TOOLBAR-NEG-D run1: rc=10 judge=11/12|FAIL red=[TB-10] expect=[TB-10] coveredFalse=1(>=1) coveredTrue=0(0) -> OK
```

### 3.3 T35 正题 + 三组负控（与 mac **逐位一致**）

```
  TIMELINK run1/2/3: rc=0 judge=7/7|PASS red=[] leak=0(0) restore=1(>=1) gate=0
                    recomputedTL01=True recomputedTL07=True -> pos-pass
  TIMELINK summary: pos-pass=3 env-skip=0 bad=0 of 3 ; window-gate hits=0 (reading only)

    recheck TL-01: run1 W=2.9973 dJD=0.6    rate=0.2 pred=0.59946 ratio=1.0009 rel_dev=0.09% OK
                   run2 W=3.0006 dJD=0.6    rate=0.2 pred=0.60012 ratio=0.9998 rel_dev=0.02% OK
                   run3 W=2.9992 dJD=0.5998 rate=0.2 pred=0.59984 ratio=0.9999 rel_dev=0.01% OK
    recheck TL-07: run1 dJD=0.20005 dLST=72.2152 expected=72.2152 residual=0 tol=1 OK
                   run2 dJD=0.2     dLST=72.1971 expected=72.1971 residual=0 tol=1 OK
                   run3 dJD=0.2     dLST=72.1971 expected=72.1971 residual=0 tol=1 OK

  TIMELINK-NEG-A run1: rc=10 judge=6/7|FAIL red=[TL-01]             expect=[TL-01]             -> OK
  TIMELINK-NEG-B run1: rc=10 judge=4/7|FAIL red=[TL-01,TL-02,TL-05] expect=[...]               -> OK
  TIMELINK-NEG-C run1: rc=10 judge=6/7|FAIL red=[TL-04]             expect=[TL-04]             -> OK
```

### 3.4 相邻回归 / S3 / A2

| 套件 | rc | 耗时 |
|---|---|---|
| `clockcheck` | 0 | 1.0s |
| `actioncheck` | 0 | 17.2s |
| `searchcheck` | 0 | 16.1s |
| `locatecheck` | 0 | 23.3s |
| `locate-uicheck` | 0 | 15.2s |
| `timecheck` | 0 | 19.3s |
| `returnuicheck` | 0 | 18.2s |
| `replaycheck` | 0 | 18.2s |
| `timeuicheck` | 0 | 26.2s |
| `locationcheck` | 0 | 18.2s |
| `a2-vulkan` | 0 | 3.1s |
| `s3-stela3-legacy` | 0 | 14.2s（`verdictPASS=True`） |

### 3.5 探针

```
  TOOLBARPROBE: rc=0 verdictDONE=True Q1lines=1(>=1) Q2candidates=12(>=12) -> OK
  TIMELINKPROBE: rc=0 verdictDONE=True placeholderLeak=0(0) missingAnchors=[] -> OK
```

### 3.6 `INTERACTCHECK` 与"对仪器本身的回归断言"

```
  INTERACT run1/2/3: rc=0 armed=1 full18=True degraded=False crosses=0 -> pos-pass
  INTERACT summary: pos-pass=3 env-skip=0 of 3

  INTERACT-PROBE: rc=10 crosses=[IT-05,IT-06,IT-13,IT-16,IT-17,IT-18]
                          expect=[IT-05,IT-06,IT-13,IT-16,IT-17,IT-18] -> OK
```

T34/T35 **都没改 `INTERACTCHECK`**，所以失活窗下的红项集合**必须仍是 W-T31/W-T32 实测的那个边界**
——这条是**对仪器本身的回归断言**，不是产品断言。它成立 ⇒ 要么这台机器的环境没动，
要么仪器没动；若它漂了，那本身就是一条发现。

### 3.7 DYN 双路

`engine` ×3（33.3s）与 `test` 替身 ×3（11.1s）全 rc=0，每次都有
`producer-readback: requested=… observed=… OK` —— **判别性对照回读到了被对照对象的身份**。

---

## 4 跨平台对照：哪些**该一致**，哪些**本来就该不同**

### 4.1 逐位一致（产品口径）

| 项 | mac | Windows |
|---|---|---|
| `TOOLBARCHECK` 正题 | `12/12` / `red=[]` | **同** |
| TB 负控 A/B/C/D 红项 | `[TB-07,09,12]` / `[TB-09,12]` / `[TB-10]` / `[TB-10]`+`covered=false` | **同** |
| `TIMELINKCHECK` 正题 | `7/7` / `red=[]` / 漏 0 | **同** |
| TL 负控 A/B/C 红项 | `[TL-01]` / `[TL-01,02,05]` / `[TL-04]` | **同** |
| 判据条数 | 12 + 7 | **同** |
| 探针结构事实 | `VERDICT=DONE` + 锚点齐全 | **同** |

### 4.2 本来就该不同（平台事实，不是缺陷）

| 项 | mac | Windows | 为什么该不同 |
|---|---|---|---|
| 图形后端 | Metal / MoltenVK | **原生 Vulkan**（`NVIDIA 591.74`） | 两台机器的驱动栈不同 |
| `INTERACT` 失活窗红项 | `[IT-06,IT-13,IT-14,IT-17,IT-18]` | `[IT-05,IT-06,IT-13,IT-16,IT-17,IT-18]` | 窗口失活时 `forceActiveFocus()` 拿不到焦点 ⇒ 注入键到不了 QML 链；**哪几条腿被卡住是平台相关的** |
| TL-07 的 `ΔLST` 数值 | 76.3846° | 72.1971° / 72.2152° | 窗口长度不同；**残差都恒为 0**，恒等式本身两侧都成立 |
| 二进制 md5 | — | `7687513E…` | **链接产物非位级可复现**（既定结论） |

---

## 5 送源同源判据：**`git hash-object`，不是裸 MD5**

⚠️ **Windows 上是 git 检出（CRLF），mac 侧是 scp 送的（LF）** ⇒ **裸 MD5 比的是行尾**。
实测 `src/app/ActionRouter.cpp`：raw `46ad9286…`(mac) vs `5f0a0c3b…`(win) **不同**，
但 `tr -d '\r'` 后**逐位相同**（`win 5810B / 171 CR` vs `mac 5639B / 0 CR`）。

⇒ 判同源改用 **`git hash-object <path>`**（应用与 `git add` 相同的 clean filter，做 EOL 归一化）：
两侧都返回 `fef39003087ff09584b8f46bf893fc8395f68269`。

**结果**：94 项比对 / **90 同源 / 0 内容不同 / 4 缺失**。
4 个"缺失"全是 mac 专用脚本（`tools/t32-verify.sh` / `t33-verify.sh` / `t34-verify.sh` / `t35-verify.sh`）
—— 它们本来就不在 Windows 上跑。详见 `docs/evidence/2026-09-30-t35-timelink/windows-src-sync/README.md`。

⚠️ 那台的 **`repo HEAD = cf738bb` 是撒谎的**（T31 的提交；`git fetch` 报
`Recv failure: Connection was reset` / `Empty reply from server`，树靠 scp 更新）。
**权威是逐文件哈希自证，不是 HEAD。**

---

## 6 本轮新血泪（仪器面 6 条）

### 6.1 🔴🔴 **"修法是实例级而不是类级"** —— 同一个坑换个门又进来（本轮最大教训）

见 §2.4。推广条款：

- 凡是"**从 code point 拼出来的 token**"与"调用点临时变量"共处一文件，就是雷区；
- 改完一处的变量名撞车，**逐 token grep 一遍大小写变体**；
- 更彻底：**让这类 token 消失**（正则里零非 ASCII）；
- 回归断言要覆盖**调用序**（连调三次同值 + 中间插别的 tag + 毒化大小写变体）。

### 6.2 🔴 裸 MD5 跨平台比的是**行尾**（CRLF vs LF）

见 §5。判同源只用 `git hash-object`。

### 6.3 ⚠️ UU远程"前端重启 = 隧道复活"；**`nc -z` 不能当隧道判据**

现象：`UURemote` 仍占 `127.0.0.1:2222`、TCP 能连，但 **SSH banner 不返回** ⇒ 会话已断，
`ssh` 会**静默挂起**（本轮被自动后台化多次）。
**`nc -z 127.0.0.1 2222` 报 PORT-OPEN** —— 它只证明端口在听。
正解 = 读 banner（`nc` 读 6 秒零字节）+ `lsof` 看到仍 LISTEN ⇒ 判"会话断"；
`kill -TERM <UURemote 前端 pid>` + `open -a /Applications/UURemote.app` ⇒ `hostname` 立刻恢复。

### 6.4 ⚠️ `win.sh -ps '<多行脚本>'` 静默失效；`*>>` 绞掉子进程输出

- **`win.sh -ps '多行带 foreach/变量 的脚本'`**：**零输出、零产物、退出码 0**
  ⇒ 一律 `.ps1` 文件 + `scp` + `-File`。
- **`*>> $LOG` 把子进程控制台输出绞成 NUL 字节**（本轮 `t35w-launch.log` 实况：
  头部与 `launcher exit=` 之间只有一列 `0` 加 NUL）。这是**技能里早已记过的**坑
  （PS 5.1 经控制台码页重编码 + UTF-16LE），**照样又踩了**。
  ⇒ 改为 `Start-Process -RedirectStandardOutput/-RedirectStandardError`（原始字节直落盘）。
  ⚠️ **本批跑的是旧版 launcher**（`md5=c20653ec8f23f298cc83116bd33626fd`），
  修好的版本（`md5=a19a59772b0df1a7473e212a7834cb58`）**未被本批验证**。
  影响面很小：SUMMARY 与逐套件 `.out.txt` 都是套件脚本直接用 `Out-File` 落盘的，
  **证据链没有缺口**，损失的只是一个便利日志。

### 6.5 ⚠️ 收尾扫"全部"计划任务残留，不只扫自己那个

`schtasks /query /fo csv` 拉全量、**按任务名**筛（状态字段本地化会乱码成 `����`）。
本轮除自建的两个外，还扫出 **`StelQuickT17Build` / `StelQuickT18LongRun` / `t17winbuild`**
三个**僵尸任务**（2026-09-24 的一次性任务，早已 N/A）并清掉
（定义已存档于 `docs/evidence/.../windows/t35w-launch.log` 同级的
`schtasks-residue/`，含 XML 与删除日志）。

### 6.6 ⚠️ 探针冒号后的**空格宽度逐探针不同**

实测 `TOOLBARPROBE:` 后 **1 个空格**、`TIMELINKPROBE:` 后 **3 个空格**
⇒ 硬编码单空格的锚点**恒不命中**，会把整批探针门判红（假红）。
⇒ 一律 `-match "^<PROBE>:\s+"`，**永不在锚点里硬编码空格数**。
（这条是**本地真值测试抓到的**，零真机往返。）

---

## 7 复现

```sh
# mac 侧（口径与 T17–T35 一致，刻意不换）
tools/t35-verify.sh core 5         # T35 正题 5 跑 + 负控 + 邻套件
tools/t34-verify.sh core 5         # T34 同款

# Windows 侧（需 UU远程隧道 + 桌面会话）
scp -i ~/.ssh/id_ed25519_uu_windows -P 2222 tools/windows/wt35-*.ps1 ztuqfvy@127.0.0.1:'C:/temp/'
ssh ... 'schtasks /create /tn StelQC_t35w_suites /sc once /st 23:59 /it /f \
         /tr "powershell -NoProfile -ExecutionPolicy Bypass -File C:\temp\wt35-launch.ps1"'
ssh ... 'schtasks /run /tn StelQC_t35w_suites'
# 轮询 C:\temp\t35w-launch.log 里出现 "launcher exit="；跑完立刻
ssh ... 'schtasks /delete /tn StelQC_t35w_suites /f'
```

仪器本地自测（mac 上、零真机往返）：

```sh
cd /tmp/pstest35 && $HOME/.local/opt/pwsh/pwsh -NoProfile -File test.ps1         # 期望 97/97
$HOME/.local/opt/pwsh/pwsh -NoProfile -File negctl-judge.ps1                    # 期望 3/3 复现
```

---

## 8 与既有文档的关系 / 未覆盖与移交

- **收口**：`docs/T34_TOOLBAR.zh_CN.md` 与 `docs/T35_TIMELINK.zh_CN.md`
  §"未覆盖与移交"里各有一条"W-T34 / W-T35 Windows 跨平台复验" —— **本文件即该条的结清**。
- **顺带把 `A4` 的跨平台面补齐**：A4 此前在 mac 上收口（`A-alpha` 出口第一条），
  现在**工具栏 + 时间链路两套判据在原生 Vulkan 上也全绿**。

**未覆盖 / 仍然欠着**：

| 项 | 状态 |
|---|---|
| `LOC-04 (b)` 帧延迟量化 | 未做（下一位） |
| 两个自检"就绪门"预算 | 未做 |
| T33 移交的 `findLocations` 排序（完全匹配优先） | 未做 |
| 修好的 `wt35-launch.ps1`（`Start-Process` 版） | **未被本批验证** —— 下一轮 Windows 跑批时它才第一次生效 |
| Windows 侧 `INTERACTCHECK` 的失活红项边界 | **故意保持断言**；若将来漂移，先怀疑环境/仪器，再怀疑产品 |
