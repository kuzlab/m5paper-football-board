// 日付ユーティリティ (§2.3a の dateFrom/dateTo とアラーム計算の土台)。
#include <unity.h>

#include "core/datetime.h"

using namespace fb;

void test_make_utc_matches_known_epoch() {
  TEST_ASSERT_EQUAL_INT64(0, make_utc(1970, 1, 1));
  TEST_ASSERT_EQUAL_INT64(1767225600, make_utc(2026, 1, 1));
}

void test_date_string_round_trip() {
  TEST_ASSERT_EQUAL_STRING("2026-08-30",
                           utc_date_string(make_utc(2026, 8, 30, 12, 0, 0)).c_str());
  TEST_ASSERT_EQUAL_STRING("2026-01-01",
                           utc_date_string(make_utc(2026, 1, 1, 0, 0, 0)).c_str());
  TEST_ASSERT_EQUAL_STRING("2026-12-31",
                           utc_date_string(make_utc(2026, 12, 31, 23, 59, 59)).c_str());
}

void test_date_string_leap_day() {
  TEST_ASSERT_EQUAL_STRING("2028-02-29",
                           utc_date_string(make_utc(2028, 2, 29)).c_str());
}

void test_45_day_window_crosses_new_year() {
  // dateFrom = 今日 - 45日。年をまたぐケースで日付が壊れないこと (§2.3a)。
  const std::time_t now = make_utc(2027, 1, 10, 6, 0, 0);
  const std::time_t from = now - 45 * 86400;
  TEST_ASSERT_EQUAL_STRING("2026-11-26", utc_date_string(from).c_str());
  TEST_ASSERT_EQUAL_STRING("2027-01-10", utc_date_string(now).c_str());
}

void test_utc_day_boundary() {
  // JST 07:00 は前日の UTC 日に入る。アラーム計算の前提。
  const std::time_t jst0700 = make_utc(2026, 8, 29, 22, 0, 0);  // 8/30 07:00 JST
  const std::time_t jst0900 = make_utc(2026, 8, 30, 0, 0, 0);   // 8/30 09:00 JST
  TEST_ASSERT_EQUAL_INT32(utc_day_of(make_utc(2026, 8, 29)), utc_day_of(jst0700));
  TEST_ASSERT_NOT_EQUAL(utc_day_of(jst0700), utc_day_of(jst0900));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_make_utc_matches_known_epoch);
  RUN_TEST(test_date_string_round_trip);
  RUN_TEST(test_date_string_leap_day);
  RUN_TEST(test_45_day_window_crosses_new_year);
  RUN_TEST(test_utc_day_boundary);
  return UNITY_END();
}
