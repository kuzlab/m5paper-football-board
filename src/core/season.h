// シーズン年の判定 (§2.2)。
// API-FOOTBALL の season はシーズン開始年。2026-27 シーズンなら season=2026。
#pragma once

#include <ctime>

namespace fb {

// 欧州主要リーグはおおむね7〜8月開幕。既定のカットオーバー月は7月。
// 7月1日 00:00 UTC 以降はその年が開始年、6月30日以前は前年が開始年。
constexpr int kDefaultSeasonCutoverMonth = 7;  // 1-origin

// utc: UTC の time_t。cutover_month: 1〜12。
int season_year_from_utc(std::time_t utc, int cutover_month = kDefaultSeasonCutoverMonth);

// 年月日から直接。テスト用。month/day は 1-origin。
int season_year_from_ymd(int year, int month, int cutover_month = kDefaultSeasonCutoverMonth);

// UTC の time_t を作る (テスト用ヘルパ。timegm 非依存)。
std::time_t make_utc(int year, int month, int day, int hour = 0, int min = 0, int sec = 0);

}  // namespace fb
