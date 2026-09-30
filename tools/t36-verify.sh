#!/bin/zsh
# T36 验证脚本（2026-09-30）：**合流形态的个人版配置目录隔离 + 首次播种**。
#
# 用法：tools/t36-verify.sh              （跑全部）
#       tools/t36-verify.sh core 2       （正题场景 ×2：首次 + 非首次）
#       tools/t36-verify.sh negctl       （只跑两组负控）
#       tools/t36-verify.sh regress      （只跑相邻回归）
#
# 证据落 docs/evidence/2026-09-30-t36-config/mac/。
# 环境口径（Metal + MoltenVK）**刻意与 t17..t35 完全一致，不换**。
#
# ── 这一轮要定的"性" ─────────────────────────────────────────────────────────
# 合流形态此前用引擎**默认**用户目录 = **原版 Stellarium 的目录**。实测
# （docs/evidence/2026-09-30-t36-config/probe/）：跑一次 STELQUICK_TOOL_CHECK=1 后，
#   config.ini                  mtime 变（QSettings 整体重写）
#   log.txt                     10308 B → 10387 B（原版日志被顶掉）
#   modules/Oculars/ocular.ini  mtime 变
# 根因是路径选错：`immediateSave()` 在 `immediate_save_details=true` 时直写
# `getSettings()`；日志 `StelLogger::init(userDir + "/log.txt")` 同源。
# A-1.0 范围表：「资源路径 …… 配置保存 | 最小 | **必须**」+「配置使用**独立个人版
# 目录**，避免修改原程序设置」。
#
# ── 本脚本的判据分工（**内外两层，别互相顶替**）──────────────────────────────
#   内层（被测自检 `CONFIGCHECK`，8 条）：CFG-01..CFG-08，见 ConfigIsolationCheck.hpp。
#   外层（本脚本，**换来源的对照量**，陷阱 43）：
#     · 原目录 **逐文件 size+mtime+md5 清单** 跑前 == 跑后（正题场景必须逐位相同）；
#     · 播种清单**独立复算**：不用被测的 CFG-08 读数，脚本自己 find 两边比；
#     · 配置种子**独立解析**：不用 QSettings，脚本自己按行解析两边 config.ini 的键集；
#     · "用户改过的值不被覆盖"：往个人版 config.ini 追加一个哨兵段，跑完必须还在；
#     · 判据行与红项集合按**预期集合**逐位比对（负控红项必须两两不同）。
#
# ── 负控跑在**替身用户目录**上（安全设计，不是偷懒）──────────────────────────
#   负控 A（关隔离）会让程序**故意**写穿"原目录"。若拿真实原版目录去撞，就是拿
#   用户的真实配置当耗材。做法：负控统一用 `STEL_USERDIR=/tmp/t36-fake-original`
#   （真实原目录的一份拷贝）⇒ 派生出的"原目录"是 `/tmp/t36-fake-original-quick`，
#   真实原目录**全程零风险**。判据全是路径/字节事实，替身上同样成立。
#   ⚠️ 真实原目录只在**正题场景**被涉及 —— 而正题恰恰要证明它**一字节不动**。
#
# ── 安全门（硬约束）─────────────────────────────────────────────────────────
#   `rm -rf` 只允许作用于 `$PERSONAL`（= `$ORIG-quick`）与 `/tmp` 下的替身，
#   且路径必须通过 guard_paths() 的形态检查。**永不**触碰 `$ORIG`。
set -u
cd /Users/ztuqfvy/qt_demo/stellarium_vulkan

export VK_DRIVER_FILES=/opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json
export QT_VULKAN_LIB=/opt/homebrew/opt/vulkan-loader/lib/libvulkan.1.dylib
export STELQUICK_GRAPHICS_API=metal

BIN=./build-release/src/ui/stelQuickUI.app/Contents/MacOS/stelQuickUI
ROOT=docs/evidence/2026-09-30-t36-config
OUT=$ROOT/mac
ORIG="$HOME/Library/Application Support/Stellarium"
PERSONAL="$ORIG-quick"
FAKE=/tmp/t36-fake-original
FAKE_PERSONAL="$FAKE-quick"
PY=/Users/ztuqfvy/.workbuddy/binaries/python/versions/3.13.12/bin/python3
TARGET=${1:-all}
CORE_RUNS=${2:-2}
DYN_RUNS=${2:-3}
mkdir -p "$OUT"
FAILED=0

# ── 安全门 ─────────────────────────────────────────────────────────────────
guard_paths() {
  case "$PERSONAL" in
    */Stellarium-quick) ;;
    *) echo "!! 拒绝：PERSONAL=$PERSONAL 形态不对，脚本停止"; exit 2 ;;
  esac
  [[ "$PERSONAL" != "$ORIG" ]] || { echo "!! 拒绝：PERSONAL == ORIG"; exit 2; }
  [[ -n "$ORIG" && "$ORIG" != "/" ]] || { echo "!! 拒绝：ORIG 异常"; exit 2; }
}
guard_paths
echo "原目录（**只读，永不写**）：$ORIG"
echo "个人版目录（可清理）：$PERSONAL"

rm_personal()   { guard_paths; /bin/rm -rf -- "$PERSONAL"; }
rm_fake_all()   {
  case "$FAKE" in
    /tmp/t36-fake-original) ;;
    *) echo "!! 拒绝：FAKE=$FAKE 形态不对"; exit 2 ;;
  esac
  /bin/rm -rf -- "$FAKE" "$FAKE_PERSONAL"
}

# 显示器休眠门（T29/W-T29 的教训，照抄）：本机电池档 displaysleep=2。
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
  if pgrep -q mdbulkimport; then
    echo "环境注记：Spotlight 批量索引正在运行（mdbulkimport）——可能拖慢布局/polish"
  fi
  echo "二进制：$(ls -l "$BIN" | awk '{print $5" B  "$6" "$7" "$8}')  md5=$(md5 -q "$BIN")"
}

# ── 读数与判据抽取（只用原始量，不复刻被测逻辑）──────────────────────────────
judge_line() { /usr/bin/grep -E "CONFIGCHECK: 判据" "$1" | LC_ALL=C /usr/bin/tail -1; }
verdict_line() { /usr/bin/grep -E "CONFIGCHECK: VERDICT=" "$1" | LC_ALL=C /usr/bin/tail -1; }
red_ids() {
  /usr/bin/grep -E "CONFIGCHECK: +✗ " "$1" \
    | /usr/bin/grep -oE "✗ CFG-[0-9]+" \
    | /usr/bin/awk '{print $2}' | LC_ALL=C /usr/bin/sort | /usr/bin/tr '\n' ',' | sed 's/,$//'
}
# 逐文件清单：relpath|size|mtime|md5
manifest() {
  local out=$1 root=$2
  : > "$out"
  /usr/bin/find "$root" -type f -print | LC_ALL=C /usr/bin/sort | while IFS= read -r f; do
    printf '%s|%s|%s|%s\n' "${f#$root/}" \
      "$(/usr/bin/stat -f '%z' "$f")" "$(/usr/bin/stat -f '%m' "$f")" "$(/sbin/md5 -q "$f")" >> "$out"
  done
}
# 播种清单的**独立复算**（不比内容，只比存在性；排除瞬态）
fileset() {
  local root=$1 out=$2
  /usr/bin/find "$root" -type f -print \
    | sed "s|^$root/||" \
    | LC_ALL=C /usr/bin/grep -vE '^(log\.txt|output\.txt|config\.old)$' \
    | LC_ALL=C /usr/bin/sort > "$out"
}
# 配置键集的**独立解析**（自己按行解析，不用 QSettings —— 换来源的对照量）
keys_of() {
  local f=$1 out=$2
  "$PY" - "$f" > "$out" <<'PY'
import re, sys
sec = ""
try:
    fh = open(sys.argv[1], encoding="utf-8", errors="replace")
except OSError:
    sys.exit(0)
for line in fh:
    s = line.strip()
    if not s or s.startswith(";") or s.startswith("#"):
        continue
    m = re.match(r"^\[(.+)\]$", s)
    if m:
        sec = m.group(1)
        continue
    if "=" in s:
        k = s.split("=", 1)[0].strip()
        if k:
            print("%s/%s" % (sec, k))
PY
  LC_ALL=C /usr/bin/sort -o "$out" "$out"
}
# 一条成对判据的记账
declare -i SCRIPT_PASS=0 SCRIPT_FAIL=0
judge() {  # judge <cond-ok:0/1> <描述>
  if [[ "$1" -eq 0 ]]; then
    (( SCRIPT_PASS++ )); echo "  ✓ $2" | tee -a "$OUT/rc-summary.txt"
  else
    (( SCRIPT_FAIL++ )); FAILED=1; echo "  ✗ $2" | tee -a "$OUT/rc-summary.txt"
  fi
}

: > "$OUT/rc-summary.txt"
env_note | tee -a "$OUT/rc-summary.txt"

# ══ ① 正题 S1：**首次**（个人版目录不存在 ⇒ 必须播种）════════════════════════
if [[ "$TARGET" == "all" || "$TARGET" == "core" || "$TARGET" == "pos" ]]; then
  echo "──────── S1 首次（先清空个人版目录；原目录保持不动作为对照）────────" \
    | tee -a "$OUT/rc-summary.txt"
  manifest "$OUT/orig-manifest-before-s1.txt" "$ORIG"
  rm_personal
  run_suite "$OUT/configcheck-first.txt" env STELQUICK_CONFIG_CHECK=1 "$BIN"
  rc=$?
  manifest "$OUT/orig-manifest-after-s1.txt" "$ORIG"

  line=$(judge_line "$OUT/configcheck-first.txt"); verd=$(verdict_line "$OUT/configcheck-first.txt")
  reds=$(red_ids "$OUT/configcheck-first.txt")
  mig=$(/usr/bin/grep -E "^CONFIGISO: migration=" "$OUT/configcheck-first.txt" | LC_ALL=C /usr/bin/tail -1)

  judge $([[ $rc -eq 0 ]] && echo 0 || echo 1) "S1 rc=0（实得 $rc）"
  judge $([[ "$line" == *"判据 8/8"* ]] && echo 0 || echo 1) "S1 判据 8/8（$line）"
  judge $([[ "$verd" == *"VERDICT=PASS"* ]] && echo 0 || echo 1) "S1 VERDICT=PASS（$verd）"
  judge $([[ -z "$reds" ]] && echo 0 || echo 1) "S1 红项集合为空（实得 [$reds]）"
  judge $([[ "$mig" == *"migration=ran"* ]] && echo 0 || echo 1) "S1 引导期确实进了播种流程（$mig）"

  # 外层对照 1：原目录**逐位**不变（size+mtime+md5 清单全等）
  /usr/bin/diff "$OUT/orig-manifest-before-s1.txt" "$OUT/orig-manifest-after-s1.txt" \
    > "$OUT/orig-manifest-diff-s1.txt" 2>&1
  judge $([[ -s "$OUT/orig-manifest-diff-s1.txt" ]] && echo 1 || echo 0) \
    "S1 原目录清单跑前==跑后（差异行数 $(/usr/bin/wc -l < "$OUT/orig-manifest-diff-s1.txt" | tr -d ' ')）"

  # 外层对照 2：播种清单**独立复算**（不采信 CFG-08 的读数）
  fileset "$ORIG" "$OUT/fileset-orig.txt"
  fileset "$PERSONAL" "$OUT/fileset-personal.txt"
  LC_ALL=C /usr/bin/comm -23 "$OUT/fileset-orig.txt" "$OUT/fileset-personal.txt" \
    > "$OUT/fileset-missing.txt"
  judge $([[ -s "$OUT/fileset-missing.txt" ]] && echo 1 || echo 0) \
    "S1 独立复算：原目录 $(/usr/bin/wc -l < "$OUT/fileset-orig.txt" | tr -d ' ') 个非瞬态文件在个人版目录里一个不缺（缺 $(/usr/bin/wc -l < "$OUT/fileset-missing.txt" | tr -d ' ')）"

  # 外层对照 3：配置键集**独立解析**（不用 QSettings）
  keys_of "$ORIG/config.ini" "$OUT/keys-orig.txt"
  keys_of "$PERSONAL/config.ini" "$OUT/keys-personal-s1.txt"
  LC_ALL=C /usr/bin/comm -23 "$OUT/keys-orig.txt" "$OUT/keys-personal-s1.txt" \
    > "$OUT/keys-missing-s1.txt"
  judge $([[ -s "$OUT/keys-missing-s1.txt" ]] && echo 1 || echo 0) \
    "S1 独立解析：原目录 $(/usr/bin/wc -l < "$OUT/keys-orig.txt" | tr -d ' ') 个键在个人版里全在（缺 $(/usr/bin/wc -l < "$OUT/keys-missing-s1.txt" | tr -d ' ')）"

  echo "  · S1 CONFIGISO 读数：" | tee -a "$OUT/rc-summary.txt"
  /usr/bin/grep -E "^CONFIGISO: " "$OUT/configcheck-first.txt" | sed 's/^/      /' \
    | tee -a "$OUT/rc-summary.txt"
  echo "  · S1 判据全文（见 $OUT/configcheck-first.txt）" | tee -a "$OUT/rc-summary.txt"
fi

# ══ ② 正题 S2：**非首次**（个人版目录已存在 ⇒ 必须跳过播种且不覆盖用户改动）══
if [[ "$TARGET" == "all" || "$TARGET" == "core" || "$TARGET" == "pos" ]]; then
  echo '──────── S2 非首次（个人版 config.ini 里先埋一个"用户改过"的哨兵段）────────' \
    | tee -a "$OUT/rc-summary.txt"
  SENTINEL_KEY=keep_me
  SENTINEL_VAL=12345
  printf '\n[cfgiso_probe]\n%s  = %s\n' "$SENTINEL_KEY" "$SENTINEL_VAL" >> "$PERSONAL/config.ini"
  /usr/bin/grep -nE "^[[:space:]]*${SENTINEL_KEY}[[:space:]]*=" "$PERSONAL/config.ini" \
    > "$OUT/s2-sentinel-before.txt"
  manifest "$OUT/orig-manifest-before-s2.txt" "$ORIG"
  run_suite "$OUT/configcheck-second.txt" env STELQUICK_CONFIG_CHECK=1 "$BIN"
  rc=$?
  manifest "$OUT/orig-manifest-after-s2.txt" "$ORIG"

  line=$(judge_line "$OUT/configcheck-second.txt"); verd=$(verdict_line "$OUT/configcheck-second.txt")
  reds=$(red_ids "$OUT/configcheck-second.txt")
  mig=$(/usr/bin/grep -E "^CONFIGISO: migration=" "$OUT/configcheck-second.txt" | LC_ALL=C /usr/bin/tail -1)

  judge $([[ $rc -eq 0 ]] && echo 0 || echo 1) "S2 rc=0（实得 $rc）"
  judge $([[ "$line" == *"判据 8/8"* ]] && echo 0 || echo 1) "S2 判据 8/8（$line）"
  judge $([[ "$verd" == *"VERDICT=PASS"* ]] && echo 0 || echo 1) "S2 VERDICT=PASS（$verd）"
  judge $([[ -z "$reds" ]] && echo 0 || echo 1) "S2 红项集合为空（实得 [$reds]）"
  judge $([[ "$mig" == *"migration=skipped"* ]] && echo 0 || echo 1) "S2 引导期确实跳过了播种（$mig）"

  /usr/bin/diff "$OUT/orig-manifest-before-s2.txt" "$OUT/orig-manifest-after-s2.txt" \
    > "$OUT/orig-manifest-diff-s2.txt" 2>&1
  judge $([[ -s "$OUT/orig-manifest-diff-s2.txt" ]] && echo 1 || echo 0) \
    "S2 原目录清单跑前==跑后（差异行数 $(/usr/bin/wc -l < "$OUT/orig-manifest-diff-s2.txt" | tr -d ' ')）"

  # "不覆盖"的外层判据：哨兵必须原样还在（值也必须对 —— 只查键名会漏"被重置"）
  /usr/bin/grep -nE "^[[:space:]]*${SENTINEL_KEY}[[:space:]]*=[[:space:]]*${SENTINEL_VAL}[[:space:]]*$" \
    "$PERSONAL/config.ini" > "$OUT/s2-sentinel-after.txt"
  judge $([[ -s "$OUT/s2-sentinel-after.txt" ]] && echo 0 || echo 1) \
    "S2 用户改过的值未被覆盖（哨兵 ${SENTINEL_KEY}=${SENTINEL_VAL} 仍在）"
  echo "  · S2 判据全文（见 $OUT/configcheck-second.txt）" | tee -a "$OUT/rc-summary.txt"
  /usr/bin/grep -E "^CONFIGISO: " "$OUT/configcheck-second.txt" | sed 's/^/      /' \
    | tee -a "$OUT/rc-summary.txt"
fi

# ══ ③ 负控（替身用户目录；真实原目录零风险）═════════════════════════════════
if [[ "$TARGET" == "all" || "$TARGET" == "negctl" ]]; then
  echo "──────── 准备替身用户目录（真实原目录的拷贝）────────" | tee -a "$OUT/rc-summary.txt"
  rm_fake_all
  /bin/cp -R "$ORIG" "$FAKE"
  echo "  替身原目录=$FAKE（$(/usr/bin/find "$FAKE" -type f | /usr/bin/wc -l | tr -d ' ') 个文件）" \
    | tee -a "$OUT/rc-summary.txt"

  # ── 负控 A：关隔离 ⇒ 行为回到修复前（写穿替身原目录）──────────────────────
  run_suite "$OUT/negctl-A-isolate-off.txt" env STEL_USERDIR="$FAKE" \
    STELQUICK_CFG_ISOLATE_OFF=1 STELQUICK_CONFIG_CHECK=1 "$BIN"
  rc=$?
  line=$(judge_line "$OUT/negctl-A-isolate-off.txt"); reds=$(red_ids "$OUT/negctl-A-isolate-off.txt")
  EXPECT_A="CFG-01,CFG-02,CFG-03,CFG-04,CFG-06,CFG-07"
  judge $([[ $rc -eq 10 ]] && echo 0 || echo 1) "负控A rc=10（实得 $rc）"
  judge $([[ "$line" == *"判据 2/8"* ]] && echo 0 || echo 1) "负控A 判据 2/8（$line）"
  judge $([[ "$reds" == "$EXPECT_A" ]] && echo 0 || echo 1) \
    "负控A 红项**恰好** [$EXPECT_A]（实得 [$reds]）"
  echo "  负控A 的写穿是**预期**结果，撞在替身上；读数：" | tee -a "$OUT/rc-summary.txt"
  /usr/bin/grep -E "^CONFIGISO: " "$OUT/negctl-A-isolate-off.txt" | sed 's/^/      /' \
    | tee -a "$OUT/rc-summary.txt"
  /usr/bin/grep -E "^CONFIGCHECK: +✗ " "$OUT/negctl-A-isolate-off.txt" | sed 's/^/      /' \
    | tee -a "$OUT/rc-summary.txt"

  # ── 负控 B：关播种（隔离在）⇒ 个人版目录空着 ⇒ 配置种子/数据种子两条腿断 ──
  /bin/rm -rf -- "$FAKE_PERSONAL"
  run_suite "$OUT/negctl-B-migrate-off.txt" env STEL_USERDIR="$FAKE" \
    STELQUICK_CFG_MIGRATE_OFF=1 STELQUICK_CONFIG_CHECK=1 "$BIN"
  rc=$?
  line=$(judge_line "$OUT/negctl-B-migrate-off.txt"); reds=$(red_ids "$OUT/negctl-B-migrate-off.txt")
  EXPECT_B="CFG-05,CFG-08"
  judge $([[ $rc -eq 10 ]] && echo 0 || echo 1) "负控B rc=10（实得 $rc）"
  judge $([[ "$line" == *"判据 6/8"* ]] && echo 0 || echo 1) "负控B 判据 6/8（$line）"
  judge $([[ "$reds" == "$EXPECT_B" ]] && echo 0 || echo 1) \
    "负控B 红项**恰好** [$EXPECT_B]（实得 [$reds]）"
  echo "  负控B 读数：" | tee -a "$OUT/rc-summary.txt"
  /usr/bin/grep -E "^CONFIGISO: " "$OUT/negctl-B-migrate-off.txt" | sed 's/^/      /' \
    | tee -a "$OUT/rc-summary.txt"
  /usr/bin/grep -E "^CONFIGCHECK: +✗ " "$OUT/negctl-B-migrate-off.txt" | sed 's/^/      /' \
    | tee -a "$OUT/rc-summary.txt"

  # A/B 红项集必须**两两不同**（各自承重；照 T35 的纪律）
  judge $([[ "$EXPECT_A" != "$EXPECT_B" ]] && echo 0 || echo 1) \
    "两组负控红项集合两两不同（A=[$EXPECT_A] B=[$EXPECT_B]）"

  rm_fake_all
  echo "  替身目录已清理" | tee -a "$OUT/rc-summary.txt"
fi

# ══ ④ 相邻回归（十一套件 + INTERACTCHECK + S3 + A2 + DYN 双路）══════════════
if [[ "$TARGET" == "all" || "$TARGET" == "core" || "$TARGET" == "regress" ]]; then
  # 先做一次**暖机**：播种只在首启发生，别把一次性拷贝成本算进回归读数。
  if [[ "$TARGET" == "regress" ]]; then
    run_suite "$OUT/regression-warmup.txt" env STELQUICK_CONFIG_CHECK=1 "$BIN"
    echo "regression-warmup rc=$? （首启播种成本一次性摊掉）" | tee -a "$OUT/rc-summary.txt"
  fi
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
    "STELQUICK_LOC_CHECK=1:locationcheck" \
    "STELQUICK_TOOL_CHECK=1:toolbarcheck" \
    "STELQUICK_TIMELINK_CHECK=1:timelinkcheck"
  do
    var=${spec%%:*}; name=${spec##*:}
    run_suite "$OUT/regression-$name.txt" env $var "$BIN"
    rc=$?
    echo "regression-$name rc=$rc" | tee -a "$OUT/rc-summary.txt"
    [[ $rc -eq 0 ]] || FAILED=1
  done

  # INTERACTCHECK：依赖窗口**真的拿到系统焦点**（血泪第 17 条），单独处理
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

  # A2 逐像素（Metal）
  run_suite "$OUT/regression-a2-metal.txt" env STELQUICK_A2_CHECK=1 "$BIN"
  rc=$?
  echo "regression-a2-metal rc=$rc" | tee -a "$OUT/rc-summary.txt"
  [[ $rc -eq 0 ]] || FAILED=1

  # DYN 双路
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
    echo "regression-dyn-$kind-metal $pass/$n PASS" | tee -a "$OUT/rc-summary.txt"
    [[ $pass -eq $n ]] || FAILED=1
  }
  run_dyn engine "$DYN_RUNS"
  run_dyn stub "$DYN_RUNS"
fi

# ══ ⑤ 收尾：原目录最终再核一遍（**合流形态**全程必须零改动）══════════════════
#   ⚠️ 范围限定"合流形态"：本脚本跑过的 stelQuickUI 全部套件（S1/S2/12 套件/
#   INTERACT/A2/DYN）**都不许**碰原目录。旧宿主 S3 不在此列 —— 见 ⑥ 的说明。
if [[ "$TARGET" == "all" || "$TARGET" == "core" || "$TARGET" == "pos" ]]; then
  manifest "$OUT/orig-manifest-final.txt" "$ORIG"
  /usr/bin/diff "$OUT/orig-manifest-before-s1.txt" "$OUT/orig-manifest-final.txt" \
    > "$OUT/orig-manifest-diff-final.txt" 2>&1
  judge $([[ -s "$OUT/orig-manifest-diff-final.txt" ]] && echo 1 || echo 0) \
    "收尾：合流形态全程原目录零改动（差异行数 $(/usr/bin/wc -l < "$OUT/orig-manifest-diff-final.txt" | tr -d ' ')）"
fi

# ══ ⑥ 旧宿主 S3 单跑（**刻意排在 ⑤ 之后**）═══════════════════════════════════
#   为什么单独一格、而且放在"零改动"核对之后：
#   旧形态宿主 `build-release/src/stellarium`（QWidget）**不走** LiveSkyRuntime 的
#   隔离引导 —— 它就是"原版 Stellarium"的等价物。它写原版用户目录是**它应有的
#   行为**（那是它自己的配置），A-1.0 要求隔离的是**个人版 UI**。
#   实测证据：把 S3 混在 ④ 段里时，⑤ 会因这 4 个文件（config.ini / log.txt /
#   modules/Oculars/ocular.ini / output.txt）报"原目录被改动" —— 那是判据范围
#   划错的**假红**，不是产品缺陷。故拆开：S3 单独跑，只验 rc（陷阱 37：真跑，
#   不看 md5），不参与"零改动"核对。
if [[ "$TARGET" == "all" || "$TARGET" == "core" || "$TARGET" == "regress" ]]; then
  STELA3_CHECK=1 ./build-release/src/stellarium > "$OUT/regression-s3-stela3.txt" 2>&1
  rc=$?
  echo "regression-s3-stela3 rc=$rc（旧形态；写原版目录属预期，不计入零改动）" \
    | tee -a "$OUT/rc-summary.txt"
  [[ $rc -eq 0 ]] || FAILED=1

  # S3 对原目录的影响**如实记录**（不作判据，只作证据：证明"隔离只覆盖合流形态"）
  manifest "$OUT/orig-manifest-after-s3.txt" "$ORIG"
  /usr/bin/diff "$OUT/orig-manifest-final.txt" "$OUT/orig-manifest-after-s3.txt" \
    > "$OUT/orig-manifest-diff-s3.txt" 2>&1
  echo "  · S3 对原目录的改动（预期非空，仅留证）：$(/usr/bin/wc -l < "$OUT/orig-manifest-diff-s3.txt" | tr -d ' ') 行" \
    | tee -a "$OUT/rc-summary.txt"
fi

# ── 汇总 ───────────────────────────────────────────────────────────────────
{
  echo "════════════════════════════════════════"
  echo "脚本层判据：$SCRIPT_PASS 通过 / $SCRIPT_FAIL 失败"
  env_note
} | tee -a "$OUT/rc-summary.txt"

[[ $FAILED -eq 0 && $SCRIPT_FAIL -eq 0 ]] && echo "SCRIPT-RC=0 FAILED=0" || echo "SCRIPT-RC=1 FAILED=1"
exit $(( FAILED || SCRIPT_FAIL ? 1 : 0 ))
