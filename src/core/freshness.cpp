#include "core/freshness.h"

#include <ArduinoJson.h>

#include <algorithm>

namespace fb {
namespace {

// 保存形式の版。これと異なる版の記録は信用しない。
//
// v2 は旧ファームの id 一覧を「表示済み」として取り込んでいたが、旧ファームは
// 画面に入りきらず "+N more" に回した試合まで記録していた。そのため昨晩の
// プレミアが一度も表示されないまま既読になった (2026-09-13 実機で発生)。
// 版を上げ、v2 の記録も捨てて履歴なしからやり直させる。
constexpr int kFormatVersion = 3;

// 履歴が無いときに仮定する、キックオフから結果が届くまでの時間。
// 試合時間 (約2時間) ぶん。実際の反映はこれより遅れることがあるので、
// 遅れて届いた試合を古く見積もる側に倒れる。
constexpr std::time_t kAssumedArrivalSec = 2 * 3600;

}  // namespace

SeenLog::Rec* SeenLog::find(long id) {
  for (auto& r : records_) {
    if (r.id == id) return &r;
  }
  return nullptr;
}

const SeenLog::Rec* SeenLog::find(long id) const {
  for (const auto& r : records_) {
    if (r.id == id) return &r;
  }
  return nullptr;
}

void SeenLog::observe(const std::vector<Match>& matches, std::time_t now) {
  for (const auto& m : matches) {
    if (!m.finished() || !m.has_score() || m.fixture_id <= 0) continue;
    if (find(m.fixture_id)) continue;  // 初めて見た時刻を上書きしない
    std::time_t t = now;
    if (bootstrap_) {
      // 履歴が無いので「今初めて見た」とは言えない。
      t = std::min(now, m.kickoff_utc + kAssumedArrivalSec);
    }
    records_.push_back(Rec{m.fixture_id, t});
  }
  // 仮定を使うのはこの回だけ。保存すれば次回からは実際の履歴になる。
  bootstrap_ = false;
}

std::time_t SeenLog::first_seen(long fixture_id) const {
  const Rec* r = find(fixture_id);
  return r ? r->first_seen : 0;
}

void SeenLog::prune(std::time_t now, int keep_hours) {
  const std::time_t cutoff = now - static_cast<std::time_t>(keep_hours) * 3600;
  records_.erase(std::remove_if(records_.begin(), records_.end(),
                                [cutoff](const Rec& r) {
                                  return r.first_seen < cutoff;
                                }),
                 records_.end());
}

bool SeenLog::parse(const char* json, std::size_t len) {
  records_.clear();
  bootstrap_ = true;  // 読めた版が正しいと確認できるまでは履歴なし扱い

  JsonDocument doc;
  if (deserializeJson(doc, json, len)) return false;
  if ((doc["v"] | 0) != kFormatVersion) return true;  // 旧形式: 記録は捨てる

  for (JsonArrayConst r : doc["seen"].as<JsonArrayConst>()) {
    if (r.size() < 2) continue;
    const long id = r[0].as<long>();
    const std::time_t t = static_cast<std::time_t>(r[1].as<int64_t>());
    if (id > 0 && t > 0 && !find(id)) records_.push_back(Rec{id, t});
  }
  bootstrap_ = false;
  return true;
}

std::string SeenLog::serialize() const {
  JsonDocument doc;
  doc["v"] = kFormatVersion;
  JsonArray a = doc["seen"].to<JsonArray>();
  for (const auto& r : records_) {
    JsonArray e = a.add<JsonArray>();
    e.add(r.id);
    e.add(static_cast<int64_t>(r.first_seen));
  }
  std::string out;
  serializeJson(doc, out);
  return out;
}

std::vector<Match> select_fresh(const std::vector<Match>& window,
                                const SeenLog& log, std::time_t now,
                                const FreshnessPolicy& p) {
  const std::time_t kickoff_floor =
      now - static_cast<std::time_t>(p.kickoff_cap_hours) * 3600;
  const std::time_t fresh_sec = static_cast<std::time_t>(p.fresh_hours) * 3600;

  std::vector<Match> out;
  for (const auto& m : window) {
    if (!m.finished() || !m.has_score()) continue;
    if (m.kickoff_utc < kickoff_floor) continue;
    const std::time_t seen = log.first_seen(m.fixture_id);
    if (seen == 0) continue;  // observe() されていない
    if (now - seen >= fresh_sec) continue;
    out.push_back(m);
  }
  return out;
}

std::vector<Match> select_recent(const std::vector<Match>& window,
                                 std::time_t now, const FreshnessPolicy& p) {
  const std::time_t kickoff_floor =
      now - static_cast<std::time_t>(p.kickoff_cap_hours) * 3600;
  std::vector<Match> out;
  for (const auto& m : window) {
    if (m.finished() && m.has_score() && m.kickoff_utc >= kickoff_floor) {
      out.push_back(m);
    }
  }
  return out;
}

}  // namespace fb
