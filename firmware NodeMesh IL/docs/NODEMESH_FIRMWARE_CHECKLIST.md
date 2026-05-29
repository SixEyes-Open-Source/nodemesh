# NodeMesh Firmware Checklist

## Done

- [x] Created NodeMesh framework root README and topology summary.
- [x] Created shared packet protocol headers and codec implementation.
- [x] Added shared ring buffer utility for non-blocking queueing.
- [x] Scaffolded PlatformIO projects for node0, node1, node2, node3.
- [x] Added node0 orchestrator main loop scaffold (UART ingest, SD queue, IL trainer hooks).
- [x] Added node0 shoulder mirror stepper scaffold for dual-driver lockstep pulses.
- [x] Added node0 SD SPI wiring doc.
- [x] Added node1 input ADC-to-packet streaming scaffold.
- [x] Added node2/node3 camera node boot and packet stubs.
- [x] Added follower pinout doc for PCB routing.
- [x] Audited follower pin assignments for collisions (no duplicates in current map).
- [x] Verified shared EN is compatible with current control model.
- [x] Verified shared PDN is not compatible with current TMC2209 selection logic without firmware refactor.
- [x] Finalized hardware policy: shared EN + separate PDN.
- [x] Updated follower firmware EN pin config to EN_ALL on GPIO14.
- [x] Created one-page connector legend for travel/bench wiring.
- [x] Created NodeMesh IL overview doc (public summary + full technical breakdown).
- [x] Created phased roadmap with entry/exit criteria and cross-cutting tracks.

## Phase A — Actuation (prerequisite for everything else)

- [x] Updated schematic net labels and connector pin table to match EN_ALL on GPIO14.
- [x] Implement true non-blocking high-speed UART pipeline at 921600 on node0.
- [x] Implement SD filesystem init and pre-allocated/circular write blocks.
- [x] Add PSRAM allocation paths (`ps_malloc`) for vision and training datasets.
- [x] Implement ESP-NOW transport and feature-vector extraction on node2/node3.
- [x] Integrate Node0 ESP-NOW receive path and merge node2/node3 features into orchestrator packets.
- [x] Wire IK output to stepper pulse generation and servo writes in `main.cpp` (MotionController + LPF).
- [x] Add per-joint angle limits and velocity clipping in IkSolver / MotionController.
- [ ] Validate full teleoperation loop end-to-end: Node1 → Node0 IK → motors actuate.
- [x] Port IK calibration constants from Python stack to match physical arm geometry (250 mm links, 500 mm reach; TODO: verify with calipers).

## Phase B — Data Quality (needed before training)

- [x] Add session boundary markers to SD log (SessionHeader: magic, session_id from NVS, start_epoch_s, packet_count at offset 0).
- [x] Add packet sequence numbers to `ExperiencePacket` for gap detection during offline replay.
- [x] Build offline log validation script (reads binary log, checks CRCs and sequence continuity).
- [x] Add packet-level integration tests for node1 → node0 and cam nodes → node0.
- [x] Add fault-injection tests for heartbeat timeout and motor disable behavior.
- [ ] **Target: 100 clean teleoperation episodes recorded to SD.**

## Phase C — Vision Upgrade (needed for spatial tasks)

- [x] Replace global grayscale histogram with 8×8 spatial grid of mean brightness (64 values, same packet size).
- [x] Evaluate whether ESP32-CAM can decode JPEG to get luminance-only quickly enough for 30 FPS feature extraction.
      **Result:** Not needed. Raw QQVGA grayscale path completes in ~13.4 ms/frame (74.9 FPS max), leaving ~20 ms of slack.
      Full JPEG decode (TJpgDec) adds 5.5 ms with no capture speedup. DC-only partial decoder saves trivially but requires
      ~200 lines of custom bitstream code and is fragile. Keep `PIXFORMAT_GRAYSCALE`. See `tools/eval_jpeg_timing.py`.
- [x] Update `ExperiencePacket` vision feature interpretation in `IkSolver` and `IlTrainer` after format change.

## Phase D — On-Device Behavioral Cloning (`IlTrainer`)

- [x] Implement forward pass for 3-layer MLP (input 134 → hidden 32 → hidden 16 → output 6) in `il_trainer.cpp`.
- [x] Implement MSE loss computation over the 6 output joints.
- [x] Implement backward pass (hardcoded chain rule for this fixed 3-layer architecture).
- [x] Implement SGD weight update with configurable learning rate and gradient norm clipping.
- [x] Add weight persistence: save/load trained weights as raw float binary blob to/from SD on boot.
- [x] Validate: training loss decreases over 1000 epochs on a fixed 100-demo dataset in PSRAM.
      **Result:** 97.9% MSE reduction (21969 -> 458) over 1000 epochs on synthetic 100-demo dataset.
      Pure Python mirror of il_trainer.cpp (same Kaiming init, MSE loss, factored grad-clip, SGD).
      26/26 tests passing. See `tools/test_il_training.py`.
- [x] Add mode switching: `TELEOP_LOG` → `INFER` with NVS persistence across reboots.

## Phase E — Inference Episode

- [x] Add inference mode: `IlTrainer` forward pass replaces IK joint targets when in `INFER` mode.
- [x] Wire inference output to motor actuation path.
- [ ] **Episode goal: fixed-station pick-and-place — block placed within ±3 cm of pickup station, destination marker 20 cm away, ≥ 60% success over 20 autonomous attempts after training on 100 demonstrations.**

## Hardware Pre-Flight (PCB)

- [ ] Confirm exact ESP32-S3 module pin availability for chosen board SKU.
- [ ] Confirm SD module voltage domain is 3.3V-safe on all IO.
- [ ] Add pull-ups/series resistors on PDN/UART nets per TMC2209 design.
- [ ] Verify dual-shoulder STEP/DIR trace matching and return paths.
- [ ] Review high-current ground routing for servo and stepper domains.
