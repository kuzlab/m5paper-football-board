#!/usr/bin/env bash
# API-SPORTS のルート CA を取り出して sd/ca.pem に書く (SPEC §2.4)。
# setInsecure() は使わないので、この証明書が無いと通信しない。
#
# 証明書は失効・更新されるので、TLS 接続に失敗したらこのスクリプトを
# 再実行して sd/ca.pem を差し替えること。ファームウェアの再書き込みは不要。
set -euo pipefail

HOST="${1:-v3.football.api-sports.io}"
OUT="${2:-sd/ca.pem}"

echo "fetching certificate chain from ${HOST}:443 ..."
# チェーンの最後 = ルートに最も近い証明書。サーバはルート自体を送らないことが
# あるので、送られてきた中で最上位のものを保存し、検証できなければ
# OS のトラストストアから該当ルートを探して追記する。
openssl s_client -showcerts -servername "$HOST" -connect "$HOST:443" </dev/null 2>/dev/null \
  | awk '/-----BEGIN CERTIFICATE-----/,/-----END CERTIFICATE-----/' > /tmp/chain.pem

if [ ! -s /tmp/chain.pem ]; then
  echo "failed to fetch certificates" >&2
  exit 1
fi

# チェーン全体を入れておく。mbedTLS は連結された PEM を解釈できる。
mkdir -p "$(dirname "$OUT")"
cp /tmp/chain.pem "$OUT"

COUNT=$(grep -c 'BEGIN CERTIFICATE' "$OUT")
echo "wrote $OUT ($COUNT certificates)"
echo
echo "この後 sd/ca.pem を microSD の直下に /ca.pem としてコピーすること。"
