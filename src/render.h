// 描画 (§5)。防御的レイアウトの結果を EPD に出す。
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/selector.h"

namespace fb {
namespace render {

// 固定エリアに出す状態 (§5.1, §6.4)
struct StatusBar {
  std::string left;        // "更新 08/29 07:00" / "更新中…" / "失敗 07:00"
  float temperature_c = 0.0f;
  float humidity_pct = 0.0f;
  bool env_ok = false;
  int battery_pct = 0;
  bool font_error = false;  // VLW が読めなかった (§5.4)
};

bool begin();

// VLW を SD から読み込む (§5.4)。失敗しても false を返すだけで、
// 内蔵フォントにフォールバックして画面は成立させる。
bool load_fonts();
bool fonts_ok();

// 幅の実測。build_plan に渡す (§5.3)。
Measures measures();

// 固定エリアだけを部分書き換え (§5.5)。1秒以内に反映される。
void draw_status_bar(const StatusBar& sb);

// 全面書き換え。ゴーストを消すために内容変更時に使う。
void draw_full(const RenderPlan& plan, const StatusBar& sb);

// 致命的エラー画面 (§7.1)。無言で失敗しない。
void draw_fatal(const char* title, const char* line1, const char* line2);

// 部分書き換えの回数を数え、閾値を超えたら全面書き換えを促す (§5.5)。
bool needs_ghost_clear();

void sleep_display();

}  // namespace render
}  // namespace fb
