// 日付ユーティリティ。ロケールにも timegm にも依存しない。
//
// football-data.org は現行シーズンが既定なので season パラメータが要らない。
// 旧 season.h にあったシーズン年判定は不要になったため削除した。
#pragma once

#include <ctime>
#include <string>

namespace fb {

// UTC の time_t を作る。month/day は 1-origin。
std::time_t make_utc(int year, int month, int day, int hour = 0, int min = 0,
                     int sec = 0);

// epoch からの日数 (負の時刻でも切り捨て方向を揃える)。
long utc_day_of(std::time_t utc);

// "YYYY-MM-DD" (UTC)。API の dateFrom / dateTo とログのファイル名に使う。
std::string utc_date_string(std::time_t utc);

}  // namespace fb
