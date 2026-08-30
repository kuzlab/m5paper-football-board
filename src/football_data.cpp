#include "football_data.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <esp_heap_caps.h>

#include <cstdio>
#include <cstring>

#include "core/datetime.h"
#include "logging.h"

namespace fb {
namespace api {
namespace {

// PSRAM を使うアロケータ (§3.2)。45日分の試合一覧は1競技で数十KBあるので
// 内部 RAM で受けると断片化で落ちる。
struct PsramAllocator : ArduinoJson::Allocator {
  void* allocate(std::size_t n) override {
    void* p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM);
    return p ? p : malloc(n);  // PSRAM が無い個体でも動かす
  }
  void deallocate(void* p) override { heap_caps_free(p); }
  void* reallocate(void* p, std::size_t n) override {
    void* q = heap_caps_realloc(p, n, MALLOC_CAP_SPIRAM);
    return q ? q : realloc(p, n);
  }
};

PsramAllocator g_alloc;

// "2026-08-29T14:00:00Z" を UTC epoch に。失敗したら 0。
std::time_t parse_iso8601_utc(const char* s) {
  if (!s) return 0;
  int y = 0, mo = 0, d = 0, h = 0, mi = 0, sec = 0;
  if (sscanf(s, "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &sec) < 5) return 0;
  return make_utc(y, mo, d, h, mi, sec);
}

// shortName を優先し、無ければ name にフォールバックする (§3.2)。
// "Borussia Mönchengladbach" より "M'gladbach" のほうが画面幅に収まる。
std::string team_name(JsonObjectConst team) {
  const char* s = team["shortName"];
  if (s && *s) return s;
  const char* n = team["name"];
  return n ? n : "";
}

// HTTP ステータスの共通処理。続行してよければ true。
bool status_ok(const net::Response& res, const char* what, FetchStats& stats) {
  if (res.status == 200) return true;
  ++stats.http_errors;
  if (res.status == 429) {
    stats.rate_limited = true;
    LOGE("%s: 429 rate limited (retry after %ds)", what, res.retry_after_sec);
  } else if (res.status == 401 || res.status == 403) {
    // 403 は「トークンは有効だがこの競技会は契約外」でも返る。
    stats.auth_error = (res.status == 401);
    stats.forbidden_comp = (res.status == 403);
    LOGE("%s: HTTP %d — token invalid or competition not in your plan", what,
         res.status);
  } else {
    LOGW("%s: HTTP %d", what, res.status);
  }
  return false;
}

void note_headers(const net::Response& res, FetchStats& stats) {
  if (res.minute_remaining >= 0) stats.minute_remaining = res.minute_remaining;
}

}  // namespace

std::size_t psram_free() {
  return heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
}

bool fetch_matches(net::KeepAliveClient& http, net::RatePacer& pacer,
                   const AppConfig& cfg, const Competition& comp,
                   int comp_index, std::time_t now_utc,
                   std::vector<Match>& out, FetchStats& stats) {
  if (comp.code.empty()) return false;

  // 45日窓。シーズン全体を取ると数百試合になり起床時間を圧迫する (§2.3a)。
  const std::time_t from_t =
      now_utc - static_cast<std::time_t>(cfg.form_window_days) * 86400;
  char path[192];
  snprintf(path, sizeof(path),
           "/v4/competitions/%s/matches?status=FINISHED&dateFrom=%s&dateTo=%s",
           comp.code.c_str(), utc_date_string(from_t).c_str(),
           utc_date_string(now_utc).c_str());

  pacer.wait_turn();
  net::Response res;
  net::BodyStream body;
  ++stats.requests;
  if (!http.request(path, cfg.football_data_token.c_str(), res, body)) {
    ++stats.http_errors;
    LOGW("matches %s: request failed", comp.key.c_str());
    return false;
  }
  note_headers(res, stats);
  if (!status_ok(res, comp.key.c_str(), stats)) {
    http.finish(body, res);
    return false;
  }

  // フィルタ (§3.2)。フィールド名は実レスポンスで必ず検証すること。
  JsonDocument filter(&g_alloc);
  JsonObject m = filter["matches"].add<JsonObject>();
  m["id"] = true;
  m["utcDate"] = true;
  m["status"] = true;
  m["homeTeam"]["id"] = true;
  m["homeTeam"]["name"] = true;
  m["homeTeam"]["shortName"] = true;
  m["awayTeam"]["id"] = true;
  m["awayTeam"]["name"] = true;
  m["awayTeam"]["shortName"] = true;
  m["score"]["fullTime"]["home"] = true;
  m["score"]["fullTime"]["away"] = true;

  JsonDocument doc(&g_alloc);
  const DeserializationError err =
      deserializeJson(doc, body, DeserializationOption::Filter(filter));
  http.finish(body, res);

  if (err) {
    ++stats.parse_errors;
    LOGE("matches %s: parse %s", comp.key.c_str(), err.c_str());
    return false;  // 1競技の失敗で他を止めない (§3.2)
  }

  int added = 0;
  for (JsonObjectConst o : doc["matches"].as<JsonArrayConst>()) {
    Match mt;
    mt.comp_index = comp_index;
    mt.fixture_id = o["id"] | 0L;
    mt.kickoff_utc = parse_iso8601_utc(o["utcDate"]);
    const char* st = o["status"];
    mt.status = st ? st : "";
    mt.home_id = o["homeTeam"]["id"] | 0;
    mt.away_id = o["awayTeam"]["id"] | 0;
    mt.home_name = team_name(o["homeTeam"]);
    mt.away_name = team_name(o["awayTeam"]);
    // 中止・延期の試合はスコアが null で返る。
    JsonVariantConst ft = o["score"]["fullTime"];
    mt.home_goals = ft["home"].isNull() ? -1 : ft["home"].as<int>();
    mt.away_goals = ft["away"].isNull() ? -1 : ft["away"].as<int>();

    if (!mt.finished() || !mt.has_score()) continue;
    if (mt.home_name.empty() || mt.away_name.empty()) continue;
    out.push_back(mt);
    ++added;
  }
  stats.matches += added;
  LOGI("matches %s: %d in %dd window (heap %u)", comp.key.c_str(), added,
       cfg.form_window_days, (unsigned)ESP.getFreeHeap());
  return true;
}

bool fetch_standings(net::KeepAliveClient& http, net::RatePacer& pacer,
                     const AppConfig& cfg, const Competition& comp,
                     int comp_index, LeagueStandings& out, FetchStats& stats) {
  if (comp.code.empty() || !comp.has_standings) return false;

  char path[96];
  snprintf(path, sizeof(path), "/v4/competitions/%s/standings",
           comp.code.c_str());

  pacer.wait_turn();
  net::Response res;
  net::BodyStream body;
  ++stats.requests;
  if (!http.request(path, cfg.football_data_token.c_str(), res, body)) {
    ++stats.http_errors;
    return false;
  }
  note_headers(res, stats);
  if (!status_ok(res, comp.key.c_str(), stats)) {
    http.finish(body, res);
    return false;
  }

  JsonDocument filter(&g_alloc);
  JsonObject g = filter["standings"].add<JsonObject>();
  g["type"] = true;   // TOTAL / HOME / AWAY。TOTAL 以外は捨てる
  g["stage"] = true;
  JsonObject r = g["table"].add<JsonObject>();
  r["position"] = true;
  r["points"] = true;
  r["playedGames"] = true;
  r["form"] = true;  // 参考値。判定には使わない (§2.4)
  r["team"]["id"] = true;
  r["team"]["name"] = true;
  r["team"]["shortName"] = true;

  JsonDocument doc(&g_alloc);
  const DeserializationError err =
      deserializeJson(doc, body, DeserializationOption::Filter(filter));
  http.finish(body, res);

  if (err) {
    ++stats.parse_errors;
    LOGE("standings %s: parse %s", comp.key.c_str(), err.c_str());
    return false;
  }

  out.comp_index = comp_index;
  out.rows.clear();
  bool saw_form = false;
  for (JsonObjectConst grp : doc["standings"].as<JsonArrayConst>()) {
    const char* type = grp["type"];
    // HOME / AWAY のテーブルも同じ配列に入ってくる。混ぜると順位が壊れる。
    if (!type || strcmp(type, "TOTAL") != 0) continue;
    for (JsonObjectConst row : grp["table"].as<JsonArrayConst>()) {
      StandingRow sr;
      sr.rank = row["position"] | 0;
      sr.points = row["points"] | 0;
      sr.played = row["playedGames"] | 0;
      const char* f = row["form"];
      sr.api_form = f ? f : "";
      if (!sr.api_form.empty()) saw_form = true;
      sr.team_id = row["team"]["id"] | 0;
      sr.team_name = team_name(row["team"]);
      if (sr.team_id > 0 && sr.rank > 0) out.rows.push_back(sr);
    }
  }
  stats.standings_rows += static_cast<int>(out.rows.size());
  // 無料枠で form が来るかどうかを実機で確認するためのログ (§2.4)。
  // 来ていても判定には使わない。
  LOGI("standings %s: %d rows, api form present=%d", comp.key.c_str(),
       (int)out.rows.size(), (int)saw_form);
  return !out.rows.empty();
}

}  // namespace api
}  // namespace fb
