// 結果列の自前集計 (§2.4)。API の form に依存しなくなった代わりに、
// ここが狂うと連勝・連敗がすべて壊れる。
#include <unity.h>

#include <vector>

#include "core/datetime.h"
#include "core/form.h"

using namespace fb;

namespace {

Match played(long id, int home, int away, int hg, int ag, std::time_t ko) {
  Match m;
  m.fixture_id = id;
  m.home_id = home;
  m.away_id = away;
  m.home_goals = hg;
  m.away_goals = ag;
  m.kickoff_utc = ko;
  m.status = "FINISHED";
  m.home_name = "H";
  m.away_name = "A";
  return m;
}

constexpr int kArsenal = 57;
constexpr int kChelsea = 61;
constexpr int kSpurs = 73;

}  // namespace

void test_result_for_each_side() {
  const Match m = played(1, kArsenal, kChelsea, 2, 1, 1000);
  TEST_ASSERT_EQUAL_INT('W', result_for(m, kArsenal));
  TEST_ASSERT_EQUAL_INT('L', result_for(m, kChelsea));
  TEST_ASSERT_EQUAL_INT(0, result_for(m, kSpurs));  // 出場していない
}

void test_draw() {
  const Match m = played(1, kArsenal, kChelsea, 1, 1, 1000);
  TEST_ASSERT_EQUAL_INT('D', result_for(m, kArsenal));
  TEST_ASSERT_EQUAL_INT('D', result_for(m, kChelsea));
}

void test_form_is_oldest_to_newest() {
  // Arsenal: 勝ち → 引き分け → 負け の順。末尾が最新。
  std::vector<Match> v = {
      played(1, kArsenal, kChelsea, 2, 0, 1000),
      played(2, kSpurs, kArsenal, 1, 1, 2000),
      played(3, kArsenal, kSpurs, 0, 3, 3000),
  };
  FormTable f;
  f.build(v);
  TEST_ASSERT_EQUAL_STRING("WDL", f.form_of(kArsenal).c_str());
  TEST_ASSERT_EQUAL_INT(3, f.window_played(kArsenal));
  TEST_ASSERT_EQUAL_INT(3, f.last_match_of(kArsenal));
}

void test_input_order_does_not_matter() {
  // 呼び出し側がどの順で渡しても、日付昇順に積み直されること。
  std::vector<Match> forward = {
      played(1, kArsenal, kChelsea, 2, 0, 1000),
      played(2, kSpurs, kArsenal, 1, 1, 2000),
      played(3, kArsenal, kSpurs, 0, 3, 3000),
  };
  std::vector<Match> shuffled = {forward[2], forward[0], forward[1]};

  FormTable a, b;
  a.build(forward);
  b.build(shuffled);
  TEST_ASSERT_EQUAL_STRING(a.form_of(kArsenal).c_str(),
                           b.form_of(kArsenal).c_str());
  TEST_ASSERT_EQUAL_STRING("WDL", b.form_of(kArsenal).c_str());
}

void test_opponents_get_mirrored_results() {
  std::vector<Match> v = {played(1, kArsenal, kChelsea, 3, 0, 1000)};
  FormTable f;
  f.build(v);
  TEST_ASSERT_EQUAL_STRING("W", f.form_of(kArsenal).c_str());
  TEST_ASSERT_EQUAL_STRING("L", f.form_of(kChelsea).c_str());
}

void test_unplayed_matches_are_ignored() {
  std::vector<Match> v = {
      played(1, kArsenal, kChelsea, 2, 0, 1000),
      played(2, kArsenal, kSpurs, -1, -1, 2000),  // スコアなし (中止・延期)
  };
  v[1].status = "POSTPONED";
  FormTable f;
  f.build(v);
  TEST_ASSERT_EQUAL_STRING("W", f.form_of(kArsenal).c_str());
}

void test_team_with_no_matches_in_window() {
  // 45日窓に1試合も入らないチーム (§4.3)。空文字を返し、落ちないこと。
  std::vector<Match> v = {played(1, kArsenal, kChelsea, 2, 0, 1000)};
  FormTable f;
  f.build(v);
  TEST_ASSERT_EQUAL_STRING("", f.form_of(kSpurs).c_str());
  TEST_ASSERT_EQUAL_INT(0, f.window_played(kSpurs));
  TEST_ASSERT_EQUAL_INT(0, f.last_match_of(kSpurs));
}

void test_empty_input() {
  FormTable f;
  f.build({});
  TEST_ASSERT_EQUAL_INT(0, (int)f.size());
  TEST_ASSERT_EQUAL_STRING("", f.form_of(kArsenal).c_str());
}

void test_long_window_keeps_every_match() {
  // 45日ぶんは6〜9節。窓を全部積むこと (切り詰めない)。
  std::vector<Match> v;
  for (int i = 0; i < 9; ++i) {
    v.push_back(played(i + 1, kArsenal, 1000 + i, 1, 0,
                       make_utc(2026, 8, 1) + i * 5 * 86400));
  }
  FormTable f;
  f.build(v);
  TEST_ASSERT_EQUAL_STRING("WWWWWWWWW", f.form_of(kArsenal).c_str());
  TEST_ASSERT_EQUAL_INT(9, f.window_played(kArsenal));
}

void test_same_kickoff_is_stable_by_id() {
  // 同時刻キックオフ (最終節など) でも順序が決定的であること。
  std::vector<Match> v = {
      played(20, kArsenal, kChelsea, 1, 0, 5000),
      played(10, kSpurs, kArsenal, 0, 2, 5000),
  };
  FormTable a, b;
  a.build(v);
  std::vector<Match> rev = {v[1], v[0]};
  b.build(rev);
  TEST_ASSERT_EQUAL_STRING(a.form_of(kArsenal).c_str(),
                           b.form_of(kArsenal).c_str());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_result_for_each_side);
  RUN_TEST(test_draw);
  RUN_TEST(test_form_is_oldest_to_newest);
  RUN_TEST(test_input_order_does_not_matter);
  RUN_TEST(test_opponents_get_mirrored_results);
  RUN_TEST(test_unplayed_matches_are_ignored);
  RUN_TEST(test_team_with_no_matches_in_window);
  RUN_TEST(test_empty_input);
  RUN_TEST(test_long_window_keeps_every_match);
  RUN_TEST(test_same_kickoff_is_stable_by_id);
  return UNITY_END();
}
