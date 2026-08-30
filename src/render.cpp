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
  // 色深度は中間調の出方に直結する。グレー背景やコントラスト不足の
  // 切り分けに要るので実機の値をログに残す (§6.0)。
  LOGI("display: %dx%d, colorDepth=%d bpp", g_lm.screen_w, g_lm.screen_h,
       (int)M5.Display.getColorDepth());
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

LayoutMetrics metrics() {
  LayoutMetrics lm = g_lm;
  // VLW の行高は指定サイズより大きい (28px 指定で ascent33+descent9=42px)。
  // 実測値から行高を決めないと、文字が下の行や罫線を貫く。
  const int h28 = g_fonts_ok ? g_metric28.fontHeight() : 28;
  const int h20 = g_fonts_ok ? g_metric20.fontHeight() : 20;
  lm.row_h = h28 + 6;
  lm.heading_h = h28 + 14;   // 罫線を引く余白を下に確保する
  lm.overflow_h = h20 + 6;
  return lm;
}

bool needs_ghost_clear() {
  return storage::partial_refresh_count() >= kMaxPartialBeforeFull;
}

void draw_body(const RenderPlan& plan);  // 下で定義

void draw_status_bar(const StatusBar& sb, const RenderPlan& body) {
  // 電源ラッチ方式では起床がコールドブートで、EPD のフレームバッファ (RAM)
  // が空で始まる。パネルには前回の絵が残っているが RAM には無い (§6.2-1)。
  //
  // そのため「固定エリアだけ描いて押し出す」ことができない。領域を限定して
  // 押しても、パネル側の該当外領域を保てる保証がなく、実機では試合一覧が
  // 消えた。コールドブートの前提に合わせ、毎回フレームバッファ全体を
  // 組み立て直してから押し出す。本文は前回描いた内容を SD から復元する。
  M5.Display.setEpdMode(m5gfx::epd_mode_t::epd_fast);
  M5.Display.fillScreen(TFT_WHITE);
  draw_body(body);
  draw_status_contents(sb);
  M5.Display.display();
  storage::set_partial_refresh_count(storage::partial_refresh_count() + 1);
}

void draw_body(const RenderPlan& plan) {
  const LayoutMetrics lm = metrics();
  M5.Display.setTextColor(TFT_BLACK, TFT_WHITE);

  use_font28();
  M5.Display.setTextDatum(top_left);
  const int text_h = M5.Display.fontHeight();
  for (const auto& r : plan.rows) {
    if (r.kind == PlanRow::kHeading) {
      // 罫線は文字の下端より下に引く。行高を実測しないとここが重なる。
      const int rule_y = r.y + text_h + 6;
      M5.Display.fillRect(g_lm.heading_x, r.y + text_h / 2 - 6, 8, 14,
                          TFT_BLACK);
      M5.Display.drawString(r.heading.c_str(), g_lm.heading_x + 18, r.y);
      M5.Display.drawFastHLine(g_lm.heading_x, rule_y,
                               g_lm.screen_w - g_lm.heading_x * 2, TFT_BLACK);
    } else {
      const int ty = r.y + 3;
      M5.Display.setTextDatum(top_left);
      M5.Display.drawString(r.home.c_str(), g_lm.col_home_x, ty);
      M5.Display.setTextDatum(top_center);
      M5.Display.drawString(r.score.c_str(),
                            g_lm.col_score_x + g_lm.col_score_w / 2, ty);
      M5.Display.setTextDatum(top_left);
      M5.Display.drawString(r.away.c_str(), g_lm.col_away_x, ty);
    }
  }

  use_font20();
  // 20px の行は 28px の行の中心に合わせる。上揃えだと浮いて見える。
  const int fact_offset = (lm.row_h - M5.Display.fontHeight()) / 2;
  M5.Display.setTextDatum(top_left);
  for (const auto& r : plan.rows) {
    if (r.kind != PlanRow::kMatch) continue;
    const std::string t = safe_text(r.fact);
    if (t.empty()) continue;  // ファクトが無ければ何も書かない (§4.2)
    M5.Display.drawString(t.c_str(), g_lm.col_fact_x, r.y + fact_offset);
  }
  if (plan.has_overflow()) {
    M5.Display.setTextDatum(top_right);
    const std::string t = fold_unsupported(msg::overflow_text(plan.overflow_count));
    M5.Display.drawString(safe_text(t).c_str(), g_lm.screen_w - 24,
                          plan.overflow_y + 4);
    M5.Display.setTextDatum(top_left);
  }
}

void draw_full(const RenderPlan& plan, const StatusBar& sb) {
  M5.Display.setEpdMode(m5gfx::epd_mode_t::epd_quality);

  // 黒フラッシュ。部分書き換えを重ねた後の面は中間調が残っており、
  // 白で塗るだけでは真っ白に戻らない (背景がグレーに見える原因)。
  // 一度黒で塗ってパネルの全画素を駆動する。
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.display();

  // 押し出しは内容を描き切ってから1回だけ。白で押してから描くと、
  // その間に電源が落ちた場合に白紙が残り、§8.1 の「前回の画面をそのまま
  // 残す」に反する。
  M5.Display.fillScreen(TFT_WHITE);
  draw_body(plan);
  draw_status_contents(sb);
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
