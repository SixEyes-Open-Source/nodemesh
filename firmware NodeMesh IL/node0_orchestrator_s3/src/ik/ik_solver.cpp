#include "ik/ik_solver.h"

#include <Arduino.h>
#include <math.h>

namespace node0 {

namespace {
constexpr float kRadToDeg = 57.295779513f;

float clampf(float v, float lo, float hi) {
  if (v < lo) {
    return lo;
  }
  if (v > hi) {
    return hi;
  }
  return v;
}

float lerp(float a, float b, float t) {
  return a + (b - a) * t;
}
} // namespace

IkSolver &IkSolver::instance() {
  static IkSolver inst;
  return inst;
}

void IkSolver::begin() {
  Serial.println("[Node0][IK] Solver initialized");
}

bool IkSolver::solvePlanar2Link(float x_mm, float y_mm, float &shoulder_deg,
                                float &elbow_deg) const {
  const float l1 = calib::kUpperArmMm;
  const float l2 = calib::kForearmMm;

  const float r2 = x_mm * x_mm + y_mm * y_mm;
  const float cos_elbow = clampf((r2 - l1 * l1 - l2 * l2) / (2.0f * l1 * l2), -1.0f, 1.0f);
  const float elbow = acosf(cos_elbow);

  const float k1 = l1 + l2 * cosf(elbow);
  const float k2 = l2 * sinf(elbow);
  const float shoulder = atan2f(y_mm, x_mm) - atan2f(k2, k1);

  shoulder_deg = shoulder * kRadToDeg;
  elbow_deg = elbow * kRadToDeg;
  return true;
}

bool IkSolver::solveFromPacket(const nodemesh::ExperiencePacket &packet,
                               std::array<float, 6> &joint_targets_deg) {
  // Base yaw is passed through from teleop input.
  joint_targets_deg[0] = packet.joints[0] * 180.0f;

  // Map normalized packet joints into a planar workspace for shoulder/elbow IK.
  const float x_mm = lerp(calib::kReachXMinMm, calib::kReachXMaxMm,
                          clampf(packet.joints[1], 0.0f, 1.0f));
  const float y_mm = lerp(calib::kReachYMinMm, calib::kReachYMaxMm,
                          clampf(packet.joints[2], 0.0f, 1.0f));

  float shoulder_deg = 0.0f;
  float elbow_deg = 0.0f;
  if (!solvePlanar2Link(x_mm, y_mm, shoulder_deg, elbow_deg)) {
    return false;
  }

  joint_targets_deg[1] = shoulder_deg;
  joint_targets_deg[2] = elbow_deg;

  // Vision features are now a 8x8 spatial grid (out[row*8+col] = mean brightness).
  // Compute horizontal (x) and vertical (y) brightness centroids in [0,1].
  // These are used as a mild wrist stabilization hint only — not primary control.
  float total   = 0.0f;
  float cx_sum  = 0.0f;
  float cy_sum  = 0.0f;
  constexpr size_t kGridCols = 8;
  constexpr size_t kGridRows = 8;
  for (size_t r = 0; r < kGridRows; ++r) {
    for (size_t c = 0; c < kGridCols; ++c) {
      const float v = static_cast<float>(packet.vision_features[r * kGridCols + c]);
      total   += v;
      cx_sum  += v * (static_cast<float>(c) / (kGridCols - 1));
      cy_sum  += v * (static_cast<float>(r) / (kGridRows - 1));
    }
  }

  float brightness = 0.5f;  // fallback
  float cx = 0.5f;
  float cy = 0.5f;
  if (total > 1.0f) {
    cx = cx_sum / total;
    cy = cy_sum / total;
    brightness = total / (kGridCols * kGridRows * 255.0f);
  }

  const float wrist_pitch_base = packet.joints[3] * 180.0f;
  const float wrist_yaw_base = packet.joints[4] * 180.0f;
  const float gripper_base = packet.joints[5] * 180.0f;

  // Small correction terms from spatial centroid to aid wrist orientation.
  // cx/cy are in [0,1]; 0.5 = centred = no correction.
  const float pitch_correction = (cy - 0.5f) * calib::kWristPitchVisionGainDeg;
  const float yaw_correction   = (cx - 0.5f) * calib::kWristYawVisionGainDeg;

  joint_targets_deg[3] =
      clampf(wrist_pitch_base + pitch_correction, calib::kServoMinDeg, calib::kServoMaxDeg);
  joint_targets_deg[4] =
      clampf(wrist_yaw_base + yaw_correction, calib::kServoMinDeg, calib::kServoMaxDeg);
  joint_targets_deg[5] = clampf(gripper_base, calib::kServoMinDeg, calib::kServoMaxDeg);

  return true;
}

} // namespace node0
