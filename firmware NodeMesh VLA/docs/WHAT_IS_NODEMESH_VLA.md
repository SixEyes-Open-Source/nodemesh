# What Is NodeMeshVLA?

## 1) Concise Non-Technical Overview

NodeMeshVLA is a way of giving a robotic arm its own distributed "nervous system" so it can sense, learn, and act directly on-device without needing a constant laptop connection. Instead of one computer doing everything, multiple tiny processors cooperate in real time: one reads human input, two understand visual context, and one coordinates motion and learning. The significance is reliability, portability, and lower latency: the robot can keep working in places where a full external computer setup is inconvenient, fragile, or unavailable.

## 2) Full Technical Breakdown (Engineer/Developer)

This section captures the full technical scope defined for SixEyes NodeMeshVLA and the current implementation direction.

### 2.1 System Identity and Goal

- Project: SixEyes 6-DOF robotic arm (embodied AI).
- Framework concept: NodeMesh, a distributed multi-MCU architecture.
- Runtime objective: standalone edge training + inference with deterministic motion control.
- Software direction: migration from host-centric Python flows to embedded C++ (Arduino/ESP-IDF-compatible style).

### 2.2 Node-Level Topology and Responsibilities

NodeMeshVLA uses four asynchronous nodes with explicit task separation:

1. NODE_0 Orchestrator (ESP32-S3, target with PSRAM + microSD)
- Acts as central coordinator.
- Runs/hosts action generation path (including IK and lightweight VLA mapping).
- Owns storage/logging pipeline to microSD.
- Drives actuators (4 TMC2209 channels + 3 servos).
- Handles stream synchronization between teleoperation state and visual features.

2. NODE_1 Input (ESP32-C6)
- Samples 6 channels of 12-bit analog input from leader arm potentiometers.
- Publishes joint-state stream to NODE_0 over high-speed wired UART.

3. NODE_2 Perception Global (ESP32-S3 CAM)
- Captures global camera perspective.
- Performs local feature extraction.
- Sends compact feature vector stream wirelessly to NODE_0.

4. NODE_3 Perception Wrist (ESP32-S3 CAM)
- Captures eye-in-hand camera perspective.
- Performs local feature extraction.
- Sends compact feature vector stream wirelessly to NODE_0.

Why this split exists:
- It isolates hard real-time motion from camera and parsing workloads.
- It keeps per-node firmware simpler and easier to validate.
- It scales incrementally (algorithm changes on one node do not require whole-system redesign).

### 2.3 Data Plane and Control Plane

NodeMeshVLA has two operational planes:

1. Data plane (teleop/training payload)
- Joint state stream from NODE_1.
- Vision feature streams from NODE_2/3.
- Aggregated experience packets written by NODE_0.

2. Control/safety plane
- Deterministic control loop for motors/servos.
- Heartbeat/safety monitoring and fault gates.
- Enable gating (EN_ALL) and independent driver diagnostics via PDN selection.

### 2.4 Core Data Flow

Canonical training/teleoperation flow:

1. NODE_1 samples joint inputs and transmits to NODE_0 (UART, target 921600).
2. NODE_2 and NODE_3 generate and send visual embeddings/features to NODE_0 (ESP-NOW).
3. NODE_0 time-aligns streams, forms experience records, and logs to microSD.
4. NODE_0 later replays buffered data into memory for lightweight model fitting.
5. Learned mapping influences action-token generation and motion targets.

Key property: feature extraction should happen near sensors; only compact vectors are transported.

### 2.5 Packetization and Binary Protocol

Design requirement is packet-based binary transport, not ad-hoc text streams.

Required conceptual record:
- Experience packet with timestamp + 6 joints + 128-byte vision vector.

Current shared scaffold includes:
- Magic/version for packet sanity.
- Source node ID tagging.
- Payload length field.
- Timestamp in microseconds.
- CRC integrity check.

Engineering implications:
- Deterministic packet parsing under noisy links.
- Versioning path for future schema changes.
- Easier offline tooling and replay.

### 2.6 Motion Architecture and Determinism

Motion stack requirements:

- Stepper pulses should be interrupt/timer-driven at high frequency.
- Servo control should use hardware PWM (LEDC or MCPWM) to reduce jitter.
- Control loop target is deterministic periodic execution (current follower code targets 400 Hz).
- SD and communications must not block the control loop.

Dual-driver shoulder requirement:
- Shoulder load is split across two TMC2209 channels.
- Those two channels must receive synchronized motion intent.
- In NodeMesh scaffold, shoulder mirror pulse handling is explicitly separated to preserve lockstep behavior.

### 2.7 TMC2209 Control Strategy and Pin Policy

Current selected hardware policy:
- Shared EN: yes (`EN_ALL` on GPIO6).
- Separate PDN_UART lines: yes (one PDN select per driver).

Why:
- Shared EN reduces pin use and fits board constraints.
- Existing driver code uses PDN line selection logic (drive one PDN low, others high) for one-at-a-time UART interactions.
- Fully shared PDN bus would require driver-addressing strategy and firmware refactor to avoid contention.

Follower signal map in active revision:
- STEP: GPIO4, 8, 12, 16
- DIR: GPIO5, 9, 13, 17
- EN_ALL: GPIO6
- PDN: GPIO7, 11, 15, 21
- Servos: GPIO35, 36, 37
- Inter-board UART: RX GPIO38, TX GPIO39

### 2.8 Communications Breakdown

1. Wired inter-board UART (NODE_1 -> NODE_0)
- High-rate stream for joint teleoperation values.
- Needs non-blocking receive and bounded buffering.
- CRC-framed packets preferred to avoid parser drift.

2. Wireless ESP-NOW (NODE_2/3 -> NODE_0)
- Lightweight, low-overhead telemetry from camera nodes.
- Requires packet sizing discipline and retry/error instrumentation.

3. USB serial path (development/telemetry)
- Used for diagnostics and bridge-style observability.
- Should not be in critical timing path for control.

### 2.9 Memory and Storage Strategy

1. RAM policy
- Use PSRAM for large feature/training buffers.
- Use explicit allocation discipline (`ps_malloc`) to protect internal SRAM budget.

2. SD logging policy
- External microSD over SPI.
- Recommended card/class: class 10.
- Filesystem target: FAT32 with larger cluster preference (32 KB) for write behavior.
- Use circular or queued writer architecture so storage latency does not stall control.

3. Persistence model
- Online runtime samples become durable training corpus.
- Enables post-run replay and iterative onboard tuning.

### 2.10 Safety and Fault-Handling Intent

Safety model expectations include:
- Heartbeat supervision.
- Fast disable path through EN gating.
- Fault manager that can de-energize motion path quickly.
- Separation between command parsing and actuator execution.

Practical effect:
- Communication loss should move system toward safe state.
- Logging/learning features must never bypass safety constraints.

### 2.11 Power-Domain Architecture

Three principal rails are expected:
- 24V motor domain for TMC VMOT.
- 6.6V servo domain.
- 3.3V logic domain for MCUs and low-voltage interfaces.

Layout implications:
- Strong common grounding strategy.
- High-current return paths kept away from sensitive logic reference paths.
- Decoupling near noisy consumers and interface modules (for example SD and drivers).

### 2.12 SD Interface Requirements (Node0)

SPI six-wire pattern:
- Power: VCC, GND.
- Signals: SCK, MISO, MOSI, CS.

Current Node0 wiring plan:
- SCK GPIO40
- MISO GPIO41
- MOSI GPIO42
- CS GPIO2

Constraints:
- Keep traces short, especially clock.
- Prefer 3.3V-safe module interface.
- Validate board-variant pin availability before lock.

### 2.13 Software Structure in the Current Scaffold

The current framework under firmware NodeMesh VLA includes:
- Shared protocol/types/utilities.
- Per-node project skeletons.
- Node0 modules for UART ingest, mirrored shoulder stepper, SD queue logger stub, VLA learning stub.
- Node1 ADC sampling + packet transmit scaffold.
- Node2/Node3 camera-node stubs for future feature extraction and ESP-NOW transmission.
- Dedicated docs for pinout, SD wiring, checklist, and connector mapping.

This is a framework baseline, not final production firmware.

### 2.14 Immediate Engineering Work Items

Defined priority tasks include:
- Complete dual-driver shoulder pulse strategy in final motion scheduler.
- Port IK from existing Python implementation to embedded C++ module(s).
- Implement robust non-blocking 921600 UART pipeline.
- Implement SD buffered writer with bounded queue and failure handling.
- Add PSRAM-backed sample storage and learner path.
- Complete camera feature extraction + ESP-NOW transport.
- Add integration and fault-injection tests.

### 2.15 Why NodeMeshVLA Matters Technically

- It makes embodied learning possible within embedded constraints by splitting work across specialized MCUs.
- It keeps control determinism as a first-class constraint rather than an afterthought.
- It transforms raw teleoperation into structured, reusable on-device datasets.
- It creates a path from hand-guided behavior to autonomous policy execution on edge hardware.

### 2.16 Current Decision Snapshot

- Controller board context: ESP32-S3 DevKitC-1 on header pins into custom PCB.
- Pin-budget strategy: shared EN + separate PDN.
- Collision audit: active map has no pin double assignment in current revision.
- Documentation status: pinout, SD wiring, checklist, and connector tables are in place for PCB execution.
