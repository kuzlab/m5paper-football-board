// レート制限の待ち時間計算 (§2.5)。実時間に依存しないので native でテストできる。
//
// 固定間隔 (旧 min_request_interval_ms = 7秒) をやめた理由:
// 1回の起床で投げるのは7本なので、無料枠の 10 req/min には元から収まる。
// それでも7秒間隔を強制していたため、起床61秒のうち42秒がただのウェイトで、
// その間 Wi-Fi を最大電力で握ったままだった (2026-10-12 の実機ログ)。
// 「直近60秒で上限を超えない」ことだけを保証すれば、通常は1本も待たない。
#pragma once

namespace fb {

class RateWindow {
 public:
  // max_per_window: 窓内に許すリクエスト数。無料枠 10 req/min に対して
  //                 9 など1本余裕を残した値を渡す。
  // min_gap_ms: 連投の間隔。keep-alive 接続を叩きすぎないための礼儀。
  explicit RateWindow(int max_per_window, unsigned long window_ms = 60000,
                      unsigned long min_gap_ms = 250)
      : max_(max_per_window < 1 ? 1 : (max_per_window > kCap ? kCap : max_per_window)),
        window_ms_(window_ms),
        min_gap_ms_(min_gap_ms) {}

  // now_ms の時点で次の1本を投げるまでに待つべきミリ秒。0 なら即投げてよい。
  unsigned long wait_for(unsigned long now_ms) const {
    unsigned long wait = 0;
    if (n_ > 0) {
      const unsigned long since = now_ms - s_[n_ - 1];
      if (since < min_gap_ms_) wait = min_gap_ms_ - since;
    }
    // 窓内が上限に達していたら、古いものが窓から出るまで待つ。
    int in = 0;
    int oldest = -1;
    for (int i = 0; i < n_; ++i) {
      if (now_ms - s_[i] < window_ms_) {
        if (oldest < 0) oldest = i;
        ++in;
      }
    }
    if (in >= max_ && oldest >= 0) {
      // (in - max_ + 1) 本が窓から出れば1本投げられる。
      const int idx = oldest + (in - max_);
      const unsigned long age = now_ms - s_[idx];
      const unsigned long w = (age < window_ms_) ? (window_ms_ - age) : 0;
      if (w > wait) wait = w;
    }
    return wait;
  }

  // 実際に投げた時刻を記録する。
  void record(unsigned long now_ms) {
    if (n_ == kCap) {
      for (int i = 1; i < kCap; ++i) s_[i - 1] = s_[i];
      --n_;
    }
    s_[n_++] = now_ms;
  }

  int in_window(unsigned long now_ms) const {
    int n = 0;
    for (int i = 0; i < n_; ++i) {
      if (now_ms - s_[i] < window_ms_) ++n;
    }
    return n;
  }

 private:
  static constexpr int kCap = 16;  // max_requests_per_wake (既定12) より大きく
  int max_;
  unsigned long window_ms_;
  unsigned long min_gap_ms_;
  unsigned long s_[kCap] = {0};
  int n_ = 0;
};

}  // namespace fb
