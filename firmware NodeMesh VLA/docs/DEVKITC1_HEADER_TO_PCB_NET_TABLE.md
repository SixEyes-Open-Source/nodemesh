# ESP32-S3 DevKitC-1 Header to PCB Net Table (Follower)

This table is the wiring map for the follower controller using:

- Shared EN (`EN_ALL` on GPIO6)
- Separate PDN per TMC2209 driver
- Separate STEP/DIR per axis

Reference used: the ESP32-S3 DevKitC-1 pinout image you shared.

## Main Signal Mapping

| DevKit Header GPIO | Schematic Net Name | Destination | Notes |
|:--|:--|:--|:--|
| GPIO4 | STEP_J1 | TMC2209 J1 STEP | Base axis step pulse |
| GPIO5 | DIR_J1 | TMC2209 J1 DIR | Base axis direction |
| GPIO6 | EN_ALL | TMC2209 J1/J2/J3/J4 EN | Shared enable, active low |
| GPIO7 | PDN_J1 | TMC2209 J1 PDN_UART | Per-driver PDN select |
| GPIO8 | STEP_J2 | TMC2209 J2 STEP | Shoulder A step pulse |
| GPIO9 | DIR_J2 | TMC2209 J2 DIR | Shoulder A direction |
| GPIO11 | PDN_J2 | TMC2209 J2 PDN_UART | Per-driver PDN select |
| GPIO12 | STEP_J3 | TMC2209 J3 STEP | Shoulder B step pulse |
| GPIO13 | DIR_J3 | TMC2209 J3 DIR | Shoulder B direction |
| GPIO15 | PDN_J3 | TMC2209 J3 PDN_UART | Per-driver PDN select |
| GPIO16 | STEP_J4 | TMC2209 J4 STEP | Elbow step pulse |
| GPIO17 | DIR_J4 | TMC2209 J4 DIR | Elbow direction |
| GPIO21 | PDN_J4 | TMC2209 J4 PDN_UART | Per-driver PDN select |
| GPIO35 | SERVO_WRIST_PITCH | Wrist pitch servo signal | 50 Hz PWM |
| GPIO36 | SERVO_WRIST_YAW | Wrist yaw servo signal | 50 Hz PWM |
| GPIO37 | SERVO_GRIPPER | Gripper servo signal | 50 Hz PWM |
| GPIO38 | UART_LEADER_RX | Leader UART TX -> follower RX | Inter-board UART |
| GPIO39 | UART_LEADER_TX | Leader UART RX <- follower TX | Inter-board UART |

## Reserved / Free GPIOs in This Revision

| GPIO | Status | Suggested Use |
|:--|:--|:--|
| GPIO10 | Free | Future limit switch / fault input |
| GPIO14 | Free | Future limit switch / fault input |
| GPIO18 | Free | Future debug GPIO / estop sense |

## Power and Ground Connector Nets

| Net | Destination |
|:--|:--|
| +3V3_LOGIC | ESP32-S3 3.3V, TMC VIO, logic pull-ups |
| +6V6_SERVO | Servo power rail |
| +24V_MOTOR | TMC VMOT rail |
| GND | Common ground star-connected near power entry |

## SD Module (Node0 Orchestrator Only)

Do not route these to follower if follower board is motor-only.

| DevKit Header GPIO | Schematic Net Name | Destination |
|:--|:--|:--|
| GPIO40 | SD_SCK | SD module CLK |
| GPIO41 | SD_MISO | SD module DO/MISO |
| GPIO42 | SD_MOSI | SD module DI/MOSI |
| GPIO2 | SD_CS | SD module CS |

## Pre-Flight Wiring Checks

- Confirm `EN_ALL` fans out to all 4 TMC EN pins and nowhere else.
- Confirm each `PDN_Jx` only goes to its matching driver.
- Confirm shoulder dual drivers (J2/J3) receive separate STEP/DIR (no accidental short).
- Confirm UART lines are crossed correctly: leader TX -> follower RX, leader RX <- follower TX.
- Confirm servo grounds return to common ground and do not share narrow high-current motor return traces.
