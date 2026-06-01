#include "adc_reader.h"

#include "board_pins.h"
#include <Arduino.h>

namespace node1 {

void AdcReader::begin() {
  analogReadResolution(12);
}

std::array<float, 6> AdcReader::readJointAngles() {
  const int raw[6] = {
      analogRead(kAdcJ1), analogRead(kAdcJ2), analogRead(kAdcJ3),
      analogRead(kAdcJ4), analogRead(kAdcJ5), analogRead(kAdcJ6)};

  std::array<float, 6> out{};
  for (size_t i = 0; i < out.size(); ++i) {
    const float sample = static_cast<float>(raw[i]) / 4095.0f;
    if (ema_[i] < 0.f) {
      ema_[i] = sample;  // seed on first call
    } else {
      ema_[i] += kAlpha * (sample - ema_[i]);
    }
    out[i] = ema_[i];
  }
  return out;
}

} // namespace node1
