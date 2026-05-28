#pragma once

#include <stdint.h>

namespace node2 {

constexpr uint8_t kEspNowChannel = 6;
constexpr uint8_t kNode0Mac[6] = {0x24, 0x6F, 0x28, 0x00, 0x00, 0x01};

// AI Thinker ESP32-CAM pin map.
constexpr int kCamPinPwdn = 32;
constexpr int kCamPinReset = -1;
constexpr int kCamPinXclk = 0;
constexpr int kCamPinSiod = 26;
constexpr int kCamPinSioc = 27;
constexpr int kCamPinY9 = 35;
constexpr int kCamPinY8 = 34;
constexpr int kCamPinY7 = 39;
constexpr int kCamPinY6 = 36;
constexpr int kCamPinY5 = 21;
constexpr int kCamPinY4 = 19;
constexpr int kCamPinY3 = 18;
constexpr int kCamPinY2 = 5;
constexpr int kCamPinVsync = 25;
constexpr int kCamPinHref = 23;
constexpr int kCamPinPclk = 22;

} // namespace node2
