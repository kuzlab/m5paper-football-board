// 起動シーケンス (§6.5)。
//
// 電源投入（RTCアラーム または 電源ボタン）
//   → 起床理由の判定 → NVS/SD 読み出し → SHT30 → 「更新中…」部分書き換え
//   → 電池チェック → クールダウン/日次上限 → Wi-Fi → SNTP → HTTPS
//   → ファクト算出 → 選抜 → ハッシュ比較 → 描画 → 保存 → アラーム → 電源断
#include <M5Unified.h>
#include <WiFi.h>

#include <algorithm>
#include <vector>

#include "api_football.h"
#include "core/budget.h"
#include "core/config_parse.h"
#include "core/facts.h"
#include "core/messages.h"
#include "core/season.h"
#include "core/selector.h"
#include "logging.h"
#include "net_http.h"
#include "power.h"
#include "render.h"
#include "sht30.h"
#include "storage.h"

using namespace fb;

namespace {

AppConfig g_cfg;
std::vector<Competition> g_comps;
BudgetState g_budget;
render::StatusBar g_bar;
std::string g_ca_pem;
unsigned long g_t0 = 0;

std::time_t rtc_now_utc() {
  // RTC は UTC で保持する。表示とアラームで tz_offset_min を足し引きする。
  const auto dt = M5.Rtc.getDateTime();
  if (dt.date.year < 2020) return 0;  // RTC 未設定
  return make_utc(dt.date.year, dt.date.month, dt.date.date, dt.time.hours,
                  dt.time.minutes, dt.time.seconds);
}

void rtc_write_utc(std::time_t t) {
  std::tm tmv{};
  gmtime_r(&t, &tmv);
  m5::rtc_datetime_t dt;
  dt.date.year = tmv.tm_year + 1900;
  dt.date.month = tmv.tm_mon + 1;
  dt.date.date = tmv.tm_mday;
  dt.date.weekDay = tmv.tm_wday;
  dt.time.hours = tmv.tm_hour;
  dt.time.minutes = tmv.tm_min;
  dt.time.seconds = tmv.tm_sec;
  M5.Rtc.setDateTime(dt);
}

std::string local_hhmm(std::time_t utc, int tz_offset_min) {
  const std::time_t local = utc + static_cast<std::time_t>(tz_offset_min) * 60;
  std::tm tmv{};
  gmtime_r(&local, &tmv);
  char buf[24];
  snprintf(buf, sizeof(buf), "%02d/%02d %02d:%02d", tmv.tm_mon + 1, tmv.tm_mday,
           tmv.tm_hour, tmv.tm_min);
  return std::string(buf);
}

// 次回アラームを設定して電源を切る。アラーム設定に失敗したら切らない (§6.2)。
[[noreturn]] void finish_and_power_off(std::time_t now_utc) {
  int skip_days = 0;
  const int fails = storage::consecutive_failures();
  if (fails > g_cfg.max_consecutive_failures) {
    // 連続失敗が続くなら起床間隔を延ばして電池を温存する (§8.1)。
    skip_days = std::min(fails - g_cfg.max_consecutive_failures, 2);
    LOGW("backoff: %d consecutive failures -> skip %d day(s)", fails, skip_days);
  }

  const std::time_t alarm =
      power::next_daily_alarm_utc(now_utc, g_cfg.daily_wake_hour,
                                  g_cfg.daily_wake_min, g_cfg.tz_offset_min,
                                  skip_days);
  bool alarm_ok = power::set_alarm_utc(alarm);
  if (!alarm_ok) {
    // 1度だけ再試行する。それでも駄目なら電源を切らずに残す。
    delay(200);
    alarm_ok = power::set_alarm_utc(alarm);
  }

  log::prune(g_cfg.log_retention_days, now_utc);
  LOGI("done in %lums, heap=%u, psram=%u", millis() - g_t0,
       (unsigned)ESP.getFreeHeap(), (unsigned)api::psram_free());
  log::flush();

  render::sleep_display();

  if (!alarm_ok) {
    // 電源を切ると二度と起きない。通電したまま残して人が気づけるようにする。
    LOGE("ALARM SET FAILED - staying powered on so the device can still wake");
    log::flush();
    render::draw_fatal("RTC ALARM ERROR", "次回アラームを設定できませんでした",
                       "電源を切らずに待機します");
    while (true) delay(10000);
  }
  power::shutdown();
  while (true) delay(1000);
}

// SD / config.json が無い場合 (§7.1)。無言で失敗しない。
[[noreturn]] void fatal_config(const char* detail) {
  LOGE("config fatal: %s", detail);
  render::draw_fatal(msg::kSdConfigError, msg::kSdSetupHelp1,
                     msg::kSdSetupHelp2);
  log::flush();
  // 既定値でアラームだけ設定して落とす。人が SD を直せば翌朝復帰する。
  const std::time_t now = rtc_now_utc();
  if (now > 0 && power::set_alarm_utc(power::next_daily_alarm_utc(
                     now, g_cfg.daily_wake_hour, g_cfg.daily_wake_min,
                     g_cfg.tz_offset_min))) {
    render::sleep_display();
    power::shutdown();
  }
  while (true) delay(10000);
}

bool connect_wifi() {
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(g_cfg.wifi_ssid.c_str(), g_cfg.wifi_password.c_str());
  const unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - start > 20000) {  // タイムアウト20秒 (§6.5)
      LOGE("wifi timeout");
      return false;
    }
    delay(200);
  }
  LOGI("wifi ok in %lums, rssi=%d", millis() - start, WiFi.RSSI());
  return true;
}

std::time_t sync_time() {
  configTime(0, 0, "pool.ntp.org", "time.cloudflare.com");
  const unsigned long start = millis();
  std::time_t now = 0;
  while (millis() - start < 8000) {
    now = time(nullptr);
    if (now > 1700000000) break;
    delay(200);
  }
  if (now > 1700000000) {
    rtc_write_utc(now);
    log::set_time(now);
    LOGI("sntp ok");
    return now;
  }
  LOGW("sntp failed, keeping RTC time");
  return 0;
}

// 取得したデータからファクトを算出し、画面用の Entry を組む (§4, §5.2)。
std::vector<Entry> build_entries(const std::vector<Match>& matches,
                                 const StandingsPool& pool,
                                 const std::vector<long>& seen) {
  std::vector<Entry> out;
  out.reserve(matches.size());
  for (const auto& m : matches) {
    Entry e;
    e.match = m;
    e.fact = compute_fact(m, pool, g_comps, g_cfg.thresholds);
    e.seen = std::find(seen.begin(), seen.end(), m.fixture_id) != seen.end();
    out.push_back(e);
  }
  sort_entries(out, g_comps);
  return out;
}

}  // namespace

void setup() {
  g_t0 = millis();
  auto cfg = M5.config();
  M5.begin(cfg);
  Serial.begin(115200);

  storage::begin_nvs();
  const bool sd_ok = storage::begin_sd();

  std::time_t now = rtc_now_utc();
  log::begin(now > 0 ? now : 1767225600);  // RTC 未設定なら仮の日付
  LOGI("boot: sd=%d heap=%u psram=%u", (int)sd_ok, (unsigned)ESP.getFreeHeap(),
       (unsigned)api::psram_free());

  const power::WakeReason reason = power::wake_reason();
  const bool is_auto = (reason == power::WakeReason::kRtcAlarm);
  power::clear_alarm_flag();
  LOGI("wake reason: %s", is_auto ? "RTC alarm (auto)" : "button (manual)");

  render::begin();
  const bool fonts = render::load_fonts();

  // --- 設定の読み込み (§7.1) --------------------------------------------
  if (!sd_ok) fatal_config("SD not mounted");
  {
    std::string json;
    std::string err;
    if (!storage::read_file(storage::kConfigPath, json)) {
      fatal_config("config.json not readable");
    }
    if (!parse_config(json.c_str(), json.size(), g_cfg, err)) {
      fatal_config(err.c_str());
    }
    log::register_secret(g_cfg.apisports_key);
    log::register_secret(g_cfg.wifi_password);
  }
  {
    std::string json;
    std::string err;
    if (!storage::read_file(storage::kCompetitionsPath, json)) {
      fatal_config("competitions.json not readable");
    }
    if (!parse_competitions(json.c_str(), json.size(), g_comps, err)) {
      fatal_config(err.c_str());
    }
  }
  // ルート CA は SD に置く。setInsecure() は使わない (§2.4)。
  if (!storage::read_file("/ca.pem", g_ca_pem, 16384) || g_ca_pem.empty()) {
    fatal_config("/ca.pem not found (see README: tools/fetch_ca.sh)");
  }
  LOGI("config ok: %d competitions, wake %02d:%02d local",
       (int)g_comps.size(), g_cfg.daily_wake_hour, g_cfg.daily_wake_min);

  // --- 環境と固定エリア --------------------------------------------------
  const sht30::Reading env = sht30::measure();
  g_bar.env_ok = env.ok;
  g_bar.temperature_c = env.temperature_c;
  g_bar.humidity_pct = env.humidity_pct;
  g_bar.battery_pct = power::battery_percent();
  g_bar.font_error = !fonts;

  const std::time_t last_ok = storage::last_success_utc();
  g_bar.left = std::string(msg::kUpdating);
  // 押されたら通信前に「更新中…」を出す (§6.3)。
  render::draw_status_bar(g_bar);

  // --- 電池チェック ------------------------------------------------------
  const float vbat = power::battery_volt();
  if (vbat > 0.1f && vbat < g_cfg.low_battery_volt) {
    LOGW("battery low: %.2fV -> skip fetch", vbat);
    g_bar.left = std::string(msg::kLowBattery);
    render::draw_status_bar(g_bar);
    finish_and_power_off(now > 0 ? now : rtc_now_utc());
  }

  // --- クールダウンと日次上限 (§2.3, §6.4) -------------------------------
  storage::load_budget(g_budget);
  if (now > 0) roll_over(g_budget, now);

  const std::time_t last_fetch = storage::last_fetch_utc();
  if (!is_auto && now > 0 && last_fetch > 0 &&
      now - last_fetch < g_cfg.min_refresh_sec) {
    LOGI("cooldown: %lds since last fetch", (long)(now - last_fetch));
    g_bar.left = std::string(msg::kAlreadyFresh) + "  " +
                 local_hhmm(last_ok, g_cfg.tz_offset_min);
    render::draw_status_bar(g_bar);
    storage::save_budget(g_budget);
    finish_and_power_off(now);
  }

  // 1回の取得で投げたいリクエスト数を見積もる
  int want = 0;
  for (const auto& c : g_comps) {
    if (c.league_id > 0) ++want;             // fixtures
    if (c.has_standings) ++want;             // standings
  }
  const Allowance allowance = allow(g_budget, g_cfg.budget, is_auto, want);
  if (allowance.blocked) {
    LOGW("budget blocked: %s (used=%d manual=%d)", allowance.reason,
         g_budget.requests_used, g_budget.manual_fetches);
    g_bar.left = std::string(msg::kQuotaReached);
    render::draw_status_bar(g_bar);
    storage::save_budget(g_budget);
    finish_and_power_off(now);
  }

  // --- Wi-Fi / SNTP ------------------------------------------------------
  if (!connect_wifi()) {
    // 取得失敗時は前回の画面をそのまま残す (§8.1)。クリアしない。
    storage::set_consecutive_failures(storage::consecutive_failures() + 1);
    g_bar.left = std::string(msg::kFailedPrefix) + " " +
                 local_hhmm(last_ok, g_cfg.tz_offset_min);
    render::draw_status_bar(g_bar);
    storage::save_budget(g_budget);
    finish_and_power_off(now > 0 ? now : rtc_now_utc());
  }
  const std::time_t synced = sync_time();
  if (synced > 0) {
    now = synced;
    roll_over(g_budget, now);
  }

  // --- 取得 (§2.4 keep-alive) --------------------------------------------
  net::KeepAliveClient http;
  net::RatePacer pacer(g_cfg.budget.max_requests_per_minute);
  api::FetchStats stats;
  std::time_t ref = now;
  std::vector<Match> matches;
  std::vector<LeagueStandings> standings_now;
  bool any_success = false;

  if (!http.begin(api::kHost, api::kPort, g_ca_pem.c_str())) {
    LOGE("tls connect failed");
  } else {
    // デモモードでは基準時刻とシーズンを設定値で置き換える (無料プラン対策)
    int season = season_year_from_utc(now);
    ref = now;
    if (g_cfg.demo_mode()) {
      int y = 0, mo = 0, d = 0;
      if (sscanf(g_cfg.demo_date.c_str(), "%d-%d-%d", &y, &mo, &d) == 3) {
        ref = make_utc(y, mo, d, 23, 59, 59);
        season = g_cfg.demo_season;
        LOGW("DEMO MODE: season=%d, window ends %s", season,
             g_cfg.demo_date.c_str());
      } else {
        LOGE("demo_date is malformed: %s", g_cfg.demo_date.c_str());
      }
    }
    LOGI("season=%d, budget allows %d requests", season, allowance.requests);

    api::resolve_missing_league_ids(http, pacer, g_cfg, g_comps, season, stats);

    for (std::size_t i = 0; i < g_comps.size(); ++i) {
      if (stats.rate_limited) break;
      if (stats.requests >= allowance.requests) {
        LOGW("request allowance exhausted before %s", g_comps[i].key.c_str());
        break;
      }
      // 1競技のパースに失敗しても他の競技の処理を続行する (§3.2)
      if (api::fetch_fixtures(http, pacer, g_cfg, g_comps[i],
                              static_cast<int>(i), season, ref, matches,
                              stats)) {
        any_success = true;
      }
    }

    // 試合のあったリーグの standings だけを取る (§6.5)
    for (std::size_t i = 0; i < g_comps.size(); ++i) {
      if (stats.rate_limited) break;
      if (!g_comps[i].has_standings) continue;
      if (stats.requests >= allowance.requests) break;
      const bool played = std::any_of(
          matches.begin(), matches.end(),
          [i](const Match& m) { return m.comp_index == static_cast<int>(i); });
      if (!played) continue;
      LeagueStandings ls;
      if (api::fetch_standings(http, pacer, g_cfg, g_comps[i],
                               static_cast<int>(i), season, ls, stats)) {
        standings_now.push_back(ls);
      }
    }
    http.end();
  }

  record(g_budget, g_cfg.budget, is_auto, stats.requests, stats.daily_remaining);
  storage::save_budget(g_budget);
  storage::set_last_fetch_utc(now);
  LOGI("fetch: %d req, %d http err, %d api err, %d parse err, %d matches, "
       "paced %lums",
       stats.requests, stats.http_errors, stats.api_errors, stats.parse_errors,
       stats.matches, pacer.waited_ms());
  if (stats.daily_remaining >= 0) {
    LOGI("api daily remaining: %d", stats.daily_remaining);
  }

  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);

  if (!any_success || matches.empty()) {
    storage::set_consecutive_failures(storage::consecutive_failures() + 1);
    LOGW("no usable data -> keep previous screen");
    // プラン制限は設定で直せる問題なので、通信失敗と区別して表示する。
    g_bar.left = stats.plan_error
                     ? std::string(msg::kPlanError)
                     : std::string(msg::kFailedPrefix) + " " +
                           local_hhmm(last_ok, g_cfg.tz_offset_min);
    render::draw_status_bar(g_bar);
    finish_and_power_off(now);
  }

  // --- ファクト算出と選抜 -------------------------------------------------
  StandingsPool pool;
  pool.comps = &g_comps;
  pool.current = standings_now;
  storage::load_standings(g_comps, pool.previous);  // 欠損は差分なし (§2.5)
  // 今回 standings を取らなかったリーグは前回分を現在値として使う。
  // カップ戦のジャイアントキリング判定に順位表が要るため (§4.1)。
  for (const auto& prev : pool.previous) {
    const bool have = std::any_of(pool.current.begin(), pool.current.end(),
                                  [&prev](const LeagueStandings& c) {
                                    return c.comp_index == prev.comp_index;
                                  });
    if (!have) pool.current.push_back(prev);
  }

  std::vector<long> seen;
  storage::load_seen_fixtures(seen);
  const std::vector<Entry> entries = build_entries(matches, pool, seen);

  int with_fact = 0;
  for (const auto& e : entries) {
    if (e.fact.valid()) ++with_fact;
  }

  const RenderPlan plan =
      build_plan(entries, g_comps, LayoutMetrics(), render::measures());
  const std::uint32_t hash = plan_hash(plan);

  // --- 描画 (§5.5) -------------------------------------------------------
  const bool same = (hash == storage::last_plan_hash());
  if (same && !render::needs_ghost_clear()) {
    LOGI("content unchanged -> status bar only");
    g_bar.left = std::string(msg::kUpdatedPrefix) + " " +
                 local_hhmm(now, g_cfg.tz_offset_min);
    render::draw_status_bar(g_bar);
  } else {
    LOGI("render full: %d rows, %d overflow, %d facts",
         (int)plan.rows.size(), plan.overflow_count, with_fact);
    g_bar.left = std::string(msg::kUpdatedPrefix) + " " +
                 local_hhmm(now, g_cfg.tz_offset_min);
    render::draw_full(plan, g_bar);
    storage::set_last_plan_hash(hash);
  }

  // --- 保存 --------------------------------------------------------------
  if (!standings_now.empty()) {
    storage::save_standings(standings_now, g_comps);
  }
  for (const auto& e : entries) {
    if (std::find(seen.begin(), seen.end(), e.match.fixture_id) == seen.end()) {
      seen.push_back(e.match.fixture_id);
    }
  }
  storage::save_seen_fixtures(seen);
  storage::set_consecutive_failures(0);
  storage::set_last_success_utc(now);

  finish_and_power_off(now);
}

void loop() {
  // setup() が電源断で終わるので、ここには来ない。
  delay(1000);
}
