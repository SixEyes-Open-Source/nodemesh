#pragma once

#include "nodemesh/experience_packet.h"
#include <stddef.h>

namespace nodemesh {

uint16_t crc16_ccitt(const uint8_t *data, size_t len);
bool finalize_packet(ExperiencePacket &packet);
bool validate_packet(const ExperiencePacket &packet);

} // namespace nodemesh
