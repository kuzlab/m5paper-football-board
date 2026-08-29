// 日次リクエスト予算 (§2.3)。純粋関数なのでネイティブでテストする。
//
// UTC 日と JST 07:00 の関係:
//   UTC 日 U は JST 09:00(U) 〜 JST 09:00(U+1)。
//   自動更新の JST 07:00(U+1) は UTC 22:00(U)、つまり UTC 日 U の末尾にある。
//   したがって各 UTC 日はちょうど1回の自動更新を含み、その回の枠を
//   手動更新に食い潰されないよう予約しておく必要がある。
#pragma once

#include <ctime>

namespace fb {

struct BudgetPolicy {
  // API-SPORTS 無料プランは 100/日。余裕を 10 残す。
  int max_requests_per_day = 90;
  // 自動更新1回分として確保しておくリクエスト数 (fixtures 8 + standings 5)。
  int auto_reserve_requests = 13;
  // 手動更新の連打防止 (§2.3-2)。
  int max_manual_fetches_per_day = 5;
  // リトライループの暴走防止 (§2.3-3)。
  int max_requests_per_wake = 20;
  // 無料プランは毎分制限もある。1分あたりの上限 (§2.4 改訂)。
  int max_requests_per_minute = 10;
};

struct BudgetState {
  long utc_day = -1;          // epoch からの日数
  int requests_used = 0;      // その UTC 日に消費したリクエスト数
  int manual_fetches = 0;     // その UTC 日の手動取得回数
  bool auto_done = false;     // その UTC 日の自動更新が済んだか
  // API が返す残量 (§2.3)。ローカルカウンタよりこちらを優先する。
  // 未取得なら -1。
  int api_remaining = -1;
};

long utc_day_of(std::time_t utc);

// UTC 日が変わっていればカウンタをリセットする。変わったら true。
bool roll_over(BudgetState& s, std::time_t now_utc);

struct Allowance {
  int requests = 0;      // 今回の起床で使ってよいリクエスト数。0 なら通信しない
  bool blocked = false;  // 上限に達して弾かれた
  const char* reason = "";
};

// is_auto: RTC アラームによる自動起床か (§6.4)。
// wanted: 今回投げたいリクエスト数。
Allowance allow(const BudgetState& s, const BudgetPolicy& p, bool is_auto,
                int wanted);

// 実際に消費した分を記録する。
void record(BudgetState& s, const BudgetPolicy& p, bool is_auto, int used,
            int api_remaining_header);

}  // namespace fb
