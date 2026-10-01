#!/bin/zsh
# T45-C 配置损坏形态注入器（P-CFG-01 "写入损坏的个人版配置文件 ⇒ 启动可恢复"）。
#
# 用法：
#   tools/a6-cfg-inject.sh <形态> <临时根目录> [personal|original]
#
# 作用：把指定形态的字节写到 <临时根目录>/Stellarium[-quick]/config.ini，并把这次注入的
#       **客观记录**写进 <临时根目录>/inject.env（`EXPECT_BYTES=` / `EXPECT_SHA256=`），
#       供调用方 `source` 后交给判据做**注入自证**（CH-01）。
#       默认落点 = `personal`（`<root>/Stellarium-quick`）；`original` 用于
#       `STELQUICK_CFG_ISOLATE_OFF` 负控（隔离关掉后产品用的是**原目录**）。
#
# 为什么这么做（而不是在产品里加 `STELQUICK_CFG_INJECT`）：
#   配合 `STEL_USERDIR=<临时根目录>/Stellarium`，个人版目录会被 `ConfigIsolation`
#   推出到 `<临时根目录>/Stellarium-quick` —— **临时目录**。于是：
#     · 真实路径、真实文件、真实引导路径（不是桩）；
#     · 真实用户的配置**一字节都不碰**；
#     · 出货二进制里**没有**"往用户配置写垃圾"的代码（不为了测试给产品加刀）。
#
# 形态表（与 ui/ConfigHealthCheck.hpp 的期望严重度表一一对应）：
#   none      不写（目录留空 ⇒ 产品兜底播种 data/default_cfg.ini）     期望 1 正常
#   full      拷一份完整的 data/default_cfg.ini（252 键）               期望 1 正常
#             ⚠️ 只用于 ISOLATE_OFF 负控：隔离关闭后产品用**原目录**，
#                那份必须是完整配置引擎才起得来（否则测到的是"没配置 ⇒ 崩"，
#                与"隔离关掉"这个被测命题不是一回事）
#   truncate  默认配置前 40%（99 键）                                   期望 3 错误（引导修复）
#   badline   一行垃圾 + 一个合法键（1 键）                             期望 3 错误
#   badutf8   合法行 + 值里混非法 UTF-8（1 键）                         期望 3 错误
#   binary    256 字节 0x00–0xFF 循环（0 键）                           期望 3 错误
#   empty     0 字节（0 键）                                            期望 3 错误
#   readonly  拷默认配置后 chmod 444                                    ⚠️ **不进形态矩阵**
#             （个人版配置不可写 ⇒ 引擎 `findFile(Writable|File)` 落空 ⇒ 退到**安装目录**
#               新建 `./config.ini` ⇒ 测到的不是"个人版只读"，还会污染仓库根与后续 run。
#               详见 docs/T45_CONFIG_SAFETY.zh_CN.md §6.5 残余 · 1）
#
# 纪律：本脚本只写 <临时根目录> 下的东西；调用方负责 `rm -rf` 那个临时根目录。

set -u

FORM="${1:-}"
ROOTDIR="${2:-}"
TARGET="${3:-personal}"
if [[ -z "$FORM" || -z "$ROOTDIR" ]]; then
  echo "用法：$0 <形态> <临时根目录> [personal|original]" >&2
  exit 2
fi
if [[ "$TARGET" != "personal" && "$TARGET" != "original" ]]; then
  echo "第三参数只认 personal / original，收到：$TARGET" >&2
  exit 2
fi

REPO="$(cd "$(dirname "$0")/.." && pwd)"
DEFAULT_CFG="$REPO/data/default_cfg.ini"
if [[ ! -f "$DEFAULT_CFG" ]]; then
  echo "找不到 $DEFAULT_CFG（必须在仓库根跑）" >&2
  exit 2
fi

USERDIR="$ROOTDIR/Stellarium"
PERSONAL="$USERDIR-quick"
/bin/rm -rf "$ROOTDIR"
/bin/mkdir -p "$USERDIR" "$PERSONAL"
if [[ "$TARGET" == "original" ]]; then
  DESTDIR="$USERDIR"
else
  DESTDIR="$PERSONAL"
fi
CFG="$DESTDIR/config.ini"
ENVF="$ROOTDIR/inject.env"
: > "$ENVF"

case "$FORM" in
  none)
    # 刻意**不放** config.ini：走产品自己的兜底播种腿。
    # 没有期望值 ⇒ 判据只要求文件存在（CH-01 的宽松分支）。
    ;;
  full)
    /bin/cp "$DEFAULT_CFG" "$CFG"
    ;;
  truncate)
    BYTES=$(( $(/usr/bin/stat -f%z "$DEFAULT_CFG") * 40 / 100 ))
    /usr/bin/head -c "$BYTES" "$DEFAULT_CFG" > "$CFG"
    ;;
  badline)
    {
      printf '@@@ this line is not a setting at all\n'
      printf '[main]\n'
      printf 'version                             = 26.1\n'
    } > "$CFG"
    ;;
  badutf8)
    {
      printf '[main]\n'
      printf 'version                             = '
      # 非法 UTF-8 序列（0xC3 后不接续字节 / 0xFF 单独出现）
      printf '\303\050'
      printf '\377\376\375'
      printf '\n'
    } > "$CFG"
    ;;
  binary)
    # 256 字节 0x00–0xFF 循环 —— 与 T45-A 探针 Q2(b) 完全同款。
    /usr/bin/python3 -c 'import sys; sys.stdout.buffer.write(bytes(range(256)))' > "$CFG"
    ;;
  empty)
    : > "$CFG"
    ;;
  readonly)
    /bin/cp "$DEFAULT_CFG" "$CFG"
    /bin/chmod 444 "$CFG"
    ;;
  *)
    echo "不认识的形态：$FORM" >&2
    exit 2
    ;;
esac

if [[ -f "$CFG" ]]; then
  EB="$(/usr/bin/stat -f%z "$CFG")"
  ES="$(/usr/bin/shasum -a 256 "$CFG" | /usr/bin/awk '{print $1}')"
  printf 'EXPECT_BYTES=%s\nEXPECT_SHA256=%s\n' "$EB" "$ES" > "$ENVF"
  printf '[注入] form=%-9s target=%-8s 文件=%s 字节=%s sha256=%s\n' \
    "$FORM" "$TARGET" "$CFG" "$EB" "${ES[0,16]}…"
else
  printf '[注入] form=%-9s target=%-8s 目录=%s（**不放** config.ini，走产品兜底播种）\n' \
    "$FORM" "$TARGET" "$DESTDIR"
fi
printf 'INJECT_FORM=%s\nINJECT_TARGET=%s\nINJECT_ROOT=%s\nINJECT_CFG=%s\n' \
  "$FORM" "$TARGET" "$ROOTDIR" "$CFG" >> "$ENVF"
