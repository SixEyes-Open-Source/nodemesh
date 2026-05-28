#pragma once

#include "nodemesh/experience_packet.h"
#include <stddef.h>
#include <stdint.h>

namespace node0 {

class UartInput {
public:
  static UartInput &instance();
  void begin();
  bool tryRead(nodemesh::ExperiencePacket &packet);

private:
  UartInput() = default;
  bool fillRxBuffer();
  bool popPacket(nodemesh::ExperiencePacket &packet);

  static constexpr size_t kPacketBytes = sizeof(nodemesh::ExperiencePacket);
  static constexpr size_t kRxBufferBytes = kPacketBytes * 4;

  uint8_t rx_buffer_[kRxBufferBytes]{};
  size_t rx_len_ = 0;
};

} // namespace node0
