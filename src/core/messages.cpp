#include "core/messages.h"

#include <cstdio>
#include <cstring>

namespace fb {
namespace msg {
namespace {

// "place" / "places" のような単複の切り替え。
const char* plural(int n, const char* one, const char* many) {
  return n == 1 ? one : many;
}

}  // namespace

std::size_t fact_text(const Fact& f, char* buf, std::size_t n) {
  if (!buf || n == 0) return 0;
  buf[0] = '\0';
  int w = 0;
  switch (f.type) {
    case FACT_OPENING_STREAK:
      w = snprintf(buf, n, "Won all %d", f.count);
      break;
    case FACT_GIANT_KILLING:
      // count は敗者の順位。順位に応じて表現を変える。
      // 何部のチームが勝ったかは追加リクエストなしには分からないので、
      // 「格下が格上を倒した」以上のことは言わない (§4.1 追補)。
      if (f.count <= 4) {
        w = snprintf(buf, n, "Huge upset");
      } else if (f.count <= 10) {
        w = snprintf(buf, n, "Beat a top side");
      } else {
        w = snprintf(buf, n, "Cup upset");
      }
      break;
    case FACT_ZONE_ENTER:
      w = snprintf(buf, n, "%s into %s", f.positive ? "Up" : "Down",
                   f.zone_name.c_str());
      break;
    case FACT_ZONE_LEAVE:
      w = snprintf(buf, n, "Out of %s", f.zone_name.c_str());
      break;
    case FACT_UPSET:
      w = snprintf(buf, n, "Upset");
      break;
    case FACT_TOP_OF_TABLE:
      w = snprintf(buf, n, "Top of the table");
      break;
    case FACT_WIN_STREAK:
      w = snprintf(buf, n, "%d wins in a row", f.count);
      break;
    case FACT_LOSS_STREAK:
      w = snprintf(buf, n, "%d losses in a row", f.count);
      break;
    case FACT_STREAK_BROKEN:
      w = snprintf(buf, n, "Losing run ends");
      break;
    case FACT_UNBEATEN:
      w = snprintf(buf, n, "%d unbeaten", f.count);
      break;
    case FACT_RANK_CHANGE:
      w = snprintf(buf, n, "%s %d %s", f.positive ? "Up" : "Down", f.count,
                   plural(f.count, "place", "places"));
      break;
    case FACT_BIG_WIN:
      w = snprintf(buf, n, "Big win");
      break;
    case FACT_NONE:
    default:
      return 0;
  }
  if (w < 0) {
    buf[0] = '\0';
    return 0;
  }
  return strnlen(buf, n);
}

std::string fact_text(const Fact& f) {
  char buf[48];
  const std::size_t len = fact_text(f, buf, sizeof(buf));
  return std::string(buf, len);
}

std::string overflow_text(int remaining) {
  char buf[32];
  snprintf(buf, sizeof(buf), "+%d more", remaining);
  return std::string(buf);
}

}  // namespace msg
}  // namespace fb
