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
                     if (a.seen != b.seen) return !a.seen;  // 既出は後回し
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
  // FNV-1a。描画される文字列だけを混ぜる。y 座標は内容が同じなら同じになる。
  std::uint32_t h = 2166136261u;
  auto mix = [&h](const std::string& s) {
    for (unsigned char c : s) {
      h ^= c;
      h *= 16777619u;
    }
    h ^= 0xFF;
    h *= 16777619u;
  };
  for (const auto& r : plan.rows) {
    mix(r.kind == PlanRow::kHeading ? r.heading : r.home);
    if (r.kind == PlanRow::kMatch) {
      mix(r.score);
      mix(r.away);
      mix(r.fact);
    }
  }
  mix(msg::overflow_text(plan.overflow_count));
  return h;
}

}  // namespace fb
