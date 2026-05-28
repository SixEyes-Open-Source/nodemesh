# Node0 External SD Module Wiring (SPI)

For a typical SPI microSD module, connect 6 wires:

## Power

- VCC -> 3.3V (preferred for ESP32-S3 logic-level safety)
- GND -> GND

## SPI Signals

- SCK -> GPIO40
- MISO -> GPIO41
- MOSI -> GPIO42
- CS -> GPIO2

Pin compatibility notes:

- GPIO40/41/42 are dedicated here to SD SPI and are not shared with follower motor/servo/UART mappings.
- GPIO2 as CS is acceptable for this plan; avoid using boot-strapping sensitive pins for SD CS changes.
- If your specific ESP32-S3 module variant reserves any of these pins, remap only in firmware and schematic together.

## Common Module Header Labels

Most breakout boards label the same signals as:

- `CLK` = SCK
- `DO` or `MISO` = MISO (card to ESP32)
- `DI` or `MOSI` = MOSI (ESP32 to card)
- `CS` or `SS` = chip select
- `VCC`, `GND` = power

## Pins Often Present but Not Needed in SPI Mode

- `CD/DAT3` is internally used by SPI and does not need separate routing.
- `DAT1` and `DAT2` are not used in SPI mode.
- `CARD_DETECT` is optional if your socket exposes it.

## Notes

- Keep SD wires short and route over solid ground reference.
- Add local decoupling close to the SD module (100nF + 10uF).
- If your SD breakout expects 5V VCC and includes level-shifters, verify MISO high-level compatibility before connecting to the S3.
- If GPIO40/41/42 conflict with your board variant, re-map in `node0_orchestrator_s3/include/board_pins.h`.

## Fast PCB Rule-of-Thumb

- Use 3.3V-native SD modules when possible.
- Keep SPI traces short and route SCK away from noisy motor phases.
- Put a local 47uF bulk cap plus 100nF decoupler near SD VCC for write-burst stability.
