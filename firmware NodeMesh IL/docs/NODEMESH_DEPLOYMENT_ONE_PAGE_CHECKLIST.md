# NodeMesh Deployment One-Page Checklist

Use this at the bench for first-time or repeat deployments.

## A) Pre-Flash Setup

- [ ] Repo is clean enough for deployment (commit/tag noted).
- [ ] PlatformIO CLI is available.
- [ ] COM port mapping is written down:
  - [ ] Node1 input (ESP32-C6): COM____
  - [ ] Node2 cam global (ESP32-CAM): COM____
  - [ ] Node3 cam wrist (ESP32-CAM): COM____
  - [ ] Node0 orchestrator (ESP32-S3): COM____
- [ ] Power rails and grounds checked before power-on.
- [ ] SD card inserted on Node0 (if logging expected).

## B) Build Dry Run

Run from firmware root:

```powershell
cd "c:\Users\Vincent Santosa\Desktop\SixEyes\nodemesh\firmware NodeMesh IL"
```

- [ ] Node1 build
```powershell
"C:\Users\Vincent Santosa\.platformio\penv\Scripts\pio.exe" run -d "node1_input_c6" -e esp32c6_node1
```

- [ ] Node2 build
```powershell
"C:\Users\Vincent Santosa\.platformio\penv\Scripts\pio.exe" run -d "node2_cam" -e esp32cam_node2
```

- [ ] Node3 build
```powershell
"C:\Users\Vincent Santosa\.platformio\penv\Scripts\pio.exe" run -d "node3_cam" -e esp32cam_node3
```

- [ ] Node0 build
```powershell
"C:\Users\Vincent Santosa\.platformio\penv\Scripts\pio.exe" run -d "node0_orchestrator_s3" -e esp32s3_node0
```

## C) Flash Order (Recommended)

1. [ ] Flash Node1
2. [ ] Flash Node2
3. [ ] Flash Node3
4. [ ] Flash Node0 (last)

Flash template:

```powershell
"C:\Users\Vincent Santosa\.platformio\penv\Scripts\pio.exe" run -d "<node_folder>" -e <env_name> -t upload --upload-port COMx
```

## D) Bring-Up Checks

- [ ] Node1 boots and sends joint packets.
- [ ] Node2 boots, camera initializes, TX counter increments.
- [ ] Node3 boots, camera initializes, TX counter increments.
- [ ] Node0 boots and reports:
  - [ ] UART ingest active
  - [ ] Camera merge active
  - [ ] SD logger ready
  - [ ] IK/IL loop active
- [ ] No uncommanded motion during boot/reconnect.

## E) Release Evidence

- [ ] Commit hash recorded.
- [ ] Build outputs/logs captured for 4 nodes.
- [ ] First successful serial logs saved for 4 nodes.
- [ ] Any anomalies and resolutions documented.

## F) Quick Abort Rules

- [ ] Stop deployment if any node repeatedly fails flash.
- [ ] Stop deployment if Node0 shows unstable control loop behavior.
- [ ] Stop deployment if safety state is unclear after reconnect/power cycle.
