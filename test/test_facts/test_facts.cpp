// ファクト算出の単体テスト (§4.3)。
// 結果列は API の form ではなく FormTable が試合から組み立てたものを使う (§2.4)。
//
// 検証すべきケース: 結果列の並び順 / 引き分け D / 序盤で結果列が短い場合 /
// 45日窓に1試合も入らないチーム / 順位帯の境界 (17位と18位) /
// 前回データが存在しない初回。
#include <unity.h>

#include <string>
#include <vector>

#include "core/datetime.h"
#include "core/facts.h"
#include "core/form.h"
#include "core/messages.h"
#include "core/model.h"

using namespace fb;

namespace {

constexpr int kHomeTeam = 57;
constexpr int kAwayTeam = 61;

std::vector<Competition> make_comps() {
  std::vector<Competition> c;

  Competition pl;
  pl.key = "premier_league";
  pl.display = "Premier League";
  pl.code = "PL";
  pl.priority = 2;
  pl.has_standings = true;
  pl.is_domestic = true;
  pl.total_teams = 20;
  // 狭い帯を先に置く (決定事項5)。
  pl.zones = {
      {"Top", 1, 1, false},
      {"UCL", 1, 4, false},
      {"Relegation", 18, 20, true},
  };
  c.push_back(pl);

  // カップ戦は無料枠に含まれないので出荷対象外だが、competitions.json は
  // データ駆動なのでジャイアントキリングの判定自体は残してある (SPEC §2.1)。
  Competition cup;
  cup.key = "some_cup";
  cup.display = "Cup";
  cup.code = "XX";
  cup.priority = 9;
  cup.is_cup = true;
  cup.is_domestic = true;
  c.push_back(cup);

  Competition ucl;
  ucl.key = "champions_league";
  ucl.display = "UEFA Champions League";
  ucl.code = "CL";
  ucl.priority = 1;
  ucl.has_standings = true;
  ucl.is_domestic = false;
  ucl.total_teams = 36;
  c.push_back(ucl);

  return c;
}

StandingRow row(int id, const char* name, int rank, int played) {
  StandingRow r;
  r.team_id = id;
  r.team_name = name;
  r.rank = rank;
  r.played = played;
  return r;
}

// 指定した結果列になるダミー試合を積む。form は古い→新しい。
// 対戦相手は毎回変えて、相手側の結果列を汚さないようにする。
void add_form(std::vector<Match>& out, int team_id, const char* form) {
  std::time_t t = make_utc(2026, 7, 1);
  int opp = 900000 + team_id * 100;
  long id = 1000000L + team_id * 1000L;
  for (const char* p = form; *p; ++p, ++opp, ++id, t += 7 * 86400) {
    Match m;
    m.fixture_id = id;
    m.kickoff_utc = t;
    m.status = "FINISHED";
    m.home_id = team_id;
    m.away_id = opp;
    m.home_name = "H";
    m.away_name = "A";
    switch (*p) {
      case 'W': m.home_goals = 1; m.away_goals = 0; break;
      case 'L': m.home_goals = 0; m.away_goals = 1; break;
      default:  m.home_goals = 1; m.away_goals = 1; break;
    }
    out.push_back(m);
  }
}

Match match(int comp, int home_id, int away_id, int hg, int ag) {
  Match m;
  m.comp_index = comp;
  m.home_id = home_id;
  m.away_id = away_id;
  m.home_goals = hg;
  m.away_goals = ag;
  m.status = "FINISHED";
  m.fixture_id = 7777;
  m.kickoff_utc = make_utc(2026, 9, 1);
  return m;
}

// テスト1件ぶんの入力をまとめる。
struct Scene {
  std::vector<Competition> comps = make_comps();
  std::vector<Match> history;
  StandingsPool pool;
  FormTable forms;

  Scene() { pool.comps = &comps; }
  void finish() { forms.build(history); }
  Fact fact_of(const Match& m, const FactThresholds& th = FactThresholds()) {
    return compute_fact(m, pool, forms, comps, th);
  }
};

}  // namespace

// --- 結果列のパース ------------------------------------------------------

void test_trailing_run_latest_at_end() {
  // FormTable は末尾が最新。既定はこちら。
  TEST_ASSERT_EQUAL_INT(4, trailing_run("LWWWW", 'W', true));
  TEST_ASSERT_EQUAL_INT(0, trailing_run("WWWWL", 'W', true));
  TEST_ASSERT_EQUAL_INT(1, trailing_run("WWWWL", 'L', true));
}

void test_default_direction_is_latest_at_end() {
  // 既定値が FormTable の並び (日付昇順) から外れたら落ちる。
  const FactThresholds th;
  TEST_ASSERT_TRUE(th.form_latest_at_end);
  TEST_ASSERT_EQUAL_INT('L', latest_result("WWWWL", th.form_latest_at_end));
}

void test_empty_form() {
  TEST_ASSERT_EQUAL_INT(0, trailing_run("", 'W', true));
  TEST_ASSERT_EQUAL_INT(0, trailing_run_not("", 'L', true));
  TEST_ASSERT_EQUAL_INT(0, latest_result("", true));
}

void test_unbeaten_counts_draws() {
  TEST_ASSERT_EQUAL_INT(5, trailing_run_not("LWDWDW", 'L', true));
  TEST_ASSERT_EQUAL_INT(0, trailing_run_not("WWWL", 'L', true));
}

// --- ファクト -----------------------------------------------------------

void test_opening_streak() {
  Scene s;
  add_form(s.history, kHomeTeam, "WWW");
  add_form(s.history, kAwayTeam, "LDL");
  LeagueStandings ls;
  ls.comp_index = 0;
  ls.rows.push_back(row(kHomeTeam, "Arsenal", 1, 3));
  ls.rows.push_back(row(kAwayTeam, "Chelsea", 12, 3));
  s.pool.current.push_back(ls);
  s.finish();

  const Fact f = s.fact_of(match(0, kHomeTeam, kAwayTeam, 2, 1));
  TEST_ASSERT_EQUAL_INT(FACT_OPENING_STREAK, f.type);
  TEST_ASSERT_EQUAL_INT(3, f.count);
  TEST_ASSERT_EQUAL_STRING("Won all 3", msg::fact_text(f).c_str());
}

void test_opening_streak_needs_window_to_cover_season() {
  // 45日窓が全試合を覆っていない (played=12 に対し結果列は5)。
  // 「開幕」とは言えないので WIN_STREAK に落ちる。
  Scene s;
  add_form(s.history, kHomeTeam, "WWWWW");
  add_form(s.history, kAwayTeam, "LDLDL");
  LeagueStandings ls;
  ls.comp_index = 0;
  ls.rows.push_back(row(kHomeTeam, "Arsenal", 3, 12));
  ls.rows.push_back(row(kAwayTeam, "Chelsea", 12, 12));
  s.pool.current.push_back(ls);
  s.finish();

  const Fact f = s.fact_of(match(0, kHomeTeam, kAwayTeam, 2, 1));
  TEST_ASSERT_NOT_EQUAL(FACT_OPENING_STREAK, f.type);
  TEST_ASSERT_EQUAL_INT(FACT_WIN_STREAK, f.type);
  TEST_ASSERT_EQUAL_STRING("5 wins in a row", msg::fact_text(f).c_str());
}

void test_streak_beats_top_of_table() {
  // 首位は毎日同じ表示になるので、動きのある連勝を優先する (§4.2)。
  Scene s;
  add_form(s.history, kHomeTeam, "WWWWW");
  add_form(s.history, kAwayTeam, "LDLDL");
  LeagueStandings ls;
  ls.comp_index = 0;
  ls.rows.push_back(row(kHomeTeam, "Arsenal", 1, 12));
  ls.rows.push_back(row(kAwayTeam, "Chelsea", 12, 12));
  s.pool.current.push_back(ls);
  s.finish();

  const Fact f = s.fact_of(match(0, kHomeTeam, kAwayTeam, 2, 1));
  TEST_ASSERT_EQUAL_INT(FACT_WIN_STREAK, f.type);
  TEST_ASSERT_EQUAL_STRING("5 wins in a row", msg::fact_text(f).c_str());
}

void test_short_form_early_season_no_streak() {
  Scene s;
  add_form(s.history, kHomeTeam, "W");
  add_form(s.history, kAwayTeam, "L");
  LeagueStandings ls;
  ls.comp_index = 0;
  ls.rows.push_back(row(kHomeTeam, "Arsenal", 7, 1));
  ls.rows.push_back(row(kAwayTeam, "Chelsea", 11, 1));
  s.pool.current.push_back(ls);
  s.finish();

  const Fact f = s.fact_of(match(0, kHomeTeam, kAwayTeam, 1, 0));
  TEST_ASSERT_NOT_EQUAL(FACT_OPENING_STREAK, f.type);
  TEST_ASSERT_NOT_EQUAL(FACT_WIN_STREAK, f.type);
}

void test_win_streak() {
  Scene s;
  add_form(s.history, kHomeTeam, "LDWWW");  // 末尾が最新 → 3連勝
  add_form(s.history, kAwayTeam, "WLDDL");
  LeagueStandings ls;
  ls.comp_index = 0;
  ls.rows.push_back(row(kHomeTeam, "Arsenal", 6, 12));
  ls.rows.push_back(row(kAwayTeam, "Chelsea", 9, 12));
  s.pool.current.push_back(ls);
  s.finish();

  const Fact f = s.fact_of(match(0, kHomeTeam, kAwayTeam, 2, 1));
  TEST_ASSERT_EQUAL_INT(FACT_WIN_STREAK, f.type);
  TEST_ASSERT_EQUAL_STRING("3 wins in a row", msg::fact_text(f).c_str());
}

void test_streak_broken() {
  Scene s;
  add_form(s.history, kHomeTeam, "WLLLW");  // 3連敗のあと勝ち
  add_form(s.history, kAwayTeam, "WWDDL");
  LeagueStandings ls;
  ls.comp_index = 0;
  ls.rows.push_back(row(kHomeTeam, "Arsenal", 8, 12));
  ls.rows.push_back(row(kAwayTeam, "Chelsea", 9, 12));
  s.pool.current.push_back(ls);
  s.finish();

  const Fact f = s.fact_of(match(0, kHomeTeam, kAwayTeam, 1, 0));
  TEST_ASSERT_EQUAL_INT(FACT_STREAK_BROKEN, f.type);
  TEST_ASSERT_EQUAL_STRING("Losing run ends", msg::fact_text(f).c_str());
}

void test_draw_does_not_break_into_win_streak() {
  Scene s;
  add_form(s.history, kHomeTeam, "WWWD");  // 最新が D
  add_form(s.history, kAwayTeam, "DDDD");
  LeagueStandings ls;
  ls.comp_index = 0;
  ls.rows.push_back(row(kHomeTeam, "Arsenal", 6, 12));
  ls.rows.push_back(row(kAwayTeam, "Chelsea", 7, 12));
  s.pool.current.push_back(ls);
  s.finish();

  const Fact f = s.fact_of(match(0, kHomeTeam, kAwayTeam, 1, 1));
  TEST_ASSERT_NOT_EQUAL(FACT_WIN_STREAK, f.type);
}

void test_team_missing_from_form_window() {
  // 45日窓に1試合も無いチーム (§4.3)。連勝は出ないが順位ファクトは出る。
  Scene s;  // history は空
  LeagueStandings ls;
  ls.comp_index = 0;
  ls.rows.push_back(row(kHomeTeam, "Arsenal", 1, 12));
  ls.rows.push_back(row(kAwayTeam, "Chelsea", 9, 12));
  s.pool.current.push_back(ls);
  s.finish();

  const Fact f = s.fact_of(match(0, kHomeTeam, kAwayTeam, 1, 0));
  TEST_ASSERT_EQUAL_INT(FACT_TOP_OF_TABLE, f.type);
  TEST_ASSERT_EQUAL_STRING("Top of the table", msg::fact_text(f).c_str());
}

// --- 順位帯の境界 (17位と18位) ------------------------------------------

void test_zone_boundary_17_stays_out() {
  Scene s;
  LeagueStandings now, prev;
  now.comp_index = 0;
  prev.comp_index = 0;
  now.rows.push_back(row(kHomeTeam, "Alavés", 17, 20));
  prev.rows.push_back(row(kHomeTeam, "Alavés", 16, 19));
  now.rows.push_back(row(kAwayTeam, "Girona", 10, 20));
  prev.rows.push_back(row(kAwayTeam, "Girona", 10, 19));
  s.pool.current.push_back(now);
  s.pool.previous.push_back(prev);
  s.finish();

  // 16位 → 17位。降格圏 (18-20) には入っていない。
  const Fact f = s.fact_of(match(0, kHomeTeam, kAwayTeam, 0, 1));
  TEST_ASSERT_NOT_EQUAL(FACT_ZONE_ENTER, f.type);
}

void test_zone_boundary_18_falls_in() {
  Scene s;
  LeagueStandings now, prev;
  now.comp_index = 0;
  prev.comp_index = 0;
  now.rows.push_back(row(kHomeTeam, "Alavés", 18, 20));
  prev.rows.push_back(row(kHomeTeam, "Alavés", 17, 19));
  now.rows.push_back(row(kAwayTeam, "Girona", 10, 20));
  prev.rows.push_back(row(kAwayTeam, "Girona", 10, 19));
  s.pool.current.push_back(now);
  s.pool.previous.push_back(prev);
  s.finish();

  const Fact f = s.fact_of(match(0, kHomeTeam, kAwayTeam, 0, 2));
  TEST_ASSERT_EQUAL_INT(FACT_ZONE_ENTER, f.type);
  TEST_ASSERT_EQUAL_STRING("Down into Relegation", msg::fact_text(f).c_str());
}

void test_zone_enter_positive_uses_narrowest_zone() {
  // 5位 → 1位。首位 (1-1) と CL圏 (1-4) の両方に該当するが、
  // 配列の先頭から評価して最初にマッチした「首位」を採る (決定事項5)。
  Scene s;
  LeagueStandings now, prev;
  now.comp_index = 0;
  prev.comp_index = 0;
  now.rows.push_back(row(kHomeTeam, "Arsenal", 1, 20));
  prev.rows.push_back(row(kHomeTeam, "Arsenal", 5, 19));
  now.rows.push_back(row(kAwayTeam, "Chelsea", 8, 20));
  prev.rows.push_back(row(kAwayTeam, "Chelsea", 7, 19));
  s.pool.current.push_back(now);
  s.pool.previous.push_back(prev);
  s.finish();

  const Fact f = s.fact_of(match(0, kHomeTeam, kAwayTeam, 2, 0));
  TEST_ASSERT_EQUAL_INT(FACT_ZONE_ENTER, f.type);
  TEST_ASSERT_EQUAL_STRING("Up into Top", msg::fact_text(f).c_str());
}

// --- 前回データが無い初回 -----------------------------------------------

void test_no_previous_standings_is_not_an_error() {
  Scene s;
  LeagueStandings now;
  now.comp_index = 0;
  now.rows.push_back(row(kHomeTeam, "Arsenal", 1, 20));
  now.rows.push_back(row(kAwayTeam, "Chelsea", 9, 20));
  s.pool.current.push_back(now);  // previous は空
  s.finish();

  const Fact f = s.fact_of(match(0, kHomeTeam, kAwayTeam, 1, 0));
  TEST_ASSERT_EQUAL_INT(FACT_TOP_OF_TABLE, f.type);
}

void test_unknown_teams_yield_no_fact() {
  Scene s;
  s.finish();
  const Fact f = s.fact_of(match(0, 99, 98, 1, 0));
  TEST_ASSERT_EQUAL_INT(FACT_NONE, f.type);
  TEST_ASSERT_EQUAL_STRING("", msg::fact_text(f).c_str());
}

// --- 大勝・番狂わせ -----------------------------------------------------

void test_big_win_without_standings() {
  Scene s;
  s.finish();
  const Fact f = s.fact_of(match(0, 99, 98, 5, 0));
  TEST_ASSERT_EQUAL_INT(FACT_BIG_WIN, f.type);
  TEST_ASSERT_EQUAL_STRING("Big win", msg::fact_text(f).c_str());
}

void test_upset_requires_same_table() {
  Scene s;
  LeagueStandings ls;
  ls.comp_index = 0;
  ls.rows.push_back(row(kHomeTeam, "Liverpool", 2, 20));
  ls.rows.push_back(row(kAwayTeam, "Everton", 15, 20));
  s.pool.current.push_back(ls);
  s.finish();

  // 15位が2位に勝った → 順位差13
  const Fact f = s.fact_of(match(0, kHomeTeam, kAwayTeam, 0, 3));
  TEST_ASSERT_EQUAL_INT(FACT_UPSET, f.type);
  TEST_ASSERT_EQUAL_STRING("Upset", msg::fact_text(f).c_str());
}

// --- ジャイアントキリング -----------------------------------------------
// 無料枠にカップ戦が無いため出荷構成では発火しないが、competitions.json は
// データ駆動なので、カップを足せば動くことをテストで固定しておく。

void test_giant_killing_big_scalp() {
  Scene s;
  LeagueStandings ls;
  ls.comp_index = 0;
  ls.rows.push_back(row(10, "Arsenal", 2, 20));  // PL の2位
  s.pool.current.push_back(ls);
  s.finish();

  // comp 1 = カップ戦。勝者 777 は順位表に居ない = 下部リーグ。
  const Fact f = s.fact_of(match(1, 777, 10, 2, 1));
  TEST_ASSERT_EQUAL_INT(FACT_GIANT_KILLING, f.type);
  TEST_ASSERT_EQUAL_STRING("Huge upset", msg::fact_text(f).c_str());
}

void test_giant_killing_mid_table_scalp() {
  Scene s;
  LeagueStandings ls;
  ls.comp_index = 0;
  ls.rows.push_back(row(10, "Brentford", 9, 20));
  s.pool.current.push_back(ls);
  s.finish();

  const Fact f = s.fact_of(match(1, 777, 10, 1, 0));
  TEST_ASSERT_EQUAL_INT(FACT_GIANT_KILLING, f.type);
  TEST_ASSERT_EQUAL_STRING("Beat a top side", msg::fact_text(f).c_str());
}

void test_no_giant_killing_when_favourite_wins() {
  Scene s;
  LeagueStandings ls;
  ls.comp_index = 0;
  ls.rows.push_back(row(10, "Arsenal", 2, 20));
  s.pool.current.push_back(ls);
  s.finish();

  const Fact f = s.fact_of(match(1, 10, 777, 3, 0));
  TEST_ASSERT_NOT_EQUAL(FACT_GIANT_KILLING, f.type);
}

void test_giant_killing_only_in_cups() {
  Scene s;
  LeagueStandings ls;
  ls.comp_index = 0;
  ls.rows.push_back(row(10, "Arsenal", 2, 20));
  s.pool.current.push_back(ls);
  s.finish();

  // comp 0 はリーグ戦 (is_cup=false)
  const Fact f = s.fact_of(match(0, 777, 10, 1, 0));
  TEST_ASSERT_NOT_EQUAL(FACT_GIANT_KILLING, f.type);
}

void test_domestic_table_wins_over_ucl_table() {
  // 同じチームが PL と CL の両方の順位表に居る場合、国内順位を採る (§4.1)。
  Scene s;
  LeagueStandings ucl;
  ucl.comp_index = 2;  // champions_league (is_domestic = false)
  ucl.rows.push_back(row(10, "Arsenal", 30, 6));
  LeagueStandings pl;
  pl.comp_index = 0;  // premier_league (is_domestic = true)
  pl.rows.push_back(row(10, "Arsenal", 1, 20));
  s.pool.current.push_back(ucl);  // 意図的に CL を先に入れる
  s.pool.current.push_back(pl);
  s.finish();

  int ci = -1;
  const StandingRow* r = s.pool.lookup(10, &ci);
  TEST_ASSERT_NOT_NULL(r);
  TEST_ASSERT_EQUAL_INT(0, ci);
  TEST_ASSERT_EQUAL_INT(1, r->rank);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_trailing_run_latest_at_end);
  RUN_TEST(test_default_direction_is_latest_at_end);
  RUN_TEST(test_empty_form);
  RUN_TEST(test_unbeaten_counts_draws);
  RUN_TEST(test_opening_streak);
  RUN_TEST(test_opening_streak_needs_window_to_cover_season);
  RUN_TEST(test_streak_beats_top_of_table);
  RUN_TEST(test_short_form_early_season_no_streak);
  RUN_TEST(test_win_streak);
  RUN_TEST(test_streak_broken);
  RUN_TEST(test_draw_does_not_break_into_win_streak);
  RUN_TEST(test_team_missing_from_form_window);
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
  RUN_TEST(test_giant_killing_only_in_cups);
  RUN_TEST(test_domestic_table_wins_over_ucl_table);
  return UNITY_END();
}
