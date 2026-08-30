// データモデル。デバイス非依存 (§4.3)。
#pragma once

#include <ctime>
#include <string>
#include <vector>

namespace fb {

// --- 順位帯 (§4.1) ------------------------------------------------------
// zones は competitions.json の配列順に評価し、最初にマッチしたものを採用する。
// 狭い帯 (首位 1-1) を広い帯 (CL圏 1-4) より先に書くこと。
struct Zone {
  std::string name;
  int from = 0;  // 1-origin, inclusive
  int to = 0;    // 1-origin, inclusive
  // 降格圏のような「入りたくない帯」。文言の転落/浮上を切り替える (§4.2)。
  bool negative = false;

  bool contains(int rank) const { return rank >= from && rank <= to; }
};

// --- 競技会 (§2.1) ------------------------------------------------------
struct Competition {
  std::string key;      // "premier_league" — competitions.json のキー
  std::string display;  // "Premier League" — 画面表示名 (原語のまま、§5.1)
  std::string code;     // football-data.org の競技会コード ("PL" 等)。URL に直接使う
  int priority = 99;    // 1 が最優先。溢れ時の切り捨て順 (§5.2)
  bool has_standings = false;
  bool is_cup = false;
  // 国内リーグ優先の順位引きに使う (§4.1 ジャイアントキリング)。
  // CL/EL は false: 36チーム中3位という順位を国内順位と混同しないため。
  bool is_domestic = false;
  int total_teams = 0;
  std::vector<Zone> zones;

  const Zone* zone_of(int rank) const {
    for (const auto& z : zones) {
      if (z.contains(rank)) return &z;
    }
    return nullptr;
  }
};

// --- 試合 (§3.2) --------------------------------------------------------
struct Match {
  long fixture_id = 0;
  int comp_index = -1;  // Competition 配列への添字
  std::time_t kickoff_utc = 0;
  std::string status;  // football-data.org: "FINISHED" / "AWARDED"
  int home_id = 0;
  int away_id = 0;
  std::string home_name;
  std::string away_name;
  int home_goals = -1;
  int away_goals = -1;

  bool finished() const {
    // football-data.org のステータス。?status=FINISHED で問い合わせるので
    // 通常 FINISHED しか来ないが、没収試合の AWARDED も結果として扱う。
    return status == "FINISHED" || status == "AWARDED";
  }
  bool has_score() const { return home_goals >= 0 && away_goals >= 0; }
  // 0 = 引き分け, 1 = ホーム勝ち, -1 = アウェイ勝ち
  int outcome() const {
    if (!has_score()) return 0;
    if (home_goals > away_goals) return 1;
    if (home_goals < away_goals) return -1;
    return 0;
  }
  int goal_diff() const {
    return has_score() ? (home_goals > away_goals ? home_goals - away_goals
                                                  : away_goals - home_goals)
                       : 0;
  }
};

// --- 順位表 (§2.5) ------------------------------------------------------
struct StandingRow {
  int team_id = 0;
  std::string team_name;  // shortName 優先、無ければ name (§3.2)
  int rank = 0;           // football-data.org の "position"
  int points = 0;
  int played = 0;         // "playedGames"
  // 参考値。無料枠では空のことがあるので判定には使わない (§2.4)。
  // 連勝・連敗は FormTable が試合一覧から自前で算出する。
  std::string api_form;
};

struct LeagueStandings {
  int comp_index = -1;
  std::vector<StandingRow> rows;

  const StandingRow* find(int team_id) const {
    for (const auto& r : rows) {
      if (r.team_id == team_id) return &r;
    }
    return nullptr;
  }
};

// 今回と前回の順位表をまとめて引くためのプール。
// 国内リーグを CL/EL より優先して引く (§4.1)。
struct StandingsPool {
  const std::vector<Competition>* comps = nullptr;
  std::vector<LeagueStandings> current;
  std::vector<LeagueStandings> previous;

  // 見つかった行と、その行が属する competition の添字を返す。
  const StandingRow* lookup(int team_id, int* out_comp_index) const;
  const StandingRow* lookup_prev(int team_id, int comp_index) const;
};

}  // namespace fb
