// API-FOOTBALL の取得とフィルタ付きストリーミングパース (§2, §3)。
#pragma once

#include <ctime>
#include <string>
#include <vector>

#include "core/config_parse.h"
#include "core/model.h"
#include "net_http.h"

namespace fb {
namespace api {

constexpr const char* kHost = "v3.football.api-sports.io";
constexpr std::uint16_t kPort = 443;

struct FetchStats {
  int requests = 0;
  int http_errors = 0;
  int parse_errors = 0;
  int matches = 0;
  int standings_rows = 0;
  int api_errors = 0;          // HTTP 200 で errors が返った回数
  bool plan_error = false;     // プランで許可されていない (シーズン/パラメータ)
  bool rate_limited = false;   // 429 を受けた (§2.3-4)
  int daily_remaining = -1;    // 最後に見たヘッダの値
  unsigned long paced_ms = 0;  // レート制限で待った時間
};

// ArduinoJson のアロケータを PSRAM に向ける (§3.2)。
// 空き容量はログに出して実機で確認すること。
void install_psram_allocator();
std::size_t psram_free();

// 直近 window_hours 以内に終了した試合を取る (§2.2)。
// 1競技の失敗は false を返すだけで、呼び出し側は他競技を続行する (§3.2)。
bool fetch_fixtures(net::KeepAliveClient& http, net::RatePacer& pacer,
                    const AppConfig& cfg, const Competition& comp,
                    int comp_index, int season, std::time_t now_utc,
                    std::vector<Match>& out, FetchStats& stats);

// standings は rank / team.id / team.name / form / points / played のみ抽出。
bool fetch_standings(net::KeepAliveClient& http, net::RatePacer& pacer,
                     const AppConfig& cfg, const Competition& comp,
                     int comp_index, int season, LeagueStandings& out,
                     FetchStats& stats);

// リーグ ID の解決 (§2.2)。competitions.json に league_id があれば通信しない。
// 欠けているものだけ /leagues で解決し、league_ids.json にキャッシュする。
bool resolve_missing_league_ids(net::KeepAliveClient& http,
                                net::RatePacer& pacer, const AppConfig& cfg,
                                std::vector<Competition>& comps, int season,
                                FetchStats& stats);

// "YYYY-MM-DD" (UTC)
std::string utc_date_string(std::time_t t);

}  // namespace api
}  // namespace fb
