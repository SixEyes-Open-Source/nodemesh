#include "storage/sd_logger.h"

#include "board_pins.h"
#include <Arduino.h>
#include <SPI.h>

namespace node0 {

SdLogger &SdLogger::instance() {
  static SdLogger inst;
  return inst;
}

bool SdLogger::begin() {
  SPI.begin(kSdSck, kSdMiso, kSdMosi, kSdCs);
  pinMode(kSdCs, OUTPUT);
  digitalWrite(kSdCs, HIGH);
  // TODO: initialize SD card filesystem and log file handle.
  return true;
}

bool SdLogger::enqueue(const nodemesh::ExperiencePacket &packet) {
  return queue_.push(packet);
}

void SdLogger::flushOnce() {
  nodemesh::ExperiencePacket packet{};
  if (!queue_.pop(packet)) {
    return;
  }

  // TODO: append packet bytes to SD in pre-allocated block mode.
}

} // namespace node0
