#pragma once

#include "nodemesh/experience_packet.h"

namespace node0 {

class VlaStub {
public:
  static VlaStub &instance();
  void begin();
  void observe(const nodemesh::ExperiencePacket &packet);
  void trainStep();

private:
  VlaStub() = default;
};

} // namespace node0
