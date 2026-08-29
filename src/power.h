// RTC アラームと電源ラッチ (§6.2)。単体構成で最大の罠。
//
// M5Paper は deep sleep ではなく電源ラッチで完全に電源を切る。起床は
// コールドブートであり、RAM も RTC メモリも保持されない。
//
// USB 給電中は電源断が正しく動作しない。開発中に「電源が切れない」ように
// 見えるのは正常。バッテリー駆動でのみ本来の動作をする (§6.2-2)。
#pragma once

#include <ctime>

namespace fb {
namespace power {

enum class WakeReason {
  kRtcAlarm,   // BM8563 のアラーム = 定時の自動更新
  kButton,     // 電源ボタンによる手動起動 = 手動更新 (決定事項2)
  kUnknown,
};

// BM8563 のアラームフラグ (AF) を読んで起床理由を判定する。
// GPIO では判定できない。電源が落ちている間にボタン割り込みは走らないため。
WakeReason wake_reason();
// AF を落とす。次回の判定を正しくするために必ず呼ぶ。
void clear_alarm_flag();

// 次回アラームを UTC の時刻で設定し、レジスタを読み返して検証する。
// 検証に失敗したら false。呼び出し側は false のとき電源を切ってはならない
// (二度と起きなくなる、§6.2)。
bool set_alarm_utc(std::time_t when_utc);

// 現地時刻 hh:mm (tz_offset_min) の「次の」出現を UTC で返す。
// 手動起床でも翌日の定時に設定する。現在時刻 + 24時間としない (§6.4)。
std::time_t next_daily_alarm_utc(std::time_t now_utc, int hour_local,
                                 int min_local, int tz_offset_min,
                                 int skip_days = 0);

// 電池電圧 [V]
float battery_volt();
int battery_percent();

// 電源断。DEV_NO_POWEROFF が定義されていればログを出して待つだけにする。
void shutdown();

}  // namespace power
}  // namespace fb
