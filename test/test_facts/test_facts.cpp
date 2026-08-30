// ファクト算出の単体テスト (§4.3)。
// 検証すべきケース: form の向き / 引き分け D / 序盤の短い form /
// 順位帯の境界 (17位と18位) / 前回データが無い初回。
#include <unity.h>

#include <string>
#include <vector>

#include "core/facts.h"
#include "core/messages.h"
#include "core/model.h"

using namespace fb;

namespace {

std::vector<Competition> make_comps() {
  std::vector<Competition> c;

  Competition pl;
  pl.key = "premier_league";
  pl.display = "Premier League";
  pl.priority = 3;
  pl.has_standings = true;
  pl.is_domestic = true;
  pl.total_teams = 20;
  // 狭い帯を先に置く (決定事項5)。
  pl.zones = {
      {"首位", 1, 1, false},
      {"CL圏", 1, 4, false},
      {"降格圏", 18, 20, true},
  };
  c.push_back(pl);

  Competition facup;
  facup.key = "fa_cup";
  facup.display = "FA Cup";
  facup.priority = 6;
  facup.is_cup = true;
  c.push_back(facup);

  Competition ucl;
  ucl.key = "champions_league";
  ucl.display = "UEFA Champions League";
  ucl.priority = 1;
  ucl.has_standings = true;
  ucl.is_domestic = false;
  ucl.total_teams = 36;
  c.push_back(ucl);

  return c;
}

StandingRow row(int id, const char* name, int rank, const char* form, int played) {
  StandingRow r;
  r.team_id = id;
  r.team_name = name;
  r.rank = rank;
  r.form = form;
  r.played = played;
  r.points = 0;
  return r;
}

Match match(int comp, int home_id, int away_id, int hg, int ag) {
  Match m;
  m.comp_index = comp;
  m.home_id = home_id;
  m.away_id = away_id;
  m.home_goals = hg;
  m.away_goals = ag;
  m.status = "FT";
  m.fixture_id = home_id * 1000 + away_id;
  return m;
}

}  // namespace

// --- form パース --------------------------------------------------------

void test_trailing_run_latest_at_end() {
  TEST_ASSERT_EQUAL_INT(4, trailing_run("LWWWW", 'W', true));
  TEST_ASSERT_EQUAL_INT(0, trailing_run("WWWWL", 'W', true));
  TEST_ASSERT_EQUAL_INT(1, trailing_run("WWWWL", 'L', true));
}

void test_trailing_run_latest_at_front() {
  // API-FOOTBALL はこちら。実データで確認済み (2026-08-30):
  //   Liverpool 2024 の直近5試合 (古い→新しい) は "WLDLD"、
  //   standings の form は "DLDLW"。form は 新しい→古い。
  TEST_ASSERT_EQUAL_INT(4, trailing_run("WWWWL", 'W', false));
  TEST_ASSERT_EQUAL_INT(0, trailing_run("LWWWW", 'W', false));
}

void test_default_form_direction_matches_api_football() {
  // 既定値が実データの向きから外れたら気づけるようにしておく。
  const FactThresholds th;
  TEST_ASSERT_FALSE(th.form_latest_at_end);
  // 実データそのもの: Liverpool 2024 最終節は引き分けで、直前は敗戦。
  TEST_ASSERT_EQUAL_INT('D', latest_result("DLDLW", th.form_latest_at_end));
  TEST_ASSERT_EQUAL_INT(0, trailing_run("DLDLW", 'W', th.form_latest_at_end));
  TEST_ASSERT_EQUAL_INT(1, trailing_run_not("DLDLW", 'L', th.form_latest_at_end));
}

void test_empty_form() {
  TEST_ASSERT_EQUAL_INT(0, trailing_run("", 'W', true));
  TEST_ASSERT_EQUAL_INT(0, trailing_run_not("", 'L', true));
  TEST_ASSERT_EQUAL_INT(0, latest_result("", true));
}

void test_unbeaten_counts_draws() {
  // 引き分け D は無敗に含める
  TEST_ASSERT_EQUAL_INT(5, trailing_run_not("LWDWDW", 'L', true));
  TEST_ASSERT_EQUAL_INT(0, trailing_run_not("WWWL", 'L', true));
}

// --- ファクト -----------------------------------------------------------

void test_opening_streak() {
  auto comps = make_comps();
  StandingsPool pool;
  pool.comps = &comps;
  LeagueStandings ls;
  ls.comp_index = 0;
  // 全勝なので向きに依存しない。form 長 == played で開幕連勝と判定される。
  ls.rows.push_back(row(1, "Arsenal", 1, "WWW", 3));
  ls.rows.push_back(row(2, "Chelsea", 12, "LDL", 3));
  pool.current.push_back(ls);

  const Fact f = compute_fact(match(0, 1, 2, 2, 1), pool, comps, FactThresholds());
  TEST_ASSERT_EQUAL_INT(FACT_OPENING_STREAK, f.type);
  TEST_ASSERT_EQUAL_INT(3, f.count);
  TEST_ASSERT_EQUAL_STRING("開幕3連勝", msg::fact_text(f).c_str());
}

void test_short_form_early_season_no_streak() {
  // 序盤で form が1文字しかない場合、連勝ファクトは出さない
  auto comps = make_comps();
  StandingsPool pool;
  pool.comps = &comps;
  LeagueStandings ls;
  ls.comp_index = 0;
  ls.rows.push_back(row(1, "Arsenal", 7, "W", 1));
  ls.rows.push_back(row(2, "Chelsea", 11, "L", 1));
  pool.current.push_back(ls);

  const Fact f = compute_fact(match(0, 1, 2, 1, 0), pool, comps, FactThresholds());
  TEST_ASSERT_NOT_EQUAL(FACT_OPENING_STREAK, f.type);
  TEST_ASSERT_NOT_EQUAL(FACT_WIN_STREAK, f.type);
}

void test_win_streak() {
  auto comps = make_comps();
  StandingsPool pool;
  pool.comps = &comps;
  LeagueStandings ls;
  ls.comp_index = 0;
  // form は新しい→古い。先頭3つが W なので3連勝。
  ls.rows.push_back(row(1, "Arsenal", 6, "WWWDL", 12));
  ls.rows.push_back(row(2, "Chelsea", 9, "LDDLW", 12));
  pool.current.push_back(ls);

  const Fact f = compute_fact(match(0, 1, 2, 2, 1), pool, comps, FactThresholds());
  TEST_ASSERT_EQUAL_INT(FACT_WIN_STREAK, f.type);
  TEST_ASSERT_EQUAL_STRING("3連勝", msg::fact_text(f).c_str());
}

void test_streak_broken() {
  auto comps = make_comps();
  StandingsPool pool;
  pool.comps = &comps;
  LeagueStandings ls;
  ls.comp_index = 0;
  // 新しい→古い。最新が W で、その前が LLL なので連敗脱出。
  ls.rows.push_back(row(1, "Arsenal", 8, "WLLLW", 12));
  ls.rows.push_back(row(2, "Chelsea", 9, "LDDWW", 12));
  pool.current.push_back(ls);

  const Fact f = compute_fact(match(0, 1, 2, 1, 0), pool, comps, FactThresholds());
  TEST_ASSERT_EQUAL_INT(FACT_STREAK_BROKEN, f.type);
  TEST_ASSERT_EQUAL_STRING("連敗脱出", msg::fact_text(f).c_str());
}

void test_draw_does_not_break_into_win_streak() {
  auto comps = make_comps();
  StandingsPool pool;
  pool.comps = &comps;
  LeagueStandings ls;
  ls.comp_index = 0;
  ls.rows.push_back(row(1, "Arsenal", 6, "DWWW", 12));  // 最新 (先頭) が D
  ls.rows.push_back(row(2, "Chelsea", 7, "DDDD", 12));
  pool.current.push_back(ls);

  const Fact f = compute_fact(match(0, 1, 2, 1, 1), pool, comps, FactThresholds());
  TEST_ASSERT_NOT_EQUAL(FACT_WIN_STREAK, f.type);
}

// --- 順位帯の境界 (17位と18位) ------------------------------------------

void test_zone_boundary_17_stays_out() {
  auto comps = make_comps();
  StandingsPool pool;
  pool.comps = &comps;
  LeagueStandings now, prev;
  now.comp_index = 0;
  prev.comp_index = 0;
  now.rows.push_back(row(1, "Alavés", 17, "LDLDL", 20));
  prev.rows.push_back(row(1, "Alavés", 16, "DLDLW", 19));
  now.rows.push_back(row(2, "Girona", 10, "WDWDW", 20));
  prev.rows.push_back(row(2, "Girona", 10, "DWDWD", 19));
  pool.current.push_back(now);
  pool.previous.push_back(prev);

  // 16位 → 17位。降格圏 (18-20) には入っていないので ZONE ファクトは出ない。
  const Fact f = compute_fact(match(0, 1, 2, 0, 1), pool, comps, FactThresholds());
  TEST_ASSERT_NOT_EQUAL(FACT_ZONE_ENTER, f.type);
}

void test_zone_boundary_18_falls_in() {
  auto comps = make_comps();
  StandingsPool pool;
  pool.comps = &comps;
  LeagueStandings now, prev;
  now.comp_index = 0;
  prev.comp_index = 0;
  now.rows.push_back(row(1, "Alavés", 18, "LDLDL", 20));
  prev.rows.push_back(row(1, "Alavés", 17, "DLDLW", 19));
  now.rows.push_back(row(2, "Girona", 10, "WDWDW", 20));
  prev.rows.push_back(row(2, "Girona", 10, "DWDWD", 19));
  pool.current.push_back(now);
  pool.previous.push_back(prev);

  const Fact f = compute_fact(match(0, 1, 2, 0, 2), pool, comps, FactThresholds());
  TEST_ASSERT_EQUAL_INT(FACT_ZONE_ENTER, f.type);
  TEST_ASSERT_EQUAL_STRING("降格圏転落", msg::fact_text(f).c_str());
}

void test_zone_enter_positive_uses_narrowest_zone() {
  // 5位 → 1位。首位 (1-1) と CL圏 (1-4) の両方に該当するが、
  // 配列の先頭から評価して最初にマッチした「首位」を採る (決定事項5)。
  auto comps = make_comps();
  StandingsPool pool;
  pool.comps = &comps;
  LeagueStandings now, prev;
  now.comp_index = 0;
  prev.comp_index = 0;
  now.rows.push_back(row(1, "Arsenal", 1, "DWDWW", 20));
  prev.rows.push_back(row(1, "Arsenal", 5, "WDWDW", 19));
  now.rows.push_back(row(2, "Chelsea", 8, "LDLDL", 20));
  prev.rows.push_back(row(2, "Chelsea", 7, "DLDLD", 19));
  pool.current.push_back(now);
  pool.previous.push_back(prev);

  const Fact f = compute_fact(match(0, 1, 2, 2, 0), pool, comps, FactThresholds());
  TEST_ASSERT_EQUAL_INT(FACT_ZONE_ENTER, f.type);
  TEST_ASSERT_EQUAL_STRING("首位浮上", msg::fact_text(f).c_str());
}

// --- 前回データが無い初回 -----------------------------------------------

void test_no_previous_standings_is_not_an_error() {
  auto comps = make_comps();
  StandingsPool pool;
  pool.comps = &comps;
  LeagueStandings now;
  now.comp_index = 0;
  now.rows.push_back(row(1, "Arsenal", 1, "WDWDW", 20));
  now.rows.push_back(row(2, "Chelsea", 9, "LDLDL", 20));
  pool.current.push_back(now);  // previous は空

  const Fact f = compute_fact(match(0, 1, 2, 1, 0), pool, comps, FactThresholds());
  // ZONE / RANK_CHANGE は出ないが、首位は出る。落ちないことが重要。
  TEST_ASSERT_EQUAL_INT(FACT_TOP_OF_TABLE, f.type);
  TEST_ASSERT_EQUAL_STRING("首位", msg::fact_text(f).c_str());
}

void test_unknown_teams_yield_no_fact() {
  auto comps = make_comps();
  StandingsPool pool;
  pool.comps = &comps;  // 順位表なし

  const Fact f = compute_fact(match(0, 99, 98, 1, 0), pool, comps, FactThresholds());
  TEST_ASSERT_EQUAL_INT(FACT_NONE, f.type);
  TEST_ASSERT_EQUAL_STRING("", msg::fact_text(f).c_str());
}

// --- 大勝・番狂わせ -----------------------------------------------------

void test_big_win_without_standings() {
  auto comps = make_comps();
  StandingsPool pool;
  pool.comps = &comps;
  const Fact f = compute_fact(match(0, 99, 98, 5, 0), pool, comps, FactThresholds());
  TEST_ASSERT_EQUAL_INT(FACT_BIG_WIN, f.type);
  TEST_ASSERT_EQUAL_STRING("大勝", msg::fact_text(f).c_str());
}

void test_upset_requires_same_table() {
  auto comps = make_comps();
  StandingsPool pool;
  pool.comps = &comps;
  LeagueStandings ls;
  ls.comp_index = 0;
  ls.rows.push_back(row(1, "Liverpool", 2, "WWDWW", 20));
  ls.rows.push_back(row(2, "Everton", 15, "LDLDW", 20));
  pool.current.push_back(ls);

  // 15位が2位に勝った → 順位差13
  const Fact f = compute_fact(match(0, 1, 2, 0, 3), pool, comps, FactThresholds());
  TEST_ASSERT_EQUAL_INT(FACT_UPSET, f.type);
  TEST_ASSERT_EQUAL_STRING("番狂わせ", msg::fact_text(f).c_str());
}

// --- ジャイアントキリング (§4.1 追補) -----------------------------------

void test_giant_killing_big_scalp() {
  auto comps = make_comps();
  StandingsPool pool;
  pool.comps = &comps;
  LeagueStandings ls;
  ls.comp_index = 0;
  ls.rows.push_back(row(10, "Arsenal", 2, "WWWDW", 20));  // PL の2位
  pool.current.push_back(ls);

  // FA Cup (comp 1)。勝者 777 は順位表に居ない = 下部リーグ。
  const Fact f = compute_fact(match(1, 777, 10, 2, 1), pool, comps, FactThresholds());
  TEST_ASSERT_EQUAL_INT(FACT_GIANT_KILLING, f.type);
  TEST_ASSERT_EQUAL_STRING("大金星", msg::fact_text(f).c_str());
}

void test_giant_killing_mid_table_scalp() {
  auto comps = make_comps();
  StandingsPool pool;
  pool.comps = &comps;
  LeagueStandings ls;
  ls.comp_index = 0;
  ls.rows.push_back(row(10, "Brentford", 9, "WLDWL", 20));
  pool.current.push_back(ls);

  const Fact f = compute_fact(match(1, 777, 10, 1, 0), pool, comps, FactThresholds());
  TEST_ASSERT_EQUAL_INT(FACT_GIANT_KILLING, f.type);
  TEST_ASSERT_EQUAL_STRING("格上撃破", msg::fact_text(f).c_str());
}

void test_no_giant_killing_when_favourite_wins() {
  auto comps = make_comps();
  StandingsPool pool;
  pool.comps = &comps;
  LeagueStandings ls;
  ls.comp_index = 0;
  ls.rows.push_back(row(10, "Arsenal", 2, "WWWDW", 20));
  pool.current.push_back(ls);

  // 順当勝ち。ファクトなし (得失点差3なので大勝でもない)。
  const Fact f = compute_fact(match(1, 10, 777, 3, 0), pool, comps, FactThresholds());
  TEST_ASSERT_NOT_EQUAL(FACT_GIANT_KILLING, f.type);
}

void test_no_giant_killing_between_two_lower_sides() {
  auto comps = make_comps();
  StandingsPool pool;
  pool.comps = &comps;  // どちらも順位表に居ない
  const Fact f = compute_fact(match(1, 777, 888, 1, 0), pool, comps, FactThresholds());
  TEST_ASSERT_EQUAL_INT(FACT_NONE, f.type);
}

void test_giant_killing_only_in_cups() {
  // リーグ戦では成立しない (comp 0 は is_cup=false)
  auto comps = make_comps();
  StandingsPool pool;
  pool.comps = &comps;
  LeagueStandings ls;
  ls.comp_index = 0;
  ls.rows.push_back(row(10, "Arsenal", 2, "WWWDW", 20));
  pool.current.push_back(ls);

  const Fact f = compute_fact(match(0, 777, 10, 1, 0), pool, comps, FactThresholds());
  TEST_ASSERT_NOT_EQUAL(FACT_GIANT_KILLING, f.type);
}

void test_domestic_table_wins_over_ucl_table() {
  // 同じチームが PL と CL の両方の順位表に居る場合、国内順位を採る (§4.1)。
  auto comps = make_comps();
  StandingsPool pool;
  pool.comps = &comps;
  LeagueStandings ucl;
  ucl.comp_index = 2;  // champions_league (is_domestic = false)
  ucl.rows.push_back(row(10, "Arsenal", 30, "LLLLL", 6));
  LeagueStandings pl;
  pl.comp_index = 0;  // premier_league (is_domestic = true)
  pl.rows.push_back(row(10, "Arsenal", 1, "WWWWW", 20));
  pool.current.push_back(ucl);  // 意図的に CL を先に入れる
  pool.current.push_back(pl);

  int ci = -1;
  const StandingRow* r = pool.lookup(10, &ci);
  TEST_ASSERT_NOT_NULL(r);
  TEST_ASSERT_EQUAL_INT(0, ci);
  TEST_ASSERT_EQUAL_INT(1, r->rank);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_trailing_run_latest_at_end);
  RUN_TEST(test_trailing_run_latest_at_front);
  RUN_TEST(test_default_form_direction_matches_api_football);
  RUN_TEST(test_empty_form);
  RUN_TEST(test_unbeaten_counts_draws);
  RUN_TEST(test_opening_streak);
  RUN_TEST(test_short_form_early_season_no_streak);
  RUN_TEST(test_win_streak);
  RUN_TEST(test_streak_broken);
  RUN_TEST(test_draw_does_not_break_into_win_streak);
  RUN_TEST(test_zone_boundary_17_stays_out);
  RUN_TEST(test_zone_boundary_18_falls_in);
  RUN_TEST(test_zone_enter_positive_uses_narrowest_zone);
  RUN_TEST(test_no_previous_standings_is_not_an_error);
  RUN_TEST(test_unknown_teams_yield_no_fact);
  RUN_TEST(test_big_win_without_standings);
  RUN_TEST(test_upset_requires_same_table);
  RUN_TEST(test_giant_killing_big_scalp);
  RUN_TEST(test_giant_killing_mid_table_scalp);
  RUN_TEST(test_no_giant_killing_when_favourite_wins);
  RUN_TEST(test_no_giant_killing_between_two_lower_sides);
  RUN_TEST(test_giant_killing_only_in_cups);
  RUN_TEST(test_domestic_table_wins_over_ucl_table);
  return UNITY_END();
}
