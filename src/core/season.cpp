#include "core/season.h"

namespace fb {
namespace {

// Howard Hinnant の days_from_civil。timegm / gmtime のロケール依存を避けるため
// 自前で持つ。ESP32 の newlib でも native でも同じ結果になる。
long days_from_civil(int y, unsigned m, unsigned d) {
  y -= m <= 2;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);           // [0, 399]
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1; // [0, 365]
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;          // [0, 146096]
  return static_cast<long>(era) * 146097 + static_cast<long>(doe) - 719468;
}

void civil_from_days(long z, int* y, unsigned* m, unsigned* d) {
  z += 719468;
  const long era = (z >= 0 ? z : z - 146096) / 146097;
  const unsigned doe = static_cast<unsigned>(z - era * 146097);
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const long yy = static_cast<long>(yoe) + era * 400;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  *d = doy - (153 * mp + 2) / 5 + 1;
  *m = mp + (mp < 10 ? 3 : -9);
  *y = static_cast<int>(yy + (*m <= 2));
}

}  // namespace

std::time_t make_utc(int year, int month, int day, int hour, int min, int sec) {
  const long days = days_from_civil(year, static_cast<unsigned>(month),
                                    static_cast<unsigned>(day));
  return static_cast<std::time_t>(days) * 86400 + hour * 3600 + min * 60 + sec;
}

int season_year_from_ymd(int year, int month, int cutover_month) {
  return month >= cutover_month ? year : year - 1;
}

int season_year_from_utc(std::time_t utc, int cutover_month) {
  int y = 0;
  unsigned m = 0, d = 0;
  long days = static_cast<long>(utc / 86400);
  if (utc < 0 && utc % 86400 != 0) days -= 1;  // 負方向の切り捨て
  civil_from_days(days, &y, &m, &d);
  return season_year_from_ymd(y, static_cast<int>(m), cutover_month);
}

}  // namespace fb
