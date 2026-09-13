#include "core/selector.h"

#include <algorithm>
#include <cstdio>

#include "core/messages.h"

namespace fb {
namespace {

int comp_priority(const std::vector<Competition>& comps, int idx) {
  if (idx < 0 || idx >= static_cast<int>(comps.size())) return 999;
  return comps[idx].priority;
}

const char* comp_display(const std::vector<Competition>& comps, int idx) {
  if (idx < 0 || idx >= static_cast<int>(comps.size())) return "?";
  return comps[idx].display.c_str();
}

// overflow 行の高さを予約するかどうかを変えて2回試す。
// 1回目で全部入れば予約は不要で、1行分密度が上がる。
RenderPlan plan_once(const std::vector<Entry>& entries,
                     const std::vector<Competition>& comps,
                     const LayoutMetrics& lm, const Measures& me,
                     bool reserve_overflow) {
  RenderPlan plan;
  const int limit =
      lm.content_bottom - (reserve_overflow ? lm.overflow_h : 0);
  int y = lm.content_top;
  int last_comp = -2;
  std::size_t i = 0;

  for (; i < entries.size(); ++i) {
    const Entry& e = entries[i];
    const bool need_heading = (e.match.comp_index != last_comp);

    // 見出しだけ描いて試合が1件も入らない状態を防ぐ (§5.2)。
    const int needed = (need_heading ? lm.heading_h : 0) + lm.row_h;
    if (y + needed > limit) break;

    if (need_heading) {
      PlanRow h;
      h.kind = PlanRow::kHeading;
      h.y = y;
      h.heading = truncate_to_width(
          fold_unsupported(comp_display(comps, e.match.comp_index)),
          lm.screen_w - lm.heading_x * 2, me.heading);
      plan.rows.push_back(h);
      y += lm.heading_h;
      last_comp = e.match.comp_index;
    }

    PlanRow r;
    r.kind = PlanRow::kMatch;
    r.y = y;
    // 各カラム内で個別に切り詰める (§5.3)。文字数ではなくピクセル幅で判定。
    r.home = truncate_to_width(fold_unsupported(e.match.home_name),
                               lm.col_home_w, me.name);
    r.away = truncate_to_width(fold_unsupported(e.match.away_name),
                               lm.col_away_w, me.name);
    r.score = truncate_to_width(format_score(e.match), lm.col_score_w, me.name);
    r.fact = truncate_to_width(fold_unsupported(msg::fact_text(e.fact)),
                               lm.col_fact_w, me.fact);
    plan.rows.push_back(r);
    y += lm.row_h;
  }

  plan.overflow_count = static_cast<int>(entries.size() - i);
  plan.overflow_y = y;
  return plan;
}

}  // namespace

std::string format_score(const Match& m) {
  if (!m.has_score()) return "- - -";
  char buf[24];
  snprintf(buf, sizeof(buf), "%d - %d", m.home_goals, m.away_goals);
  return std::string(buf);
}

void sort_entries(std::vector<Entry>& entries,
                  const std::vector<Competition>& comps) {
  std::stable_sort(entries.begin(), entries.end(),
                   [&comps](const Entry& a, const Entry& b) {
                     const int pa = comp_priority(comps, a.match.comp_index);
                     const int pb = comp_priority(comps, b.match.comp_index);
                     if (pa != pb) return pa < pb;
                     if (a.match.kickoff_utc != b.match.kickoff_utc)
                       return a.match.kickoff_utc > b.match.kickoff_utc;
                     return a.match.fixture_id > b.match.fixture_id;
                   });
}

RenderPlan build_plan(const std::vector<Entry>& entries,
                      const std::vector<Competition>& comps,
                      const LayoutMetrics& lm, const Measures& me) {
  RenderPlan p = plan_once(entries, comps, lm, me, /*reserve_overflow=*/false);
  if (p.overflow_count == 0) return p;
  return plan_once(entries, comps, lm, me, /*reserve_overflow=*/true);
}

std::uint32_t plan_hash(const RenderPlan& plan) {
  std::uint32_t h = 2166136261u;
  auto mix_byte = [&h](unsigned char c) {
    h ^= c;
    h *= 16777619u;
  };
  auto mix = [&mix_byte](const std::string& s) {
    for (unsigned char c : s) mix_byte(c);
    mix_byte(0xFF);
  };
  auto mix_int = [&mix_byte](std::uint32_t v) {
    for (int i = 0; i < 4; ++i) mix_byte((v >> (i * 8)) & 0xFF);
  };

  // 描画コードの版。見た目だけが変わる修正でも再描画させる。
  mix_int(kRenderVersion);

  for (const auto& r : plan.rows) {
    // y 座標も混ぜる。行高が変われば同じ文字列でも見た目が変わるため、
    // フォント差し替えやレイアウト調整が自動で反映される。
    mix_int(static_cast<std::uint32_t>(r.y));
    mix_byte(r.kind == PlanRow::kHeading ? 'H' : 'M');
    mix(r.kind == PlanRow::kHeading ? r.heading : r.home);
    if (r.kind == PlanRow::kMatch) {
      mix(r.score);
      mix(r.away);
      mix(r.fact);
    }
  }
  mix_int(static_cast<std::uint32_t>(plan.overflow_y));
  mix(msg::overflow_text(plan.overflow_count));
  return h;
}

}  // namespace fb
