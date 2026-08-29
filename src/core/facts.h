// ファクト算出 (§4)。純粋関数。ネットワークにも描画にも依存させない (§4.3)。
#pragma once

#include <string>
#include <vector>

#include "core/model.h"

namespace fb {

enum FactType {
  FACT_NONE = 0,
  FACT_OPENING_STREAK,  // 開幕N連勝
  FACT_GIANT_KILLING,   // カップ戦: 下部リーグが格上を撃破 (§4.1 追補)
  FACT_ZONE_ENTER,      // 順位帯に入った
  FACT_ZONE_LEAVE,      // 順位帯から出た
  FACT_UPSET,           // 番狂わせ (同一リーグ内の順位差)
  FACT_TOP_OF_TABLE,    // 首位
  FACT_WIN_STREAK,      // N連勝
  FACT_LOSS_STREAK,     // N連敗
  FACT_STREAK_BROKEN,   // 連敗脱出
  FACT_UNBEATEN,        // N試合無敗
  FACT_RANK_CHANGE,     // Nつ順位UP/DOWN
  FACT_BIG_WIN,         // 大勝
  FACT_TYPE_COUNT,
};

struct Fact {
  FactType type = FACT_NONE;
  int count = 0;             // 連勝数・順位変動幅など
  bool positive = true;      // RANK_CHANGE の UP/DOWN、ZONE の良し悪し
  std::string zone_name;     // ZONE_ENTER / ZONE_LEAVE
  bool for_home = true;      // どちらのチームについてのファクトか

  bool valid() const { return type != FACT_NONE; }
};

// 閾値。competitions.json で上書きできるようにしてある (§4.1)。
struct FactThresholds {
  int win_streak_min = 2;
  int loss_streak_min = 2;
  int unbeaten_min = 5;
  int big_win_diff = 4;
  int upset_rank_gap = 8;
  int rank_change_min = 2;
  int giant_killing_big_rank = 4;   // 敗者がこの順位以内なら「大金星」
  int giant_killing_mid_rank = 10;  // ここまでなら「格上撃破」
  // form 文字列の向き。末尾が最新かどうかは実データで必ず確認する (§4.3)。
  bool form_latest_at_end = true;
};

// form 末尾からの連続数。latest_at_end=false なら先頭から数える。
int trailing_run(const std::string& form, char result, bool latest_at_end);
// form 末尾からの「result 以外が現れるまで」の連続数 (無敗の判定用)。
int trailing_run_not(const std::string& form, char result, bool latest_at_end);
// 最新の結果。form が空なら 0。
char latest_result(const std::string& form, bool latest_at_end);

// 1試合について、表示すべきファクトを最大1件返す (§4.2)。
// ファクトが無ければ type == FACT_NONE を返す。無理に何か作らない。
Fact compute_fact(const Match& m, const StandingsPool& pool,
                  const std::vector<Competition>& comps,
                  const FactThresholds& th);

// 優先度。小さいほど優先。
int fact_priority(FactType t);

}  // namespace fb
