#pragma once

#include "calibration_config.h"

namespace node0 {

// Backward-compatible aliases. Prefer node0::calib::* in new code.
constexpr uint32_t kControlLoopHz = calib::kControlLoopHz;
constexpr uint32_t kNode1Baud = calib::kNode1Baud;
constexpr size_t kPacketQueueDepth = calib::kPacketQueueDepth;

} // namespace node0
