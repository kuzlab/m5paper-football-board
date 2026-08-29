#include "core/text_util.h"

#include <cstring>

namespace fb {
namespace {

constexpr const char* kEllipsis = "\xE2\x80\xA6";  // U+2026 …

struct Fold {
  std::uint32_t cp;
  const char* to;
};

// Latin Extended-A の外側で実際に来るもの。ルーマニア語のコンマ付き、
// クロアチア/セルビアのラテン表記など。ここに無いものは '?' に落ちる。
constexpr Fold kFolds[] = {
    {0x0218, "S"}, {0x0219, "s"},  // Ș ș (Romanian)
    {0x021A, "T"}, {0x021B, "t"},  // Ț ț
    {0x01C4, "DZ"}, {0x01C5, "Dz"}, {0x01C6, "dz"},
    {0x01C7, "LJ"}, {0x01C8, "Lj"}, {0x01C9, "lj"},
    {0x01CA, "NJ"}, {0x01CB, "Nj"}, {0x01CC, "nj"},
    {0x0192, "f"},
    {0x2019, "'"}, {0x2018, "'"},   // ’ ‘ 弯曲アポストロフィ
    {0x201C, "\""}, {0x201D, "\""},
    {0x2013, "-"}, {0x2014, "-"},   // – —
    {0x00A0, " "},                  // NBSP
};

}  // namespace

std::uint32_t utf8_codepoint_at(const std::string& s, std::size_t byte_pos,
                                std::size_t* consumed) {
  const std::size_t n = s.size();
  if (byte_pos >= n) {
    if (consumed) *consumed = 0;
    return 0;
  }
  const auto b0 = static_cast<unsigned char>(s[byte_pos]);
  std::size_t len = 1;
  std::uint32_t cp = b0;
  if (b0 < 0x80) {
    len = 1;
    cp = b0;
  } else if ((b0 & 0xE0) == 0xC0) {
    len = 2;
    cp = b0 & 0x1F;
  } else if ((b0 & 0xF0) == 0xE0) {
    len = 3;
    cp = b0 & 0x0F;
  } else if ((b0 & 0xF8) == 0xF0) {
    len = 4;
    cp = b0 & 0x07;
  } else {
    if (consumed) *consumed = 1;
    return 0xFFFD;  // 継続バイトが単独で現れた
  }
  if (byte_pos + len > n) {
    if (consumed) *consumed = n - byte_pos;
    return 0xFFFD;
  }
  for (std::size_t i = 1; i < len; ++i) {
    const auto bi = static_cast<unsigned char>(s[byte_pos + i]);
    if ((bi & 0xC0) != 0x80) {
      if (consumed) *consumed = i;
      return 0xFFFD;
    }
    cp = (cp << 6) | (bi & 0x3F);
  }
  if (consumed) *consumed = len;
  return cp;
}

std::vector<std::string> utf8_split(const std::string& s) {
  std::vector<std::string> out;
  std::size_t i = 0;
  while (i < s.size()) {
    std::size_t used = 0;
    utf8_codepoint_at(s, i, &used);
    if (used == 0) break;
    out.emplace_back(s.substr(i, used));
    i += used;
  }
  return out;
}

std::string truncate_to_width(const std::string& s, int max_px,
                              const MeasureFn& measure) {
  if (s.empty() || max_px <= 0) return std::string();
  if (measure(s) <= max_px) return s;

  const std::vector<std::string> chars = utf8_split(s);
  const int ell_px = measure(kEllipsis);
  // "…" すら入らないなら諦めて空文字。カラムをはみ出させない方が重要。
  if (ell_px > max_px) return std::string();

  // 収まる最大の接頭辞を線形に探す。1行あたり数十文字なので二分探索は不要。
  std::string best;
  std::string acc;
  for (const auto& ch : chars) {
    acc += ch;
    if (measure(acc + kEllipsis) > max_px) break;
    best = acc;
  }
  if (best.empty()) return kEllipsis;
  return best + kEllipsis;
}

bool is_supported_codepoint(std::uint32_t cp) {
  if (cp < 0x80) return true;                        // ASCII
  if (cp >= 0x00A1 && cp <= 0x00FF) return true;     // Latin-1 Supplement
  if (cp >= 0x0100 && cp <= 0x017F) return true;     // Latin Extended-A
  if (cp >= 0x3000 && cp <= 0x30FF) return true;     // 約物・かな
  if (cp >= 0x4E00 && cp <= 0x9FFF) return true;     // CJK 統合漢字
  if (cp >= 0xFF00 && cp <= 0xFF60) return true;     // 全角英数
  if (cp == 0x2026) return true;                     // …
  if (cp == 0x2192) return true;                     // → (固定エリアの案内)
  if (cp == 0x2103) return true;                     // ℃
  return false;
}

std::string fold_unsupported(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  std::size_t i = 0;
  while (i < s.size()) {
    std::size_t used = 0;
    const std::uint32_t cp = utf8_codepoint_at(s, i, &used);
    if (used == 0) break;
    if (is_supported_codepoint(cp)) {
      out.append(s, i, used);
    } else {
      const char* repl = "?";
      for (const auto& f : kFolds) {
        if (f.cp == cp) {
          repl = f.to;
          break;
        }
      }
      out += repl;
    }
    i += used;
  }
  return out;
}

}  // namespace fb
