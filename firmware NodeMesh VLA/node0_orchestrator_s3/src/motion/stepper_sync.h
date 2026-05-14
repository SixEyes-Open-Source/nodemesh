#pragma once

namespace node0 {

// Mirrors shoulder pulses to both driver channels for lockstep motion.
class ShoulderMirrorStepper {
public:
  void begin();
  void setEnable(bool enabled);
  void pulse(bool dir_high);
};

} // namespace node0
