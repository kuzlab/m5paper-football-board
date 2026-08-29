#include "core/model.h"

namespace fb {

const StandingRow* StandingsPool::lookup(int team_id, int* out_comp_index) const {
  // 国内リーグを先に引く。CL/EL の順位 (36チーム中3位) を国内順位と
  // 取り違えると、UPSET / ZONE 判定の意味が変わってしまうため (§4.1)。
  for (int pass = 0; pass < 2; ++pass) {
    const bool want_domestic = (pass == 0);
    for (const auto& ls : current) {
      if (comps && ls.comp_index >= 0 &&
          ls.comp_index < static_cast<int>(comps->size())) {
        if ((*comps)[ls.comp_index].is_domestic != want_domestic) continue;
      } else if (!want_domestic) {
        continue;
      }
      if (const StandingRow* r = ls.find(team_id)) {
        if (out_comp_index) *out_comp_index = ls.comp_index;
        return r;
      }
    }
  }
  if (out_comp_index) *out_comp_index = -1;
  return nullptr;
}

const StandingRow* StandingsPool::lookup_prev(int team_id, int comp_index) const {
  for (const auto& ls : previous) {
    if (ls.comp_index != comp_index) continue;
    if (const StandingRow* r = ls.find(team_id)) return r;
  }
  return nullptr;
}

}  // namespace fb
