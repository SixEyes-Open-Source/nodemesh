# NodeMesh IL Firmware

Distributed firmware for on-device imitation learning on the SixEyes arm. A human operator drives the arm while Node0 records demonstrations to microSD. The arm then trains a compact behavioral cloning policy from those recordings and acts autonomously — no laptop required during inference.

## Node Topology

- `node0_orchestrator_s3`: ESP32-S3 — IK, MotionController, SdLogger, IlTrainer (134→32→16→6 MLP)
- `node1_input_c6`: ESP32-C6 — 6-channel 12-bit ADC stream from leader arm joints (EMA-filtered, 250 Hz)
- `node2_cam`: ESP32-CAM — global scene, 8×8 brightness grid → Node0 via ESP-NOW
- `node3_cam`: ESP32-CAM — wrist (eye-in-hand), same format as Node2
- `shared`: ExperiencePacket protocol, RingBuffer, CRC-CCITT codec
- `docs`: Pinouts, deployment checklist, firmware checklist

## Quick Start

See `docs/NODEMESH_FLASHING_AND_DEPLOYMENT.md` for full build/flash/monitor commands.

```powershell
cd "firmware NodeMesh IL"
pio run -d node0_orchestrator_s3 -e esp32s3_node0 -t upload --upload-port COMx
```

Then open a serial monitor at 115200 baud on Node0's COM port.

## Operational Workflow

1. **Collect demos** — `mode teleop`, then for each demo: `ep start` → operate → `ep stop`
2. **Watch training** — serial prints `mse=X.XXXXXX` every ~5 s; wait for plateau
3. **Run inference** — `mode infer`
4. **Evaluate** — `trial start` → watch arm → `trial pass` / `trial fail`; results written to `/trials.csv` on SD
5. **Reset if needed** — `log clear` wipes SD log and resets all state

All modes and episode/trial IDs persist across reboots (NVS).

## Policy Details

See the [root README](../../README.md#on-device-imitation-learning) for the full mathematical specification (loss function, weight init, gradient clipping formula, parameter count).

## Deployment Runbook

`docs/NODEMESH_FLASHING_AND_DEPLOYMENT.md` — end-to-end build, flash, monitor, and bring-up sequence.

`docs/NODEMESH_DEPLOYMENT_ONE_PAGE_CHECKLIST.md` — bench checklist for repeat deployments.

## Design Principles

- Control loop is deterministic and non-blocking (400 Hz hard deadline on Node0)
- Binary packet transport for all high-rate links; offline-validatable with `tools/validate_log.py`
- All training data persisted to SD; `loadFromLog()` on boot restores full dataset across power cycles
- Inference latency logged to serial in `INFER` mode (`avg_latency=XXX us`)
