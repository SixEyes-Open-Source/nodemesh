#pragma once

#include "nodemesh/experience_packet.h"
#include <stdint.h>

namespace node0 {

class CamInput {
public:
  static CamInput &instance();
  bool begin();
  void tick();
  void mergeIntoPacket(nodemesh::ExperiencePacket &packet);

private:
  CamInput() = default;
};

} // namespace node0
