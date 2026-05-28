# Follower ESP32-S3 Pinout (PCB Draft)

This follows the current SixEyes follower mapping.

## Stepper Drivers (TMC2209 x4)

| Channel | Joint | STEP | DIR | EN | PDN_UART |
|:--|:--|:--|:--|:--|:--|
| J1 | Base | GPIO12 | GPIO11 | GPIO14 (EN_ALL) | GPIO13 |
| J2 | Shoulder A | GPIO9 | GPIO8 | GPIO14 (EN_ALL) | GPIO10 |
| J3 | Shoulder B | GPIO15 | GPIO7 | GPIO14 (EN_ALL) | GPIO16 |
| J4 | Elbow | GPIO5 | GPIO4 | GPIO14 (EN_ALL) | GPIO6 |

## Shared EN and Shared PDN_UART Option

After checking the current follower firmware implementation, use this guidance:

- Shared EN: yes (safe and compatible with current code).
- Shared PDN_UART: not with current code; keep per-driver PDN lines unless firmware is refactored.

Final wiring decision (current):

- `EN_ALL` on GPIO14 to all driver EN inputs.
- Keep separate PDN pins per driver: GPIO13, GPIO10, GPIO16, GPIO6.

Important constraints:

- Keep independent STEP and DIR per axis (do not share those).
- Current follower firmware selects one driver at a time by pulling only that driver PDN LOW and all others HIGH.
- Shared PDN would break that selection logic and cause UART contention.
- If you later move to shared PDN bus, each driver must have unique UART address straps and the driver code must be updated.
- With shared EN, you lose per-driver emergency isolate. Keep strict current limits and fault handling.

## Pin Collision Audit (Follower)

The currently assigned follower pins are unique (no double assignment):

- Stepper STEP: 12, 9, 15, 5
- Stepper DIR: 11, 8, 7, 4
- Stepper EN: 14 (shared EN_ALL)
- Stepper PDN: 13, 10, 16, 6
- Servo PWM: 40, 41, 42
- SD SPI + detect: 35, 36, 37, 38, 39
- Inter-board UART: RX 18, TX 17

No overlaps were found across these functions.

## Servo Outputs

| Function | GPIO |
|:--|:--|
| Wrist Pitch | GPIO40 |
| Wrist Yaw | GPIO41 |
| Gripper | GPIO42 |

## SD SPI + Card Detect

| Signal | GPIO |
|:--|:--|
| SD_MOSI | GPIO35 |
| SD_SCK | GPIO36 |
| SD_MISO | GPIO37 |
| SD_CS | GPIO38 |
| SD_CD | GPIO39 |

## Inter-Board UART (Leader <-> Follower)

| Signal | Follower GPIO | Leader GPIO |
|:--|:--|:--|
| RX | GPIO18 | TX GPIO17 |
| TX | GPIO17 | RX GPIO18 |

## Power Rails

- VMOT/TMC domain: 24V input
- Servo rail: 6.6V buck output
- Logic rail: 3.3V
- Ground: common ground plane/star return near power entry
