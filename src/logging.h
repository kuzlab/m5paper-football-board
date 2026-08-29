// microSD へのログ (§8.2)。単体構成ではデバッグ手段がこれしかない。
#pragma once

#include <cstdarg>
#include <ctime>
#include <string>

namespace fb {
namespace log {

enum Level { kInfo, kWarn, kError };

// 起動直後に呼ぶ。SD が使えなくてもシリアルには出す。
void begin(std::time_t now_utc);
// RTC/SNTP で時刻が確定したら呼び直す。ファイル名を正しい日付にする。
void set_time(std::time_t now_utc);

void write(Level lv, const char* fmt, ...);
void flush();

// APIキーを登録しておくと、ログ出力時に自動でマスクする (§8.2)。
void register_secret(const std::string& s);

// 古いログを消す (§8.2)。SD を埋めない。
void prune(int retention_days, std::time_t now_utc);

}  // namespace log
}  // namespace fb

#define LOGI(...) ::fb::log::write(::fb::log::kInfo, __VA_ARGS__)
#define LOGW(...) ::fb::log::write(::fb::log::kWarn, __VA_ARGS__)
#define LOGE(...) ::fb::log::write(::fb::log::kError, __VA_ARGS__)
