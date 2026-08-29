// SHT30 温湿度センサ (§6.1)。M5Unified は環境センサを持たないので最小実装。
// I2C アドレス 0x44、単発測定 (high repeatability, clock stretch disabled)。
#pragma once

namespace fb {
namespace sht30 {

struct Reading {
  bool ok = false;
  float temperature_c = 0.0f;
  float humidity_pct = 0.0f;
};

Reading measure();

}  // namespace sht30
}  // namespace fb
