#pragma once

#include "calibration_config.h"
#include "nodemesh/experience_packet.h"
#include <array>

namespace node0 {

class IkSolver {
public:
  static IkSolver &instance();
  void begin();
  bool solveFromPacket(const nodemesh::ExperiencePacket &packet,
                       std::array<float, 6> &joint_targets_deg);

private:
  IkSolver() = default;
  bool solvePlanar2Link(float x_mm, float y_mm, float &shoulder_deg,
                        float &elbow_deg) const;
};

} // namespace node0
