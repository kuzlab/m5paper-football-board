// 選抜・溢れ処理・防御的レイアウト (§5.2, §5.3)。
// 幅の実測関数を注入するので、この層までネイティブでテストできる。
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/facts.h"
#include "core/model.h"
#include "core/text_util.h"

namespace fb {

// M5Paper 960 x 540 横向き (§5.1)。
struct LayoutMetrics {
  int screen_w = 960;
  int screen_h = 540;
  int content_top = 70;     // 固定エリアの下端
  int content_bottom = 528; // 下マージンを 12px 残す
  int heading_h = 40;
  int row_h = 40;
  int overflow_h = 30;

  // 4カラムは固定幅。カラム内で個別に切り詰める (§5.3)。
  int col_home_x = 40,  col_home_w = 258;
  int col_score_x = 306, col_score_w = 108;
  int col_away_x = 422, col_away_w = 258;
  int col_fact_x = 700, col_fact_w = 236;
  int heading_x = 24;
};

// name/heading は 28px、fact は 20px のフォントで測る (§5.4)。
struct Measures {
  MeasureFn name;
  MeasureFn fact;
  MeasureFn heading;
};

struct Entry {
  Match match;
  Fact fact;
  // 取得範囲が72時間あるため、同じ試合が最大3日連続で候補に上がる (§2.2)。
  // 既に表示した試合は除外せず優先度だけ下げる。試合が少ない日でも
  // 画面が空にならないようにするため。
  bool seen = false;
};

struct PlanRow {
  enum Kind { kHeading, kMatch };
  Kind kind = kMatch;
  int y = 0;
  std::string heading;  // kHeading のとき
  std::string home;     // kMatch — すべて切り詰め済み
  std::string score;
  std::string away;
  std::string fact;
};

struct RenderPlan {
  std::vector<PlanRow> rows;
  int overflow_count = 0;
  int overflow_y = 0;
  bool has_overflow() const { return overflow_count > 0; }
};

// 競技会優先度 (第1キー) → キックオフの新しい順 (第2キー) で降順ソート (§5.2)。
void sort_entries(std::vector<Entry>& entries,
                  const std::vector<Competition>& comps);

// 画面に積めるだけ積み、はみ出す行は最初から作らない (§5.3)。
// 見出しと最初の1試合はセットで判定する (§5.2)。
RenderPlan build_plan(const std::vector<Entry>& entries,
                      const std::vector<Competition>& comps,
                      const LayoutMetrics& lm, const Measures& me);

// 描画コードを変えたら上げる。文字内容が同じでも見た目が変わる修正
// (フォントの太さ、罫線の位置、白フラッシュの有無など) を反映させるため。
// これが無いと、描画を直しても内容が変わるまで画面が古いままになる。
constexpr std::uint32_t kRenderVersion = 4;

// 描画内容のハッシュ (§5.5)。前回と同一なら描画をスキップする。
// 文字列だけでなく行の y 座標と kRenderVersion も混ぜる。
std::uint32_t plan_hash(const RenderPlan& plan);

// "2 - 1"
std::string format_score(const Match& m);

}  // namespace fb
