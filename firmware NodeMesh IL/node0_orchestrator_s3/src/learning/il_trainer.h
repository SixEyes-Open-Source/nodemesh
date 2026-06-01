#pragma once

#include "calibration_config.h"
#include "nodemesh/experience_packet.h"
#include <array>
#include <stddef.h>

namespace node0 {

class IlTrainer {
public:
  static IlTrainer &instance();
  void begin();
  void observe(const nodemesh::ExperiencePacket &packet);
  void trainStep();

  // Forward pass: given current sensor packet, write 6 joint targets (degrees).
  // Returns false and zeroes targets if no model is trained yet.
  bool infer(const nodemesh::ExperiencePacket &packet,
             std::array<float, 6> &targets_deg) const;

private:
  IlTrainer() = default;

  static constexpr size_t kDatasetCapacity = calib::kIlDatasetCapacity;
  static constexpr size_t kJointCount = nodemesh::kJointCount;

  void updateOnlineStats(const nodemesh::ExperiencePacket &sample);

  nodemesh::ExperiencePacket *dataset_ = nullptr;
  size_t dataset_count_ = 0;
  size_t write_index_ = 0;
  bool using_psram_ = false;
  size_t train_cursor_ = 0;

  bool stats_initialized_ = false;
  uint32_t stats_samples_ = 0;
  float joint_mean_[kJointCount] = {};
  float joint_m2_[kJointCount] = {};
  float vision_mean_intensity_ = 0.0f;
  float vision_m2_intensity_ = 0.0f;

  // MLP weights — PSRAM-allocated in begin(), layout: row-major
  float *W0_ = nullptr;  // [32 * 134]
  float *b0_ = nullptr;  // [32]
  float *W1_ = nullptr;  // [16 * 32]
  float *b1_ = nullptr;  // [16]
  float *W2_ = nullptr;  // [6 * 16]
  float *b2_ = nullptr;  // [6]
  bool is_trained_ = false;

  // Running MSE accumulator — reset each stats print interval.
  float    mse_sum_   = 0.0f;
  uint32_t mse_count_ = 0;

  void initWeightsKaiming();
  bool loadWeights();
  void saveWeights() const;
};

} // namespace node0
// formerly VlaStub — renamed as part of NodeMesh IL rebranding
