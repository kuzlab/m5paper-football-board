#include "render.h"

#include <M5Unified.h>
#include <SD.h>
#include <esp_heap_caps.h>

#include <cstdio>

#include "core/messages.h"
#include "core/text_util.h"
#include "logging.h"
#include "storage.h"

namespace fb {
namespace render {
namespace {

constexpr int kMaxPartialBeforeFull = 10;  // §5.5
constexpr int kStatusH = 70;

// VLW の実体。loadFont はこのバッファを参照し続けるので解放しない。
std::uint8_t* g_font28 = nullptr;
std::uint8_t* g_font20 = nullptr;
bool g_fonts_ok = false;

// 幅の実測専用スプライト。表示用フォントを切り替えずに textWidth を取れる。
M5Canvas g_metric28(&M5.Display);
M5Canvas g_metric20(&M5.Display);

LayoutMetrics g_lm;

std::uint8_t* read_font(const char* path, std::size_t* out_size) {
  *out_size = 0;
  if (!storage::sd_ready()) return nullptr;
  File f = SD.open(path, FILE_READ);
  if (!f) {
    LOGW("font missing: %s", path);
    return nullptr;
  }
  const std::size_t n = f.size();
  if (n == 0 || n > 2 * 1024 * 1024) {
    f.close();
    return nullptr;
  }
  auto* buf = static_cast<std::uint8_t*>(heap_caps_malloc(n, MALLOC_CAP_SPIRAM));
  if (!buf) buf = static_cast<std::uint8_t*>(malloc(n));
  if (!buf) {
    f.close();
    LOGE("font alloc failed (%u bytes)", (unsigned)n);
    return nullptr;
  }
  const std::size_t got = f.read(buf, n);
  f.close();
  if (got != n) {
    free(buf);
    return nullptr;
  }
  *out_size = n;
  return buf;
}

void use_font28() {
  if (g_fonts_ok && g_font28) {
    M5.Display.loadFont(g_font28);
  } else {
    M5.Display.setFont(&fonts::Font4);
  }
}

void use_font20() {
  if (g_fonts_ok && g_font20) {
    M5.Display.loadFont(g_font20);
  } else {
    M5.Display.setFont(&fonts::Font2);
  }
}

// フォントが無いときは日本語が全部 '?' になるので、ファクトは描かない (§5.4)。
std::string safe_text(const std::string& s) {
  return g_fonts_ok ? s : std::string();
}

void draw_status_contents(const StatusBar& sb) {
  M5.Display.fillRect(0, 0, g_lm.screen_w, kStatusH, TFT_WHITE);
  M5.Display.drawFastHLine(0, kStatusH - 1, g_lm.screen_w, TFT_BLACK);

  use_font20();
  M5.Display.setTextColor(TFT_BLACK, TFT_WHITE);
  M5.Display.setTextDatum(middle_left);

  const int cy = kStatusH / 2 - 2;
  M5.Display.drawString(fold_unsupported(sb.left).c_str(), 24, cy);

  char mid[64];
  if (sb.env_ok) {
    snprintf(mid, sizeof(mid), "%.1f℃ %.0f%%   %d%%", sb.temperature_c,
             sb.humidity_pct, sb.battery_pct);
  } else {
    snprintf(mid, sizeof(mid), "--.-℃ --%%   %d%%", sb.battery_pct);
  }
  M5.Display.setTextDatum(middle_center);
  M5.Display.drawString(fold_unsupported(mid).c_str(), g_lm.screen_w / 2, cy);

  M5.Display.setTextDatum(middle_right);
  const char* hint = sb.font_error ? msg::kFontError : msg::kRefreshHint;
  M5.Display.drawString(fold_unsupported(hint).c_str(), g_lm.screen_w - 24, cy);
  M5.Display.setTextDatum(top_left);
}

}  // namespace

bool begin() {
  M5.Display.setRotation(1);  // 960 x 540 横向き (§5.1)
  g_lm.screen_w = M5.Display.width();
  g_lm.screen_h = M5.Display.height();
  if (g_lm.screen_h < g_lm.content_bottom) {
    g_lm.content_bottom = g_lm.screen_h - 12;
  }
  M5.Display.setEpdMode(m5gfx::epd_mode_t::epd_quality);
  M5.Display.setTextWrap(false);
  return true;
}

bool load_fonts() {
  std::size_t n28 = 0, n20 = 0;
  g_font28 = read_font(storage::kFont28Path, &n28);
  g_font20 = read_font(storage::kFont20Path, &n20);
  if (!g_font28 || !g_font20) {
    // SD の抜き差しや故障で完全に沈黙するのは避ける (§5.4)。
    // 英数字のみの内蔵フォントで画面を成立させ、FONT ERR を出す。
    g_fonts_ok = false;
    LOGE("VLW load failed (28=%u 20=%u) -> builtin font fallback",
         (unsigned)n28, (unsigned)n20);
    return false;
  }

  g_metric28.createSprite(1, 1);
  g_metric20.createSprite(1, 1);
  const bool ok28 = g_metric28.loadFont(g_font28);
  const bool ok20 = g_metric20.loadFont(g_font20);
  if (!ok28 || !ok20) {
    g_fonts_ok = false;
    LOGE("VLW parse failed (28=%d 20=%d)", (int)ok28, (int)ok20);
    return false;
  }
  g_fonts_ok = true;
  LOGI("fonts loaded: 28px %u bytes, 20px %u bytes", (unsigned)n28,
       (unsigned)n20);
  return true;
}

bool fonts_ok() { return g_fonts_ok; }

Measures measures() {
  Measures m;
  if (g_fonts_ok) {
    m.name = [](const std::string& s) { return g_metric28.textWidth(s.c_str()); };
    m.heading = m.name;
    m.fact = [](const std::string& s) { return g_metric20.textWidth(s.c_str()); };
  } else {
    // 内蔵フォントは等幅に近い。ASCII のみを想定した近似で十分。
    m.name = [](const std::string& s) {
      return static_cast<int>(utf8_split(s).size()) * 14;
    };
    m.heading = m.name;
    m.fact = [](const std::string& s) {
      return static_cast<int>(utf8_split(s).size()) * 10;
    };
  }
  return m;
}

bool needs_ghost_clear() {
  return storage::partial_refresh_count() >= kMaxPartialBeforeFull;
}

void draw_status_bar(const StatusBar& sb) {
  // 部分書き換え。押されてから1秒以内に「更新中…」を出すため (§6.3)。
  M5.Display.setEpdMode(m5gfx::epd_mode_t::epd_fast);
  draw_status_contents(sb);
  M5.Display.display();
  storage::set_partial_refresh_count(storage::partial_refresh_count() + 1);
}

void draw_full(const RenderPlan& plan, const StatusBar& sb) {
  M5.Display.setEpdMode(m5gfx::epd_mode_t::epd_quality);
  M5.Display.fillScreen(TFT_WHITE);
  draw_status_contents(sb);

  // 28px の要素をまとめて描く。フォントの入れ替えを2回に抑えるため
  // カラムごとではなくフォントごとに描く。
  use_font28();
  M5.Display.setTextColor(TFT_BLACK, TFT_WHITE);
  M5.Display.setTextDatum(top_left);
  for (const auto& r : plan.rows) {
    if (r.kind == PlanRow::kHeading) {
      M5.Display.fillRect(g_lm.heading_x, r.y + 12, 8, 16, TFT_BLACK);
      M5.Display.drawString(r.heading.c_str(), g_lm.heading_x + 18, r.y + 6);
      M5.Display.drawFastHLine(g_lm.heading_x, r.y + g_lm.heading_h - 4,
                               g_lm.screen_w - g_lm.heading_x * 2, TFT_BLACK);
    } else {
      const int ty = r.y + 6;
      M5.Display.setTextDatum(top_left);
      M5.Display.drawString(r.home.c_str(), g_lm.col_home_x, ty);
      M5.Display.setTextDatum(top_center);
      M5.Display.drawString(r.score.c_str(),
                            g_lm.col_score_x + g_lm.col_score_w / 2, ty);
      M5.Display.setTextDatum(top_left);
      M5.Display.drawString(r.away.c_str(), g_lm.col_away_x, ty);
    }
  }

  // 20px の要素 (ファクトと溢れ表示)
  use_font20();
  M5.Display.setTextDatum(top_left);
  for (const auto& r : plan.rows) {
    if (r.kind != PlanRow::kMatch) continue;
    const std::string t = safe_text(r.fact);
    if (t.empty()) continue;  // ファクトが無ければ何も書かない (§4.2)
    M5.Display.drawString(t.c_str(), g_lm.col_fact_x, r.y + 10);
  }
  if (plan.has_overflow()) {
    M5.Display.setTextDatum(top_right);
    const std::string t = fold_unsupported(msg::overflow_text(plan.overflow_count));
    M5.Display.drawString(safe_text(t).c_str(), g_lm.screen_w - 24,
                          plan.overflow_y + 4);
    M5.Display.setTextDatum(top_left);
  }

  M5.Display.display();
  storage::set_partial_refresh_count(0);  // 全面書き換えでゴーストが消えた
}

void draw_fatal(const char* title, const char* line1, const char* line2) {
  M5.Display.setEpdMode(m5gfx::epd_mode_t::epd_quality);
  M5.Display.fillScreen(TFT_WHITE);
  M5.Display.setTextColor(TFT_BLACK, TFT_WHITE);
  use_font28();
  M5.Display.setTextDatum(top_left);
  M5.Display.drawString(title, 48, 120);
  use_font20();
  if (line1) M5.Display.drawString(fold_unsupported(line1).c_str(), 48, 190);
  if (line2) M5.Display.drawString(fold_unsupported(line2).c_str(), 48, 226);
  M5.Display.display();
  storage::set_partial_refresh_count(0);
}

void sleep_display() { M5.Display.sleep(); }

}  // namespace render
}  // namespace fb
