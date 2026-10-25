// config.json / competitions.json のパース (§7.1, §4.1)。
// SD アクセスと分離してあるので、この層はネイティブでテストできる。
#pragma once

#include <string>
#include <vector>

#include "core/facts.h"
#include "core/model.h"

namespace fb {

struct AppConfig {
  std::string wifi_ssid;
  std::string wifi_password;
  std::string football_data_token;

  // 自動更新の現地時刻 (§6.4)。既定 07:00 JST。
  int daily_wake_hour = 7;
  int daily_wake_min = 0;
  int tz_offset_min = 540;  // JST = UTC+9

  int min_refresh_sec = 600;      // 手動更新のクールダウン (§6.4)
  int display_window_hours = 72;  // キックオフがこれより古い試合は出さない
  int form_window_days = 45;      // 結果列の集計範囲 (§2.3a)
  // 初めて完了を確認してからこの時間内の試合だけを新着として出す。
  // キックオフではなく「結果が届いた時刻」から数える (core/freshness.h)。
  int fresh_hours = 20;
  // これを下回ったら通信しない (§6.5)。M5Unified の残量表示は
  // (mV-3300)/8 % なので、3.30V = 0% だった。残量0%まで粘ってから
  // 諦める設定になっていて、実機は 2026-09-22 の起床でブラウンアウトし
  // そのまま電池を使い切った。18% 相当で止める。
  float low_battery_volt = 3.45f;
  // これを下回ったら画面に「充電してください」を出す (まだ更新は続ける)。
  // 電池が尽きると RTC も時刻とアラームを失い、人が触るまで復帰しない。
  float warn_battery_volt = 3.70f;
  int max_consecutive_failures = 5;  // 超えたら起床間隔を延ばす (§8.1)
  int log_retention_days = 14;    // 古いログを消す (§8.2)

  // レート制限 (§2.5)。日次上限は無い。
  // 直近60秒に許すリクエスト数 (無料枠 10 req/min に1本余裕を残す)。
  // 旧 min_request_interval_ms (固定7秒間隔) は廃止した。7本しか投げない
  // のに42秒待っていて、その間 Wi-Fi を最大電力で握っていた。
  int max_requests_per_minute = 9;
  int max_requests_per_wake = 12;      // リトライ暴走の防止

  FactThresholds thresholds;

  bool valid() const {
    return !wifi_ssid.empty() && !football_data_token.empty();
  }
};

// 戻り値: 成功したか。err に人間可読な理由を入れる。
bool parse_config(const char* json, std::size_t len, AppConfig& out,
                  std::string& err);

// competitions.json をパースする。zones は配列順を保つこと (§4.1 決定事項5)。
bool parse_competitions(const char* json, std::size_t len,
                        std::vector<Competition>& out, std::string& err);

// standings_prev.json (§2.5)。欠損は差分なしとして扱い、エラーにしない。
// comps を使って key から comp_index を解決する。未知の key は捨てる。
bool parse_standings_cache(const char* json, std::size_t len,
                           const std::vector<Competition>& comps,
                           std::vector<LeagueStandings>& out);
std::string serialize_standings_cache(const std::vector<LeagueStandings>& in,
                                      const std::vector<Competition>& comps);

// 今回取得できたリーグだけを prev に差し替える。順位表は「新しい結果が
// あったリーグ」の分しか取得しないため、丸ごと置き換えると次にそのリーグの
// 結果が出た日に前回順位が無く、順位変動の事実が出なくなる
// (2026-10-25 の実機で premier_league だけ取得した回に bundesliga と
// laliga の前回順位が消えていた)。空の順位表では上書きしない。
void merge_standings_cache(std::vector<LeagueStandings>& prev,
                           const std::vector<LeagueStandings>& fresh);

}  // namespace fb
