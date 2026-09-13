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
  float low_battery_volt = 3.30f; // これを下回ったら通信しない (§6.5)
  int max_consecutive_failures = 5;  // 超えたら起床間隔を延ばす (§8.1)
  int log_retention_days = 14;    // 古いログを消す (§8.2)

  // レート制限 (§2.5)。日次上限は無い。
  int min_request_interval_ms = 7000;  // リクエスト間の最低ウェイト
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

}  // namespace fb
