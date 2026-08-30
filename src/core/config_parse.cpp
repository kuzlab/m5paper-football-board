#include "core/config_parse.h"

#include <ArduinoJson.h>

#include <cstdio>
#include <cstdlib>

namespace fb {
namespace {

// "07:00" を時分に分解する。壊れていたら既定値を保つ。
bool parse_hhmm(const char* s, int* h, int* m) {
  if (!s) return false;
  int hh = 0, mm = 0;
  if (sscanf(s, "%d:%d", &hh, &mm) != 2) return false;
  if (hh < 0 || hh > 23 || mm < 0 || mm > 59) return false;
  *h = hh;
  *m = mm;
  return true;
}

template <typename T>
void assign_if(JsonVariantConst v, T& dst) {
  if (!v.isNull()) dst = v.as<T>();
}

void assign_str_if(JsonVariantConst v, std::string& dst) {
  if (!v.isNull() && v.is<const char*>()) {
    const char* s = v.as<const char*>();
    if (s) dst = s;
  }
}

}  // namespace

bool parse_config(const char* json, std::size_t len, AppConfig& out,
                  std::string& err) {
  JsonDocument doc;
  const DeserializationError e = deserializeJson(doc, json, len);
  if (e) {
    err = std::string("config.json parse error: ") + e.c_str();
    return false;
  }

  assign_str_if(doc["wifi_ssid"], out.wifi_ssid);
  assign_str_if(doc["wifi_password"], out.wifi_password);
  assign_str_if(doc["football_data_token"], out.football_data_token);

  if (!doc["daily_wake_local"].isNull()) {
    if (!parse_hhmm(doc["daily_wake_local"], &out.daily_wake_hour,
                    &out.daily_wake_min)) {
      err = "daily_wake_local must be \"HH:MM\"";
      return false;
    }
  }

  assign_if(doc["tz_offset_min"], out.tz_offset_min);
  assign_if(doc["min_refresh_sec"], out.min_refresh_sec);
  assign_if(doc["display_window_hours"], out.display_window_hours);
  assign_if(doc["form_window_days"], out.form_window_days);
  assign_if(doc["low_battery_volt"], out.low_battery_volt);
  assign_if(doc["max_consecutive_failures"], out.max_consecutive_failures);
  assign_if(doc["log_retention_days"], out.log_retention_days);

  // レート制限 (§2.5)
  assign_if(doc["min_request_interval_ms"], out.min_request_interval_ms);
  assign_if(doc["max_requests_per_wake"], out.max_requests_per_wake);

  // ファクト閾値 (§4.1)
  JsonVariantConst th = doc["thresholds"];
  if (!th.isNull()) {
    assign_if(th["win_streak_min"], out.thresholds.win_streak_min);
    assign_if(th["loss_streak_min"], out.thresholds.loss_streak_min);
    assign_if(th["unbeaten_min"], out.thresholds.unbeaten_min);
    assign_if(th["big_win_diff"], out.thresholds.big_win_diff);
    assign_if(th["upset_rank_gap"], out.thresholds.upset_rank_gap);
    assign_if(th["rank_change_min"], out.thresholds.rank_change_min);
    assign_if(th["form_latest_at_end"], out.thresholds.form_latest_at_end);
  }

  if (out.wifi_ssid.empty()) {
    err = "wifi_ssid is empty";
    return false;
  }
  if (out.football_data_token.empty()) {
    err = "football_data_token is empty";
    return false;
  }
  return true;
}

bool parse_competitions(const char* json, std::size_t len,
                        std::vector<Competition>& out, std::string& err) {
  JsonDocument doc;
  const DeserializationError e = deserializeJson(doc, json, len);
  if (e) {
    err = std::string("competitions.json parse error: ") + e.c_str();
    return false;
  }
  JsonArrayConst arr = doc["competitions"];
  if (arr.isNull()) {
    err = "competitions.json: \"competitions\" array not found";
    return false;
  }

  out.clear();
  for (JsonObjectConst o : arr) {
    Competition c;
    assign_str_if(o["key"], c.key);
    assign_str_if(o["display"], c.display);
    assign_str_if(o["code"], c.code);
    assign_if(o["priority"], c.priority);
    assign_if(o["has_standings"], c.has_standings);
    assign_if(o["is_cup"], c.is_cup);
    assign_if(o["is_domestic"], c.is_domestic);
    assign_if(o["total_teams"], c.total_teams);

    // zones は JSON の配列順をそのまま保つ。先頭から評価して最初に
    // マッチしたものを採用するため、狭い帯を先に書く運用 (決定事項5)。
    for (JsonObjectConst zo : o["zones"].as<JsonArrayConst>()) {
      Zone z;
      assign_str_if(zo["name"], z.name);
      assign_if(zo["from"], z.from);
      assign_if(zo["to"], z.to);
      assign_if(zo["negative"], z.negative);
      if (z.from > 0 && z.to >= z.from) c.zones.push_back(z);
    }

    if (c.key.empty() || c.code.empty()) continue;
    if (c.display.empty()) c.display = c.key;
    out.push_back(c);
  }

  if (out.empty()) {
    err = "competitions.json: no valid entries";
    return false;
  }
  return true;
}

bool parse_standings_cache(const char* json, std::size_t len,
                           const std::vector<Competition>& comps,
                           std::vector<LeagueStandings>& out) {
  out.clear();
  JsonDocument doc;
  if (deserializeJson(doc, json, len)) return false;  // 欠損・破損は差分なし扱い
  JsonArrayConst arr = doc["leagues"];
  if (arr.isNull()) return false;

  for (JsonObjectConst o : arr) {
    std::string key;
    assign_str_if(o["key"], key);
    int comp_index = -1;
    for (std::size_t i = 0; i < comps.size(); ++i) {
      if (comps[i].key == key) {
        comp_index = static_cast<int>(i);
        break;
      }
    }
    if (comp_index < 0) continue;  // competitions.json から消えた競技会は捨てる

    LeagueStandings ls;
    ls.comp_index = comp_index;
    for (JsonArrayConst row : o["rows"].as<JsonArrayConst>()) {
      // [team_id, rank, played, points, "form", "name"] の配列形式。
      // SD の書き込み量を抑えるため、キー名を持たせていない。
      if (row.size() < 5) continue;
      StandingRow r;
      r.team_id = row[0];
      r.rank = row[1];
      r.played = row[2];
      r.points = row[3];
      r.api_form = row[4].as<const char*>() ? row[4].as<const char*>() : "";
      if (row.size() >= 6 && row[5].as<const char*>())
        r.team_name = row[5].as<const char*>();
      ls.rows.push_back(r);
    }
    out.push_back(ls);
  }
  return !out.empty();
}

std::string serialize_standings_cache(const std::vector<LeagueStandings>& in,
                                      const std::vector<Competition>& comps) {
  JsonDocument doc;
  JsonArray arr = doc["leagues"].to<JsonArray>();
  for (const auto& ls : in) {
    JsonObject o = arr.add<JsonObject>();
    o["key"] = (ls.comp_index >= 0 &&
                ls.comp_index < static_cast<int>(comps.size()))
                   ? comps[ls.comp_index].key.c_str()
                   : "";
    JsonArray rows = o["rows"].to<JsonArray>();
    for (const auto& r : ls.rows) {
      JsonArray a = rows.add<JsonArray>();
      a.add(r.team_id);
      a.add(r.rank);
      a.add(r.played);
      a.add(r.points);
      a.add(r.api_form);
      a.add(r.team_name);
    }
  }
  std::string out;
  serializeJson(doc, out);
  return out;
}

}  // namespace fb
