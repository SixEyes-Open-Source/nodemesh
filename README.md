# NodeMesh

NodeMesh IL (Imitation Learning) gives a SixEyes robotic arm a distributed "nervous system" so it can learn from human demonstrations and act autonomously on-device — no laptop required during inference. Instead of one computer doing everything, multiple microcontrollers cooperate in real time: one reads human input, two handle visual perception, and one coordinates motion, on-device training, and inference.

A human operator drives the arm while the system records experience packets to a microSD card. The arm then trains a compact policy from those recordings and can reproduce the task autonomously.

NodeMesh IL is an optional layer on top of standard SixEyes hardware. The standard SixEyes workflow remains fully valid without it.

## Node Topology

| Node | MCU | Role |
|------|-----|------|
| **Node 0** — Orchestrator | ESP32-S3 (PSRAM + microSD) | IK solver, MotionController, IlTrainer (134→32→16→6 MLP), SdLogger, mode switching (teleop / infer) |
| **Node 1** — Input | ESP32-C6 | Samples 6× 12-bit ADC potentiometers from the leader arm; streams joint state to Node 0 over UART at 921600 baud |
| **Node 2** — Global Camera | ESP32-CAM (OV2640) | Captures global scene; extracts 8×8 spatial brightness grid; sends 64-byte feature vector to Node 0 via ESP-NOW |
| **Node 3** — Wrist Camera | ESP32-CAM (OV2640) | Same as Node 2, eye-in-hand perspective |

Why this split: hard real-time motor control is isolated from camera and parsing workloads. Each node has a single responsibility, making validation and iterative changes tractable.

## Data Flow

```
Node1 ──UART──► Node0 ──► IK solve ──► MotionController ──► motors/servos
Node2 ──ESP-NOW─►  │                        ▲
Node3 ──ESP-NOW─►  │                        │ (INFER mode)
                   ▼                        │
              SdLogger           IlTrainer.infer()
              (microSD)               ▲
                   │           IlTrainer.trainStep()
                   └──────────────────┘
                    (TELEOP_LOG mode)
```

## Operational Modes

Modes persist across reboots via NVS. Switch with the USB serial console:

```
mode teleop   # record demonstrations to SD
mode infer    # replace IK with trained MLP output
status        # print current mode + packet counters
```

## Binary Protocol — ExperiencePacket

All inter-node data uses a single packed struct (170 bytes, little-endian):

| Field | Type | Notes |
|-------|------|-------|
| `magic` | `uint32` | `0x4E4D5650` ("NMVP") |
| `version` | `uint8` | `1` |
| `source_node` | `uint8` | Node ID |
| `payload_len` | `uint16` | Always `sizeof(ExperiencePacket)` |
| `timestamp_us` | `uint32` | `micros()` at sender |
| `seq` | `uint32` | Monotonic per-source sequence number |
| `joints[6]` | `float[6]` | Joint angles in degrees |
| `vision_features[128]` | `uint8[128]` | 8×8 grid (indices 0–63); 64–127 reserved |
| `crc16` | `uint16` | CRC-CCITT over all prior bytes |

Offline log validation: `py tools/validate_log.py node0_log.bin`

## SD Log Format

The log file (`/node0_log.bin`) starts with a 32-byte `SessionHeader`:

| Field | Type | Notes |
|-------|------|-------|
| `magic` | `uint32` | `0x4E4D4C47` ("NMLG") |
| `session_id` | `uint32` | Monotonic, persisted in NVS |
| `start_epoch_s` | `uint32` | Seconds since boot at session open |
| `packet_count` | `uint32` | Updated on every flush batch |
| `reserved` | `uint8[16]` | — |

`ExperiencePacket` records follow contiguously from offset 32. The file is 64 MB pre-allocated and writes circularly.

## On-Device Imitation Learning

The `IlTrainer` class implements a 3-layer MLP in PSRAM:

- **Architecture:** 134 inputs → Dense(32, ReLU) → Dense(16, ReLU) → Dense(6, linear)
- **Input:** 6 joint angles + 128 vision features (normalized to [0, 1])
- **Output:** 6 joint target angles in degrees
- **Training:** Online SGD, batch size 1, one update per 400 Hz tick (after ≥50 demonstrations collected)
- **Loss:** MSE over 6 outputs with gradient norm clipping
- **Persistence:** Weights saved to `/policy_weights.bin` on SD every 2000 training steps

## Repository Structure

```
firmware NodeMesh IL/
  node0_orchestrator_s3/   ESP32-S3 orchestrator firmware
  node1_input_c6/          ESP32-C6 ADC input firmware
  node2_cam/               ESP32-CAM global camera firmware
  node3_cam/               ESP32-CAM wrist camera firmware
  shared/                  ExperiencePacket, RingBuffer, CRC codec (shared lib)
  tools/
    validate_log.py        Offline SD log validator
  docs/                    Firmware checklist, pinouts, deployment guide
```

## Hardware Summary

- **Actuators:** 4× TMC2209 stepper drivers + 3× MG996R servos, 6 DOF
- **Power:** 24V motor rail, 6.6V servo rail (XL4016), 3.3V logic (MP1584)
- **Shoulder:** dual-driver lockstep (GPIO register atomic pulse)
- **Servos:** GPIO40, 41, 42 via ESP32Servo / LEDC
- **SD:** SPI, class 10, FAT32, 32 KB cluster recommended
- **UART (Node1→Node0):** TX GPIO17 / RX GPIO18

## Relationship to SixEyes

NodeMesh originated in the SixEyes monorepo and was split into this repository for independent versioning and issue tracking.

- Main SixEyes repo: https://github.com/SixEyes-Open-Source/sixeyes
- This repo: https://github.com/SixEyes-Open-Source/nodemesh

## Contributing

For firmware, protocol, or hardware changes include: a concise technical rationale, updated documentation for any interface or pinout changes, and validation notes (build, flash, test evidence).

## License

See the repository license file for details.
