// 新着判定。「1日」を結果が届いた時刻から数える。
// キックオフ基準にすると、07:00 の起床に反映が間に合わなかった試合が
// 一度も表示されない。ここが崩れると毎朝の画面から試合が静かに消える。
#include <unity.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "core/datetime.h"
#include "core/freshness.h"

using namespace fb;

namespace {

constexpr std::time_t kHour = 3600;

Match finished(long id, std::time_t kickoff) {
  Match m;
  m.fixture_id = id;
  m.kickoff_utc = kickoff;
  m.status = "FINISHED";
  m.home_id = 1;
  m.away_id = 2;
  m.home_goals = 1;
  m.away_goals = 0;
  m.home_name = "H";
  m.away_name = "A";
  return m;
}

Match timed(long id, std::time_t kickoff) {
  Match m = finished(id, kickoff);
  m.status = "TIMED";
  m.home_goals = -1;
  m.away_goals = -1;
  return m;
}

FreshnessPolicy policy() {
  FreshnessPolicy p;
  p.fresh_hours = 20;
  p.kickoff_cap_hours = 72;
  return p;
}

bool contains(const std::vector<Match>& v, long id) {
  for (const auto& m : v) {
    if (m.fixture_id == id) return true;
  }
  return false;
}

// 毎朝 07:00 JST = 前日 22:00 UTC の自動起床。wake(12) は 9/13 07:00 JST。
std::time_t wake(int utc_day) { return make_utc(2026, 9, utc_day, 22, 0, 0); }

}  // namespace

// --- 記録 ---------------------------------------------------------------

void test_first_observation_records_now() {
  SeenLog log;
  log.observe({finished(1, wake(12) - 10 * kHour)}, wake(12));
  TEST_ASSERT_EQUAL_INT64(wake(12), log.first_seen(1));
  TEST_ASSERT_EQUAL_INT64(0, log.first_seen(999));
}

void test_second_observation_does_not_overwrite() {
  SeenLog log;
  const Match m = finished(1, wake(12) - 10 * kHour);
  log.observe({m}, wake(12));
  log.observe({m}, wake(13));
  TEST_ASSERT_EQUAL_INT64(wake(12), log.first_seen(1));
}

void test_unfinished_matches_are_not_recorded() {
  // 未反映の試合を記録すると、スコアが届いたときには既に「古い」扱いになる。
  SeenLog log;
  log.observe({timed(1, wake(12) - 3 * kHour)}, wake(12));
  TEST_ASSERT_EQUAL_INT64(0, log.first_seen(1));
  TEST_ASSERT_EQUAL_INT(0, static_cast<int>(log.size()));
}

// --- 新着判定 -----------------------------------------------------------

void test_new_result_is_fresh() {
  SeenLog log;
  const std::vector<Match> w = {finished(1, wake(12) - 9 * kHour)};
  log.observe(w, wake(12));
  TEST_ASSERT_TRUE(contains(select_fresh(w, log, wake(12), policy()), 1));
}

void test_yesterdays_batch_drops_out_next_morning() {
  // 前日 07:00 の起床で届いた結果は、翌朝 07:00 には出さない。
  // 24時間ちょうどを境にすると、取得にかかる秒数の差で出たり消えたりする。
  const std::vector<Match> w = {finished(1, wake(11) - 9 * kHour)};
  SeenLog log;
  log.observe(w, wake(11) + 50);  // 前日は取得に50秒かかった
  const auto shown = select_fresh(w, log, wake(12) + 40, policy());  // 翌朝は40秒
  TEST_ASSERT_FALSE(contains(shown, 1));
}

void test_late_result_is_shown_the_next_morning() {
  // Sunderland 0-2 Arsenal: 9/13 04:00 JST キックオフ、反映は 07:58 JST。
  // 9/13 07:00 の起床には間に合わず、9/14 07:00 の起床はキックオフの27時間後。
  // キックオフ基準の24時間なら一度も出ない。届いた時刻基準なら出る。
  const Match m = finished(1, make_utc(2026, 9, 12, 19, 0, 0));
  SeenLog log;
  log.observe({}, wake(12));   // 9/13 07:00 JST: まだ反映されていない
  log.observe({m}, wake(13));  // 9/14 07:00 JST: ここで初めて届く
  TEST_ASSERT_TRUE(contains(select_fresh({m}, log, wake(13), policy()), 1));
}

void test_result_seen_at_manual_refresh_survives_until_next_morning() {
  // 昼 12:00 の手動更新で初めて届いた結果は、翌朝 07:00 (19時間後) にも出す。
  const Match m = finished(1, make_utc(2026, 9, 12, 19, 0, 0));
  SeenLog log;
  log.observe({m}, make_utc(2026, 9, 13, 3, 0, 0));
  TEST_ASSERT_TRUE(contains(select_fresh({m}, log, wake(13), policy()), 1));
}

void test_kickoff_cap_is_a_safety_net() {
  // 何らかの理由で古い試合に新しい first_seen が付いても、キックオフ上限で弾く。
  const std::vector<Match> w = {
      finished(1, wake(12) - 10 * kHour),
      finished(2, wake(12) - 5 * 24 * kHour),
  };
  SeenLog log;
  log.observe(w, wake(12));
  const auto shown = select_fresh(w, log, wake(12), policy());
  TEST_ASSERT_TRUE(contains(shown, 1));
  TEST_ASSERT_FALSE(contains(shown, 2));
}

void test_nothing_fresh_returns_empty() {
  const std::vector<Match> w = {finished(1, wake(11) - 9 * kHour)};
  SeenLog log;
  log.observe(w, wake(11));
  TEST_ASSERT_EQUAL_INT(0,
                        static_cast<int>(select_fresh(w, log, wake(12), policy()).size()));
}

void test_recent_fallback_ignores_freshness() {
  const std::vector<Match> w = {
      finished(1, wake(12) - 30 * kHour),
      finished(2, wake(12) - 5 * 24 * kHour),
      timed(3, wake(12) - 1 * kHour),
  };
  const auto r = select_recent(w, wake(12), policy());
  TEST_ASSERT_TRUE(contains(r, 1));
  TEST_ASSERT_FALSE(contains(r, 2));  // キックオフ上限の外
  TEST_ASSERT_FALSE(contains(r, 3));  // 未完了
}

// --- 履歴が無いとき -----------------------------------------------------

void test_missing_history_assumes_arrival_at_kickoff() {
  // 初回起動・SD 差し替え直後。取得窓の全試合を「今初めて見た」とすると、
  // 数日前の試合が新着として出てしまう。
  const std::vector<Match> w = {
      finished(1, wake(12) - 57 * kHour),  // CL 9/11 (約2日半前)
      finished(2, wake(12) - 10 * kHour),  // 昨晩のプレミア
  };
  SeenLog log;
  log.assume_arrival_at_kickoff();
  log.observe(w, wake(12));
  TEST_ASSERT_EQUAL_INT64(wake(12) - 8 * kHour, log.first_seen(2));
  const auto shown = select_fresh(w, log, wake(12), policy());
  TEST_ASSERT_FALSE(contains(shown, 1));
  TEST_ASSERT_TRUE(contains(shown, 2));
}

void test_old_file_formats_are_not_trusted() {
  // 旧ファームの id 一覧も v2 形式も、記録を信用しない。旧ファームは画面に
  // 入りきらず "+N more" に回した試合まで記録していたため、v2 に取り込むと
  // 昨晩のプレミアが一度も表示されないまま既読になった (2026-09-13 実機で発生)。
  const std::vector<Match> w = {
      finished(560555, wake(12) - 57 * kHour),  // CL
      finished(560556, wake(12) - 10 * kHour),  // 昨晩のプレミア
  };
  char v2[128];
  snprintf(v2, sizeof(v2), "{\"v\":2,\"seen\":[[560555,%lld],[560556,%lld]]}",
           static_cast<long long>(wake(12) - 72 * kHour),
           static_cast<long long>(wake(12) - 72 * kHour));
  const char* legacy = "{\"ids\":[560555,560556]}";

  for (const char* old : {legacy, static_cast<const char*>(v2)}) {
    SeenLog log;
    TEST_ASSERT_TRUE(log.parse(old, strlen(old)));
    TEST_ASSERT_TRUE(log.bootstrapping());
    TEST_ASSERT_EQUAL_INT(0, static_cast<int>(log.size()));
    log.observe(w, wake(12));
    const auto shown = select_fresh(w, log, wake(12), policy());
    TEST_ASSERT_FALSE(contains(shown, 560555));
    TEST_ASSERT_TRUE(contains(shown, 560556));
  }
}

void test_bootstrap_applies_only_to_that_run() {
  // 仮定は1回きり。保存して読み直した次の回からは、本当に届いた時刻で記録する。
  SeenLog log;
  log.assume_arrival_at_kickoff();
  log.observe({finished(1, wake(12) - 10 * kHour)}, wake(12));
  TEST_ASSERT_FALSE(log.bootstrapping());

  const std::string json = log.serialize();
  SeenLog next;
  TEST_ASSERT_TRUE(next.parse(json.c_str(), json.size()));
  TEST_ASSERT_FALSE(next.bootstrapping());
  const Match late = finished(2, make_utc(2026, 9, 12, 19, 0, 0));
  next.observe({late}, wake(13));
  TEST_ASSERT_EQUAL_INT64(wake(13), next.first_seen(2));
}

// --- 保存 ---------------------------------------------------------------

void test_prune_drops_old_records_only() {
  SeenLog log;
  log.observe({finished(1, wake(1))}, wake(1));
  log.observe({finished(2, wake(12))}, wake(12));
  log.prune(wake(12), 168);
  TEST_ASSERT_EQUAL_INT64(0, log.first_seen(1));
  TEST_ASSERT_EQUAL_INT64(wake(12), log.first_seen(2));
}

void test_json_round_trip() {
  SeenLog a;
  a.observe({finished(560555, wake(12) - kHour), finished(560556, wake(12) - kHour)},
            wake(12));
  const std::string json = a.serialize();
  SeenLog b;
  TEST_ASSERT_TRUE(b.parse(json.c_str(), json.size()));
  TEST_ASSERT_EQUAL_INT64(wake(12), b.first_seen(560555));
  TEST_ASSERT_EQUAL_INT64(wake(12), b.first_seen(560556));
}

void test_broken_json_is_not_fatal() {
  SeenLog log;
  const char* bad = "{not json";
  TEST_ASSERT_FALSE(log.parse(bad, strlen(bad)));
  TEST_ASSERT_EQUAL_INT(0, static_cast<int>(log.size()));
  TEST_ASSERT_TRUE(log.bootstrapping());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_first_observation_records_now);
  RUN_TEST(test_second_observation_does_not_overwrite);
  RUN_TEST(test_unfinished_matches_are_not_recorded);
  RUN_TEST(test_new_result_is_fresh);
  RUN_TEST(test_yesterdays_batch_drops_out_next_morning);
  RUN_TEST(test_late_result_is_shown_the_next_morning);
  RUN_TEST(test_result_seen_at_manual_refresh_survives_until_next_morning);
  RUN_TEST(test_kickoff_cap_is_a_safety_net);
  RUN_TEST(test_nothing_fresh_returns_empty);
  RUN_TEST(test_recent_fallback_ignores_freshness);
  RUN_TEST(test_missing_history_assumes_arrival_at_kickoff);
  RUN_TEST(test_old_file_formats_are_not_trusted);
  RUN_TEST(test_bootstrap_applies_only_to_that_run);
  RUN_TEST(test_prune_drops_old_records_only);
  RUN_TEST(test_json_round_trip);
  RUN_TEST(test_broken_json_is_not_fatal);
  return UNITY_END();
}
