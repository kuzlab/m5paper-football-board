#include "core/facts.h"

#include <algorithm>
#include <cstdlib>

namespace fb {
namespace {

// §4.2 の優先度。順序を変えたいときはこの配列だけを触る。
constexpr FactType kPriorityOrder[] = {
    FACT_OPENING_STREAK,
    FACT_GIANT_KILLING,
    FACT_ZONE_ENTER,
    FACT_ZONE_LEAVE,
    FACT_UPSET,
    // 連勝・連敗は日々変わるが「首位」は動かない。首位のチームが毎日
    // 同じ「首位」を出し続けるより、直近の勢いを見せるほうが情報量が多い。
    // TOP_OF_TABLE はストリークが無いときの受け皿として下に置く。
    FACT_WIN_STREAK,
    FACT_LOSS_STREAK,
    FACT_STREAK_BROKEN,
    FACT_TOP_OF_TABLE,
    FACT_UNBEATEN,
    FACT_RANK_CHANGE,
    FACT_BIG_WIN,
};

// 順位表に載っているチームの、今回と前回の状態。
struct TeamCtx {
  bool known = false;
  int comp_index = -1;
  const Competition* comp = nullptr;
  const StandingRow* now = nullptr;
  const StandingRow* prev = nullptr;
};

TeamCtx resolve(int team_id, const StandingsPool& pool,
                const std::vector<Competition>& comps) {
  TeamCtx c;
  int ci = -1;
  c.now = pool.lookup(team_id, &ci);
  if (!c.now) return c;
  c.known = true;
  c.comp_index = ci;
  if (ci >= 0 && ci < static_cast<int>(comps.size())) c.comp = &comps[ci];
  c.prev = pool.lookup_prev(team_id, ci);
  return c;
}

void push(std::vector<Fact>& out, FactType t, int count, bool positive,
          bool for_home, const std::string& zone = std::string()) {
  Fact f;
  f.type = t;
  f.count = count;
  f.positive = positive;
  f.for_home = for_home;
  f.zone_name = zone;
  out.push_back(f);
}

// 1チーム分の結果列 / 順位由来のファクトを集める。
// 結果列は FormTable が試合一覧から自前で組み立てたもの (§2.4)。
void collect_team_facts(const TeamCtx& c, int team_id, const FormTable& forms,
                        bool for_home, const FactThresholds& th,
                        std::vector<Fact>& out) {
  const std::string& form = forms.form_of(team_id);

  // 順位由来のファクトは順位表が要るが、連勝・連敗は結果列だけで出せる。
  // 順位表に載っていないチーム (カップ戦の下部リーグなど) でも
  // 結果列があれば連勝は言える。
  if (c.known && c.now) {
    // 開幕からの全勝 (§4.1)。45日窓の結果列がシーズン消化数と一致し、
    // かつ全勝であれば「開幕N連勝」。窓がシーズン全体を覆う序盤のみ成立する。
    if (!form.empty() && static_cast<int>(form.size()) == c.now->played &&
        c.now->played >= th.win_streak_min &&
        form.find_first_not_of('W') == std::string::npos) {
      push(out, FACT_OPENING_STREAK, c.now->played, true, for_home);
    }
  }

  const int wins = trailing_run(form, 'W', th.form_latest_at_end);
  if (wins >= th.win_streak_min) push(out, FACT_WIN_STREAK, wins, true, for_home);

  const int losses = trailing_run(form, 'L', th.form_latest_at_end);
  if (losses >= th.loss_streak_min)
    push(out, FACT_LOSS_STREAK, losses, false, for_home);

  // 連敗脱出: 最新が L 以外で、その直前に連敗があった。
  if (losses == 0) {
    const char last = latest_result(form, th.form_latest_at_end);
    if (last == 'W' || last == 'D') {
      // 最新1件を落とした form の末尾連敗数を数える。
      std::string prior = form;
      if (th.form_latest_at_end) {
        prior.pop_back();
      } else {
        prior.erase(prior.begin());
      }
      const int prior_losses = trailing_run(prior, 'L', th.form_latest_at_end);
      if (prior_losses >= th.loss_streak_min)
        push(out, FACT_STREAK_BROKEN, prior_losses, true, for_home);
    }
  }

  const int unbeaten = trailing_run_not(form, 'L', th.form_latest_at_end);
  if (unbeaten >= th.unbeaten_min)
    push(out, FACT_UNBEATEN, unbeaten, true, for_home);

  if (!c.known || !c.now) return;
  if (c.now->rank == 1) push(out, FACT_TOP_OF_TABLE, 1, true, for_home);

  if (c.prev && c.prev->rank > 0 && c.now->rank > 0) {
    const int delta = c.prev->rank - c.now->rank;  // 正 = 順位が上がった
    if (std::abs(delta) >= th.rank_change_min)
      push(out, FACT_RANK_CHANGE, std::abs(delta), delta > 0, for_home);

    if (c.comp) {
      const Zone* zn = c.comp->zone_of(c.now->rank);
      const Zone* zp = c.comp->zone_of(c.prev->rank);
      const std::string now_name = zn ? zn->name : std::string();
      const std::string prev_name = zp ? zp->name : std::string();
      if (now_name != prev_name) {
        if (zn) push(out, FACT_ZONE_ENTER, 0, !zn->negative, for_home, zn->name);
        if (zp) push(out, FACT_ZONE_LEAVE, 0, !zp->negative, for_home, zp->name);
      }
    }
  }
}

}  // namespace

int fact_priority(FactType t) {
  for (int i = 0; i < static_cast<int>(sizeof(kPriorityOrder) / sizeof(FactType));
       ++i) {
    if (kPriorityOrder[i] == t) return i;
  }
  return 999;
}

char latest_result(const std::string& form, bool latest_at_end) {
  if (form.empty()) return 0;
  return latest_at_end ? form.back() : form.front();
}

int trailing_run(const std::string& form, char result, bool latest_at_end) {
  int n = 0;
  if (latest_at_end) {
    for (auto it = form.rbegin(); it != form.rend() && *it == result; ++it) ++n;
  } else {
    for (char ch : form) {
      if (ch != result) break;
      ++n;
    }
  }
  return n;
}

int trailing_run_not(const std::string& form, char result, bool latest_at_end) {
  int n = 0;
  if (latest_at_end) {
    for (auto it = form.rbegin(); it != form.rend() && *it != result; ++it) ++n;
  } else {
    for (char ch : form) {
      if (ch == result) break;
      ++n;
    }
  }
  return n;
}

Fact compute_fact(const Match& m, const StandingsPool& pool,
                  const FormTable& forms,
                  const std::vector<Competition>& comps,
                  const FactThresholds& th) {
  std::vector<Fact> cands;

  const bool is_cup =
      m.comp_index >= 0 && m.comp_index < static_cast<int>(comps.size())
          ? comps[m.comp_index].is_cup
          : false;

  const TeamCtx home = resolve(m.home_id, pool, comps);
  const TeamCtx away = resolve(m.away_id, pool, comps);
  const int result = m.outcome();

  // 勝者側のファクトを優先する。負けたチームの連勝は表示しても意味がない。
  if (result >= 0) collect_team_facts(home, m.home_id, forms, true, th, cands);
  if (result <= 0) collect_team_facts(away, m.away_id, forms, false, th, cands);
  // 引き分けや連敗は敗者側にも意味があるので、両方拾って優先度で選ばせる。
  if (result > 0) collect_team_facts(away, m.away_id, forms, false, th, cands);
  if (result < 0) collect_team_facts(home, m.home_id, forms, true, th, cands);

  // 大勝 (§4.1)
  if (m.has_score() && m.goal_diff() >= th.big_win_diff) {
    push(cands, FACT_BIG_WIN, m.goal_diff(), true, result > 0);
  }

  if (result != 0) {
    const TeamCtx& winner = (result > 0) ? home : away;
    const TeamCtx& loser = (result > 0) ? away : home;
    const bool winner_is_home = (result > 0);

    if (is_cup) {
      // ジャイアントキリング (§4.1 追補)。
      // 勝者が保持中の順位表に居ない = 下部リーグ、または我々が順位表を
      // 持っていないリーグのチーム。敗者がトップリーグに居れば格上撃破。
      // 追加リクエストなしで判定できる代わりに、敗者が「何部相手か」までは
      // 分からない。Championship と5部を区別することはできない。
      if (!winner.known && loser.known && loser.now) {
        push(cands, FACT_GIANT_KILLING, loser.now->rank, true, winner_is_home);
      }
    }

    // 番狂わせ: 同一の順位表に載っている両者の順位差 (§4.1)。
    if (winner.known && loser.known && winner.now && loser.now &&
        winner.comp_index == loser.comp_index) {
      const int gap = winner.now->rank - loser.now->rank;  // 正 = 下位が勝った
      if (gap >= th.upset_rank_gap) {
        push(cands, FACT_UPSET, gap, true, winner_is_home);
      }
    }
  }

  if (cands.empty()) return Fact();

  // 最も価値の高いもの1件だけを返す (§4.2)。同順位なら count が大きい方。
  auto best = std::min_element(
      cands.begin(), cands.end(), [](const Fact& a, const Fact& b) {
        const int pa = fact_priority(a.type), pb = fact_priority(b.type);
        if (pa != pb) return pa < pb;
        return a.count > b.count;
      });
  return *best;
}

}  // namespace fb
