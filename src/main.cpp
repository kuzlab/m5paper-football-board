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
#include "core/freshness.h"
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
// 前回描いた画面。コールドブートでフレームバッファが空になるため、
// 固定エリアだけを更新する場合もこれを描き直す必要がある (§6.2-1)。
RenderPlan g_last_plan;
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
//
// 既定のアラームは「毎日同時刻」で、日付を照合しない。そのため、次回ここに
// 到達できなくても (起動直後のブラウンアウトなど) 翌日また起きられる。
// 2026-09-22 は電池が弱った状態で起床に失敗し、アラームが再設定されないまま
// 二度と起きなくなった。日付を照合するのは起床間隔を延ばすときだけにする。
[[noreturn]] void finish_and_power_off(std::time_t now_utc) {
  int skip_days = 0;
  const int fails = storage::consecutive_failures();
  if (fails > g_cfg.max_consecutive_failures) {
    // 連続失敗が続くなら起床間隔を延ばして電池を温存する (§8.1)。
    skip_days = std::min(fails - g_cfg.max_consecutive_failures, 2);
    LOGW("backoff: %d consecutive failures -> skip %d day(s)", fails, skip_days);
  }

  std::time_t scheduled = 0;
  bool alarm_ok = false;
  for (int attempt = 0; attempt < 2 && !alarm_ok; ++attempt) {
    if (attempt > 0) delay(200);  // 1度だけ再試行する
    if (skip_days > 0 && now_utc > 0) {
      scheduled = power::next_daily_alarm_utc(
          now_utc, g_cfg.daily_wake_hour, g_cfg.daily_wake_min,
          g_cfg.tz_offset_min, skip_days);
      alarm_ok = power::set_alarm_utc(scheduled, /*match_day=*/true);
    } else if (now_utc <= 0) {
      // 時刻が分からない (電池切れ後の初回で SNTP も失敗)。絶対時刻では
      // 設定できないので、RTC の進み分だけを頼りに再試行する。
      // ただし電池が弱っているときに1時間おきに起きると使い切ってしまう。
      scheduled = 0;
      const bool weak = power::battery_volt() < g_cfg.warn_battery_volt;
      alarm_ok = power::set_alarm_after_minutes(weak ? 24 * 60 : 60);
    } else {
      scheduled = power::next_daily_alarm_utc(now_utc, g_cfg.daily_wake_hour,
                                              g_cfg.daily_wake_min,
                                              g_cfg.tz_offset_min);
      alarm_ok = power::set_alarm_utc(scheduled);
    }
  }
  // 起床理由の判定に使う。AF が消えていても予定時刻で自動起床と分かる。
  storage::set_scheduled_alarm_utc(scheduled);
  // ここまで来たら「正常に一巡した」。ブラウンアウト連鎖の数えをリセットする。
  storage::set_boot_attempts(0);

  log::prune(g_cfg.log_retention_days, now_utc);
  power::log_battery("end");
  LOGI("done in %lums, heap=%u, psram=%u", millis() - g_t0,
       (unsigned)ESP.getFreeHeap(), (unsigned)api::psram_free());
  log::flush();

  // AF が立ったまま電源を切ると INT が張られ続け、電源が入り直す。
  // 起床直後にも落としているが、念のためここでも確認する。
  power::clear_alarm_flag();
  render::sleep_display();

  if (!alarm_ok) {
    // 電源を切ると二度と起きない。通電したまま残して人が気づけるようにする。
    LOGE("ALARM SET FAILED - staying powered on so the device can still wake");
    log::flush();
    render::draw_fatal("RTC ALARM ERROR", "Could not set the next wake alarm",
                       "Staying powered on");
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
  // 既定値でアラームだけ残して落とす。人が SD を直せば翌朝復帰する。
  // 通電したまま待たせると電池を使い切り、RTC が時刻とアラームまで失う。
  const std::time_t now = rtc_now_utc();
  const bool armed =
      now > 0 ? power::set_alarm_utc(power::next_daily_alarm_utc(
                    now, g_cfg.daily_wake_hour, g_cfg.daily_wake_min,
                    g_cfg.tz_offset_min))
              : power::set_alarm_after_minutes(60);
  if (armed) {
    storage::set_scheduled_alarm_utc(0);
    storage::set_boot_attempts(0);
    log::flush();
    render::sleep_display();
    power::shutdown();
  }
  LOGE("ALARM SET FAILED - staying powered on");
  log::flush();
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
                                 const FormTable& forms) {
  std::vector<Entry> out;
  out.reserve(matches.size());
  for (const auto& m : matches) {
    Entry e;
    e.match = m;
    e.fact = compute_fact(m, pool, forms, g_comps, g_cfg.thresholds);
    out.push_back(e);
  }
  sort_entries(out, g_comps);
  return out;
}

}  // namespace

void setup() {
  g_t0 = millis();

  // NVS は M5.begin() より先に開ける。EPD の初期化で電圧が落ちて再起動する
  // 場合でも、起動回数だけは数えられるようにしておく。
  storage::begin_nvs();
  const int boot_attempt = storage::boot_attempts() + 1;
  storage::set_boot_attempts(boot_attempt);

  auto cfg = M5.config();
  // 既定の true では M5.begin() が BM8563 の 0x09-0x0C と 0x01 をゼロクリア
  // する。起床理由の AF が毎回消えて全部「ボタン起動」に見えていた原因で、
  // さらに武装済みのアラームも起動のたびに解除されてしまう (power.h 参照)。
  cfg.disable_rtc_irq = false;
  M5.begin(cfg);
  Serial.begin(115200);

  // アラームフラグ (AF) は立っている間 INT を張り続ける = 電源が入り続ける。
  // 起床理由を読んだら、他の初期化より先に落とす。ここまで来る前に落ちると
  // 再起動を繰り返すため、この区間は I2C だけで短く済ませる。
  std::time_t now = rtc_now_utc();
  const power::WakeInfo wake =
      power::wake_info(now, storage::scheduled_alarm_utc());
  const bool is_auto = (wake.reason == power::WakeReason::kRtcAlarm);
  power::clear_alarm_flag();

  const bool sd_ok = storage::begin_sd();
  log::begin(now > 0 ? now : 1767225600);  // RTC 未設定なら仮の日付

  LOGI("boot: reset=%s sd=%d attempt=%d heap=%u psram=%u", wake.reset_reason,
       (int)sd_ok, boot_attempt, (unsigned)ESP.getFreeHeap(),
       (unsigned)api::psram_free());
  LOGI("wake reason: %s (AF=%d scheduled=%d control2=%02X)",
       is_auto ? "RTC alarm (auto)" : "button (manual)", (int)wake.alarm_flag,
       (int)wake.schedule_matched, wake.control2);
  power::log_battery("boot");
  if (now <= 0) {
    // RTC が時刻を失っている = 電池が完全に切れた。アラームも失われている
    // ので、SNTP で時刻を取り直すまで自動起床は当てにできない。
    LOGW("RTC has lost its time (battery was fully drained?)");
  }
  // ここで必ず書き出す。これが無かったため 2026-09-22 のブラウンアウトは
  // ログに1行も残らなかった。
  log::flush();

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

  // 通信も描画もする前に、次回アラームを先に武装しておく。この後どこで
  // 落ちても翌日の定時には起きられる。最後に同じ値で上書きする。
  // 2026-09-22 は、起床に失敗してアラームが再設定されないまま二度と
  // 起きなくなった。武装を「一巡したあと」だけにしない。
  if (now > 0) {
    const std::time_t next = power::next_daily_alarm_utc(
        now, g_cfg.daily_wake_hour, g_cfg.daily_wake_min, g_cfg.tz_offset_min);
    if (power::set_alarm_utc(next)) storage::set_scheduled_alarm_utc(next);
  }

  // 電池の状態で3段に分ける (§6.5)。
  //   critical: 通信しない。ここから先は電圧が落ちて落ちる危険が高い
  //   warn:     更新は続けるが画面に「充電して」を出す
  // 電池を使い切ると RTC が時刻とアラームまで失い、人が触るまで戻らない。
  const float vbat = power::battery_volt();
  const bool batt_critical = (vbat > 0.1f && vbat < g_cfg.low_battery_volt);
  g_bar.battery_warn = (vbat > 0.1f && vbat < g_cfg.warn_battery_volt);

  // 起動を繰り返していないか。電池が弱った状態でブラウンアウト再起動が
  // 続くと、1日ぶんの電池を数十分で使い切る。2026-09-22 はこれで電池が
  // 尽きた可能性が高い。
  // 電圧が十分あるときは引き返さない。USB 給電中は powerOff() がラッチを
  // 切れずブラウンアウトリセットになるため、ここが毎回成立してしまう。
  if (boot_attempt >= 3 && g_bar.battery_warn) {
    LOGE("boot attempt %d without a clean finish (reset=%s, %.3fV)"
         " -> skip everything and wait for the next alarm",
         boot_attempt, wake.reset_reason, vbat);
    finish_and_power_off(now);
  }
  if (boot_attempt >= 3) {
    LOGW("boot attempt %d without a clean finish (reset=%s) but %.3fV is"
         " healthy -> continue", boot_attempt, wake.reset_reason, vbat);
  }

  // --- 環境と固定エリア --------------------------------------------------
  const sht30::Reading env = sht30::measure();
  g_bar.env_ok = env.ok;
  g_bar.temperature_c = env.temperature_c;
  g_bar.humidity_pct = env.humidity_pct;
  g_bar.battery_pct = power::battery_percent();
  g_bar.font_error = !fonts;

  const std::time_t last_ok = storage::last_success_utc();
  // 前回の画面を SD から復元する。無ければ本文なしで進む (初回)。
  // コールドブートではフレームバッファが空なので、これが無いと
  // 固定エリアを更新しただけで本文が消える (§6.2-1)。
  const bool plan_restored = storage::load_plan(g_last_plan);
  LOGI("last plan: %s (%d rows)", plan_restored ? "restored" : "none",
       (int)g_last_plan.rows.size());
  // 押されたら通信前に「Updating...」を出す (§6.3)。
  // 電池が危ないときは最初から「Battery low」を出す。書き換えが一番
  // 電流を食うので、この状況で2回押し出さない (§6.5)。
  g_bar.left = std::string(batt_critical ? msg::kLowBattery : msg::kUpdating);
  render::draw_status_bar(g_bar, g_last_plan);

  // --- 電池チェック ------------------------------------------------------
  if (batt_critical) {
    LOGW("battery low: %.3fV (%d%%) -> skip fetch", vbat,
         power::battery_percent());
    finish_and_power_off(now);
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
    render::draw_status_bar(g_bar, g_last_plan);
    finish_and_power_off(now);
  }

  // --- Wi-Fi / SNTP ------------------------------------------------------
  if (!connect_wifi()) {
    // 取得失敗時は前回の画面をそのまま残す (§8.1)。クリアしない。
    storage::set_consecutive_failures(storage::consecutive_failures() + 1);
    g_bar.left = std::string(msg::kFailedPrefix) + " " +
                 local_hhmm(last_ok, g_cfg.tz_offset_min);
    render::draw_status_bar(g_bar, g_last_plan);
    finish_and_power_off(now > 0 ? now : rtc_now_utc());
  }
  power::log_battery("wifi");
  const std::time_t synced = sync_time();
  if (synced > 0) now = synced;

  // --- 取得 (§2.3, keep-alive + レート制限内で連投) ----------------------
  net::KeepAliveClient http;
  net::RatePacer pacer(g_cfg.max_requests_per_minute);
  api::FetchStats stats;
  // 45日窓の全試合。画面表示にはこの一部を使い、結果列の集計には全部を使う。
  std::vector<Match> window;
  std::vector<LeagueStandings> standings_now;
  bool any_success = false;

  // 新着判定 (core/freshness.h)。1日以上前の結果は出さず、新着が無ければ
  // 前回の画面を維持する。「1日」は結果が届いた時刻から数える。
  SeenLog seen_log;
  if (!storage::load_seen_log(seen_log)) {
    // 履歴が無い (初回起動・SD 差し替え・破損)。この回だけ、結果は
    // キックオフの2時間後に届いていたとみなす。そうしないと45日分の
    // 試合がすべて「今初めて見た」になり、数日前の試合が新着に出る。
    seen_log.assume_arrival_at_kickoff();
  }
  const bool seen_rebuilt = seen_log.bootstrapping();
  FreshnessPolicy fresh_policy;
  fresh_policy.fresh_hours = g_cfg.fresh_hours;
  fresh_policy.kickoff_cap_hours = g_cfg.display_window_hours;
  // 記録の保持期間はキックオフ上限より長くする。捨てた試合が再観測されても
  // キックオフ上限で弾かれるので、再び新着になることはない。
  const int seen_keep_hours = std::max(168, g_cfg.display_window_hours + 24);
  std::vector<Match> display;
  bool showing_fallback = false;

  if (!http.begin(api::kHost, api::kPort, g_ca_pem.c_str())) {
    LOGE("tls connect failed");
  } else {
    LOGI("fetching %d competitions, up to %d requests per minute",
         (int)g_comps.size(), g_cfg.max_requests_per_minute);

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

    // 今回の観測を記録してから、表示する試合を決める。
    seen_log.observe(window, now);
    display = select_fresh(window, seen_log, now, fresh_policy);
    if (display.empty() && g_last_plan.rows.empty()) {
      // 新着が無く、維持すべき前回画面も無い (初回起動など)。
      // 真っ白よりは直近の結果を出すほうがよい。
      display = select_recent(window, now, fresh_policy);
      showing_fallback = true;
    }

    // 画面に出す試合がある競技会だけ standings を取る (§2.3b)。
    // 新着が無い日は順位表のリクエストが1本も飛ばない。
    for (std::size_t i = 0; i < g_comps.size(); ++i) {
      if (!g_comps[i].has_standings) continue;
      if (stats.requests >= g_cfg.max_requests_per_wake) break;
      const bool shown = std::any_of(
          display.begin(), display.end(),
          [i](const Match& m) { return m.comp_index == static_cast<int>(i); });
      if (!shown) continue;
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

  LOGI("to show: %d (%s), %d in window, %d seen records%s", (int)display.size(),
       showing_fallback ? "fallback: recent" : "new results",
       (int)window.size(), (int)seen_log.size(),
       seen_rebuilt ? " (history rebuilt, arrival assumed at kickoff+2h)" : "");

  if (!any_success) {
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
    render::draw_status_bar(g_bar, g_last_plan);
    finish_and_power_off(now);
  }

  // 新着が無い: 前回の画面をそのまま維持する。取得自体は成功しているので
  // 失敗扱いにはしない。固定エリアで「止まっている」のではなく
  // 「新着が無い」ことが分かるようにする。
  if (display.empty()) {
    LOGI("no new results -> keep previous screen");
    seen_log.prune(now, seen_keep_hours);
    storage::save_seen_log(seen_log);
    g_bar.left = std::string(msg::kNoNewResults) + " " +
                 local_hhmm(now, g_cfg.tz_offset_min);
    render::draw_status_bar(g_bar, g_last_plan);
    storage::set_consecutive_failures(0);
    storage::set_last_success_utc(now);
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

  const std::vector<Entry> entries = build_entries(display, pool, forms);

  int with_fact = 0;
  for (const auto& e : entries) {
    if (e.fact.valid()) ++with_fact;
  }

  // レイアウトはフォントの実測値から決める (固定値だと文字が罫線を貫く)。
  const RenderPlan plan =
      build_plan(entries, g_comps, render::metrics(), render::measures());
  const std::uint32_t hash = plan_hash(plan);

  // 画面は手元から見えないので、何を描いたかをログに残す (§8.2)。
  for (const auto& r : plan.rows) {
    if (r.kind == PlanRow::kHeading) {
      LOGI("  [%s]", r.heading.c_str());
    } else {
      LOGI("    %s %s %s | %s", r.home.c_str(), r.score.c_str(), r.away.c_str(),
           r.fact.c_str());
    }
  }
  if (plan.has_overflow()) LOGI("    +%d more", plan.overflow_count);

  // --- 描画 (§5.5) -------------------------------------------------------
  const bool same = (hash == storage::last_plan_hash());
  if (same && !render::needs_ghost_clear()) {
    LOGI("content unchanged -> status bar only");
    g_bar.left = std::string(msg::kUpdatedPrefix) + " " +
                 local_hhmm(now, g_cfg.tz_offset_min);
    render::draw_status_bar(g_bar, plan);
  } else {
    LOGI("render full: %d rows, %d overflow, %d facts",
         (int)plan.rows.size(), plan.overflow_count, with_fact);
    g_bar.left = std::string(msg::kUpdatedPrefix) + " " +
                 local_hhmm(now, g_cfg.tz_offset_min);
    render::draw_full(plan, g_bar);
    storage::set_last_plan_hash(hash);
    // 次回のコールドブートで画面を再構成できるよう保存する (§6.2-1)。
    storage::save_plan(plan);
  }

  // --- 保存 --------------------------------------------------------------
  if (!standings_now.empty()) {
    storage::save_standings(standings_now, g_comps);
  }
  seen_log.prune(now, seen_keep_hours);
  storage::save_seen_log(seen_log);
  storage::set_consecutive_failures(0);
  storage::set_last_success_utc(now);

  finish_and_power_off(now);
}

void loop() {
  // setup() が電源断で終わるので、ここには来ない。
  delay(1000);
}
