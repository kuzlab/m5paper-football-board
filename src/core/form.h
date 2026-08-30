// 試合一覧からチームごとの結果列を自前で組み立てる (§2.4)。
//
// football-data.org の無料枠では standings の form に依存できないため、
// §2.3(a) で取得した45日分の試合一覧から W/D/L の列を作る。
// 並びは必ず「日付昇順 = 古い→新しい」。末尾が最新。
#pragma once

#include <string>
#include <vector>

#include "core/model.h"

namespace fb {

class FormTable {
 public:
  // 試合を日付昇順に走査してチームごとの結果列を積む。
  // 引数の順序に依存しないよう、内部でソートしてから積む。
  void build(const std::vector<Match>& matches);

  // "WWDLW" (古い→新しい)。未知のチームは空文字。
  const std::string& form_of(int team_id) const;
  // この窓の中で消化した試合数。
  int window_played(int team_id) const;
  // 直近の試合の fixture id。0 なら該当なし。
  long last_match_of(int team_id) const;

  std::size_t size() const { return teams_.size(); }

 private:
  struct Row {
    int team_id = 0;
    std::string form;
    long last_match = 0;
  };
  std::vector<Row> teams_;
  static const std::string kEmpty;

  Row* find(int team_id);
  const Row* find(int team_id) const;
};

// 1試合における、あるチームから見た結果。'W' / 'D' / 'L'。
// 未消化・スコア欠損なら 0。
char result_for(const Match& m, int team_id);

}  // namespace fb
