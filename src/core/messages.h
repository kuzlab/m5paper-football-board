// 文言テンプレート (§4.2)。LLM は使わない。
// 表示する日本語はすべてこのファイルに集約する。ここを変更したら
// tools/make_vlw.py の文字リストも更新すること (README 参照)。
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
constexpr const char* kRefreshHint    = "更新 → 側面ボタン";
constexpr const char* kUpdating       = "更新中…";
constexpr const char* kAlreadyFresh   = "更新済み";
constexpr const char* kUpdatedPrefix  = "更新";
constexpr const char* kFailedPrefix   = "失敗";
constexpr const char* kLowBattery     = "電池残量低下";
constexpr const char* kFontError      = "FONT ERR";
constexpr const char* kSdConfigError  = "SD CONFIG ERROR";
constexpr const char* kWifiError      = "Wi-Fi 接続失敗";
// トークン不正、または競技会が契約プランに含まれない (401 / 403)
constexpr const char* kAuthError      = "APIトークンエラー";
// 429。サーバの指示に従って待った結果、取得しきれなかった
constexpr const char* kRateLimited    = "レート制限中";

// 溢れ表示 (§5.2)。"ほか 6試合"
std::string overflow_text(int remaining);

// SD / config.json が無いときの案内 (§7.1)。無言で失敗しない。
constexpr const char* kSdSetupHelp1 = "microSD に config.json を置いてください";
constexpr const char* kSdSetupHelp2 = "sd/config.example.json を参照";

}  // namespace msg
}  // namespace fb
