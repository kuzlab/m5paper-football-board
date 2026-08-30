# NOTICE

このリポジトリのコードは MIT ライセンス (`LICENSE`) です。以下は同梱・依存する
第三者の成果物と、公開前に確認が必要な事項です。

## football-data.org

試合結果と順位表は [football-data.org](https://www.football-data.org/) の
v4 API から取得します。

> **公開前に必ず利用規約を読むこと。**
> 帰属表示 (attribution) の要否は利用者の責任で確認してください。本リポジトリは
> その判断を行っていません。

- 規約: https://www.football-data.org/terms
- 登録: https://www.football-data.org/client/register

無料枠は12コンペティション、10リクエスト/分、日次上限なし、現行シーズン限定です。
本プロジェクトが対象とする4競技 (CL / PL / BL1 / PD) はこの枠に含まれます。

API トークンは microSD の `config.json` から実行時に読み込みます。ファームウェアや
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
