# Connector Legend (One Page) - ESP32-S3 DevKitC-1 to Follower PCB

Use this as the quick wiring card while building.

Configuration locked:
- Shared EN: yes (`EN_ALL` on GPIO6)
- PDN_UART: separate per driver
- STEP/DIR: separate per driver

## A) Motion Signals

| MCU GPIO | Net | Destination |
|:--|:--|:--|
| 4 | STEP_J1 | TMC2209 J1 STEP (Base) |
| 5 | DIR_J1 | TMC2209 J1 DIR (Base) |
| 6 | EN_ALL | TMC2209 J1/J2/J3/J4 EN (shared, active low) |
| 7 | PDN_J1 | TMC2209 J1 PDN_UART |
| 8 | STEP_J2 | TMC2209 J2 STEP (Shoulder A) |
| 9 | DIR_J2 | TMC2209 J2 DIR (Shoulder A) |
| 11 | PDN_J2 | TMC2209 J2 PDN_UART |
| 12 | STEP_J3 | TMC2209 J3 STEP (Shoulder B) |
| 13 | DIR_J3 | TMC2209 J3 DIR (Shoulder B) |
| 15 | PDN_J3 | TMC2209 J3 PDN_UART |
| 16 | STEP_J4 | TMC2209 J4 STEP (Elbow) |
| 17 | DIR_J4 | TMC2209 J4 DIR (Elbow) |
| 21 | PDN_J4 | TMC2209 J4 PDN_UART |

## B) Servo Signals

| MCU GPIO | Net | Destination |
|:--|:--|:--|
| 35 | SERVO_WRIST_PITCH | Wrist pitch servo signal |
| 36 | SERVO_WRIST_YAW | Wrist yaw servo signal |
| 37 | SERVO_GRIPPER | Gripper servo signal |

## C) Leader-Follower UART

| MCU GPIO | Net | Destination |
|:--|:--|:--|
| 38 | UART_LEADER_RX | Leader TX -> Follower RX |
| 39 | UART_LEADER_TX | Leader RX <- Follower TX |

## D) Power Nets

| Net | Purpose |
|:--|:--|
| +24V_MOTOR | TMC VMOT input rail |
| +6V6_SERVO | Servo power rail |
| +3V3_LOGIC | ESP32 + logic + TMC VIO |
| GND | Common ground |

## E) Node0 SD SPI (Only if this board also carries Node0)

| MCU GPIO | Net | Destination |
|:--|:--|:--|
| 40 | SD_SCK | SD CLK |
| 41 | SD_MISO | SD DO/MISO |
| 42 | SD_MOSI | SD DI/MOSI |
| 2 | SD_CS | SD CS |

## F) Free Pins in Current Revision

- GPIO10
- GPIO14
- GPIO18

## G) 60-Second Pre-Flight Check

- EN_ALL fans out to all four TMC EN pins.
- PDN_J1..PDN_J4 are not shorted together.
- Shoulder pair J2/J3 each has its own STEP and DIR traces.
- UART lines are crossed correctly between boards.
- Servo grounds have solid return path and are not necked through motor-current bottlenecks.
