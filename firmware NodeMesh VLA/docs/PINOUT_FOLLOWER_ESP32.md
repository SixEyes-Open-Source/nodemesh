# Follower ESP32-S3 Pinout (PCB Draft)

This follows the current SixEyes follower mapping.

## Stepper Drivers (TMC2209 x4)

| Channel | Joint | STEP | DIR | EN | PDN_UART |
|:--|:--|:--|:--|:--|:--|
| J1 | Base | GPIO4 | GPIO5 | GPIO6 | GPIO7 |
| J2 | Shoulder A | GPIO8 | GPIO9 | GPIO6 (EN_ALL) | GPIO11 |
| J3 | Shoulder B | GPIO12 | GPIO13 | GPIO6 (EN_ALL) | GPIO15 |
| J4 | Elbow | GPIO16 | GPIO17 | GPIO6 (EN_ALL) | GPIO21 |

## Shared EN and Shared PDN_UART Option

After checking the current follower firmware implementation, use this guidance:

- Shared EN: yes (safe and compatible with current code).
- Shared PDN_UART: not with current code; keep per-driver PDN lines unless firmware is refactored.

Final wiring decision (current):

- `EN_ALL` on GPIO6 to all driver EN inputs.
- Keep separate PDN pins per driver: GPIO7, GPIO11, GPIO15, GPIO21.

Important constraints:

- Keep independent STEP and DIR per axis (do not share those).
- Current follower firmware selects one driver at a time by pulling only that driver PDN LOW and all others HIGH.
- Shared PDN would break that selection logic and cause UART contention.
- If you later move to shared PDN bus, each driver must have unique UART address straps and the driver code must be updated.
- With shared EN, you lose per-driver emergency isolate. Keep strict current limits and fault handling.

## Pin Collision Audit (Follower)

The currently assigned follower pins are unique (no double assignment):

- Stepper STEP: 4, 8, 12, 16
- Stepper DIR: 5, 9, 13, 17
- Stepper EN: 6 (shared EN_ALL)
- Stepper PDN: 7, 11, 15, 21
- Servo PWM: 35, 36, 37
- Inter-board UART: RX 38, TX 39

No overlaps were found across these functions.

## Servo Outputs

| Function | GPIO |
|:--|:--|
| Wrist Pitch | GPIO35 |
| Wrist Yaw | GPIO36 |
| Gripper | GPIO37 |

## Inter-Board UART (Leader <-> Follower)

| Signal | Follower GPIO | Leader GPIO |
|:--|:--|:--|
| RX | GPIO38 | TX GPIO17 |
| TX | GPIO39 | RX GPIO18 |

## Power Rails

- VMOT/TMC domain: 24V input
- Servo rail: 6.6V buck output
- Logic rail: 3.3V
- Ground: common ground plane/star return near power entry
