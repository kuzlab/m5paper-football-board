#include "storage.h"

#include <ArduinoJson.h>
#include <M5Unified.h>
#include <Preferences.h>
#include <SD.h>
#include <SPI.h>

namespace fb {
namespace storage {
namespace {

bool g_sd_ok = false;
Preferences g_prefs;
bool g_nvs_ok = false;

constexpr const char* kNvsNamespace = "fbboard";

// M5Paper の microSD は IT8951 (EPD) と SPI バスを共有する。
// デフォルトの VSPI ピン (18/19/23) ではカードに届かないので明示する。
constexpr int kSdSck = 14;
constexpr int kSdMiso = 13;
constexpr int kSdMosi = 12;
constexpr int kSdCs = 4;

}  // namespace

bool sd_ready() { return g_sd_ok; }

bool begin_sd() {
  SPI.begin(kSdSck, kSdMiso, kSdMosi, kSdCs);
  g_sd_ok = SD.begin(kSdCs, SPI, 20000000);
  if (!g_sd_ok) {
    // 一度だけ低速で再試行する。相性の悪いカードがある。
    g_sd_ok = SD.begin(kSdCs, SPI, 4000000);
  }
  if (g_sd_ok) {
    ensure_dir("/cache");
    ensure_dir(kLogDir);
  }
  return g_sd_ok;
}

bool ensure_dir(const char* path) {
  if (!g_sd_ok) return false;
  if (SD.exists(path)) return true;
  return SD.mkdir(path);
}

bool read_file(const char* path, std::string& out, std::size_t max_bytes) {
  out.clear();
  if (!g_sd_ok) return false;
  File f = SD.open(path, FILE_READ);
  if (!f) return false;
  const std::size_t n = f.size();
  if (n == 0 || n > max_bytes) {
    f.close();
    return false;
  }
  out.resize(n);
  const std::size_t got = f.read(reinterpret_cast<uint8_t*>(&out[0]), n);
  f.close();
  if (got != n) {
    out.clear();
    return false;
  }
  return true;
}

bool write_file_atomic(const char* path, const std::string& data) {
  if (!g_sd_ok) return false;
  // 書き込み中に電源が落ちても元ファイルを壊さないよう、一時ファイル経由。
  std::string tmp = std::string(path) + ".tmp";
  File f = SD.open(tmp.c_str(), FILE_WRITE);
  if (!f) return false;
  const std::size_t written =
      f.write(reinterpret_cast<const uint8_t*>(data.data()), data.size());
  f.flush();
  f.close();
  if (written != data.size()) {
    SD.remove(tmp.c_str());
    return false;
  }
  SD.remove(path);
  return SD.rename(tmp.c_str(), path);
}

// --- NVS ----------------------------------------------------------------

bool begin_nvs() {
  g_nvs_ok = g_prefs.begin(kNvsNamespace, /*readOnly=*/false);
  return g_nvs_ok;
}

std::uint32_t last_plan_hash() {
  return g_nvs_ok ? g_prefs.getUInt("plan_hash", 0) : 0;
}
void set_last_plan_hash(std::uint32_t h) {
  if (g_nvs_ok) g_prefs.putUInt("plan_hash", h);
}
int partial_refresh_count() {
  return g_nvs_ok ? g_prefs.getInt("partial_n", 0) : 0;
}
void set_partial_refresh_count(int n) {
  if (g_nvs_ok) g_prefs.putInt("partial_n", n);
}
int consecutive_failures() {
  return g_nvs_ok ? g_prefs.getInt("fail_n", 0) : 0;
}
void set_consecutive_failures(int n) {
  if (g_nvs_ok) g_prefs.putInt("fail_n", n);
}
std::time_t last_fetch_utc() {
  return g_nvs_ok ? static_cast<std::time_t>(g_prefs.getLong64("last_fetch", 0)) : 0;
}
void set_last_fetch_utc(std::time_t t) {
  if (g_nvs_ok) g_prefs.putLong64("last_fetch", static_cast<int64_t>(t));
}
std::time_t last_success_utc() {
  return g_nvs_ok ? static_cast<std::time_t>(g_prefs.getLong64("last_ok", 0)) : 0;
}
void set_last_success_utc(std::time_t t) {
  if (g_nvs_ok) g_prefs.putLong64("last_ok", static_cast<int64_t>(t));
}

// --- 前回順位表 ---------------------------------------------------------

bool load_standings(const std::vector<Competition>& comps,
                    std::vector<LeagueStandings>& out) {
  std::string json;
  // 初回・ファイル欠損は差分なしとして扱い、エラーにしない (§2.5)。
  if (!read_file(kStandingsPath, json, 32768)) return false;
  return parse_standings_cache(json.c_str(), json.size(), comps, out);
}

bool save_standings(const std::vector<LeagueStandings>& in,
                    const std::vector<Competition>& comps) {
  return write_file_atomic(kStandingsPath,
                           serialize_standings_cache(in, comps));
}

// --- 既出 fixture -------------------------------------------------------

bool load_seen_fixtures(std::vector<long>& out) {
  out.clear();
  std::string json;
  if (!read_file(kSeenPath, json, 8192)) return false;
  JsonDocument doc;
  if (deserializeJson(doc, json)) return false;
  for (JsonVariantConst v : doc["ids"].as<JsonArrayConst>()) {
    out.push_back(v.as<long>());
  }
  return true;
}

bool save_seen_fixtures(const std::vector<long>& in, std::size_t max_keep) {
  JsonDocument doc;
  JsonArray a = doc["ids"].to<JsonArray>();
  const std::size_t start = in.size() > max_keep ? in.size() - max_keep : 0;
  for (std::size_t i = start; i < in.size(); ++i) a.add(in[i]);
  std::string out;
  serializeJson(doc, out);
  return write_file_atomic(kSeenPath, out);
}

}  // namespace storage
}  // namespace fb
