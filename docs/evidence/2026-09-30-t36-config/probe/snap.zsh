#!/bin/zsh
# 配置目录快照器（只读）：把 "<原版用户目录>" 每个文件的 path/size/mtime/md5 落盘。
# 用法：snap.zsh <tag>
set -u
D="$HOME/Library/Application Support/Stellarium"
OUT=/tmp/cfgprobe
mkdir -p "$OUT"
TAG=$1
: > "$OUT/$TAG.manifest"
/usr/bin/find "$D" -type f -print | LC_ALL=C /usr/bin/sort | while IFS= read -r f; do
  rel=${f#$D/}
  sz=$(/usr/bin/stat -f '%z' "$f")
  mt=$(/usr/bin/stat -f '%m' "$f")
  h=$(/sbin/md5 -q "$f")
  printf '%s|%s|%s|%s\n' "$rel" "$sz" "$mt" "$h" >> "$OUT/$TAG.manifest"
done
cp "$D/config.ini" "$OUT/$TAG.config.ini"
[ -f "$D/log.txt" ] && cp "$D/log.txt" "$OUT/$TAG.log.txt"
echo "snapshot[$TAG] files=$(/usr/bin/wc -l < "$OUT/$TAG.manifest")"
