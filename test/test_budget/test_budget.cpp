// 日次リクエスト予算と自動更新枠の予約 (§2.3 / 決定事項1)。
#include <unity.h>

#include "core/budget.h"
#include "core/season.h"

using namespace fb;

namespace {
BudgetPolicy policy() {
  BudgetPolicy p;
  p.max_requests_per_day = 90;
  p.auto_reserve_requests = 13;
  p.max_manual_fetches_per_day = 5;
  p.max_requests_per_wake = 20;
  return p;
}
}  // namespace

void test_fresh_day_allows_full_fetch() {
  BudgetState s;
  roll_over(s, make_utc(2026, 8, 29, 22, 0, 0));
  const Allowance a = allow(s, policy(), /*is_auto=*/true, 13);
  TEST_ASSERT_EQUAL_INT(13, a.requests);
  TEST_ASSERT_FALSE(a.blocked);
}

void test_manual_cannot_eat_the_auto_reserve() {
  // これが決定事項1の本体。手動更新で日中に使い切っても、翌朝 07:00 の
  // 自動更新分 13 リクエストは必ず残る。
  BudgetState s;
  roll_over(s, make_utc(2026, 8, 29, 0, 0, 0));
  const BudgetPolicy p = policy();

  // 手動で使えるのは 90 - 13 = 77 まで
  s.requests_used = 77;
  s.manual_fetches = 4;
  const Allowance manual = allow(s, p, /*is_auto=*/false, 13);
  TEST_ASSERT_TRUE(manual.blocked);
  TEST_ASSERT_EQUAL_INT(0, manual.requests);

  // 同じ状態でも自動更新は通る
  const Allowance automatic = allow(s, p, /*is_auto=*/true, 13);
  TEST_ASSERT_FALSE(automatic.blocked);
  TEST_ASSERT_EQUAL_INT(13, automatic.requests);
}

void test_reserve_is_released_after_auto_ran() {
  BudgetState s;
  roll_over(s, make_utc(2026, 8, 29, 0, 0, 0));
  const BudgetPolicy p = policy();
  s.requests_used = 77;
  s.manual_fetches = 1;

  TEST_ASSERT_TRUE(allow(s, p, false, 13).blocked);
  s.auto_done = true;  // その UTC 日の自動更新が済んだ
  const Allowance a = allow(s, p, false, 13);
  TEST_ASSERT_FALSE(a.blocked);
  TEST_ASSERT_EQUAL_INT(13, a.requests);
}

void test_manual_fetch_count_limit() {
  BudgetState s;
  roll_over(s, make_utc(2026, 8, 29, 0, 0, 0));
  const BudgetPolicy p = policy();
  s.manual_fetches = 5;  // 上限
  const Allowance a = allow(s, p, false, 13);
  TEST_ASSERT_TRUE(a.blocked);
  // 自動更新は手動回数の上限に縛られない
  TEST_ASSERT_FALSE(allow(s, p, true, 13).blocked);
}

void test_per_wake_cap_prevents_retry_runaway() {
  BudgetState s;
  roll_over(s, make_utc(2026, 8, 29, 0, 0, 0));
  const Allowance a = allow(s, policy(), true, 500);
  TEST_ASSERT_EQUAL_INT(20, a.requests);  // max_requests_per_wake
}

void test_api_header_overrides_local_counter() {
  // ローカルは余裕があるつもりでも、API 側の残量が少なければそちらに従う。
  BudgetState s;
  roll_over(s, make_utc(2026, 8, 29, 0, 0, 0));
  s.requests_used = 0;
  s.api_remaining = 5;
  s.auto_done = true;  // 予約は解放済み
  const Allowance a = allow(s, policy(), false, 13);
  TEST_ASSERT_EQUAL_INT(5, a.requests);
}

void test_api_header_respects_reserve_too() {
  BudgetState s;
  roll_over(s, make_utc(2026, 8, 29, 0, 0, 0));
  s.api_remaining = 10;   // 残り10だが自動更新分13を残さねばならない
  const Allowance a = allow(s, policy(), false, 13);
  TEST_ASSERT_TRUE(a.blocked);
}

void test_rollover_resets_counters() {
  BudgetState s;
  roll_over(s, make_utc(2026, 8, 29, 0, 0, 0));
  s.requests_used = 80;
  s.manual_fetches = 5;
  s.auto_done = true;
  s.api_remaining = 3;

  TEST_ASSERT_FALSE(roll_over(s, make_utc(2026, 8, 29, 23, 59, 59)));
  TEST_ASSERT_EQUAL_INT(80, s.requests_used);

  TEST_ASSERT_TRUE(roll_over(s, make_utc(2026, 8, 30, 0, 0, 0)));
  TEST_ASSERT_EQUAL_INT(0, s.requests_used);
  TEST_ASSERT_EQUAL_INT(0, s.manual_fetches);
  TEST_ASSERT_FALSE(s.auto_done);
  TEST_ASSERT_EQUAL_INT(-1, s.api_remaining);
}

void test_daily_cycle_matches_jst_schedule() {
  // 1日の流れを通す。JST 07:00 の自動更新 → 日中に手動5回 →
  // 翌朝 JST 07:00 の自動更新がまだ通ること。
  const BudgetPolicy p = policy();
  BudgetState s;

  // 8/29 07:00 JST = 8/28 22:00 UTC
  roll_over(s, make_utc(2026, 8, 28, 22, 0, 0));
  Allowance a = allow(s, p, true, 13);
  TEST_ASSERT_EQUAL_INT(13, a.requests);
  record(s, p, true, 13, -1);

  // 8/29 の日中 (JST 09:00 以降 = UTC 8/29) は新しい UTC 日
  roll_over(s, make_utc(2026, 8, 29, 3, 0, 0));
  for (int i = 0; i < 5; ++i) {
    a = allow(s, p, false, 13);
    TEST_ASSERT_FALSE(a.blocked);
    record(s, p, false, 13, -1);
  }
  TEST_ASSERT_EQUAL_INT(65, s.requests_used);

  // 6回目の手動は回数上限 (max_manual_fetches_per_day) で弾かれる。
  // リクエスト数にはまだ余裕がある (90 - 65 - 13 = 12) が、
  // 連打を防ぐためにフェッチ回数でも縛っている。
  TEST_ASSERT_TRUE(allow(s, p, false, 13).blocked);

  // 8/30 07:00 JST = 8/29 22:00 UTC。同じ UTC 日だが自動更新は通る。
  TEST_ASSERT_FALSE(roll_over(s, make_utc(2026, 8, 29, 22, 0, 0)));
  a = allow(s, p, true, 13);
  TEST_ASSERT_FALSE(a.blocked);
  TEST_ASSERT_EQUAL_INT(13, a.requests);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_fresh_day_allows_full_fetch);
  RUN_TEST(test_manual_cannot_eat_the_auto_reserve);
  RUN_TEST(test_reserve_is_released_after_auto_ran);
  RUN_TEST(test_manual_fetch_count_limit);
  RUN_TEST(test_per_wake_cap_prevents_retry_runaway);
  RUN_TEST(test_api_header_overrides_local_counter);
  RUN_TEST(test_api_header_respects_reserve_too);
  RUN_TEST(test_rollover_resets_counters);
  RUN_TEST(test_daily_cycle_matches_jst_schedule);
  return UNITY_END();
}
