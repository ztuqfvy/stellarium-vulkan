#!/bin/zsh
# T34 验证脚本（2026-09-30）：**真实工具栏 + 显示开关**（A4 / A-alpha 的最后一项）。
#
# 用法：tools/t34-verify.sh              （跑全部）
#       tools/t34-verify.sh core 5       （正题 ×5 + 三组负控 + 探针 + 相邻回归）
#       tools/t34-verify.sh negctl       （只跑三组负控 + 探针）
#       tools/t34-verify.sh regress      （只跑九套件 + INTERACTCHECK + S3 + A2 + DYN）
#
# 证据落 docs/evidence/2026-09-30-t34-toolbar/mac/。
# 环境口径（Metal + MoltenVK）**刻意与 t17..t33 / wt29 完全一致，不换**。
#
# ── 本轮改动面 ────────────────────────────────────────────────────────────────
# ① 产品：`src/ui/qml/Toolbar.qml`（新，取代 A2 开发期的临时页切换器）、
#    `src/ui/qml/MainWindow.qml`（挂 Toolbar）、`AppFacade` 新增
#    `displayTogglesRevision`（revision token）+ `actionChecked/actionIsCheckable/actionText`
#    （读侧投影）+ `ensureDisplayForwarding()`（懒订阅 StelActionMgr::actionToggled）。
# ② 仪器：`src/ui/main.cpp` 新增两段
#    - `STELQUICK_TOOL_PROBE=1`  → 命令面探针（注册表规模 / 12 候选逐项 / trigger 翻转 /
#      actionToggled 观测 / revision 读数）——**只报读数、不打 PASS**
#    - `STELQUICK_TOOL_CHECK=1`  → 工具栏自检 **12 条判据**（TB-01..TB-12）
#
# ── 四组读数（判据数恒为 **12**；四组负控的红项见下表）─────────────────────────
# ① 正题（`STELQUICK_TOOL_CHECK=1`）
#    期望 `判据 12/12` + `VERDICT=PASS` + rc=0，且日志里必须有
#    `前提：12 个候选动作 present=12/12 checkable=12/12` ×1（**仪器自证**：没有它，
#    12/12 可能是"动作注册面缺失 ⇒ 判据空跑全过"）。
#    另：`TB-10 落点覆盖：covered=true` ×1（**布局自证** —— 点击落点必须落在
#    按钮及其全部祖先的矩形内；"Toolbar 高度 0"那类假绿会被这一条抓住）。
# ② 负控 A（`STELQUICK_TOOL_REV_OFF=1`）：AppFacade **不订阅** actionToggled
#    ⇒ revision 恒 0 ⇒ TB-07 必红；且 QML 绑定**永不重算** ⇒ TB-09 / TB-12 必红。
#    期望 rc=10、判据 9/12、红项**恰好** [TB-07, TB-09, TB-12]。
# ③ 负控 B（`STELQUICK_TOOL_TOKEN_OFF=1`）：`engineOn` 绑定**不读** revision token
#    ⇒ 引擎翻转后按钮态停在**首帧**。revision 本身照常增长（TB-07 仍绿）
#    ⇒ 期望 rc=10、判据 10/12、红项**恰好** [TB-09, TB-12]。
# ④ 负控 C（`STELQUICK_TOOL_CLICK_OFF=1`）：开关按钮 onClicked 不派发
#    ActionRouter.trigger ⇒ 真实点击不翻引擎 ⇒ TB-10 必红；且引擎**没动**
#    ⇒ engineOn 与引擎仍然一致 ⇒ TB-12 **不**受牵连。
#    期望 rc=10、判据 11/12、红项**恰好** [TB-10]。
#    ⑤ 负控 D（`STELQUICK_TOOL_LAYOUT_BREAK=1`）：Toolbar 根 `implicitHeight`
#    打回 **0** ⇒ 复现"布局坏掉但**不裁剪**"的**假绿掩护**缺陷形态：按钮照画、
#    存在性/可见性判据全绿，只有真实点击落空。期望 rc=10、判据 11/12、
#    红项**恰好** [TB-10]，且 `落点覆盖：covered=**false**`（并点名未覆盖的祖先）。
#    ⑥ 探针（`STELQUICK_TOOL_PROBE=1`）—— 命令面的实测依据（分类靠它，不靠猜）
#    期望 rc=0、`TOOLBARPROBE: VERDICT=DONE`，且 `Q3b` 翻转 4/4。
#    rc=6（`VERDICT=UNAVAILABLE`）= 引擎未初始化，**不洗成 PASS**。
#
# ── 四组负控的红项总表 ────────────────────────────────────────────────────────
#   A REV_OFF      : [TB-07, TB-09, TB-12]
#   B TOKEN_OFF    : [TB-09, TB-12]
#   C CLICK_OFF    : [TB-10]
#   D LAYOUT_BREAK : [TB-10]（与 C 同集，但**证据不同**：D 靠 covered=false，
#                    C 靠引擎读回真值；两者合起来证明"点击腿"与"落点自证"各自承重）
#
# ── 🔴 本轮两条**仪器面**陷阱（脚本要能抓住它们复现）─────────────────────────
# ① Repeater delegate 的 **QObject 父链是空的**（只 setParentItem(Flow)）⇒
#    `QObject::findChild` 永远扫不到它（静态声明的导航按钮却能扫到——同款 API
#    两种结果）。唯一可靠查找 = 视觉树 `QQuickItem::childItems()` 递归。
#    脚本自证：TB-09 必须报 `按钮 12/12 找到`。
# ② Toolbar 根曾是裸 `Rectangle` ⇒ `implicitHeight=0` ⇒ ColumnLayout 给 0 高度、
#    布局全乱，而 Rectangle **默认不裁剪** ⇒ 按钮照画、存在性判据全绿——**假绿掩护**。
#    真实点击落在根内容控件上（按钮根本没收到事件）。
#    脚本自证：`TB-10 落点覆盖：covered=true`（几何必要条件）。
# ③ ⚠️ **`childAt` 会撒谎**（本轮第二条仪器面血泪）：按钮明明在 (48,58)、点击也确实
#    生效，`contentItem()->childAt()` 仍一路返回根级 `ApplicationWindowContentControl`
#    （返回的是 contentItem 自己、不是后代）⇒ 拿它当"点击有没有砸中按钮"的判据
#    必误判。已弃用，改走③的几何覆盖自证。
#
# ── 相邻回归（本轮动了 main.cpp / AppFacade / QML ⇒ 必须全套重跑）──────────────
#  9 套件（time / returnui / search / action / locate / locate-ui / replay / clock /
#  timeui）要求 rc=0；另加 A2 逐像素、DYN 双路（engine + 替身）、`producer-readback`、
#  S3 旧宿主。
#  ⚠️ **INTERACTCHECK 单列**：它依赖"窗口真的拿到系统焦点"（血泪第 17 条）。脚本
#     `env` 直启的进程不被 LaunchServices 认作 `.app` 实例 ⇒ 常常抢不到前台 ⇒
#     判据记 UNAVAILABLE（rc=6）。这是**仪器前提不成立、不是回归** ⇒ 记 ENV-SKIP，
#     **不洗成 PASS**，也不算失败；真出 ✗ 才判红。
set -u
cd /Users/ztuqfvy/qt_demo/stellarium_vulkan

export VK_DRIVER_FILES=/opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json
export QT_VULKAN_LIB=/opt/homebrew/opt/vulkan-loader/lib/libvulkan.1.dylib
export STELQUICK_GRAPHICS_API=metal

BIN=./build-release/src/ui/stelQuickUI.app/Contents/MacOS/stelQuickUI
OUT=docs/evidence/2026-09-30-t34-toolbar/mac
TARGET=${1:-all}
CORE_RUNS=${2:-5}
DYN_RUNS=${2:-3}
mkdir -p "$OUT"
FAILED=0

# 显示器休眠门（T29/W-T29 的教训，照抄）：本机电池档 displaysleep=2。显示器一旦睡，
# 布局/polish 与帧泵节拍都会受影响 ⇒ 读数污染；T34 的点击腿**更**依赖布局已落定。
wake_display() {
  caffeinate -u -t 3 >/dev/null 2>&1
  caffeinate -dimsu -t 7200 >/dev/null 2>&1 &
  CAFFEINATE_PID=$!
  sleep 1
  echo "环境：已请求唤醒显示器并挂 caffeinate（pid=$CAFFEINATE_PID，2 小时）"
}
CAFFEINATE_PID=""
wake_display

run_suite() {
  local out=$1; shift
  "$@" > "$out" 2>&1
  return $?
}

env_note() {
  echo "环境：$(uptime | sed 's/^ *//')"
  local ds
  ds=$(pmset -g custom 2>/dev/null | grep -E "^[[:space:]]*displaysleep" | head -1 | awk '{print $2}')
  [[ -n "$ds" ]] && echo "环境注记：displaysleep=${ds} 分钟（本机为 2 ⇒ 跑前必须 caffeinate 唤醒）"
  if pgrep -q mdbulkimport; then
    echo "环境注记：Spotlight 批量索引正在运行（mdbulkimport）——可能拖慢布局/polish"
  fi
  echo "二进制：$(ls -l "$BIN" | awk '{print $5" B  "$6" "$7" "$8}')  md5=$(md5 -q "$BIN")"
}

# ✗ 行里的判据 ID 抽取（只用原始量，不复刻被测逻辑）。
red_ids() {
  /usr/bin/grep -E "TOOLBARCHECK: +✗ " "$1" \
    | /usr/bin/grep -oE "✗ TB-[0-9]+[ab]?" \
    | /usr/bin/awk '{print $2}' | sort | tr '\n' ','
}

: > "$OUT/rc-summary.txt"

# ══ ① 正题：TOOLBARCHECK × N（12 条）═════════════════════════════════════════
if [[ "$TARGET" == "all" || "$TARGET" == "core" || "$TARGET" == "pos" ]]; then
  integer pass=0 envskip=0 bad=0 i rc
  : > "$OUT/toolbarcheck-mac-n$CORE_RUNS.txt"
  for i in $(seq 1 $CORE_RUNS); do
    run_suite "$OUT/toolbarcheck-mac-run$i.txt" env STELQUICK_TOOL_CHECK=1 "$BIN"
    rc=$?
    line=$(/usr/bin/grep -E "TOOLBARCHECK: 判据" "$OUT/toolbarcheck-mac-run$i.txt" | tail -1)
    verd=$(/usr/bin/grep -E "TOOLBARCHECK: VERDICT" "$OUT/toolbarcheck-mac-run$i.txt" | tail -1)
    fails=$(/usr/bin/grep -cE "TOOLBARCHECK: +✗" "$OUT/toolbarcheck-mac-run$i.txt")
    # 仪器自证：12 候选动作 present/checkable 前提行。缺了它 12/12 不算数。
    pre=$(/usr/bin/grep -cE "TOOLBARCHECK: +前提：12 个候选动作 present=12/12 checkable=12/12" \
              "$OUT/toolbarcheck-mac-run$i.txt")
    # 仪器面自证②：TB-09 必须 12/12 找到按钮（Repeater delegate 的 QObject 父链为
    # 空 ⇒ findChild 扫不到，靠视觉树递归才修好）。
    found12=$(/usr/bin/grep -cE "TOOLBARCHECK: +✓ TB-09 UI 腿：按钮 12/12 找到、态一致 12/12" \
                  "$OUT/toolbarcheck-mac-run$i.txt")
    # 布局自证：点击落点必须落在按钮**及其全部祖先**的矩形内（几何必要条件）。
    # ⚠️ **不要**用 `childAt` 当判据：它在 ApplicationWindow 的 contentItem 上会
    #    撒谎（实测按钮明明在 (48,58) 且点击生效，childAt 仍返回根级
    #    `ApplicationWindowContentControl`）⇒ 只会造成误判（T34 血泪）。
    cov=$(/usr/bin/grep -cE "TOOLBARCHECK: +TB-10 落点覆盖：covered=true" \
              "$OUT/toolbarcheck-mac-run$i.txt")
    kind="FAIL"
    if [[ $rc -eq 0 && "$line" == *"判据 12/12"* && "$verd" == *"VERDICT=PASS"* \
          && $fails -eq 0 && $pre -eq 1 && $found12 -eq 1 && $cov -eq 1 ]]; then
      kind="PASS"; (( pass++ ))
    elif [[ $rc -eq 6 && "$verd" == *"UNAVAILABLE"* && $fails -eq 0 ]]; then
      # 引擎未初始化 ⇒ 前提门判 UNAVAILABLE（**不洗成 PASS**）
      kind="ENV-SKIP（引擎前提不成立 ⇒ UNAVAILABLE，非产品缺陷）"; (( envskip++ ))
    else
      kind="FAIL"; (( bad++ )); FAILED=1
    fi
    {
      echo "──────── TOOLBARCHECK run $i/$CORE_RUNS（rc=$rc）→ $kind（前提行=$pre/1 按钮12/12行=$found12/1 落点覆盖=$cov/1）────────"
      grep -E "TOOLBARCHECK:" "$OUT/toolbarcheck-mac-run$i.txt"
    } >> "$OUT/toolbarcheck-mac-n$CORE_RUNS.txt"
  done
  echo "toolbarcheck-mac pos-pass=$pass env-skip=$envskip bad=$bad（共 $CORE_RUNS 跑；env-skip = 引擎前提不成立，**不洗成 PASS**）" \
    | tee -a "$OUT/rc-summary.txt"
  ghosts=$(pgrep -fc stelQuickUI 2>/dev/null || echo 0)
  echo "toolbarcheck-mac 残留进程数=$ghosts（要求 0）" | tee -a "$OUT/rc-summary.txt"
  [[ "$ghosts" == "0" ]] || FAILED=1
  if [[ $pass -eq 0 && $envskip -eq 0 ]]; then
    echo "toolbarcheck-mac 正题未取得任何一次 12/12 —— 环境太差（FAILED）" | tee -a "$OUT/rc-summary.txt"
    FAILED=1
  fi
fi

# ══ ②..⑥ 四组负控 + 探针 ═════════════════════════════════════════════════════
if [[ "$TARGET" == "all" || "$TARGET" == "core" || "$TARGET" == "negctl" ]]; then
  : > "$OUT/negctl-mac.txt"

  # ── 负控 A：REV_OFF（AppFacade 不订阅 actionToggled）──────────────────────
  run_suite "$OUT/negctl-A-rev-off.txt" env STELQUICK_TOOL_CHECK=1 \
    STELQUICK_TOOL_REV_OFF=1 "$BIN"
  rc=$?
  line=$(/usr/bin/grep -E "TOOLBARCHECK: 判据" "$OUT/negctl-A-rev-off.txt" | tail -1)
  reds=$(red_ids "$OUT/negctl-A-rev-off.txt")
  vA="negctl-A-BAD"
  if [[ $rc -eq 10 && "$line" == *"判据 9/12"* && "$reds" == "TB-07,TB-09,TB-12," ]]; then
    vA="negctl-A-OK"; else FAILED=1
  fi
  {
    echo "──────── 负控 A（REV_OFF）（rc=$rc）→ $vA ────────"
    echo "  期望：rc=10 ∧ 判据 9/12 ∧ 红项**恰好** [TB-07,TB-09,TB-12]"
    echo "  实测：红=[$reds]"
    grep -E "TOOLBARCHECK: (✗|判据|VERDICT)" "$OUT/negctl-A-rev-off.txt"
  } >> "$OUT/negctl-mac.txt"
  echo "negctl-A $vA（要求 rc=10 ∧ 9/12 ∧ 红恰好 TB-07/09/12）" | tee -a "$OUT/rc-summary.txt"

  # ── 负控 B：TOKEN_OFF（engineOn 绑定不读 revision token）──────────────────
  run_suite "$OUT/negctl-B-token-off.txt" env STELQUICK_TOOL_CHECK=1 \
    STELQUICK_TOOL_TOKEN_OFF=1 "$BIN"
  rc=$?
  line=$(/usr/bin/grep -E "TOOLBARCHECK: 判据" "$OUT/negctl-B-token-off.txt" | tail -1)
  reds=$(red_ids "$OUT/negctl-B-token-off.txt")
  vB="negctl-B-BAD"
  if [[ $rc -eq 10 && "$line" == *"判据 10/12"* && "$reds" == "TB-09,TB-12," ]]; then
    vB="negctl-B-OK"; else FAILED=1
  fi
  {
    echo "──────── 负控 B（TOKEN_OFF）（rc=$rc）→ $vB ────────"
    echo "  期望：rc=10 ∧ 判据 10/12 ∧ 红项**恰好** [TB-09,TB-12]（revision 照常涨 ⇒ TB-07 仍绿）"
    echo "  实测：红=[$reds]"
    grep -E "TOOLBARCHECK: (✗|判据|VERDICT)" "$OUT/negctl-B-token-off.txt"
  } >> "$OUT/negctl-mac.txt"
  echo "negctl-B $vB（要求 rc=10 ∧ 10/12 ∧ 红恰好 TB-09/12）" | tee -a "$OUT/rc-summary.txt"

  # ── 负控 C：CLICK_OFF（按钮 onClicked 不派发）─────────────────────────────
  # 🔴 **隔离良好的判别负控**：只 TB-10 红，TB-12 **不**受牵连（引擎没动 ⇒
  #    engineOn 与引擎依旧一致）。若 TB-12 也跟着红，说明"绑定一致"判据把
  #    "点击没接上"算进去了 ⇒ 隔离性坏了。
  run_suite "$OUT/negctl-C-click-off.txt" env STELQUICK_TOOL_CHECK=1 \
    STELQUICK_TOOL_CLICK_OFF=1 "$BIN"
  rc=$?
  line=$(/usr/bin/grep -E "TOOLBARCHECK: 判据" "$OUT/negctl-C-click-off.txt" | tail -1)
  reds=$(red_ids "$OUT/negctl-C-click-off.txt")
  vC="negctl-C-BAD"
  if [[ $rc -eq 10 && "$line" == *"判据 11/12"* && "$reds" == "TB-10," ]]; then
    vC="negctl-C-OK"; else FAILED=1
  fi
  {
    echo "──────── 负控 C（CLICK_OFF）（rc=$rc）→ $vC ────────"
    echo "  期望：rc=10 ∧ 判据 11/12 ∧ 红项**恰好** [TB-10]（TB-12 不许被牵连）"
    echo "  实测：红=[$reds]"
    grep -E "TOOLBARCHECK: (✗|判据|VERDICT)" "$OUT/negctl-C-click-off.txt"
  } >> "$OUT/negctl-mac.txt"
  echo "negctl-C $vC（要求 rc=10 ∧ 11/12 ∧ 红恰好 TB-10）" | tee -a "$OUT/rc-summary.txt"

  # ── 负控 D：LAYOUT_BREAK（Toolbar implicitHeight 打回 0）────────────────────
  # 🔴 复现的**不是**"某个开关失灵"，而是本轮最阴的一类：布局坏掉但
  #    **不裁剪** ⇒ 按钮照画、存在性/可见性判据全绿（假绿掩护）；只有真实点击
  #    落空（TB-10 红）+ `落点覆盖=false` 能抓到。
  #    期望：rc=10 ∧ 判据 11/12 ∧ 红项**恰好** [TB-10] ∧ 覆盖行 `covered=**false**`。
  run_suite "$OUT/negctl-D-layout-break.txt" env STELQUICK_TOOL_CHECK=1 \
    STELQUICK_TOOL_LAYOUT_BREAK=1 "$BIN"
  rc=$?
  line=$(/usr/bin/grep -E "TOOLBARCHECK: 判据" "$OUT/negctl-D-layout-break.txt" | tail -1)
  reds=$(red_ids "$OUT/negctl-D-layout-break.txt")
  covfalse=$(/usr/bin/grep -cE "TOOLBARCHECK: +TB-10 落点覆盖：covered=\*\*false\*\*" \
                 "$OUT/negctl-D-layout-break.txt")
  covnames=$(/usr/bin/grep -m1 -E "TOOLBARCHECK: +TB-10 落点覆盖：" "$OUT/negctl-D-layout-break.txt" \
             | sed 's/.*未覆盖的祖先：//;s/）.*//')
  vD="negctl-D-BAD"
  if [[ $rc -eq 10 && "$line" == *"判据 11/12"* && "$reds" == "TB-10," && $covfalse -eq 1 ]]; then
    vD="negctl-D-OK"; else FAILED=1
  fi
  {
    echo "──────── 负控 D（LAYOUT_BREAK）（rc=$rc）→ $vD ────────"
    echo "  期望：rc=10 ∧ 判据 11/12 ∧ 红项**恰好** [TB-10] ∧ 落点覆盖行 covered=false"
    echo "  实测：红=[$reds]  覆盖=false 行=$covfalse/1  未覆盖的祖先=[$covnames]"
    grep -E "TOOLBARCHECK: (✗|TB-10 落点覆盖|判据|VERDICT)" "$OUT/negctl-D-layout-break.txt"
  } >> "$OUT/negctl-mac.txt"
  echo "negctl-D $vD（要求 rc=10 ∧ 11/12 ∧ 红恰好 TB-10 ∧ covered=false）" | tee -a "$OUT/rc-summary.txt"

  # ── ⑤ 探针：命令面的实测依据 ──────────────────────────────────────────────
  run_suite "$OUT/probe-tool-data-mac.txt" env STELQUICK_TOOL_PROBE=1 "$BIN"
  prc=$?
  pline=$(/usr/bin/grep -E "TOOLBARPROBE: VERDICT" "$OUT/probe-tool-data-mac.txt" | tail -1)
  pq1=$(/usr/bin/grep -cE "TOOLBARPROBE: Q1 " "$OUT/probe-tool-data-mac.txt")
  # ⚠️ **不要**把 Q3b 的**值**写进判据：引擎把开关态落盘，boot 初值**逐跑可变**
  #    （实测：有的跑 Lines=false、有的跑 Lines=true）⇒ `写后 getter=false` 的**条数**
  #    在 1..4 之间浮动。判据只许认"**4 条 Q3b**（逐一对照、应翻转）"这件结构性事实
  #    —— 这与 TB-* 一律用"写前快照"当对照是同一个道理。
  pq3b=$(/usr/bin/grep -cE "TOOLBARPROBE: Q3b " "$OUT/probe-tool-data-mac.txt")
  pq4=$(/usr/bin/grep -cE "TOOLBARPROBE: Q4 actionToggled 观测：共发射 4 次" "$OUT/probe-tool-data-mac.txt")
  if [[ $prc -eq 0 && "$pline" == *"VERDICT=DONE"* && $pq1 -eq 1 && $pq3b -eq 4 && $pq4 -eq 1 ]]; then
    echo "probe-tool-data-mac probe-OK（$pline；Q1 行=$pq1 Q3b 对照行=$pq3b/4 Q4 发射 4 次行=$pq4）" | tee -a "$OUT/rc-summary.txt"
  elif [[ $prc -eq 6 && "$pline" == *"UNAVAILABLE"* ]]; then
    echo "probe-tool-data-mac ENV-SKIP（$pline ⇒ 引擎未初始化，**不洗成 PASS**）" | tee -a "$OUT/rc-summary.txt"
    FAILED=1
  else
    echo "probe-tool-data-mac probe-BAD（rc=$prc / $pline / Q1=$pq1 Q3b=$pq3b）" | tee -a "$OUT/rc-summary.txt"
    FAILED=1
  fi
fi

# ══ ⑥ 相邻回归：本轮改了 main.cpp / AppFacade / QML ⇒ 全套重跑 ═══════════════
if [[ "$TARGET" == "all" || "$TARGET" == "core" || "$TARGET" == "regress" ]]; then
  for spec in \
    "STELQUICK_TIME_CHECK=1:timecheck" \
    "STELQUICK_RETURN_UI_CHECK=1:returnuicheck" \
    "STELQUICK_SEARCH_CHECK=1:searchcheck" \
    "STELQUICK_ACTION_CHECK=1:actioncheck" \
    "STELQUICK_LOCATE_CHECK=1:locatecheck" \
    "STELQUICK_UI_CHECK=1:locate-uicheck" \
    "STELQUICK_REPLAY_CHECK=1:replaycheck" \
    "STELQUICK_CLOCK_CHECK=1:clockcheck" \
    "STELQUICK_TIME_UI_CHECK=1:timeuicheck" \
    "STELQUICK_LOC_CHECK=1:locationcheck"
  do
    var=${spec%%:*}; name=${spec##*:}
    run_suite "$OUT/regression-$name.txt" env $var "$BIN"
    rc=$?
    echo "regression-$name rc=$rc" | tee -a "$OUT/rc-summary.txt"
    [[ $rc -eq 0 ]] || FAILED=1
  done

  # ── INTERACTCHECK 单独处理：它依赖**窗口真的拿到系统焦点**（血泪第 17 条）──
  run_suite "$OUT/regression-interactcheck.txt" env STELQUICK_INTERACT_UI_CHECK=1 \
    STELQUICK_INTERACT_REQUEST_ACTIVATE=1 "$BIN"
  irc=$?
  iline=$(/usr/bin/grep -E "INTERACTCHECK: 判据" "$OUT/regression-interactcheck.txt" | tail -1)
  ireds=$(/usr/bin/grep -cE "INTERACTCHECK: ✗" "$OUT/regression-interactcheck.txt")
  if [[ $irc -eq 0 && "$iline" == *"判据 18/18"* && $ireds -eq 0 ]]; then
    echo "regression-interactcheck rc=0（18/18，窗口已激活）" | tee -a "$OUT/rc-summary.txt"
  elif [[ $irc -eq 6 && "$iline" == *"UNAVAILABLE"* && $ireds -eq 0 ]]; then
    echo "regression-interactcheck ENV-SKIP（$iline ⇒ 窗口未获系统焦点，**不洗成 PASS**）" \
      | tee -a "$OUT/rc-summary.txt"
  else
    echo "regression-interactcheck FAIL（rc=$irc；$iline；✗=$ireds）" | tee -a "$OUT/rc-summary.txt"
    FAILED=1
  fi

  # ── S3 旧宿主（`stellarium`）—— **真的跑一遍**，不再看 md5（陷阱 37）────────
  STELA3_CHECK=1 ./build-release/src/stellarium > "$OUT/regression-s3-stela3.txt" 2>&1
  rc=$?
  echo "regression-s3-stela3 rc=$rc" | tee -a "$OUT/rc-summary.txt"
  [[ $rc -eq 0 ]] || FAILED=1
fi

# ══ ⑦ A2 逐像素（Metal）═════════════════════════════════════════════════════
if [[ "$TARGET" == "all" || "$TARGET" == "regress" || "$TARGET" == "core" ]]; then
  run_suite "$OUT/regression-a2-metal.txt" env STELQUICK_A2_CHECK=1 "$BIN"
  rc=$?
  echo "regression-a2-metal rc=$rc" | tee -a "$OUT/rc-summary.txt"
  [[ $rc -eq 0 ]] || FAILED=1
fi

# ══ ⑧ DYN 双跑（真实引擎 + 替身判别性对照）══════════════════════════════════
# ⚠️ 生产者必须**显式**指定：`STELQUICK_DYN_PRODUCER` 默认是替身（main.cpp 只看
#    `== "engine"`），不设它就两次都跑替身 —— 见
#    docs/evidence/2026-09-29-t29-ime/CORRECTION.md。
run_dyn() {
  local kind=$1 n=$DYN_RUNS pass=0 i rc
  : > "$OUT/regression-dyn-$kind-metal.txt"
  for i in $(seq 1 $n); do
    run_suite "$OUT/regression-dyn-$kind-metal-run$i.txt" env STELQUICK_DYN_CHECK=1 \
      STELQUICK_DYN_PRODUCER=$kind "$BIN"
    rc=$?
    echo "──────── DYN($kind) run $i/$n（rc=$rc）────────" >> "$OUT/regression-dyn-$kind-metal.txt"
    grep -E "DYNCHECK:" "$OUT/regression-dyn-$kind-metal-run$i.txt" >> "$OUT/regression-dyn-$kind-metal.txt"
    [[ $rc -eq 0 ]] && (( pass++ ))
  done
  { echo "──────── 汇总 ────────"; env_note
    echo "DYN 显示/降级判据（producer=$kind）：$pass/$n 次 PASS"
    echo "生产者回读：$(/usr/bin/grep -m1 -h '生产者=' "$OUT/regression-dyn-$kind-metal-run1.txt" 2>/dev/null)"
  } >> "$OUT/regression-dyn-$kind-metal.txt"
  echo "regression-dyn-$kind-metal $pass/$n PASS" | tee -a "$OUT/rc-summary.txt"
}
if [[ "$TARGET" == "all" || "$TARGET" == "regress" || "$TARGET" == "dyn" ]]; then
  run_dyn engine
  run_dyn test
  # 判别性对照必须**回读身份**：engine 路的标签里必须是"真实引擎"，test 路必须是"替身"
  r1=$(/usr/bin/grep -m1 -h '生产者=' "$OUT/regression-dyn-engine-metal-run1.txt" 2>/dev/null)
  r2=$(/usr/bin/grep -m1 -h '生产者=' "$OUT/regression-dyn-test-metal-run1.txt" 2>/dev/null)
  if [[ "$r1" == *"真实引擎"* && "$r2" == *"替身场景"* ]]; then
    echo "producer-readback OK" | tee -a "$OUT/rc-summary.txt"
  else
    echo "producer-readback MISMATCH（engine=$r1 / test=$r2）" | tee -a "$OUT/rc-summary.txt"
    FAILED=1
  fi
fi

cat "$OUT/rc-summary.txt"
[[ -n "$CAFFEINATE_PID" ]] && kill "$CAFFEINATE_PID" 2>/dev/null
echo "T34 证据已写入 $OUT/（FAILED=$FAILED）"
[[ $FAILED -eq 0 ]] || exit 1
