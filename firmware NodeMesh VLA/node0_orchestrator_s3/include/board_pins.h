#pragma once

#include <stdint.h>

namespace node0 {

struct StepperPins {
  uint8_t step;
  uint8_t dir;
  uint8_t en;
  uint8_t pdn_uart;
};

// Aligned to existing follower pin map.
constexpr StepperPins kBase{4, 5, 6, 7};
constexpr StepperPins kShoulderA{8, 9, 10, 11};
constexpr StepperPins kShoulderB{12, 13, 14, 15};
constexpr StepperPins kElbow{16, 17, 18, 21};

constexpr uint8_t kServoWristPitch = 35;
constexpr uint8_t kServoWristYaw = 36;
constexpr uint8_t kServoGripper = 37;

constexpr uint8_t kUartRxFromNode1 = 38;
constexpr uint8_t kUartTxToNode1 = 39;

// External MicroSD over SPI (adjust CS for your PCB routing).
constexpr uint8_t kSdSck = 40;
constexpr uint8_t kSdMiso = 41;
constexpr uint8_t kSdMosi = 42;
constexpr uint8_t kSdCs = 2;

} // namespace node0
