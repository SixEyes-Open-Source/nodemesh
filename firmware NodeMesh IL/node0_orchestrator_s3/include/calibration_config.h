#pragma once

#include <stddef.h>
#include <stdint.h>

namespace node0 {

namespace calib {

// Core runtime scheduling and transport.
constexpr uint32_t kControlLoopHz = 400;
constexpr uint32_t kNode1Baud = 921600;
constexpr size_t kPacketQueueDepth = 64;

// IK geometry and workspace normalization.
// Segment lengths: tech reference specifies 500 mm total reach → 250 mm per link.
// TODO: verify kUpperArmMm, kForearmMm with physical calipers before bench testing.
constexpr float kUpperArmMm = 250.0f;
constexpr float kForearmMm  = 250.0f;
// Teleop workspace bounds (mm in shoulder-pivot frame, elbow-up solution).
// Scaled proportionally from the 140 mm placeholder geometry.
// TODO: tune these limits to match actual collision envelope on the physical arm.
constexpr float kReachXMinMm =  70.0f;   // near-body minimum extension
constexpr float kReachXMaxMm = 430.0f;   // near-full extension (L1+L2 = 500 mm)
constexpr float kReachYMinMm = -180.0f;  // negative = behind shoulder plane
constexpr float kReachYMaxMm =  320.0f;  // upward / forward reach

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

// Vision-conditioned wrist correction gains.
constexpr float kWristPitchVisionGainDeg = 16.0f;
constexpr float kWristYawVisionGainDeg = 12.0f;

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
