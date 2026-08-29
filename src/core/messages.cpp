#include "core/messages.h"

#include <cstdio>
#include <cstring>

namespace fb {
namespace msg {

std::size_t fact_text(const Fact& f, char* buf, std::size_t n) {
  if (!buf || n == 0) return 0;
  buf[0] = '\0';
  int w = 0;
  switch (f.type) {
    case FACT_OPENING_STREAK:
      w = snprintf(buf, n, "開幕%d連勝", f.count);
      break;
    case FACT_GIANT_KILLING:
      // count は敗者の順位。順位に応じて表現を変える。
      // 何部のチームが勝ったかは追加リクエストなしには分からないので、
      // 「格下が格上を倒した」以上のことは言わない (§4.1 追補)。
      if (f.count <= 4) {
        w = snprintf(buf, n, "大金星");
      } else if (f.count <= 10) {
        w = snprintf(buf, n, "格上撃破");
      } else {
        w = snprintf(buf, n, "番狂わせ");
      }
      break;
    case FACT_ZONE_ENTER:
      w = snprintf(buf, n, "%s%s", f.zone_name.c_str(),
                   f.positive ? "浮上" : "転落");
      break;
    case FACT_ZONE_LEAVE:
      w = snprintf(buf, n, "%s%s", f.zone_name.c_str(),
                   f.positive ? "陥落" : "脱出");
      break;
    case FACT_UPSET:
      w = snprintf(buf, n, "番狂わせ");
      break;
    case FACT_TOP_OF_TABLE:
      w = snprintf(buf, n, "首位");
      break;
    case FACT_WIN_STREAK:
      w = snprintf(buf, n, "%d連勝", f.count);
      break;
    case FACT_LOSS_STREAK:
      w = snprintf(buf, n, "%d連敗", f.count);
      break;
    case FACT_STREAK_BROKEN:
      w = snprintf(buf, n, "連敗脱出");
      break;
    case FACT_UNBEATEN:
      w = snprintf(buf, n, "%d試合無敗", f.count);
      break;
    case FACT_RANK_CHANGE:
      w = snprintf(buf, n, "%dつ順位%s", f.count, f.positive ? "UP" : "DOWN");
      break;
    case FACT_BIG_WIN:
      w = snprintf(buf, n, "大勝");
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
  snprintf(buf, sizeof(buf), "ほか %d試合", remaining);
  return std::string(buf);
}

}  // namespace msg
}  // namespace fb
