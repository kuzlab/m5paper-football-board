# M5Paper 単体サッカー結果掲示板

欧州サッカー4競技（CL / プレミア / ブンデス / ラ・リーガ）の直近結果を、
1日1回の自動更新で e-paper に表示する掲示板。
**サーバーも MQTT ブローカーも使わず、M5Paper 単体で完結する。**

仕様書: [`SPEC_m5paper_standalone_football.md`](SPEC_m5paper_standalone_football.md)

```
┌────────────────────────────────────────────────────────────┐
│ Updated 08/30 07:05   25.1℃ 69%  100%   Refresh: side button│
├────────────────────────────────────────────────────────────┤
│ ■ Premier League                                           │
│   Arsenal            2 - 1  Chelsea       Won all 3         │
│   Liverpool          0 - 3  Everton       Upset             │
│ ■ Bundesliga                                               │
│   M'gladbach         4 - 0  Union Berlin  3 wins in a row   │
│ ■ LaLiga                                                   │
│   Alavés             0 - 2  Girona        Down into Releg…  │
│                                              +6 more        │
└────────────────────────────────────────────────────────────┘
```

---

## 動作に必要なもの

- M5Paper (v1.1)
- microSD カード — **必ず FAT32**。後述の注意を参照
- [football-data.org](https://www.football-data.org/client/register) のトークン
  — 氏名とメールのみで取得でき、無料枠で動く
- Wi-Fi (2.4GHz)

## 対象競技

| 優先度 | 競技会 | コード |
|---|---|---|
| 1 | UEFA Champions League | `CL` |
| 2 | Premier League | `PL` |
| 3 | Bundesliga | `BL1` |
| 4 | LaLiga | `PD` |

**Europa League / FA Cup / DFB-Pokal / Copa del Rey は対象外です。**
football-data.org の無料枠（12コンペティション）に含まれず、**有料プランに
上げても解決しません**（€12/月のプランでも競技数は12のまま）。カップ戦を
安価に取得する現実的な手段が無いため、4競技で確定としています。

その帰結として、**カップ戦のジャイアントキリング判定（`大金星` / `格上撃破`）は
出荷構成では発火しません。** `competitions.json` はデータ駆動なので、上位プランで
カップ戦を追加すれば動作します。判定ロジックとテストは残してあります。

---

## セットアップ

### 1. microSD を用意する

```
microSD 直下/
├── config.json          # sd/config.example.json をコピーして編集
├── competitions.json    # sd/competitions.json をそのままコピー
├── ca.pem               # tools/fetch_ca.sh で生成
├── fonts/
│   ├── board_20.vlw     # tools/make_vlw.py で生成
│   ├── board_28.vlw
│   └── OFL.txt          # 使用したフォントの OFL 全文
├── cache/               # 自動生成
└── logs/                # 自動生成
```

> **exFAT のカードは使えません。** ESP32 の FatFs は FAT16/FAT32 しか
> 読めませんが、macOS は 64GB 以上のカードを既定で exFAT にします。
> exFAT のまま挿すと起動ログに
> `f_mount failed: (13) There is no valid FAT volume` が出ます。
>
> ```bash
> diskutil list external                                  # ディスク番号を確認
> diskutil eraseDisk FAT32 M5PAPER MBRFormat /dev/diskN   # 中身は全消去される
> ```
>
> 32GB 以下のカードなら確実です。なお、カード自体と通信できていない場合は
> 別のエラー (`Card Failed! cmd: 0x00` / `(3) The physical drive cannot work`)
> になるので、切り分けの目印になります。

**`config.json` はリポジトリにコミットしないこと。** Wi-Fi パスワードと API
キーが入ります。`.gitignore` に登録済みです。

```bash
cp sd/config.example.json sd/config.json
$EDITOR sd/config.json      # wifi_ssid / wifi_password / football_data_token を入れる
```

### 2. TLS ルート証明書を取る

`setInsecure()` は使わない設計なので、証明書が無いと通信しません。

```bash
./tools/fetch_ca.sh
# → sd/ca.pem ができる。microSD の直下に /ca.pem としてコピーする
```

TLS 接続に失敗するようになったら、証明書が更新された可能性があります。
このスクリプトを再実行して `ca.pem` を差し替えてください。**ファームウェアの
再書き込みは不要です。**

### 3. フォントを作る

SIL OFL のフォントを用意して、必要な文字だけに絞った VLW を生成します。

> **⚠️ VLW のグリフ表はコードポイント昇順でなければなりません。**
> M5GFX (LovyanGFX) は `std::lower_bound` で二分探索するため、順序が崩れた
> 位置より後ろのグリフが**一切引けなくなります**。画面から文字が消えても
> エラーは出ないので気づきにくい罠です。`make_vlw.py` はソートしたうえで
> 書き出し直前にも assert で検証します。

> **日本語フォント1本では足りません。** Noto Sans JP は Latin Extended-A を
> **98文字欠いており** (`ş` `č` `ğ` `ı` など)、`Beşiktaş` や `Crvena zvezda`
> が欠字します。`--font` を複数指定すると、先頭のフォントを優先しつつ
> グリフを持たない文字を後続のフォントから補います。

```bash
pip install freetype-py     # pip が失敗する場合は --index-url https://pypi.org/simple

curl -fsSLO "https://raw.githubusercontent.com/google/fonts/main/ofl/notosansjp/NotoSansJP%5Bwght%5D.ttf"
curl -fsSLO "https://raw.githubusercontent.com/google/fonts/main/ofl/notosans/NotoSans%5Bwdth,wght%5D.ttf"

python3 tools/make_vlw.py \
  --font "NotoSansJP[wght].ttf" \
  --font "NotoSans[wdth,wght].ttf" \
  --out sd/fonts --strict
```

```
charset: 321 glyphs
  wrote sd/fonts/board_20.vlw: 321 glyphs (98 from fallback fonts),  57 KB
  wrote sd/fonts/board_28.vlw: 321 glyphs (98 from fallback fonts), 100 KB
```

`--strict` は1文字でも欠けたら失敗します。欠字したまま気づかず焼くのを防ぐため、
**常に付けることを推奨します。**

収録される文字:

- ASCII 全域
- **Latin-1 Supplement** — `Mönchengladbach` `Alavés`
- **Latin Extended-A** — `Beşiktaş` `Kraków` `Ferencváros`
- `src/core/messages.cpp` から自動抽出した非 ASCII 文字（UI を日本語化した
  場合はその漢字・かなも自動で含まれます）

> **`src/core/messages.cpp` の文言テンプレートを変更したら、`make_vlw.py` を
> 再実行すること。** 文字リストはこのファイルから自動抽出されます。

Latin Extended-A の外側 (キリル文字など) は `fold_unsupported()` が `?` や
ASCII 近似に畳みます。豆腐や描画崩れにはなりません。

### 4. microSD に配置する

```bash
./tools/prepare_sd.sh /Volumes/<カード名>      # 引数なしで実行すると候補を一覧表示
diskutil unmount /Volumes/<カード名>
```

必要なファイルが揃っているかを確認してからコピーします。揃っていなければ
不足分と、その作り方を表示します。

### 5. 書き込む

```bash
pio run -e m5paper -t upload
pio device monitor
```

---

## 開発

### 単体テスト (ネイティブ実行)

ファクト算出・レイアウト・シーズン判定・予算はすべてデバイス非依存の
純粋関数 (`src/core/`) にしてあり、PC 上でテストできます。

```bash
pio test -e native
```

```
native  test_layout    PASSED
native  test_form      PASSED
native  test_datetime  PASSED
native  test_facts     PASSED
```

### 開発中に電源を切らせない

```ini
# platformio.ini の build_flags
-DDEV_NO_POWEROFF
```

電源断の代わりにログを出して 60 秒待ってから再起動します。

---

## ⚠️ 開発中に必ずハマる点

### USB 給電中は電源断が動作しない

M5Paper は ESP32 の deep sleep ではなく、**電源ラッチ回路で完全に電源を切ります。**
USB から給電されている間はラッチを切っても落ちない、あるいは即座に再起動します。

**「電源が切れない」ように見えるのは正常です。** バッテリー駆動でのみ本来の
動作をします。仕様書のマイルストーン11と12 (RTCアラーム / 物理ボタン) は
**必ずバッテリー駆動で検証してください。**

### 起床はコールドブートであり、スリープ復帰ではない

RAM も RTC メモリも保持されません。持ち越したい状態はすべて NVS または
microSD に書いています (`src/storage.cpp`)。

> **これは EPD のフレームバッファにも当てはまります。ここで実機の画面が
> 消えました。**
>
> e-paper のパネルには前回の絵が物理的に残りますが、こちら側の
> フレームバッファ (RAM) は空で始まります。この非対称を部分更新では
> 埋められません。「固定エリアだけ描いて押し出す」と、フレームバッファ上で
> 空白のままの本文領域がパネルに押し出され、**試合一覧が消えます。**
> `display(x, y, w, h)` で押し出す領域を限定しても救えませんでした。
>
> 対処として、**描画した画面を `/cache/last_plan.json` に保存し、起床時に
> 復元してフレームバッファ全体を組み立て直してから押し出します。**
> 起動ログの `last plan: restored (N rows)` で確認できます。

### タッチは使えない

電源が落ちている間 GT911 は反応しません。画面にソフトボタンを描いても
タップでは何も起きません。**更新は電源ボタンを押して起動する操作に
割り当てています。** 起床理由は BM8563 のアラームフラグ (AF) を読んで
判別します — GPIO では判別できません。

---

## 設計上の判断

仕様書からの変更点と、その理由です。

### 連勝・連敗は自前で計算する (SPEC §2.4)

football-data.org の無料枠では順位表の `form` に依存できません。代わりに
**45日分の試合一覧から、チームごとの結果列を自前で組み立てます**
(`src/core/form.cpp`)。

1リクエストで2つの用途を賄うのが要点です:

- 直近72時間ぶん → 画面表示
- 45日ぶん全体   → 連勝・連敗の集計

結果列は必ず**日付昇順（古い→新しい、末尾が最新）**に積みます。呼び出し側が
どの順で試合を渡しても同じ結果になるよう内部でソートしており、
`test/test_form/` がそれを固定しています。

順位表の `form` が来ているかどうかは起動ログに出しますが、**判定には使いません。**

### 表示言語は英語

チーム名・競技会名が原語表記なので、UI も英語で揃えています。表示文字列は
`src/core/messages.h/.cpp` と `sd/competitions.json` の順位帯名の2箇所に
集約してあります。

日本語化する場合はこの2箇所を書き換えて `make_vlw.py` を再実行すれば動きます
（文字リストは `messages.cpp` から自動抽出されるため、手作業は不要です）。

### 連勝は「首位」より優先する

「首位」は毎日同じ表示になりますが、「5連勝」は日々変わります。ハッシュ一致で
描画をスキップする設計 (§5.5) とも噛み合うので、`TOP_OF_TABLE` はストリークが
無いときの受け皿として優先度を下げてあります。

### レート制限 (SPEC §2.5)

無料枠は **10リクエスト/分、日次上限なし**。上限ぴったりを狙わず、リクエスト間に
**7秒のウェイト**を入れて約8.5 req/分に抑えます (`min_request_interval_ms`)。

4競技 × 2エンドポイント = 最大8リクエストなので、**通信フェーズは約56秒、
起床から電源断まで100秒程度**を見込みます。keep-alive はウェイト中も維持します。

429 を受けたら `Retry-After` / `X-RequestCounter-Reset` に従って待ち、
**無視して再送しません。**

> **日次上限が無いため、手動更新を何度押しても「今日の枠を使い切る」ことが
> ありません。** クールダウン (`min_refresh_sec`) は API 枠のためではなく
> 電池消費を抑えるために残しています。

### カップ戦のジャイアントキリング（休眠中）

出荷構成にカップ戦が無いため発火しませんが、ロジックは残してあります。
**保持している国内リーグの順位表をチームIDの参照テーブルとして使い**、
追加リクエストなしで格下の勝利を検出します。

| 勝者 | 敗者 | 判定 |
|---|---|---|
| 順位表に有 | 順位表に有 | 通常の `番狂わせ` (順位差 ≥ 8) |
| **順位表に無** | **順位表に有** | ジャイアントキリング |
| 順位表に無 | 順位表に無 | ファクトなし (下部同士) |
| 順位表に有 | 順位表に無 | ファクトなし (順当) |

文言は敗者の順位で段階化します: 4位以内→`大金星`、10位以内→`格上撃破`、
それ以外→`番狂わせ`。

**限界:** Championship のクラブと5部のクラブは、追加リクエストなしには
区別できません。「何部相手か」は表示しません。

順位の引き当ては**国内リーグを CL/EL より優先**します。CL の順位表にも
プレミア勢が載っており、「36チーム中3位」を国内順位と取り違えると
`番狂わせ` の意味が変わるためです。

### 順位帯は配列の先頭から評価する

`competitions.json` の `zones` は上から評価し、**最初にマッチした帯を採用**
します。`首位 (1-1)` と `CL圏 (1-5)` のように重なる帯があるため、
**狭い帯を先に書いてください。**

`negative: true` を付けた帯は「入りたくない帯」として扱われ、文言が
`降格圏転落` / `降格圏脱出` になります。付けない帯は `CL圏浮上` / `CL圏陥落`
です。

### 72時間ウィンドウの再掲

取得範囲が72時間あるため、同じ試合が最大3日連続で候補に上がります。
**既に表示した試合は除外せず、同じ競技会の中で優先度だけ下げます。**
試合が少ない日に画面が空になるのを避けるためです
(`sd/cache/seen_fixtures.json`)。

### 競技会コードは `competitions.json` に同梱

football-data.org はコード (`PL` / `BL1` / `PD` / `CL`) を URL パスに直接使うため、
ID 解決のリクエストが不要です。

### 連続失敗時のバックオフ

`max_consecutive_failures` (既定5) を超えたら、次回アラームを翌々日・
3日後と段階的に延ばして電池を温存します。成功でリセットされます。

### アラーム設定の検証

`M5Unified` の API は成否を返さないことがあるため、BM8563 のレジスタ
(`0x09`–`0x0C`, `0x01`) を**直接読み返して照合**しています。検証に通らない
場合は電源を切らず、画面に `RTC ALARM ERROR` を出して通電したまま待機します
(電源を切ると二度と起きないため)。

> **実機で M5Unified の `setAlarmIRQ` が使えるなら、そちらに差し替えて
> 構いません** (仕様書の判断保留事項)。読み返しによる検証は残してください。

---

## リポジトリ構成

```
├── platformio.ini
├── .github/workflows/build.yml    # native テスト + ファームウェアビルド + 秘密混入チェック
├── src/
│   ├── main.cpp                   # 起動シーケンス (§6.5)
│   ├── core/                      # ← デバイス非依存。ネイティブでテストする
│   │   ├── model.h/.cpp           #   データモデル・順位表プール
│   │   ├── datetime.h/.cpp        #   日付ユーティリティ (§2.3a)
│   │   ├── form.h/.cpp            #   結果列の自前集計 (§2.4)
│   │   ├── facts.h/.cpp           #   ファクト算出 (§4・純粋関数)
│   │   ├── messages.h/.cpp        #   文言テンプレート (§4.2)
│   │   ├── selector.h/.cpp        #   選抜と溢れ処理 (§5.2)
│   │   ├── text_util.h/.cpp       #   UTF-8 切り詰め・グリフ畳み込み (§5.3)
│   │   └── config_parse.h/.cpp    #   JSON パース (§7.1)
│   ├── storage.h/.cpp             # NVS / microSD
│   ├── net_http.h/.cpp            # HTTPS keep-alive・chunked・レート制御 (§2.5-2.6)
│   ├── football_data.h/.cpp       # 取得とフィルタパース (§2, §3)
│   ├── render.h/.cpp              # 描画 (§5)
│   ├── power.h/.cpp               # RTCアラームと電源断 (§6.2)
│   ├── sht30.h/.cpp               # 温湿度 (§6.1)
│   └── logging.h/.cpp             # microSD ログ (§8.2)
├── test/
│   ├── test_facts/                # 結果列 / 順位帯の境界 / 優先度
│   ├── test_form/                 # 結果列の集計 (§2.4)
│   ├── test_layout/               # 切り詰め / 溢れ / グリフ畳み込み
│   └── test_datetime/             # 45日窓の年またぎ・UTC日境界
├── tools/
│   ├── make_vlw.py                # VLW サブセット生成 (§5.4)
│   └── fetch_ca.sh                # ルート CA 取得 (§2.4)
└── sd/
    ├── config.example.json
    └── competitions.json          # 順位帯定義 (§4.1)
```

---

## ログ

サーバーが無いのでログの置き場は microSD だけです (`/logs/YYYY-MM-DD.log`)。

```
07:00:03Z INFO +412ms  wake reason: RTC alarm (auto)
07:00:04Z INFO +1204ms config ok: 8 competitions, wake 07:00 local
07:00:12Z INFO +9310ms wifi ok in 5120ms, rssi=-58
07:00:21Z INFO +18402ms matches premier_league: 68 in 45d window (heap 142880)
...
07:01:33Z INFO +90118ms fetch: 8 req, 0 http err, 0 parse err, 241 matches in window, paced 49000ms, minute_remaining=2
07:01:34Z INFO +90140ms form table: 96 teams from 241 matches
07:01:34Z INFO +90200ms display window: 14 of 241 matches
07:01:36Z INFO +93002ms render full: 11 rows, 6 overflow, 7 facts
07:01:37Z INFO +94210ms done in 94210ms, heap=138112, psram=3801088
```

**API トークンと Wi-Fi パスワードは自動でマスクされます** (`log::register_secret`)。
古いログは `log_retention_days` (既定14日) を過ぎると削除されます。

---

## 未検証の項目

仕様書 §6.0 の一次情報と実機ログで確認します。

**実機で確認済み:**

- [x] **PSRAM 4,188,007 bytes free** — 起動ログの `psram=` で確認
- [x] **M5GFX の autodetect が `M5Paper` を正しく返す** — PlatformIO の
      board 定義が `m5stack-fire` でも機種判別に影響しない
- [x] **microSD の SPI ピン** — EPD とバスを共有し `SCK=14 / MISO=13 /
      MOSI=12 / CS=4`。デフォルトの VSPI では通信できない
- [x] **フォント欠如時のフォールバック** と `SD CONFIG ERROR` の異常系が
      想定どおり動作
- [x] **起床理由の判定** — リセット起動で `button (manual)` になる

**未確認:**

- [ ] football-data.org のフィールド名 (`src/football_data.cpp` のフィルタ)
- [ ] `shortName` が実際に短いか、null で返る競技会がないか (§3.2)
- [ ] 順位表に `form` が含まれるか (起動ログの `api form present=` を見る。
      **含まれていても判定には使わない**)
- [ ] 7秒間隔で 429 が出ないこと、通信フェーズの実測時間
      (**マイルストーン6で必ず計測**)
- [ ] BM8563 のアラームレジスタ挙動と、**RTC アラーム起動時に AF が
      立つこと** (決定事項2の前提。ボタン起動時に立たないことは確認済み)
- [ ] EPD モード名 (`m5gfx::epd_mode_t`) と部分書き換えの実測時間
- [ ] バッテリー駆動での電源断と RTC アラーム起床 (**マイルストーン11**)
- [ ] 電源ボタンによる手動更新 (**マイルストーン12**)
- [ ] 溢れ処理 (`+N more`)。開幕直後で試合数が足りず未発火

---

## ライセンス

MIT (`LICENSE`)。第三者の成果物と帰属表示については [`NOTICE.md`](NOTICE.md)
を参照してください。**football-data.org の帰属表示の要否は、公開前に必ず
規約を確認してください。**
