#pragma once

#include <array>

namespace node1 {

class AdcReader {
public:
  void begin();
  std::array<float, 6> readJointAngles();
};

} // namespace node1
