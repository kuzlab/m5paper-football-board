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

#include "core/config_parse.h"
#include "core/datetime.h"
#include "core/facts.h"
#include "core/form.h"
#include "core/messages.h"
#include "core/selector.h"
#include "football_data.h"
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
                                 const FormTable& forms,
                                 const std::vector<long>& seen) {
  std::vector<Entry> out;
  out.reserve(matches.size());
  for (const auto& m : matches) {
    Entry e;
    e.match = m;
    e.fact = compute_fact(m, pool, forms, g_comps, g_cfg.thresholds);
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
    log::register_secret(g_cfg.football_data_token);
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
  // ルート CA は SD に置く。setInsecure() は使わない (§2.6)。
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

  // --- クールダウン (§6.4) -----------------------------------------------
  // football-data.org には日次上限が無いので API 枠のための制限は不要。
  // クールダウンは電池消費を抑えるために残す (§2.5-5)。
  const std::time_t last_fetch = storage::last_fetch_utc();
  if (!is_auto && now > 0 && last_fetch > 0 &&
      now - last_fetch < g_cfg.min_refresh_sec) {
    LOGI("cooldown: %lds since last fetch", (long)(now - last_fetch));
    g_bar.left = std::string(msg::kAlreadyFresh) + "  " +
                 local_hhmm(last_ok, g_cfg.tz_offset_min);
    render::draw_status_bar(g_bar);
    finish_and_power_off(now);
  }

  // --- Wi-Fi / SNTP ------------------------------------------------------
  if (!connect_wifi()) {
    // 取得失敗時は前回の画面をそのまま残す (§8.1)。クリアしない。
    storage::set_consecutive_failures(storage::consecutive_failures() + 1);
    g_bar.left = std::string(msg::kFailedPrefix) + " " +
                 local_hhmm(last_ok, g_cfg.tz_offset_min);
    render::draw_status_bar(g_bar);
    finish_and_power_off(now > 0 ? now : rtc_now_utc());
  }
  const std::time_t synced = sync_time();
  if (synced > 0) now = synced;

  // --- 取得 (§2.3, keep-alive + 7秒間隔) ---------------------------------
  net::KeepAliveClient http;
  net::RatePacer pacer(g_cfg.min_request_interval_ms);
  api::FetchStats stats;
  // 45日窓の全試合。画面表示にはこの一部を使い、結果列の集計には全部を使う。
  std::vector<Match> window;
  std::vector<LeagueStandings> standings_now;
  bool any_success = false;

  if (!http.begin(api::kHost, api::kPort, g_ca_pem.c_str())) {
    LOGE("tls connect failed");
  } else {
    LOGI("fetching %d competitions, %dms between requests",
         (int)g_comps.size(), g_cfg.min_request_interval_ms);

    for (std::size_t i = 0; i < g_comps.size(); ++i) {
      if (stats.requests >= g_cfg.max_requests_per_wake) {
        LOGW("per-wake request cap reached before %s", g_comps[i].key.c_str());
        break;
      }
      // 1競技のパースに失敗しても他の競技の処理を続行する (§3.2)
      if (api::fetch_matches(http, pacer, g_cfg, g_comps[i],
                             static_cast<int>(i), now, window, stats)) {
        any_success = true;
      } else if (stats.rate_limited) {
        // サーバの指示ぶん待ってから次に進む。無視して再送しない (§2.5-3)。
        pacer.back_off(0);
        stats.rate_limited = false;
      }
    }

    // 新しい試合が検出された競技会だけ standings を取る (§2.3b)。
    // 試合がなければ順位は動かないのでキャッシュで足りる。
    for (std::size_t i = 0; i < g_comps.size(); ++i) {
      if (!g_comps[i].has_standings) continue;
      if (stats.requests >= g_cfg.max_requests_per_wake) break;
      const bool played = std::any_of(
          window.begin(), window.end(), [i, now, this_cfg = &g_cfg](const Match& m) {
            return m.comp_index == static_cast<int>(i) &&
                   m.kickoff_utc >=
                       now - static_cast<std::time_t>(
                                 this_cfg->display_window_hours) * 3600;
          });
      if (!played) continue;
      LeagueStandings ls;
      if (api::fetch_standings(http, pacer, g_cfg, g_comps[i],
                               static_cast<int>(i), ls, stats)) {
        standings_now.push_back(ls);
      } else if (stats.rate_limited) {
        pacer.back_off(0);
        stats.rate_limited = false;
      }
    }
    http.end();
  }

  storage::set_last_fetch_utc(now);
  LOGI("fetch: %d req, %d http err, %d parse err, %d matches in window, "
       "paced %lums, minute_remaining=%d",
       stats.requests, stats.http_errors, stats.parse_errors, stats.matches,
       pacer.waited_ms(), stats.minute_remaining);

  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);

  // 画面に載せるのは直近 display_window_hours ぶんだけ (§2.3a)。
  const std::time_t display_cutoff =
      now - static_cast<std::time_t>(g_cfg.display_window_hours) * 3600;
  std::vector<Match> recent;
  for (const auto& m : window) {
    if (m.kickoff_utc >= display_cutoff) recent.push_back(m);
  }
  LOGI("display window: %d of %d matches", (int)recent.size(),
       (int)window.size());

  if (!any_success || recent.empty()) {
    storage::set_consecutive_failures(storage::consecutive_failures() + 1);
    LOGW("no usable data -> keep previous screen");
    // 原因が分かる表示にする。通信失敗と設定ミスは直し方が違う。
    if (stats.auth_error || stats.forbidden_comp) {
      g_bar.left = std::string(msg::kAuthError);
    } else if (stats.rate_limited) {
      g_bar.left = std::string(msg::kRateLimited);
    } else {
      g_bar.left = std::string(msg::kFailedPrefix) + " " +
                   local_hhmm(last_ok, g_cfg.tz_offset_min);
    }
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

  // 45日窓の全試合から結果列を組み立てる (§2.4)。
  // 画面に出す試合だけで作ると連勝が数えられない。
  FormTable forms;
  forms.build(window);
  LOGI("form table: %d teams from %d matches", (int)forms.size(),
       (int)window.size());

  std::vector<long> seen;
  storage::load_seen_fixtures(seen);
  const std::vector<Entry> entries = build_entries(recent, pool, forms, seen);

  int with_fact = 0;
  for (const auto& e : entries) {
    if (e.fact.valid()) ++with_fact;
  }

  // レイアウトはフォントの実測値から決める (固定値だと文字が罫線を貫く)。
  const RenderPlan plan =
      build_plan(entries, g_comps, render::metrics(), render::measures());
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
