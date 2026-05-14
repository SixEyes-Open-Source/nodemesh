#include "comms/uart_input.h"

#include "app_config.h"
#include "board_pins.h"
#include "nodemesh/packet_codec.h"
#include <Arduino.h>

namespace node0 {

UartInput &UartInput::instance() {
  static UartInput inst;
  return inst;
}

void UartInput::begin() {
  Serial1.begin(kNode1Baud, SERIAL_8N1, kUartRxFromNode1, kUartTxToNode1);
}

bool UartInput::tryRead(nodemesh::ExperiencePacket &packet) {
  if (Serial1.available() < static_cast<int>(sizeof(packet))) {
    return false;
  }

  const auto read = Serial1.readBytes(reinterpret_cast<char *>(&packet), sizeof(packet));
  if (read != sizeof(packet)) {
    return false;
  }

  return nodemesh::validate_packet(packet);
}

} // namespace node0
