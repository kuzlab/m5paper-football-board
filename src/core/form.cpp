#include "core/form.h"

#include <algorithm>

namespace fb {

const std::string FormTable::kEmpty;

char result_for(const Match& m, int team_id) {
  if (!m.has_score()) return 0;
  const bool home = (m.home_id == team_id);
  const bool away = (m.away_id == team_id);
  if (!home && !away) return 0;
  const int mine = home ? m.home_goals : m.away_goals;
  const int theirs = home ? m.away_goals : m.home_goals;
  if (mine > theirs) return 'W';
  if (mine < theirs) return 'L';
  return 'D';
}

FormTable::Row* FormTable::find(int team_id) {
  for (auto& r : teams_) {
    if (r.team_id == team_id) return &r;
  }
  return nullptr;
}

const FormTable::Row* FormTable::find(int team_id) const {
  for (const auto& r : teams_) {
    if (r.team_id == team_id) return &r;
  }
  return nullptr;
}

void FormTable::build(const std::vector<Match>& matches) {
  teams_.clear();

  // 呼び出し側がどの順で渡してきても結果が変わらないよう、必ず日付昇順に
  // 並べ直してから積む。ここが狂うと連勝・連敗が逆になる (§4.3)。
  std::vector<const Match*> sorted;
  sorted.reserve(matches.size());
  for (const auto& m : matches) {
    if (m.finished() && m.has_score()) sorted.push_back(&m);
  }
  std::stable_sort(sorted.begin(), sorted.end(),
                   [](const Match* a, const Match* b) {
                     if (a->kickoff_utc != b->kickoff_utc)
                       return a->kickoff_utc < b->kickoff_utc;
                     return a->fixture_id < b->fixture_id;
                   });

  for (const Match* m : sorted) {
    for (int side = 0; side < 2; ++side) {
      const int id = side == 0 ? m->home_id : m->away_id;
      if (id <= 0) continue;
      const char r = result_for(*m, id);
      if (!r) continue;
      Row* row = find(id);
      if (!row) {
        teams_.push_back(Row{id, std::string(), 0});
        row = &teams_.back();
      }
      row->form.push_back(r);
      row->last_match = m->fixture_id;
    }
  }
}

const std::string& FormTable::form_of(int team_id) const {
  const Row* r = find(team_id);
  return r ? r->form : kEmpty;
}

int FormTable::window_played(int team_id) const {
  const Row* r = find(team_id);
  return r ? static_cast<int>(r->form.size()) : 0;
}

long FormTable::last_match_of(int team_id) const {
  const Row* r = find(team_id);
  return r ? r->last_match : 0;
}

}  // namespace fb
