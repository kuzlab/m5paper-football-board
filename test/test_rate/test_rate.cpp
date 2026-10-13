// レート制限の待ち時間。1回の起床で7本しか投げないのに固定7秒間隔を
// 強制していたため、起床61秒のうち42秒がウェイトだった。上限を超えない
// 範囲では待たないこと、上限に達したら必ず待つことを両方押さえる。
#include <unity.h>

#include "core/rate_window.h"

using namespace fb;

namespace {

// 無料枠 10 req/min に対して9本。窓60秒、連投間隔250ms。
RateWindow pacer() { return RateWindow(9, 60000, 250); }

}  // namespace

void test_first_request_does_not_wait() {
  RateWindow w = pacer();
  TEST_ASSERT_EQUAL_UINT32(0, w.wait_for(1000));
}

void test_back_to_back_keeps_minimum_gap() {
  RateWindow w = pacer();
  w.record(1000);
  TEST_ASSERT_EQUAL_UINT32(250, w.wait_for(1000));
  TEST_ASSERT_EQUAL_UINT32(50, w.wait_for(1200));
  TEST_ASSERT_EQUAL_UINT32(0, w.wait_for(1250));
  TEST_ASSERT_EQUAL_UINT32(0, w.wait_for(5000));
}

void test_seven_requests_fit_without_waiting() {
  // 実際の1回の起床: 試合4本 + 順位表3本。1本も待たずに収まること。
  RateWindow w = pacer();
  unsigned long now = 0;
  unsigned long total_wait = 0;
  for (int i = 0; i < 7; ++i) {
    const unsigned long wait = w.wait_for(now);
    total_wait += wait;
    now += wait;
    w.record(now);
    now += 1200;  // 1本あたりの通信時間 (実測 1〜2秒)
  }
  TEST_ASSERT_EQUAL_UINT32(0, total_wait);
  TEST_ASSERT_EQUAL_INT(7, w.in_window(now));
}

void test_tenth_request_waits_for_the_window() {
  // 上限9本を使い切ったら、最初の1本が窓から出るまで待つ。
  RateWindow w = pacer();
  for (int i = 0; i < 9; ++i) w.record(1000 + i * 300);
  const unsigned long now = 1000 + 9 * 300;
  const unsigned long wait = w.wait_for(now);
  // 最初の1本 (t=1000) が窓を出るのは t=61000
  TEST_ASSERT_EQUAL_UINT32(61000 - now, wait);
  // 待った後なら投げられる
  TEST_ASSERT_EQUAL_UINT32(0, w.wait_for(now + wait));
}

void test_old_requests_leave_the_window() {
  RateWindow w = pacer();
  for (int i = 0; i < 9; ++i) w.record(1000 + i * 300);
  TEST_ASSERT_EQUAL_INT(0, w.in_window(1000 + 60000 + 9 * 300));
  TEST_ASSERT_EQUAL_UINT32(0, w.wait_for(1000 + 60000 + 9 * 300));
}

void test_cap_does_not_overflow() {
  // 上限より多く記録しても壊れない (リトライが暴走したとき)。
  RateWindow w = pacer();
  for (int i = 0; i < 40; ++i) w.record(1000 + i * 100);
  TEST_ASSERT_TRUE(w.in_window(5000) <= 16);
  TEST_ASSERT_TRUE(w.wait_for(5000) > 0);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_first_request_does_not_wait);
  RUN_TEST(test_back_to_back_keeps_minimum_gap);
  RUN_TEST(test_seven_requests_fit_without_waiting);
  RUN_TEST(test_tenth_request_waits_for_the_window);
  RUN_TEST(test_old_requests_leave_the_window);
  RUN_TEST(test_cap_does_not_overflow);
  return UNITY_END();
}
