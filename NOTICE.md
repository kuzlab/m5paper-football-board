# NOTICE

このリポジトリのコードは MIT ライセンス (`LICENSE`) です。以下は同梱・依存する
第三者の成果物と、公開前に確認が必要な事項です。

## API-FOOTBALL (API-SPORTS)

試合結果と順位表は [API-FOOTBALL](https://www.api-football.com/) から取得します。

> **公開前に必ず利用規約を読むこと。**
> 無料プランには帰属表示 (attribution) の条件が付く場合があります。表示が
> 必要な場合、固定エリアまたは README に指定された文言を入れる必要があります。
> 本リポジトリはその判断を行っていません。利用者の責任で確認してください。

- 規約: https://www.api-football.com/terms
- ダッシュボード: https://dashboard.api-football.com/

API キーは microSD の `config.json` から実行時に読み込みます。ファームウェアや
リポジトリには含まれません (SPEC §7.1)。

## フォント

日本語表示には **SIL Open Font License 1.1** のフォントを使います
(Noto Sans JP などを想定)。

- **OFL 全文を `sd/fonts/OFL.txt` として同梱すること。**
- `tools/make_vlw.py` が生成する VLW はサブセット化された派生物です。
  OFL の条件により、**派生フォントのファイル名に元の書体名をそのまま使わない
  こと。** 本ツールの既定の出力名は `board_20.vlw` / `board_28.vlw` です。
- フォントバイナリはリポジトリにコミットしません (`.gitignore`)。各自が
  ライセンスを確認したうえで生成してください。

## ライブラリ

| ライブラリ | ライセンス | 用途 |
|---|---|---|
| [M5Unified](https://github.com/m5stack/M5Unified) | MIT | M5Paper のペリフェラル抽象化 |
| [M5GFX](https://github.com/m5stack/M5GFX) | FreeBSD (2-clause BSD) | EPD 描画・VLW フォント |
| [ArduinoJson](https://arduinojson.org/) | MIT | フィルタ付きストリーミングパース |
| [Unity](https://github.com/ThrowTheSwitch/Unity) | MIT | ネイティブ単体テスト |
| Arduino core for ESP32 | LGPL-2.1 / Apache-2.0 | フレームワーク |

各ライブラリのライセンス全文は PlatformIO が `.pio/libdeps/` に取得した
ソースツリーに含まれます。

## TLS ルート証明書

`sd/ca.pem` は各自が `tools/fetch_ca.sh` で取得します。証明書自体は
リポジトリに含めません。
