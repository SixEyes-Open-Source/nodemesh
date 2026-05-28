#pragma once

#include <stddef.h>
#include <stdint.h>

namespace nodemesh {

constexpr uint32_t kPacketMagic = 0x4E4D5650; // NMVP
constexpr uint8_t kPacketVersion = 1;
constexpr size_t kJointCount = 6;
constexpr size_t kVisionFeatureBytes = 128;

#pragma pack(push, 1)
struct ExperiencePacket {
  uint32_t magic;
  uint8_t version;
  uint8_t source_node;
  uint16_t payload_len;
  uint32_t timestamp_us;
  uint32_t seq;           // monotonic per-source sequence number for gap detection
  float joints[kJointCount];
  uint8_t vision_features[kVisionFeatureBytes];
  uint16_t crc16;
};
#pragma pack(pop)

} // namespace nodemesh
