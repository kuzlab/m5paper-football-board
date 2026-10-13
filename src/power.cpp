#include "power.h"

#include <Arduino.h>
#include <M5Unified.h>
#include <esp_system.h>

#include "core/datetime.h"
#include "logging.h"

namespace fb {
namespace power {
namespace {

// BM8563
constexpr std::uint8_t kRtcAddr = 0x51;
constexpr std::uint8_t kRegControl2 = 0x01;
constexpr std::uint8_t kRegAlarmMinute = 0x09;  // 0x09..0x0C
constexpr std::uint32_t kI2cFreq = 400000;

constexpr std::uint8_t kBitAlarmFlag = 0x08;    // AF
constexpr std::uint8_t kBitAlarmEnable = 0x02;  // AIE

constexpr std::uint8_t kDisableMatch = 0x80;  // bit7=1 でその項目を照合しない

std::uint8_t to_bcd(int v) {
  return static_cast<std::uint8_t>(((v / 10) << 4) | (v % 10));
}

bool read_reg(std::uint8_t reg, std::uint8_t* buf, std::size_t len) {
  return M5.In_I2C.readRegister(kRtcAddr, reg, buf, len, kI2cFreq);
}

bool write_reg(std::uint8_t reg, const std::uint8_t* buf, std::size_t len) {
  return M5.In_I2C.writeRegister(kRtcAddr, reg, buf, len, kI2cFreq);
}

}  // namespace

const char* reset_reason_text() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON:  return "poweron";   // 電源ラッチが入った = 正常
    case ESP_RST_BROWNOUT: return "BROWNOUT";  // 電圧不足。電池を疑う
    case ESP_RST_PANIC:    return "PANIC";
    case ESP_RST_INT_WDT:  return "INT_WDT";
    case ESP_RST_TASK_WDT: return "TASK_WDT";
    case ESP_RST_WDT:      return "WDT";
    case ESP_RST_SW:       return "sw";
    case ESP_RST_DEEPSLEEP: return "deepsleep";
    case ESP_RST_EXT:      return "ext";
    default:               return "unknown";
  }
}

WakeInfo wake_info(std::time_t now_utc, std::time_t scheduled_utc) {
  WakeInfo info;
  info.reset_reason = reset_reason_text();

  if (!read_reg(kRegControl2, &info.control2, 1)) {
    LOGW("RTC: control2 read failed");
  } else {
    info.alarm_flag = (info.control2 & kBitAlarmFlag) != 0;
  }

  // 予定時刻との照合。AF だけに頼ると、M5.begin() がフラグを消す構成
  // (disable_rtc_irq=true) に戻ったときに静かに誤判定する。
  if (scheduled_utc > 0 && now_utc > 0) {
    const std::time_t diff = now_utc - scheduled_utc;
    const std::time_t abs_diff = diff < 0 ? -diff : diff;
    info.schedule_matched = (abs_diff <= 20 * 60);
  }

  if (info.alarm_flag || info.schedule_matched) {
    info.reason = WakeReason::kRtcAlarm;
  } else {
    // 電源が落ちている間ボタン割り込みは走らないので、どちらでもなければ
    // 人が電源ボタンを押した (決定事項2)。
    info.reason = WakeReason::kButton;
  }
  return info;
}

bool clear_alarm_flag() {
  for (int i = 0; i < 3; ++i) {
    std::uint8_t c2 = 0;
    if (!read_reg(kRegControl2, &c2, 1)) {
      delay(5);
      continue;
    }
    if (!(c2 & kBitAlarmFlag)) return true;  // すでに落ちている
    c2 &= static_cast<std::uint8_t>(~kBitAlarmFlag);
    write_reg(kRegControl2, &c2, 1);
    delay(5);
  }
  LOGE("RTC: could not clear the alarm flag (INT stays asserted)");
  return false;
}

std::time_t next_daily_alarm_utc(std::time_t now_utc, int hour_local,
                                 int min_local, int tz_offset_min,
                                 int skip_days) {
  const std::time_t offset = static_cast<std::time_t>(tz_offset_min) * 60;
  const std::time_t local_now = now_utc + offset;
  // 現地の日付の 00:00
  const std::time_t local_midnight = (local_now / 86400) * 86400;
  std::time_t target_local =
      local_midnight + hour_local * 3600 + min_local * 60;
  // すでに過ぎていたら翌日。手動起床でも「翌日の定時」になる (§6.4)。
  if (target_local <= local_now) target_local += 86400;
  target_local += static_cast<std::time_t>(skip_days) * 86400;
  return target_local - offset;
}

bool set_alarm_utc(std::time_t when_utc, bool match_day) {
  std::tm tmv{};
  gmtime_r(&when_utc, &tmv);

  // BM8563 のアラームは分・時・日・曜日。曜日は使わない (0x80 で無効化)。
  // 日も照合しないのが既定。そうすると毎日同時刻のアラームになり、
  // 再設定できないまま落ちても翌日また起きる (2026-09-22 の再発防止)。
  const std::uint8_t want[4] = {
      to_bcd(tmv.tm_min),                               // 0x09 minute
      to_bcd(tmv.tm_hour),                              // 0x0A hour
      match_day ? to_bcd(tmv.tm_mday) : kDisableMatch,  // 0x0B day
      kDisableMatch,                                    // 0x0C weekday
  };
  if (!write_reg(kRegAlarmMinute, want, 4)) {
    LOGE("RTC: alarm write failed");
    return false;
  }

  // AIE を立て、AF を落とす。
  std::uint8_t c2 = 0;
  if (!read_reg(kRegControl2, &c2, 1)) {
    LOGE("RTC: control2 read failed");
    return false;
  }
  c2 |= kBitAlarmEnable;
  c2 &= static_cast<std::uint8_t>(~kBitAlarmFlag);
  if (!write_reg(kRegControl2, &c2, 1)) {
    LOGE("RTC: control2 write failed");
    return false;
  }

  // 読み返して検証する。M5Unified の API は成否を返さないことがあるので、
  // レジスタの実値で確認する。ここが通らない限り電源を切ってはいけない。
  std::uint8_t got[4] = {0};
  if (!read_reg(kRegAlarmMinute, got, 4)) {
    LOGE("RTC: alarm readback failed");
    return false;
  }
  for (int i = 0; i < 3; ++i) {
    if (got[i] != want[i]) {
      LOGE("RTC: alarm mismatch at %d: want %02X got %02X", i, want[i], got[i]);
      return false;
    }
  }
  // 曜日レジスタは未使用ビットの読み値が保証されないので、照合無効ビット
  // (bit7) だけを見る。ここで厳密比較すると電源を切れなくなる恐れがある。
  if (!(got[3] & kDisableMatch)) {
    LOGE("RTC: weekday match not disabled (%02X)", got[3]);
    return false;
  }
  std::uint8_t c2b = 0;
  if (!read_reg(kRegControl2, &c2b, 1) || !(c2b & kBitAlarmEnable)) {
    LOGE("RTC: AIE not set (control2=%02X)", c2b);
    return false;
  }

  if (match_day) {
    LOGI("RTC: alarm set for %04d-%02d-%02d %02d:%02dZ", tmv.tm_year + 1900,
         tmv.tm_mon + 1, tmv.tm_mday, tmv.tm_hour, tmv.tm_min);
  } else {
    LOGI("RTC: alarm set for %02d:%02dZ daily", tmv.tm_hour, tmv.tm_min);
  }
  return true;
}

bool set_alarm_after_minutes(int minutes) {
  // RTC の生の値を使う。年が壊れていても時分だけは進んでいるので、
  // 「n 分後」は正しく設定できる。
  // 現在と同じ分を武装すると即時に AF が立つ恐れがあるため、1440分の
  // 倍数は避ける (24時間後は 1439 分後として扱う)。
  if (minutes % 1440 == 0) minutes = (minutes / 1440) * 1440 - 1;
  const auto dt = M5.Rtc.getDateTime();
  int utc_min = dt.time.hours * 60 + dt.time.minutes + minutes;
  utc_min %= 1440;
  if (utc_min < 0) utc_min += 1440;
  LOGW("RTC: time unknown -> retry in %d min (%02d:%02d by RTC)", minutes,
       utc_min / 60, utc_min % 60);
  return set_alarm_utc(static_cast<std::time_t>(utc_min) * 60, false);
}

float battery_volt() {
  return static_cast<float>(M5.Power.getBatteryVoltage()) / 1000.0f;
}

int battery_percent() {
  const int p = M5.Power.getBatteryLevel();
  return (p < 0) ? 0 : (p > 100 ? 100 : p);
}

int charging() {
  const auto c = M5.Power.isCharging();
  if (c == m5::Power_Class::is_charging_t::is_charging) return 1;
  if (c == m5::Power_Class::is_charging_t::is_discharging) return 0;
  return -1;
}

void log_battery(const char* phase) {
  // 残量% は M5Unified の (mV-3300)/8 という直線近似にすぎない。
  // 後から放電カーブを引けるよう、電圧をそのまま残す。
  LOGI("batt %s: %.3fV %d%% charging=%d", phase, battery_volt(),
       battery_percent(), charging());
}

void shutdown() {
  log::flush();
#ifdef DEV_NO_POWEROFF
  // 開発中は電源断の代わりにログ + 長い delay (§6.2-3)。
  LOGI("DEV_NO_POWEROFF: would power off here");
  log::flush();
  Serial.flush();
  delay(60000);
  ESP.restart();
#else
  Serial.flush();
  // EXT の 5V 昇圧を明示的に落とす (既定でも off だが意図を残す)。
  M5.Power.setExtOutput(false);
  M5.Power.powerOff();
  // M5Paper の powerOff() はラッチを切れなかった場合 deep sleep に落ちる
  // ので、ここへは戻ってこない。戻ってきたら何かが根本的に違う。
  delay(3000);
  LOGW("powerOff() returned (USB powered?)");
  log::flush();
  delay(57000);
  ESP.restart();
#endif
}

}  // namespace power
}  // namespace fb
