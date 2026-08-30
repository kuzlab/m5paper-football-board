# M5Paper 単体サッカー結果掲示板

欧州サッカー8競技の直近結果を、1日1回の自動更新で e-paper に表示する掲示板。
**サーバーも MQTT ブローカーも使わず、M5Paper 単体で完結する。**

仕様書: [`SPEC_m5paper_standalone_football.md`](SPEC_m5paper_standalone_football.md)

```
┌────────────────────────────────────────────────────────────┐
│ 更新 08/29 07:00   26.4℃ 58%  72%     更新 → 側面ボタン      │
├────────────────────────────────────────────────────────────┤
│ ■ Premier League                                           │
│   Arsenal            2 - 1  Chelsea          開幕3連勝       │
│   Liverpool          0 - 3  Everton          番狂わせ        │
│ ■ Bundesliga                                               │
│   Bayern München     4 - 0  Union Berlin     首位           │
│ ■ FA Cup                                                   │
│   Bromley            2 - 1  Arsenal          大金星          │
│                                          ほか 6試合          │
└────────────────────────────────────────────────────────────┘
```

---

## 動作に必要なもの

- M5Paper (v1.1)
- microSD カード — **必ず FAT32**。後述の注意を参照
- API-FOOTBALL (API-SPORTS) のアカウント — 無料プランで動く
- Wi-Fi (2.4GHz)

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
$EDITOR sd/config.json      # wifi_ssid / wifi_password / apisports_key を入れる
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

### 3. 日本語フォントを作る

SIL OFL のフォントを用意して、必要な文字だけに絞った VLW を生成します。

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
charset: 391 glyphs
  wrote sd/fonts/board_20.vlw: 391 glyphs (98 from fallback fonts),  82 KB
  wrote sd/fonts/board_28.vlw: 391 glyphs (98 from fallback fonts), 144 KB
```

`--strict` は1文字でも欠けたら失敗します。欠字したまま気づかず焼くのを防ぐため、
**常に付けることを推奨します。**

収録される文字:

- ASCII 全域
- **Latin-1 Supplement** — `Mönchengladbach` `Alavés`
- **Latin Extended-A** — `Beşiktaş` `Kraków` `Ferencváros`
- `src/core/messages.cpp` から自動抽出した日本語

> **`src/core/messages.cpp` の文言テンプレートを変更したら、`make_vlw.py` を
> 再実行すること。** 文字リストはこのファイルから自動抽出されるため、
> 新しい漢字を追加してフォントを作り直さないと `?` になります。

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
native  test_layout  PASSED
native  test_season  PASSED
native  test_budget  PASSED
native  test_facts   PASSED
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

### タッチは使えない

電源が落ちている間 GT911 は反応しません。画面にソフトボタンを描いても
タップでは何も起きません。**更新は電源ボタンを押して起動する操作に
割り当てています。** 起床理由は BM8563 のアラームフラグ (AF) を読んで
判別します — GPIO では判別できません。

---

## 設計上の判断

仕様書からの変更点と、その理由です。

### 自動更新の枠を予約する (SPEC §2.3 の修正)

日次カウンタは **UTC 00:00 = JST 09:00** にリセットされますが、自動更新は
**JST 07:00 = 前日 UTC 22:00** に走ります。つまり各 UTC 日の末尾に1回の
自動更新が入る構造です。

素直に「1日5回まで」とすると、**前日の日中に手動更新を使い切った時点で
翌朝の自動更新が弾かれます。** そこで:

- 今日の自動更新がまだ走っていない間は、`auto_reserve_requests` (既定13)
  を手動更新から隠す
- 自動更新が済んだら予約を解放する
- カウンタはフェッチ回数ではなく**リクエスト数**で持つ (1フェッチが8〜13と
  変動するため)
- API が返す `x-ratelimit-requests-remaining` をローカルカウンタより優先する

`test/test_budget/` がこの挙動を検証します。

### 毎分レート制限を守る (SPEC §2.4 / §6.5 の修正)

API-SPORTS の無料プランには 100 req/日 に加えて **10 req/分** の制限があります。
13 リクエストを一気に流すと 11 発目で 429 を食らいます。

`net::RatePacer` が投入間隔を制御するため、**通信フェーズは最大 90 秒程度**
かかります。仕様書の「起床から電源断まで20秒以内」はこの制限とは両立しません。

電池への影響は軽微です (90秒 × 実効80mA ≒ 2mAh/日、1150mAh に対して 0.2%/日)。
keep-alive は引き続き有効で、TLS ハンドシェイクは1回に抑えています。

> **マイルストーン6で実測すること。** 毎分制限が実際には無い/緩ければ
> `max_requests_per_minute` を上げて元の20秒目標に戻せます。

### カップ戦のジャイアントキリング (SPEC §4.1 の追補)

FA Cup / DFB-Pokal / Copa del Rey には順位表が存在しないため、`form` 由来の
ファクトも順位帯も算出できません。代わりに、**保持している国内リーグの順位表を
チームIDの参照テーブルとして使い**、追加リクエストなしで格下の勝利を検出します。

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

### リーグ ID は `competitions.json` に同梱

仕様書は `/leagues` で解決してキャッシュせよとしていますが、既知の ID を
`competitions.json` に書いてあるので**通常は通信が発生しません。** ID が
欠けている競技会だけ `/leagues` で解決します。

> 同梱の ID は API-FOOTBALL の `/leagues` で必ず検証してください。

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
│   │   ├── season.h/.cpp          #   シーズン年の判定 (§2.2)
│   │   ├── budget.h/.cpp          #   日次リクエスト予算 (§2.3)
│   │   ├── facts.h/.cpp           #   ファクト算出 (§4・純粋関数)
│   │   ├── messages.h/.cpp        #   文言テンプレート (§4.2)
│   │   ├── selector.h/.cpp        #   選抜と溢れ処理 (§5.2)
│   │   ├── text_util.h/.cpp       #   UTF-8 切り詰め・グリフ畳み込み (§5.3)
│   │   └── config_parse.h/.cpp    #   JSON パース (§7.1)
│   ├── storage.h/.cpp             # NVS / microSD
│   ├── net_http.h/.cpp            # HTTPS keep-alive・chunked・レート制御 (§2.4)
│   ├── api_football.h/.cpp        # 取得とフィルタパース (§2, §3)
│   ├── render.h/.cpp              # 描画 (§5)
│   ├── power.h/.cpp               # RTCアラームと電源断 (§6.2)
│   ├── sht30.h/.cpp               # 温湿度 (§6.1)
│   └── logging.h/.cpp             # microSD ログ (§8.2)
├── test/
│   ├── test_facts/                # form / 順位帯の境界 / ジャイアントキリング
│   ├── test_layout/               # 切り詰め / 溢れ / グリフ畳み込み
│   ├── test_season/               # 年末年始の境界
│   └── test_budget/               # 自動更新枠の予約
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
07:00:21Z INFO +18402ms fixtures premier_league: 6 matches (heap 142880)
...
07:01:33Z INFO +90118ms fetch: 11 req, 0 http err, 0 parse err, 23 matches, paced 60100ms
07:01:33Z INFO +90140ms api daily remaining: 78
07:01:36Z INFO +93002ms render full: 11 rows, 6 overflow, 7 facts
07:01:37Z INFO +94210ms done in 94210ms, heap=138112, psram=3801088
```

**API キーと Wi-Fi パスワードは自動でマスクされます** (`log::register_secret`)。
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

- [ ] `form` 文字列の向き — 末尾が最新かどうか。違っていたら
      `config.json` の `thresholds.form_latest_at_end` を `false` にする
- [ ] API-FOOTBALL のフィールド名 (`src/api_football.cpp` のフィルタ)
- [ ] `competitions.json` のリーグ ID
- [ ] 毎分レート制限の実際の値 (**マイルストーン6で必ず計測**)
- [ ] BM8563 のアラームレジスタ挙動と、**RTC アラーム起動時に AF が
      立つこと** (決定事項2の前提。ボタン起動時に立たないことは確認済み)
- [ ] EPD モード名 (`m5gfx::epd_mode_t`) と部分書き換えの実測時間

---

## ライセンス

MIT (`LICENSE`)。第三者の成果物と帰属表示については [`NOTICE.md`](NOTICE.md)
を参照してください。**API-FOOTBALL の無料プランには表示条件が付く場合が
あるので、公開前に必ず規約を読んでください。**
