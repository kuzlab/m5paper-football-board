#include "power.h"

#include <Arduino.h>
#include <M5Unified.h>

#include "core/season.h"
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

WakeReason wake_reason() {
  std::uint8_t c2 = 0;
  if (!read_reg(kRegControl2, &c2, 1)) {
    LOGW("RTC: control2 read failed, assuming button wake");
    return WakeReason::kUnknown;
  }
  // AF が立っていればアラームで起きた。電源が切れている間ボタン割り込みは
  // 走らないので、AF が立っていなければ人が電源ボタンを押した (決定事項2)。
  return (c2 & kBitAlarmFlag) ? WakeReason::kRtcAlarm : WakeReason::kButton;
}

void clear_alarm_flag() {
  std::uint8_t c2 = 0;
  if (!read_reg(kRegControl2, &c2, 1)) return;
  c2 &= static_cast<std::uint8_t>(~kBitAlarmFlag);
  write_reg(kRegControl2, &c2, 1);
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

bool set_alarm_utc(std::time_t when_utc) {
  std::tm tmv{};
  gmtime_r(&when_utc, &tmv);

  // BM8563 のアラームは分・時・日・曜日。曜日は使わない (0x80 で無効化)。
  const std::uint8_t want[4] = {
      to_bcd(tmv.tm_min),   // 0x09 minute  (bit7=0 で有効)
      to_bcd(tmv.tm_hour),  // 0x0A hour
      to_bcd(tmv.tm_mday),  // 0x0B day
      0x80,                 // 0x0C weekday: 無効
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
  std::uint8_t c2b = 0;
  if (!read_reg(kRegControl2, &c2b, 1) || !(c2b & kBitAlarmEnable)) {
    LOGE("RTC: AIE not set (control2=%02X)", c2b);
    return false;
  }

  LOGI("RTC: alarm set for %04d-%02d-%02d %02d:%02dZ", tmv.tm_year + 1900,
       tmv.tm_mon + 1, tmv.tm_mday, tmv.tm_hour, tmv.tm_min);
  return true;
}

float battery_volt() {
  return static_cast<float>(M5.Power.getBatteryVoltage()) / 1000.0f;
}

int battery_percent() {
  const int p = M5.Power.getBatteryLevel();
  return (p < 0) ? 0 : (p > 100 ? 100 : p);
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
  M5.Power.powerOff();
  // powerOff() が効かない (USB 給電中など) 場合の保険。
  delay(3000);
  LOGW("powerOff() did not take effect (USB powered?)");
  log::flush();
  delay(57000);
  ESP.restart();
#endif
}

}  // namespace power
}  // namespace fb
