#include "sht30.h"

#include <Arduino.h>
#include <M5Unified.h>

namespace fb {
namespace sht30 {
namespace {

constexpr std::uint8_t kAddr = 0x44;
constexpr std::uint32_t kFreq = 100000;

// CRC-8 (poly 0x31, init 0xFF)。センサが返す1バイトのチェックサム。
std::uint8_t crc8(const std::uint8_t* d, std::size_t n) {
  std::uint8_t crc = 0xFF;
  for (std::size_t i = 0; i < n; ++i) {
    crc ^= d[i];
    for (int b = 0; b < 8; ++b) {
      crc = (crc & 0x80) ? static_cast<std::uint8_t>((crc << 1) ^ 0x31)
                         : static_cast<std::uint8_t>(crc << 1);
    }
  }
  return crc;
}

}  // namespace

Reading measure() {
  Reading r;
  // 0x2400: high repeatability, clock stretching disabled
  const std::uint8_t cmd[2] = {0x24, 0x00};
  if (!M5.In_I2C.start(kAddr, false, kFreq)) return r;
  const bool wrote = M5.In_I2C.write(cmd, 2);
  M5.In_I2C.stop();
  if (!wrote) return r;

  delay(20);  // 高精度測定は最大 15ms

  std::uint8_t buf[6] = {0};
  if (!M5.In_I2C.start(kAddr, true, kFreq)) return r;
  const bool read_ok = M5.In_I2C.read(buf, 6);
  M5.In_I2C.stop();
  if (!read_ok) return r;

  if (crc8(buf, 2) != buf[2] || crc8(buf + 3, 2) != buf[5]) return r;

  const std::uint16_t raw_t = (buf[0] << 8) | buf[1];
  const std::uint16_t raw_h = (buf[3] << 8) | buf[4];
  r.temperature_c = -45.0f + 175.0f * static_cast<float>(raw_t) / 65535.0f;
  r.humidity_pct = 100.0f * static_cast<float>(raw_h) / 65535.0f;
  r.ok = true;
  return r;
}

}  // namespace sht30
}  // namespace fb
