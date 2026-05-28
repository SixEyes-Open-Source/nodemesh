# Backprop Implementation — Subagent Prompt

Use this prompt verbatim when asking an AI agent to implement on-MCU imitation
learning backprop in `il_trainer.h` / `il_trainer.cpp`.

---

## Prompt

You are implementing on-MCU imitation learning (IL) backprop for a 3-layer MLP
running on an ESP32-S3 (240 MHz, 512 KB SRAM, 8 MB PSRAM).  The project is
"NodeMesh IL" — a pick-and-place robotic arm that learns from teleoperation
demonstrations.

### Files to modify

- `nodemesh/firmware NodeMesh IL/node0_orchestrator_s3/src/learning/il_trainer.h`
- `nodemesh/firmware NodeMesh IL/node0_orchestrator_s3/src/learning/il_trainer.cpp`

### Existing class skeleton

```cpp
// il_trainer.h  (partial — keep everything that is already there)
class IlTrainer {
public:
  static IlTrainer &instance();
  void begin();
  void observe(const nodemesh::ExperiencePacket &packet);
  void trainStep();
  bool infer(const nodemesh::ExperiencePacket &packet,
             std::array<float, 6> &targets_deg) const;
private:
  // dataset ring buffer — already exists, do not change
  nodemesh::ExperiencePacket *dataset_ = nullptr;
  size_t dataset_count_ = 0;
  size_t write_index_   = 0;
  bool   using_psram_   = false;
  size_t train_cursor_  = 0;
  // Welford stats — already exist, do not change
  bool     stats_initialized_  = false;
  uint32_t stats_samples_      = 0;
  float    joint_mean_[6]      = {};
  float    joint_m2_[6]        = {};
  float    vision_mean_intensity_ = 0.0f;
  float    vision_m2_intensity_   = 0.0f;
};
```

### Network architecture

| Layer | Type   | In  | Out | Activation |
|-------|--------|-----|-----|------------|
| 0     | Dense  | 134 | 32  | ReLU       |
| 1     | Dense  | 32  | 16  | ReLU       |
| 2     | Dense  | 16  | 6   | linear     |

Input vector (134 floats):
- indices [0..5]   = `packet.joints[0..5]`        (raw float, already in degrees)
- indices [6..133] = `packet.vision_features[0..127]` (uint8 cast to float, divided by 255.0f)

Output vector (6 floats) = predicted joint targets in degrees.

### Loss

MSE over all 6 outputs:  
`L = (1/6) * sum_i (pred_i - target_i)^2`

Target for supervised IL = the joint values from the recorded demonstration packet.

### Backward pass

Hardcode the chain rule for this **exact** 3-layer architecture.  Do **not**
use dynamic tape/autograd.  Standard notation:

```
dL/d_out2[i]      = (2/6) * (pred[i] - target[i])
dL/d_h1[j]        = sum_i (W2[i][j] * dL/d_out2[i])     * relu_grad(h1[j])
dL/d_h0[k]        = sum_j (W1[j][k] * dL/d_h1[j])       * relu_grad(h0[k])

W2[i][j]  -= lr * dL/d_out2[i] * h1[j]
b2[i]     -= lr * dL/d_out2[i]
W1[j][k]  -= lr * dL/d_h1[j]   * h0[k]
b1[j]     -= lr * dL/d_h1[j]
W0[k][m]  -= lr * dL/d_h0[k]   * input[m]
b0[k]     -= lr * dL/d_h0[k]
```

where `relu_grad(x) = (x > 0.0f) ? 1.0f : 0.0f`

### Gradient norm clipping

Before each weight update, compute the global gradient L2 norm across ALL weight
gradients.  If it exceeds `kGradNormClip = 1.0f`, scale all gradients by
`kGradNormClip / norm`.

### Hyperparameters (add as `constexpr` in `calibration_config.h`)

```cpp
constexpr float    kIlLearningRate    = 0.001f;
constexpr float    kGradNormClip      = 1.0f;
constexpr uint32_t kMinSamplesForTrain = 50;  // don't train until N demos collected
```

### Memory allocation

All weight arrays **must** use `ps_malloc` (PSRAM) with `malloc` fallback.
Allocate in `begin()` after the dataset allocation.  Use `float*` raw pointers
stored as member variables.  Sizes:

```
W0: 134*32 = 4288 floats  (17152 bytes)
b0:     32 floats  (128 bytes)
W1:  32*16 = 512 floats  (2048 bytes)
b1:     16 floats  (64 bytes)
W2:  16* 6 = 96 floats  (384 bytes)
b2:      6 floats  (24 bytes)
Total: ~20 KB
```

Initialise weights with Kaiming (He) uniform: `W ~ Uniform(-sqrt(6/fan_in), +sqrt(6/fan_in))`.
Use `esp_random()` for the random source.  Biases initialise to zero.

### Weight persistence (SD card)

In `begin()`:
- Try to load `/policy_weights.bin` from SD (raw float32 dump in the order
  W0, b0, W1, b1, W2, b2).
- If file not found or size mismatch, initialise randomly (Kaiming).

In `trainStep()` (every `kIlStatsLogEverySteps` steps):
- Write all weights back to `/policy_weights.bin` atomically: write to
  `/policy_weights.tmp` first, then rename.
- Log `[IL] Saved weights` to Serial.

### `infer()` implementation

Replace the stub with a real forward pass:
1. Build 134-float input from packet.
2. Dense0: `h0[k] = relu(dot(W0_row_k, input) + b0[k])`
3. Dense1: `h1[j] = relu(dot(W1_row_j, h0) + b1[j])`
4. Dense2: `out[i] = dot(W2_row_i, h1) + b2[i]`
5. Copy output to `targets_deg[i]`.  Clamp each to `[0, 180]` before return.
6. Return `true` if weights are loaded/trained, `false` if still at random init.

### `trainStep()` integration

The function is called once per 400 Hz control tick.  Do NOT run a full
training step every tick — that would exceed the loop budget.  Instead, run
ONE SGD update per call (online/stochastic gradient descent, batch size = 1,
sample from `dataset_` using `train_cursor_`).

Leave the existing Welford stats accumulation and periodic logging intact.
Add the SGD update AFTER the Welford block.  Guard with:
```cpp
if (dataset_count_ < kMinSamplesForTrain || W0_ == nullptr) return;
```

### Constraints

- C++14/17, Arduino/ESP-IDF framework.
- No dynamic polymorphism, no STL containers beyond `std::array`.
- No heap alloc inside `trainStep()` or `infer()` — everything pre-allocated in `begin()`.
- No FPU-intensive intrinsics; plain `for` loops are fine.
- Keep all symbols inside `namespace node0`.
- Do not change public API signatures (only add implementations to the stubs).

### Expected output from you

1. Updated `il_trainer.h` — new private member variables (weight pointers,
   `is_trained_` flag), updated private method declarations.
2. Updated `il_trainer.cpp` — full implementation of weight init, `infer()`,
   and the SGD block in `trainStep()`.  Leave `begin()`, `observe()`, and
   Welford stats untouched except for appending the weight allocation.
3. Updated `calibration_config.h` snippet — the 3 new `constexpr` lines to add.
4. Build instructions: confirm the file changes compile with
   `pio run -e esp32s3_node0`.
