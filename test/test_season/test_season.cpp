// シーズン年判定の境界 (§2.2 / §9.1)。年末年始をまたぐケースを必ず通す。
#include <unity.h>

#include "core/budget.h"
#include "core/season.h"

using namespace fb;

void test_mid_season_autumn() {
  // 2026-09-15 は 2026-27 シーズン → season=2026
  TEST_ASSERT_EQUAL_INT(2026, season_year_from_utc(make_utc(2026, 9, 15)));
}

void test_new_years_eve() {
  // 12/31 はまだ 2026-27 シーズン
  TEST_ASSERT_EQUAL_INT(2026, season_year_from_utc(make_utc(2026, 12, 31, 23, 59, 59)));
}

void test_new_years_day() {
  // 1/1 も同じシーズン。開始年は前年のまま。ここを間違えると
  // 年明けに全リーグの取得が空になる。
  TEST_ASSERT_EQUAL_INT(2026, season_year_from_utc(make_utc(2027, 1, 1, 0, 0, 0)));
}

void test_spring_still_previous_year() {
  TEST_ASSERT_EQUAL_INT(2026, season_year_from_utc(make_utc(2027, 5, 31)));
}

void test_june_30_is_previous_season() {
  TEST_ASSERT_EQUAL_INT(2026, season_year_from_utc(make_utc(2027, 6, 30, 23, 59, 59)));
}

void test_july_1_flips() {
  TEST_ASSERT_EQUAL_INT(2027, season_year_from_utc(make_utc(2027, 7, 1, 0, 0, 0)));
}

void test_leap_day() {
  TEST_ASSERT_EQUAL_INT(2027, season_year_from_utc(make_utc(2028, 2, 29)));
}

void test_custom_cutover() {
  // カットオーバーを8月にした場合、7月中は前シーズン扱い
  TEST_ASSERT_EQUAL_INT(2026, season_year_from_ymd(2027, 7, 8));
  TEST_ASSERT_EQUAL_INT(2027, season_year_from_ymd(2027, 8, 8));
}

void test_make_utc_matches_known_epoch() {
  TEST_ASSERT_EQUAL_INT64(0, make_utc(1970, 1, 1));
  TEST_ASSERT_EQUAL_INT64(1767225600, make_utc(2026, 1, 1));
}

void test_utc_day_boundary() {
  // UTC 日の境界。JST 07:00 は前日の UTC 日に入る (§2.3 の予約枠の前提)。
  const std::time_t jst0700 = make_utc(2026, 8, 29, 22, 0, 0);  // = 8/30 07:00 JST
  const std::time_t jst0900 = make_utc(2026, 8, 30, 0, 0, 0);   // = 8/30 09:00 JST
  TEST_ASSERT_EQUAL_INT32(utc_day_of(make_utc(2026, 8, 29)), utc_day_of(jst0700));
  TEST_ASSERT_NOT_EQUAL(utc_day_of(jst0700), utc_day_of(jst0900));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_mid_season_autumn);
  RUN_TEST(test_new_years_eve);
  RUN_TEST(test_new_years_day);
  RUN_TEST(test_spring_still_previous_year);
  RUN_TEST(test_june_30_is_previous_season);
  RUN_TEST(test_july_1_flips);
  RUN_TEST(test_leap_day);
  RUN_TEST(test_custom_cutover);
  RUN_TEST(test_make_utc_matches_known_epoch);
  RUN_TEST(test_utc_day_boundary);
  return UNITY_END();
}
