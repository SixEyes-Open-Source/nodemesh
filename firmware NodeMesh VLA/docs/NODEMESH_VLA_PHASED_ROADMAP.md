# NodeMeshVLA Phased Roadmap

Purpose: turn implementation into manageable phases with objective entry and exit criteria.

Scope covered:
- Node firmware chunks (Node0, Node1, Node2/3)
- BLE Android app for mode control
- Teleop logging, on-device learning, inference
- Hardware reliability improvements
- Safety, interfaces, observability, testing, deployment

## Phase 0 - System Contract and Ground Rules

Goal:
- Freeze system interfaces before deeper implementation.

Entry criteria:
- Existing architecture and pin docs available.
- Team agrees on BLE-only control policy.

Work items:
- Define mode state machine: TELEOP_LOG, LEARN, INFER, TRANSITION.
- Define command protocol for BLE control and diagnostics.
- Define packet schema and versioning policy across nodes.
- Define episode file format and metadata index format.
- Define timestamps and synchronization policy.
- Define safety invariants (what must always be true).

Exit criteria:
- Interface spec document approved.
- Safety invariants documented and testable.
- Version compatibility rules defined.

## Phase 1 - Node0 Base Firmware (Orchestrator)

Goal:
- Bring Node0 to stable, deterministic baseline operation.

Entry criteria:
- Phase 0 complete.
- Pinout and SD wiring finalized.

Work items:
- Runtime mode manager with NVS persistence.
- Safe mode transition engine and guards.
- Non-blocking UART ingestion for Node1 stream.
- SD circular/buffered writer with crash-safe behavior.
- PSRAM allocation and bounded memory manager.
- Health counters and diagnostics endpoints.

Exit criteria:
- 30-minute soak test without control-loop overruns.
- Safe boot and safe mode switch demonstrated.
- SD writes continue under load without motion starvation.

## Phase 2 - Node1 Base Firmware (Input)

Goal:
- Stable 6-channel teleop acquisition and transport.

Entry criteria:
- Phase 1 core ingest path available.

Work items:
- 12-bit ADC sampling pipeline with filtering.
- Packet framing + CRC + sequence counters.
- Configurable sampling rate and latency telemetry.
- Error handling for UART backpressure.

Exit criteria:
- Continuous stream at target rate with bounded jitter.
- Packet loss/corruption counters validated.
- No blocking behavior in sampling path.

## Phase 3 - Node2 and Node3 Base Firmware (Perception)

Goal:
- Stable perception feature streams from both cameras.

Entry criteria:
- Phase 1 has receiving hooks for vision features.

Work items:
- Camera init and health monitoring.
- Lightweight feature extraction path.
- ESP-NOW transport with retry and counters.
- Sequence + timestamp inclusion in feature packets.

Exit criteria:
- Dual camera streams run continuously for demo window.
- Feature packet drops remain within threshold.
- Node0 receives and tags both streams correctly.

## Phase 4 - BLE Android Kotlin App (Control Plane)

Goal:
- App controls modes and surfaces system health without external compute dependency.

Entry criteria:
- Node0 mode manager and BLE command handlers implemented.

Work items:
- BLE pairing/bonding and reconnect logic.
- Mode switch UI with explicit transition state feedback.
- Start/stop episode controls.
- Diagnostics panel (node health, queue depth, faults, SD status).
- Safe command acknowledgments with sequence IDs.

Exit criteria:
- Mode changes succeed reliably across reconnect cycles.
- App disconnect does not alter robot mode unexpectedly.
- Critical commands acknowledged and logged.

## Phase 5 - Teleoperation and Data Logging Validation

Goal:
- Produce high-quality multi-episode data reliably.

Entry criteria:
- Phase 1 to 4 complete for end-to-end capture path.

Work items:
- Episode lifecycle controls (open, append, close, recover).
- Data alignment validation (joints vs features timestamps).
- Integrity checks and corruption recovery tooling.
- Throughput and latency profiling during long runs.

Exit criteria:
- At least 20 valid episodes with recoverable metadata.
- Time alignment meets defined tolerance.
- Power-loss recovery verified without undefined actuator behavior.

## Phase 6 - On-Device Learning Validation

Goal:
- Prove learning pipeline works within device constraints.

Entry criteria:
- Phase 5 dataset quality gate passed.

Work items:
- Chunked episode loader from SD to PSRAM.
- Baseline model path (for example KNN/MLP-lite).
- Training loop with bounded CPU and memory usage.
- Metrics capture for learning gain vs baseline.

Exit criteria:
- Learning process completes without watchdog faults.
- At least one task metric improves over static baseline.
- Deterministic control remains stable during/after learning.

## Phase 7 - Inference Validation

Goal:
- Stable autonomous execution using learned mapping.

Entry criteria:
- Phase 6 gain demonstrated and repeatable.

Work items:
- Inference runtime path optimization.
- Safety envelopes and action clipping.
- Fallback behavior when model confidence/health fails.
- Mode transition tests between LEARN and INFER.

Exit criteria:
- Inference run stable for full demo duration.
- Safe fallback triggers validated under induced faults.
- No uncommanded motion across mode boundaries.

## Phase 8 - Hardware Reliability Improvements

Goal:
- Improve thermal, power, and mechanical robustness for repeatable operation.

Entry criteria:
- Software path stable enough to expose hardware bottlenecks.

Work items:
- Thermal profiling and cooling updates.
- Power integrity checks under peak servo/stepper load.
- Connector strain relief and vibration mitigation.
- Grounding and return path refinement.

Exit criteria:
- Thermal limits remain within defined envelope.
- No brownouts or reset events under stress tests.
- Mechanical reliability passes repeated handling cycles.

## Cross-Cutting Tracks (Run in Parallel)

### Track A - Safety and Fault Recovery

- Watchdog behavior and estop path validation.
- Fault taxonomy and fault-to-action mapping.
- Safe-state verification for every major failure mode.

Completion condition:
- All hard-stop hazards in go/no-go doc have test evidence.

### Track B - Observability and Debuggability

- Structured logs and event IDs.
- Counters: packet loss, queue overflow, SD latency, mode transition timing.
- On-demand diagnostic snapshot export via BLE control flow.

Completion condition:
- Root cause isolation for common failures within target engineering time.

### Track C - Test Harness and Regression

- Bench harness for packet replay and fault injection.
- Smoke tests after every firmware change.
- Release candidate checklist for multi-node compatibility.

Completion condition:
- Regression pass rate stays above target threshold.

### Track D - Deployment and Versioning

- Semantic versioning for each node and app.
- Compatibility matrix and migration notes.
- Rollback procedure for bad firmware.

Completion condition:
- Reproducible release package and rollback tested.

## Recommended Weekly Cadence

- Week planning: choose one phase objective + one cross-cutting objective.
- Mid-week checkpoint: verify entry assumptions still true.
- End-week review: pass/fail on exit criteria with evidence links.
- Go/no-go review against objective scorecard.

## Stop Rules

Pause new feature work if either occurs:
- Safety or determinism gate regresses.
- Data quality gate fails for two consecutive test cycles.

Resume only after corrective actions are validated.

## Suggested Initial Execution Order

1. Phase 0 (contract freeze)
2. Phase 1 (Node0 base)
3. Phase 4 (BLE app basic mode control)
4. Phase 2 and 3 (Node1 and Node2/3 streams)
5. Phase 5 (teleop logging quality)
6. Phase 6 (learning)
7. Phase 7 (inference)
8. Phase 8 (hardware hardening)
