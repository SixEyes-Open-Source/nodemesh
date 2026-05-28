# ESP32-S3 DevKitC-1 Header to PCB Net Table (Follower)

This table is the wiring map for the follower controller using:

- Shared EN (`EN_ALL` on GPIO14)
- Shared EN (`EN_ALL` on GPIO14)
- Separate PDN per TMC2209 driver
- Separate STEP/DIR per axis

Reference used: the ESP32-S3 DevKitC-1 pinout image you shared.

## Main Signal Mapping

| DevKit Header GPIO | Schematic Net Name | Destination | Notes |
|:--|:--|:--|:--|
| GPIO14 | EN_ALL | TMC2209 J1/J2/J3/J4 EN | Shared enable, active low |
| GPIO13 | PDN_J1 | TMC2209 J1 PDN_UART | Per-driver PDN select |
| GPIO12 | STEP_J1 | TMC2209 J1 STEP | Base axis step pulse |
| GPIO11 | DIR_J1 | TMC2209 J1 DIR | Base axis direction |
| GPIO10 | PDN_J2 | TMC2209 J2 PDN_UART | Per-driver PDN select |
| GPIO9 | STEP_J2 | TMC2209 J2 STEP | Shoulder A step pulse |
| GPIO8 | DIR_J2 | TMC2209 J2 DIR | Shoulder A direction |
| GPIO18 | UART_LEADER_RX | Leader UART TX -> follower RX | Inter-board UART |
| GPIO17 | UART_LEADER_TX | Leader UART RX <- follower TX | Inter-board UART |
| GPIO16 | PDN_J3 | TMC2209 J3 PDN_UART | Per-driver PDN select |
| GPIO15 | STEP_J3 | TMC2209 J3 STEP | Shoulder B step pulse |
| GPIO7 | DIR_J3 | TMC2209 J3 DIR | Shoulder B direction |
| GPIO6 | PDN_J4 | TMC2209 J4 PDN_UART | Per-driver PDN select |
| GPIO5 | STEP_J4 | TMC2209 J4 STEP | Elbow step pulse |
| GPIO4 | DIR_J4 | TMC2209 J4 DIR | Elbow direction |
| GPIO35 | SD_MOSI | SD module DI/MOSI | Left-side SD block |
| GPIO36 | SD_SCK | SD module CLK | Left-side SD block |
| GPIO37 | SD_MISO | SD module DO/MISO | Left-side SD block |
| GPIO38 | SD_CS | SD module CS | Left-side SD block |
| GPIO39 | SD_CD | SD card detect input | Left-side SD block |
| GPIO40 | SERVO_WRIST_PITCH | Wrist pitch servo signal | 50 Hz PWM |
| GPIO41 | SERVO_WRIST_YAW | Wrist yaw servo signal | 50 Hz PWM |
| GPIO42 | SERVO_GRIPPER | Gripper servo signal | 50 Hz PWM |

## Reserved / Free GPIOs in This Revision

| GPIO | Status | Suggested Use |
|:--|:--|:--|
| GPIO21 | Free | RTC / future extension |
| GPIO47 | Free | Future extension |
| GPIO1 | Free | Touch/ADC future extension |
| GPIO2 | Free | Touch/ADC future extension |

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
| GPIO36 | SD_SCK | SD module CLK |
| GPIO37 | SD_MISO | SD module DO/MISO |
| GPIO35 | SD_MOSI | SD module DI/MOSI |
| GPIO38 | SD_CS | SD module CS |
| GPIO39 | SD_CD | SD card detect input |

## Pre-Flight Wiring Checks

- Confirm `EN_ALL` fans out to all 4 TMC EN pins and nowhere else.
- Confirm each `PDN_Jx` only goes to its matching driver.
- Confirm shoulder dual drivers (J2/J3) receive separate STEP/DIR (no accidental short).
- Confirm UART lines are crossed correctly: leader TX -> follower RX, leader RX <- follower TX.
- Confirm servo grounds return to common ground and do not share narrow high-current motor return traces.
