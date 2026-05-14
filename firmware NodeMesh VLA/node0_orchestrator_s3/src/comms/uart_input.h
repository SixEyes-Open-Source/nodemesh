#pragma once

#include "nodemesh/experience_packet.h"

namespace node0 {

class UartInput {
public:
  static UartInput &instance();
  void begin();
  bool tryRead(nodemesh::ExperiencePacket &packet);

private:
  UartInput() = default;
};

} // namespace node0
