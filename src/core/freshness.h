// 新着判定。1日以上前の結果は出さず、新着が無ければ前回の画面を維持する。
//
// 「1日」はキックオフからではなく、端末が初めてその試合の完了を確認した
// 時刻から数える。欧州の遅い試合は 06:00〜07:00 JST 頃に終わり、API への
// 反映がその後になるため、07:00 の起床に間に合わないことがある。翌朝の起床は
// キックオフの27時間後なので、キックオフ基準の24時間では一度も表示されない。
// (実例: 9/13 04:00 JST キックオフの Sunderland 0-2 Arsenal は反映が 07:58)
#pragma once

#include <ctime>
#include <string>
#include <vector>

#include "core/model.h"

namespace fb {

// 試合ごとに「初めて完了を確認した時刻」を持つ。SD に保存して起床をまたぐ。
class SeenLog {
 public:
  // 完了済みの試合を観測する。初めて見た試合だけ時刻を記録し、
  // 既に記録がある試合は上書きしない。
  void observe(const std::vector<Match>& matches, std::time_t now);

  // 初めて完了を確認した時刻。記録が無ければ 0。
  std::time_t first_seen(long fixture_id) const;

  // first_seen が keep_hours より古い記録を捨てる。
  // keep_hours はキックオフ上限より長くすること。捨てた試合が再観測されて
  // first_seen=now になっても、キックオフ上限で弾かれるので再表示されない。
  void prune(std::time_t now, int keep_hours);

  // 履歴が無い (初回起動・SD 差し替え・ファイル破損) ときに呼ぶ。
  // 次の observe() 1回に限り、初めて見た試合を「今届いた」ではなく
  // 「キックオフの2時間後に届いていた」とみなす。そうしないと取得窓の
  // 全試合が新着になり、数日前の試合が新着として出てしまう。
  void assume_arrival_at_kickoff() { bootstrap_ = true; }
  bool bootstrapping() const { return bootstrap_; }

  std::size_t size() const { return records_.size(); }

  // {"v":3,"seen":[[id,first_seen],...]}
  // 旧形式 (旧ファームの id 一覧、v2) は記録を捨て、履歴なしとして扱う。
  // 壊れていたら false を返す。どちらの場合も bootstrapping() が true になる。
  bool parse(const char* json, std::size_t len);
  std::string serialize() const;

 private:
  struct Rec {
    long id;
    std::time_t first_seen;
  };
  std::vector<Rec> records_;
  bool bootstrap_ = false;

  Rec* find(long id);
  const Rec* find(long id) const;
};

struct FreshnessPolicy {
  // 初めて完了を確認してからこの時間内なら新着。24時間にしないのは、
  // 毎朝 07:00 の起床で前日の分がちょうど24時間前後になり、取得に
  // かかる数十秒の差で出たり消えたりするため。
  int fresh_hours = 20;
  // キックオフがこれより古い試合は出さない。履歴が壊れたときの安全網。
  int kickoff_cap_hours = 72;
};

// 新着として表示する試合を選ぶ。window は取得した45日分の試合。
// 先に SeenLog::observe() を呼んで今回の観測を記録しておくこと。
std::vector<Match> select_fresh(const std::vector<Match>& window,
                                const SeenLog& log, std::time_t now,
                                const FreshnessPolicy& p);

// 新着が無く、維持すべき前回画面も無いとき (初回起動など) の受け皿。
// キックオフ上限内の完了試合をすべて返す。
std::vector<Match> select_recent(const std::vector<Match>& window,
                                 std::time_t now, const FreshnessPolicy& p);

}  // namespace fb
