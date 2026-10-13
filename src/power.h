// RTC アラームと電源ラッチ (§6.2)。単体構成で最大の罠。
//
// M5Paper は deep sleep ではなく電源ラッチで完全に電源を切る。起床は
// コールドブートであり、RAM も RTC メモリも保持されない。
//
// USB 給電中は電源断が正しく動作しない。開発中に「電源が切れない」ように
// 見えるのは正常。バッテリー駆動でのみ本来の動作をする (§6.2-2)。
//
// 重要: M5.config() の disable_rtc_irq を false にしておくこと。
// 既定の true では M5.begin() が BM8563 の 0x09-0x0C と 0x01 を
// ゼロクリアするため、(1) 起床理由の AF が毎回消えて全部「ボタン起動」に
// 見え、(2) 武装済みのアラームが起動のたびに解除される。(2) は致命的で、
// 起動直後にブラウンアウトすると二度と自動起床しなくなる。
#pragma once

#include <cstdint>
#include <ctime>

namespace fb {
namespace power {

enum class WakeReason {
  kRtcAlarm,   // BM8563 のアラーム = 定時の自動更新
  kButton,     // 電源ボタンによる手動起動 = 手動更新 (決定事項2)
  kUnknown,
};

struct WakeInfo {
  WakeReason reason = WakeReason::kUnknown;
  bool alarm_flag = false;      // BM8563 の AF
  bool schedule_matched = false;  // NVS に保存した予定時刻と一致した
  std::uint8_t control2 = 0;
  const char* reset_reason = "?";  // esp_reset_reason() の文字列
};

// 起床理由を判定する。AF (ハード) と NVS の予定時刻 (ソフト) の両方を見て、
// どちらかが自動起床を示せば自動とする。AF は取りこぼしの歴史があるため
// 片方だけに頼らない。scheduled_utc が 0 なら予定時刻の照合はしない。
WakeInfo wake_info(std::time_t now_utc, std::time_t scheduled_utc);
// AF を落とす。INT を解放し、次回の判定を正しくするために必ず呼ぶ。
// AF が立ったまま電源を切ると INT が張られ続け、電源が入り直してしまう。
// 読み返して確認し、落ちるまで数回試す。落とせなければ false。
bool clear_alarm_flag();

// 次回アラームを UTC の時刻で設定し、レジスタを読み返して検証する。
//
// match_day=false は「毎日同時刻」になるが、BM8563 は分・時が現在値と一致した
// 瞬間に AF を立てる可能性があり、起床と同じ分のうちに同じ時刻を武装すると
// そのまま再起動を繰り返しかねない。通常は match_day=true (翌日の日付) を使い、
// 日付を使えないとき (時刻不明) は set_alarm_after_minutes() を使う。
// 検証に失敗したら false。呼び出し側は false のとき電源を切ってはならない
// (二度と起きなくなる、§6.2)。
bool set_alarm_utc(std::time_t when_utc, bool match_day = true);

// RTC の現在値から n 分後に設定する。時刻が不明なとき (電池切れ後の初回で
// SNTP も失敗) の再試行用。絶対時刻が分からなくても必ず起きられる。
bool set_alarm_after_minutes(int minutes);

// 現地時刻 hh:mm (tz_offset_min) の「次の」出現を UTC で返す。
// 手動起床でも翌日の定時に設定する。現在時刻 + 24時間としない (§6.4)。
std::time_t next_daily_alarm_utc(std::time_t now_utc, int hour_local,
                                 int min_local, int tz_offset_min,
                                 int skip_days = 0);

// 電池電圧 [V]
float battery_volt();
int battery_percent();
// 充電中か。1=充電中 0=非充電 -1=不明 (M5Paper は検出線が無く不明になる)
int charging();
// 電池の状態を1行でログに残す。電池切れの原因は後から電圧でしか追えない。
void log_battery(const char* phase);

// 電源断。DEV_NO_POWEROFF が定義されていればログを出して待つだけにする。
void shutdown();

}  // namespace power
}  // namespace fb
