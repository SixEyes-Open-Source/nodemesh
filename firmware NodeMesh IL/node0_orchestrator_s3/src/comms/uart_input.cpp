#include "comms/uart_input.h"

#include "app_config.h"
#include "board_pins.h"
#include "nodemesh/packet_codec.h"
#include <Arduino.h>
#include <string.h>

namespace node0 {

UartInput &UartInput::instance() {
  static UartInput inst;
  return inst;
}

void UartInput::begin() {
  Serial1.begin(kNode1Baud, SERIAL_8N1, kUartRxFromNode1, kUartTxToNode1);
}

bool UartInput::tryRead(nodemesh::ExperiencePacket &packet) {
  fillRxBuffer();
  return popPacket(packet);
}

bool UartInput::fillRxBuffer() {
  const int available = Serial1.available();
  if (available <= 0) {
    return false;
  }

  const size_t space = kRxBufferBytes - rx_len_;
  const size_t to_read = static_cast<size_t>(available) < space
                             ? static_cast<size_t>(available)
                             : space;

  if (to_read > 0) {
    const auto read = Serial1.readBytes(
        reinterpret_cast<char *>(rx_buffer_ + rx_len_), to_read);
    rx_len_ += static_cast<size_t>(read);
    return read > 0;
  }

  // Buffer is full and no packet has been consumed yet. Keep only the newest
  // partial frame window so parsing can resync without unbounded growth.
  const size_t keep = kPacketBytes - 1;
  memmove(rx_buffer_, rx_buffer_ + (rx_len_ - keep), keep);
  rx_len_ = keep;
  return false;
}

bool UartInput::popPacket(nodemesh::ExperiencePacket &packet) {
  if (rx_len_ < kPacketBytes) {
    return false;
  }

  const size_t max_start = rx_len_ - kPacketBytes;
  for (size_t start = 0; start <= max_start; ++start) {
    nodemesh::ExperiencePacket candidate{};
    memcpy(&candidate, rx_buffer_ + start, kPacketBytes);

    if (candidate.magic != nodemesh::kPacketMagic) {
      continue;
    }
    if (candidate.version != nodemesh::kPacketVersion) {
      continue;
    }
    if (candidate.payload_len != static_cast<uint16_t>(kPacketBytes)) {
      continue;
    }
    if (!nodemesh::validate_packet(candidate)) {
      continue;
    }

    packet = candidate;
    const size_t consumed = start + kPacketBytes;
    const size_t remaining = rx_len_ - consumed;
    if (remaining > 0) {
      memmove(rx_buffer_, rx_buffer_ + consumed, remaining);
    }
    rx_len_ = remaining;
    return true;
  }

  // No valid packet found. Keep a tail window in case frame boundary straddles
  // the next read.
  const size_t keep = kPacketBytes - 1;
  memmove(rx_buffer_, rx_buffer_ + (rx_len_ - keep), keep);
  rx_len_ = keep;
  return false;
}

} // namespace node0
