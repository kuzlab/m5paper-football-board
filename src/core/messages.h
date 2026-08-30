// 文言テンプレート (§4.2)。LLM は使わない。
// 表示する文字列はすべてこのファイルに集約する。ここを変更したら
// tools/make_vlw.py を再実行してフォントを作り直すこと (README 参照)。
//
// 表示言語は英語。チーム名・競技会名が原語表記なので、UI も英語で揃えている。
// 日本語化する場合はこのファイルの文字列を差し替えて make_vlw.py を
// 再実行すれば動く (フォント生成は日本語グリフに対応している)。
#pragma once

#include <cstddef>
#include <string>

#include "core/facts.h"

namespace fb {
namespace msg {

// --- ファクト文言 -------------------------------------------------------
// buf に収まらない場合は切り詰められる。戻り値は書き込んだ長さ。
std::size_t fact_text(const Fact& f, char* buf, std::size_t n);
std::string fact_text(const Fact& f);

// --- 固定エリアの文言 (§6.3 / §6.4) -------------------------------------
constexpr const char* kRefreshHint    = "Refresh: side button";
constexpr const char* kUpdating       = "Updating...";
constexpr const char* kAlreadyFresh   = "Up to date";
constexpr const char* kUpdatedPrefix  = "Updated";
constexpr const char* kFailedPrefix   = "Failed";
constexpr const char* kLowBattery     = "Battery low";
constexpr const char* kFontError      = "FONT ERR";
constexpr const char* kSdConfigError  = "SD CONFIG ERROR";
constexpr const char* kWifiError      = "Wi-Fi failed";
// トークン不正、または競技会が契約プランに含まれない (401 / 403)
constexpr const char* kAuthError      = "API token error";
// 429。サーバの指示に従って待った結果、取得しきれなかった
constexpr const char* kRateLimited    = "Rate limited";

// 溢れ表示 (§5.2)。"+6 more"
std::string overflow_text(int remaining);

// SD / config.json が無いときの案内 (§7.1)。無言で失敗しない。
constexpr const char* kSdSetupHelp1 = "Put config.json on the microSD card";
constexpr const char* kSdSetupHelp2 = "See sd/config.example.json";

}  // namespace msg
}  // namespace fb
