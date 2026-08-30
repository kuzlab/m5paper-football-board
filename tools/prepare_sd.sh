#!/usr/bin/env bash
# microSD に必要なファイルを配置する。
#
#   ./tools/prepare_sd.sh /Volumes/M5PAPER
#
# 引数を省略すると /Volumes 配下のボリュームを一覧表示する。
set -euo pipefail

cd "$(dirname "$0")/.."

if [ $# -lt 1 ]; then
  echo "使い方: $0 <microSD のマウントポイント>"
  echo
  echo "マウント中のボリューム:"
  ls -1 /Volumes/ 2>/dev/null | sed 's/^/  \/Volumes\//'
  exit 1
fi

DEST="$1"
[ -d "$DEST" ] || { echo "エラー: $DEST が存在しません" >&2; exit 1; }

missing=0
for f in sd/config.json sd/competitions.json sd/ca.pem \
         sd/fonts/board_20.vlw sd/fonts/board_28.vlw sd/fonts/OFL.txt; do
  if [ ! -f "$f" ]; then
    echo "不足: $f" >&2
    missing=1
  fi
done
if [ "$missing" -ne 0 ]; then
  cat >&2 <<'EOM'

用意する手順:
  config.json  cp sd/config.example.json sd/config.json && $EDITOR sd/config.json
  ca.pem       ./tools/fetch_ca.sh
  *.vlw        python3 tools/make_vlw.py --font <JP.ttf> --font <Latin.ttf> --out sd/fonts
EOM
  exit 1
fi

mkdir -p "$DEST/fonts" "$DEST/cache" "$DEST/logs"
cp sd/config.json        "$DEST/config.json"
cp sd/competitions.json  "$DEST/competitions.json"
cp sd/ca.pem             "$DEST/ca.pem"
cp sd/fonts/board_20.vlw "$DEST/fonts/"
cp sd/fonts/board_28.vlw "$DEST/fonts/"
cp sd/fonts/OFL.txt      "$DEST/fonts/"
sync

echo "配置しました:"
find "$DEST" -type f -not -name '._*' -not -name '.DS_Store' \
  | sed "s|^$DEST|  |" | sort
echo
echo "アンマウントしてから M5Paper に挿してください:"
echo "  diskutil unmount \"$DEST\""
