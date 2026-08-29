#include "logging.h"

#include <Arduino.h>
#include <SD.h>

#include <cstdio>
#include <vector>

#include "storage.h"

namespace fb {
namespace log {
namespace {

std::string g_path;
std::string g_buffer;
std::vector<std::string> g_secrets;
std::time_t g_now = 0;
unsigned long g_boot_ms = 0;

const char* level_tag(Level lv) {
  switch (lv) {
    case kWarn: return "WARN";
    case kError: return "ERR ";
    default: return "INFO";
  }
}

// 登録済みの秘密を伏せる。ログに API キーを残さない (§8.2)。
void mask(std::string& s) {
  for (const auto& sec : g_secrets) {
    if (sec.size() < 6) continue;
    std::size_t pos = 0;
    while ((pos = s.find(sec, pos)) != std::string::npos) {
      s.replace(pos, sec.size(), "***MASKED***");
      pos += 12;
    }
  }
}

std::string date_string(std::time_t t) {
  std::tm tmv{};
  gmtime_r(&t, &tmv);
  char buf[16];
  snprintf(buf, sizeof(buf), "%04d-%02d-%02d", tmv.tm_year + 1900,
           tmv.tm_mon + 1, tmv.tm_mday);
  return std::string(buf);
}

}  // namespace

void begin(std::time_t now_utc) {
  g_boot_ms = millis();
  g_buffer.reserve(4096);
  set_time(now_utc);
}

void set_time(std::time_t now_utc) {
  g_now = now_utc;
  g_path = std::string(storage::kLogDir) + "/" + date_string(now_utc) + ".log";
}

void register_secret(const std::string& s) {
  if (!s.empty()) g_secrets.push_back(s);
}

void write(Level lv, const char* fmt, ...) {
  char body[256];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(body, sizeof(body), fmt, ap);
  va_end(ap);

  std::string line = body;
  mask(line);

  char head[48];
  std::tm tmv{};
  const std::time_t t = g_now + (millis() - g_boot_ms) / 1000;
  gmtime_r(&t, &tmv);
  snprintf(head, sizeof(head), "%02d:%02d:%02dZ %s +%lums ", tmv.tm_hour,
           tmv.tm_min, tmv.tm_sec, level_tag(lv), millis() - g_boot_ms);

  Serial.print(head);
  Serial.println(line.c_str());

  g_buffer += head;
  g_buffer += line;
  g_buffer += '\n';
  // バッファが膨らみすぎたら都度書き出す。
  if (g_buffer.size() > 3072) flush();
}

void flush() {
  if (g_buffer.empty() || !storage::sd_ready() || g_path.empty()) return;
  File f = SD.open(g_path.c_str(), FILE_APPEND);
  if (!f) return;
  f.write(reinterpret_cast<const uint8_t*>(g_buffer.data()), g_buffer.size());
  f.flush();
  f.close();
  g_buffer.clear();
}

void prune(int retention_days, std::time_t now_utc) {
  if (!storage::sd_ready() || retention_days <= 0) return;
  File dir = SD.open(storage::kLogDir);
  if (!dir) return;
  const std::string cutoff =
      date_string(now_utc - static_cast<std::time_t>(retention_days) * 86400);
  std::vector<std::string> doomed;
  for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
    std::string name = f.name();
    const std::size_t slash = name.find_last_of('/');
    if (slash != std::string::npos) name = name.substr(slash + 1);
    f.close();
    // ファイル名が YYYY-MM-DD.log なので文字列比較で新旧が判定できる。
    if (name.size() >= 10 && name.substr(0, 10) < cutoff) {
      doomed.push_back(std::string(storage::kLogDir) + "/" + name);
    }
  }
  dir.close();
  for (const auto& p : doomed) SD.remove(p.c_str());
}

}  // namespace log
}  // namespace fb
