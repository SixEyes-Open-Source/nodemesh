#include "motion/motion_controller.h"

#include "board_pins.h"
#include "motion/stepper_sync.h"
#include <Arduino.h>
#include <math.h>

namespace node0 {

MotionController &MotionController::instance() {
  static MotionController inst;
  return inst;
}

// Static constexpr member definitions required in C++14/17.
constexpr float MotionController::kLimitMin[6];
constexpr float MotionController::kLimitMax[6];

float MotionController::clampf(float v, float lo, float hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}

float MotionController::lpf(float current, float target, float alpha) {
  return current + alpha * (target - current);
}

void MotionController::begin() {
  // Steppers: base (J0), shoulder (J1, dual-driver), elbow (J2).
  // shoulder is handled by ShoulderMirrorStepper; base/elbow use direct GPIO.
  pinMode(kBase.step,  OUTPUT); digitalWrite(kBase.step,  LOW);
  pinMode(kBase.dir,   OUTPUT); digitalWrite(kBase.dir,   LOW);
  pinMode(kBase.en,    OUTPUT); digitalWrite(kBase.en,    HIGH); // disabled

  pinMode(kElbow.step, OUTPUT); digitalWrite(kElbow.step, LOW);
  pinMode(kElbow.dir,  OUTPUT); digitalWrite(kElbow.dir,  LOW);
  pinMode(kElbow.en,   OUTPUT); digitalWrite(kElbow.en,   HIGH); // disabled

  // Shoulder mirror stepper is owned and begun externally (main.cpp).

  // Servos.
  servo_wp_.attach(kServoWristPitch);
  servo_wy_.attach(kServoWristYaw);
  servo_gr_.attach(kServoGripper);

  // Park servos at mid-range.
  servo_wp_.write(90);
  servo_wy_.write(90);
  servo_gr_.write(90);

  // Initialise smoothed targets to mechanical zero.
  for (size_t i = 0; i < 6; ++i) {
    smoothed_[i] = clampf(0.0f, kLimitMin[i], kLimitMax[i]);
  }
  stepper_pos_[0] = smoothed_[0]; // base
  stepper_pos_[1] = smoothed_[1]; // shoulder
  stepper_pos_[2] = smoothed_[2]; // elbow

  // Enable stepper drivers.
  digitalWrite(kBase.en,  LOW);
  digitalWrite(kElbow.en, LOW);
  ShoulderMirrorStepper::instance().setEnable(true);

  begun_ = true;
  Serial.println("[Node0][MOT] MotionController ready");
}

void MotionController::setTargets(const std::array<float, 6> &targets_deg) {
  for (size_t i = 0; i < 6; ++i) {
    const float clamped = clampf(targets_deg[i], kLimitMin[i], kLimitMax[i]);
    smoothed_[i] = lpf(smoothed_[i], clamped, calib::kMotionLpfAlpha);
  }
}

void MotionController::tick() {
  if (!begun_) return;

  // --- Stepper axes: step once toward smoothed target if error >= 1 step ---
  // Joint 0: base
  {
    const float err = smoothed_[0] - stepper_pos_[0];
    if (fabsf(err) >= kDegPerStep) {
      const bool dir = err > 0.0f;
      digitalWrite(kBase.dir, dir ? HIGH : LOW);
      // Atomic pulse via register write for base (single pin, so digitalWrite ok here).
      digitalWrite(kBase.step, HIGH);
      delayMicroseconds(2);
      digitalWrite(kBase.step, LOW);
      stepper_pos_[0] += dir ? kDegPerStep : -kDegPerStep;
    }
  }

  // Joint 1: shoulder (dual-driver, uses ShoulderMirrorStepper)
  {
    const float err = smoothed_[1] - stepper_pos_[1];
    if (fabsf(err) >= kDegPerStep) {
      const bool dir = err > 0.0f;
      ShoulderMirrorStepper::instance().pulse(dir);
      stepper_pos_[1] += dir ? kDegPerStep : -kDegPerStep;
    }
  }

  // Joint 2: elbow
  {
    const float err = smoothed_[2] - stepper_pos_[2];
    if (fabsf(err) >= kDegPerStep) {
      const bool dir = err > 0.0f;
      digitalWrite(kElbow.dir, dir ? HIGH : LOW);
      digitalWrite(kElbow.step, HIGH);
      delayMicroseconds(2);
      digitalWrite(kElbow.step, LOW);
      stepper_pos_[2] += dir ? kDegPerStep : -kDegPerStep;
    }
  }

  // --- Servo joints: write directly (ESP32Servo handles PWM, no blocking) ---
  servo_wp_.write(static_cast<int>(smoothed_[3]));
  servo_wy_.write(static_cast<int>(smoothed_[4]));
  servo_gr_.write(static_cast<int>(smoothed_[5]));
}

// Make ShoulderMirrorStepper accessible as singleton from here.
// The instance is also declared in main.cpp; we add a getter here for tick().
ShoulderMirrorStepper &ShoulderMirrorStepper::instance() {
  static ShoulderMirrorStepper inst;
  return inst;
}

} // namespace node0
