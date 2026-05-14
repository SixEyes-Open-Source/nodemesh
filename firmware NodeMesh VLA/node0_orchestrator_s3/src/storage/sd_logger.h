#pragma once

#include "nodemesh/experience_packet.h"
#include "nodemesh/ring_buffer.h"

namespace node0 {

class SdLogger {
public:
  static SdLogger &instance();
  bool begin();
  bool enqueue(const nodemesh::ExperiencePacket &packet);
  void flushOnce();

private:
  SdLogger() = default;
  nodemesh::RingBuffer<nodemesh::ExperiencePacket, 128> queue_;
};

} // namespace node0
