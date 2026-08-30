// config.json / competitions.json のパース (§7.1, §4.1)。
// SD アクセスと分離してあるので、この層はネイティブでテストできる。
#pragma once

#include <string>
#include <vector>

#include "core/budget.h"
#include "core/facts.h"
#include "core/model.h"

namespace fb {

struct AppConfig {
  std::string wifi_ssid;
  std::string wifi_password;
  std::string apisports_key;

  // 自動更新の現地時刻 (§6.4)。既定 07:00 JST。
  int daily_wake_hour = 7;
  int daily_wake_min = 0;
  int tz_offset_min = 540;  // JST = UTC+9

  int min_refresh_sec = 600;      // 手動更新のクールダウン (§6.4)
  int fetch_window_hours = 72;    // 取得範囲 (§2.2)
  float low_battery_volt = 3.30f; // これを下回ったら通信しない (§6.5)
  int max_consecutive_failures = 5;  // 超えたら起床間隔を延ばす (§8.1)
  int log_retention_days = 14;    // 古いログを消す (§8.2)

  // 開発用。API-FOOTBALL の無料プランは現行シーズンにアクセスできないため
  // (2022-2024 のみ)、過去シーズンのデータで動作確認するための設定。
  // 有料プランに切り替えたら両方とも外すこと。
  int demo_season = 0;        // 0 = 実時刻からシーズンを判定する
  std::string demo_date;      // "YYYY-MM-DD"。空なら実時刻を使う
  bool demo_mode() const { return demo_season > 0 && !demo_date.empty(); }

  BudgetPolicy budget;
  FactThresholds thresholds;

  bool valid() const {
    return !wifi_ssid.empty() && !apisports_key.empty();
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

// league_ids.json (§2.2)。season とセットで持ち、シーズンが変わったら再解決する。
struct LeagueIdCache {
  int season = 0;
  // key -> league_id
  std::vector<std::pair<std::string, int>> ids;
  int find(const std::string& key) const {
    for (const auto& kv : ids) {
      if (kv.first == key) return kv.second;
    }
    return 0;
  }
};
bool parse_league_ids(const char* json, std::size_t len, LeagueIdCache& out);
std::string serialize_league_ids(const LeagueIdCache& in);

}  // namespace fb
