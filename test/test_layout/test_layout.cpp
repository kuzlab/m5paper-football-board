// 防御的レイアウトと溢れ処理 (§5.2, §5.3)。
// 幅の実測はダミーに差し替える。半角=12px / 全角=24px の等幅として扱う。
#include <unity.h>

#include <string>
#include <vector>

#include "core/selector.h"
#include "core/text_util.h"

using namespace fb;

namespace {

// 28px フォント想定のダミー実測。ASCII は 14px、それ以外は 28px。
int measure_name(const std::string& s) {
  int w = 0;
  for (const auto& ch : utf8_split(s)) {
    w += (ch.size() == 1) ? 14 : 28;
  }
  return w;
}

// 20px フォント想定。
int measure_fact(const std::string& s) {
  int w = 0;
  for (const auto& ch : utf8_split(s)) {
    w += (ch.size() == 1) ? 10 : 20;
  }
  return w;
}

Measures make_measures() {
  Measures m;
  m.name = measure_name;
  m.heading = measure_name;
  m.fact = measure_fact;
  return m;
}

std::vector<Competition> make_comps() {
  std::vector<Competition> c;
  Competition ucl;
  ucl.key = "champions_league";
  ucl.display = "UEFA Champions League";
  ucl.priority = 1;
  c.push_back(ucl);

  Competition pl;
  pl.key = "premier_league";
  pl.display = "Premier League";
  pl.priority = 3;
  c.push_back(pl);

  Competition bl;
  bl.key = "bundesliga";
  bl.display = "Bundesliga";
  bl.priority = 4;
  c.push_back(bl);
  return c;
}

Entry make_entry(int comp, const char* home, const char* away, int hg, int ag,
                 std::time_t ko, long id) {
  Entry e;
  e.match.comp_index = comp;
  e.match.home_name = home;
  e.match.away_name = away;
  e.match.home_goals = hg;
  e.match.away_goals = ag;
  e.match.kickoff_utc = ko;
  e.match.fixture_id = id;
  e.match.status = "FT";
  return e;
}

}  // namespace

// --- UTF-8 と切り詰め ---------------------------------------------------

void test_utf8_split_handles_multibyte() {
  const auto v = utf8_split("Alavés");
  TEST_ASSERT_EQUAL_INT(6, static_cast<int>(v.size()));
  TEST_ASSERT_EQUAL_STRING("é", v[4].c_str());
}

void test_no_truncation_when_it_fits() {
  TEST_ASSERT_EQUAL_STRING("Arsenal",
                           truncate_to_width("Arsenal", 258, measure_name).c_str());
}

void test_long_german_name_is_truncated_with_ellipsis() {
  // 24文字 x 14px = 336px > 258px。切り詰められて … が付く。
  const std::string out =
      truncate_to_width("Borussia Mönchengladbach", 258, measure_name);
  TEST_ASSERT_TRUE(out != "Borussia Mönchengladbach");
  TEST_ASSERT_TRUE(measure_name(out) <= 258);
  // 末尾が … であること
  TEST_ASSERT_TRUE(out.size() >= 3);
  TEST_ASSERT_EQUAL_STRING("…", out.substr(out.size() - 3).c_str());
}

void test_truncation_is_by_pixels_not_characters() {
  // 全角7文字 (=196px) と半角14文字 (=196px) が同じ幅で切られる。
  // 文字数で判定していたらここが壊れる。
  const std::string wide = truncate_to_width("首位浮上首位浮上首位浮上", 200, measure_name);
  const std::string narrow = truncate_to_width("abcdefghijklmnopqrst", 200, measure_name);
  TEST_ASSERT_TRUE(measure_name(wide) <= 200);
  TEST_ASSERT_TRUE(measure_name(narrow) <= 200);
}

void test_truncate_never_exceeds_width_for_any_prefix() {
  const std::string src = "Real Sociedad de Fútbol";
  for (int w = 0; w <= 400; w += 7) {
    const std::string out = truncate_to_width(src, w, measure_name);
    TEST_ASSERT_TRUE(measure_name(out) <= w);
  }
}

// --- グリフのフォールバック (§5.4) --------------------------------------

void test_latin_extended_a_is_kept() {
  // Beşiktaş の ş は U+015F (Latin Extended-A)。VLW に含めるので保持する。
  TEST_ASSERT_EQUAL_STRING("Beşiktaş", fold_unsupported("Beşiktaş").c_str());
  TEST_ASSERT_EQUAL_STRING("Kraków", fold_unsupported("Kraków").c_str());
  TEST_ASSERT_EQUAL_STRING("Ferencváros", fold_unsupported("Ferencváros").c_str());
}

void test_latin1_umlaut_is_kept() {
  TEST_ASSERT_EQUAL_STRING("Mönchengladbach",
                           fold_unsupported("Mönchengladbach").c_str());
  TEST_ASSERT_EQUAL_STRING("Alavés", fold_unsupported("Alavés").c_str());
}

void test_romanian_comma_below_is_folded() {
  // Ș/ș は Latin Extended-B。収録していないので S/s に畳む。
  TEST_ASSERT_EQUAL_STRING("Steaua Sud", fold_unsupported("Șteaua Sud").c_str());
}

void test_cyrillic_becomes_question_marks_not_garbage() {
  const std::string out = fold_unsupported("Црвена");
  TEST_ASSERT_EQUAL_STRING("??????", out.c_str());
}

void test_japanese_is_kept() {
  TEST_ASSERT_EQUAL_STRING("開幕3連勝", fold_unsupported("開幕3連勝").c_str());
  TEST_ASSERT_EQUAL_STRING("更新 → 側面ボタン",
                           fold_unsupported("更新 → 側面ボタン").c_str());
}

// --- 並び替え -----------------------------------------------------------

void test_sort_by_competition_then_kickoff() {
  auto comps = make_comps();
  std::vector<Entry> v = {
      make_entry(2, "Bayern", "Union", 4, 0, 1000, 1),      // Bundesliga (p4)
      make_entry(1, "Arsenal", "Chelsea", 2, 1, 500, 2),    // PL (p3), 古い
      make_entry(1, "Liverpool", "Everton", 0, 3, 2000, 3), // PL (p3), 新しい
      make_entry(0, "Real", "PSG", 1, 1, 100, 4),           // UCL (p1)
  };
  sort_entries(v, comps);
  TEST_ASSERT_EQUAL_INT(0, v[0].match.comp_index);   // UCL が先頭
  TEST_ASSERT_EQUAL_INT(1, v[1].match.comp_index);
  TEST_ASSERT_EQUAL_STRING("Liverpool", v[1].match.home_name.c_str());  // 新しい方が先
  TEST_ASSERT_EQUAL_STRING("Arsenal", v[2].match.home_name.c_str());
  TEST_ASSERT_EQUAL_INT(2, v[3].match.comp_index);   // Bundesliga が最後
}

// --- 溢れ処理 -----------------------------------------------------------

void test_plan_fits_within_screen() {
  auto comps = make_comps();
  LayoutMetrics lm;
  std::vector<Entry> v;
  for (int i = 0; i < 40; ++i) {
    v.push_back(make_entry(i % 3, "Home", "Away", 1, 0, 1000 - i, i));
  }
  sort_entries(v, comps);
  const RenderPlan p = build_plan(v, comps, lm, make_measures());

  TEST_ASSERT_TRUE(p.has_overflow());
  for (const auto& r : p.rows) {
    const int h = (r.kind == PlanRow::kHeading) ? lm.heading_h : lm.row_h;
    // はみ出す行は最初から作らない (§5.3)
    TEST_ASSERT_TRUE(r.y + h <= lm.content_bottom);
    TEST_ASSERT_TRUE(r.y >= lm.content_top);
  }
  TEST_ASSERT_TRUE(p.overflow_y + lm.overflow_h <= lm.content_bottom);
}

void test_heading_is_never_orphaned() {
  // 見出しだけ描いて試合が0件、という行が生まれないこと (§5.2)
  auto comps = make_comps();
  LayoutMetrics lm;
  for (int n = 1; n <= 30; ++n) {
    std::vector<Entry> v;
    for (int i = 0; i < n; ++i) {
      v.push_back(make_entry(i % 3, "Home", "Away", 1, 0, 1000 - i, i));
    }
    sort_entries(v, comps);
    const RenderPlan p = build_plan(v, comps, lm, make_measures());
    for (std::size_t i = 0; i < p.rows.size(); ++i) {
      if (p.rows[i].kind != PlanRow::kHeading) continue;
      TEST_ASSERT_TRUE(i + 1 < p.rows.size());
      TEST_ASSERT_TRUE(p.rows[i + 1].kind == PlanRow::kMatch);
    }
  }
}

void test_no_overflow_when_everything_fits() {
  auto comps = make_comps();
  LayoutMetrics lm;
  std::vector<Entry> v = {
      make_entry(0, "Real", "PSG", 1, 1, 100, 1),
      make_entry(1, "Arsenal", "Chelsea", 2, 1, 200, 2),
  };
  sort_entries(v, comps);
  const RenderPlan p = build_plan(v, comps, lm, make_measures());
  TEST_ASSERT_FALSE(p.has_overflow());
  TEST_ASSERT_EQUAL_INT(4, static_cast<int>(p.rows.size()));  // 見出し2 + 試合2
}

void test_columns_are_truncated_independently() {
  auto comps = make_comps();
  LayoutMetrics lm;
  std::vector<Entry> v = {make_entry(1, "Borussia Mönchengladbach",
                                     "Bayer 04 Leverkusen Fussball", 3, 2, 100, 1)};
  Entry& e = v[0];
  e.fact.type = FACT_UNBEATEN;
  e.fact.count = 12;

  const RenderPlan p = build_plan(v, comps, lm, make_measures());
  const PlanRow& row = p.rows[1];
  TEST_ASSERT_TRUE(measure_name(row.home) <= lm.col_home_w);
  TEST_ASSERT_TRUE(measure_name(row.away) <= lm.col_away_w);
  TEST_ASSERT_TRUE(measure_name(row.score) <= lm.col_score_w);
  TEST_ASSERT_TRUE(measure_fact(row.fact) <= lm.col_fact_w);
  TEST_ASSERT_EQUAL_STRING("3 - 2", row.score.c_str());
}

void test_plan_hash_changes_with_content() {
  auto comps = make_comps();
  LayoutMetrics lm;
  std::vector<Entry> a = {make_entry(1, "Arsenal", "Chelsea", 2, 1, 100, 1)};
  std::vector<Entry> b = {make_entry(1, "Arsenal", "Chelsea", 2, 2, 100, 1)};
  const auto ha = plan_hash(build_plan(a, comps, lm, make_measures()));
  const auto hb = plan_hash(build_plan(b, comps, lm, make_measures()));
  TEST_ASSERT_NOT_EQUAL(ha, hb);
  const auto ha2 = plan_hash(build_plan(a, comps, lm, make_measures()));
  TEST_ASSERT_EQUAL_UINT32(ha, ha2);
}

void test_seen_matches_are_deprioritised_not_dropped() {
  // 72時間ウィンドウの再掲。既出は同じ競技会内で後ろに回すが、捨てない。
  auto comps = make_comps();
  std::vector<Entry> v = {
      make_entry(1, "Old", "Match", 1, 0, 5000, 1),   // 新しいが既出
      make_entry(1, "New", "Match", 2, 0, 1000, 2),   // 古いが未見
  };
  v[0].seen = true;
  sort_entries(v, comps);
  TEST_ASSERT_EQUAL_STRING("New", v[0].match.home_name.c_str());
  TEST_ASSERT_EQUAL_STRING("Old", v[1].match.home_name.c_str());
  TEST_ASSERT_EQUAL_INT(2, static_cast<int>(v.size()));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_seen_matches_are_deprioritised_not_dropped);
  RUN_TEST(test_utf8_split_handles_multibyte);
  RUN_TEST(test_no_truncation_when_it_fits);
  RUN_TEST(test_long_german_name_is_truncated_with_ellipsis);
  RUN_TEST(test_truncation_is_by_pixels_not_characters);
  RUN_TEST(test_truncate_never_exceeds_width_for_any_prefix);
  RUN_TEST(test_latin_extended_a_is_kept);
  RUN_TEST(test_latin1_umlaut_is_kept);
  RUN_TEST(test_romanian_comma_below_is_folded);
  RUN_TEST(test_cyrillic_becomes_question_marks_not_garbage);
  RUN_TEST(test_japanese_is_kept);
  RUN_TEST(test_sort_by_competition_then_kickoff);
  RUN_TEST(test_plan_fits_within_screen);
  RUN_TEST(test_heading_is_never_orphaned);
  RUN_TEST(test_no_overflow_when_everything_fits);
  RUN_TEST(test_columns_are_truncated_independently);
  RUN_TEST(test_plan_hash_changes_with_content);
  return UNITY_END();
}
