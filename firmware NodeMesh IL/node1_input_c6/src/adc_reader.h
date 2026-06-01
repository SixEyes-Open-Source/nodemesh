#pragma once

#include <array>

namespace node1 {

class AdcReader {
public:
  void begin();
  std::array<float, 6> readJointAngles();

private:
  // EMA filter state. Initialised to -1 to signal "not yet seeded".
  float ema_[6] = {-1.f, -1.f, -1.f, -1.f, -1.f, -1.f};

  // Smoothing factor: 0 = never updates, 1 = no filtering.
  // 0.2 @ 250 Hz gives a ~20 ms time constant — removes ADC jitter
  // while keeping motion lag well below the 4 ms send interval.
  static constexpr float kAlpha = 0.2f;
};

} // namespace node1
