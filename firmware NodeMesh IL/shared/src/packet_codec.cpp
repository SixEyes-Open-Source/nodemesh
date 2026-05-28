#include "nodemesh/packet_codec.h"

namespace nodemesh {

uint16_t crc16_ccitt(const uint8_t *data, size_t len) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < len; ++i) {
    crc ^= static_cast<uint16_t>(data[i]) << 8;
    for (uint8_t b = 0; b < 8; ++b) {
      crc = (crc & 0x8000) ? static_cast<uint16_t>((crc << 1) ^ 0x1021)
                           : static_cast<uint16_t>(crc << 1);
    }
  }
  return crc;
}

bool finalize_packet(ExperiencePacket &packet) {
  packet.magic = kPacketMagic;
  packet.version = kPacketVersion;
  packet.payload_len = static_cast<uint16_t>(sizeof(ExperiencePacket));
  packet.crc16 = 0;

  const auto crc = crc16_ccitt(reinterpret_cast<const uint8_t *>(&packet),
                               sizeof(ExperiencePacket));
  packet.crc16 = crc;
  return true;
}

bool validate_packet(const ExperiencePacket &packet) {
  if (packet.magic != kPacketMagic || packet.version != kPacketVersion) {
    return false;
  }

  ExperiencePacket copy = packet;
  const auto expected = copy.crc16;
  copy.crc16 = 0;
  const auto actual = crc16_ccitt(reinterpret_cast<const uint8_t *>(&copy),
                                  sizeof(ExperiencePacket));
  return actual == expected;
}

} // namespace nodemesh
