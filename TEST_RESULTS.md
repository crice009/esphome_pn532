# PN532 Enhanced Component - Test Results (2026-03-01)

## Hardware Setup
- **MCU:** ESP32-C6 (DevKitC-1)
- **Framework:** ESP-IDF
- **I2C Reader (ACM1):** SDA=21, SCL=22, Address=0x24 (400kHz)
- **SPI Reader (ACM1):** CLK=18, MISO=20, MOSI=23, CS=5

## Results Summary

| Test Category | Status | Notes |
|---|---|---|
| **Mixed Boot** | PASS | Dual-bus initialization stable after cold boot. |
| **Bus Recovery** | PASS | Both I2C and SPI recover gracefully from wire interruptions. |
| **Tag Detection** | PASS | Reliable detection on both buses at 250ms update intervals. |
| **Anti-Collision** | PASS | Simultaneous detection of 2 tags on I2C reader verified. |
| **Removal Logic** | PASS | Flapping fix (threshold 5) prevents flickering during collisions. |
| **NDEF Operations**| MIXED| SPI: Stable. I2C: Prone to module latch-up during writes. |
| **Stability** | PASS | ESP32-C6 remains responsive even if one reader hangs. |

## Verified Features
- [x] Dual Bus Coexistence
- [x] Multi-Tag Parsing (Max 2)
- [x] Non-blocking Logic (<30ms loop latency during NDEF)
- [x] Removal Flapping Fix (Threshold masking)
- [x] High-Speed I2C (400kHz verified)
- [x] Fast Polling (250ms interval verified)
