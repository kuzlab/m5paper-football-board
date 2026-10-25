// 前回順位の持ち越し。順位表は「新しい結果があったリーグ」の分しか取得しない
// ため、保存で丸ごと置き換えると次にそのリーグの結果が出た日に差分が取れず、
// 「Up N places」も昇降格ラインの出入りも出なくなる。
// 2026-10-25 の実機で premier_league だけ取得した回に bundesliga と laliga の
// 前回順位が消えていた。
#include <unity.h>

#include <string>
#include <vector>

#include "core/config_parse.h"

using namespace fb;

namespace {

LeagueStandings league(int comp_index, int first_team_id, int first_rank) {
  LeagueStandings ls;
  ls.comp_index = comp_index;
  for (int i = 0; i < 3; ++i) {
    StandingRow r;
    r.team_id = first_team_id + i;
    r.rank = first_rank + i;
    r.played = 10;
    r.points = 20 - i;
    r.team_name = "T";
    ls.rows.push_back(r);
  }
  return ls;
}

const LeagueStandings* find(const std::vector<LeagueStandings>& v, int comp_index) {
  for (const auto& l : v) {
    if (l.comp_index == comp_index) return &l;
  }
  return nullptr;
}

std::vector<Competition> comps() {
  std::vector<Competition> c(3);
  c[0].key = "premier_league";
  c[1].key = "bundesliga";
  c[2].key = "laliga";
  return c;
}

}  // namespace

void test_untouched_leagues_survive() {
  std::vector<LeagueStandings> prev = {league(0, 100, 1), league(1, 200, 1),
                                       league(2, 300, 1)};
  merge_standings_cache(prev, {league(0, 100, 5)});
  TEST_ASSERT_EQUAL_INT(3, (int)prev.size());
  TEST_ASSERT_EQUAL_INT(5, find(prev, 0)->rows[0].rank);  // 差し替わった
  TEST_ASSERT_EQUAL_INT(1, find(prev, 1)->rows[0].rank);  // 残っている
  TEST_ASSERT_EQUAL_INT(1, find(prev, 2)->rows[0].rank);
}

void test_new_league_is_appended() {
  std::vector<LeagueStandings> prev = {league(0, 100, 1)};
  merge_standings_cache(prev, {league(2, 300, 7)});
  TEST_ASSERT_EQUAL_INT(2, (int)prev.size());
  TEST_ASSERT_EQUAL_INT(7, find(prev, 2)->rows[0].rank);
}

void test_empty_standings_do_not_overwrite() {
  // 取得はしたが0行だった回に前回順位を捨てない。
  std::vector<LeagueStandings> prev = {league(0, 100, 1)};
  LeagueStandings empty;
  empty.comp_index = 0;
  merge_standings_cache(prev, {empty});
  TEST_ASSERT_EQUAL_INT(1, (int)prev.size());
  TEST_ASSERT_EQUAL_INT(3, (int)find(prev, 0)->rows.size());
}

void test_unknown_league_is_ignored() {
  std::vector<LeagueStandings> prev = {league(0, 100, 1)};
  LeagueStandings bad = league(-1, 400, 1);
  merge_standings_cache(prev, {bad});
  TEST_ASSERT_EQUAL_INT(1, (int)prev.size());
}

void test_merge_survives_a_save_load_round_trip() {
  // 実機の経路どおり: 読み込む → 差し替える → 書き出す → また読み込む。
  const auto c = comps();
  std::vector<LeagueStandings> day1 = {league(0, 100, 1), league(1, 200, 1)};
  const std::string json1 = serialize_standings_cache(day1, c);

  std::vector<LeagueStandings> loaded;
  TEST_ASSERT_TRUE(parse_standings_cache(json1.c_str(), json1.size(), c, loaded));
  merge_standings_cache(loaded, {league(0, 100, 9)});  // 翌日は PL だけ取得
  const std::string json2 = serialize_standings_cache(loaded, c);

  std::vector<LeagueStandings> day2;
  TEST_ASSERT_TRUE(parse_standings_cache(json2.c_str(), json2.size(), c, day2));
  TEST_ASSERT_EQUAL_INT(2, (int)day2.size());
  TEST_ASSERT_EQUAL_INT(9, find(day2, 0)->rows[0].rank);
  TEST_ASSERT_EQUAL_INT(1, find(day2, 1)->rows[0].rank);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_untouched_leagues_survive);
  RUN_TEST(test_new_league_is_appended);
  RUN_TEST(test_empty_standings_do_not_overwrite);
  RUN_TEST(test_unknown_league_is_ignored);
  RUN_TEST(test_merge_survives_a_save_load_round_trip);
  return UNITY_END();
}
