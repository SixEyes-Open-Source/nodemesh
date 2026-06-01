#include "learning/il_trainer.h"

#include <Arduino.h>
#include <SD.h>
#include <esp_heap_caps.h>
#include <esp_random.h>
#include <math.h>
#include <stdlib.h>

namespace node0 {

IlTrainer &IlTrainer::instance() {
  static IlTrainer inst;
  return inst;
}

void IlTrainer::begin() {
  const size_t bytes =
      kDatasetCapacity * sizeof(nodemesh::ExperiencePacket);

  dataset_ = static_cast<nodemesh::ExperiencePacket *>(ps_malloc(bytes));
  if (dataset_ != nullptr) {
    using_psram_ = true;
    Serial.printf("[Node0][IL] Dataset in PSRAM (%u bytes)\n",
                  static_cast<unsigned>(bytes));
  } else {
    dataset_ = static_cast<nodemesh::ExperiencePacket *>(malloc(bytes));
    if (dataset_ != nullptr) {
      using_psram_ = false;
      Serial.printf("[Node0][IL] Dataset in internal RAM fallback (%u bytes)\n",
                    static_cast<unsigned>(bytes));
    } else {
      Serial.println("[Node0][IL] Dataset allocation failed");
    }
  }

  if (dataset_ == nullptr) return;

  // ----- MLP weight allocation (PSRAM preferred) -----
  auto alloc_w = [](size_t n) -> float * {
    float *p = static_cast<float *>(ps_malloc(n * sizeof(float)));
    if (p == nullptr) p = static_cast<float *>(malloc(n * sizeof(float)));
    return p;
  };

  W0_ = alloc_w(134 * 32);
  b0_ = alloc_w(32);
  W1_ = alloc_w(32 * 16);
  b1_ = alloc_w(16);
  W2_ = alloc_w(16 * 6);
  b2_ = alloc_w(6);

  if (W0_ == nullptr || b0_ == nullptr || W1_ == nullptr ||
      b1_ == nullptr || W2_ == nullptr || b2_ == nullptr) {
    Serial.println("[Node0][IL] Weight allocation failed");
    return;
  }

  if (!loadWeights()) {
    initWeightsKaiming();
  }
}

void IlTrainer::observe(const nodemesh::ExperiencePacket &packet) {
  if (dataset_ == nullptr) {
    return;
  }

  dataset_[write_index_] = packet;
  write_index_ = (write_index_ + 1) % kDatasetCapacity;
  if (dataset_count_ < kDatasetCapacity) {
    ++dataset_count_;
  }
}

void IlTrainer::updateOnlineStats(const nodemesh::ExperiencePacket &sample) {
  ++stats_samples_;
  const float n = static_cast<float>(stats_samples_);

  for (size_t i = 0; i < kJointCount; ++i) {
    const float x = sample.joints[i];
    const float delta = x - joint_mean_[i];
    joint_mean_[i] += delta / n;
    const float delta2 = x - joint_mean_[i];
    joint_m2_[i] += delta * delta2;
  }

  float sum = 0.0f;
  for (size_t i = 0; i < nodemesh::kVisionFeatureBytes; ++i) {
    sum += static_cast<float>(sample.vision_features[i]);
  }
  const float intensity = sum / static_cast<float>(nodemesh::kVisionFeatureBytes);
  const float d = intensity - vision_mean_intensity_;
  vision_mean_intensity_ += d / n;
  const float d2 = intensity - vision_mean_intensity_;
  vision_m2_intensity_ += d * d2;
}

void IlTrainer::trainStep() {
  if (dataset_ == nullptr || dataset_count_ == 0) {
    return;
  }

  const nodemesh::ExperiencePacket &sample = dataset_[train_cursor_];
  updateOnlineStats(sample);
  stats_initialized_ = true;

  train_cursor_ = (train_cursor_ + 1) % dataset_count_;

  static uint32_t step_counter = 0;
  ++step_counter;
  if ((step_counter % calib::kIlStatsLogEverySteps) == 0U && stats_initialized_) {
    const float denom = stats_samples_ > 1 ? static_cast<float>(stats_samples_ - 1) : 1.0f;
    const float joint0_std = sqrtf(joint_m2_[0] / denom);
    const float vision_std = sqrtf(vision_m2_intensity_ / denom);
    const float avg_mse = (mse_count_ > 0) ? (mse_sum_ / static_cast<float>(mse_count_)) : -1.0f;
    mse_sum_   = 0.0f;
    mse_count_ = 0;
    if (avg_mse >= 0.0f) {
      Serial.printf(
          "[Node0][IL] step=%u n=%u mse=%.6f j0_mean=%.3f j0_std=%.3f vis_mean=%.2f vis_std=%.2f\n",
          static_cast<unsigned>(step_counter),
          static_cast<unsigned>(dataset_count_), avg_mse,
          joint_mean_[0], joint0_std, vision_mean_intensity_, vision_std);
    } else {
      Serial.printf(
          "[Node0][IL] step=%u n=%u mse=n/a (< %u samples) j0_mean=%.3f vis_mean=%.2f\n",
          static_cast<unsigned>(step_counter),
          static_cast<unsigned>(dataset_count_),
          static_cast<unsigned>(calib::kMinSamplesForTrain),
          joint_mean_[0], vision_mean_intensity_);
    }
    if (W0_ != nullptr && is_trained_) {
      saveWeights();
    }
  }

  // ----- SGD update (one sample per 400 Hz tick) -----
  if (dataset_count_ < calib::kMinSamplesForTrain || W0_ == nullptr) return;

  // Forward pass
  float input[134];
  for (size_t i = 0; i < 6; ++i)   input[i]     = sample.joints[i];
  for (size_t i = 0; i < 128; ++i) input[6 + i] = static_cast<float>(sample.vision_features[i]) / 255.0f;

  float h0[32];
  for (size_t k = 0; k < 32; ++k) {
    float acc = b0_[k];
    const float *row = W0_ + k * 134;
    for (size_t m = 0; m < 134; ++m) acc += row[m] * input[m];
    h0[k] = acc > 0.0f ? acc : 0.0f;
  }

  float h1[16];
  for (size_t j = 0; j < 16; ++j) {
    float acc = b1_[j];
    const float *row = W1_ + j * 32;
    for (size_t k = 0; k < 32; ++k) acc += row[k] * h0[k];
    h1[j] = acc > 0.0f ? acc : 0.0f;
  }

  float pred[6];
  for (size_t i = 0; i < 6; ++i) {
    float acc = b2_[i];
    const float *row = W2_ + i * 16;
    for (size_t j = 0; j < 16; ++j) acc += row[j] * h1[j];
    pred[i] = acc;
  }

  // Accumulate MSE for periodic logging.
  float mse = 0.0f;
  for (size_t i = 0; i < 6; ++i) {
    const float e = pred[i] - sample.joints[i];
    mse += e * e;
  }
  mse_sum_   += mse / 6.0f;
  mse_count_ += 1;

  // Backward pass
  float d_out2[6];
  for (size_t i = 0; i < 6; ++i)
    d_out2[i] = (2.0f / 6.0f) * (pred[i] - sample.joints[i]);

  float d_h1[16];
  for (size_t j = 0; j < 16; ++j) {
    float acc = 0.0f;
    for (size_t i = 0; i < 6; ++i) acc += W2_[i * 16 + j] * d_out2[i];
    d_h1[j] = acc * (h1[j] > 0.0f ? 1.0f : 0.0f);
  }

  float d_h0[32];
  for (size_t k = 0; k < 32; ++k) {
    float acc = 0.0f;
    for (size_t j = 0; j < 16; ++j) acc += W1_[j * 32 + k] * d_h1[j];
    d_h0[k] = acc * (h0[k] > 0.0f ? 1.0f : 0.0f);
  }

  // Gradient norm clipping (factored form to avoid O(n^2) nested loops)
  float d_out2_sq = 0.0f; for (size_t i = 0; i < 6;   ++i) d_out2_sq += d_out2[i] * d_out2[i];
  float h1_sq     = 0.0f; for (size_t j = 0; j < 16;  ++j) h1_sq     += h1[j]     * h1[j];
  float d_h1_sq   = 0.0f; for (size_t j = 0; j < 16;  ++j) d_h1_sq   += d_h1[j]   * d_h1[j];
  float h0_sq     = 0.0f; for (size_t k = 0; k < 32;  ++k) h0_sq     += h0[k]     * h0[k];
  float d_h0_sq   = 0.0f; for (size_t k = 0; k < 32;  ++k) d_h0_sq   += d_h0[k]   * d_h0[k];
  float input_sq  = 0.0f; for (size_t m = 0; m < 134; ++m) input_sq  += input[m]  * input[m];

  // ||g||^2 = ||dW2||^2 + ||db2||^2 + ||dW1||^2 + ||db1||^2 + ||dW0||^2 + ||db0||^2
  const float grad_norm_sq = d_out2_sq * h1_sq    + d_out2_sq
                           + d_h1_sq   * h0_sq    + d_h1_sq
                           + d_h0_sq   * input_sq + d_h0_sq;

  float lr = calib::kIlLearningRate;
  if (grad_norm_sq > calib::kGradNormClip * calib::kGradNormClip) {
    lr *= calib::kGradNormClip / sqrtf(grad_norm_sq);
  }

  // Weight updates
  for (size_t i = 0; i < 6; ++i) {
    for (size_t j = 0; j < 16; ++j) W2_[i * 16 + j] -= lr * d_out2[i] * h1[j];
    b2_[i] -= lr * d_out2[i];
  }
  for (size_t j = 0; j < 16; ++j) {
    for (size_t k = 0; k < 32; ++k) W1_[j * 32 + k] -= lr * d_h1[j] * h0[k];
    b1_[j] -= lr * d_h1[j];
  }
  for (size_t k = 0; k < 32; ++k) {
    for (size_t m = 0; m < 134; ++m) W0_[k * 134 + m] -= lr * d_h0[k] * input[m];
    b0_[k] -= lr * d_h0[k];
  }

  is_trained_ = true;
}

bool IlTrainer::infer(const nodemesh::ExperiencePacket &packet,
                      std::array<float, 6> &targets_deg) const {
  if (W0_ == nullptr) {
    targets_deg.fill(0.0f);
    return false;
  }

  float input[134];
  for (size_t i = 0; i < 6;   ++i) input[i]     = packet.joints[i];
  for (size_t i = 0; i < 128; ++i) input[6 + i] = static_cast<float>(packet.vision_features[i]) / 255.0f;

  float h0[32];
  for (size_t k = 0; k < 32; ++k) {
    float acc = b0_[k];
    const float *row = W0_ + k * 134;
    for (size_t m = 0; m < 134; ++m) acc += row[m] * input[m];
    h0[k] = acc > 0.0f ? acc : 0.0f;
  }

  float h1[16];
  for (size_t j = 0; j < 16; ++j) {
    float acc = b1_[j];
    const float *row = W1_ + j * 32;
    for (size_t k = 0; k < 32; ++k) acc += row[k] * h0[k];
    h1[j] = acc > 0.0f ? acc : 0.0f;
  }

  for (size_t i = 0; i < 6; ++i) {
    float acc = b2_[i];
    const float *row = W2_ + i * 16;
    for (size_t j = 0; j < 16; ++j) acc += row[j] * h1[j];
    if (acc < 0.0f)   acc = 0.0f;
    if (acc > 180.0f) acc = 180.0f;
    targets_deg[i] = acc;
  }

  return is_trained_;
}

// ---- Weight helpers ----

void IlTrainer::initWeightsKaiming() {
  auto kaiming_val = [](float fan_in) -> float {
    const float limit = sqrtf(6.0f / fan_in);
    const float r = static_cast<float>(esp_random()) / 4294967296.0f;
    return limit * (2.0f * r - 1.0f);
  };
  for (size_t i = 0; i < 134 * 32; ++i) W0_[i] = kaiming_val(134.0f);
  for (size_t i = 0; i < 32;       ++i) b0_[i] = 0.0f;
  for (size_t i = 0; i < 32 * 16;  ++i) W1_[i] = kaiming_val(32.0f);
  for (size_t i = 0; i < 16;       ++i) b1_[i] = 0.0f;
  for (size_t i = 0; i < 16 * 6;   ++i) W2_[i] = kaiming_val(16.0f);
  for (size_t i = 0; i < 6;        ++i) b2_[i] = 0.0f;
  Serial.println("[Node0][IL] Weights initialised (Kaiming He)");
}

bool IlTrainer::loadWeights() {
  static constexpr size_t kWeightFloats = 134*32 + 32 + 32*16 + 16 + 16*6 + 6;
  static constexpr size_t kWeightBytes  = kWeightFloats * sizeof(float);

  File f = SD.open("/policy_weights.bin", FILE_READ);
  if (!f) return false;

  if (static_cast<size_t>(f.size()) != kWeightBytes) {
    f.close();
    return false;
  }

  auto read_floats = [&](float *ptr, size_t n) -> bool {
    return static_cast<size_t>(f.read(reinterpret_cast<uint8_t *>(ptr),
                                      n * sizeof(float))) == n * sizeof(float);
  };

  const bool ok = read_floats(W0_, 134*32) && read_floats(b0_, 32) &&
                  read_floats(W1_, 32*16)  && read_floats(b1_, 16) &&
                  read_floats(W2_, 16*6)   && read_floats(b2_, 6);
  f.close();

  if (ok) {
    is_trained_ = true;
    Serial.println("[Node0][IL] Loaded weights from SD");
  }
  return ok;
}

void IlTrainer::saveWeights() const {
  static constexpr size_t kWeightFloats = 134*32 + 32 + 32*16 + 16 + 16*6 + 6;

  SD.remove("/policy_weights.tmp");
  File f = SD.open("/policy_weights.tmp", FILE_WRITE);
  if (!f) {
    Serial.println("[Node0][IL] Failed to open weights tmp file");
    return;
  }

  auto write_floats = [&](const float *ptr, size_t n) {
    f.write(reinterpret_cast<const uint8_t *>(ptr), n * sizeof(float));
  };

  write_floats(W0_, 134*32);
  write_floats(b0_, 32);
  write_floats(W1_, 32*16);
  write_floats(b1_, 16);
  write_floats(W2_, 16*6);
  write_floats(b2_, 6);
  f.close();

  SD.remove("/policy_weights.bin");
  SD.rename("/policy_weights.tmp", "/policy_weights.bin");
  Serial.println("[IL] Saved weights");
  (void)kWeightFloats;
}

size_t IlTrainer::loadFromLog() {
  if (dataset_ == nullptr) return 0;

  File f = SD.open("/node0_log.bin", FILE_READ);
  if (!f) {
    Serial.println("[Node0][IL] loadFromLog: file not found, skipping preload");
    return 0;
  }

  const size_t file_size = f.size();
  if (file_size < 32) { f.close(); return 0; }

  // Magic constants mirrored from sd_logger.h (no circular include needed).
  constexpr uint32_t kSessionMagic = 0x4E4D4C47UL;
  constexpr uint32_t kPacketMagic  = 0x4E4D5650UL;
  constexpr uint32_t kEpisodeMagic = 0x4E4D4550UL;
  constexpr size_t   kPacketSize   = sizeof(nodemesh::ExperiencePacket);
  constexpr size_t   kMarkerSize   = 32;

  // ── Pass 1: locate all closed episode byte ranges ──────────────────────
  // EpisodeMarker layout (packed):
  //   magic(4) episode_id(4) event(1) reserved0(1) reserved1(2)
  //   timestamp_ms(4) reserved2(16)  -> 32 bytes
  // event byte is at offset 8 within the marker.

  struct EpRange { uint32_t start_off; uint32_t stop_off; };
  // Static so it isn't stack-allocated. 256 episodes * 8 bytes = 2 KB.
  static EpRange ep_ranges[256];
  uint16_t ep_count = 0;

  bool    ep_open      = false;
  uint32_t ep_start_off = 0;
  size_t   scan_offset  = kMarkerSize; // skip session header

  while (scan_offset + 4 <= file_size) {
    if (!f.seek(scan_offset)) break;
    uint32_t magic = 0;
    if (f.read(reinterpret_cast<uint8_t *>(&magic), 4) != 4) break;

    if (magic == kPacketMagic) {
      scan_offset += kPacketSize;
    } else if (magic == kEpisodeMagic) {
      if (scan_offset + kMarkerSize > file_size) break;
      // Read event byte: 4 (magic) + 4 (episode_id) = offset 8
      if (!f.seek(scan_offset + 8)) break;
      uint8_t event = 0xff;
      if (f.read(&event, 1) != 1) break;
      if (event == 0) {                      // episode start
        ep_open       = true;
        ep_start_off  = static_cast<uint32_t>(scan_offset + kMarkerSize);
      } else if (event == 1 && ep_open) {    // episode stop — episode is now closed
        if (ep_count < 256) {
          ep_ranges[ep_count++] = {
              ep_start_off,
              static_cast<uint32_t>(scan_offset)
          };
        }
        ep_open = false;
      }
      scan_offset += kMarkerSize;
    } else if (magic == kSessionMagic) {
      scan_offset += kMarkerSize; // session header is also 32 bytes
    } else {
      break; // zeros (pre-alloc padding) or corruption — end of written data
    }
  }

  Serial.printf("[Node0][IL] loadFromLog: %u closed episode(s) found\n",
                static_cast<unsigned>(ep_count));

  if (ep_count == 0) { f.close(); return 0; }

  // ── Count total packets across all closed episodes ─────────────────────
  // Approximation: treat entire [start, stop) span as packets.
  // This over-counts slightly if there are markers inside, but that doesn't
  // happen in normal operation.
  size_t total_available = 0;
  for (uint16_t e = 0; e < ep_count; ++e) {
    const size_t span = ep_ranges[e].stop_off - ep_ranges[e].start_off;
    total_available += span / kPacketSize;
  }

  // Uniform stride so all episodes contribute proportionally when dataset is
  // larger than kDatasetCapacity.
  const size_t stride = (total_available > kDatasetCapacity)
                        ? (total_available / kDatasetCapacity)
                        : 1;

  // ── Pass 2: load packets at uniform stride ─────────────────────────────
  size_t loaded      = 0;
  size_t packet_idx  = 0; // global counter across all episodes for stride

  for (uint16_t e = 0; e < ep_count && loaded < kDatasetCapacity; ++e) {
    uint32_t pos            = ep_ranges[e].start_off;
    const uint32_t ep_stop  = ep_ranges[e].stop_off;

    while (pos + kPacketSize <= ep_stop && pos + kPacketSize <= file_size
           && loaded < kDatasetCapacity) {
      if (!f.seek(pos)) break;
      uint32_t magic = 0;
      if (f.read(reinterpret_cast<uint8_t *>(&magic), 4) != 4) break;

      if (magic == kPacketMagic) {
        if ((packet_idx % stride) == 0) {
          nodemesh::ExperiencePacket pkt{};
          f.seek(pos);
          if (f.read(reinterpret_cast<uint8_t *>(&pkt), kPacketSize)
              == static_cast<int>(kPacketSize)) {
            dataset_[write_index_] = pkt;
            write_index_ = (write_index_ + 1) % kDatasetCapacity;
            if (dataset_count_ < kDatasetCapacity) ++dataset_count_;
            ++loaded;
          }
        }
        ++packet_idx;
        pos += kPacketSize;
      } else if (magic == kEpisodeMagic) {
        pos += kMarkerSize; // skip any stray nested marker
      } else {
        break;
      }
    }
  }

  f.close();
  Serial.printf(
      "[Node0][IL] loadFromLog: loaded %u/%u packets  stride=%u  dataset=%u/%u\n",
      static_cast<unsigned>(loaded),
      static_cast<unsigned>(total_available),
      static_cast<unsigned>(stride),
      static_cast<unsigned>(dataset_count_),
      static_cast<unsigned>(kDatasetCapacity));
  return loaded;
}

} // namespace node0
