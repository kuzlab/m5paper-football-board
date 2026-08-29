#include "core/budget.h"

#include <algorithm>

namespace fb {

long utc_day_of(std::time_t utc) {
  long d = static_cast<long>(utc / 86400);
  if (utc < 0 && utc % 86400 != 0) d -= 1;
  return d;
}

bool roll_over(BudgetState& s, std::time_t now_utc) {
  const long today = utc_day_of(now_utc);
  if (s.utc_day == today) return false;
  s.utc_day = today;
  s.requests_used = 0;
  s.manual_fetches = 0;
  s.auto_done = false;
  s.api_remaining = -1;
  return true;
}

Allowance allow(const BudgetState& s, const BudgetPolicy& p, bool is_auto,
                int wanted) {
  Allowance a;
  if (wanted <= 0) return a;

  // 自動更新分の予約。まだ今日の自動更新が走っていない間は、手動更新から
  // その分を隠しておく。走った後は解放する (決定事項 1)。
  const int reserve = (is_auto || s.auto_done) ? 0 : p.auto_reserve_requests;

  int remaining = p.max_requests_per_day - s.requests_used - reserve;
  // API が返した残量の方が信用できる。リトライやエラーでのずれを吸収する。
  if (s.api_remaining >= 0) {
    remaining = std::min(remaining, s.api_remaining - reserve);
  }

  if (!is_auto && s.manual_fetches >= p.max_manual_fetches_per_day) {
    a.blocked = true;
    a.reason = "manual fetch limit";
    return a;
  }
  if (remaining <= 0) {
    a.blocked = true;
    a.reason = is_auto ? "daily request limit" : "daily request limit (reserved)";
    return a;
  }

  a.requests = std::min({wanted, remaining, p.max_requests_per_wake});
  // 部分的にしか投げられないなら fixtures を優先する。呼び出し側が
  // この数を上限として順に投げる。
  return a;
}

void record(BudgetState& s, const BudgetPolicy& p, bool is_auto, int used,
            int api_remaining_header) {
  (void)p;
  if (used > 0) s.requests_used += used;
  if (is_auto) {
    s.auto_done = true;
  } else if (used > 0) {
    s.manual_fetches += 1;
  }
  if (api_remaining_header >= 0) s.api_remaining = api_remaining_header;
}

}  // namespace fb
