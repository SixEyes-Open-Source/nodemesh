#pragma once

#include <stddef.h>
#include <stdint.h>

namespace node0 {

namespace calib {

// Core runtime scheduling and transport.
constexpr uint32_t kControlLoopHz = 400;
constexpr uint32_t kNode1Baud = 921600;
constexpr size_t kPacketQueueDepth = 64;

// IK geometry constants (kept for reference; not used in direct-passthrough teleop).
constexpr float kUpperArmMm = 250.0f;
constexpr float kForearmMm  = 250.0f;
constexpr float kReachXMinMm =  70.0f;
constexpr float kReachXMaxMm = 430.0f;
constexpr float kReachYMinMm = -180.0f;
constexpr float kReachYMaxMm =  320.0f;

// Servo clamp range for generated degree commands.
constexpr float kServoMinDeg = 0.0f;
constexpr float kServoMaxDeg = 180.0f;

// Per-joint angle limits (degrees).
// Stepper joints: 0=base, 1=shoulder, 2=elbow. Servo joints: 3=wp, 4=wy, 5=gr.
constexpr float kJointMin0 = -90.0f;  constexpr float kJointMax0 = 90.0f;
constexpr float kJointMin1 =   0.0f;  constexpr float kJointMax1 = 150.0f;
constexpr float kJointMin2 =  10.0f;  constexpr float kJointMax2 = 160.0f;
constexpr float kJointMin3 =   0.0f;  constexpr float kJointMax3 = 180.0f;
constexpr float kJointMin4 =   0.0f;  constexpr float kJointMax4 = 180.0f;
constexpr float kJointMin5 =   0.0f;  constexpr float kJointMax5 = 180.0f;

// Low-pass filter alpha for trajectory smoothing (0 = frozen, 1 = no filter).
// At 400 Hz, alpha=0.08 gives ~30 ms settling time.
constexpr float kMotionLpfAlpha = 0.08f;

// Stepper resolution: 1.8 deg/full-step, 16 microsteps.
constexpr float kDegPerMicrostep = 1.8f / 16.0f;  // 0.1125 deg/step

// Camera input freshness window.
constexpr uint32_t kVisionStaleMs = 300;

// IL dataset/runtime tuning.
constexpr size_t kIlDatasetCapacity = 512;
constexpr uint32_t kIlStatsLogEverySteps = 2000;

// IL backprop hyperparameters.
constexpr float    kIlLearningRate     = 0.001f;
constexpr float    kGradNormClip       = 1.0f;
constexpr uint32_t kMinSamplesForTrain = 50;  // don't train until N demos collected

} // namespace calib

} // namespace node0
