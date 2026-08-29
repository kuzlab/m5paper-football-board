// 文字列ユーティリティ。防御的レイアウトの土台 (§5.3)。
// 幅の実測関数を外から注入するので、ネイティブでテストできる。
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace fb {

// 文字列を描画したときの幅をピクセルで返す。実機では M5GFX の textWidth()、
// テストではダミー実装を渡す。
using MeasureFn = std::function<int(const std::string&)>;

// UTF-8 文字列をコードポイント単位に分解する (不正バイトは U+FFFD 扱い)。
std::vector<std::string> utf8_split(const std::string& s);
std::uint32_t utf8_codepoint_at(const std::string& s, std::size_t byte_pos,
                                std::size_t* consumed);

// 文字数ではなくピクセル幅で切り詰め、末尾に "…" を付ける (§5.3)。
// 収まる場合は原文をそのまま返す。
std::string truncate_to_width(const std::string& s, int max_px,
                              const MeasureFn& measure);

// VLW に収録していないグリフを、収録済みの近い文字に畳む (§5.4)。
// 収録範囲: ASCII / Latin-1 Supplement / Latin Extended-A / 日本語。
// それ以外 (キリル文字など) は '?' になる。豆腐や描画崩れを避けるための保険。
std::string fold_unsupported(const std::string& s);

// 収録範囲内かどうか。fold_unsupported の判定と同じ。
bool is_supported_codepoint(std::uint32_t cp);

}  // namespace fb
