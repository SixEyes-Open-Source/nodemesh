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
  // Node1 pots are mounted directly on the leader arm joints.
  // Each packet.joints[i] is a normalized ADC reading in [0, 1].
  // Map linearly to the per-joint degree range defined in calibration_config.h.
  // No IK or Cartesian conversion — this is a direct joint-space passthrough.

  static const float kMin[6] = {
      calib::kJointMin0, calib::kJointMin1, calib::kJointMin2,
      calib::kJointMin3, calib::kJointMin4, calib::kJointMin5,
  };
  static const float kMax[6] = {
      calib::kJointMax0, calib::kJointMax1, calib::kJointMax2,
      calib::kJointMax3, calib::kJointMax4, calib::kJointMax5,
  };

  for (size_t i = 0; i < 6; ++i) {
    const float t = clampf(packet.joints[i], 0.0f, 1.0f);
    joint_targets_deg[i] = lerp(kMin[i], kMax[i], t);
  }

  return true;
}

} // namespace node0
