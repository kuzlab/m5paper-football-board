#include "api_football.h"

#include <ArduinoJson.h>
#include <esp_heap_caps.h>

#include <cstdio>
#include <cstring>

#include "core/season.h"
#include "logging.h"

namespace fb {
namespace api {
namespace {

// PSRAM を使うアロケータ (§3.2)。fixtures のレスポンスは数十KBあるので
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

// ISO8601 "2026-08-29T18:30:00+00:00" を UTC epoch に。失敗したら 0。
std::time_t parse_iso8601_utc(const char* s) {
  if (!s) return 0;
  int y = 0, mo = 0, d = 0, h = 0, mi = 0, sec = 0;
  if (sscanf(s, "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &sec) < 5) return 0;
  std::time_t t = make_utc(y, mo, d, h, mi, sec);
  // タイムゾーンオフセット。timezone=UTC で要求するので通常は +00:00。
  const char* p = strchr(s, 'T');
  if (p) {
    const char* z = strpbrk(p, "+-");
    if (z) {
      int oh = 0, om = 0;
      if (sscanf(z + 1, "%d:%d", &oh, &om) == 2) {
        const int off = oh * 3600 + om * 60;
        t += (*z == '+') ? -off : off;
      }
    }
  }
  return t;
}

void note_rate_headers(const net::Response& res, FetchStats& stats) {
  if (res.daily_remaining >= 0) stats.daily_remaining = res.daily_remaining;
}

}  // namespace

void install_psram_allocator() { /* JsonDocument 生成時に渡す方式 */ }

std::size_t psram_free() {
  return heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
}

std::string utc_date_string(std::time_t t) {
  // gmtime_r は newlib にある。ロケール非依存。
  std::tm tmv{};
  gmtime_r(&t, &tmv);
  char buf[16];
  snprintf(buf, sizeof(buf), "%04d-%02d-%02d", tmv.tm_year + 1900,
           tmv.tm_mon + 1, tmv.tm_mday);
  return std::string(buf);
}

bool fetch_fixtures(net::KeepAliveClient& http, net::RatePacer& pacer,
                    const AppConfig& cfg, const Competition& comp,
                    int comp_index, int season, std::time_t now_utc,
                    std::vector<Match>& out, FetchStats& stats) {
  if (comp.league_id <= 0) return false;

  const std::time_t from_t = now_utc - cfg.fetch_window_hours * 3600;
  char path[224];
  snprintf(path, sizeof(path),
           "/fixtures?league=%d&season=%d&from=%s&to=%s&status=FT-AET-PEN"
           "&timezone=UTC",
           comp.league_id, season, utc_date_string(from_t).c_str(),
           utc_date_string(now_utc).c_str());

  pacer.wait_turn();
  net::Response res;
  net::BodyStream body;
  ++stats.requests;
  if (!http.request(path, cfg.apisports_key.c_str(), res, body)) {
    ++stats.http_errors;
    LOGW("fixtures %s: request failed", comp.key.c_str());
    return false;
  }
  note_rate_headers(res, stats);

  if (res.status == 429) {
    // 429 が返ったらその回の取得を即座に中止する (§2.3-4)
    stats.rate_limited = true;
    ++stats.http_errors;
    http.finish(body, res);
    LOGE("fixtures %s: 429 rate limited", comp.key.c_str());
    return false;
  }
  if (res.status != 200) {
    ++stats.http_errors;
    http.finish(body, res);
    LOGW("fixtures %s: HTTP %d", comp.key.c_str(), res.status);
    return false;
  }

  // フィルタ (§3.2)。フィールド名は実レスポンスで検証すること。
  JsonDocument filter(&g_alloc);
  JsonObject fx = filter["response"][0].to<JsonObject>();
  fx["fixture"]["id"] = true;
  fx["fixture"]["date"] = true;
  fx["fixture"]["status"]["short"] = true;
  fx["teams"]["home"]["id"] = true;
  fx["teams"]["home"]["name"] = true;
  fx["teams"]["away"]["id"] = true;
  fx["teams"]["away"]["name"] = true;
  fx["goals"]["home"] = true;
  fx["goals"]["away"] = true;

  JsonDocument doc(&g_alloc);
  const DeserializationError err =
      deserializeJson(doc, body, DeserializationOption::Filter(filter));
  http.finish(body, res);

  if (err) {
    ++stats.parse_errors;
    LOGE("fixtures %s: parse %s", comp.key.c_str(), err.c_str());
    return false;  // 1競技の失敗で他を止めない (§3.2)
  }

  const std::time_t cutoff = now_utc - cfg.fetch_window_hours * 3600;
  int added = 0;
  for (JsonObjectConst o : doc["response"].as<JsonArrayConst>()) {
    Match m;
    m.comp_index = comp_index;
    m.fixture_id = o["fixture"]["id"] | 0L;
    m.kickoff_utc = parse_iso8601_utc(o["fixture"]["date"]);
    const char* st = o["fixture"]["status"]["short"];
    m.status = st ? st : "";
    m.home_id = o["teams"]["home"]["id"] | 0;
    m.away_id = o["teams"]["away"]["id"] | 0;
    const char* hn = o["teams"]["home"]["name"];
    const char* an = o["teams"]["away"]["name"];
    m.home_name = hn ? hn : "";
    m.away_name = an ? an : "";
    // goals は未消化試合で null になる。null を -1 として扱う。
    m.home_goals = o["goals"]["home"].isNull() ? -1 : o["goals"]["home"].as<int>();
    m.away_goals = o["goals"]["away"].isNull() ? -1 : o["goals"]["away"].as<int>();

    if (!m.finished() || !m.has_score()) continue;
    if (m.kickoff_utc > 0 && m.kickoff_utc < cutoff) continue;  // 72時間の外
    if (m.home_name.empty() || m.away_name.empty()) continue;
    out.push_back(m);
    ++added;
  }
  stats.matches += added;
  LOGI("fixtures %s: %d matches (heap %u)", comp.key.c_str(), added,
       (unsigned)ESP.getFreeHeap());
  return true;
}

bool fetch_standings(net::KeepAliveClient& http, net::RatePacer& pacer,
                     const AppConfig& cfg, const Competition& comp,
                     int comp_index, int season, LeagueStandings& out,
                     FetchStats& stats) {
  if (comp.league_id <= 0 || !comp.has_standings) return false;

  char path[96];
  snprintf(path, sizeof(path), "/standings?league=%d&season=%d", comp.league_id,
           season);

  pacer.wait_turn();
  net::Response res;
  net::BodyStream body;
  ++stats.requests;
  if (!http.request(path, cfg.apisports_key.c_str(), res, body)) {
    ++stats.http_errors;
    return false;
  }
  note_rate_headers(res, stats);

  if (res.status == 429) {
    stats.rate_limited = true;
    ++stats.http_errors;
    http.finish(body, res);
    return false;
  }
  if (res.status != 200) {
    ++stats.http_errors;
    http.finish(body, res);
    LOGW("standings %s: HTTP %d", comp.key.c_str(), res.status);
    return false;
  }

  // response[0].league.standings は「グループの配列の配列」。
  // CL/EL はリーグフェーズ中のみ存在し、ノックアウトに入ると空になる。
  JsonDocument filter(&g_alloc);
  JsonObject st =
      filter["response"][0]["league"]["standings"][0][0].to<JsonObject>();
  st["rank"] = true;
  st["points"] = true;
  st["form"] = true;
  st["team"]["id"] = true;
  st["team"]["name"] = true;
  st["all"]["played"] = true;

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
  JsonArrayConst groups =
      doc["response"][0]["league"]["standings"].as<JsonArrayConst>();
  for (JsonArrayConst group : groups) {
    for (JsonObjectConst r : group) {
      StandingRow row;
      row.rank = r["rank"] | 0;
      row.points = r["points"] | 0;
      const char* form = r["form"];
      row.form = form ? form : "";
      row.team_id = r["team"]["id"] | 0;
      const char* tn = r["team"]["name"];
      row.team_name = tn ? tn : "";
      row.played = r["all"]["played"] | 0;
      if (row.team_id > 0 && row.rank > 0) out.rows.push_back(row);
    }
  }
  stats.standings_rows += static_cast<int>(out.rows.size());
  LOGI("standings %s: %d rows", comp.key.c_str(), (int)out.rows.size());
  return !out.rows.empty();
}

bool resolve_missing_league_ids(net::KeepAliveClient& http,
                                net::RatePacer& pacer, const AppConfig& cfg,
                                std::vector<Competition>& comps, int season,
                                FetchStats& stats) {
  // competitions.json に league_id が入っていれば通信は発生しない (§2.2)。
  bool any_missing = false;
  for (const auto& c : comps) {
    if (c.league_id <= 0) any_missing = true;
  }
  if (!any_missing) return true;

  bool all_ok = true;
  for (auto& c : comps) {
    if (c.league_id > 0) continue;
    char path[128];
    snprintf(path, sizeof(path), "/leagues?season=%d&search=%s", season,
             c.key.c_str());

    pacer.wait_turn();
    net::Response res;
    net::BodyStream body;
    ++stats.requests;
    if (!http.request(path, cfg.apisports_key.c_str(), res, body)) {
      all_ok = false;
      continue;
    }
    note_rate_headers(res, stats);
    if (res.status != 200) {
      if (res.status == 429) stats.rate_limited = true;
      http.finish(body, res);
      all_ok = false;
      continue;
    }

    JsonDocument filter(&g_alloc);
    filter["response"][0]["league"]["id"] = true;
    filter["response"][0]["league"]["name"] = true;
    JsonDocument doc(&g_alloc);
    const DeserializationError err =
        deserializeJson(doc, body, DeserializationOption::Filter(filter));
    http.finish(body, res);
    if (err) {
      all_ok = false;
      continue;
    }
    const int id = doc["response"][0]["league"]["id"] | 0;
    if (id > 0) {
      c.league_id = id;
      LOGI("resolved league %s -> %d", c.key.c_str(), id);
    } else {
      all_ok = false;
    }
    if (stats.rate_limited) break;
  }
  return all_ok;
}

}  // namespace api
}  // namespace fb
