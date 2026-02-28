# PN532 Enhanced Component - Test Results (2026-02-28)

## Hardware Setup
- **MCU:** ESP32-C6 (DevKitC-1)
- **Framework:** ESP-IDF
- **I2C Reader (Hub 1):** SDA=21, SCL=22, Address=0x24 (400kHz)
- **SPI Reader (Hub 2):** CLK=18, MISO=20, MOSI=23, CS=5 (1MHz)
- **Shared Feedback:** Relay LED on GPIO 2

## Phase 1: Communication & Stability
| Test Case | Result | Notes |
|---|---|---|
| **Mixed Boot** | **PASS** | Both I2C and SPI hubs initialized successfully. |
| **Health Check** | **PASS** | Periodic version checks (10s interval) logged correctly on both buses. |
| **Exponential Backoff** | **PASS** | Interval correctly throttled during simulated bus disconnection. |
| **Skip-if-Busy Logic**| **PASS** | 500ms interval remained stable during long-running 3.5s NTAG write operations. |

## Phase 2: Card Detection & Logic
| Test Case | Result | Notes |
|---|---|---|
| **Multi-Tag Detection** | **PASS** | Correctly identified and parsed 2 tags simultaneously in one poll cycle. |
| **Removal Persistence** | **PASS** | 3-cycle threshold prevented LED/Binary Sensor flapping during busy states. |
| **Snappy Detection** | **PASS** | <500ms detection latency confirmed when tags entered the field. |
| **State Consistency** | **PASS** | Binary sensors correctly synchronized with removal persistence triggers. |

## Phase 3: NDEF Operations
| Test Case | Result | Notes |
|---|---|---|
| **NDEF Read (I2C)** | **PASS** | Successfully read string "tes" from Mifare Classic tag. |
| **NDEF Write (SPI)** | **PASS** | Successfully wrote URI to NTAG216 ring (operation duration: ~3.5s). |
| **Mifare Auth Fallback**| **PASS** | Successfully attempted fallback to DEFAULT_KEY when NDEF_KEY failed. |
| **Mifare Formatting** | **PENDING** | Intermittent auth failures on specific non-standard tags under investigation. |

## Success Criteria Checklist
- [x] SPI Hardware Stability
- [x] Dual Bus Coexistence
- [x] Multi-Tag Parsing (Max 2)
- [x] Non-blocking Logic (<30ms during idle)
- [x] Removal Flapping Fix
- [ ] Mifare Non-Standard Key Support
- [ ] Robust Counterfeit Detection
- [ ] NTAG216 Write Latency Optimization
