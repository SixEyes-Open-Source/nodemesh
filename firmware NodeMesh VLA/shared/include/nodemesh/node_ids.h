#pragma once

#include <stdint.h>

namespace nodemesh {

enum class NodeId : uint8_t {
  kNode0Orchestrator = 0,
  kNode1Input = 1,
  kNode2CamGlobal = 2,
  kNode3CamWrist = 3,
};

} // namespace nodemesh
