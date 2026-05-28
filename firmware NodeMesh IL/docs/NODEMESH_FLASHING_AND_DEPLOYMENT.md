# NodeMesh Flashing and Deployment Guide

This runbook is for deploying the NodeMesh IL firmware stack across Node0-Node3.

Scope:
- Node0: ESP32-S3 orchestrator
- Node1: ESP32-C6 input node
- Node2: ESP32-CAM global camera node
- Node3: ESP32-CAM wrist camera node

## 1) Prerequisites

- PlatformIO installed and available in terminal.
- Four USB data cables (or one cable with strict flash order).
- Known COM ports for each node.
- Repo checked out and clean.

Suggested check commands:

```powershell
cd "c:\Users\Vincent Santosa\Desktop\SixEyes\nodemesh\firmware NodeMesh VLA"
pio --version
git status
```

## 2) Firmware Targets

From each node platform config:

- Node0 environment: esp32s3_node0
- Node1 environment: esp32c6_node1
- Node2 environment: esp32cam_node2
- Node3 environment: esp32cam_node3

## 3) Recommended Deployment Order

Use this order to reduce debug ambiguity:

1. Node1 (teleop input source)
2. Node2 (global camera)
3. Node3 (wrist camera)
4. Node0 (orchestrator, flashed last so it boots against updated peers)

## 4) Build Commands

Run from repo root folder:

```powershell
cd "c:\Users\Vincent Santosa\Desktop\SixEyes\nodemesh\firmware NodeMesh IL"

pio run -d "node1_input_c6" -e esp32c6_node1
pio run -d "node2_cam" -e esp32cam_node2
pio run -d "node3_cam" -e esp32cam_node3
pio run -d "node0_orchestrator_s3" -e esp32s3_node0
```

If you want clean rebuilds before release:

```powershell
pio run -d "node1_input_c6" -e esp32c6_node1 -t clean
pio run -d "node2_cam" -e esp32cam_node2 -t clean
pio run -d "node3_cam" -e esp32cam_node3 -t clean
pio run -d "node0_orchestrator_s3" -e esp32s3_node0 -t clean
```

Then run normal builds again.

## 5) Flash Commands

Replace COMx with your real port each time.

### Node1

```powershell
pio run -d "node1_input_c6" -e esp32c6_node1 -t upload --upload-port COMx
```

### Node2

```powershell
pio run -d "node2_cam" -e esp32cam_node2 -t upload --upload-port COMx
```

### Node3

```powershell
pio run -d "node3_cam" -e esp32cam_node3 -t upload --upload-port COMx
```

### Node0

```powershell
pio run -d "node0_orchestrator_s3" -e esp32s3_node0 -t upload --upload-port COMx
```

## 6) Serial Monitor Commands

### Node1

```powershell
pio device monitor -d "node1_input_c6" -b 115200 -p COMx
```

### Node2

```powershell
pio device monitor -d "node2_cam" -b 115200 -p COMx
```

### Node3

```powershell
pio device monitor -d "node3_cam" -b 115200 -p COMx
```

### Node0

```powershell
pio device monitor -d "node0_orchestrator_s3" -b 115200 -p COMx
```

## 7) Bring-Up Validation Sequence

Run this sequence after flashing:

1. Boot Node1 and verify periodic packet transmit behavior.
2. Boot Node2 and Node3 and verify camera init plus ESP-NOW transmit counters.
3. Boot Node0 and verify:
   - UART packets are being consumed.
   - Camera merge path is receiving data.
   - SD logger initializes.
   - IK and IL runtime loop remains active.

## 8) Failure Recovery

If upload fails:

1. Confirm data-capable USB cable.
2. Confirm correct COM port.
3. Power-cycle board and retry upload.
4. For camera-class boards, use BOOT/RESET button sequence if required by board.
5. Retry with a lower upload speed only if repeated port timeouts occur.

## 9) Release Artifact Checklist

Before declaring deployment-ready:

- Record git commit hash used for each node deployment.
- Record COM-to-node mapping used during flash.
- Save build output logs for all four nodes.
- Capture first successful boot logs from all four nodes.

## 10) Related NodeMesh Docs

- NodeMesh firmware checklist: docs/NODEMESH_FIRMWARE_CHECKLIST.md
- Go/No-Go gates: docs/NODEMESH_VLA_GO_NO_GO.md
- One-page bench checklist: docs/NODEMESH_DEPLOYMENT_ONE_PAGE_CHECKLIST.md
- SD wiring (Node0): docs/SD_MODULE_WIRING_NODE0.md
- NodeMesh architecture summary: docs/WHAT_IS_NODEMESH_VLA.md
