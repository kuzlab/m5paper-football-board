// football-data.org v4 の取得とフィルタ付きストリーミングパース (§2, §3)。
#pragma once

#include <ctime>
#include <string>
#include <vector>

#include "core/config_parse.h"
#include "core/model.h"
#include "net_http.h"

namespace fb {
namespace api {

constexpr const char* kHost = "api.football-data.org";
constexpr std::uint16_t kPort = 443;

struct FetchStats {
  int requests = 0;
  int http_errors = 0;
  int parse_errors = 0;
  int matches = 0;          // 45日窓に入った試合の総数
  int standings_rows = 0;
  bool rate_limited = false;   // 429 を受けた (§2.5-3)
  bool auth_error = false;     // 401/403。トークンが違う
  bool forbidden_comp = false; // 無料枠に含まれない競技会
  int minute_remaining = -1;   // X-Requests-Available-Minute
  unsigned long paced_ms = 0;
};

std::size_t psram_free();

// 45日分の試合一覧 (§2.3a)。1リクエストで
//   - 直近 display_window_hours ぶん  → 画面表示
//   - 45日ぶん全体                    → 結果列の集計 (§2.4)
// の両方を賄う。out には窓内の全試合を入れ、表示の絞り込みは呼び出し側で行う。
bool fetch_matches(net::KeepAliveClient& http, net::RatePacer& pacer,
                   const AppConfig& cfg, const Competition& comp,
                   int comp_index, std::time_t now_utc,
                   std::vector<Match>& out, FetchStats& stats);

// 順位表 (§2.3b)。type == "TOTAL" のテーブルのみを採る。
bool fetch_standings(net::KeepAliveClient& http, net::RatePacer& pacer,
                     const AppConfig& cfg, const Competition& comp,
                     int comp_index, LeagueStandings& out, FetchStats& stats);

}  // namespace api
}  // namespace fb
