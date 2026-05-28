#pragma once

#include "calibration_config.h"
#include <ESP32Servo.h>
#include <array>

namespace node0 {

// Owns all actuators: 4 stepper axes + 3 servos.
// Applies per-joint angle limits, a first-order low-pass smoothing filter,
// and converts degree targets into stepper step pulses each control tick.
class MotionController {
public:
  static MotionController &instance();

  void begin();

  // Called once per control tick with IK-computed target angles in degrees.
  // joints[0] = base yaw (stepper)
  // joints[1] = shoulder (stepper, dual-driver via ShoulderMirrorStepper)
  // joints[2] = elbow (stepper)
  // joints[3] = wrist pitch (servo)
  // joints[4] = wrist yaw (servo)
  // joints[5] = gripper (servo)
  void setTargets(const std::array<float, 6> &targets_deg);

  // Drives one step per stepper axis toward current smoothed target.
  // Call once per control tick from main loop.
  void tick();

private:
  MotionController() = default;

  // Per-joint limits in degrees.
  static constexpr float kLimitMin[6] = {
      calib::kJointMin0, calib::kJointMin1, calib::kJointMin2,
      calib::kJointMin3, calib::kJointMin4, calib::kJointMin5};
  static constexpr float kLimitMax[6] = {
      calib::kJointMax0, calib::kJointMax1, calib::kJointMax2,
      calib::kJointMax3, calib::kJointMax4, calib::kJointMax5};

  // Degrees per stepper step (microstepping-dependent).
  // 1.8 deg/full-step, 16 microsteps = 0.1125 deg/microstep.
  static constexpr float kDegPerStep = calib::kDegPerMicrostep;

  float smoothed_[6] = {};   // Current smoothed target (deg)
  float stepper_pos_[3] = {}; // Tracked position for steppers 0-2 (deg)

  Servo servo_wp_;  // wrist pitch
  Servo servo_wy_;  // wrist yaw
  Servo servo_gr_;  // gripper

  bool begun_ = false;

  static float clampf(float v, float lo, float hi);
  static float lpf(float current, float target, float alpha);
};

} // namespace node0
